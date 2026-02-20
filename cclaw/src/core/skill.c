// skill.c - Skill system implementation for CClaw
// SPDX-License-Identifier: MIT

#include "core/skill.h"
#include "core/alloc.h"
#include "core/error.h"
#include "core/extension.h"
#include "core/tool.h"
#include "core/agent.h"
#include "core/memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

// ============================================================================
// String utilities (internal implementation)
// ============================================================================

static inline void str_free_internal(str_t* s) {
    if (s && s->data) {
        free((void*)s->data);
        s->data = NULL;
        s->len = 0;
    }
}

static inline void str_init_internal(str_t* s) {
    if (s) {
        s->data = NULL;
        s->len = 0;
    }
}

static inline err_t str_copy_internal(str_t* dst, const str_t* src) {
    if (!dst || !src) return ERR_INVALID_ARGUMENT;

    str_free_internal(dst);

    if (str_empty(*src)) {
        dst->data = NULL;
        dst->len = 0;
        return ERR_OK;
    }

    char* data = malloc(src->len + 1);
    if (!data) return ERR_OUT_OF_MEMORY;

    memcpy(data, src->data, src->len);
    data[src->len] = '\0';

    dst->data = data;
    dst->len = src->len;
    return ERR_OK;
}

static inline err_t str_append_internal(str_t* s, const char* text) {
    if (!s || !text) return ERR_INVALID_ARGUMENT;

    size_t text_len = strlen(text);
    if (text_len == 0) return ERR_OK;

    size_t new_len = s->len + text_len;
    char* new_data = realloc((void*)s->data, new_len + 1);
    if (!new_data) return ERR_OUT_OF_MEMORY;

    memcpy(new_data + s->len, text, text_len);
    new_data[new_len] = '\0';

    s->data = new_data;
    s->len = (uint32_t)new_len;
    return ERR_OK;
}

static inline bool str_ends_with_internal(const str_t* s, const char* suffix) {
    if (!s || !suffix) return false;
    size_t suffix_len = strlen(suffix);
    if (suffix_len > s->len) return false;
    return strncmp(s->data + s->len - suffix_len, suffix, suffix_len) == 0;
}

static inline bool str_equals_internal(const str_t* a, const str_t* b) {
    if (!a || !b) return false;
    if (a->len != b->len) return false;
    if (a->data == b->data) return true;
    if (!a->data || !b->data) return false;
    return memcmp(a->data, b->data, a->len) == 0;
}

// ============================================================================
// File utilities (internal implementation)
// ============================================================================

static inline bool file_exists_internal(const str_t* path) {
    if (!path || !path->data) return false;
    struct stat st;
    return stat(path->data, &st) == 0;
}

static inline err_t file_read_all_internal(const str_t* path, str_t* out_content) {
    if (!path || !out_content) return ERR_INVALID_ARGUMENT;

    FILE* f = fopen(path->data, "rb");
    if (!f) return ERR_FILE_NOT_FOUND;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* data = malloc(size + 1);
    if (!data) {
        fclose(f);
        return ERR_OUT_OF_MEMORY;
    }

    size_t read_size = fread(data, 1, size, f);
    fclose(f);

    data[read_size] = '\0';

    out_content->data = data;
    out_content->len = (uint32_t)read_size;
    return ERR_OK;
}

