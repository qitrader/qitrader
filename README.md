# BitCoinTrader - 高性能比特币交易系统

BitCoinTrader是一个基于C++23和Boost.Asio构建的高性能、异步事件驱动的比特币交易系统。该系统采用模块化设计，支持多交易所接入和灵活的交易策略。

## 🚀 功能特性

- **高性能交易引擎**: 基于Boost.Asio的异步事件驱动架构，支持高并发交易处理
- **多交易所支持**: 目前支持OKX交易所，可扩展支持其他主流交易所
- **模块化设计**: 引擎、市场接口、策略、通知系统相互独立，易于扩展
- **实时通知**: 集成企业微信通知，实时推送交易状态和异常信息
- **配置化管理**: 使用INI配置文件管理API密钥、交易参数等敏感信息
- **现代C++标准**: 采用C++23最新特性，确保代码性能和可维护性

## 📁 项目结构

```
BitCoinTrader/
├── common/           # 公共组件
│   ├── config/       # 配置管理
│   ├── context/      # 上下文管理
│   └── utils/        # 工具函数
├── engine/           # 交易引擎核心
│   ├── engine.h/cpp  # 引擎主类
│   └── object.h      # 事件对象定义
├── market/           # 市场接口
│   ├── base/         # 基础网关接口
│   └── okx/          # OKX交易所实现
├── notice/           # 通知系统
│   ├── base/         # 通知基础类
│   └── wework/       # 企业微信通知
├── strategy/         # 交易策略
│   ├── base/         # 策略基础类
│   └── testing/      # 测试策略
├── repo/             # 依赖包管理
└── xmake.lua         # 构建配置文件
```

## 🛠️ 构建要求

### 系统要求
- Linux操作系统
- Clang编译器（支持C++23）
- xmake构建工具

### 依赖库
- Boost (asio, beast, url, json, system等)
- fmt (格式化库)
- OpenSSL (加密)
- CryptoPP (加密算法)
- glog (日志)
- liburing (异步IO)
- jsoncpp (JSON解析)
- cpphttp (HTTP客户端)

## 📦 安装与构建

### 1. 安装xmake
```bash
# 使用包管理器安装
sudo apt install xmake

# 或从源码安装
curl -fsSL https://xmake.io/shget.text | bash
```

### 2. 克隆项目
```bash
git clone <repository-url>
cd BitCoinTrader
```

### 3. 构建项目
```bash
# 调试模式构建
xmake

# 发布模式构建
xmake config --mode=release
xmake
```

### 4. 运行
```bash
# 创建配置文件
cp config.example.ini config.ini

# 编辑配置文件，填入API密钥等信息
vim config.ini

# 运行程序
xmake run BitCoinTrader
```

## ⚙️ 配置说明

创建`config.ini`配置文件：

```ini
[common]
timeout_ms = 5000

[okx]
api_key = your_api_key
secret_key = your_secret_key
passphrase = your_passphrase

[wework]
key = your_wework_key

[compare]
min_diff = 0.5
report_time = 60
```

## 🔧 核心模块

### 交易引擎 (Engine)
- 事件驱动的异步架构
- 支持组件注册和管理
- 提供事件发布/订阅机制

### 市场接口 (Market Gateway)
- 抽象网关接口，支持多交易所
- OKX交易所完整实现
- HTTP API调用和数据处理

### 交易策略 (Strategy)
- 策略基类，支持策略扩展
- 测试策略实现
- 账户和持仓数据接收

### 通知系统 (Notice)
- 企业微信通知集成
- 交易状态实时推送
- 异常告警功能

## 🎯 使用示例

### 基本使用流程
1. 配置API密钥和交易参数
2. 启动交易引擎
3. 注册市场接口组件
4. 注册交易策略组件
5. 注册通知组件
6. 引擎自动处理交易事件

### 扩展开发
要添加新的交易所支持：
1. 继承`base::Gateway`类
2. 实现交易所特定的API调用
3. 注册到交易引擎

要添加新的交易策略：
1. 继承`base::Stragy`类
2. 实现策略逻辑
3. 注册到交易引擎

## 📊 性能特点

- **异步非阻塞**: 基于Boost.Asio的协程，避免线程阻塞
- **事件驱动**: 高效的事件处理机制，减少资源消耗
- **内存安全**: 智能指针管理，避免内存泄漏
- **高并发**: 支持同时处理多个交易对和策略

## 🐛 问题排查

### 常见问题
1. **构建失败**: 检查依赖库是否安装完整
2. **运行时错误**: 验证配置文件格式和API密钥
3. **网络连接问题**: 检查防火墙和代理设置

### 日志查看
程序使用glog进行日志记录，日志输出到stderr，可通过环境变量调整日志级别。

## 🤝 贡献指南

1. Fork项目
2. 创建功能分支
3. 提交更改
4. 推送到分支
5. 创建Pull Request

## 📄 许可证

本项目采用MIT许可证，详见LICENSE文件。

## 📞 联系方式

如有问题或建议，请通过以下方式联系：
- 提交Issue
- 发送邮件到项目维护者

---

**注意**: 交易有风险，请在充分了解市场风险的情况下使用本系统。建议先在模拟环境中测试。
