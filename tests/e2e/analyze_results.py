"""分析 Playwright JUnit 结果，输出 CI 可消费的 analysis.json。"""
import json
import os
import re
import sys
import xml.etree.ElementTree as ElementTree

HERE = os.path.dirname(os.path.abspath(__file__))
RESULT_DIR = os.path.join(HERE, "test-results")
JUNIT_PATH = os.path.join(RESULT_DIR, "junit.xml")
ANALYSIS_PATH = os.path.join(RESULT_DIR, "analysis.json")


def extract_testids(message):
    return sorted(set(re.findall(r'getByTestId\(["\']([^"\']+)["\']\)', message or "")))


def categorize(message):
    text = (message or "").lower()
    if "500" in text or "internal server error" in text or "5xx" in text:
        return "product_bug"
    if "getbytestid" in text or "locator" in text or "strict mode" in text:
        return "test_locator_drift"
    if "timeout" in text or "exceeded" in text or "timed out" in text:
        return "flaky_or_slow"
    if "econnrefused" in text or "err_network" in text or "browser" in text:
        return "environment"
    if "expect" in text or "expected" in text or "tohavevalue" in text:
        return "test_assertion"
    return "unknown"


def main():
    analysis = {
        "status": "missing", "tests": 0, "failures": 0, "errors": 0,
        "skipped": 0, "categories": {}, "details": [],
    }
    if os.path.isfile(JUNIT_PATH):
        root = ElementTree.parse(JUNIT_PATH).getroot()
        suites = root.findall(".//testsuite")
        if not suites and root.tag == "testsuite":
            suites = [root]
        for suite in suites:
            analysis["tests"] += int(suite.get("tests", "0"))
            analysis["failures"] += int(suite.get("failures", "0"))
            analysis["errors"] += int(suite.get("errors", "0"))
            analysis["skipped"] += int(suite.get("skipped", "0"))
        for case in root.iter("testcase"):
            for node in case:
                if node.tag in ("failure", "error"):
                    message = ((node.get("message") or "") + "\n" +
                               (node.text or "")).strip()[:2000]
                    category = categorize(message)
                    analysis["categories"][category] = analysis["categories"].get(category, 0) + 1
                    analysis["details"].append({
                        "name": case.get("name", ""),
                        "category": category,
                        "classification": category,
                        "message": message,
                        "testids": extract_testids(message),
                    })
        analysis["status"] = "pass" if (
            analysis["failures"] == 0 and analysis["errors"] == 0) else "fail"

    if not os.path.isdir(RESULT_DIR):
        os.makedirs(RESULT_DIR)
    with open(ANALYSIS_PATH, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(analysis, handle, ensure_ascii=False, indent=2)

    print("E2E analysis: {status}, tests={tests}, failures={failures}, errors={errors}".format(
        **analysis))
    if analysis["categories"]:
        print("Categories: {}".format(json.dumps(analysis["categories"], ensure_ascii=False)))
    for detail in analysis["details"]:
        print("  - [{category}] {name}".format(**detail))
    return 0


if __name__ == "__main__":
    sys.exit(main())
