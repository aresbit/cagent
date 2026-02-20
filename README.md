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
git clone https://github.com/aresbit/cagent.git
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