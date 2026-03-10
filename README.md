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

# Debug构建
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug

# Release构建
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### 使用脚本

```bash
# Debug构建
.\build.bat

# Release构建
.\build_release.bat
```

## 配置文件

项目包含两个配置文件：
- `config.toml` - 实际运行的配置文件
- `config.example.toml` - 配置示例文件

### 配置说明

配置文件位于项目根目录，包含以下配置项：

```toml
serviceName="OPCUAKafkaProxy"

mode="consumer"              # producer 或 consumer
enableProd=false             # 是否启用生产模式
showContent = true           # 是否显示消息内容

[LOGGER]
    level= 1                 # 0=trace; 1=debug; 2=info; 3=warn; 4=error; 5=critical
    outdir="logs/OPCUAKafkaProxy-Logs"

[DEV]
    brokers="127.0.0.1:9097"
    topics=["test-topic-1"]
    groupId="tester"
    saslUsername=""          # SASL用户名
    saslPassword=""          # SASL密码
    sslCaLocation=""         # SSL CA证书路径
    sslKeyLocation=""        # SSL客户端密钥路径
    sslCertificateLocation="" # SSL客户端证书路径
    securityProtocol=""      # SASL_SSL, SSL, PLAINTEXT
    saslMechanism=""         # PLAIN, SCRAM-SHA-256, SCRAM-SHA-512
    sslEndpointIdentificationAlgorithm="" # none, https

[PROD]
    brokers="127.0.0.1:9092"
    topics=[
        "FTP-TEST-TOPIC-1",
        "FTP-TEST-TOPIC-2"
    ]
    groupId="xt-iot"
    saslUsername="username"
    saslPassword="password"
    sslCaLocation="kafka-trust.crt"
    sslKeyLocation=""
    sslCertificateLocation=""
    securityProtocol="SASL_SSL"
    saslMechanism="PLAIN"
    sslEndpointIdentificationAlgorithm="none"
```

## 运行

```bash
# Debug版本
.\build\bin\Debug\MQProxy.exe

# Release版本
.\build\bin\Release\MQProxy.exe
```

## 日志

日志文件输出到配置的 `outdir` 目录下，按天分割。

## 项目结构

```
MQProxy/
├── CMakeLists.txt          # CMake构建配置
├── config.toml             # 配置文件（实际运行使用）
├── config.example.toml     # 配置示例文件
├── build.bat               # Debug构建脚本
├── build_release.bat       # Release构建脚本
├── backup.bat              # 备份脚本
├── include/                # 头文件
│   ├── Config.h           # 配置管理
│   ├── Logger.h           # 日志封装
│   └── KafkaConsumer.h    # Kafka消费者
├── src/                    # 源文件
│   ├── Config.cpp
│   ├── Logger.cpp
│   ├── KafkaConsumer.cpp
│   └── main.cpp
├── 3d-partys/             # 本地第三方库
│   └── tomlplusplus/     # TOML解析库
└── .vscode/               # VS Code配置
    ├── launch.json
    └── tasks.json
```

## 特性

- 支持从config.toml加载配置
- 支持开发/生产环境切换
- 支持SASL认证 (PLAIN, SCRAM-SHA-256, SCRAM-SHA-512)
- 支持SSL/TLS加密
- 使用spdlog记录日志
- 支持多主题订阅
- 优雅退出处理
- 完整的错误处理
- **断线自动重连机制**
  - 可配置重连间隔 (reconnect.backoff.ms)
  - 最大重连间隔 (reconnect.backoff.max.ms)
  - Socket超时配置 (socket.timeout.ms)
  - 元数据刷新间隔 (metadata.max.age.ms)
  - 支持临时性错误自动恢复
  - 网络传输错误自动重连

## 备份

使用 `backup.bat` 脚本备份项目（排除中间文件和日志）：

```bash
.\backup.bat
```

备份文件保存在 `d:\backup_kafka` 目录。
