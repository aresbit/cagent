# 如何将扩展移动到技能目录

根据检查，系统中有以下目录结构：

1. `~/.cclaw/extentions/` - 包含多个扩展目录（注意拼写是"extentions"）
2. 需要创建 `~/.cclaw/skills/` 目录
3. 将所有扩展目录复制到技能目录

## 需要手动执行的命令

由于权限限制，您需要手动执行以下命令：

```bash
# 1. 创建技能目录
mkdir -p ~/.cclaw/skills

# 2. 复制所有扩展到技能目录
cp -r ~/.cclaw/extentions/* ~/.cclaw/skills/

# 3. 验证复制结果
ls -la ~/.cclaw/skills/ | wc -l
echo "总共复制了 $(ls ~/.cclaw/skills/ | wc -l) 个技能"
```

## 扩展列表

根据之前的检查，`~/.cclaw/extentions/` 目录中包含以下扩展：

- agent-browser
- blogwatcher
- canvas
- clawdhub
- coding-agent
- discord
- docx
- frontend-slides
- gemini
- gifgrep
- git-wt
- github
- kimi
- matecode
- mermaid-validator
- model-usage
- modern-bash
- ...（还有更多）

## 注意事项

1. **拼写差异**：原始目录是 `extentions`（少了一个's'），而不是 `extensions`
2. **技能格式**：这些扩展可能需要转换为技能格式（添加SKILL.toml或SKILL.md文件）
3. **兼容性**：技能系统使用不同的接口，可能需要调整扩展代码

## 技能系统要求

根据skill.h的定义，技能应该包含以下文件之一：

1. `SKILL.toml` - TOML格式的技能清单
2. `SKILL.md` - Markdown格式的技能文档
3. `skill.json` - JSON格式的技能定义

您可能需要为每个扩展创建相应的技能清单文件。

## 下一步

完成复制后，技能系统应该能够从 `~/.cclaw/skills/` 目录加载这些技能。