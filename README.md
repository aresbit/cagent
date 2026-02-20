# CAgent 🦀

**Zero overhead. Zero compromise. 100% Portable AI Assistant Infrastructure**

CAgent is an AI assistant infrastructure project with two implementations:
- **CClaw**: Ultra-lightweight C implementation (~14MB)
- **ZeroClaw**: Full-featured Rust implementation

## Features

- **Multi-Provider Support**: OpenAI, Anthropic, DeepSeek, OpenRouter, Kimi
- **Multiple Channels**: CLI, Telegram, Webhook
- **Tool System**: Shell, File I/O, Memory operations
- **Memory**: SQLite, Markdown storage
- **Interactive Mode**: Chat with AI assistant

## Quick Start

```bash
# Build
make

# Run agent (interactive)
./build/bin/cclaw agent

# Run single message
./build/bin/cclaw agent -m "Hello!"
```

## Downloads

Pre-built binaries for v1.0.6:

| Platform | File |
|----------|------|
| Linux | [cclaw-linux-amd64](https://github.com/aresbit/cagent/releases/latest) |
| macOS ARM | [cclaw-macos-arm64](https://github.com/aresbit/cagent/releases/latest) |
| macOS Intel | [cclaw-macos-x86_64](https://github.com/aresbit/cagent/releases/latest) |
| Windows | [cclaw-windows.exe](https://github.com/aresbit/cagent/releases/latest) |

## Build from Source

### Prerequisites

- Rust (for ZeroClaw)
- Clang
- libcurl, libsqlite3, libsodium, libuv

### Build

```bash
# Clone
git clone https://github.com/aresbit/cagent.git
cd cagent

# Build all
make

# Or build individually
cd cclaw && make        # C implementation
cd ../zeroclaw && cargo build --release  # Rust implementation
```

## Project Structure

```
cagent/
├── cclaw/           # C implementation
├── zeroclaw/        # Rust implementation
├── build/           # Build output
├── Makefile         # Build orchestration
└── .github/         # CI/CD
```

## Usage

```bash
# Interactive agent
./build/bin/cclaw agent

# With specific provider
./build/bin/cclaw agent -p openai -m "Hello"

# List available commands
./build/bin/cclaw help
```

## License

MIT License - see [cclaw/LICENSE](cclaw/LICENSE) and [zeroclaw/LICENSE](zeroclaw/LICENSE)

## GitHub

https://github.com/aresbit/cagent
