"""Kafka 单机服务管理：start / stop / status / restart。

用法：
    python kafka_service.py start   [--config kafka-local.toml]
    python kafka_service.py stop    [--config kafka-local.toml]
    python kafka_service.py status  [--config kafka-local.toml]
    python kafka_service.py restart [--config kafka-local.toml]

成功判定：broker 端口 TCP 连通；zookeeper 模式额外要求 ZK 端口连通。
停止顺序：broker -> zookeeper；仅终止本脚本 PID 文件记录的进程树，
不使用 Kafka 自带 wmic 停止脚本（其会按命令行模糊匹配杀掉所有 Kafka 进程）。
"""
import argparse
import os
import subprocess
import sys
import time
import uuid

from kafka_common import (
    GENERATED_DIR, MODE_KRAFT, MODE_ZK, ConfigError, bin_dir, bootstrap_server,
    load_config, port_open, process_alive, read_pid, remove_pid,
    render_properties, run_command, start_java, terminate_tree, wait_port,
    wait_port_closed, write_pid,
)

DEFAULT_CONFIG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "kafka-local.toml")
START_TIMEOUT_SECONDS = 60
STOP_TIMEOUT_SECONDS = 15
CLUSTER_ID_FILE = os.path.join(GENERATED_DIR, "cluster_id.txt")


def _log(message):
    print("[kafka-service] {}".format(message), flush=True)


def _ensure_cluster_id(cfg, server_properties):
    """KRaft 模式确保 storage 已按 cluster_id 格式化；返回 (cluster_id, error)。"""
    os.makedirs(GENERATED_DIR, exist_ok=True)
    configured = str(cfg["kraft"]["cluster_id"] or "").strip()
    if configured:
        cluster_id = configured
    elif os.path.isfile(CLUSTER_ID_FILE):
        with open(CLUSTER_ID_FILE, "r", encoding="utf-8") as handle:
            cluster_id = handle.read().strip()
    else:
        cluster_id = str(uuid.uuid4())
        with open(CLUSTER_ID_FILE, "w", encoding="utf-8") as handle:
            handle.write(cluster_id)
    marker = os.path.join(GENERATED_DIR, "kraft-formatted.marker")
    if os.path.isfile(marker):
        return cluster_id, None
    command = os.path.join(bin_dir(cfg), "kafka-storage.bat")
    try:
        returncode, output = run_command(
            command, ["format", "-t", cluster_id, "-c", server_properties])
        if returncode != 0:
            return cluster_id, "kafka-storage format 退出码 {}: {}".format(
                returncode, output[-500:])
        with open(marker, "w", encoding="utf-8") as handle:
            handle.write(cluster_id)
        return cluster_id, None
    except (OSError, subprocess.TimeoutExpired) as error:
        return cluster_id, "kafka-storage format 执行失败: {}".format(error)


def start(cfg):
    """启动服务；返回进程退出码 0=成功，1=失败，2=配置错误，3=端口占用。"""
    server_cfg = cfg["server"]
    host, port = server_cfg["broker_host"], server_cfg["broker_port"]
    zk_host, zk_port = cfg["zookeeper"]["host"], cfg["zookeeper"]["port"]
    if port_open(host, port):
        _log("broker 端口 {} 已被占用，拒绝启动".format(port))
        return 3
    if cfg["kafka"]["mode"] == MODE_ZK and port_open(zk_host, zk_port):
        _log("zookeeper 端口 {} 已被占用，拒绝启动".format(zk_port))
        return 3

    props = render_properties(cfg, fresh=True)
    started = []
    try:
        if cfg["kafka"]["mode"] == MODE_KRAFT:
            cluster_id, error = _ensure_cluster_id(cfg, props["server"])
            if error:
                _log(error)
                return 1
            _log("KRaft cluster_id={}".format(cluster_id))
        else:
            zk_pid = start_java(
                cfg, "org.apache.zookeeper.server.quorum.QuorumPeerMain",
                props["zookeeper"], os.path.join(GENERATED_DIR, "zookeeper.log"))
            write_pid("zookeeper", zk_pid)
            started.append("zookeeper")
            _log("zookeeper 启动中 pid={}".format(zk_pid))
            if not wait_port(zk_host, zk_port, START_TIMEOUT_SECONDS):
                _log("zookeeper 端口 {} 在 {}s 内未就绪".format(zk_port, START_TIMEOUT_SECONDS))
                return 1
            time.sleep(2)
            if not process_alive(read_pid("zookeeper")):
                _log("zookeeper 就绪后进程退出，请查看 generated/zookeeper.log")
                return 1
            _log("zookeeper 就绪 {}:{}".format(zk_host, zk_port))

        broker_pid = start_java(
            cfg, "kafka.Kafka", props["server"],
            os.path.join(GENERATED_DIR, "broker.log"))
        write_pid("broker", broker_pid)
        started.append("broker")
        _log("broker 启动中 pid={}".format(broker_pid))
        if not wait_port(host, port, START_TIMEOUT_SECONDS):
            _log("broker 端口 {} 在 {}s 内未就绪，请查看 generated/broker.log".format(
                port, START_TIMEOUT_SECONDS))
            return 1
        _log("broker 就绪，bootstrap.servers={}".format(bootstrap_server(cfg)))
        return 0
    finally:
        # 启动失败时回滚已启动组件，避免残留半启动状态
        # （return 已决定退出码，此处仅做清理）
        if not port_open(host, port):
            for name in reversed(started):
                pid = read_pid(name)
                if process_alive(pid):
                    terminate_tree(pid)
                remove_pid(name)
            _log("启动失败，已清理已启动组件")


