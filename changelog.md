# Changelog

All notable changes to CAgent (CClaw + ZeroClaw) will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Telegram Bot Integration**: Full Telegram bot support with CClaw configuration compatibility
- **Daemon Improvements**: Enhanced daemon management with ZeroClaw integration
- **Configuration Loader**: ZeroClaw can now directly read CClaw's `~/.cclaw/config.json` files

### Changed
- **Security Model**: Relaxed security policies to allow more flexible command execution
- **Build System**: CClaw build now depends on ZeroClaw library for better integration

### Fixed
- **Telegram Configuration**: Fixed API key missing issue in Telegram channel configuration
- **Build Dependencies**: Fixed build order to ensure ZeroClaw library is built before CClaw

## [2.0.0] - 2026-02-21

### Added
- **Telegram Bot Integration**: Complete Telegram bot support with CClaw configuration compatibility
- **Direct Config Loading**: ZeroClaw can now directly read CClaw's JSON configuration files
- **Enhanced Daemon**: Improved daemon management using ZeroClaw's mature daemon implementation

### Changed
- **Configuration System**: Unified configuration handling between CClaw and ZeroClaw
- **FFI Interface**: Enhanced FFI interface for better C-Rust integration

### Fixed
- **Telegram Message Handling**: Fixed API key configuration for Telegram message processing
- **Config Synchronization**: Eliminated configuration sync issues between CClaw and ZeroClaw

## [1.0.8] - 2026-02-21

### Added
- **Configuration Documentation**: Added comprehensive configuration section to README (API keys, providers)
- **Simplified Release Workflow**: Build only Linux amd64 for releases to streamline process

### Changed
- **Release Process**: Simplified GitHub Actions release workflow
- **Version Updates**: Updated all components to version 1.0.8

## [1.0.6] - 2026-02-20

### Fixed
- **YAML Indentation**: Fixed YAML formatting in GitHub Actions release workflow
- **Documentation**: Rewrote README.md for better clarity and organization

### Changed
- **Version Updates**: Updated all components to version 1.0.6

## [1.0.5] - 2026-02-20

### Fixed
- **Release Workflow**: Made release workflow independent per platform
- **Build Resilience**: Added create-release job that runs even if some builds fail

### Changed
- **Workflow Strategy**: Used `if: always()` and `continue-on-error: true` for better reliability
- **Version Updates**: Updated all components to version 1.0.5

## [1.0.3] - 2026-02-20

### Changed
- **Version Updates**: Updated all components to version 1.0.3

## [1.0.2] - 2026-02-20

### Fixed
- **Build Dependencies**: CClaw build now properly depends on ZeroClaw library
- **Agent UI**: Improved agent user interface with cleaner box design
- **Dependencies**: Added uuid-dev to GitHub Actions workflow dependencies

### Changed
- **Version Display**: Using `env!` macro for version display in UI
- **Build System**: Root Makefile cclaw target now builds zeroclaw first

## [1.0.1] - 2026-02-20

### Fixed
- **Hardcoded Paths**: Fixed ZEROCLAW_DIR path in cclaw/Makefile (was `./cagent/zeroclaw`, now `../zeroclaw`)
- **Cross-Platform Builds**: Added release workflow for cross-platform builds

### Added
- **Release Workflow**: GitHub Actions workflow for automated releases
- **Download Links**: Updated README with download links for releases

### Changed
- **Version Updates**: Updated both CClaw and ZeroClaw to version 1.0.1

## [1.0.0] - 2026-02-20

### Added
- **Initial Release**: First stable release of CAgent project
- **Dual Implementation**: Both CClaw (C) and ZeroClaw (Rust) implementations
- **Core Features**:
  - Multi-provider support (OpenAI, Anthropic, DeepSeek, OpenRouter, Kimi)
  - Multiple channels (CLI, Telegram, Webhook)
  - Tool system (Shell, File I/O, Memory operations)
  - Memory storage (SQLite, Markdown)
  - Interactive chat mode
  - Daemon mode for background operation

### Security
- **Workspace Sandboxing**: Restricted file access to workspace directory
- **Command Allowlisting**: Controlled command execution
- **Path Traversal Protection**: Blocked access to sensitive system paths
- **Autonomy Levels**: Configurable security levels (ReadOnly/Supervised/Full)

## [0.1.0] - 2026-02-13

### Added
- **ZeroClaw Initial Release**: First release of ZeroClaw Rust implementation
- **Core Architecture**: Trait-based pluggable system for Provider, Channel, Observer, RuntimeAdapter, Tool
- **Provider Support**: OpenRouter implementation (access Claude, GPT-4, Llama, Gemini via single API)
- **Channels**: CLI channel with interactive and single-message modes
- **Observability**: NoopObserver (zero overhead), LogObserver (tracing), MultiObserver (fan-out)
- **Security**: Workspace sandboxing, command allowlisting, path traversal blocking, autonomy levels
- **Tools**: Shell (sandboxed), FileRead (path-checked), FileWrite (path-checked)
- **Memory (Brain)**: SQLite persistent backend (searchable, survives restarts), Markdown backend
- **Heartbeat Engine**: Periodic task execution from HEARTBEAT.md
- **Runtime**: Native adapter for Mac/Linux/Raspberry Pi
- **Config**: TOML-based configuration with sensible defaults
- **Onboarding**: Interactive CLI wizard with workspace scaffolding
- **CLI Commands**: agent, gateway, status, cron, channel, tools, onboard
- **CI/CD**: GitHub Actions with cross-platform builds (Linux, macOS Intel/ARM, Windows)
- **Tests**: 159 inline tests covering all modules and edge cases
- **Binary**: 3.1MB optimized release build (includes bundled SQLite)

### Security
- Path traversal attack prevention
- Command injection blocking
- Workspace escape prevention
- Forbidden system path protection (`/etc`, `/root`, `~/.ssh`)

## Legacy Notes

### Security Migration
- **Legacy XOR cipher migration**: The `enc:` prefix (XOR cipher) is deprecated in ZeroClaw
- **New Encryption**: Secrets using legacy format are automatically migrated to `enc2:` (ChaCha20-Poly1305 AEAD)
- **Backward Compatibility**: Legacy values are still decrypted but should be migrated

[2.0.0]: https://github.com/aresbit/cagent/releases/tag/v2.0.0
[1.0.8]: https://github.com/aresbit/cagent/releases/tag/v1.0.8
[1.0.6]: https://github.com/aresbit/cagent/releases/tag/v1.0.6
[1.0.5]: https://github.com/aresbit/cagent/releases/tag/v1.0.5
[1.0.3]: https://github.com/aresbit/cagent/releases/tag/v1.0.3
[1.0.2]: https://github.com/aresbit/cagent/releases/tag/v1.0.2
[1.0.1]: https://github.com/aresbit/cagent/releases/tag/v1.0.1
[1.0.0]: https://github.com/aresbit/cagent/releases/tag/v1.0.0
[0.1.0]: https://github.com/theonlyhennygod/zeroclaw/releases/tag/v0.1.0