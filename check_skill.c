#include "cclaw/include/core/skill.h"

int main() {
    // 检查所有声明的函数
    skill_t* skill = NULL;
    skill_t** skills = NULL;
    uint32_t count = 0;
    str_t str;
    str_init(&str);
    
    // 测试函数调用（不实际执行）
    skill_registry_init();
    skill_registry_shutdown();
    
    skill_load(&str, &skill);
    skill_unload(skill);
    skill_reload(skill);
    
    skill_registry_find(&str, &skill);
    skill_registry_list(&skills, &count);
    skill_load_from_directory(&str, &skills, &count);
    
    skill_manifest_parse_toml(&str, NULL);
    skill_manifest_parse_md(&str, &str, NULL);
    skill_manifest_free(NULL);
    skill_manifest_to_json(NULL, &str);
    skill_manifest_to_prompt(NULL, &str);
    
    skill_tool_to_extension(NULL, NULL);
    skill_register_tools(NULL, NULL);
    skills_to_system_prompt(NULL, 0, &str);
    
    skill_open_skills_clone(&str);
    skill_open_skills_pull(&str);
    skill_should_sync_open_skills(&str);
    skill_mark_open_skills_synced(&str);
    skill_get_open_skills_dir();
    skill_load_open_skills(&skills, &count);
    
    skill_execute_tool(NULL, &str, &str, NULL);
    skill_integrate_prompt(NULL, NULL);
    
    str_free(&str);
    return 0;
}