// skill.h - Skill system for CClaw (inspired by ZeroClaw skill system)
// SPDX-License-Identifier: MIT

#ifndef CCLAW_CORE_SKILL_H
#define CCLAW_CORE_SKILL_H

#include "core/types.h"
#include "core/error.h"
#include "core/agent.h"

#include <stdint.h>
#include <stdbool.h>

// Forward declarations
typedef struct skill_t skill_t;
typedef struct skill_tool_t skill_tool_t;
typedef struct skill_manifest_t skill_manifest_t;

typedef struct {
    str_t key;
    str_t value;
} skill_arg_t;

// Skill tool definition
struct skill_tool_t {
    str_t name;                // Tool identifier
    str_t description;         // Tool description
    str_t kind;                // "shell", "http", "script", "c_function"
    str_t command;             // Command/URL/function name
    skill_arg_t* args;         // Optional arguments
    uint32_t arg_count;
};

// Skill manifest (similar to ZeroClaw's SKILL.toml)
struct skill_manifest_t {
    str_t name;                // Skill identifier
    str_t description;         // Skill description
    str_t version;             // Semver
    str_t author;              // Optional author
    str_t* tags;               // Skill tags
    uint32_t tag_count;
    skill_tool_t* tools;       // Tools defined by this skill
    uint32_t tool_count;
    str_t* prompts;            // Prompt templates
    uint32_t prompt_count;
    str_t location;            // File location
};

// Skill instance
struct skill_t {
    skill_manifest_t manifest;
    bool loaded;
    uint64_t load_time;
    void* user_data;           // Extension-specific data
};

// ============================================================================
// Skill Registry (inspired by ZeroClaw)
// ============================================================================

err_t skill_registry_init(void);
void skill_registry_shutdown(void);

err_t skill_load(const str_t* path, skill_t** out_skill);
err_t skill_unload(skill_t* skill);
err_t skill_reload(skill_t* skill);

err_t skill_registry_find(const str_t* name, skill_t** out_skill);
err_t skill_registry_list(skill_t*** out_skills, uint32_t* out_count);

// Load all skills from directory (like ZeroClaw's load_skills_from_directory)
err_t skill_load_from_directory(const str_t* dir_path, skill_t*** out_skills, uint32_t* out_count);

// ============================================================================
// Skill Manifest Operations
// ============================================================================

err_t skill_manifest_parse_toml(const str_t* toml, skill_manifest_t* out_manifest);
err_t skill_manifest_parse_md(const str_t* md_content, const str_t* skill_name, skill_manifest_t* out_manifest);
void skill_manifest_free(skill_manifest_t* manifest);

err_t skill_manifest_to_json(const skill_manifest_t* manifest, str_t* out_json);
err_t skill_manifest_to_prompt(const skill_manifest_t* manifest, str_t* out_prompt);

// ============================================================================
// Skill Integration with Extensions
// ============================================================================

// Convert a skill tool to an extension tool
err_t skill_tool_to_extension(const skill_tool_t* skill_tool, tool_def_t* out_tool_def);

// Register all tools from a skill as extensions
err_t skill_register_tools(skill_t* skill, const extension_api_t* api);

// Build system prompt from all loaded skills (like ZeroClaw's skills_to_prompt)
err_t skills_to_system_prompt(skill_t** skills, uint32_t skill_count, str_t* out_prompt);

// ============================================================================
// Skill Discovery and Management
// ============================================================================

// Default skill directories
#define SKILL_DIR_DEFAULT ".cclaw/skills"
#define SKILL_DIR_WORKSPACE "workspace/skills"
#define OPEN_SKILLS_DIR "open-skills"

// Skill file names
#define SKILL_FILE_TOML "SKILL.toml"
#define SKILL_FILE_MD "SKILL.md"
#define SKILL_FILE_JSON "skill.json"

// Open skills repository (like ZeroClaw)
#define OPEN_SKILLS_REPO_URL "https://github.com/besoeasy/open-skills"
#define OPEN_SKILLS_SYNC_MARKER ".cclaw-open-skills-sync"
#define OPEN_SKILLS_SYNC_INTERVAL_SECS (60 * 60 * 24 * 7)  // 7 days

// Open skills management (inspired by ZeroClaw)
err_t skill_open_skills_clone(const str_t* target_dir);
err_t skill_open_skills_pull(const str_t* repo_dir);
bool skill_should_sync_open_skills(const str_t* repo_dir);
err_t skill_mark_open_skills_synced(const str_t* repo_dir);

// Get open skills directory
str_t skill_get_open_skills_dir(void);

// Load skills from open-skills repository
err_t skill_load_open_skills(skill_t*** out_skills, uint32_t* out_count);

// ============================================================================
// Skill Execution
// ============================================================================

// Execute a skill tool
err_t skill_execute_tool(const skill_t* skill, const str_t* tool_name, 
                         const str_t* args, tool_result_t* result);

// Execute a skill prompt (integrate into agent context)
err_t skill_integrate_prompt(const skill_t* skill, agent_t* agent);

// ============================================================================
// Constants
// ============================================================================

#define SKILL_MAX_NAME_LEN 64
#define SKILL_MAX_TOOLS 32
#define SKILL_MAX_PROMPTS 16
#define SKILL_MAX_TAGS 8

#endif // CCLAW_CORE_SKILL_H
