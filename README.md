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

---

**CAgent Project** — Zero overhead. Zero compromise. Deploy anywhere. Swap anything. 🦀

Choose your implementation based on your needs, or use both for different deployment scenarios. Both share the same vision: making AI assistant infrastructure accessible, secure, and efficient for everyone.