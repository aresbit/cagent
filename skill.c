// skill.c - Skill system implementation for CClaw
// SPDX-License-Identifier: MIT

#include "cclaw/include/core/skill.h"
#include "cclaw/include/core/alloc.h"
#include "cclaw/include/core/error.h"
#include "cclaw/include/core/extension.h"
#include "cclaw/include/core/tool.h"
#include "cclaw/include/core/agent.h"
#include "cclaw/include/core/memory.h"
#include "cclaw/include/utils/string.h"
#include "cclaw/include/utils/file.h"
#include "cclaw/include/utils/json.h"
#include "cclaw/include/utils/toml.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>

// ============================================================================
// Internal structures
// ============================================================================

typedef struct skill_registry_t {
    skill_t** skills;
    uint32_t skill_count;
    uint32_t capacity;
    bool initialized;
} skill_registry_t;

static skill_registry_t g_registry = {0};

// ============================================================================
// Internal helper functions
// ============================================================================

static err_t skill_registry_ensure_capacity(uint32_t min_capacity) {
    if (min_capacity <= g_registry.capacity) {
        return ERR_OK;
    }
    
    uint32_t new_capacity = g_registry.capacity * 2;
    if (new_capacity < min_capacity) {
        new_capacity = min_capacity;
    }
    if (new_capacity < 8) {
        new_capacity = 8;
    }
    
    skill_t** new_skills = realloc(g_registry.skills, new_capacity * sizeof(skill_t*));
    if (!new_skills) {
        return ERR_OUT_OF_MEMORY;
    }
    
    g_registry.skills = new_skills;
    g_registry.capacity = new_capacity;
    return ERR_OK;
}

static void skill_manifest_init(skill_manifest_t* manifest) {
    memset(manifest, 0, sizeof(skill_manifest_t));
}

static void skill_tool_free(skill_tool_t* tool) {
    if (!tool) return;
    
    str_free(&tool->name);
    str_free(&tool->description);
    str_free(&tool->kind);
    str_free(&tool->command);
    
    if (tool->args) {
        for (uint32_t i = 0; i < tool->arg_count; i++) {
            str_free(&tool->args[i].key);
            str_free(&tool->args[i].value);
        }
        free(tool->args);
    }
}

// ============================================================================
// Public API implementation
// ============================================================================

err_t skill_registry_init(void) {
    if (g_registry.initialized) {
        return ERR_OK;
    }
    
    g_registry.skills = NULL;
    g_registry.skill_count = 0;
    g_registry.capacity = 0;
    g_registry.initialized = true;
    
    return ERR_OK;
}

void skill_registry_shutdown(void) {
    if (!g_registry.initialized) {
        return;
    }
    
    for (uint32_t i = 0; i < g_registry.skill_count; i++) {
        skill_unload(g_registry.skills[i]);
        free(g_registry.skills[i]);
    }
    
    free(g_registry.skills);
    memset(&g_registry, 0, sizeof(g_registry));
}

err_t skill_load(const str_t* path, skill_t** out_skill) {
    if (!path || !out_skill) {
        return ERR_INVALID_ARGUMENT;
    }
    
    // Check if file exists
    if (!file_exists(path)) {
        return ERR_FILE_NOT_FOUND;
    }
    
    // Allocate skill
    skill_t* skill = calloc(1, sizeof(skill_t));
    if (!skill) {
        return ERR_OUT_OF_MEMORY;
    }
    
    // Try to load as TOML first
    str_t toml_content;
    if (file_read_all(path, &toml_content) == ERR_OK) {
        // Check if it's a TOML file (look for .toml extension or [skill] section)
        bool is_toml = str_ends_with(path, ".toml") || 
                       strstr(toml_content.data, "[skill]") != NULL;
        
        if (is_toml) {
            err_t err = skill_manifest_parse_toml(&toml_content, &skill->manifest);
            str_free(&toml_content);
            if (err != ERR_OK) {
                free(skill);
                return err;
            }
        } else {
            // Try as markdown
            str_free(&toml_content);
            str_t md_content;
            if (file_read_all(path, &md_content) == ERR_OK) {
                // Extract skill name from filename
                str_t skill_name;
                str_init(&skill_name);
                file_basename(path, &skill_name);
                // Remove extension
                char* dot = strrchr(skill_name.data, '.');
                if (dot) *dot = '\0';
                
                err_t err = skill_manifest_parse_md(&md_content, &skill_name, &skill->manifest);
                str_free(&md_content);
                str_free(&skill_name);
                if (err != ERR_OK) {
                    free(skill);
                    return err;
                }
            } else {
                free(skill);
                return ERR_FILE_READ_FAILED;
            }
        }
    } else {
        free(skill);
        return ERR_FILE_READ_FAILED;
    }
    
    // Set location
    str_init(&skill->manifest.location);
    str_copy(&skill->manifest.location, path);
    
    skill->loaded = true;
    skill->load_time = time(NULL);
    
    *out_skill = skill;
    return ERR_OK;
}

