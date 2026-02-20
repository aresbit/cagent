# CAgent 项目 🦀

**零开销。零妥协。100% 可移植的 AI 助手基础设施**

CAgent 是一个全面的 AI 助手基础设施项目，包含两个互补的实现：
- **CClaw**：超轻量级 C 语言实现，追求极致性能和可移植性
- **ZeroClaw**：功能完整的 Rust 语言实现，拥有丰富的生态系统支持
## compile
```bash
cargo build --release --lib && make
```

## 项目概述

本项目提供快速、小巧且完全自主的 AI 助手基础设施，可在任何地方运行——从 10 美元的硬件到企业级服务器。两个实现共享相同的架构理念，但针对不同的使用场景和约束条件。

## 项目结构

```
cagent/
├── cclaw/           # C 语言实现（超轻量级）
├── zeroclaw/        # Rust 语言实现（功能完整）
├── memory/          # 共享内存文件
├── state/           # 共享状态文件
└── README.md        # 主说明文件
└── README_CN.md     # 中文说明文件（本文件）
```

## CClaw（C 语言实现）

**零开销。零妥协。100% C。**

CClaw 是 ZeroClaw 的 C 语言移植版本，保持了相同的架构和功能集，同时提供了 C 语言的性能和可移植性。

### 主要特性
- 🏎️ **超轻量级**：<3MB 内存占用
- 💰 **成本极低**：可在 10 美元硬件上运行
- ⚡ **闪电般快速**：<5ms 启动时间
- 🌍 **真正可移植**：单个二进制文件支持 ARM、x86 和 RISC-V
- 🔧 **完全可替换**：基于虚函数表的插件架构

### 快速开始
```bash
cd cclaw
make
./bin/cclaw --help
```

### 文档
- [CClaw README](cclaw/README.md) - 完整文档
- [CClaw CONTRIBUTING](cclaw/CONTRIBUTING.md) - 贡献指南
- [CClaw GUIDE](cclaw/GUIDE.md) - 用户指南
- [AGENT_FRAMEWORK](cclaw/AGENT_FRAMEWORK.md) - 架构概述

## ZeroClaw（Rust 语言实现）

**零开销。零妥协。100% Rust。100% 无绑定。**

ZeroClaw 是原始的 Rust 语言实现，拥有广泛的生态系统支持和功能完整性。

### 主要特性
- 🏎️ **超轻量级**：<5MB 内存占用
- 💰 **成本极低**：可在 10 美元硬件上运行
- ⚡ **闪电般快速**：<10ms 启动时间
- 🌍 **真正可移植**：单个自包含二进制文件
- 🔧 **完全可替换**：基于 trait 的插件架构

### 快速开始
```bash
cd zeroclaw
cargo build --release
cargo install --path . --force
zeroclaw onboard --api-key sk-... --provider openrouter
```

### 文档
- [ZeroClaw README](zeroclaw/README.md) - 完整文档
- [ZeroClaw CONTRIBUTING](zeroclaw/CONTRIBUTING.md) - 贡献指南
- [AGENTS.md](zeroclaw/AGENTS.md) - 代理框架
- [SECURITY.md](zeroclaw/SECURITY.md) - 安全指南

## 架构对比

两个实现遵循相同的架构原则：

| 子系统 | CClaw (C) | ZeroClaw (Rust) |
|--------|-----------|-----------------|
| **AI 模型** | `provider_t` 虚函数表 | `Provider` trait |
| **通信渠道** | `channel_t` 虚函数表 | `Channel` trait |
| **内存系统** | `memory_t` 虚函数表 | `Memory` trait |
| **工具系统** | `tool_t` 虚函数表 | `Tool` trait |
| **安全系统** | 多种接口 | `SecurityPolicy` trait |
| **异步运行时** | 基于 libuv | 基于 Tokio |

## 性能对比

| 指标 | CClaw (目标) | ZeroClaw (实际) |
|------|--------------|-----------------|
| 二进制大小 | <2MB | 3.4MB |
| 内存使用 | <3MB | <5MB |
| 启动时间 | <5ms | <10ms |
| 请求延迟 | <100ms | <100ms |
| 并发连接数 | 1000+ | 1000+ |

## 使用场景

### 选择 CClaw 当：
- 您需要在资源受限的硬件上获得最大性能
- 您需要与现有系统的 C 语言兼容性
- 您需要在特殊架构上运行（RISC-V、MIPS 等）
- 您希望最小的二进制大小和内存占用

### 选择 ZeroClaw 当：
- 您需要完整的 Rust 库生态系统
- 您需要广泛的提供商支持（22+ AI 提供商）
- 您需要全面的安全功能
- 您偏好内存安全保证
- 您希望更容易的扩展性和插件开发

## 开始使用

### 先决条件

对于 CClaw：
```bash
sudo apt-get install clang clang-tidy clang-format valgrind \
    libcurl4-openssl-dev libsqlite3-dev libsodium-dev libuv1-dev
```

对于 ZeroClaw：
```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
```

### 快速测试

测试两个实现：

```bash
# 测试 CClaw
cd cclaw
make test

# 测试 ZeroClaw
cd zeroclaw
cargo test
```

## 安全特性

两个实现都包含全面的安全功能：

- **网关配对**：6 位一次性代码交换
- **文件系统范围限制**：默认仅限工作空间访问
- **渠道白名单**：明确的用户/联系人授权
- **加密密钥**：API 密钥在存储时加密
- **Docker 沙箱**：可选的容器隔离
- **速率限制**：每个客户端的请求节流

## 开发

### CClaw 开发
```bash
cd cclaw
make debug=1      # 带检测器的调试构建
make test         # 运行测试
make lint         # 静态分析
make format       # 代码格式化
```

### ZeroClaw 开发
```bash
cd zeroclaw
cargo build              # 开发构建
cargo test               # 运行测试（1,017 个测试）
cargo clippy             # 代码检查
cargo fmt                # 代码格式化
```

## 贡献

我们欢迎对两个实现的贡献！请参阅：
- [CClaw CONTRIBUTING](cclaw/CONTRIBUTING.md)
- [ZeroClaw CONTRIBUTING](zeroclaw/CONTRIBUTING.md)

## 许可证

两个项目都使用 MIT 许可证：
- [CClaw LICENSE](cclaw/LICENSE)
- [ZeroClaw LICENSE](zeroclaw/LICENSE)

## 致谢

- **ZeroClaw 团队** - 原始的 Rust 实现和灵感来源
- **sp.h 库** - CClaw 使用的单头文件 C 库
- **libuv** - CClaw 的异步 I/O 库
- **Tokio** - ZeroClaw 的异步运行时
- **所有贡献者** - 感谢您让这个项目变得更好

## 支持

如果您觉得这个项目有用，请考虑支持开发："Buy Me a Coffee"

---

**CAgent 项目** — 零开销。零妥协。随处部署。任意替换。🦀

根据您的需求选择实现，或为不同的部署场景同时使用两者。两者共享相同的愿景：让 AI 助手基础设施对每个人来说都变得可访问、安全和高效。