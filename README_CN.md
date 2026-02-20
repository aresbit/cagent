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
## agent case

> 请使用agent-browser 技能访问鸿蒙官网并截图保存
我注意到您提到了"agent-browser"技能，但目前可用的技能列表中并没有这个技能。让我先检查一下当前目录结构和可用的技能文件，然后看看如何帮助您访问鸿蒙官网并截图。让我重新执行命令来查看当前目录结构：让我检查一下当前的工作目录和可用的技能：现在让我检查技能目录，看看是否有浏览器相关的技能：让我查看技能目录中有哪些可用的技能：太好了！我看到了有一个"browser-automation-agent"技能，这应该就是您提到的浏览器自动化技能。让我先读取这个技能的说明文件：让我检查一下这个技能目录的结构：现在让我读取SKILL.md文件来了解如何使用这个浏览器自动化技能：很好！现在我了解了如何使用agent-browser技能。首先，我需要检查是否已经安装了agent-browser。让我检查一下：很好！agent-browser已经安装。现在让我检查它的版本并确保它可以正常工作：完美！现在我可以使用agent-browser来访问鸿蒙官网。首先，我需要知道鸿蒙官网的URL。让我先尝试访问华为的鸿蒙官网（harmonyos.com）：很好！页面已经打开。现在让我等待几秒钟让页面完全加载，然后截取屏幕截图：现在让我截取屏幕截图。根据技能说明，我可以使用`screenshot`命令：太好了！截图已经保存。现在让我检查一下截图文件是否创建成功：让我检查当前目录下的所有文件：看起来截图可能保存在其他位置。让我检查agent-browser的默认保存位置，或者尝试使用绝对路径：找到了！截图保存在`/home/ares/yyscode/cagent/cclaw/harmonyos_homepage.png`。让我检查一下这个文件：很好！现在让我获取这个图片的信息，确认它确实是鸿蒙官网的截图：完美！现在让我关闭浏览器会话：
完成！我已经成功使用agent-browser技能访问了鸿蒙官网并截图保存。让我总结一下完成的操作：

## 任务完成总结

1. **检查并确认agent-browser已安装**：
   - 确认agent-browser已安装，版本为0.13.0

2. **访问鸿蒙官网**：
   - 使用命令 `agent-browser open https://www.harmonyos.com` 成功打开华为HarmonyOS官网

3. **截取屏幕截图**：
   - 等待页面加载后，使用命令 `agent-browser screenshot harmonyos_homepage.png` 截取屏幕截图
   - 截图已成功保存为PNG文件

4. **验证截图文件**：
   - 截图文件保存在：`/home/ares/yyscode/cagent/cclaw/harmonyos_homepage.png`
   - 文件大小：199,607字节
   - 分辨率：1280x720像素

5. **清理资源**：
   - 使用 `agent-browser close` 命令关闭浏览器会话

截图已成功保存，您可以查看该文件来确认鸿蒙官网的访问结果。如果您需要进一步的操作，比如：
- 获取页面内容
- 与页面元素交互（点击按钮、填写表单等）
- 生成PDF版本
- 导航到其他页面

请告诉我，我可以继续使用agent-browser技能来完成这些任务。

## CClaw 技能加载架构分析

  核心结论：CClaw 是套皮，技能加载完全依赖 ZeroClaw (Rust)

  ┌─────────────────────────────────────────────────────────────┐
  │                        CClaw (C)                            │
  │  ┌─────────────────────────────────────────────────────┐   │
  │  │  commands_zeroclaw.c                                │   │
  │  │  └─ cmd_agent_zeroclaw()                            │   │
  │  │     └─ zc_agent_init() ──────────────────────┐      │   │
  │  └──────────────────────────────────────────────┼──────┘   │
  └─────────────────────────────────────────────────┼───────────┘
                                                    │
                                                    ▼ FFI 调用
  ┌─────────────────────────────────────────────────────────────┐
  │                     ZeroClaw (Rust)                         │
  │  ┌─────────────────────────────────────────────────────┐   │
  │  │  ffi/mod.rs                                         │   │
  │  │  └─ zc_agent_init()                                 │   │
  │  │     └─ build_system_prompt() ────────────────┐      │   │
  │  │        └─ load_skills(&config.workspace_dir) │      │   │
  │  │                                              ▼      │   │
  │  │        skills/mod.rs                                │   │
  │  │        ├─ load_skills()                             │   │
  │  │        │   ├─ load_open_skills()      ← ~/.zeroclaw/open-skills  │
  │  │        │   └─ load_workspace_skills() ← ~/.cclaw/workspace/skills │
  │  │        ├─ load_skill_toml()                         │   │
  │  │        └─ load_skill_md()                           │   │
  │  └─────────────────────────────────────────────────────┘   │
  └─────────────────────────────────────────────────────────────┘

  技能加载流程

  1. CClaw 启动时（commands_zeroclaw.c）
  // 传递 workspace 给 ZeroClaw
  zc_agent_init(
      config_json,
      workspace,  // 通常是 "." (当前目录)
      &runtime
  );

  2. ZeroClaw 初始化时（ffi/mod.rs:119）
  fn build_system_prompt(config: &Config, tools: &[Box<dyn Tool>]) -> String {
      let skills = crate::skills::load_skills(&config.workspace_dir);
      // ... 将技能添加到系统提示词
  }

  3. 技能加载逻辑（skills/mod.rs:73-82）
  pub fn load_skills(workspace_dir: &Path) -> Vec<Skill> {
      let mut skills = Vec::new();

      // 1. 加载 open-skills (社区技能库)
      if let Some(open_skills_dir) = ensure_open_skills_repo() {
          skills.extend(load_open_skills(&open_skills_dir));  // ~/.zeroclaw/open-skills
      }

      // 2. 加载用户工作空间技能
      skills.extend(load_workspace_skills(workspace_dir));  // ~/.cclaw/workspace/skills

      skills
  }

  默认目录结构

  类型: ZeroClaw 配置
  路径: ~/.zeroclaw/config.toml
  说明: ZeroClaw 自身配置
  ────────────────────────────────────────
  类型: CClaw/ZeroClaw 工作空间
  路径: ~/.cclaw/workspace/
  说明: 默认 workspace_dir
  ────────────────────────────────────────
  类型: 用户技能
  路径: ~/.cclaw/workspace/skills/<name>/SKILL.toml
  说明: 用户自定义技能
  ────────────────────────────────────────
  类型: 社区技能
  路径: ~/.zeroclaw/open-skills/ 或 ~/open-skills/
  说明: 从 GitHub 克隆的 open-skills 仓库

  关键点

  1. CClaw 的 skill.c 是"空气" - 虽然存在，但当前没有调用它的代码。cclaw/src/cli/commands_zeroclaw.c 直接走
  FFI 调用 Rust。
  2. 技能加载完全在 Rust 层 - zeroclaw/src/skills/mod.rs 有完整的技能加载、解析、管理实现。
  3. ZeroClaw 默认使用 ~/.cclaw 作为工作空间（见 config/schema.rs:816）：
  let cclaw_dir = home.join(".cclaw");
  Self {
      workspace_dir: cclaw_dir,  // ← 默认就是 ~/.cclaw
      ...
  }
  4. 技能路径解析：
    - 用户技能：~/.cclaw/workspace/skills/<skill-name>/SKILL.toml
    - 或：~/.cclaw/workspace/skills/<skill-name>/SKILL.md

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