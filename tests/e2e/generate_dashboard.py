"""把 E2E 分析结果累积为历史指标并生成静态看板。"""
import datetime
import html
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RESULT_DIR = os.path.join(HERE, "test-results")
METRICS_DIR = os.path.join(HERE, "metrics")
ANALYSIS_PATH = os.path.join(RESULT_DIR, "analysis.json")
HISTORY_PATH = os.path.join(METRICS_DIR, "history.jsonl")
DASHBOARD_PATH = os.path.join(RESULT_DIR, "dashboard.html")


def read_analysis():
    if not os.path.isfile(ANALYSIS_PATH):
        return {"status": "missing", "tests": 0, "failures": 0, "errors": 0, "categories": {}}
    with open(ANALYSIS_PATH, "r", encoding="utf-8") as handle:
        return json.load(handle)


def read_history():
    if not os.path.isfile(HISTORY_PATH):
        return []
    history = []
    with open(HISTORY_PATH, "r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if line:
                try:
                    history.append(json.loads(line))
                except ValueError:
                    pass
    return history


def main():
    analysis = read_analysis()
    entry = {
        "time": datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "status": analysis.get("status"),
        "tests": analysis.get("tests", 0),
        "failures": analysis.get("failures", 0),
        "errors": analysis.get("errors", 0),
        "categories": analysis.get("categories", {}),
    }
    if not os.path.isdir(METRICS_DIR):
        os.makedirs(METRICS_DIR)
    with open(HISTORY_PATH, "a", encoding="utf-8", newline="\n") as handle:
        handle.write(json.dumps(entry, ensure_ascii=False) + "\n")

    history = read_history()[-20:]
    total = sum(item["tests"] for item in history)
    failed = sum(item["failures"] + item["errors"] for item in history)
    pass_rate = (100.0 * (total - failed) / total) if total else 0.0

    rows = []
    for item in reversed(history):
        rows.append("<tr><td>{}</td><td>{}</td><td>{}</td><td>{}</td><td>{}</td></tr>".format(
            html.escape(item["time"]), html.escape(str(item["status"])), item["tests"],
            item["failures"], item["errors"]))
    categories = analysis.get("categories", {})
    category_text = ", ".join("{}: {}".format(key, value)
                              for key, value in sorted(categories.items())) or "无"

    page = """<!DOCTYPE html>
<html lang="zh-CN"><head><meta charset="UTF-8"><title>MQProxy E2E 指标看板</title>
<style>body{{font-family:"Segoe UI","Microsoft YaHei",sans-serif;margin:32px;color:#1f2937}}
h1{{font-size:22px}} .kpi{{display:inline-block;background:#eef2f7;padding:14px 20px;margin:0 12px 20px 0;border-radius:8px}}
table{{border-collapse:collapse;min-width:640px}} th,td{{border:1px solid #d1d5db;padding:8px 12px;text-align:left}}
th{{background:#e5e7eb}}</style></head><body>
<h1>MQProxy E2E 指标看板</h1>
<div class="kpi">最近状态：<b>{status}</b></div>
<div class="kpi">近 {runs} 次通过率：<b>{rate:.1f}%</b></div>
<div class="kpi">本次用例数：<b>{tests}</b></div>
<p>本次失败分类：{categories}</p>
<table><tr><th>时间</th><th>状态</th><th>用例</th><th>失败</th><th>错误</th></tr>{rows}</table>
</body></html>""".format(
        status=html.escape(str(analysis.get("status"))), runs=len(history), rate=pass_rate,
        tests=analysis.get("tests", 0), categories=html.escape(category_text),
        rows="".join(rows))

    if not os.path.isdir(RESULT_DIR):
        os.makedirs(RESULT_DIR)
    with open(DASHBOARD_PATH, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(page)
    print("Dashboard generated: {} (runs={}, pass_rate={:.1f}%)".format(
        DASHBOARD_PATH, len(history), pass_rate))
    return 0


if __name__ == "__main__":
    sys.exit(main())
