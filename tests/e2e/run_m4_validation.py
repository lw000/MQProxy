"""M4 验证：12 场景全量、分片、flaky 隔离与指标看板。"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
RESULT_DIR = os.path.join(HERE, "test-results")
QUARANTINE_PATH = os.path.join(HERE, "quarantine.json")
DASHBOARD_PATH = os.path.join(RESULT_DIR, "dashboard.html")
QUARANTINED_TEST = "刷新按钮重新拉取状态"


def run(command, env):
    print("[m4val] {}".format(" ".join(command)), flush=True)
    completed = subprocess.run(
        command, cwd=HERE, env=env, text=True, encoding="utf-8", errors="replace",
        shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    safe_output = completed.stdout.encode("gbk", errors="replace").decode("gbk")
    print(safe_output[-1800:], flush=True)
    return completed.returncode, safe_output


def contains_count(output, count):
    return "{} passed".format(count) in output


def main():
    env = os.environ.copy()
    env["PATH"] = (r"C:\Program Files\nodejs;C:\Users\lwstar\AppData\Roaming\npm;"
                   + env.get("PATH", ""))
    fd, backup = tempfile.mkstemp(prefix="mqproxy-quarantine-", suffix=".json")
    os.close(fd)
    shutil.copyfile(QUARANTINE_PATH, backup)
    checks = {}
    try:
        code, output = run(["npm.cmd", "test"], env)
        checks["full_suite_12"] = code == 0 and contains_count(output, 12)

        code, _ = run([sys.executable, "analyze_results.py"], env)
        checks["analyzer_ran"] = code == 0
        code, _ = run([sys.executable, "generate_dashboard.py"], env)
        checks["dashboard_generated"] = code == 0 and os.path.isfile(DASHBOARD_PATH)

        counts = []
        for shard in ("1", "2"):
            code, output = run(["npm.cmd", "test", "--", "--shard={}/2".format(shard)], env)
            matched = re.search(r"(\d+) passed", output)
            counts.append(int(matched.group(1)) if code == 0 and matched else 0)
        # fullyParallel=false 时 Playwright 按文件组分片，实际为 9+3；只要求各片通过且总数=12
        checks["shards_cover_all_12"] = all(count > 0 for count in counts) and sum(counts) == 12

        with open(QUARANTINE_PATH, "w", encoding="utf-8", newline="\n") as handle:
            json.dump({"tests": [{"name": QUARANTINED_TEST, "reason": "M4 验证",
                                  "expires": "2026-12-31"}]}, handle, ensure_ascii=False)
        code, output = run(["npm.cmd", "test"], env)
        checks["main_excludes_quarantined"] = code == 0 and contains_count(output, 11)
        code, output = run(["npm.cmd", "run", "test:quarantine"], env)
        checks["quarantine_runs_only"] = code == 0 and contains_count(output, 1)
    finally:
        shutil.copyfile(backup, QUARANTINE_PATH)
        os.remove(backup)

    code, output = run(["npm.cmd", "test"], env)
    checks["restored_full_suite_12"] = code == 0 and contains_count(output, 12)

    for name, passed in checks.items():
        print("[m4val] {}: {}".format(name, "PASS" if passed else "FAIL"), flush=True)
    print("[m4val] RESULT: {}/{}".format(sum(1 for value in checks.values() if value),
                                         len(checks)))
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
