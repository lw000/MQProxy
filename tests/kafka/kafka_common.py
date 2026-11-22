"""Kafka 本地测试公共模块：配置加载/校验/渲染、进程与端口辅助。

职责：为 kafka_service.py 与 run_e2e.py 提供唯一的配置真源解析与
运维原语；本模块不启动服务、不执行用例。
"""
import datetime
import os
import re
import socket
import subprocess
import time

import tomli

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
GENERATED_DIR = os.path.join(SCRIPT_DIR, "generated")
REPORTS_DIR = os.path.join(SCRIPT_DIR, "reports")

DEFAULTS = {
    "kafka": {"home": "C:/tools/kafka_2.12-3.3.1", "mode": "zookeeper"},
    "server": {
        "broker_host": "127.0.0.1", "broker_port": 9092, "broker_id": 0,
        "log_dir": "C:/tools/kafka-data/kafka-logs", "num_partitions": 1,
        "default_replication_factor": 1, "offsets_topic_replication_factor": 1,
        "log_retention_hours": 168,
    },
    "zookeeper": {"host": "127.0.0.1", "port": 2181, "data_dir": "C:/tools/kafka-data/zookeeper"},
    "kraft": {"controller_port": 9093, "node_id": 1, "cluster_id": ""},
    "topic": {"name": "test-topic-1", "partitions": 1, "replication_factor": 1},
    "consumer": {"group_id": "tester", "auto_offset_reset": "earliest"},
    "test": {
        "message_count": 10, "payload_prefix": "mqproxy-e2e-",
        "produce_timeout_ms": 10000, "consume_timeout_ms": 30000,
    },
}

MODE_ZK = "zookeeper"
MODE_KRAFT = "kraft"


class ConfigError(Exception):
    """配置校验失败。"""


def load_config(path):
    """加载 TOML 配置，填充默认值并校验；返回扁平化后的字典。"""
    if not os.path.isfile(path):
        raise ConfigError("配置文件不存在: {}".format(path))
    with open(path, "rb") as handle:
        raw = tomli.load(handle)
    cfg = {}
    for section, fields in DEFAULTS.items():
        cfg[section] = dict(fields)
        user_section = raw.get(section, {})
        if not isinstance(user_section, dict):
            raise ConfigError("[{}] 必须是表".format(section))
        for key, value in user_section.items():
            if key not in fields:
                raise ConfigError("[{}] 未知字段: {}".format(section, key))
            cfg[section][key] = value
    errors = validate(cfg)
    if errors:
        raise ConfigError("; ".join(errors))
    return cfg


def _is_int(value):
    return isinstance(value, int) and not isinstance(value, bool)


