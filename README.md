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