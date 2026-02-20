# CAgent 项目统一构建系统
# 用于构建 CClaw (C) 和 ZeroClaw (Rust) 两个实现

.PHONY: all build release debug test clean help
.DEFAULT_GOAL := all

# 颜色定义
RED := \033[0;31m
GREEN := \033[0;32m
YELLOW := \033[1;33m
BLUE := \033[0;34m
NC := \033[0m # No Color

# 项目信息
PROJECT_NAME := CAgent
VERSION := 1.0.0

# 目录定义
CAGENT_DIR := .
CCLAW_DIR := cclaw
ZEROCLAW_DIR := zeroclaw

# 输出目录
BUILD_DIR := build
BIN_DIR := $(BUILD_DIR)/bin
LIB_DIR := $(BUILD_DIR)/lib

# 目标文件 - 使用 build 目录
CCLAW_BIN := $(BIN_DIR)/cclaw
ZEROCLAW_BIN := $(ZEROCLAW_DIR)/target/release/libzeroclaw.a

# 帮助信息
help:
	@echo "$(GREEN)$(PROJECT_NAME) 统一构建系统$(NC)"
	@echo "$(YELLOW)版本: $(VERSION)$(NC)"
	@echo ""
	@echo "$(BLUE)可用目标:$(NC)"
	@echo "  $(GREEN)all$(NC)          - 构建所有项目 (release 版本)"
	@echo "  $(GREEN)build$(NC)        - 构建所有项目 (release 版本)"
	@echo "  $(GREEN)release$(NC)      - 构建所有项目 (release 版本)"
	@echo "  $(GREEN)debug$(NC)        - 构建所有项目 (debug 版本)"
	@echo "  $(GREEN)test$(NC)         - 运行所有测试"
	@echo "  $(GREEN)clean$(NC)        - 清理所有构建文件"
	@echo "  $(GREEN)install$(NC)      - 安装所有二进制文件到系统"
	@echo "  $(GREEN)status$(NC)       - 显示项目状态"
	@echo "  $(GREEN)help$(NC)         - 显示此帮助信息"
	@echo ""
	@echo "$(BLUE)子项目构建:$(NC)"
	@echo "  $(GREEN)cclaw$(NC)        - 仅构建 CClaw (release)"
	@echo "  $(GREEN)cclaw-debug$(NC)  - 仅构建 CClaw (debug)"
	@echo "  $(GREEN)zeroclaw$(NC)     - 仅构建 ZeroClaw (release)"
	@echo "  $(GREEN)zeroclaw-debug$(NC) - 仅构建 ZeroClaw (debug)"
	@echo ""
	@echo "$(BLUE)工具:$(NC)"
	@echo "  $(GREEN)setup$(NC)        - 设置开发环境"
	@echo "  $(GREEN)lint$(NC)         - 运行代码检查"
	@echo "  $(GREEN)format$(NC)       - 格式化代码"
	@echo "  $(GREEN)doctor$(NC)       - 运行系统诊断"

# 创建构建目录
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(BIN_DIR)
	@mkdir -p $(LIB_DIR)

# 显示项目状态
status:
	@echo "$(YELLOW)=== $(PROJECT_NAME) 项目状态 ===$(NC)"
	@echo "$(BLUE)项目根目录:$(NC) $(CAGENT_DIR)"
	@echo "$(BLUE)CClaw 目录:$(NC) $(CCLAW_DIR)"
	@echo "$(BLUE)ZeroClaw 目录:$(NC) $(ZEROCLAW_DIR)"
	@echo "$(BLUE)构建目录:$(NC) $(BUILD_DIR)"
	@echo ""
	@echo "$(YELLOW)依赖检查:$(NC)"
	@which clang >/dev/null 2>&1 && echo "$(GREEN)✓$(NC) clang 已安装" || echo "$(RED)✗$(NC) clang 未安装"
	@which cargo >/dev/null 2>&1 && echo "$(GREEN)✓$(NC) cargo 已安装" || echo "$(RED)✗$(NC) cargo 未安装"
	@which make >/dev/null 2>&1 && echo "$(GREEN)✓$(NC) make 已安装" || echo "$(RED)✗$(NC) make 未安装"
	@echo ""
	@echo "$(YELLOW)构建状态:$(NC)"
	@if [ -f "$(CCLAW_BIN)" ]; then echo "$(GREEN)✓$(NC) CClaw 已构建"; else echo "$(RED)✗$(NC) CClaw 未构建"; fi
	@if [ -f "$(ZEROCLAW_BIN)" ]; then echo "$(GREEN)✓$(NC) ZeroClaw 已构建"; else echo "$(RED)✗$(NC) ZeroClaw 未构建"; fi

# 设置开发环境
setup: $(BUILD_DIR)
	@echo "$(YELLOW)=== 设置开发环境 ===$(NC)"
	@echo "$(BLUE)安装 CClaw 依赖...$(NC)"
	cd $(CCLAW_DIR) && $(MAKE) setup || echo "$(RED)警告: CClaw 依赖安装失败$(NC)"
	@echo "$(BLUE)检查 Rust 工具链...$(NC)"
	@which rustc >/dev/null 2>&1 || curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
	@echo "$(GREEN)✓ 开发环境设置完成$(NC)"

# 默认目标：构建 release 版本
all: cclaw zeroclaw
	@echo "$(GREEN)✓ 所有项目构建完成 (release)$(NC)"

# 构建所有项目 (debug)
debug: cclaw-debug zeroclaw-debug
	@echo "$(GREEN)✓ 所有项目构建完成 (debug)$(NC)"

# 构建 CClaw (release)
cclaw: zeroclaw $(BUILD_DIR)
	@echo "$(YELLOW)=== 构建 CClaw (release) ===$(NC)"
	cd $(CCLAW_DIR) && $(MAKE)
	@mkdir -p $(BIN_DIR)
	@cp -f $(CCLAW_DIR)/bin/cclaw $(BIN_DIR)/cclaw
	@echo "$(GREEN)✓ CClaw 构建完成 -> $(BIN_DIR)/cclaw$(NC)"

# 构建 CClaw (debug)
cclaw-debug: zeroclaw-debug $(BUILD_DIR)
	@echo "$(YELLOW)=== 构建 CClaw (debug) ===$(NC)"
	cd $(CCLAW_DIR) && $(MAKE) debug=1
	@cp -f $(CCLAW_DIR)/bin/cclaw-debug $(BIN_DIR)/cclaw-debug 2>/dev/null || true
	@echo "$(GREEN)✓ CClaw (debug) 构建完成$(NC)"

# 构建 ZeroClaw (release)
zeroclaw: $(BUILD_DIR)
	@echo "$(YELLOW)=== 构建 ZeroClaw (release) ===$(NC)"
	cd $(ZEROCLAW_DIR) && cargo build --release --lib
	@cp -f $(ZEROCLAW_BIN) $(BIN_DIR)/zeroclaw 2>/dev/null || true
	@echo "$(GREEN)✓ ZeroClaw 构建完成$(NC)"

# 构建 ZeroClaw (debug)
zeroclaw-debug: $(BUILD_DIR)
	@echo "$(YELLOW)=== 构建 ZeroClaw (debug) ===$(NC)"
	cd $(ZEROCLAW_DIR) && cargo build
	@cp -f $(ZEROCLAW_DIR)/target/debug/zeroclaw $(BIN_DIR)/zeroclaw-debug 2>/dev/null || true
	@echo "$(GREEN)✓ ZeroClaw (debug) 构建完成$(NC)"

# 运行所有测试
test: test-cclaw test-zeroclaw
	@echo "$(GREEN)✓ 所有测试完成$(NC)"

# 测试 CClaw
test-cclaw:
	@echo "$(YELLOW)=== 测试 CClaw ===$(NC)"
	cd $(CCLAW_DIR) && $(MAKE) test
	@echo "$(GREEN)✓ CClaw 测试完成$(NC)"

# 测试 ZeroClaw
test-zeroclaw:
	@echo "$(YELLOW)=== 测试 ZeroClaw ===$(NC)"
	cd $(ZEROCLAW_DIR) && cargo test
	@echo "$(GREEN)✓ ZeroClaw 测试完成$(NC)"

# 代码检查
lint: lint-cclaw lint-zeroclaw
	@echo "$(GREEN)✓ 代码检查完成$(NC)"

# CClaw 代码检查
lint-cclaw:
	@echo "$(YELLOW)=== CClaw 代码检查 ===$(NC)"
	cd $(CCLAW_DIR) && $(MAKE) lint
	@echo "$(GREEN)✓ CClaw 代码检查完成$(NC)"

# ZeroClaw 代码检查
lint-zeroclaw:
	@echo "$(YELLOW)=== ZeroClaw 代码检查 ===$(NC)"
	cd $(ZEROCLAW_DIR) && cargo clippy
	@echo "$(GREEN)✓ ZeroClaw 代码检查完成$(NC)"

# 代码格式化
format: format-cclaw format-zeroclaw
	@echo "$(GREEN)✓ 代码格式化完成$(NC)"

# CClaw 代码格式化
format-cclaw:
	@echo "$(YELLOW)=== CClaw 代码格式化 ===$(NC)"
	cd $(CCLAW_DIR) && $(MAKE) format
	@echo "$(GREEN)✓ CClaw 代码格式化完成$(NC)"

# ZeroClaw 代码格式化
format-zeroclaw:
	@echo "$(YELLOW)=== ZeroClaw 代码格式化 ===$(NC)"
	cd $(ZEROCLAW_DIR) && cargo fmt
	@echo "$(GREEN)✓ ZeroClaw 代码格式化完成$(NC)"

# 系统诊断
doctor: status
	@echo "$(YELLOW)=== 系统诊断 ===$(NC)"
	@echo "$(BLUE)运行 ZeroClaw 诊断...$(NC)"
	@if [ -f "$(ZEROCLAW_BIN)" ]; then cd $(ZEROCLAW_DIR) && ./target/release/zeroclaw doctor 2>/dev/null || echo "$(YELLOW)ZeroClaw 诊断需要先构建$(NC)"; else echo "$(YELLOW)ZeroClaw 未构建，跳过诊断$(NC)"; fi
	@echo "$(GREEN)✓ 系统诊断完成$(NC)"

# 安装到系统
install: release
	@echo "$(YELLOW)=== 安装到系统 ===$(NC)"
	@if [ -f "$(BIN_DIR)/cclaw" ]; then \
		sudo cp $(BIN_DIR)/cclaw /usr/local/bin/cclaw && \
		echo "$(GREEN)✓ CClaw 安装到 /usr/local/bin/cclaw$(NC)"; \
	else \
		echo "$(RED)✗ CClaw 未找到，请先构建$(NC)"; \
	fi
	@if [ -f "$(BIN_DIR)/zeroclaw" ]; then \
		sudo cp $(BIN_DIR)/zeroclaw /usr/local/bin/zeroclaw && \
		echo "$(GREEN)✓ ZeroClaw 安装到 /usr/local/bin/zeroclaw$(NC)"; \
	else \
		echo "$(RED)✗ ZeroClaw 未找到，请先构建$(NC)"; \
	fi
	@echo "$(GREEN)✓ 安装完成$(NC)"

# 清理所有构建文件
clean:
	@echo "$(YELLOW)=== 清理构建文件 ===$(NC)"
	@echo "$(BLUE)清理 CClaw...$(NC)"
	cd $(CCLAW_DIR) && $(MAKE) clean 2>/dev/null || true
	@echo "$(BLUE)清理 ZeroClaw...$(NC)"
	cd $(ZEROCLAW_DIR) && cargo clean 2>/dev/null || true
	@echo "$(BLUE)清理构建目录...$(NC)"
	rm -rf $(BUILD_DIR)
	@echo "$(GREEN)✓ 清理完成$(NC)"

# 显示版本信息
version:
	@echo "$(PROJECT_NAME) v$(VERSION)"

# 快速验证构建
quick:
	@echo "$(YELLOW)=== 快速验证构建 ===$(NC)"
	@if [ -f "$(CCLAW_BIN)" ]; then \
		echo "$(GREEN)✓ CClaw 已构建$(NC)"; \
		$(CCLAW_BIN) --version 2>/dev/null || echo "$(YELLOW)CClaw 版本检查失败$(NC)"; \
	else \
		echo "$(RED)✗ CClaw 未构建$(NC)"; \
	fi
	@if [ -f "$(ZEROCLAW_BIN)" ]; then \
		echo "$(GREEN)✓ ZeroClaw 已构建$(NC)"; \
		$(ZEROCLAW_BIN) --version 2>/dev/null || echo "$(YELLOW)ZeroClaw 版本检查失败$(NC)"; \
	else \
		echo "$(RED)✗ ZeroClaw 未构建$(NC)"; \
	fi