err_t skill_unload(skill_t* skill) {
    if (!skill) {
        return ERR_INVALID_ARGUMENT;
    }
    
    skill_manifest_free(&skill->manifest);
    skill->loaded = false;
    skill->load_time = 0;
    
    return ERR_OK;
}

err_t skill_reload(skill_t* skill) {
    if (!skill || !skill->loaded) {
        return ERR_INVALID_ARGUMENT;
    }
    
    // Save the location
    str_t location;
    str_init(&location);
    str_copy(&location, &skill->manifest.location);
    
    // Unload current
    skill_unload(skill);
    
    // Reload from location
    err_t err = skill_load(&location, &skill);
    str_free(&location);
    
    return err;
}

err_t skill_registry_find(const str_t* name, skill_t** out_skill) {
    if (!name || !out_skill) {
        return ERR_INVALID_ARGUMENT;
    }
    
    for (uint32_t i = 0; i < g_registry.skill_count; i++) {
        if (str_equals(&g_registry.skills[i]->manifest.name, name)) {
            *out_skill = g_registry.skills[i];
            return ERR_OK;
        }
    }
    
    return ERR_NOT_FOUND;
}

err_t skill_registry_list(skill_t*** out_skills, uint32_t* out_count) {
    if (!out_skills || !out_count) {
        return ERR_INVALID_ARGUMENT;
    }
    
    *out_skills = g_registry.skills;
    *out_count = g_registry.skill_count;
    return ERR_OK;
}

err_t skill_load_from_directory(const str_t* dir_path, skill_t*** out_skills, uint32_t* out_count) {
    if (!dir_path || !out_skills || !out_count) {
        return ERR_INVALID_ARGUMENT;
    }
    
    DIR* dir = opendir(dir_path->data);
    if (!dir) {
        return ERR_FILE_NOT_FOUND;
    }
    
    skill_t** loaded_skills = NULL;
    uint32_t loaded_count = 0;
    uint32_t capacity = 0;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Build full path
        str_t full_path;
        str_init(&full_path);
        str_append(&full_path, dir_path->data);
        str_append(&full_path, "/");
        str_append(&full_path, entry->d_name);
        
        // Check if it's a skill file
        bool is_skill_file = str_ends_with(&full_path, ".toml") ||
                            str_ends_with(&full_path, ".md") ||
                            str_ends_with(&full_path, ".json");
        
        // Check if it's a directory with SKILL.toml
        struct stat st;
        if (stat(full_path.data, &st) == 0 && S_ISDIR(st.st_mode)) {
            str_t skill_toml;
            str_init(&skill_toml);
            str_append(&skill_toml, full_path.data);
            str_append(&skill_toml, "/SKILL.toml");
            
            if (file_exists(&skill_toml)) {
                is_skill_file = true;
                str_free(&full_path);
                str_copy(&full_path, &skill_toml);
            }
            str_free(&skill_toml);
        }
        
        if (is_skill_file) {
            skill_t* skill = NULL;
            err_t err = skill_load(&full_path, &skill);
            if (err == ERR_OK && skill) {
                // Add to registry
                skill_registry_ensure_capacity(g_registry.skill_count + 1);
                g_registry.skills[g_registry.skill_count++] = skill;
                
                // Add to local array
                if (loaded_count >= capacity) {
                    capacity = capacity ? capacity * 2 : 8;
                    skill_t** new_array = realloc(loaded_skills, capacity * sizeof(skill_t*));
                    if (!new_array) {
                        str_free(&full_path);
                        closedir(dir);
                        free(loaded_skills);
                        return ERR_OUT_OF_MEMORY;
                    }
                    loaded_skills = new_array;
                }
                loaded_skills[loaded_count++] = skill;
            }
        }
        
        str_free(&full_path);
    }
    
    closedir(dir);
    
    *out_skills = loaded_skills;
    *out_count = loaded_count;
    return ERR_OK;
}