def validate(cfg):
    """返回校验错误列表；空列表表示通过。"""
    errors = []
    kafka_cfg, server_cfg = cfg["kafka"], cfg["server"]
    zk_cfg, kraft_cfg = cfg["zookeeper"], cfg["kraft"]
    topic_cfg, consumer_cfg = cfg["topic"], cfg["consumer"]
    test_cfg = cfg["test"]

    home = kafka_cfg["home"]
    if not os.path.isdir(home):
        errors.append("kafka.home 目录不存在: {}".format(home))
    elif not os.path.isfile(os.path.join(home, "bin", "windows", "kafka-server-start.bat")):
        errors.append("kafka.home 缺少 bin/windows/kafka-server-start.bat")
    if kafka_cfg["mode"] not in (MODE_ZK, MODE_KRAFT):
        errors.append("kafka.mode 仅允许 zookeeper/kraft")

    ports = {
        "broker_port": server_cfg["broker_port"],
        "zookeeper.port": zk_cfg["port"],
        "kraft.controller_port": kraft_cfg["controller_port"],
    }
    for name, port in ports.items():
        if not _is_int(port) or not (1024 <= port <= 65535):
            errors.append("{} 必须是 1024-65535 的整数".format(name))
    if len(set(ports.values())) != len(ports):
        errors.append("broker/zookeeper/controller 端口必须互不相同")
    if not _is_int(server_cfg["broker_id"]) or server_cfg["broker_id"] < 0:
        errors.append("server.broker_id 必须 >= 0")

    install_root = os.path.normcase(os.path.abspath(home))
    for label, path in (("server.log_dir", server_cfg["log_dir"]),
                        ("zookeeper.data_dir", zk_cfg["data_dir"])):
        normalized = os.path.normcase(os.path.abspath(path))
        if normalized.startswith(install_root + os.sep):
            errors.append("{} 不得位于 Kafka 安装目录内".format(label))

    for key in ("default_replication_factor", "offsets_topic_replication_factor"):
        if server_cfg[key] != 1:
            errors.append("server.{} 单机必须为 1".format(key))
    if not _is_int(server_cfg["num_partitions"]) or not (1 <= server_cfg["num_partitions"] <= 1000):
        errors.append("server.num_partitions 必须在 1-1000")
    if not _is_int(server_cfg["log_retention_hours"]) or server_cfg["log_retention_hours"] <= 0:
        errors.append("server.log_retention_hours 必须 > 0")

    if not re.fullmatch(r"[A-Za-z0-9._-]+", str(topic_cfg["name"])):
        errors.append("topic.name 仅允许 [A-Za-z0-9._-]")
    if not _is_int(topic_cfg["partitions"]) or not (1 <= topic_cfg["partitions"] <= 1000):
        errors.append("topic.partitions 必须在 1-1000")
    if topic_cfg["replication_factor"] != 1:
        errors.append("topic.replication_factor 单机必须为 1")
    if not str(consumer_cfg["group_id"]).strip():
        errors.append("consumer.group_id 不得为空")
    if consumer_cfg["auto_offset_reset"] not in ("earliest", "latest", "none"):
        errors.append("consumer.auto_offset_reset 仅允许 earliest/latest/none")
    if not _is_int(test_cfg["message_count"]) or not (1 <= test_cfg["message_count"] <= 10000):
        errors.append("test.message_count 必须在 1-10000")
    for key in ("produce_timeout_ms", "consume_timeout_ms"):
        if not _is_int(test_cfg[key]) or test_cfg[key] <= 0:
            errors.append("test.{} 必须 > 0".format(key))
    return errors


def _to_java_path(path):
    """Java properties 中使用正斜杠路径，避免 Windows 反斜杠转义问题。"""
    return os.path.abspath(path).replace("\\", "/")


