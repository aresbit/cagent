// zeroclaw_ffi.h - C interface to ZeroClaw Rust agent
// SPDX-License-Identifier: MIT

#ifndef ZEROCLAW_FFI_H
#define ZEROCLAW_FFI_H

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

// Free a string returned by ZeroClaw
void zc_free_string(char* s);

// Get ZeroClaw version string (static string, do not free)
const char* zc_version(void);

#ifdef __cplusplus
}
#endif

#endif // ZEROCLAW_FFI_H
