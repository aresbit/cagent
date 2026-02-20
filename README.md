# CAgent Project 🦀

**Zero overhead. Zero compromise. 100% Portable AI Assistant Infrastructure**

CAgent is a comprehensive AI assistant infrastructure project featuring two complementary implementations:
- **CClaw**: Ultra-lightweight C implementation for maximum performance and portability
- **ZeroClaw**: Full-featured Rust implementation with extensive ecosystem support

## Overview

This project provides fast, small, and fully autonomous AI assistant infrastructure that can run anywhere — from $10 hardware to enterprise servers. Both implementations share the same architectural philosophy but target different use cases and constraints.

## Project Structure

```
cagent/
├── cclaw/           # C implementation (ultra-lightweight)
├── zeroclaw/        # Rust implementation (full-featured)
├── memory/          # Shared memory files
├── state/           # Shared state files
└── README.md        # This file
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

## CClaw (C Implementation)

**Zero overhead. Zero compromise. 100% C.**

CClaw is a C port of ZeroClaw, maintaining the same architecture and feature set while providing the performance and portability of C.

### Key Features
- 🏎️ **Ultra-Lightweight**: <3MB memory footprint
- 💰 **Minimal Cost**: Runs on $10 hardware
- ⚡ **Lightning Fast**: <5ms startup time
- 🌍 **True Portability**: Single binary across ARM, x86, and RISC-V
- 🔧 **Fully Swappable**: Plugin architecture with vtables

### Quick Start
```bash
cd cclaw
make
./bin/cclaw --help
```

### Documentation
- [CClaw README](cclaw/README.md) - Full documentation
- [CClaw CONTRIBUTING](cclaw/CONTRIBUTING.md) - Contribution guidelines
- [CClaw GUIDE](cclaw/GUIDE.md) - User guide
- [AGENT_FRAMEWORK](cclaw/AGENT_FRAMEWORK.md) - Architecture overview

## ZeroClaw (Rust Implementation)

**Zero overhead. Zero compromise. 100% Rust. 100% Agnostic.**

ZeroClaw is the original Rust implementation with extensive ecosystem support and feature completeness.

### Key Features
- 🏎️ **Ultra-Lightweight**: <5MB memory footprint
- 💰 **Minimal Cost**: Runs on $10 hardware
- ⚡ **Lightning Fast**: <10ms startup time
- 🌍 **True Portability**: Single self-contained binary
- 🔧 **Fully Swappable**: Trait-based plugin architecture

### Quick Start
```bash
cd zeroclaw
cargo build --release
cargo install --path . --force
zeroclaw onboard --api-key sk-... --provider openrouter
```

### Documentation
- [ZeroClaw README](zeroclaw/README.md) - Full documentation
- [ZeroClaw CONTRIBUTING](zeroclaw/CONTRIBUTING.md) - Contribution guidelines
- [AGENTS.md](zeroclaw/AGENTS.md) - Agent framework
- [SECURITY.md](zeroclaw/SECURITY.md) - Security guidelines

## Architecture Comparison

Both implementations follow the same architectural principles:

| Subsystem | CClaw (C) | ZeroClaw (Rust) |
|-----------|-----------|-----------------|
| **AI Models** | `provider_t` vtable | `Provider` trait |
| **Channels** | `channel_t` vtable | `Channel` trait |
| **Memory** | `memory_t` vtable | `Memory` trait |
| **Tools** | `tool_t` vtable | `Tool` trait |
| **Security** | Various interfaces | `SecurityPolicy` trait |
| **Async Runtime** | libuv-based | Tokio-based |

## Performance Comparison

| Metric | CClaw (Target) | ZeroClaw (Actual) |
|--------|----------------|-------------------|
| Binary Size | <2MB | 3.4MB |
| Memory Usage | <3MB | <5MB |
| Startup Time | <5ms | <10ms |
| Request Latency | <100ms | <100ms |
| Concurrent Connections | 1000+ | 1000+ |

## Use Cases

### Choose CClaw when:
- You need maximum performance on resource-constrained hardware
- You require C compatibility with existing systems
- You need to run on exotic architectures (RISC-V, MIPS, etc.)
- You want minimal binary size and memory footprint

### Choose ZeroClaw when:
- You need the full ecosystem of Rust libraries
- You want extensive provider support (22+ AI providers)
- You need comprehensive security features
- You prefer memory safety guarantees
- You want easier extensibility and plugin development

## Getting Started

### Prerequisites

For CClaw:
```bash
sudo apt-get install clang clang-tidy clang-format valgrind \
    libcurl4-openssl-dev libsqlite3-dev libsodium-dev libuv1-dev
