"""Kafka 单机 E2E 测试编排：校验 -> 启动 -> TC1~TC5 -> 停止 -> 报告。

用法：
    python run_e2e.py [--config kafka-local.toml] [--no-manage]

默认自行管理服务生命周期（start 成功则结束时 stop）；--no-manage 表示
复用已运行的服务且结束时不停服。所有用例结果写入 reports/ 下的 Markdown 报告。
"""
import argparse
import datetime
import os
import re
import subprocess
import sys

from kafka_common import (
    REPORTS_DIR, ConfigError, bootstrap_server, load_config, run_tool_ex,
)
import kafka_service

DEFAULT_CONFIG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "kafka-local.toml")


class CaseResult:
    """单条用例结果。"""

    def __init__(self, case_id, title, passed, detail):
        self.case_id = case_id
        self.title = title
        self.passed = passed
        self.detail = detail


def _log(message):
    print("[e2e] {}".format(message), flush=True)


def _unique_token():
    return datetime.datetime.now().strftime("%Y%m%d%H%M%S")


def _topic_admin(cfg, args, timeout=30):
    return run_tool_ex(cfg, "kafka-topics.bat", args, timeout_seconds=timeout)


def _prepare_topic(cfg):
    """重建 topic，保证分区数/副本数按配置生效且无历史消息干扰。"""
    name = cfg["topic"]["name"]
    bs = bootstrap_server(cfg)
    _log("重建 topic {}".format(name))
    _topic_admin(cfg, ["--bootstrap-server", bs, "--delete", "--if-exists", "--topic", name])
    _topic_admin(cfg, [
        "--bootstrap-server", bs, "--create", "--topic", name,
        "--partitions", cfg["topic"]["partitions"],
        "--replication-factor", cfg["topic"]["replication_factor"],
    ])


def tc1_config_effective(cfg):
    """TC1：topic 分区数与副本数与配置一致。"""
    output = ""
    returncode = 1
    for _ in range(5):
        returncode, stdout, stderr = _topic_admin(cfg, [
            "--bootstrap-server", bootstrap_server(cfg),
            "--describe", "--topic", cfg["topic"]["name"],
        ])
        output = stdout + stderr
        if "PartitionCount:" in output:
            break
        import time
        time.sleep(1)
    partitions_match = re.search(r"PartitionCount:\s*(\d+)", output)
    replicas_match = re.search(r"ReplicationFactor:\s*(\d+)", output)
    expected_p = int(cfg["topic"]["partitions"])
    expected_r = int(cfg["topic"]["replication_factor"])
    passed = (returncode == 0 and partitions_match is not None and replicas_match is not None
              and int(partitions_match.group(1)) == expected_p
              and int(replicas_match.group(1)) == expected_r)
    detail = "returncode={} PartitionCount={} ReplicationFactor={} 期望={}/{}".format(
        returncode,
        partitions_match.group(1) if partitions_match else "N/A",
        replicas_match.group(1) if replicas_match else "N/A",
        expected_p, expected_r)
    return CaseResult("TC1", "配置生效（分区数/副本数）", passed, detail)


