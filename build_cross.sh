#!/bin/bash
# Cross-platform build script for CClaw
# Usage: ./build.sh [platform]
# Platforms: linux-amd64, macos-arm64, macos-x86_64, windows, termux

set -e

VERSION=${VERSION:-1.0.0}
BUILD_DIR=release
mkdir -p $BUILD_DIR

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo_step() {
    echo -e "${GREEN}[BUILD]${NC} $1"
}

echo_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Build for current platform
build_current() {
    echo_step "Building for current platform..."
    
    # Build ZeroClaw
    echo_step "Building ZeroClaw..."
    cd zeroclaw
    cargo build --release --lib
    cd ..
    
    # Build CClaw
    echo_step "Building CClaw..."
    cd cclaw
    make clean
    make
    cd ..
    
    # Copy binaries
    cp cclaw/bin/cclaw $BUILD_DIR/
    
    echo_step "Build complete!"
}

# Build Linux amd64
build_linux_amd64() {
    echo_step "Building for Linux amd64..."
    
    # Requires: sudo apt-get install mingw-w64
    # This is a placeholder - full cross-compile needs more setup
    
    echo_error "Linux amd64 cross-compilation requires MinGW setup"
    echo "Run locally: make"
}

# Build macOS arm64
build_macos_arm64() {
    echo_step "Building for macOS ARM (Apple Silicon)..."
    
    if [ "$(uname)" != "Darwin" ]; then
        echo_error "macOS builds must be run on macOS"
        exit 1
    fi
    
    # Build for Apple Silicon
    cargo build --release --lib --target aarch64-apple-darwin
    
    # Build CClaw
    make clean
    make
    
    cp cclaw/bin/cclaw $BUILD_DIR/cclaw-macos-arm64
}

# Build Windows
build_windows() {
    echo_step "Building for Windows..."
    
    # Requires cross-compiler
    if ! command -v x86_64-w64-mingw32-gcc &> /dev/null; then
        echo_error "MinGW not installed. Run: sudo apt-get install mingw-w64"
        exit 1
    fi
    
    # Build ZeroClaw for Windows
    cd zeroclaw
    cargo build --release --lib --target x86_64-pc-windows-gnu
    cd ..
    
    # Build CClaw with MinGW
    cd cclaw
    make clean
    make CC=x86_64-w64-mingw32-gcc
    cd ..
    
    # Rename for Windows
    cp cclaw/bin/cclaw $BUILD_DIR/cclaw.exe
}

# Build Termux (Android)
build_termux() {
    echo_step "Building for Termux (Android)..."
    
    echo_error "Termux builds require Termux environment"
    echo "Run in Termux: pkg install rust make clang && make"
}

# Main
case "${1:-current}" in
    linux-amd64)
        build_linux_amd64
        ;;
    macos-arm64)
        build_macos_arm64
        ;;
    macos-x86_64)
        build_macos_x86_64
        ;;
    windows)
        build_windows
        ;;
    termux)
        build_termux
        ;;
    all)
        build_current
        echo "Note: Full cross-compilation requires platform-specific environments"
        ;;
    *)
        echo "Usage: $0 [platform]"
        echo "Platforms:"
        echo "  current       - Build for current platform (default)"
        echo "  linux-amd64   - Linux x86_64"
        echo "  macos-arm64   - macOS Apple Silicon"
        echo "  macos-x86_64  - macOS Intel"
        echo "  windows       - Windows x64"
        echo "  termux        - Termux/Android"
        echo "  all           - Build all (current only)"
        ;;
esac