```

For ZeroClaw:
```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
```

### Quick Test

Test both implementations:

```bash
# Test CClaw
cd cclaw
make test

# Test ZeroClaw
cd zeroclaw
cargo test
```

## Security Features

Both implementations include comprehensive security:

- **Gateway Pairing**: 6-digit one-time code exchange
- **Filesystem Scoping**: Workspace-only access by default
- **Channel Allowlists**: Explicit user/contact authorization
- **Encrypted Secrets**: API keys encrypted at rest
- **Docker Sandboxing**: Optional container isolation
- **Rate Limiting**: Request throttling per client

## Development

### CClaw Development
```bash
cd cclaw
make debug=1      # Debug build with sanitizers
make test         # Run tests
make lint         # Static analysis
make format       # Format code
```

### ZeroClaw Development
```bash
cd zeroclaw
cargo build              # Dev build
cargo test               # Run tests (1,017 tests)
cargo clippy             # Lint
cargo fmt                # Format
```

## Contributing

We welcome contributions to both implementations! Please see:
- [CClaw CONTRIBUTING](cclaw/CONTRIBUTING.md)
- [ZeroClaw CONTRIBUTING](zeroclaw/CONTRIBUTING.md)

## License

Both projects are licensed under the MIT License:
- [CClaw LICENSE](cclaw/LICENSE)
- [ZeroClaw LICENSE](zeroclaw/LICENSE)

## Acknowledgments

- **ZeroClaw Team** - Original Rust implementation and inspiration
- **sp.h Library** - Single-header C library used by CClaw
- **libuv** - Async I/O library for CClaw
- **Tokio** - Async runtime for ZeroClaw
- **All Contributors** - Thank you for making this project better

## Support

If you find this project useful, consider supporting the development:

<a href="https://buymeacoffee.com/argenistherose"><img src="https://img.shields.io/badge/Buy%20Me%20a%20Coffee-Donate-yellow.svg?style=for-the-badge&logo=buy-me-a-coffee" alt="Buy Me a Coffee" /></a>

## Downloads / Releases

Pre-built binaries for v1.0.0 are available on the [GitHub Releases](https://github.com/theonlyhennygod/cagent/releases) page:

| Platform | File | Architecture |
|----------|------|--------------|
| Linux | `cclaw-linux-amd64` | x86_64 |
| macOS | `cclaw-macos-arm64` | Apple Silicon (M1/M2/M3) |
| macOS | `cclaw-macos-x86_64` | Intel |
| Windows | `cclaw-windows.exe` | x86_64 (coming soon) |

### Build from Source

```bash
# Clone and build
git clone https://github.com/theonlyhennygod/cagent.git
cd cagent
make

# Or build individual components
cd zeroclaw && cargo build --release --lib # Rust implementation
cd cclaw && make        # C implementation
```

### Cross-Compilation

This project uses GitHub Actions for cross-platform builds. See [.github/workflows/release.yml](.github/workflows/release.yml) for build configurations.

---

**CAgent Project** — Zero overhead. Zero compromise. Deploy anywhere. Swap anything. 🦀

Choose your implementation based on your needs, or use both for different deployment scenarios. Both share the same vision: making AI assistant infrastructure accessible, secure, and efficient for everyone.