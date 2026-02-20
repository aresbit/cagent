# CClaw 开发计划

## 目标
1. **放宽 ZeroClaw 安全模型** - 允许 agent-browser 等技能执行，开放网络访问
2. **Daemon 套皮 ZeroClaw** - 复用 Rust 层成熟的 daemon 实现

---

## 任务 1: 放宽安全模型

### 问题分析
当前 `zeroclaw/src/security/policy.rs` 安全策略过于严格：
- `workspace_only: true` - 只能访问工作空间内文件
- 命令白名单限制太多常用命令
- `curl`, `wget`, `ssh` 被标记为高风险命令
- 中高风险命令需要人工批准

### 修改方案

#### 1.1 修改默认安全策略 (`zeroclaw/src/security/policy.rs`)

```rust
// 修改 Default 实现
impl Default for SecurityPolicy {
    fn default() -> Self {
        Self {
            autonomy: AutonomyLevel::Full,  // 改为 Full
            workspace_dir: PathBuf::from("."),
            workspace_only: false,  // 改为 false - 允许访问任意路径
            allowed_commands: vec![],  // 空列表 = 允许所有命令
            forbidden_paths: vec![
                // 只保留真正危险的系统目录
                "/etc/passwd".into(),
                "/etc/shadow".into(),
                "/etc/ssh".into(),
                "/root".into(),
                "/boot".into(),
                "/proc".into(),
                "/sys".into(),
                "/dev".into(),
                // 敏感配置
                "~/.ssh".into(),
                "~/.gnupg".into(),
                "~/.aws/credentials".into(),
            ],
            max_actions_per_hour: 1000,  // 大幅提高
            max_cost_per_day_cents: 10000,
            require_approval_for_medium_risk: false,  // 不需要批准
            block_high_risk_commands: false,  // 不阻止高风险命令
            tracker: ActionTracker::new(),
        }
    }
}
```

#### 1.2 修改命令风险等级判断

在 `command_risk_level()` 中：
- 移除 `curl`, `wget`, `ssh` 等网络命令的高风险标记
- 只保留真正危险的命令（如 `rm -rf /`, `mkfs`, `dd if=/dev/zero`）

#### 1.3 修改 `is_command_allowed()`

当 `autonomy == AutonomyLevel::Full` 时直接返回 `true`。

### 验证步骤
```bash
cd /home/ares/yyscode/cagent/zeroclaw
cargo build --release
# 测试浏览器技能可以执行
# 测试 curl 命令可以执行
```

---

## 任务 2: Daemon 套皮 ZeroClaw

### 问题分析
当前 cclaw 的 daemon (`cclaw/src/runtime/daemon.c`) 是简单的 C 实现，功能有限。ZeroClaw 的 daemon (`zeroclaw/src/daemon/mod.rs`) 更成熟：
- 支持 gateway、channels、heartbeat、scheduler 多组件
- 有自动重启机制
- 有健康检查和状态记录
- 支持 macOS launchd 和 Linux systemd

### 修改方案

#### 2.1 添加 FFI 接口 (`zeroclaw/src/ffi/mod.rs`)

```rust
// 添加 daemon 相关 FFI 函数

#[no_mangle]
pub extern "C" fn zc_daemon_start(
    config_json: *const c_char,
    host: *const c_char,
    port: u16,
) -> zc_result_t {
    // 解析配置，启动 daemon
}

#[no_mangle]
pub extern "C" fn zc_daemon_stop() -> zc_result_t {
    // 发送停止信号
}

#[no_mangle]
pub extern "C" fn zc_daemon_status(state_json: *mut *mut c_char) -> zc_result_t {
    // 读取 daemon_state.json 返回状态
}
```

#### 2.2 修改 cclaw daemon 命令 (`cclaw/src/cli/commands.c`)

```c
err_t cmd_daemon(config_t* config, int argc, char** argv) {
    // 构建配置 JSON
    char* config_json = build_zeroclaw_config(config);

    if (strcmp(action, "start") == 0) {
        // 调用 ZeroClaw FFI 启动 daemon
        return zc_daemon_start(config_json, "127.0.0.1", 8080);
    } else if (strcmp(action, "stop") == 0) {
        return zc_daemon_stop();
    } else if (strcmp(action, "status") == 0) {
        char* state_json = NULL;
        err_t err = zc_daemon_status(&state_json);
        if (err == ERR_OK && state_json) {
            print_daemon_status(state_json);
            free(state_json);
        }
        return err;
    }
}
```

#### 2.3 添加状态解析和展示

```c
static void print_daemon_status(const char* state_json) {
    // 解析 JSON，显示：
    // - 运行状态
    // - 各组件状态 (gateway, channels, heartbeat, scheduler)
    // - 重启次数
    // - 最后更新时间
}
```

### 保持的 C 层功能
- PID 文件管理（用于检查 daemon 是否运行）
- 命令行参数解析（start/stop/restart/status）

### 复用的 Rust 层功能
- 多组件监督（supervisor）
- 自动重启和退避
- 健康检查和状态记录
- 信号处理（Ctrl+C）

---

## 实施步骤

### 阶段 1: 放宽安全（2-3 小时）
1. 修改 `zeroclaw/src/security/policy.rs` 默认配置
2. 调整命令风险等级判断
3. 编译并测试
4. 验证 agent-browser 技能可以正常工作

### 阶段 2: Daemon FFI 接口（3-4 小时）
1. 在 `zeroclaw/src/ffi/mod.rs` 添加 daemon 相关 FFI 函数
2. 处理异步 runtime 的 FFI 调用问题
3. 编译 zeroclaw 生成新的 `.a` 文件

### 阶段 3: C 层适配（2-3 小时）
1. 修改 `cclaw/src/cli/commands.c` 的 `cmd_daemon`
2. 添加状态解析和展示
3. 添加 FFI 声明到头文件
4. 编译 cclaw

### 阶段 4: 测试（1-2 小时）
1. 测试 daemon start/stop/status
2. 测试各组件正常工作
3. 测试与 agent 的集成

---

## 关键代码位置

### 安全模型
- `zeroclaw/src/security/policy.rs:96-234` - SecurityPolicy Default
- `zeroclaw/src/security/policy.rs:259-378` - command_risk_level()
- `zeroclaw/src/security/policy.rs:424-492` - is_command_allowed()

### Daemon FFI
- `zeroclaw/src/ffi/mod.rs` - 需要添加新函数
- `zeroclaw/src/daemon/mod.rs:11-103` - daemon::run() 主函数

### CClaw Daemon 命令
- `cclaw/src/cli/commands.c:144-232` - cmd_daemon()
- `cclaw/include/zeroclaw_ffi.h` - FFI 声明

---

## 注意事项

1. **安全风险**: 放宽安全模型后，agent 可以执行任意命令。确保在受控环境使用。

2. **Daemon 生命周期**: ZeroClaw daemon 是异步 Rust 代码，FFI 调用需要考虑：
   - 如何优雅地停止（当前用 `tokio::signal::ctrl_c()`）
   - 如何传递停止信号

3. **配置传递**: cclaw 和 zeroclaw 的配置格式不同，需要转换：
   - cclaw: JSON 配置
   - zeroclaw: TOML 配置
   - 当前已有 `build_zeroclaw_config()` 函数处理转换

4. **兼容性**: 确保修改后：
   - 原有 cclaw 独立功能（非套皮部分）仍然工作
   - zeroclaw 的测试仍然通过