static inline void file_basename_internal(const str_t* path, str_t* out_name) {
    if (!path || !out_name) return;

    str_init_internal(out_name);

    const char* last_slash = strrchr(path->data, '/');
    const char* base = last_slash ? last_slash + 1 : path->data;

    out_name->data = strdup(base);
    out_name->len = (uint32_t)strlen(base);
}

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

    str_free_internal(&tool->name);
    str_free_internal(&tool->description);
    str_free_internal(&tool->kind);
    str_free_internal(&tool->command);

    if (tool->args) {
        for (uint32_t i = 0; i < tool->arg_count; i++) {
            str_free_internal(&tool->args[i].key);
            str_free_internal(&tool->args[i].value);
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
    if (!file_exists_internal(path)) {
        return ERR_FILE_NOT_FOUND;
    }

    // Allocate skill
    skill_t* skill = calloc(1, sizeof(skill_t));
    if (!skill) {
        return ERR_OUT_OF_MEMORY;
    }

    // Try to load file content
    str_t content;
    if (file_read_all_internal(path, &content) != ERR_OK) {
        free(skill);
        return ERR_IO;
    }

    skill_manifest_init(&skill->manifest);

    // Set name from filename
    str_t basename;
    file_basename_internal(path, &basename);
    char* dot = strrchr(basename.data, '.');
    if (dot) *dot = '\0';
    str_append_internal(&skill->manifest.name, basename.data);
    str_free_internal(&basename);

    // Set basic fields
    str_append_internal(&skill->manifest.description, "Skill loaded from ");
    str_append_internal(&skill->manifest.description, path->data);
    str_append_internal(&skill->manifest.version, "0.1.0");

    // Set location
    str_copy_internal(&skill->manifest.location, path);

    str_free_internal(&content);

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
    str_init_internal(&location);
    str_copy_internal(&location, &skill->manifest.location);

    // Unload current
    skill_unload(skill);

    // Reload from location
    err_t err = skill_load(&location, &skill);
    str_free_internal(&location);

    return err;
}

err_t skill_registry_find(const str_t* name, skill_t** out_skill) {
    if (!name || !out_skill) {
        return ERR_INVALID_ARGUMENT;
    }

    for (uint32_t i = 0; i < g_registry.skill_count; i++) {
        if (str_equals_internal(&g_registry.skills[i]->manifest.name, name)) {
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

        // Check extension
        size_t name_len = strlen(entry->d_name);
        bool is_skill_file = false;
        if (name_len > 5 && strcmp(entry->d_name + name_len - 5, ".toml") == 0) {
            is_skill_file = true;
        } else if (name_len > 3 && strcmp(entry->d_name + name_len - 3, ".md") == 0) {
            is_skill_file = true;
        }

        if (is_skill_file) {
            // Build full path
            char full_path[1024];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path->data, entry->d_name);

            str_t path_str = {.data = full_path, .len = (uint32_t)strlen(full_path)};
            skill_t* skill = NULL;
            err_t err = skill_load(&path_str, &skill);
            if (err == ERR_OK && skill) {
                // Add to registry
                skill_registry_ensure_capacity(g_registry.skill_count + 1);
                g_registry.skills[g_registry.skill_count++] = skill;

                // Add to local array
                if (loaded_count >= capacity) {
                    capacity = capacity ? capacity * 2 : 8;
                    skill_t** new_array = realloc(loaded_skills, capacity * sizeof(skill_t*));
                    if (!new_array) {
                        closedir(dir);
                        free(loaded_skills);
                        return ERR_OUT_OF_MEMORY;
                    }
                    loaded_skills = new_array;
                }
                loaded_skills[loaded_count++] = skill;
            }
        }
    }

    closedir(dir);

    *out_skills = loaded_skills;
    *out_count = loaded_count;
    return ERR_OK;
}

void skill_manifest_free(skill_manifest_t* manifest) {
    if (!manifest) return;

    str_free_internal(&manifest->name);
    str_free_internal(&manifest->description);
    str_free_internal(&manifest->version);
    str_free_internal(&manifest->author);
    str_free_internal(&manifest->location);

    if (manifest->tags) {
        for (uint32_t i = 0; i < manifest->tag_count; i++) {
            str_free_internal(&manifest->tags[i]);
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
            str_free_internal(&manifest->prompts[i]);
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

    // TODO: Implement proper TOML parsing
    // For now, just set basic fields
    str_init_internal(&out_manifest->name);
    str_append_internal(&out_manifest->name, "unknown");

    str_init_internal(&out_manifest->description);
    str_append_internal(&out_manifest->description, "Skill loaded from TOML");

    str_init_internal(&out_manifest->version);
    str_append_internal(&out_manifest->version, "0.1.0");

    return ERR_OK;
}

err_t skill_manifest_parse_md(const str_t* md_content, const str_t* skill_name, skill_manifest_t* out_manifest) {
    if (!md_content || !skill_name || !out_manifest) {
        return ERR_INVALID_ARGUMENT;
    }

    skill_manifest_init(out_manifest);

    // Set name from skill_name
    str_init_internal(&out_manifest->name);
    str_copy_internal(&out_manifest->name, skill_name);

    // Set description
    str_init_internal(&out_manifest->description);
    str_append_internal(&out_manifest->description, "Skill loaded from markdown");

    // Default version
    str_init_internal(&out_manifest->version);
    str_append_internal(&out_manifest->version, "0.1.0");

    return ERR_OK;
}

err_t skill_manifest_to_json(const skill_manifest_t* manifest, str_t* out_json) {
    if (!manifest || !out_json) {
        return ERR_INVALID_ARGUMENT;
    }

    str_init_internal(out_json);
    str_append_internal(out_json, "{\n");

    // Name
    str_append_internal(out_json, "  \"name\": \"");
    if (manifest->name.data) str_append_internal(out_json, manifest->name.data);
    str_append_internal(out_json, "\",\n");

    // Description
    str_append_internal(out_json, "  \"description\": \"");
    if (manifest->description.data) str_append_internal(out_json, manifest->description.data);
    str_append_internal(out_json, "\",\n");

    // Version
    str_append_internal(out_json, "  \"version\": \"");
    if (manifest->version.data) str_append_internal(out_json, manifest->version.data);
    str_append_internal(out_json, "\"\n");

    str_append_internal(out_json, "}");

    return ERR_OK;
}

err_t skill_manifest_to_prompt(const skill_manifest_t* manifest, str_t* out_prompt) {
    if (!manifest || !out_prompt) {
        return ERR_INVALID_ARGUMENT;
    }

    str_init_internal(out_prompt);

    // Add skill header
    str_append_internal(out_prompt, "# Skill: ");
    if (manifest->name.data) str_append_internal(out_prompt, manifest->name.data);
    str_append_internal(out_prompt, "\n\n");

    // Add description
    if (manifest->description.data) {
        str_append_internal(out_prompt, manifest->description.data);
        str_append_internal(out_prompt, "\n\n");
    }

    // Add tools section
    if (manifest->tool_count > 0) {
        str_append_internal(out_prompt, "## Available Tools\n\n");
        for (uint32_t i = 0; i < manifest->tool_count; i++) {
            str_append_internal(out_prompt, "### ");
            if (manifest->tools[i].name.data) str_append_internal(out_prompt, manifest->tools[i].name.data);
            str_append_internal(out_prompt, "\n");
            if (manifest->tools[i].description.data) {
                str_append_internal(out_prompt, manifest->tools[i].description.data);
                str_append_internal(out_prompt, "\n\n");
            }
        }
    }

    return ERR_OK;
}

err_t skill_execute_tool(const skill_t* skill, const str_t* tool_name,
                         const str_t* args, tool_result_t* result) {
    if (!skill || !tool_name || !skill->loaded) {
        return ERR_INVALID_ARGUMENT;
    }

    // Find the tool
    skill_tool_t* tool = NULL;
    for (uint32_t i = 0; i < skill->manifest.tool_count; i++) {
        if (str_equals_internal(&skill->manifest.tools[i].name, tool_name)) {
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
        str_init_internal(&result->content);
        str_append_internal(&result->content, "Tool executed: ");
        if (tool_name->data) str_append_internal(&result->content, tool_name->data);
        result->success = true;
    }

    return ERR_OK;
}

err_t skill_get_prompt(const skill_t* skill, uint32_t index, str_t* out_prompt) {
    if (!skill || !out_prompt) {
        return ERR_INVALID_ARGUMENT;
    }

    if (index >= skill->manifest.prompt_count) {
        return ERR_INVALID_ARGUMENT;
    }

    str_copy_internal(out_prompt, &skill->manifest.prompts[index]);
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
        return ERR_CONFIG_INVALID;
    }

    if (skill->manifest.description.len == 0) {
        return ERR_CONFIG_INVALID;
    }

    // Validate tools
    for (uint32_t i = 0; i < skill->manifest.tool_count; i++) {
        skill_tool_t* tool = &skill->manifest.tools[i];

        if (tool->name.len == 0) {
            return ERR_CONFIG_INVALID;
        }

        if (tool->description.len == 0) {
            return ERR_CONFIG_INVALID;
        }

        if (tool->kind.len == 0) {
            return ERR_CONFIG_INVALID;
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
        if (str_equals_internal(&g_registry.skills[i]->manifest.name, &skill->manifest.name)) {
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
        if (str_equals_internal(&g_registry.skills[i]->manifest.name, name)) {
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

err_t skill_create_tool(skill_tool_t* tool, const str_t* name, const str_t* description,
                        const str_t* kind, const str_t* command) {
    if (!tool || !name || !description || !kind) {
        return ERR_INVALID_ARGUMENT;
    }

    memset(tool, 0, sizeof(skill_tool_t));

    str_init_internal(&tool->name);
    str_copy_internal(&tool->name, name);

    str_init_internal(&tool->description);
    str_copy_internal(&tool->description, description);

    str_init_internal(&tool->kind);
    str_copy_internal(&tool->kind, kind);

    if (command) {
        str_init_internal(&tool->command);
        str_copy_internal(&tool->command, command);
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

    str_init_internal(&arg->key);
    str_copy_internal(&arg->key, key);

    str_init_internal(&arg->value);
    str_copy_internal(&arg->value, value);

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
    str_init_internal(&manifest->tags[manifest->tag_count]);
    str_copy_internal(&manifest->tags[manifest->tag_count], tag);

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
    str_init_internal(&manifest->prompts[manifest->prompt_count]);
    str_copy_internal(&manifest->prompts[manifest->prompt_count], prompt);

    manifest->prompt_count++;
    return ERR_OK;
}

void skill_print_info(const skill_t* skill) {
    if (!skill) {
        printf("Skill: NULL\n");
        return;
    }

    printf("Skill: %s\n", skill->manifest.name.data ? skill->manifest.name.data : "(unnamed)");
    printf("  Description: %s\n", skill->manifest.description.data ? skill->manifest.description.data : "(none)");
    printf("  Version: %s\n", skill->manifest.version.data ? skill->manifest.version.data : "(none)");

    if (skill->manifest.author.len > 0) {
        printf("  Author: %s\n", skill->manifest.author.data);
    }

    if (skill->manifest.tag_count > 0) {
        printf("  Tags: ");
        for (uint32_t i = 0; i < skill->manifest.tag_count; i++) {
            if (i > 0) printf(", ");
            printf("%s", skill->manifest.tags[i].data ? skill->manifest.tags[i].data : "");
        }
        printf("\n");
    }

    printf("  Tools: %u\n", skill->manifest.tool_count);
    for (uint32_t i = 0; i < skill->manifest.tool_count; i++) {
        printf("    - %s (%s): %s\n",
               skill->manifest.tools[i].name.data ? skill->manifest.tools[i].name.data : "(unnamed)",
               skill->manifest.tools[i].kind.data ? skill->manifest.tools[i].kind.data : "(unknown)",
               skill->manifest.tools[i].description.data ? skill->manifest.tools[i].description.data : "(none)");
    }

    printf("  Prompts: %u\n", skill->manifest.prompt_count);
    printf("  Location: %s\n", skill->manifest.location.data ? skill->manifest.location.data : "(unknown)");
    printf("  Loaded: %s\n", skill->loaded ? "yes" : "no");

    if (skill->loaded) {
        char time_buf[64];
        struct tm* tm_info = localtime(&skill->load_time);
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
        printf("  Load time: %s\n", time_buf);
    }
}

void skill_registry_print_all(void) {
    printf("============================\n");
    printf("Registered Skills (%u)\n", g_registry.skill_count);
    printf("============================\n");

    for (uint32_t i = 0; i < g_registry.skill_count; i++) {
        printf("[%u] ", i + 1);
        skill_print_info(g_registry.skills[i]);
        printf("\n");
    }
}

err_t skill_tool_to_extension(const skill_tool_t* skill_tool, tool_def_t* out_tool_def) {
    if (!skill_tool || !out_tool_def) {
        return ERR_INVALID_ARGUMENT;
    }

    memset(out_tool_def, 0, sizeof(tool_def_t));

    // Copy basic fields
    str_copy_internal(&out_tool_def->name, &skill_tool->name);
    str_copy_internal(&out_tool_def->description, &skill_tool->description);

    // TODO: Build parameters JSON schema from skill_tool args
    str_init_internal(&out_tool_def->parameters);
    str_append_internal(&out_tool_def->parameters, "{}");

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
        // Note: This is a simplified version - actual implementation would
        // depend on how extension_api_t is defined

        // Free tool_def resources
        str_free_internal(&tool_def.name);
        str_free_internal(&tool_def.description);
        str_free_internal(&tool_def.parameters);
    }

    return ERR_OK;
}

err_t skills_to_system_prompt(skill_t** skills, uint32_t skill_count, str_t* out_prompt) {
    if (!skills || !out_prompt) {
        return ERR_INVALID_ARGUMENT;
    }

    str_init_internal(out_prompt);
    str_append_internal(out_prompt, "# Available Skills\n\n");

    for (uint32_t i = 0; i < skill_count; i++) {
        if (!skills[i] || !skills[i]->loaded) {
            continue;
        }

        str_t skill_prompt;
        str_init_internal(&skill_prompt);

        err_t err = skill_manifest_to_prompt(&skills[i]->manifest, &skill_prompt);
        if (err == ERR_OK) {
            if (skill_prompt.data) str_append_internal(out_prompt, skill_prompt.data);
            str_append_internal(out_prompt, "\n---\n\n");
        }

        str_free_internal(&skill_prompt);
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
        return ERR_WRITE_FAILED;
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
    char marker_path[1024];
    snprintf(marker_path, sizeof(marker_path), "%s/%s", repo_dir->data, OPEN_SKILLS_SYNC_MARKER);

    struct stat st;
    if (stat(marker_path, &st) != 0) {
        return true;
    }

    // Check if sync interval has passed
    time_t now = time(NULL);
    time_t last_sync = st.st_mtime;

    return (now - last_sync) > OPEN_SKILLS_SYNC_INTERVAL_SECS;
}

err_t skill_mark_open_skills_synced(const str_t* repo_dir) {
    if (!repo_dir) {
        return ERR_INVALID_ARGUMENT;
    }

    char marker_path[1024];
    snprintf(marker_path, sizeof(marker_path), "%s/%s", repo_dir->data, OPEN_SKILLS_SYNC_MARKER);

    // Create or update marker file
    FILE* f = fopen(marker_path, "w");
    if (!f) {
        return ERR_WRITE_FAILED;
    }

    fprintf(f, "Last sync: %ld\n", (long)time(NULL));
    fclose(f);

    return ERR_OK;
}

str_t skill_get_open_skills_dir(void) {
    str_t dir;
    str_init_internal(&dir);

    // First check environment variable
    char* env_dir = getenv("CCLAW_OPEN_SKILLS_DIR");
    if (env_dir) {
        str_append_internal(&dir, env_dir);
        return dir;
    }

    // Default to workspace directory
    str_append_internal(&dir, ".cclaw/open-skills");
    return dir;
}

err_t skill_load_open_skills(skill_t*** out_skills, uint32_t* out_count) {
    if (!out_skills || !out_count) {
        return ERR_INVALID_ARGUMENT;
    }

    str_t open_skills_dir = skill_get_open_skills_dir();
    err_t err = skill_load_from_directory(&open_skills_dir, out_skills, out_count);
    str_free_internal(&open_skills_dir);

    return err;
}

err_t skill_integrate_prompt(const skill_t* skill, agent_t* agent) {
    if (!skill || !agent || !skill->loaded) {
        return ERR_INVALID_ARGUMENT;
    }

    // TODO: Implement prompt integration with agent
    // For now, just return success
    return ERR_OK;
}
