// commands_zeroclaw.c - CLI commands using ZeroClaw Rust agent via FFI
// SPDX-License-Identifier: MIT

#include "commands.h"
#include "zeroclaw_ffi.h"
#include "core/config.h"
#include "core/error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Utility: Build JSON config from cclaw config
static char* build_zeroclaw_config(const config_t* config) {
    if (!config) return NULL;

    // Calculate buffer size needed
    size_t bufsize = 4096;
    char* json = malloc(bufsize);
    if (!json) return NULL;

    // Build minimal JSON config
    snprintf(json, bufsize,
        "{"
        "\"api_key\":\"%.*s\","
        "\"default_provider\":\"%.*s\","
        "\"default_model\":\"%.*s\","
        "\"default_temperature\":%.2f,"
        "\"workspace_dir\":\"%.*s\","
        "\"memory\":{\"backend\":\"%.*s\"},"
        "\"autonomy\":{\"level\":%d},"
        "\"browser\":{\"enabled\":false},"
        "\"composio\":{\"enabled\":false}"
        "}",
        (int)config->api_key.len, config->api_key.data ? config->api_key.data : "",
        (int)config->default_provider.len, config->default_provider.data ? config->default_provider.data : "openrouter",
        (int)config->default_model.len, config->default_model.data ? config->default_model.data : "",
        config->default_temperature,
        (int)config->workspace_dir.len, config->workspace_dir.data ? config->workspace_dir.data : "~/.cclaw",
        (int)config->memory.backend.len, config->memory.backend.data ? config->memory.backend.data : "sqlite",
        config->autonomy.level
    );

    return json;
}

// Agent command using ZeroClaw
err_t cmd_agent_zeroclaw(config_t* config, int argc, char** argv) {
    // Parse arguments
    const char* message = NULL;
    const char* provider_override = NULL;
    const char* model_override = NULL;
    double temperature = config->default_temperature;

    for (int i = 0; i < argc; i++) {
        if ((strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--message") == 0) && i + 1 < argc) {
            message = argv[i + 1];
            i++;
        } else if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--provider") == 0) && i + 1 < argc) {
            provider_override = argv[i + 1];
            i++;
        } else if ((strcmp(argv[i], "--model") == 0) && i + 1 < argc) {
            model_override = argv[i + 1];
            i++;
        } else if ((strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--temperature") == 0) && i + 1 < argc) {
            temperature = atof(argv[i + 1]);
            i++;
        }
    }

    // Build config JSON
    char* config_json = build_zeroclaw_config(config);
    if (!config_json) {
        fprintf(stderr, "Failed to build configuration\n");
        return ERR_OUT_OF_MEMORY;
    }

    // Initialize ZeroClaw runtime - use current directory as primary workspace
    // The agent can also access ~/.cclaw for skills/config
    char cwd[1024];
    const char* workspace = ".";
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        workspace = cwd;
    }
    
    zc_agent_runtime_t* runtime = NULL;
    zc_result_t result = zc_agent_init(
        config_json,
        workspace,
        &runtime
    );

    free(config_json);

    if (result != ZC_OK) {
        fprintf(stderr, "Failed to initialize ZeroClaw agent: %d\n", result);
        return ERR_NOT_INITIALIZED;
    }

    printf("\033[2J\033[H"); // Clear screen
    printf("\033[1m");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║                  CClaw Agent                                    ║\n");
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║  Type /quit to exit  |  CClaw v%s\n", zc_version());
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\033[0m\n");

    if (message) {
        // Single message mode
        char* response = NULL;
        result = zc_agent_run_single(
            runtime,
            message,
            provider_override,
            model_override,
            temperature,
            &response
        );

        if (result == ZC_OK && response) {
            // Print response
            printf("\n%s\n\n", response);
            fflush(stdout);
            zc_free_string(response);
        } else {
            fprintf(stderr, "Agent error: %d, response: %p\n", result, (void*)response);
        }
    } else {
        // Interactive mode
        result = zc_agent_run_interactive(
            runtime,
            provider_override,
            model_override,
            temperature
        );

        if (result != ZC_OK) {
            fprintf(stderr, "Agent error: %d\n", result);
        }
    }

    // Cleanup
    zc_agent_shutdown(runtime);

    printf("\n\033[32m[Session ended. Goodbye!]\033[0m\n");

    return (result == ZC_OK) ? ERR_OK : ERR_FAILED;
}

// Build TOML config for ZeroClaw daemon
char* build_zeroclaw_toml_config(const config_t* config) {
    if (!config) return NULL;

    size_t bufsize = 8192;
    char* toml = malloc(bufsize);
    if (!toml) return NULL;

    const char* workspace = config->workspace_dir.data ? config->workspace_dir.data : "~/.cclaw";
    const char* provider = config->default_provider.data ? config->default_provider.data : "openrouter";
    const char* model = config->default_model.data ? config->default_model.data : "anthropic/claude-sonnet-4-20250514";
    const char* memory_backend = config->memory.backend.data ? config->memory.backend.data : "sqlite";

    // Autonomy level: 0 = readonly, 1 = supervised, 2 = full
    const char* autonomy_level = "supervised";
    if (config->autonomy.level == 0) autonomy_level = "readonly";
    else if (config->autonomy.level == 2) autonomy_level = "full";

    snprintf(toml, bufsize,
        "default_provider = \"%s\"\n"
        "default_model = \"%s\"\n"
        "default_temperature = %.2f\n"
        "\n"
        "[autonomy]\n"
        "level = \"%s\"\n"
        "workspace_only = false\n"
        "allowed_commands = []\n"
        "forbidden_paths = []\n"
        "max_actions_per_hour = 1000\n"
        "max_cost_per_day_cents = 10000\n"
        "require_approval_for_medium_risk = false\n"
        "block_high_risk_commands = false\n"
        "\n"
        "[memory]\n"
        "backend = \"%s\"\n"
        "auto_save = true\n"
        "\n"
        "[browser]\n"
        "enabled = false\n"
        "\n"
        "[composio]\n"
        "enabled = false\n"
        "\n"
        "[heartbeat]\n"
        "enabled = false\n"
        "interval_minutes = 30\n"
        "\n"
        "[observability]\n"
        "backend = \"none\"\n"
        "\n"
        "[reliability]\n"
        "channel_initial_backoff_secs = 1\n"
        "channel_max_backoff_secs = 60\n"
        "\n"
        "model_routes = []\n"
        "\n"
        "[identity]\n"
        "format = \"openclaw\"\n"
        "name = \"CClaw\"\n",
        provider,
        model,
        config->default_temperature,
        autonomy_level,
        memory_backend
    );

    return toml;
}
