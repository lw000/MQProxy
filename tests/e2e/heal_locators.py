"""根据失败分析生成定位器自愈建议（仅产出 patch/PR 描述，不修改源码）。"""
import difflib
import glob
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RESULT_DIR = os.path.join(HERE, "test-results")
ANALYSIS_PATH = os.path.join(RESULT_DIR, "analysis.json")
HTML_PATH = os.path.abspath(os.path.join(HERE, "..", "..", "web", "index.html"))
PROPOSAL_PATH = os.path.join(RESULT_DIR, "healing-proposal.json")
PATCH_PATH = os.path.join(RESULT_DIR, "healing-proposal.patch")
PR_BODY_PATH = os.path.join(RESULT_DIR, "healing-pr-body.md")

TESTID_PATTERN = re.compile(r'data-testid=["\']([^"\']+)["\']')
LOCATOR_PATTERN = re.compile(r'getByTestId\(([\'"])([^\'"]+)\1\)')
MIN_CONFIDENCE = 0.5


def read_json(path, default):
    if not os.path.isfile(path):
        return default
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def rewrite_spec(text, mapping):
    def replace(match):
        quote = match.group(1)
        old = match.group(2)
        return "getByTestId({}{}{})".format(quote, mapping.get(old, old), quote)
    return LOCATOR_PATTERN.sub(replace, text)


def main():
    analysis = read_json(ANALYSIS_PATH, {"details": [], "status": "missing"})
    failures = [item for item in analysis.get("details", [])
                if item.get("category") == "test_locator_drift"]
    with open(HTML_PATH, "r", encoding="utf-8") as handle:
        html = handle.read()
    available = sorted(set(TESTID_PATTERN.findall(html)))

    suggestions = []
    seen = set()
    for item in failures:
        for old in item.get("testids", []):
            if old in seen or old in available:
                continue
            seen.add(old)
            best = None
            best_ratio = 0.0
            for candidate in available:
                ratio = difflib.SequenceMatcher(None, old, candidate).ratio()
                if ratio > best_ratio:
                    best, best_ratio = candidate, ratio
            suggestions.append({
                "old": old,
                "new": best if best_ratio >= MIN_CONFIDENCE else None,
                "confidence": round(best_ratio, 3),
                "evidence": item.get("message", "")[:200],
                "test": item.get("name", ""),
            })

    mapping = {item["old"]: item["new"] for item in suggestions if item["new"]}
    patch_parts = []
    touched = []
    for spec_path in sorted(glob.glob(os.path.join(HERE, "*.spec.js"))):
        with open(spec_path, "r", encoding="utf-8") as handle:
            original = handle.read()
        updated = rewrite_spec(original, mapping)
        if updated != original:
            name = os.path.basename(spec_path)
            touched.append(name)
            patch_parts.extend(difflib.unified_diff(
                original.splitlines(True), updated.splitlines(True),
                fromfile=name, tofile=name))

    status = "proposed" if touched else (
        "no_locator_drift" if not failures else "no_candidate")
    proposal = {
        "status": status,
        "analysis_status": analysis.get("status"),
        "suggestions": suggestions,
        "files": touched,
        "available_testids": available,
    }
    if not os.path.isdir(RESULT_DIR):
        os.makedirs(RESULT_DIR)
    with open(PROPOSAL_PATH, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(proposal, handle, ensure_ascii=False, indent=2)
    with open(PATCH_PATH, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("".join(patch_parts))

    lines = [
        "# 定位器自愈建议（需人工审核，禁止自动合并）", "",
        "状态：{}".format(status), "",
    ]
    for item in suggestions:
        lines.append("- 用例 `{}`：建议 `{}` → `{}`（置信度 {}）".format(
            item["test"], item["old"], item["new"] or "无匹配", item["confidence"]))
    lines += ["", "产物：", "- `test-results/healing-proposal.json`",
              "- `test-results/healing-proposal.patch`", ""]
    with open(PR_BODY_PATH, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))

    print("Healing proposal: {}, suggestions={}, files={}".format(
        status, len(suggestions), touched))
    return 0


if __name__ == "__main__":
    sys.exit(main())
