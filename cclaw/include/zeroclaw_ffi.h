// zeroclaw_ffi.h - C interface to ZeroClaw Rust agent
// SPDX-License-Identifier: MIT

#ifndef ZEROCLAW_FFI_H
#define ZEROCLAW_FFI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Result codes
typedef enum {
    ZC_OK = 0,
    ZC_ERROR = -1,
    ZC_INVALID_ARG = -2,
    ZC_NOT_INITIALIZED = -3,
    ZC_OUT_OF_MEMORY = -4,
} zc_result_t;

// Opaque handle to agent runtime
typedef struct zc_agent_runtime zc_agent_runtime_t;
typedef struct zc_session_handle zc_session_handle_t;

// Session event types for event-driven TUI integration
typedef enum {
    ZC_EVT_NONE = 0,
    ZC_EVT_THINKING = 1,
    ZC_EVT_ASSISTANT_TEXT = 2,
    ZC_EVT_TOOL_START = 3,
    ZC_EVT_TOOL_END = 4,
    ZC_EVT_ERROR = 5,
    ZC_EVT_TURN_DONE = 6,
    ZC_EVT_CANCELLED = 7,
} zc_event_type_t;

typedef struct {
    zc_event_type_t type;
    uint64_t turn_id;
    const char* role;
    const char* name;
    const char* payload;
    uint64_t ts_ms;
} zc_event_t;

// Initialize ZeroClaw agent runtime
// config_json: JSON configuration string (can be NULL to use defaults)
// workspace_dir: Path to workspace directory (can be NULL)
// out_handle: Output handle to the created runtime
zc_result_t zc_agent_init(
    const char* config_json,
    const char* workspace_dir,
    zc_agent_runtime_t** out_handle
);

// Shutdown and free agent runtime
void zc_agent_shutdown(zc_agent_runtime_t* handle);

// Run single message through agent
// message: User message to process
// provider: Provider name override (can be NULL)
// model: Model name override (can be NULL)
// temperature: Sampling temperature (0.0-2.0)
// out_response: Output response string (must be freed with zc_free_string)
zc_result_t zc_agent_run_single(
    zc_agent_runtime_t* handle,
    const char* message,
    const char* provider,
    const char* model,
    double temperature,
    char** out_response
);

// Run interactive agent loop
// provider: Provider name override (can be NULL)
// model: Model name override (can be NULL)
// temperature: Sampling temperature (0.0-2.0)
zc_result_t zc_agent_run_interactive(
    zc_agent_runtime_t* handle,
    const char* provider,
    const char* model,
    double temperature
);

// Session API (event-driven; additive, does not replace existing APIs)
zc_result_t zc_session_create(
    zc_agent_runtime_t* runtime,
    const char* provider_override,
    const char* model_override,
    double temperature,
    zc_session_handle_t** out_session
);

zc_result_t zc_session_send(
    zc_session_handle_t* session,
    const char* user_message,
    uint64_t* out_turn_id
);

zc_result_t zc_session_poll_event(
    zc_session_handle_t* session,
    zc_event_t* out_event,
    uint32_t timeout_ms
);

zc_result_t zc_session_cancel(
    zc_session_handle_t* session,
    uint64_t turn_id
);

void zc_session_free_event(zc_event_t* event);
void zc_session_destroy(zc_session_handle_t* session);

// Free a string returned by ZeroClaw
void zc_free_string(char* s);

// Get ZeroClaw version string (static string, do not free)
const char* zc_version(void);

// Daemon management
// config_toml: TOML configuration string (can be NULL to use defaults)
// host: Server host (can be NULL for default 127.0.0.1)
// port: Server port
zc_result_t zc_daemon_start(const char* config_toml, const char* host, uint16_t port);

// Stop the daemon
zc_result_t zc_daemon_stop(void);

// Get daemon status as JSON (must be freed with zc_free_string)
// Returns JSON with component status, restart counts, etc.
zc_result_t zc_daemon_status(char** state_json);

// Check if daemon is running
bool zc_daemon_is_running(void);

#ifdef __cplusplus
}
#endif

#endif // ZEROCLAW_FFI_H
