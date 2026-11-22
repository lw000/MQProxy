"""MQProxy 动态配置与 Web 管理业务验证。

用法：
    python run_dynamic_validation.py

覆盖：配置读写与持久化、Web 管理页与 REST API、Kafka 启停重启、
日志等级与刷新间隔动态调整（保存后手动重启生效）。

前置：build-verify/bin/Debug/MQProxy.exe 已构建；kafka-local.toml 指向本地 Kafka。
"""
import ctypes
import datetime
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.request

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
sys.path.insert(0, SCRIPT_DIR)

import kafka_common
import kafka_service

EXE_PATH = os.path.join(PROJECT_ROOT, "build-verify", "bin", "Debug", "MQProxy.exe")
GENERATED_DIR = os.path.join(SCRIPT_DIR, "generated")
REPORTS_DIR = os.path.join(SCRIPT_DIR, "reports")
CONFIG_PATH = os.path.join(GENERATED_DIR, "dynamic-config.toml")
DB_PATH = os.path.join(GENERATED_DIR, "dynamic-config.db")
LOG_DIR = os.path.join(GENERATED_DIR, "dynamic-logs")
WEB_ROOT = os.path.join(PROJECT_ROOT, "web")
PORT = 18080
BASE_URL = "http://127.0.0.1:{}".format(PORT)
CTRL_BREAK_EVENT = 1


class CaseResult(object):
    def __init__(self, case_id, title, passed, detail):
        self.case_id = case_id
        self.title = title
        self.passed = passed
        self.detail = detail


def log(message):
    print("[dynval] {}".format(message), flush=True)


def posix_path(path):
    return path.replace("\\", "/")


def build_config(kafka_cfg):
    """生成验证专用启动配置；动态配置首次由该 TOML 播种进 SQLite。"""
    os.makedirs(GENERATED_DIR, exist_ok=True)
    if os.path.isfile(DB_PATH):
        os.remove(DB_PATH)
    if os.path.isdir(LOG_DIR):
        for name in os.listdir(LOG_DIR):
            try:
                os.remove(os.path.join(LOG_DIR, name))
            except OSError:
                pass

    broker = kafka_common.bootstrap_server(kafka_cfg)
    topic = kafka_cfg["topic"]["name"]
    content = """serviceName="MQProxyDynamicValidation"
mode="consumer"
enableProd=false
showContent=true

[SERVER]
host="127.0.0.1"
port={port}
webroot="{webroot}"

[DATABASE]
path="{dbpath}"

[LOGGER]
level=1
flushIntervalMs=1000
outdir="{outdir}"

[DEV]
brokers="{broker}"
topics=["{topic}"]
groupId="dynamic-orig"
saslUsername=""
saslPassword=""
sslCaLocation=""
sslKeyLocation=""
sslCertificateLocation=""
securityProtocol=""
saslMechanism=""
sslEndpointIdentificationAlgorithm=""

[PROD]
brokers="{broker}"
topics=["{topic}"]
groupId="dynamic-prod"
saslUsername=""
saslPassword=""
sslCaLocation=""
sslKeyLocation=""
sslCertificateLocation=""
securityProtocol=""
saslMechanism=""
sslEndpointIdentificationAlgorithm=""
""".format(port=PORT, webroot=posix_path(WEB_ROOT), dbpath=posix_path(DB_PATH),
           outdir=posix_path(LOG_DIR), broker=broker, topic=topic)
    with open(CONFIG_PATH, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(content)
    return broker, topic


def api(path, payload=None):
    """返回 (http_status, json_body)；payload 为 None 时发 GET。"""
    body = None if payload is None else json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(BASE_URL + path, data=body,
                                     method="GET" if body is None else "POST")
    if body is not None:
        request.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(request, timeout=10) as response:
        return response.status, json.loads(response.read().decode("utf-8"))


def post_api(path):
    """无参数 POST 也发送 JSON 空对象，确保携带 Content-Length。"""
    return api(path, {})


def page_title():
    with urllib.request.urlopen(BASE_URL + "/", timeout=10) as response:
        return response.status, response.read().decode("utf-8", "replace")


def wait_ready(timeout_seconds=30):
    deadline = time.time() + timeout_seconds
    last_error = None
    while time.time() < deadline:
        try:
            status, body = api("/api/status")
            if status == 200 and body.get("code") == 0:
                return True, ""
            last_error = "status={} body={}".format(status, body)
        except Exception as error:
            last_error = str(error)
        time.sleep(0.4)
    return False, last_error or "timeout"


def start_app():
    handle = open(os.path.join(GENERATED_DIR, "dynamic-stdout.log"), "ab")
    process = subprocess.Popen(
        [EXE_PATH, CONFIG_PATH],
        cwd=PROJECT_ROOT,
        stdin=subprocess.DEVNULL,
        stdout=handle,
        stderr=subprocess.STDOUT,
        creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
    )
    handle.close()
    ready, reason = wait_ready()
    if not ready:
        process.kill()
        process.wait()
        raise RuntimeError("MQProxy 未就绪: {}".format(reason))
    return process


def stop_app(process):
    if process.poll() is not None:
        return process.returncode
    try:
        kernel32 = ctypes.windll.kernel32
        kernel32.GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, process.pid)
    except (AttributeError, OSError):
        process.terminate()
    try:
        process.wait(timeout=15)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
    return process.returncode