def render_properties(cfg, fresh=False):
    """按 cfg 渲染 properties 到 generated/，返回路径字典。

    fresh=True 时在配置的数据根目录之下创建独立的 run-<时间戳> 子目录，
    避免上次异常退出残留的 ZK 临时节点导致 NodeExists 启动失败。
    """
    os.makedirs(GENERATED_DIR, exist_ok=True)
    instance_file = os.path.join(GENERATED_DIR, "instance.txt")
    token = None
    if fresh or not os.path.isfile(instance_file):
        token = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        with open(instance_file, "w", encoding="utf-8") as handle:
            handle.write(token)
    else:
        with open(instance_file, "r", encoding="utf-8") as handle:
            token = handle.read().strip()
    suffix = "run-{}".format(token)
    server_cfg, zk_cfg = cfg["server"], cfg["zookeeper"]
    kraft_cfg = cfg["kafka"]
    host, port = server_cfg["broker_host"], server_cfg["broker_port"]
    common_lines = [
        "num.network.threads=3",
        "num.io.threads=8",
        "socket.send.buffer.bytes=102400",
        "socket.receive.buffer.bytes=102400",
        "socket.request.max.bytes=104857600",
        "num.partitions={}".format(server_cfg["num_partitions"]),
        "offsets.topic.replication.factor={}".format(server_cfg["offsets_topic_replication_factor"]),
        "transaction.state.log.replication.factor={}".format(server_cfg["offsets_topic_replication_factor"]),
        "transaction.state.log.min.isr=1",
        "log.retention.hours={}".format(server_cfg["log_retention_hours"]),
        "group.initial.rebalance.delay.ms=0",
        "log.dirs={}".format(_to_java_path(os.path.join(server_cfg["log_dir"], suffix))),
    ]
    result = {}
    if cfg["kafka"]["mode"] == MODE_KRAFT:
        kraft = cfg["kraft"]
        controller_port = kraft["controller_port"]
        listeners = "PLAINTEXT://{}:{},CONTROLLER://{}:{}".format(
            host, port, host, controller_port)
        lines = [
            "process.roles=broker,controller",
            "node.id={}".format(kraft["node_id"]),
            "controller.quorum.voters={}@{}:{}".format(kraft["node_id"], host, controller_port),
            "listeners={}".format(listeners),
            "inter.broker.listener.name=PLAINTEXT",
            "advertised.listeners=PLAINTEXT://{}:{}".format(host, port),
            "controller.listener.names=CONTROLLER",
            "listener.security.protocol.map=CONTROLLER:PLAINTEXT,PLAINTEXT:PLAINTEXT,"
            "SSL:SSL,SASL_PLAINTEXT:SASL_PLAINTEXT,SASL_SSL:SASL_SSL",
        ] + common_lines
        path = os.path.join(GENERATED_DIR, "kraft-server.properties")
        result["server"] = path
    else:
        lines = [
            "broker.id={}".format(server_cfg["broker_id"]),
            "listeners=PLAINTEXT://{}:{}".format(host, port),
            "advertised.listeners=PLAINTEXT://{}:{}".format(host, port),
            "zookeeper.connect={}:{}".format(zk_cfg["host"], zk_cfg["port"]),
            "zookeeper.connection.timeout.ms=18000",
        ] + common_lines
        path = os.path.join(GENERATED_DIR, "server.properties")
        result["server"] = path
        zk_path = os.path.join(GENERATED_DIR, "zookeeper.properties")
        _write_lines(zk_path, [
            "dataDir={}".format(_to_java_path(os.path.join(zk_cfg["data_dir"], suffix))),
            "clientPort={}".format(zk_cfg["port"]),
            "maxClientCnxns=0",
            "admin.enableServer=false",
        ])
        result["zookeeper"] = zk_path
    _write_lines(result["server"], lines)
    return result


def _write_lines(path, lines):
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")


def bootstrap_server(cfg):
    """返回 host:port 引导串。"""
    server_cfg = cfg["server"]
    return "{}:{}".format(server_cfg["broker_host"], server_cfg["broker_port"])


def bin_dir(cfg):
    """返回 Kafka bin/windows 目录。"""
    return os.path.join(cfg["kafka"]["home"], "bin", "windows")


def port_open(host, port, timeout=0.5):
    """TCP 连通性探测；端口可达返回 True。"""
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def wait_port(host, port, timeout_seconds):
    """在超时前每 0.5 秒探测端口，可达返回 True。"""
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        if port_open(host, port):
            return True
        time.sleep(0.5)
    return False


def wait_port_closed(host, port, timeout_seconds):
    """在超时前等待端口释放；已释放返回 True。"""
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        if not port_open(host, port):
            return True
        time.sleep(0.5)
    return False


def pid_path(name):
    """返回组件 PID 文件路径。"""
    return os.path.join(GENERATED_DIR, "{}.pid".format(name))


def read_pid(name):
    """读取 PID 文件；不存在或内容非法返回 None。"""
    path = pid_path(name)
    if not os.path.isfile(path):
        return None
    with open(path, "r", encoding="utf-8") as handle:
        text = handle.read().strip()
    return int(text) if text.isdigit() else None


def write_pid(name, pid):
    """写入 PID 文件。"""
    os.makedirs(GENERATED_DIR, exist_ok=True)
    with open(pid_path(name), "w", encoding="utf-8") as handle:
        handle.write(str(pid))


