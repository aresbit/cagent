// FFI module - Expose ZeroClaw agent to C
// This module provides a C-compatible interface for the ZeroClaw agent

use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_double};
use std::path::PathBuf;
use std::sync::Arc;

use crate::agent;
use crate::config::Config;
use crate::memory::{self, Memory};
use crate::observability;
use crate::runtime;
use crate::security::SecurityPolicy;
use crate::tools::{self, Tool};

/// Opaque handle to agent runtime
pub struct AgentRuntime {
    config: Config,
    security: Arc<SecurityPolicy>,
    memory: Arc<dyn Memory>,
    tools: Vec<Box<dyn Tool>>,
}

/// Result codes
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub enum ZcResult {
    Ok = 0,
    Error = -1,
    InvalidArg = -2,
    NotInitialized = -3,
    OutOfMemory = -4,
}

/// Initialize ZeroClaw agent runtime
///
/// # Safety
/// Caller must ensure config_json is a valid null-terminated UTF-8 string or NULL
#[no_mangle]
pub unsafe extern "C" fn zc_agent_init(
    config_json: *const c_char,
    workspace_dir: *const c_char,
    out_handle: *mut *mut AgentRuntime,
) -> ZcResult {
    if out_handle.is_null() {
        return ZcResult::InvalidArg;
    }

    // Load or create config
    let mut config: Config = if config_json.is_null() {
        Config::load_or_init().unwrap_or_default()
    } else {
        let json_str = match CStr::from_ptr(config_json).to_str() {
            Ok(s) => s,
            Err(_) => return ZcResult::InvalidArg,
        };
        match serde_json::from_str(json_str) {
            Ok(cfg) => cfg,
            Err(_) => return ZcResult::InvalidArg,
        }
    };

    // Set workspace if provided
    if !workspace_dir.is_null() {
        let ws = match CStr::from_ptr(workspace_dir).to_str() {
            Ok(s) => s,
            Err(_) => return ZcResult::InvalidArg,
        };
        config.workspace_dir = PathBuf::from(ws);
    }

    let security = Arc::new(SecurityPolicy::from_config(
        &config.autonomy,
        &config.workspace_dir,
    ));

    let memory: Arc<dyn Memory> = match memory::create_memory(
        &config.memory,
        &config.workspace_dir,
        config.api_key.as_deref(),
    ) {
        Ok(m) => Arc::from(m),
        Err(_) => return ZcResult::Error,
    };

    // Create tool registry
    let tools = tools::all_tools_with_runtime(
        &security,
        Arc::new(runtime::NativeRuntime::new()),
        memory.clone(),
        config.composio.api_key.as_deref().filter(|k| config.composio.enabled && !k.is_empty()),
        &config.browser,
    );

    let agent = Box::new(AgentRuntime {
        config,
        security,
        memory,
        tools,
    });

    *out_handle = Box::into_raw(agent);
    ZcResult::Ok
}

/// Shutdown and free agent runtime
///
/// # Safety
/// Caller must ensure handle is a valid pointer returned by zc_agent_init
#[no_mangle]
pub unsafe extern "C" fn zc_agent_shutdown(handle: *mut AgentRuntime) {
    if !handle.is_null() {
        let _ = Box::from_raw(handle);
    }
}

/// Run single message through agent
///
/// # Safety
/// Caller must ensure handle is valid, message is null-terminated UTF-8, and out_response can be written to
#[no_mangle]
pub unsafe extern "C" fn zc_agent_run_single(
    handle: *mut AgentRuntime,
    message: *const c_char,
    provider: *const c_char,
    model: *const c_char,
    temperature: c_double,
    out_response: *mut *mut c_char,
) -> ZcResult {
    if handle.is_null() || message.is_null() || out_response.is_null() {
        return ZcResult::InvalidArg;
    }

    let agent = &*handle;

    let msg = match CStr::from_ptr(message).to_str() {
        Ok(s) => s,
        Err(_) => return ZcResult::InvalidArg,
    };

    let provider_override = if provider.is_null() {
        None
    } else {
        match CStr::from_ptr(provider).to_str() {
            Ok(s) => Some(s.to_string()),
            Err(_) => return ZcResult::InvalidArg,
        }
    };

    let model_override = if model.is_null() {
        None
    } else {
        match CStr::from_ptr(model).to_str() {
            Ok(s) => Some(s.to_string()),
            Err(_) => return ZcResult::InvalidArg,
        }
    };

    // Create tokio runtime for this call
    let rt = match tokio::runtime::Runtime::new() {
        Ok(r) => r,
        Err(_) => return ZcResult::Error,
    };

    // Run the agent
    let result = rt.block_on(async {
        agent::run(
            agent.config.clone(),
            Some(msg.to_string()),
            provider_override,
            model_override,
            temperature,
        )
        .await
    });

    match result {
        Ok(_) => {
            // For now, return empty response - actual response is printed to stdout
            let empty = match CString::new("") {
                Ok(s) => s,
                Err(_) => return ZcResult::Error,
            };
            *out_response = empty.into_raw();
            ZcResult::Ok
        }
        Err(_) => ZcResult::Error,
    }
}

/// Run interactive agent loop
///
/// # Safety
/// Caller must ensure handle is valid
#[no_mangle]
pub unsafe extern "C" fn zc_agent_run_interactive(
    handle: *mut AgentRuntime,
    provider: *const c_char,
    model: *const c_char,
    temperature: c_double,
) -> ZcResult {
    if handle.is_null() {
        return ZcResult::InvalidArg;
    }

    let agent = &*handle;

    let provider_override = if provider.is_null() {
        None
    } else {
        match CStr::from_ptr(provider).to_str() {
            Ok(s) => Some(s.to_string()),
            Err(_) => return ZcResult::InvalidArg,
        }
    };

    let model_override = if model.is_null() {
        None
    } else {
        match CStr::from_ptr(model).to_str() {
            Ok(s) => Some(s.to_string()),
            Err(_) => return ZcResult::InvalidArg,
        }
    };

    // Create tokio runtime
    let rt = match tokio::runtime::Runtime::new() {
        Ok(r) => r,
        Err(_) => return ZcResult::Error,
    };

    // Run the agent
    let result = rt.block_on(async {
        agent::run(
            agent.config.clone(),
            None,
            provider_override,
            model_override,
            temperature,
        )
        .await
    });

    match result {
        Ok(_) => ZcResult::Ok,
        Err(_) => ZcResult::Error,
    }
}

/// Free a string returned by ZeroClaw
///
/// # Safety
/// Caller must ensure s is a valid pointer returned by ZeroClaw
#[no_mangle]
pub unsafe extern "C" fn zc_free_string(s: *mut c_char) {
    if !s.is_null() {
        let _ = CString::from_raw(s);
    }
}

/// Get ZeroClaw version string
///
/// # Safety
/// Returns a static string - caller must not free
#[no_mangle]
pub extern "C" fn zc_version() -> *const c_char {
    static VERSION: &str = concat!(env!("CARGO_PKG_VERSION"), "\0");
    VERSION.as_ptr() as *const c_char
}