def read_logs():
    if not os.path.isdir(LOG_DIR):
        return ""
    content = []
    for name in sorted(os.listdir(LOG_DIR)):
        path = os.path.join(LOG_DIR, name)
        if not os.path.isfile(path):
            continue
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as handle:
                content.append(handle.read())
        except OSError:
            pass
    return "\n".join(content)


def wait_until(predicate, timeout_seconds=15):
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        if predicate():
            return True
        time.sleep(0.3)
    return False


def run_cases(kafka_cfg, broker, topic):
    results = []
    process = start_app()
    try:
        status, html = page_title()
        results.append(CaseResult(
            "WEB1", "静态管理页可通过 Web 访问",
            status == 200 and "MQProxy 管理控制台" in html,
            "http={}, 标题命中={}".format(status, "MQProxy 管理控制台" in html)))

        status, body = api("/api/kafka/config")
        seeded_kafka = body.get("data", {})
        results.append(CaseResult(
            "CFG1", "Kafka 动态配置由 SQLite 读取",
            status == 200 and body.get("code") == 0 and
            seeded_kafka.get("brokers") == broker and
            seeded_kafka.get("groupId") == "dynamic-orig",
            "brokers={}, groupId={}".format(seeded_kafka.get("brokers"),
                                            seeded_kafka.get("groupId"))))

        status, body = api("/api/logger/config")
        seeded_logger = body.get("data", {})
        results.append(CaseResult(
            "CFG2", "Logger 动态配置由 SQLite 读取",
            status == 200 and body.get("code") == 0 and
            seeded_logger.get("level") == 1 and
            seeded_logger.get("flushIntervalMs") == 1000,
            "data={}".format(seeded_logger)))

        status, body = api("/api/status")
        initial_status = body.get("data", {})
        results.append(CaseResult(
            "WEB2", "状态接口返回运行中 Kafka 与运行时日志参数",
            status == 200 and initial_status.get("kafka", {}).get("running") is True and
            initial_status.get("logger", {}).get("level") == 1,
            "kafka.running={}, logger={}".format(
                initial_status.get("kafka", {}).get("running"), initial_status.get("logger"))))

        new_group = "dynamic-changed"
        new_topics = [topic, topic + "-extra"]
        payload = {"brokers": broker, "topics": new_topics, "groupId": new_group}
        status, saved = api("/api/kafka/config", payload)
        status_after_save, after_save = api("/api/status")
        results.append(CaseResult(
            "CFG3", "Kafka 配置保存成功且保存后不立即生效",
            status == 200 and saved.get("code") == 0 and
            status_after_save == 200 and
            after_save["data"]["kafka"]["groupId"] == "dynamic-orig",
            "save={}, 运行态groupId={}".format(saved.get("code"),
                after_save["data"]["kafka"]["groupId"])))

        status, restarted = post_api("/api/kafka/restart")
        status_new, new_state = api("/api/status")
        kafka_data = new_state.get("data", {}).get("kafka", {})
        results.append(CaseResult(
            "KAFKA1", "Kafka 重启后加载最新配置并恢复运行",
            status == 200 and restarted.get("code") == 0 and
            kafka_data.get("running") is True and
            kafka_data.get("groupId") == new_group and
            sorted(kafka_data.get("topics", [])) == sorted(new_topics) and
            "Kafka consumer initialized, brokers: {}, groupId: {}".format(broker, new_group)
            in read_logs(),
            "running={}, groupId={}, topics={}".format(
                kafka_data.get("running"), kafka_data.get("groupId"), kafka_data.get("topics"))))

        status, stopped = post_api("/api/kafka/stop")
        status_stopped, stop_state = api("/api/status")
        status, started = post_api("/api/kafka/start")
        time.sleep(1.0)
        status_started, start_state = api("/api/status")
        results.append(CaseResult(
            "KAFKA2", "Kafka 可通过 Web 停止与启动",
            stopped.get("code") == 0 and started.get("code") == 0 and
            stop_state["data"]["kafka"]["running"] is False and
            start_state["data"]["kafka"]["running"] is True,
            "stopped={}, started={}".format(stop_state["data"]["kafka"]["running"],
                                            start_state["data"]["kafka"]["running"])))

        tokens = ["dynval-" + datetime.datetime.now().strftime("%H%M%S") + "-" + str(i)
                  for i in range(3)]
        returncode, stdout, stderr = kafka_common.run_tool_ex(
            kafka_cfg, "kafka-console-producer.bat",
            ["--broker-list", broker, "--topic", topic],
            input_text="\n".join(tokens) + "\n")
        consumed = wait_until(lambda: all(token in read_logs() for token in tokens), 20)
        results.append(CaseResult(
            "KAFKA3", "重启后的消费者仍可消费消息",
            returncode == 0 and consumed,
            "producer_exit={}, tokens_hit={}, stderr={}".format(
                returncode, consumed, stderr[-120:])))

        status, bad_kafka = api("/api/kafka/config", {"brokers": "", "groupId": "x", "topics": []})
        status, bad_logger = api("/api/logger/config", {"level": 99, "flushIntervalMs": 1000})
        results.append(CaseResult(
            "CFG4", "非法配置被拒绝且不落库",
            bad_kafka.get("code") == 1001 and bad_logger.get("code") == 1001,
            "kafka={}, logger={}".format(bad_kafka.get("code"), bad_logger.get("code"))))

        status, logger_saved = api("/api/logger/config",
                                   {"level": 4, "flushIntervalMs": 2500})
        status_before_apply, before_apply = api("/api/status")
        status, logger_applied = post_api("/api/logger/restart")
        status_after_apply, after_apply = api("/api/status")
        results.append(CaseResult(
            "LOG1", "日志等级与刷新间隔保存后手动重启生效",
            logger_saved.get("code") == 0 and logger_applied.get("code") == 0 and
            before_apply["data"]["logger"] == {"level": 1, "flushIntervalMs": 1000} and
            after_apply["data"]["logger"] == {"level": 4, "flushIntervalMs": 2500} and
            "Logger runtime config applied" in read_logs(),
            "before={}, after={}".format(before_apply["data"]["logger"],
                                         after_apply["data"]["logger"])))

        exit_code = stop_app(process)
        process = None
        # 此时日志等级已调为 error，INFO 级收尾日志按设计被过滤，故以退出码判定
        results.append(CaseResult(
            "EXIT1", "Web 控制停止后进程优雅退出",
            exit_code == 0,
            "exit={}（CTRL_BREAK 优雅退出，error 级下 INFO 收尾日志被过滤）".format(exit_code)))

        process = start_app()
        status, persisted_kafka = api("/api/kafka/config")
        status, persisted_state = api("/api/status")
        pk = persisted_kafka.get("data", {})
        runtime_logger = persisted_state.get("data", {}).get("logger", {})
        results.append(CaseResult(
            "PERSIST", "动态配置跨进程持久化（SQLite）",
            pk.get("groupId") == new_group and
            sorted(pk.get("topics", [])) == sorted(new_topics) and
            runtime_logger == {"level": 4, "flushIntervalMs": 2500},
            "groupId={}, topics={}, logger={}".format(pk.get("groupId"), pk.get("topics"),
                                                      runtime_logger)))
        return results
    finally:
        if process is not None:
            stop_app(process)


