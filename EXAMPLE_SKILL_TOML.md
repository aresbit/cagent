# 技能文件示例

以下是一个SKILL.toml文件的示例，展示了如何将扩展转换为技能格式：

## 示例：git-wt 扩展转换为技能

```toml
[skill]
name = "git-wt"
description = "Git workspace tools for managing multiple repositories"
version = "1.0.0"
author = "CClaw Team"
tags = ["git", "version-control", "workspace"]

[[tools]]
name = "git_status"
description = "Check git status of current repository"
kind = "shell"
command = "git status"

[[tools]]
name = "git_pull_all"
description = "Pull all changes in workspace"
kind = "shell"
command = "find . -name .git -type d | xargs -n1 -I{} dirname {} | xargs -I{} bash -c 'echo \"Pulling {}...\" && cd {} && git pull'"

[[tools]]
name = "git_commit"
description = "Commit changes with message"
kind = "shell"
command = "git commit -m"

[[tools.args]]
key = "message"
value = "Commit message"

[prompts]
setup = """
You are a Git workspace assistant. You can help with:
1. Checking git status
2. Pulling changes from remote
3. Committing changes
4. Managing multiple repositories
"""
```

## 示例：coding-agent 扩展转换为技能

```toml
[skill]
name = "coding-agent"
description = "AI coding assistant for software development"
version = "2.0.0"
author = "CClaw Team"
tags = ["coding", "development", "ai", "assistant"]

[[tools]]
name = "analyze_code"
description = "Analyze code structure and quality"
kind = "builtin"
command = "code_analysis"

[[tools]]
name = "generate_tests"
description = "Generate unit tests for code"
kind = "builtin"
command = "test_generation"

[[tools]]
name = "refactor_code"
description = "Refactor code to improve quality"
kind = "builtin"
command = "code_refactoring"

[prompts]
code_review = """
As a coding assistant, I can help with:
1. Code analysis and review
2. Test generation
3. Code refactoring
4. Debugging assistance
5. Documentation generation

Please provide the code you want me to analyze.
"""

test_generation = """
I will generate unit tests for your code.
Please provide:
1. The code to test
2. Testing framework preference (if any)
3. Any specific test cases you want
"""
```

## 技能文件位置

每个技能应该放在自己的目录中，例如：

```
~/.cclaw/skills/
├── git-wt/
│   └── SKILL.toml
├── coding-agent/
│   └── SKILL.toml
├── agent-browser/
│   └── SKILL.toml
└── ...
```

## 转换步骤

1. 为每个扩展创建目录：`mkdir -p ~/.cclaw/skills/<extension-name>`
2. 在目录中创建 `SKILL.toml` 文件
3. 根据扩展的功能定义工具和提示
4. 可选：将扩展的源代码或资源文件复制到技能目录
5. 测试技能加载：使用技能系统的API加载和测试技能

## 技能系统API

技能系统提供以下主要函数：

```c
// 加载技能目录中的所有技能
err_t skill_load_from_directory(const str_t* dir_path, skill_t*** out_skills, uint32_t* out_count);

// 执行技能工具
err_t skill_execute_tool(const skill_t* skill, const str_t* tool_name, 
                         const str_t* args, tool_result_t* result);

// 获取技能信息
err_t skill_manifest_to_json(const skill_manifest_t* manifest, str_t* out_json);
```

完成转换后，这些扩展就可以作为技能被CClaw系统使用了。