def remove_pid(name):
    """删除 PID 文件（幂等）。"""
    if os.path.isfile(pid_path(name)):
        os.remove(pid_path(name))


def process_alive(pid):
    """通过 tasklist 判断 PID 对应进程是否存在。"""
    if pid is None:
        return False
    try:
        output = subprocess.check_output(
            ["tasklist", "/FI", "PID eq {}".format(pid), "/FO", "CSV", "/NH"],
            stderr=subprocess.STDOUT,
        ).decode("utf-8", errors="replace")
    except (OSError, subprocess.CalledProcessError):
        return False
    return '"{}"'.format(pid) in output


def start_java(cfg, main_class, properties_path, log_path):
    """直接用 java.exe 启动 Kafka/ZK（绕过 .bat），返回 PID。

    .bat 经 cmd.exe 托管时分离启动不稳定且日志常被截断；直接调用 java
    可确保进程树单一、输出可靠重定向。
    """
    os.makedirs(os.path.dirname(log_path), exist_ok=True)
    home = cfg["kafka"]["home"]
    classpath = os.path.join(home, "libs", "*")
    log4j = os.path.join(home, "config", "log4j.properties")
    command = [
        "java", "-Xmx1G", "-Xms1G",
        "-Dlog4j.configuration=file:{}".format(log4j),
        "-cp", classpath, main_class, properties_path,
    ]
    handle = open(log_path, "ab")
    creationflags = 0
    if os.name == "nt":
        creationflags = subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.DETACHED_PROCESS
    process = subprocess.Popen(
        command, cwd=home, stdin=subprocess.DEVNULL,
        stdout=handle, stderr=subprocess.STDOUT,
        creationflags=creationflags, close_fds=True,
    )
    handle.close()
    return process.pid


def terminate_tree(pid):
    """终止进程及其子进程树；成功返回 True。"""
    try:
        result = subprocess.run(
            ["taskkill", "/PID", str(pid), "/T", "/F"],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        )
        return result.returncode == 0
    except OSError:
        return False


def run_tool(cfg, bat_name, args, timeout_seconds=30):
    """同步执行 Kafka bin/windows 下的工具脚本，返回 (returncode, output)。"""
    command = os.path.join(bin_dir(cfg), bat_name)
    if not os.path.isfile(command):
        raise RuntimeError("工具脚本不存在: {}".format(command))
    result = subprocess.run(
        [command] + [str(arg) for arg in args],
        cwd=bin_dir(cfg),
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout_seconds,
    )
    return result.returncode, result.stdout.decode("utf-8", errors="replace")


def run_tool_ex(cfg, bat_name, args, input_text=None, timeout_seconds=60):
    """执行 Kafka 工具，返回 (returncode, stdout, stderr)；可传入 stdin 文本。"""
    command = os.path.join(bin_dir(cfg), bat_name)
    if not os.path.isfile(command):
        raise RuntimeError("工具脚本不存在: {}".format(command))
    if input_text is None:
        result = subprocess.run(
            [command] + [str(arg) for arg in args],
            cwd=bin_dir(cfg), stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            timeout=timeout_seconds,
        )
    else:
        result = subprocess.run(
            [command] + [str(arg) for arg in args],
            cwd=bin_dir(cfg), input=input_text.encode("utf-8"),
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            timeout=timeout_seconds,
        )
    return (
        result.returncode,
        result.stdout.decode("utf-8", errors="replace"),
        result.stderr.decode("utf-8", errors="replace"),
    )


def run_command(command, args, timeout_seconds=60):
    """同步执行任意命令，返回 (returncode, output)。"""
    result = subprocess.run(
        [command] + [str(arg) for arg in args],
        cwd=os.path.dirname(command),
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout_seconds,
    )
    return result.returncode, result.stdout.decode("utf-8", errors="replace")