def _produce(cfg, lines):
    """通过 console producer 生产一批消息，每行一条。"""
    payload = "\n".join(lines) + "\n"
    return run_tool_ex(
        cfg, "kafka-console-producer.bat",
        ["--bootstrap-server", bootstrap_server(cfg), "--topic", cfg["topic"]["name"]],
        input_text=payload,
        timeout_seconds=max(30, int(cfg["test"]["produce_timeout_ms"]) // 1000 + 20),
    )


def _consume(cfg, group_id, max_messages, from_beginning=True):
    """通过 console consumer 消费指定条数消息。"""
    args = [
        "--bootstrap-server", bootstrap_server(cfg),
        "--topic", cfg["topic"]["name"],
        "--group", group_id,
        "--max-messages", str(max_messages),
        "--timeout-ms", str(cfg["test"]["consume_timeout_ms"]),
        "--property", "print.key=false",
    ]
    if from_beginning:
        args.append("--from-beginning")
    return run_tool_ex(
        cfg, "kafka-console-consumer.bat", args,
        timeout_seconds=max(45, int(cfg["test"]["consume_timeout_ms"]) // 1000 + 20),
    )


def tc2_produce_consume(cfg, token):
    """TC2：N 条消息准确发送与接收，无丢失无乱序重复。"""
    count = int(cfg["test"]["message_count"])
    prefix = str(cfg["test"]["payload_prefix"])
    expected = ["{}{}#{:04d}".format(prefix, token, index) for index in range(count)]
    rc, out, err = _produce(cfg, expected)
    if rc != 0:
        return CaseResult("TC2", "生产/消费完整流程", False,
                          "生产失败 returncode={} err={}".format(rc, err[-300:]))
    group = "{}-tc2-{}".format(cfg["consumer"]["group_id"], token)
    rc, out, err = _consume(cfg, group, count)
    received = [line.strip() for line in out.splitlines()
                if str(cfg["test"]["payload_prefix"]) in line and token in line]
    passed = rc == 0 and received == expected
    detail = "生产={}条 消费={}条 内容一致={} returncode={}".format(
        count, len(received), received == expected, rc)
    if not passed:
        detail += " 前3条实际={}".format(received[:3])
    return CaseResult("TC2", "生产/消费完整流程", passed, detail)


def tc3_consumer_group(cfg, token):
    """TC3：消费者组按配置生效并已提交位点。"""
    group = "{}-tc2-{}".format(cfg["consumer"]["group_id"], token)
    rc, out, err = run_tool_ex(cfg, "kafka-consumer-groups.bat", [
        "--bootstrap-server", bootstrap_server(cfg), "--describe", "--group", group,
    ], timeout_seconds=30)
    output = out + err
    current_offset = None
    for line in output.splitlines():
        if group in line and "CURRENT-OFFSET" not in line:
            parts = line.split()
            if len(parts) >= 4 and parts[3].isdigit():
                current_offset = int(parts[3])
                break
    passed = (rc == 0 and current_offset is not None
              and current_offset >= int(cfg["test"]["message_count"]))
    detail = "group={} returncode={} CURRENT-OFFSET={}".format(
        group, rc, current_offset if current_offset is not None else "N/A")
    return CaseResult("TC3", "消费者组与位点提交", passed, detail)


def tc4_boundary(cfg, token):
    """TC4：空负载、大消息（10KB）、中文特殊字符均可投递。"""
    prefix = str(cfg["test"]["payload_prefix"])
    large = "{}{}#LARGE#{}".format(prefix, token, "x" * 10000)
    special = "{}{}#SPECIAL#中文#@$%^&*".format(prefix, token)
    blank = ""  # 空负载由空行表示
    expected = [large, special, blank]
    rc, out, err = _produce(cfg, expected)
    if rc != 0:
        return CaseResult("TC4", "边界消息", False,
                          "生产失败 returncode={} err={}".format(rc, err[-300:]))
    group = "{}-tc4-{}".format(cfg["consumer"]["group_id"], token)
    total = int(cfg["test"]["message_count"]) + 3
    rc, out, err = _consume(cfg, group, total, from_beginning=True)
    lines = [line.rstrip("\r") for line in out.splitlines()]
    large_ok = large in lines
    special_ok = special in lines
    blank_ok = blank in lines
    passed = rc == 0 and large_ok and special_ok and blank_ok
    detail = "returncode={} 空负载={} 10KB={} 中文特殊字符={}".format(
        rc, blank_ok, large_ok, special_ok)
    return CaseResult("TC4", "边界消息", passed, detail)


def tc5_offset_consistency(cfg, token):
    """TC5：新消费者组可完整重放全部消息，验证数据一致性。"""
    group = "{}-tc5-{}".format(cfg["consumer"]["group_id"], token)
    total = int(cfg["test"]["message_count"]) + 3
    expected = int(cfg["test"]["message_count"]) + 2
    rc, out, err = _consume(cfg, group, total, from_beginning=True)
    received = [line.strip() for line in out.splitlines()
                if token in line and cfg["test"]["payload_prefix"] in line]
    passed = rc == 0 and len(received) == expected and len(set(received)) == expected
    detail = "returncode={} 重放条数={} 无重复={}".format(
        rc, len(received), len(set(received)) == expected)
    return CaseResult("TC5", "位点重放与数据一致性", passed, detail)


def write_report(cfg, results, started_by_us):
    """输出 Markdown 报告，返回报告路径。"""
    os.makedirs(REPORTS_DIR, exist_ok=True)
    name = "e2e-{}.md".format(datetime.datetime.now().strftime("%Y%m%d-%H%M"))
    path = os.path.join(REPORTS_DIR, name)
    passed = sum(1 for item in results if item.passed)
    lines = [
        "# Kafka 单机 E2E 测试报告",
        "",
        "- 生成时间：{}".format(datetime.datetime.now().isoformat(timespec="seconds")),
        "- 配置：{}".format(cfg["kafka"]["mode"]),
        "- bootstrap.servers：{}".format(bootstrap_server(cfg)),
        "- 服务由本脚本管理：{}".format(started_by_us),
        "- 总计：{} / {} 通过".format(passed, len(results)),
        "",
        "| 用例 | 标题 | 结果 | 关键信息 |",
        "| --- | --- | --- | --- |",
    ]
    for item in results:
        lines.append("| {} | {} | {} | {} |".format(
            item.case_id, item.title, "PASS" if item.passed else "FAIL", item.detail))
    lines.append("")
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))
    return path


def main(argv=None):
    parser = argparse.ArgumentParser(description="Kafka 单机 E2E 测试")
    parser.add_argument("--config", default=DEFAULT_CONFIG, help="TOML 配置文件路径")
    parser.add_argument("--no-manage", action="store_true", help="复用已运行服务，结束时不停服")
    args = parser.parse_args(argv)
    try:
        cfg = load_config(args.config)
    except ConfigError as error:
        _log("配置错误: {}".format(error))
        return 2

    token = _unique_token()
    started_by_us = False
    results = []
    try:
        if not args.no_manage:
            code = kafka_service.start(cfg)
            if code == 0:
                started_by_us = True
            elif code != 3:
                _log("服务启动失败，退出码 {}".format(code))
                return 1
            else:
                _log("端口已占用，复用现有服务")
        _prepare_topic(cfg)
        results.append(tc1_config_effective(cfg))
        results.append(tc2_produce_consume(cfg, token))
        results.append(tc3_consumer_group(cfg, token))
        results.append(tc4_boundary(cfg, token))
        results.append(tc5_offset_consistency(cfg, token))
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        results.append(CaseResult("FATAL", "执行异常", False, str(error)))
    finally:
        if started_by_us:
            kafka_service.stop(cfg)

    for item in results:
        _log("{} {} {} - {}".format(item.case_id, item.title,
                                    "PASS" if item.passed else "FAIL", item.detail))
    report = write_report(cfg, results, started_by_us)
    _log("报告: {}".format(report))
    return 0 if results and all(item.passed for item in results) else 1


if __name__ == "__main__":
    sys.exit(main())
