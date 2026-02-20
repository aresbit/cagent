# 修复工作目录配置方案

## 问题分析

当前CClaw/ZeroClaw项目的工作目录配置过于严格：
1. `autonomy.workspace_only = true` 默认启用
2. 工作目录默认设置为 `~/.cclaw/workspace`
3. 用户无法访问 `~/.cclaw` 目录（配置文件所在目录）

## 解决方案

### 方案1：修改默认配置（推荐）
修改 `cclaw/src/core/config.c` 中的 `config_default` 函数：

```c
// 修改前：
char workspace_path[512];
snprintf(workspace_path, sizeof(workspace_path), "%s/.cclaw/workspace", home);

// 修改后：
char workspace_path[512];
snprintf(workspace_path, sizeof(workspace_path), "%s/.cclaw", home);
```

### 方案2：放宽工作目录限制
修改 `autonomy.workspace_only` 的默认值：

```c
// 修改前：
config->autonomy.workspace_only = true;

// 修改后：
config->autonomy.workspace_only = false;
```

### 方案3：添加额外允许的目录
在 `allowed_workspace_roots` 中添加 `~/.cclaw` 目录：

```c
// 在 config_default 函数中添加：
const char* default_allowed_roots[] = {
    "~/.cclaw",
    // 其他目录...
};
```

## 实施步骤

1. **备份当前配置**：
   ```bash
   cp ~/.cclaw/config.json ~/.cclaw/config.json.backup
   ```

2. **修改源代码**：
   ```bash
   # 编辑配置文件
   nano cclaw/src/core/config.c
   ```

3. **重新编译**：
   ```bash
   cd cclaw
   make clean
   make
   ```

4. **更新配置**：
   ```bash
   ./bin/cclaw onboard --channels-only
   ```

## 配置示例

修改后的配置应该类似：

```json
{
  "autonomy": {
    "workspace_only": false,
    "allowed_commands": ["git", "npm", "cargo", "ls", "cat", "grep"],
    "forbidden_paths": ["/etc", "/root", "/proc", "/sys", "~/.ssh", "~/.gnupg", "~/.aws"]
  },
  "runtime": {
    "docker": {
      "allowed_workspace_roots": ["~/.cclaw"]
    }
  }
}
```

## 安全考虑

放宽工作目录访问时，需要确保：
1. 仍然禁止访问敏感系统目录（`/etc`, `/root`, `/proc`, `/sys`）
2. 仍然禁止访问用户敏感目录（`~/.ssh`, `~/.gnupg`, `~/.aws`）
3. 保持适当的命令白名单
4. 考虑启用Docker沙箱以获得额外安全层

## 测试验证

修复后测试：
1. 检查是否能读取 `~/.cclaw/config.json`
2. 检查是否能写入 `~/.cclaw/workspace` 目录
3. 验证安全限制仍然有效
4. 运行测试套件确保功能正常
```bash
cd cclaw
make test
```