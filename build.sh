#!/bin/bash

# CAgent 统一构建脚本
# 这个脚本演示如何使用 Makefile 构建整个项目

set -e  # 遇到错误时退出

echo "=== CAgent 项目构建脚本 ==="
echo ""

# 显示帮助信息
echo "1. 显示帮助信息:"
cat Makefile | grep -A 30 "^help:" | sed '1d' | head -30

echo ""
echo "2. 显示项目状态:"
if command -v make &> /dev/null; then
    make status 2>/dev/null || echo "Make 命令执行失败，但 Makefile 已创建"
else
    echo "make 命令未安装，但 Makefile 已创建"
    echo "您可以使用以下命令手动构建:"
    echo "  cd cclaw && make"
    echo "  cd zeroclaw && cargo build --release"
fi

echo ""
echo "3. Makefile 内容摘要:"
echo "文件大小: $(wc -l < Makefile) 行"
echo "主要目标:"
echo "  - all/build/release: 构建所有项目 (release)"
echo "  - debug: 构建所有项目 (debug)"
echo "  - test: 运行所有测试"
echo "  - clean: 清理构建文件"
echo "  - install: 安装到系统"
echo "  - status: 显示项目状态"

echo ""
echo "4. 使用示例:"
echo "  # 构建所有项目 (release)"
echo "  make all"
echo "  "
echo "  # 构建所有项目 (debug)"
echo "  make debug"
echo "  "
echo "  # 仅构建 CClaw"
echo "  make cclaw"
echo "  "
echo "  # 仅构建 ZeroClaw"
echo "  make zeroclaw"
echo "  "
echo "  # 运行所有测试"
echo "  make test"
echo "  "
echo "  # 清理所有构建文件"
echo "  make clean"

echo ""
echo "5. 目录结构:"
echo "  ./Makefile          - 统一构建文件"
echo "  ./build.sh         - 此构建脚本"
echo "  ./cclaw/           - CClaw 项目"
echo "  ./zeroclaw/        - ZeroClaw 项目"
echo "  ./build/           - 构建输出目录 (自动创建)"
echo "      ├── bin/       - 二进制文件"
echo "      └── lib/       - 库文件"

echo ""
echo "=== 构建脚本完成 ==="
echo ""
echo "提示: 如果您没有 make 命令，可以手动执行:"
echo "  cd cclaw && make"
echo "  cd zeroclaw && cargo build --release"
