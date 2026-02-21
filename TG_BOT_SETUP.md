# CClaw Telegram Bot 配置指南

连接 Telegram 的保姆级教程：
---
步骤 1：启动 Daemon
cd /home/ares/yyscode/cagent/cclaw
./build/bin/cclaw daemon start
步骤 2：获取配对码
Daemon 启动后会显示配对码，类似：
  🔐 PAIRING REQUIRED — use this one-time code:
     ┌──────────────┐
     │  123456  │
     └──────────────┘
     Send: POST /pair with header X-Pairing-Code: 123456
步骤 3：配置 Telegram Bot Token
有两种方式：
方法 A：环境变量（推荐测试用）
export CCLAW_TELEGRAM_BOT_TOKEN="your_bot_token_here"
export CCLAW_TELEGRAM_ALLOWED_USERS="your_telegram_username"
# 然后重启 daemon
./build/bin/cclaw daemon restart
方法 B：配置文件
编辑 ~/.cclaw/config.json：
{
  api_key: your_api_key,
  default_provider: openrouter,
  channels: {
    telegram: {
      bot_token: YOUR_BOT_TOKEN_HERE,
      allowed_users: [your_telegram_username]
    }
  }
}
步骤 4：获取 Bot Token
1. 在 Telegram 搜索 @BotFather
2. 发送 /newbot
3. 设置名称和用户名
4. 复制 token（格式：123456789:ABCdefGHIjklMNOpqrsTUVwxyz）
步骤 5：通过 HTTP 请求完成 Pair
配对是通过 HTTP POST 到 gateway 完成的，不是直接在 Telegram 发消息：
curl -X POST http://localhost:8080/pair \
  -H "X-Pairing-Code: 565495"
成功后会返回 bearer token。
步骤 6：通过 Telegram 发送消息
配对成功后，直接在 Telegram 对 bot 发送消息即可使用。
---
注意：当前 Telegram channel 实现是基于 allowed_users 白名单机制的认证，不是通过 pair 命令。Pair 是用于 API/Webhook 客户端认证的。
如果只需要 Telegram Bot 功能，配置 allowed_users 即可，无需走 pair 流程。

## 配置 TG Bot Token

要在 CClaw daemon 中启用 Telegram Bot，需要在配置文件中添加 `[channels.telegram]` 部分。

## 方法一：通过环境变量（推荐用于测试）

设置环境变量并启动 daemon：

```bash
export CCLAW_TELEGRAM_BOT_TOKEN="your_bot_token_here"
export CCLAW_TELEGRAM_ALLOWED_USERS="user1,user2"  # 可选，限制允许的用户
./build/bin/cclaw daemon start
```

## 方法二：通过配置文件

编辑 `~/.cclaw/config.json` 添加 telegram 配置：

```json
{
  "api_key": "your_api_key",
  "default_provider": "openrouter",
  "default_model": "anthropic/claude-sonnet-4-20250514",
  "autonomy": {
    "level": 2
  },
  "memory": {
    "backend": "markdown"
  },
  "channels": {
    "cli": true,
    "telegram": {
      "bot_token": "YOUR_BOT_TOKEN_HERE",
      "allowed_users": ["your_telegram_username"]
    }
  }
}
```

## 获取 Bot Token

1. 在 Telegram 中搜索 `@BotFather`
2. 发送 `/newbot` 命令
3. 按照提示设置 bot 名称和用户名
4. 复制获得的 token（格式：`123456789:ABCdefGHIjklMNOpqrsTUVwxyz`）

## 运行 Daemon

配置完成后，启动 daemon：

```bash
# 后台模式（推荐）
./build/bin/cclaw daemon start

# 前台模式（用于调试）
./build/bin/cclaw daemon start -f

# 检查状态
./build/bin/cclaw daemon status

# 停止 daemon
./build/bin/cclaw daemon stop
```

## 验证配置

启动 daemon 后，检查日志输出是否包含：
```
Components: gateway, channels, heartbeat, scheduler
```

如果 channels 组件正常工作，Telegram bot 应该能够接收消息。

## 故障排除

1. **Daemon 无法启动**：检查 `~/.cclaw/daemon.pid` 是否存在，如果存在则删除后重试
2. **Bot 无响应**：检查 token 是否正确，使用浏览器访问：
   ```
   https://api.telegram.org/bot<YOUR_TOKEN>/getMe
   ```
3. **权限问题**：确保 `allowed_users` 包含你的 Telegram 用户名（不带 @）

## 技术说明

CClaw 使用 ZeroClaw Rust 库的 FFI 接口来运行 daemon。配置通过 `build_zeroclaw_toml_config()` 函数转换为 TOML 格式传递给 Rust runtime。

支持的 channels：
- CLI（默认启用）
- Telegram（需要配置 bot_token）
- Discord（需要配置 bot_token）
- Slack（需要配置 bot_token）