def write_report(results):
    os.makedirs(REPORTS_DIR, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    path = os.path.join(REPORTS_DIR, "dynval-{}.md".format(stamp))
    passed = sum(1 for item in results if item.passed)
    lines = [
        "# MQProxy 动态配置与 Web 管理验证报告", "",
        "## 1. 测试范围", "",
        "配置读写与持久化、Web 页与 REST API、Kafka 启停重启与消费、"
        "日志等级/刷新间隔动态调整。", "",
        "## 2. 测试方法", "",
        "```", "cd c:/github/cpp_works/MQProxy/tests/kafka",
        "python -m py_compile run_dynamic_validation.py",
        "python run_dynamic_validation.py", "```", "",
        "## 3. 测试结果", "",
        "| 用例 | 名称 | 结果 | 证据 |", "| --- | --- | --- | --- |",
    ]
    for item in results:
        lines.append("| {} | {} | {} | {} |".format(
            item.case_id, item.title, "PASS" if item.passed else "FAIL", item.detail))
    lines += ["", "汇总：{}/{} PASS".format(passed, len(results)), "",
              "## 4. 已知问题", "", "无。", ""]
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))
    return path, passed, len(results)


def main():
    if not os.path.isfile(EXE_PATH):
        log("可执行文件不存在: {}".format(EXE_PATH))
        return 2
    kafka_cfg = kafka_common.load_config(os.path.join(SCRIPT_DIR, "kafka-local.toml"))
    broker, topic = build_config(kafka_cfg)
    started_kafka = kafka_service.start(kafka_cfg) == 0
    if not started_kafka and not kafka_common.port_open(
            kafka_cfg["server"]["broker_host"], kafka_cfg["server"]["broker_port"]):
        log("Kafka 启动失败")
        return 3
    kafka_common.run_tool_ex(
        kafka_cfg, "kafka-topics.bat",
        ["--create", "--if-not-exists", "--topic", topic, "--bootstrap-server", broker,
         "--partitions", "1", "--replication-factor", "1"])
    kafka_common.run_tool_ex(
        kafka_cfg, "kafka-topics.bat",
        ["--create", "--if-not-exists", "--topic", topic + "-extra", "--bootstrap-server", broker,
         "--partitions", "1", "--replication-factor", "1"])
    results = []
    try:
        results = run_cases(kafka_cfg, broker, topic)
    finally:
        kafka_service.stop(kafka_cfg)
    report, passed, total = write_report(results)
    log("结果: {}/{} PASS, 报告: {}".format(passed, total, report))
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
