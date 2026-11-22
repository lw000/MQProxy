"""MQProxy 系统业务验证：配置加载 -> 连接 Kafka -> 消费 -> 优雅退出。

用法：
    python run_system_validation.py

前置：build-verify/bin/Debug/MQProxy.exe 已构建，config.toml 已就位。
验证场景：SYS1 启动链路 / SYS2 消费一致性 / SYS3 showContent 开关 /
SYS4 优雅退出 / SYS5 配置异常。
"""
import ctypes
import datetime
import os
import re
import subprocess
import sys
import time

from kafka_common import (
    REPORTS_DIR, ConfigError, load_config, render_properties, run_tool_ex,
    bootstrap_server,
)
import kafka_service

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
EXE_PATH = os.path.join(PROJECT_ROOT, "build-verify", "bin", "Debug", "MQProxy.exe")
CONFIG_PATH = os.path.join(PROJECT_ROOT, "config.toml")
LOG_DIR = os.path.join(PROJECT_ROOT, "logs", "system-validation")
CTRL_BREAK_EVENT = 1


class CaseResult:
    """单场景结果。"""

    def __init__(self, case_id, title, passed, detail):
        self.case_id = case_id
        self.title = title
        self.passed = passed
        self.detail = detail


def _log(message):
    print("[sysval] {}".format(message), flush=True)


def _token():
    return datetime.datetime.now().strftime("%Y%m%d%H%M%S")


def _set_show_content(enabled):
    """改写 config.toml 的 showContent 字段（仅测试期）。"""
    with open(CONFIG_PATH, "r", encoding="utf-8") as handle:
        text = handle.read()
    replacement = "showContent={}".format("true" if enabled else "false")
    text = re.sub(r"showContent\s*=\s*(true|false)", replacement, text, count=1)
    with open(CONFIG_PATH, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)


def _clear_logs():
    """清空验证日志目录；被占用的文件跳过（不致命）。"""
    if not os.path.isdir(LOG_DIR):
        return
    for name in os.listdir(LOG_DIR):
        path = os.path.join(LOG_DIR, name)
        if not os.path.isfile(path):
            continue
        try:
            os.remove(path)
        except OSError:
            pass


def _read_logs():
    """拼接日志目录内所有文本文件内容。"""
    content = ""
    if not os.path.isdir(LOG_DIR):
        return content
    for name in sorted(os.listdir(LOG_DIR)):
        path = os.path.join(LOG_DIR, name)
        if os.path.isfile(path):
            try:
                with open(path, "r", encoding="utf-8", errors="replace") as handle:
                    content += handle.read()
            except OSError:
                pass
    return content


def _start_mqproxy(config_path=CONFIG_PATH):
    """启动 MQProxy，返回 Popen（输出到独立日志）。"""
    os.makedirs(LOG_DIR, exist_ok=True)
    stdout_path = os.path.join(LOG_DIR, "process-stdout.log")
    handle = open(stdout_path, "ab")
    process = subprocess.Popen(
        [EXE_PATH, config_path],
        cwd=PROJECT_ROOT,
        stdin=subprocess.DEVNULL,
        stdout=handle,
        stderr=subprocess.STDOUT,
        creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
    )
    handle.close()
    return process


def _graceful_stop(process, timeout_seconds=15):
    """发送 CTRL_BREAK_EVENT 请求优雅退出，返回退出码。"""
    if process.poll() is not None:
        return process.returncode
    try:
        kernel32 = ctypes.windll.kernel32
        kernel32.GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, process.pid)
    except (AttributeError, OSError):
        process.terminate()
    try:
        process.wait(timeout=timeout_seconds)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
    return process.returncode


def _produce(kafka_cfg, lines):
    """通过 console producer 生产消息。"""
    return run_tool_ex(
        kafka_cfg, "kafka-console-producer.bat",
        ["--bootstrap-server", bootstrap_server(kafka_cfg),
         "--topic", kafka_cfg["topic"]["name"]],
        input_text="\n".join(lines) + "\n", timeout_seconds=60,
    )


def sys1_startup(kafka_cfg):
    """SYS1：MQProxy 启动并完成订阅。"""
    _clear_logs()
    process = _start_mqproxy()
    deadline = time.time() + 30
    log = ""
    while time.time() < deadline:
        time.sleep(1)
        log = _read_logs()
        if "Application is running" in log and "Assigned" in log:
            break
    assigned = "Assigned" in log
    passed = (process.poll() is None and "initialized" in log.lower()
              and "Subscribed to" in log and "Application is running" in log
              and assigned)
    detail = "alive={} initialized={} subscribed={} running={} assigned={}".format(
        process.poll() is None,
        "initialized" in log.lower(),
        "Subscribed to" in log,
        "Application is running" in log,
        assigned)
    return CaseResult("SYS1", "启动与订阅链路", passed, detail), process


def sys2_consume(kafka_cfg, process, token):
    """SYS2：消息消费内容与数量一致。"""
    count = 10
    payloads = ["{}-SYS2-{:04d}".format(token, index) for index in range(count)]
    rc, out, err = _produce(kafka_cfg, payloads)
    if rc != 0:
        return CaseResult("SYS2", "消息消费一致性", False,
                          "生产失败 rc={} err={}".format(rc, err[-200:]))
    deadline = time.time() + 40
    received = []
    while time.time() < deadline:
        time.sleep(1)
        log = _read_logs()
        received = [payload for payload in payloads if payload in log]
        if len(received) == count:
            break
    passed = len(received) == count and received == payloads
    detail = "生产={} 日志命中={} 内容一致={}".format(
        count, len(received), received == payloads)
    return CaseResult("SYS2", "消息消费一致性", passed, detail), payloads