def _stop_component(name, host, port):
    """停止单个组件；返回 0=成功或本就停止，1=异常。"""
    pid = read_pid(name)
    if pid is None:
        if port_open(host, port):
            _log("{} 端口 {} 可达但无 PID 记录，拒绝终止未知进程".format(name, port))
            return 1
        _log("{} 未运行".format(name))
        return 0
    if not process_alive(pid):
        remove_pid(name)
        _log("{} PID {} 已不存在，清理记录".format(name, pid))
        return 0
    if not terminate_tree(pid):
        _log("{} PID {} 终止失败".format(name, pid))
        return 1
    remove_pid(name)
    if not wait_port_closed(host, port, STOP_TIMEOUT_SECONDS):
        _log("{} 端口 {} 在 {}s 内未释放".format(name, port, STOP_TIMEOUT_SECONDS))
        return 1
    _log("{} 已停止".format(name))
    return 0


def stop(cfg):
    """逆序停止服务（broker -> zookeeper）；返回退出码。"""
    server_cfg = cfg["server"]
    result = _stop_component("broker", server_cfg["broker_host"], server_cfg["broker_port"])
    if cfg["kafka"]["mode"] == MODE_ZK:
        zk_result = _stop_component(
            "zookeeper", cfg["zookeeper"]["host"], cfg["zookeeper"]["port"])
        result = result or zk_result
    return result


def status(cfg):
    """输出各组件状态；0=全部运行，1=部分异常，2=全部停止。"""
    server_cfg = cfg["server"]
    entries = [("broker", server_cfg["broker_host"], server_cfg["broker_port"])]
    if cfg["kafka"]["mode"] == MODE_ZK:
        entries.append(("zookeeper", cfg["zookeeper"]["host"], cfg["zookeeper"]["port"]))
    running = 0
    for name, host, port in entries:
        pid = read_pid(name)
        alive = process_alive(pid)
        reachable = port_open(host, port)
        if alive and reachable:
            state = "RUNNING"
            running += 1
        elif not alive and not reachable:
            state = "STOPPED"
        else:
            state = "ABNORMAL"
        _log("{:<10} state={:<8} pid={:<8} {}:{}".format(
            name, state, pid if pid is not None else "-", host, port))
    return 0 if running == len(entries) else (2 if running == 0 else 1)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Kafka 单机服务管理")
    parser.add_argument("action", choices=("start", "stop", "status", "restart"))
    parser.add_argument("--config", default=DEFAULT_CONFIG, help="TOML 配置文件路径")
    args = parser.parse_args(argv)
    try:
        cfg = load_config(args.config)
    except ConfigError as error:
        _log("配置错误: {}".format(error))
        return 2
    if args.action == "start":
        return start(cfg)
    if args.action == "stop":
        return stop(cfg)
    if args.action == "status":
        return status(cfg)
    stop_result = stop(cfg)
    if stop_result != 0:
        _log("restart: 停止阶段异常，仍尝试启动")
    return start(cfg)


if __name__ == "__main__":
    sys.exit(main())
