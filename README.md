# MQProxy - Kafka Consumer

使用librdkafka和spdlog的Kafka消费者项目。

## 依赖项

本项目使用vcpkg管理依赖，需要安装以下库：
- librdkafka
- spdlog
- nlohmann_json

注：tomlplusplus使用本地3d-partys目录下的版本

## 安装vcpkg

如果还未安装vcpkg，请执行以下步骤：

```bash
# 克隆vcpkg仓库
git clone https://github.com/Microsoft/vcpkg.git D:/work/vcpkg

# 运行bootstrap脚本
cd D:/work/vcpkg
.\bootstrap-vcpkg.bat
```

## 安装依赖包

```bash
cd D:/work/vcpkg

# 安装所有需要的依赖
.\vcpkg install rdkafka:x64-windows spdlog:x64-windows nlohmann-json:x64-windows
```

## 构建项目

### 使用VS Code

1. 按 `Ctrl+Shift+P` 打开命令面板
2. 选择 `Tasks: Run Task`
3. 选择 `build` 进行构建
4. 构建完成后，按 `F5` 开始调试

### 使用命令行

```bash
# 进入项目目录
cd D:\work\cpp_work\MQProxy

# 配置CMake
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# 构建项目
cmake --build build --config Debug
```

## 配置文件

配置文件 `config.toml` 位于项目根目录，包含以下配置：

```toml
serviceName="MQProxy"
mode="consumer"
enableProd=false
showContent = true

[LOGGER]
    level=1  # 0=trace;1=debug;2=info;3=warn;4=error;5=critical
    outdir="logs/MQProxy-Logs"

[DEV]
    brokers="localhost:9092"
    topics=["test-topic"]
    groupId="test-group"
    saslUsername=""
    saslPassword=""
    sslCaLocation=""
    securityProtocol=""
    saslMechanism=""
    sslEndpointIdentificationAlgorithm=""

[PROD]
    brokers="prod-kafka:9092"
    topics=["prod-topic"]
    groupId="prod-group"
    saslUsername="username"
    saslPassword="password"
    sslCaLocation="path/to/ca.crt"
    securityProtocol="SASL_SSL"
    saslMechanism="PLAIN"
    sslEndpointIdentificationAlgorithm="none"
```

## 运行

```bash
# 直接运行
.\build\bin\MQProxy.exe

# 或指定配置文件
.\build\bin\MQProxy.exe path\to\config.toml
```

## 日志

日志文件输出到配置的 `outdir` 目录下，按天分割。

## 项目结构

```
MQProxy/
├── CMakeLists.txt          # CMake构建配置
├── config.toml             # 配置文件
├── include/                # 头文件
│   ├── Config.h           # 配置管理
│   ├── Logger.h           # 日志封装
│   └── KafkaConsumer.h    # Kafka消费者
├── src/                    # 源文件
│   ├── Config.cpp
│   ├── Logger.cpp
│   ├── KafkaConsumer.cpp
│   └── main.cpp
└── .vscode/               # VS Code配置
    ├── launch.json
    └── tasks.json
```

## 特性

- 支持从config.toml加载配置
- 支持开发/生产环境切换
- 支持SASL认证
- 支持SSL/TLS加密
- 使用spdlog记录日志
- 支持多主题订阅
- 优雅退出处理
- 完整的错误处理
