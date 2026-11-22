# 测试报告：Playwright M0 测试骨架

## 1. 测试范围

本次为 AI 驱动测试方案的 M0 基线：为 Web 管理页补充稳定定位锚点，建立可回归的 Playwright smoke 用例，并完成 CodeBuddy 插件环境的 Playwright-MCP 配置验证。

涉及文件：

- `web/index.html`（修改）：为状态面板、Kafka/日志表单与按钮、操作结果区、动态状态徽章补充 `data-testid`。
- `tests/e2e/playwright.config.js`（新增）：静态服务 webServer、Chromium、list reporter、trace on-first-retry。
- `tests/e2e/smoke.spec.js`（新增）：3 条 smoke 用例，使用 `page.route` 打桩 `/api/*`。
- `README.md`（修改）：同步 Playwright smoke 运行命令。
- `.gitignore`（修改）：忽略 `test-results/`、`playwright-report/`、`.playwright-mcp/`。
- 用户级 `~/.codebuddy/mcp.json`（修改，不在项目 diff 中）：注册 Playwright MCP server。

不在范围：真实 Kafka/SQLite 后端的端到端验证（由 `run_dynamic_validation.py` 覆盖）、CI 流水线配置（M2）、AI 生成与自愈（M1/M3）。

## 2. 测试方法

环境：Windows 10 / Node.js v26.13.1 / npm 11.20.0 / `@playwright/test` + `@playwright/mcp` 全局安装 / Playwright CLI 1.65.1 / Chromium 159.0.8154.6 / Python 3.7（静态文件服务）。

命令（可复制重放，PowerShell）：

```powershell
cd c:/github/cpp_works/MQProxy
$env:Path = 'C:\Program Files\nodejs;C:\Users\lwstar\AppData\Roaming\npm;' + $env:Path
$env:NODE_PATH = 'C:\Users\lwstar\AppData\Roaming\npm\node_modules;C:\Users\lwstar\AppData\Roaming\npm\node_modules\@playwright\test\node_modules'
playwright test --config tests/e2e/playwright.config.js

python -m json.tool C:/Users/lwstar/.codebuddy/mcp.json
```

MCP 验证方式：以 stdio JSON-RPC 启动全局 `@playwright/mcp/cli.js`，依次调用 `initialize`、`tools/list`、`browser_navigate`、`browser_snapshot`，打开本地静态服务中的管理页。

## 3. 测试结果

| 检查项 | 结果 | 证据 |
| --- | --- | --- |
| MCP 配置文件 JSON 校验 | PASS | `python -m json.tool` 退出码 0 |
| MCP `initialize` | PASS | `protocolVersion=2025-06-18`，`serverInfo=Playwright 1.66.0-alpha-2026-11-18` |
| MCP `tools/list` | PASS | 返回 25 个工具（含 `browser_navigate`、`browser_snapshot` 等） |
| MCP 页面导航与快照 | PASS | Page Title = `MQProxy 管理控制台`，快照 2237 字节，标题命中 |
| Playwright smoke 1：页面状态 | PASS | 标题、运行中徽章、brokers/groupId 断言通过 |
| Playwright smoke 2：表单填充 | PASS | brokers/topics/groupId/logger level/flush 值断言通过 |
| Playwright smoke 3：重启 POST | PASS | `calls.kafkaRestart=1`，后续状态请求命中 |
| 汇总 | **3/3 PASS** | `3 passed (5.6s)`，退出码 0 |

## 4. 已知问题

- 按用户选择的全局安装方案，依赖未锁定在 `package.json`；运行测试需显式设置 `NODE_PATH`，CI 迁移到 M2 时需改为项目级清单以保证可复现。
- smoke 用例以 `page.route` 打桩 API，不验证真实后端行为；真实业务链路仍由 `run_dynamic_validation.py`（12/12）覆盖。
- CodeBuddy 插件可能不会自动感知外部写入的 `~/.codebuddy/mcp.json`，需在 Settings → MCP 点击 Try to Run 或重启 IDE。
- 静态页面请求 favicon 会产生 console error，不影响用例结论。