def sys3_content_switch(kafka_cfg, token):
    """SYS3：showContent=false 时不打印 payload 明文。"""
    _set_show_content(False)
    _clear_logs()
    process = _start_mqproxy()
    deadline = time.time() + 30
    while time.time() < deadline and "Application is running" not in _read_logs():
        time.sleep(1)
    payload = "{}-SENSITIVE-PAYLOAD".format(token)
    rc, out, err = _produce(kafka_cfg, [payload])
    if rc != 0:
        process.kill()
        process.wait()
        _set_show_content(True)
        return CaseResult("SYS3", "showContent 开关", False, "生产失败")
    time.sleep(6)
    log = _read_logs()
    passed = ("Payload length:" in log) and (payload not in log)
    detail = "含长度日志={} 泄漏明文={}".format(
        "Payload length:" in log, payload in log)
    return CaseResult("SYS3", "showContent 开关", passed, detail), process


def sys4_graceful_exit(process):
    """SYS4：CTRL_BREAK 后优雅退出。"""
    exit_code = _graceful_stop(process)
    time.sleep(1)
    log = _read_logs()
    passed = (exit_code == 0 and "Application stopped successfully" in log
              and _no_process())
    detail = "exit_code={} 收尾日志={} 无残留进程={}".format(
        exit_code, "Application stopped successfully" in log, _no_process())
    return CaseResult("SYS4", "优雅退出", passed, detail)


def sys5_invalid_config():
    """SYS5：不存在的配置文件导致快速失败。"""
    missing = os.path.join(PROJECT_ROOT, "no-such-config.toml")
    process = subprocess.Popen(
        [EXE_PATH, missing], cwd=PROJECT_ROOT,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )
    output = process.communicate(timeout=20)[0].decode("utf-8", errors="replace")
    passed = (process.returncode == 1 and "Failed to load configuration" in output)
    detail = "exit_code={} 错误信息={}".format(
        process.returncode, "Failed to load configuration" in output)
    return CaseResult("SYS5", "配置异常快速失败", passed, detail)


def _no_process():
    try:
        output = subprocess.check_output(
            ["tasklist", "/FI", "IMAGENAME eq MQProxy.exe", "/FO", "CSV", "/NH"],
            stderr=subprocess.STDOUT).decode("utf-8", errors="replace")
        return "MQProxy.exe" not in output
    except (OSError, subprocess.CalledProcessError):
        return True


def write_report(results):
    """输出 Markdown 报告。"""
    os.makedirs(REPORTS_DIR, exist_ok=True)
    name = "sysval-{}.md".format(datetime.datetime.now().strftime("%Y%m%d-%H%M"))
    path = os.path.join(REPORTS_DIR, name)
    passed = sum(1 for item in results if item.passed)
    lines = [
        "# MQProxy 系统业务验证报告", "",
        "- 时间：{}".format(datetime.datetime.now().isoformat(timespec="seconds")),
        "- 可执行程序：`{}`".format(EXE_PATH),
        "- 总计：{} / {} 通过".format(passed, len(results)), "",
        "| 场景 | 标题 | 结果 | 关键信息 |", "| --- | --- | --- | --- |",
    ]
    for item in results:
        lines.append("| {} | {} | {} | {} |".format(
            item.case_id, item.title, "PASS" if item.passed else "FAIL", item.detail))
    lines.append("")
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))
    return path


def main():
    if not os.path.isfile(EXE_PATH):
        _log("缺少可执行程序: {}".format(EXE_PATH))
        return 2
    try:
        kafka_cfg = load_config(os.path.join(os.path.dirname(__file__), "kafka-local.toml"))
    except ConfigError as error:
        _log("Kafka 配置错误: {}".format(error))
        return 2

    _set_show_content(True)
    results = []
    process = None
    try:
        kafka_code = kafka_service.start(kafka_cfg)
        if kafka_code != 0:
            _log("Kafka 启动失败，退出码 {}".format(kafka_code))
            return 1
        from kafka_common import bootstrap_server
        run_tool_ex(kafka_cfg, "kafka-topics.bat", [
            "--bootstrap-server", bootstrap_server(kafka_cfg), "--create",
            "--if-not-exists", "--topic", kafka_cfg["topic"]["name"],
            "--partitions", kafka_cfg["topic"]["partitions"],
            "--replication-factor", kafka_cfg["topic"]["replication_factor"],
        ], timeout_seconds=30)
        token = _token()

        result, process = sys1_startup(kafka_cfg)
        results.append(result)
        payloads = []
        if result.passed:
            result2, payloads = sys2_consume(kafka_cfg, process, token)
            results.append(result2)
            results.append(sys4_graceful_exit(process))
            process = None
            flushed_log = _read_logs()
            hits = sum(1 for payload in payloads if payload in flushed_log)
            result2.detail += " 退出后flush命中={}/{}".format(hits, len(payloads))
            if hits == len(payloads) and len(payloads) > 0:
                result2.passed = True

        result3 = sys3_content_switch(kafka_cfg, token)
        if isinstance(result3, tuple):
            results.append(result3[0])
            results.append(sys4_graceful_exit(result3[1]))
        else:
            results.append(result3)
        results.append(sys5_invalid_config())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        results.append(CaseResult("FATAL", "执行异常", False, str(error)))
    finally:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait()
        _set_show_content(True)
        kafka_service.stop(kafka_cfg)

    for item in results:
        _log("{} {} {} - {}".format(item.case_id, item.title,
                                    "PASS" if item.passed else "FAIL", item.detail))
    report = write_report(results)
    _log("报告: {}".format(report))
    return 0 if results and all(item.passed for item in results) else 1


if __name__ == "__main__":
    sys.exit(main())