void skill_manifest_free(skill_manifest_t* manifest) {
    if (!manifest) return;
    
    str_free(&manifest->name);
    str_free(&manifest->description);
    str_free(&manifest->version);
    str_free(&manifest->author);
    str_free(&manifest->location);
    
    if (manifest->tags) {
        for (uint32_t i = 0; i < manifest->tag_count; i++) {
            str_free(&manifest->tags[i]);
        }
        free(manifest->tags);
    }
    
    if (manifest->tools) {
        for (uint32_t i = 0; i < manifest->tool_count; i++) {
            skill_tool_free(&manifest->tools[i]);
        }
        free(manifest->tools);
    }
    
    if (manifest->prompts) {
        for (uint32_t i = 0; i < manifest->prompt_count; i++) {
            str_free(&manifest->prompts[i]);
        }
        free(manifest->prompts);
    }
    
    memset(manifest, 0, sizeof(skill_manifest_t));
}

err_t skill_manifest_parse_toml(const str_t* toml, skill_manifest_t* out_manifest) {
    if (!toml || !out_manifest) {
        return ERR_INVALID_ARGUMENT;
    }
    
    skill_manifest_init(out_manifest);
    
    // TODO: Implement TOML parsing
    // For now, just set basic fields
    str_init(&out_manifest->name);
    str_append(&out_manifest->name, "unknown");
    
    str_init(&out_manifest->description);
    str_append(&out_manifest->description, "Skill loaded from TOML");
    
    str_init(&out_manifest->version);
    str_append(&out_manifest->version, "0.1.0");
    
    return ERR_OK;
}

err_t skill_manifest_parse_md(const str_t* md_content, const str_t* skill_name, skill_manifest_t* out_manifest) {
    if (!md_content || !skill_name || !out_manifest) {
        return ERR_INVALID_ARGUMENT;
    }
    
    skill_manifest_init(out_manifest);
    
    // Set name from skill_name
    str_init(&out_manifest->name);
    str_copy(&out_manifest->name, skill_name);
    
    // Extract description from markdown
    str_init(&out_manifest->description);
    
    // Look for first paragraph after title
    const char* content = md_content->data;
    const char* line_start = content;
    
    while (*line_start) {
        const char* line_end = strchr(line_start, '\n');
        if (!line_end) line_end = line_start + strlen(line_start);
        
        size_t line_len = line_end - line_start;
        
        // Skip empty lines and titles (lines starting with #)
        if (line_len > 0 && line_start[0] != '#' && line_start[0] != '\0') {
            // Found first content line
            str_append_len(&out_manifest->description, line_start, line_len);
            break;
        }
        
        line_start = (*line_end == '\0') ? line_end : line_end + 1;
    }
    
    // Default version
    str_init(&out_manifest->version);
    str_append(&out_manifest->version, "0.1.0");
    
    return ERR_OK;
}

