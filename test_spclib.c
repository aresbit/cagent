#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 简单测试spclib技能加载
int main() {
    printf("Testing spclib skill loading...\n");
    
    // 检查技能目录
    printf("Checking skill directories...\n");
    
    // 可能的技能目录位置
    const char* skill_dirs[] = {
        "~/.cclaw/skills",
        "~/.cclaw/extensions",
        ".cclaw/skills",
        "skills",
        NULL
    };
    
    for (int i = 0; skill_dirs[i] != NULL; i++) {
        printf("Checking directory: %s\n", skill_dirs[i]);
        
        // 展开~到home目录
        char expanded_path[256];
        if (skill_dirs[i][0] == '~') {
            const char* home = getenv("HOME");
            if (home) {
                snprintf(expanded_path, sizeof(expanded_path), "%s%s", home, skill_dirs[i] + 1);
                printf("  Expanded to: %s\n", expanded_path);
            }
        }
    }
    
    printf("\nTo actually test skill loading, you need to:\n");
    printf("1. Ensure spclib skill exists in ~/.cclaw/skills/ or ~/.cclaw/extensions/\n");
    printf("2. It should have a SKILL.toml, SKILL.md, or skill.json file\n");
    printf("3. Compile and run the skill system test\n");
    
    return 0;
}