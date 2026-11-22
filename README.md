# MQProxy - Kafka Consumer + Web 管理控制台

基于 cppkafka、spdlog、SQLiteCpp 与 cpp-httplib 的 Kafka 消费者服务，提供动态配置存储与 Web 管理页面。

## 依赖项

vcpkg 依赖：`cppkafka`、`spdlog`、`nlohmann_json`、`SQLiteCpp`（含 `sqlite3`）。
本地受管依赖（`3d-partys/`，严禁修改）：`tomlplusplus`（TOML 解析）、`cpp-httplib` 0.66.0（Web 服务）。

```bash
cd C:/vcpkg
.\vcpkg install rdkafka:x64-windows spdlog:x64-windows nlohmann-json:x64-windows sqlitecpp:x64-windows
```

CMake 通过 `VCPKG_ROOT`（默认 `C:/vcpkg`）定位工具链与依赖；cpp-httplib 要求 Windows 10 及以上（已设置 `_WIN32_WINNT=0x0A00`）。

## 构建

```bash
cmake -S . -B build-verify -G "Visual Studio 16 2019" -A x64
cmake --build build-verify --config Debug
```

原有 `build.bat` / `build_release.bat` 仍可用。构建产物在 `build/bin/`，运行时所需的 vcpkg DLL 与 `web/` 目录由 POST_BUILD 自动复制。

## 配置模型

配置分为两层：

1. **启动依赖配置** `config.toml`：服务启动必需项，不从 Web 修改。
   - `[SERVER]`：`host`、`port`、`webroot`（管理页静态目录）
   - `[DATABASE]`：`path`（SQLite 动态配置库路径）
   - `[LOGGER]`：`level`、`flushIntervalMs`、`outdir` 仅作首次入库种子
   - `[DEV]` / `[PROD]`：Kafka 连接种子，仅作首次入库种子
2. **动态配置** SQLite：按模块分表，运行期以库内为权威。
   - `logger_config`：`level`、`flush_interval_ms`
   - `kafka_config`：brokers、topics（JSON 数组）、groupId 及 SASL/SSL 参数

首次启动自动建表并用 TOML 种子填充；后续在 Web 页面修改。配置保存仅落库，**需点击对应"重启生效"按钮才应用**：
- Kafka 重启：重建消费者并按最新配置重新订阅；
- 日志重启：立即调整运行等级与定时刷新线程，不重启进程。

`config.example.toml` 为完整模板；`config.toml` 为本地实际配置（已 gitignore）。

## 运行与 Web 管理

```bash
.\build-verify\bin\Debug\MQProxy.exe config.toml
```

启动后访问 `http://127.0.0.1:8080/`（端口由 `[SERVER].port` 决定）。页面提供状态查看、Kafka 配置与启停重启、日志等级与刷新间隔调整。

### 手动调试：先启动 Kafka 服务

直接运行/管理本地 Kafka 的脚本是 `tests/kafka/kafka_service.py`，手动调试 mqproxy 前先把 Kafka 跑起来：

```bat
cd /d tests\kafka
python kafka_service.py start      :: 启动 Zookeeper(2181) + broker(9092)
python kafka_service.py status     :: 确认 broker 为 RUNNING
cd /d ..\..
.\build-verify\bin\Debug\MQProxy.exe config.toml   :: 或在 VS 中调试该 exe
```

- 脚本支持 `start` / `stop` / `status` / `restart`，配置真源为 `tests/kafka/kafka-local.toml`：Kafka 目录 `C:/tools/kafka_2.12-3.3.1`、`zookeeper` 模式、broker `127.0.0.1:9092`、topic `test-topic-1`；脚本依赖 Python `tomli` 包。
- **brokers 必须对齐**：`config.example.toml` 的 `[DEV].brokers` 是 `127.0.0.1:9097`，而脚本监听 `9092`；调试前确认本地 `config.toml` 当前生效环境的 brokers/topic 与之匹配，否则消费者连不上。
- 启动日志在 `tests/kafka/generated/broker.log` 与 `zookeeper.log`；调试结束执行 `python kafka_service.py stop` 停服（逆序 broker → ZK，仅终止 PID 文件记录的进程树）。
- 需要测试消息时可执行 `python run_e2e.py --no-manage`，复用已运行服务且结束时不停服。

REST API（统一响应包络 `{"code":0,"message":"ok","data":{...}}`）：

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/api/status` | Kafka 运行态与运行时日志参数 |
| GET/POST | `/api/kafka/config` | 读取 / 保存 Kafka 动态配置 |
| POST | `/api/kafka/start` | 启动或恢复消费 |
| POST | `/api/kafka/stop` | 停止消费 |
| POST | `/api/kafka/restart` | 按最新配置重建并启动 |
| GET/POST | `/api/logger/config` | 读取 / 保存日志动态配置 |
| POST | `/api/logger/restart` | 应用已保存的日志配置 |

错误码：`0` 成功，`1001` 参数错误，`1002` 存储错误，`1003` 服务状态错误，`9999` 内部错误。

## 项目结构

```
MQProxy/
├── apps/mqproxy/main.cc              入口：装配 + ServiceManager 编排
├── common/                           公共层：服务基类/管理器/启动配置/日志
├── services/
│   ├── config_store/                 SQLiteCpp 动态配置存储（每模块一表）
│   ├── kafka_manager/                Kafka 生命周期与配置重载
│   └── web_server/                   cpp-httplib REST API + 静态页挂载
├── web/index.html                    管理控制台页面
├── tests/kafka/                      Kafka 单机与业务验证脚本
├── docs/reports/                     正式测试报告
└── changelog/                        按日变更日志
```

## 业务验证

```bash
cd tests/kafka
python run_dynamic_validation.py   # 动态配置/Web/Kafka/日志 12 项验证
python run_system_validation.py    # 既有消费链路 6 项回归

# Playwright E2E（tests/e2e 自带依赖清单，无需 NODE_PATH）
cd ../..
npm install --prefix tests/e2e        # 首次或 lock 变更时执行
npm test --prefix tests/e2e           # 12 条 UI E2E，API 打桩、不依赖 Kafka
npm run test:quarantine --prefix tests/e2e  # 仅运行 quarantine.json 中登记的 flaky 用例
python tests/e2e/analyze_results.py   # 解析 JUnit，输出 test-results/analysis.json
python tests/e2e/generate_dashboard.py       # 输出 test-results/dashboard.html 与历史指标
python tests/e2e/run_healing_validation.py  # 变异验证：注入定位器漂移→归因→自愈建议→恢复全绿
python tests/e2e/run_m4_validation.py        # 规模化验证：全量/分片/隔离/看板
```

CI：`.github/workflows/e2e.yml` 在 PR/push 到 `main`、每日定时及手动触发时运行，使用 Windows self-hosted runner，包含 build / e2e（3 分片矩阵）/ system-validation 三类 job，并上传 Playwright 制品与 `analysis.json`。失败时额外运行 `heal_locators.py`，产出 `healing-proposal.patch` 与 PR 描述供人工审核，**不自动合并**。flaky 用例登记在 `tests/e2e/quarantine.json`，从主套件排除并单独运行。

## 特性

- TOML 启动配置 + SQLite 动态配置分层，按模块分表
- Web 管理控制台与 JSON REST API
- Kafka 动态配置、启动/停止/重启（保存后手动生效）
- 日志等级与定时刷新间隔运行期动态调整
- SASL/SSL、多主题订阅、断线自动重连、优雅退出
- 服务生命周期统一由 `ServiceManager` 编排，停止按注册逆序执行
