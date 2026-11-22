"""M3 变异验证：注入定位器漂移 → 检出失败 → 生成自愈建议 → 恢复后全绿。"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
RESULT_DIR = os.path.join(HERE, "test-results")
HTML_PATH = os.path.abspath(os.path.join(HERE, "..", "..", "web", "index.html"))
BACKUP_FD, BACKUP_PATH = tempfile.mkstemp(prefix="mqproxy-healing-", suffix=".html")
os.close(BACKUP_FD)
OLD_TESTID = 'data-testid="kafka-save"'
NEW_TESTID = 'data-testid="kafka-save-v2"'
TARGET_TEST = "Kafka 配置保存发送正确 payload"


def run(command, cwd, env):
    print("[healval] {}".format(" ".join(command)), flush=True)
    completed = subprocess.run(
        command, cwd=cwd, env=env, text=True, encoding="utf-8", errors="replace",
        shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    safe_output = completed.stdout.encode("gbk", errors="replace").decode("gbk")
    print(safe_output[-2500:], flush=True)
    return completed.returncode, completed.stdout


def main():
    if not os.path.isdir(RESULT_DIR):
        os.makedirs(RESULT_DIR)
    env = os.environ.copy()
    env["PATH"] = (r"C:\Program Files\nodejs;C:\Users\lwstar\AppData\Roaming\npm;"
                   + env.get("PATH", ""))
    with open(HTML_PATH, "r", encoding="utf-8") as handle:
        original = handle.read()
    if original.count(OLD_TESTID) != 1:
        print("[healval] unexpected testid count")
        return 2
    shutil.copyfile(HTML_PATH, BACKUP_PATH)

    checks = {}
    try:
        with open(HTML_PATH, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(original.replace(OLD_TESTID, NEW_TESTID))

        code, output = run(["npm.cmd", "test", "--", "-g", TARGET_TEST], HERE, env)
        checks["mutation_failed_as_expected"] = code != 0 and "kafka-save" in output

        code, _ = run([sys.executable, "analyze_results.py"], HERE, env)
        checks["analyzer_ran"] = code == 0

        code, _ = run([sys.executable, "heal_locators.py"], HERE, env)
        checks["healer_ran"] = code == 0

        proposal_path = os.path.join(RESULT_DIR, "healing-proposal.json")
        proposal = {}
        if os.path.isfile(proposal_path):
            with open(proposal_path, "r", encoding="utf-8") as handle:
                proposal = json.load(handle)
        checks["drift_detected"] = proposal.get("status") == "proposed"
        checks["correct_suggestion"] = any(
            item.get("old") == "kafka-save" and item.get("new") == "kafka-save-v2"
            for item in proposal.get("suggestions", []))
    finally:
        shutil.copyfile(BACKUP_PATH, HTML_PATH)
        os.remove(BACKUP_PATH)

    code, output = run(["npm.cmd", "test"], HERE, env)
    checks["restored_suite_green"] = code == 0 and "8 passed" in output

    for name, passed in checks.items():
        print("[healval] {}: {}".format(name, "PASS" if passed else "FAIL"), flush=True)
    print("[healval] RESULT: {}/{}".format(sum(1 for v in checks.values() if v), len(checks)))
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