err_t skill_manifest_to_json(const skill_manifest_t* manifest, str_t* out_json) {
    if (!manifest || !out_json) {
        return ERR_INVALID_ARGUMENT;
    }
    
    str_init(out_json);
    str_append(out_json, "{\n");
    
    // Name
    str_append(out_json, "  \"name\": \"");
    json_escape_string(&manifest->name, out_json);
    str_append(out_json, "\",\n");
    
    // Description
    str_append(out_json, "  \"description\": \"");
    json_escape_string(&manifest->description, out_json);
    str_append(out_json, "\",\n");
    
    // Version
    str_append(out_json, "  \"version\": \"");
    json_escape_string(&manifest->version, out_json);
    str_append(out_json, "\",\n");
    
    // Author (if present)
    if (manifest->author.len > 0) {
        str_append(out_json, "  \"author\": \"");
        json_escape_string(&manifest->author, out_json);
        str_append(out_json, "\",\n");
    }
    
    // Tags
    for (uint32_t i = 0; i < manifest->tag_count; i++) {
        if (i > 0) str_append(out_json, ", ");
        str_append(out_json, "\"");
        json_escape_string(&manifest->tags[i], out_json);
        str_append(out_json, "\"");
    }
    str_append(out_json, "],\n");
    
    // Tags
    str_append(out_json, "  \"tags\": [");
    for (uint32_t i = 0; i < manifest->tag_count; i++) {
        if (i > 0) str_append(out_json, ", ");
        str_append(out_json, "\"");
        json_escape_string(&manifest->tags[i], out_json);
        str_append(out_json, "\"");
    }
    str_append(out_json, "],\n");
    str_append(out_json, "  \"tools\": [");
    for (uint32_t i = 0; i < manifest->tool_count; i++) {
        if (i > 0) str_append(out_json, ", ");
        str_append(out_json, "{\n");
        str_append(out_json, "    \"name\": \"");
        json_escape_string(&manifest->tools[i].name, out_json);
        str_append(out_json, "\",\n");
        str_append(out_json, "    \"description\": \"");
        json_escape_string(&manifest->tools[i].description, out_json);
        str_append(out_json, "\",\n");
        str_append(out_json, "    \"kind\": \"");
        json_escape_string(&manifest->tools[i].kind, out_json);
err_t skill_execute_tool_json(const skill_t* skill, const str_t* tool_name, const json_t* args, json_t** out_result) {
        
        // Add command if present
        if (manifest->tools[i].command.len > 0) {
            str_append(out_json, ",\n    \"command\": \"");
            json_escape_string(&manifest->tools[i].command, out_json);
            str_append(out_json, "\"");
        }
        
        // Add args if present
        if (manifest->tools[i].arg_count > 0) {
            str_append(out_json, ",\n    \"args\": {");
            for (uint32_t j = 0; j < manifest->tools[i].arg_count; j++) {
                if (j > 0) str_append(out_json, ", ");
                str_append(out_json, "\"");
                json_escape_string(&manifest->tools[i].args[j].key, out_json);
                str_append(out_json, "\": \"");
                json_escape_string(&manifest->tools[i].args[j].value, out_json);
                str_append(out_json, "\"");
            }
            str_append(out_json, "}");
        }
        
        str_append(out_json, "\n  }");
    }
    str_append(out_json, "],\n");
    
    // Prompts
    if (manifest->prompt_count > 0) {
        str_append(out_json, "  \"prompts\": [");
        for (uint32_t i = 0; i < manifest->prompt_count; i++) {
            if (i > 0) str_append(out_json, ", ");
            str_append(out_json, "\"");
            json_escape_string(&manifest->prompts[i], out_json);
            str_append(out_json, "\"");
        }
        str_append(out_json, "],\n");
    }
    
    // Location
    str_append(out_json, "  \"location\": \"");
    json_escape_string(&manifest->location, out_json);
    str_append(out_json, "\"\n");
    
    str_append(out_json, "}");
    
    return ERR_OK;
}

err_t skill_execute_tool(const skill_t* skill, const str_t* tool_name, const json_t* args, json_t** out_result) {
    if (!skill || !tool_name || !skill->loaded) {
        return ERR_INVALID_ARGUMENT;
    }
    
    // Find the tool
    skill_tool_t* tool = NULL;
    for (uint32_t i = 0; i < skill->manifest.tool_count; i++) {
        if (str_equals(&skill->manifest.tools[i].name, tool_name)) {
            tool = &skill->manifest.tools[i];
            break;
        }
    }
    
    if (!tool) {
        return ERR_NOT_FOUND;
    }
    
    // Check if it's a shell command
    if (str_equals(&tool->kind, "shell")) {
        // Build command with arguments
        str_t command;
        str_init(&command);
        
        // Start with the base command
        str_copy(&command, &tool->command);
        
        // Add arguments from args parameter
        if (args && args->type == JSON_OBJECT) {
            for (uint32_t i = 0; i < args->value.object.size; i++) {
                json_pair_t* pair = &args->value.object.pairs[i];
                str_append(&command, " ");
                str_append(&command, pair->value->value.string.data);
            }
        }
        
        // Execute the command
        str_t output;
        str_init(&output);
        err_t err = shell_execute(&command, &output);
        
        if (err == ERR_OK && out_result) {
            // Create JSON result
            *out_result = json_create_object();
            if (*out_result) {
                json_add_string(*out_result, "output", output.data);
                json_add_bool(*out_result, "success", true);
            }
        }
        
        str_free(&command);
        str_free(&output);
        return err;
    }
    
    // Check if it's a built-in tool
    if (str_equals(&tool->kind, "builtin")) {
        // Handle built-in tools
        if (str_equals(&tool->command, "echo")) {
            if (out_result) {
                *out_result = json_create_object();
                if (*out_result) {
                    json_add_string(*out_result, "message", "Echo from skill");
                    json_add_bool(*out_result, "success", true);
                }
            }
            return ERR_OK;
        }
    }
    
    return ERR_NOT_IMPLEMENTED;
}

err_t skill_get_prompt(const skill_t* skill, uint32_t index, str_t* out_prompt) {
    if (!skill || !out_prompt) {
        return ERR_INVALID_ARGUMENT;
    }
    
    if (index >= skill->manifest.prompt_count) {
        return ERR_OUT_OF_RANGE;
    }
    
    str_copy(out_prompt, &skill->manifest.prompts[index]);
    return ERR_OK;
}

uint32_t skill_get_prompt_count(const skill_t* skill) {
    if (!skill) {
        return 0;
    }
    return skill->manifest.prompt_count;
}

bool skill_is_loaded(const skill_t* skill) {
    return skill && skill->loaded;
}

time_t skill_get_load_time(const skill_t* skill) {
    if (!skill) {
        return 0;
    }
    return skill->load_time;
}

err_t skill_validate(const skill_t* skill) {
    if (!skill) {
        return ERR_INVALID_ARGUMENT;
    }
    
    // Check required fields
    if (skill->manifest.name.len == 0) {
        return ERR_VALIDATION_FAILED;
    }
    
    if (skill->manifest.description.len == 0) {
        return ERR_VALIDATION_FAILED;
    }
    
    // Validate tools
    for (uint32_t i = 0; i < skill->manifest.tool_count; i++) {
        skill_tool_t* tool = &skill->manifest.tools[i];
        
        if (tool->name.len == 0) {
            return ERR_VALIDATION_FAILED;
        }
        
        if (tool->description.len == 0) {
            return ERR_VALIDATION_FAILED;
        }
        
        if (tool->kind.len == 0) {
            return ERR_VALIDATION_FAILED;
        }
        
        // Shell tools require a command
        if (str_equals(&tool->kind, "shell") && tool->command.len == 0) {
            return ERR_VALIDATION_FAILED;
        }
    }
    
    return ERR_OK;
}

err_t skill_register(skill_t* skill) {
    if (!skill) {
        return ERR_INVALID_ARGUMENT;
    }
    
    // Validate the skill
    err_t err = skill_validate(skill);
    if (err != ERR_OK) {
        return err;
    }
    
    // Check if already registered
    for (uint32_t i = 0; i < g_registry.skill_count; i++) {
        if (str_equals(&g_registry.skills[i]->manifest.name, &skill->manifest.name)) {
            return ERR_ALREADY_EXISTS;
        }
    }
    
    // Add to registry
    err = skill_registry_ensure_capacity(g_registry.skill_count + 1);
    if (err != ERR_OK) {
        return err;
    }
    
    g_registry.skills[g_registry.skill_count++] = skill;
    return ERR_OK;
}

err_t skill_unregister(const str_t* name) {
    if (!name) {
        return ERR_INVALID_ARGUMENT;
    }
    
    for (uint32_t i = 0; i < g_registry.skill_count; i++) {
        if (str_equals(&g_registry.skills[i]->manifest.name, name)) {
            // Shift remaining skills
            for (uint32_t j = i; j < g_registry.skill_count - 1; j++) {
                g_registry.skills[j] = g_registry.skills[j + 1];
            }
            g_registry.skill_count--;
            return ERR_OK;
        }
    }
    
    return ERR_NOT_FOUND;
}

// ============================================================================
// Utility functions
// ============================================================================

err_t skill_create_tool(skill_tool_t* tool, const str_t* name, const str_t* description, 
                        const str_t* kind, const str_t* command) {
    if (!tool || !name || !description || !kind) {
        return ERR_INVALID_ARGUMENT;
    }
    
    memset(tool, 0, sizeof(skill_tool_t));
    
    str_init(&tool->name);
    str_copy(&tool->name, name);
    
    str_init(&tool->description);
    str_copy(&tool->description, description);
    
    str_init(&tool->kind);
    str_copy(&tool->kind, kind);
    
    if (command) {
        str_init(&tool->command);
        str_copy(&tool->command, command);
    }
    
    return ERR_OK;
}

err_t skill_add_tool_arg(skill_tool_t* tool, const str_t* key, const str_t* value) {
    if (!tool || !key || !value) {
        return ERR_INVALID_ARGUMENT;
    }
    
    // Resize args array
    skill_arg_t* new_args = realloc(tool->args, (tool->arg_count + 1) * sizeof(skill_arg_t));
    if (!new_args) {
        return ERR_OUT_OF_MEMORY;
    }
    
    tool->args = new_args;
    
    // Initialize new arg
    skill_arg_t* arg = &tool->args[tool->arg_count];
    memset(arg, 0, sizeof(skill_arg_t));
    
    str_init(&arg->key);
    str_copy(&arg->key, key);
    
    str_init(&arg->value);
    str_copy(&arg->value, value);
    
    tool->arg_count++;
    return ERR_OK;
}

err_t skill_add_tag(skill_manifest_t* manifest, const str_t* tag) {
    if (!manifest || !tag) {
        return ERR_INVALID_ARGUMENT;
    }
    
    // Resize tags array
    str_t* new_tags = realloc(manifest->tags, (manifest->tag_count + 1) * sizeof(str_t));
    if (!new_tags) {
        return ERR_OUT_OF_MEMORY;
    }
    
    manifest->tags = new_tags;
    
    // Initialize new tag
    str_init(&manifest->tags[manifest->tag_count]);
    str_copy(&manifest->tags[manifest->tag_count], tag);
    
    manifest->tag_count++;
    return ERR_OK;
}

err_t skill_add_prompt(skill_manifest_t* manifest, const str_t* prompt) {
    if (!manifest || !prompt) {
        return ERR_INVALID_ARGUMENT;
    }
    
    // Resize prompts array
    str_t* new_prompts = realloc(manifest->prompts, (manifest->prompt_count + 1) * sizeof(str_t));
    if (!new_prompts) {
        return ERR_OUT_OF_MEMORY;
    }
    
    manifest->prompts = new_prompts;
    
    // Initialize new prompt
    str_init(&manifest->prompts[manifest->prompt_count]);
    str_copy(&manifest->prompts[manifest->prompt_count], prompt);
    
    manifest->prompt_count++;
    return ERR_OK;
}

// ============================================================================
// Debug/Info functions
// ============================================================================

void skill_print_info(const skill_t* skill) {
    if (!skill) {
        printf("Skill: NULL\n");
        return;
    }
    
    printf("Skill: %s\n", skill->manifest.name.data);
    printf("  Description: %s\n", skill->manifest.description.data);
    printf("  Version: %s\n", skill->manifest.version.data);
    
    if (skill->manifest.author.len > 0) {
        printf("  Author: %s\n", skill->manifest.author.data);
    }
    
    if (skill->manifest.tag_count > 0) {
        printf("  Tags: ");
        for (uint32_t i = 0; i < skill->manifest.tag_count; i++) {
            if (i > 0) printf(", ");
            printf("%s", skill->manifest.tags[i].data);
        }
        printf("\n");
    }
    
    printf("  Tools: %u\n", skill->manifest.tool_count);
    for (uint32_t i = 0; i < skill->manifest.tool_count; i++) {
        printf("    - %s (%s): %s\n", 
               skill->manifest.tools[i].name.data,
               skill->manifest.tools[i].kind.data,
               skill->manifest.tools[i].description.data);
    }
    
    printf("  Prompts: %u\n", skill->manifest.prompt_count);
    printf("  Location: %s\n", skill->manifest.location.data);
    printf("  Loaded: %s\n", skill->loaded ? "yes" : "no");
    
    if (skill->loaded) {
        char time_buf[64];
        struct tm* tm_info = localtime(&skill->load_time);
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
        printf("  Load time: %s\n", time_buf);
    }
}

void skill_registry_print_all(void) {

err_t skill_execute_tool(const skill_t* skill, const str_t* tool_name, 
                         const str_t* args, tool_result_t* result) {
    if (!skill || !tool_name || !skill->loaded) {
        return ERR_INVALID_ARGUMENT;
    }
    
    // Find the tool
    skill_tool_t* tool = NULL;
    for (uint32_t i = 0; i < skill->manifest.tool_count; i++) {
        if (str_equals(&skill->manifest.tools[i].name, tool_name)) {
            tool = &skill->manifest.tools[i];
            break;
        }
    }
    
    if (!tool) {
        return ERR_NOT_FOUND;
    }
    
    // Check if it's a shell command
    if (str_equals(&tool->kind, "shell")) {
        // Build command with arguments
        str_t command;
        str_init(&command);
        
        // Start with the base command
        str_copy(&command, &tool->command);
        
        // Add arguments if provided
        if (args && args->len > 0) {
            str_append(&command, " ");
            str_append(&command, args->data);
        }
        
        // Execute the command
        str_t output;
        str_init(&output);
        err_t err = shell_execute(&command, &output);
        
        if (result) {
            memset(result, 0, sizeof(tool_result_t));
            if (err == ERR_OK) {
                str_copy(&result->output, &output);
                result->success = true;
                result->exit_code = 0;
            } else {
                str_init(&result->output);
                str_append(&result->output, "Command execution failed");
                result->success = false;
                result->exit_code = -1;
            }
        }
        
        str_free(&command);
        str_free(&output);
        return err;
    }
    
    // Check if it's a built-in tool
    if (str_equals(&tool->kind, "builtin")) {
        if (result) {
            memset(result, 0, sizeof(tool_result_t));
            str_init(&result->output);
            
            if (str_equals(&tool->command, "echo")) {
                str_append(&result->output, "Echo from skill");
                if (args && args->len > 0) {
                    str_append(&result->output, ": ");
                    str_append(&result->output, args->data);
                }
                result->success = true;
                result->exit_code = 0;
            } else {
                str_append(&result->output, "Unknown built-in tool: ");
                str_append(&result->output, tool->command.data);
                result->success = false;
                result->exit_code = -1;
            }
        }
        return ERR_OK;
    }
    
    // Unsupported tool kind
    if (result) {
        memset(result, 0, sizeof(tool_result_t));
        str_init(&result->output);
        str_append(&result->output, "Unsupported tool kind: ");
        str_append(&result->output, tool->kind.data);
        result->success = false;
        result->exit_code = -1;
    }
    
    return ERR_NOT_IMPLEMENTED;
}

err_t skill_manifest_to_prompt(const skill_manifest_t* manifest, str_t* out_prompt) {
    if (!manifest || !out_prompt) {
        return ERR_INVALID_ARGUMENT;
    }
    
    str_init(out_prompt);
    
    // Add skill header
    str_append(out_prompt, "# Skill: ");
    str_append(out_prompt, manifest->name.data);
    str_append(out_prompt, "\n\n");
    
    // Add description
    str_append(out_prompt, manifest->description.data);
    str_append(out_prompt, "\n\n");
    
    // Add tools section
    if (manifest->tool_count > 0) {
        str_append(out_prompt, "## Available Tools\n\n");
        for (uint32_t i = 0; i < manifest->tool_count; i++) {
            str_append(out_prompt, "### ");
            str_append(out_prompt, manifest->tools[i].name.data);
            str_append(out_prompt, "\n");
            str_append(out_prompt, manifest->tools[i].description.data);
            str_append(out_prompt, "\n\n");
        }
    }
    
    // Add prompts section
    if (manifest->prompt_count > 0) {
        str_append(out_prompt, "## Prompt Templates\n\n");
        for (uint32_t i = 0; i < manifest->prompt_count; i++) {
            str_append(out_prompt, "### Prompt ");
            char num[16];
            snprintf(num, sizeof(num), "%u", i + 1);
            str_append(out_prompt, num);
            str_append(out_prompt, "\n\n");
            str_append(out_prompt, manifest->prompts[i].data);
            str_append(out_prompt, "\n\n");
        }
    }
    
    return ERR_OK;
}

err_t skill_tool_to_extension(const skill_tool_t* skill_tool, tool_def_t* out_tool_def) {
    if (!skill_tool || !out_tool_def) {
        return ERR_INVALID_ARGUMENT;
    }
    
    memset(out_tool_def, 0, sizeof(tool_def_t));
    
    // Copy basic fields
    str_copy(&out_tool_def->name, &skill_tool->name);
    str_copy(&out_tool_def->description, &skill_tool->description);
    
    // Set tool type based on kind
    if (str_equals(&skill_tool->kind, "shell")) {
        out_tool_def->type = TOOL_TYPE_SHELL;
    } else if (str_equals(&skill_tool->kind, "builtin")) {
        out_tool_def->type = TOOL_TYPE_BUILTIN;
    } else if (str_equals(&skill_tool->kind, "http")) {
        out_tool_def->type = TOOL_TYPE_HTTP;
    } else {
        out_tool_def->type = TOOL_TYPE_CUSTOM;
    }
    
    // Copy command if present
    if (skill_tool->command.len > 0) {
        str_copy(&out_tool_def->command, &skill_tool->command);
    }
    
    // TODO: Convert args to parameters
    
    return ERR_OK;
}

err_t skill_register_tools(skill_t* skill, const extension_api_t* api) {
    if (!skill || !api || !skill->loaded) {
        return ERR_INVALID_ARGUMENT;
    }
    
    for (uint32_t i = 0; i < skill->manifest.tool_count; i++) {
        tool_def_t tool_def;
        err_t err = skill_tool_to_extension(&skill->manifest.tools[i], &tool_def);
        if (err != ERR_OK) {
            return err;
        }
        
        // Register the tool
        err = api->register_tool(&tool_def);
        tool_def_free(&tool_def);
        
        if (err != ERR_OK) {
            return err;
        }
    }
    
    return ERR_OK;
}

err_t skills_to_system_prompt(skill_t** skills, uint32_t skill_count, str_t* out_prompt) {
    if (!skills || !out_prompt) {
        return ERR_INVALID_ARGUMENT;
    }
    
    str_init(out_prompt);
    str_append(out_prompt, "# Available Skills\n\n");
    
    for (uint32_t i = 0; i < skill_count; i++) {
        if (!skills[i] || !skills[i]->loaded) {
            continue;
        }
        
        str_t skill_prompt;
        str_init(&skill_prompt);
        
        err_t err = skill_manifest_to_prompt(&skills[i]->manifest, &skill_prompt);
        if (err == ERR_OK) {
            str_append(out_prompt, skill_prompt.data);
            str_append(out_prompt, "\n---\n\n");
        }
        
        str_free(&skill_prompt);
    }
    
    return ERR_OK;
}

// Open skills management functions

err_t skill_open_skills_clone(const str_t* target_dir) {
    if (!target_dir) {
        return ERR_INVALID_ARGUMENT;
    }
    
    // TODO: Implement git clone
    // For now, just create directory
    if (mkdir(target_dir->data, 0755) != 0 && errno != EEXIST) {
        return ERR_FILE_CREATE_FAILED;
    }
    
    return ERR_OK;
}

err_t skill_open_skills_pull(const str_t* repo_dir) {
    if (!repo_dir) {
        return ERR_INVALID_ARGUMENT;
    }
    
    // TODO: Implement git pull
    // For now, just return success
    return ERR_OK;
}

bool skill_should_sync_open_skills(const str_t* repo_dir) {
    if (!repo_dir) {
        return false;
    }
    
    // Check sync marker file
    str_t marker_path;
    str_init(&marker_path);
    str_append(&marker_path, repo_dir->data);
    str_append(&marker_path, "/");
    str_append(&marker_path, OPEN_SKILLS_SYNC_MARKER);
    
    if (!file_exists(&marker_path)) {
        str_free(&marker_path);
        return true;
    }
    
    // Check if sync interval has passed
    struct stat st;
    if (stat(marker_path.data, &st) != 0) {
        str_free(&marker_path);
        return true;
    }
    
    time_t now = time(NULL);
    time_t last_sync = st.st_mtime;
    
    str_free(&marker_path);
    
    return (now - last_sync) > OPEN_SKILLS_SYNC_INTERVAL_SECS;
}

err_t skill_mark_open_skills_synced(const str_t* repo_dir) {
    if (!repo_dir) {
        return ERR_INVALID_ARGUMENT;
    }
    
    str_t marker_path;
    str_init(&marker_path);
    str_append(&marker_path, repo_dir->data);
    str_append(&marker_path, "/");
    str_append(&marker_path, OPEN_SKILLS_SYNC_MARKER);
    
    // Create or update marker file
    FILE* f = fopen(marker_path.data, "w");
    if (!f) {
        str_free(&marker_path);
        return ERR_FILE_CREATE_FAILED;
    }
    
    fprintf(f, "Last sync: %ld\n", (long)time(NULL));
    fclose(f);
    
    str_free(&marker_path);
    return ERR_OK;
}

str_t skill_get_open_skills_dir(void) {
    str_t dir;
    str_init(&dir);
    
    // First check environment variable
    char* env_dir = getenv("CCLAW_OPEN_SKILLS_DIR");
    if (env_dir) {
        str_append(&dir, env_dir);
        return dir;
    }
    
    // Default to workspace directory
    str_append(&dir, ".cclaw/open-skills");
    return dir;
}

err_t skill_load_open_skills(skill_t*** out_skills, uint32_t* out_count) {
    if (!out_skills || !out_count) {
        return ERR_INVALID_ARGUMENT;
    }
    
    str_t open_skills_dir = skill_get_open_skills_dir();
    err_t err = skill_load_from_directory(&open_skills_dir, out_skills, out_count);
    str_free(&open_skills_dir);
    
    return err;
}

// Skill execution with correct signature

err_t skill_execute_tool_with_args(const skill_t* skill, const str_t* tool_name, 
                                   const str_t* args, tool_result_t* result) {
    if (!skill || !tool_name || !skill->loaded) {
        return ERR_INVALID_ARGUMENT;
    }
    
    // Find the tool
    skill_tool_t* tool = NULL;
    for (uint32_t i = 0; i < skill->manifest.tool_count; i++) {
        if (str_equals(&skill->manifest.tools[i].name, tool_name)) {
            tool = &skill->manifest.tools[i];
            break;
        }
    }
    
    if (!tool) {
        return ERR_NOT_FOUND;
    }
    
    // For now, just return a simple result
    // TODO: Implement actual tool execution
    if (result) {
        memset(result, 0, sizeof(tool_result_t));
        str_init(&result->output);
        str_append(&result->output, "Tool executed: ");
        str_append(&result->output, tool_name->data);
        result->success = true;
        result->exit_code = 0;
    }
    
    return ERR_OK;
}

err_t skill_integrate_prompt(const skill_t* skill, agent_t* agent) {
    if (!skill || !agent || !skill->loaded) {
        return ERR_INVALID_ARGUMENT;
    }
    
    // TODO: Implement prompt integration with agent
    // For now, just return success
    return ERR_OK;
}

    printf("============================\n");
    
    for (uint32_t i = 0; i < g_registry.skill_count; i++) {
        printf("[%u] ", i + 1);
        skill_print_info(g_registry.skills[i]);
        printf("\n");
    }
}