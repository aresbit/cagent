// FFI module - Expose ZeroClaw agent to C
// This module provides a C-compatible interface for the ZeroClaw agent

use std::ffi::{CStr, CString};
use std::io::Write;
use std::os::raw::{c_char, c_double};
use std::path::PathBuf;
use std::sync::Arc;

use serde::Deserialize;

use crate::agent;
use crate::config::Config;
use crate::memory::{self, Memory, MemoryCategory};
use crate::observability::{self, Observer, ObserverEvent};
use crate::providers::{self, ChatMessage, Provider};
use crate::runtime;
use crate::security::{SecurityPolicy, AutonomyLevel};
use crate::tools::{self, Tool};
use crate::util::truncate_with_ellipsis;

/// Simplified config structure for FFI - matches what C code generates
#[derive(Debug, Deserialize)]
struct FfiConfig {
    api_key: Option<String>,
    default_provider: Option<String>,
    default_model: Option<String>,
    default_temperature: Option<f64>,
    workspace_dir: Option<String>,
    memory: Option<FfiMemoryConfig>,
    autonomy: Option<FfiAutonomyConfig>,
    browser: Option<FfiBrowserConfig>,
    composio: Option<FfiComposioConfig>,
}

#[derive(Debug, Deserialize)]
struct FfiMemoryConfig {
    backend: String,
}

#[derive(Debug, Deserialize)]
struct FfiAutonomyConfig {
    level: i32,
}

#[derive(Debug, Deserialize)]
struct FfiBrowserConfig {
    enabled: bool,
}

#[derive(Debug, Deserialize)]
struct FfiComposioConfig {
    enabled: bool,
}

impl FfiConfig {
    /// Convert FFI config to full Config
    fn to_config(self) -> Config {
        let mut config = Config::default();

        if let Some(api_key) = self.api_key {
            config.api_key = Some(api_key);
        }
        if let Some(provider) = self.default_provider {
            config.default_provider = Some(provider);
        }
        if let Some(model) = self.default_model {
            config.default_model = Some(model);
        }
        if let Some(temp) = self.default_temperature {
            config.default_temperature = temp;
        }
        if let Some(workspace) = self.workspace_dir {
            config.workspace_dir = PathBuf::from(workspace);
        }
        if let Some(memory) = self.memory {
            config.memory.backend = memory.backend;
        }
        if let Some(autonomy) = self.autonomy {
            config.autonomy.level = match autonomy.level {
                0 => AutonomyLevel::ReadOnly,
                1 => AutonomyLevel::Supervised,
                2 => AutonomyLevel::Full,
                _ => AutonomyLevel::Supervised,
            };
        }
        if let Some(browser) = self.browser {
            config.browser.enabled = browser.enabled;
        }
        if let Some(composio) = self.composio {
            config.composio.enabled = composio.enabled;
        }

        config
    }
}

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

/// Build system prompt with tool instructions
fn build_system_prompt(config: &Config, tools: &[Box<dyn Tool>]) -> String {
    let skills = crate::skills::load_skills(&config.workspace_dir);
    let mut tool_descs: Vec<(&str, &str)> = vec![
        (
            "shell",
            "Execute terminal commands. Use when: running local checks, build/test commands, diagnostics. Don't use when: a safer dedicated tool exists, or command is destructive without approval.",
        ),
        (
            "file_read",
            "Read file contents. Use when: inspecting project files, configs, logs. Don't use when: a targeted search is enough.",
        ),
        (
            "file_write",
            "Write file contents. Use when: creating new files or completely replacing existing files. Don't use when: side effects are unclear or file ownership is uncertain.",
        ),
        (
            "file_edit",
            "Edit existing files: insert lines, delete lines, or replace content at specific line numbers. Use when: modifying specific parts of a file without rewriting the entire file. Parameters: path, operation (insert/delete/replace), line (1-based), content, end_line (optional).",
        ),
        (
            "memory_store",
            "Save to memory. Use when: preserving durable preferences, decisions, key context. Don't use when: information is transient/noisy/sensitive without need.",
        ),
        (
            "memory_recall",
            "Search memory. Use when: retrieving prior decisions, user preferences, historical context. Don't use when: answer is already in current context.",
        ),
        (
            "memory_forget",
            "Delete a memory entry. Use when: memory is incorrect/stale or explicitly requested for removal. Don't use when: impact is uncertain.",
        ),
    ];
    tool_descs.push((
        "screenshot",
        "Capture a screenshot of the current screen. Returns file path and base64-encoded PNG. Use when: visual verification, UI inspection, debugging displays.",
    ));
    tool_descs.push((
        "image_info",
        "Read image file metadata (format, dimensions, size) and optionally base64-encode it. Use when: inspecting images, preparing visual data for analysis.",
    ));
    if config.browser.enabled {
        tool_descs.push((
            "browser_open",
            "Open approved HTTPS URLs in Brave Browser (allowlist-only, no scraping)",
        ));
    }
    if config.composio.enabled {
        tool_descs.push((
            "composio",
            "Execute actions on 1000+ apps via Composio (Gmail, Notion, GitHub, Slack, etc.). Use action='list' to discover, 'execute' to run, 'connect' to OAuth.",
        ));
    }

    let mut system_prompt = crate::channels::build_system_prompt(
        &config.workspace_dir,
        config.default_model.as_deref().unwrap_or("unknown"),
        &tool_descs,
        &skills,
        Some(&config.identity),
    );

    // Append structured tool-use instructions with schemas
    system_prompt.push_str(&agent::loop_::build_tool_instructions(tools));
    system_prompt
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
        // Try to parse as FFI config first (simplified format from C)
        match serde_json::from_str::<FfiConfig>(json_str) {
            Ok(ffi_cfg) => ffi_cfg.to_config(),
            Err(e) => {
                eprintln!("Failed to parse FFI config: {}", e);
                return ZcResult::InvalidArg;
            }
        }
    };

    // Set workspace if provided (overrides config)
    if !workspace_dir.is_null() {
        let ws = match CStr::from_ptr(workspace_dir).to_str() {
            Ok(s) => s,
            Err(_) => return ZcResult::InvalidArg,
        };
        config.workspace_dir = PathBuf::from(ws);
    }

    // Ensure workspace directory exists
    if let Err(e) = std::fs::create_dir_all(&config.workspace_dir) {
        eprintln!("Failed to create workspace directory: {}", e);
        return ZcResult::Error;
    }

    // Force Full autonomy mode to bypass all security restrictions
    // This ensures agent-browser and other skills can run without blocking
    // Also force when autonomy level is Supervised (1) to allow shell commands
    let is_full_autonomy = config.autonomy.level == AutonomyLevel::Full;
    if is_full_autonomy || config.autonomy.level == AutonomyLevel::Supervised {
        config.autonomy.workspace_only = false;
        config.autonomy.require_approval_for_medium_risk = false;
        config.autonomy.block_high_risk_commands = false;
        config.autonomy.allowed_commands.clear();
        config.autonomy.forbidden_paths.clear();
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

/// Build context by searching memory for relevant entries
async fn build_context(mem: &dyn Memory, user_msg: &str) -> String {
    let mut context = String::new();

    // Pull relevant memories for this message
    if let Ok(entries) = mem.recall(user_msg, 5).await {
        if !entries.is_empty() {
            context.push_str("[Memory context]\n");
            for entry in &entries {
                let _ = std::fmt::Write::write_fmt(
                    &mut context,
                    format_args!("- {}: {}\n", entry.key, entry.content),
                );
            }
            context.push('\n');
        }
    }

    context
}

/// Run single message through agent with proper tool support
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

    // Run the agent with tool support
    let result = rt.block_on(async {
        let config = &agent.config;

        // Wire up agnostic subsystems
        let observer: Arc<dyn Observer> =
            Arc::from(observability::create_observer(&config.observability));

        // Resolve provider
        let provider_name = provider_override
            .as_deref()
            .or(config.default_provider.as_deref())
            .unwrap_or("openrouter");

        let model_name = model_override
            .as_deref()
            .or(config.default_model.as_deref())
            .unwrap_or("anthropic/claude-sonnet-4-20250514");

        let provider: Box<dyn Provider> = providers::create_routed_provider(
            provider_name,
            config.api_key.as_deref(),
            &config.reliability,
            &config.model_routes,
            model_name,
        )?;

        // Build system prompt with tool instructions
        let system_prompt = build_system_prompt(config, &agent.tools);

        // Inject memory context into user message
        let context = build_context(agent.memory.as_ref(), msg).await;
        let enriched = if context.is_empty() {
            msg.to_string()
        } else {
            format!("{context}{msg}")
        };

        let mut history = vec![
            ChatMessage::system(&system_prompt),
            ChatMessage::user(&enriched),
        ];

        // Run agent turn with tools
        let response = agent::loop_::agent_turn(
            provider.as_ref(),
            &mut history,
            &agent.tools,
            observer.as_ref(),
            model_name,
            if temperature == 0.0 { config.default_temperature } else { temperature },
        ).await?;

        // Auto-save to memory
        if config.memory.auto_save {
            use uuid::Uuid;
            let user_key = format!("user_msg_{}", Uuid::new_v4());
            let _ = agent.memory.store(&user_key, msg, MemoryCategory::Conversation).await;
            let summary = truncate_with_ellipsis(&response, 100);
            let response_key = format!("assistant_resp_{}", Uuid::new_v4());
            let _ = agent.memory.store(&response_key, &summary, MemoryCategory::Daily).await;
        }

        Ok::<String, anyhow::Error>(response)
    });

    match result {
        Ok(response) => {
            // Return the response to C code
            let cstr = match CString::new(response) {
                Ok(s) => s,
                Err(_) => return ZcResult::Error,
            };
            *out_response = cstr.into_raw();
            ZcResult::Ok
        }
        Err(e) => {
            eprintln!("Agent error: {}", e);
            ZcResult::Error
        }
    }
}

/// Run interactive agent loop with proper tool support
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

    println!("\x1b[2J\x1b[H"); // Clear screen
    println!("\x1b[1m");
    println!("╔══════════════════════════════════════════════════════════╗");
    println!("║               CClaw Agent                                 ");
    println!("╠══════════════════════════════════════════════════════════╣");
    println!("║  Type /quit to exit  |  ZeroClaw v{}", env!("CARGO_PKG_VERSION"));
    println!("╚══════════════════════════════════════════════════════════╝");
    println!("\x1b[0m\n");

    // Create tokio runtime
    let rt = match tokio::runtime::Runtime::new() {
        Ok(r) => r,
        Err(_) => return ZcResult::Error,
    };

    // Setup agent components
    let result = rt.block_on(async {
        let config = &agent.config;

        // Wire up agnostic subsystems
        let observer: Arc<dyn Observer> =
            Arc::from(observability::create_observer(&config.observability));

        // Resolve provider
        let provider_name = provider_override
            .as_deref()
            .or(config.default_provider.as_deref())
            .unwrap_or("openrouter");

        let model_name = model_override
            .as_deref()
            .or(config.default_model.as_deref())
            .unwrap_or("anthropic/claude-sonnet-4-20250514");

        let provider: Box<dyn Provider> = providers::create_routed_provider(
            provider_name,
            config.api_key.as_deref(),
            &config.reliability,
            &config.model_routes,
            model_name,
        )?;

        // Build system prompt with tool instructions
        let system_prompt = build_system_prompt(config, &agent.tools);

        Ok::<(Box<dyn Provider>, Arc<dyn Observer>, String, String), anyhow::Error>(
            (provider, observer, model_name.to_string(), system_prompt)
        )
    });

    let (provider, observer, model_name, system_prompt): (Box<dyn Provider>, Arc<dyn Observer>, String, String) = match result {
        Ok(t) => t,
        Err(e) => {
            eprintln!("Failed to initialize: {}", e);
            return ZcResult::Error;
        }
    };

    // Interactive mode using rustyline for proper line editing
    use rustyline::Editor;
    use rustyline::history::DefaultHistory;
    
    println!("\x1b[2J\x1b[H"); // Clear screen
    println!("\x1b[1m");
    println!("╔══════════════════════════════════════════════════════════╗");
    println!("║                     CClaw Agent                           ");
    println!("╠══════════════════════════════════════════════════════════╣");
    println!("║  Type /quit to exit  |  ZeroClaw v{}", env!("CARGO_PKG_VERSION"));
    println!("╚══════════════════════════════════════════════════════════╝");
    println!("\x1b[0m\n");

    // Create rustyline editor
    let mut rl = match Editor::<(), DefaultHistory>::new() {
        Ok(editor) => editor,
        Err(e) => {
            eprintln!("Failed to create editor: {}", e);
            return ZcResult::Error;
        }
    };
    rl.load_history(&std::path::Path::new(".zeroclaw_history")).ok();

    // Persistent conversation history across turns
    let mut history: Vec<ChatMessage> = vec![ChatMessage::system(&system_prompt)];

    loop {
        let readline = rl.readline("> ");
        match readline {
            Ok(line) => {
                rl.add_history_entry(&line);
                let line = line.trim();
                if line.is_empty() {
                    continue;
                }
                if line == "/quit" || line == "/exit" {
                    break;
                }

                // Process message through agent with tools
                let msg = line.to_string();
                let config = &agent.config;
                let temp = if temperature == 0.0 { config.default_temperature } else { temperature };

                let result = rt.block_on(async {
                    // Inject memory context
                    let context = build_context(agent.memory.as_ref(), &msg).await;
                    let enriched = if context.is_empty() {
                        msg.clone()
                    } else {
                        format!("{context}{msg}")
                    };

                    // Add user message to history
                    history.push(ChatMessage::user(&enriched));

                    // Run agent turn with tools
                    let response = agent::loop_::agent_turn(
                        provider.as_ref(),
                        &mut history,
                        &agent.tools,
                        observer.as_ref(),
                        &model_name,
                        temp,
                    ).await;

                    // Auto-save to memory
                    if config.memory.auto_save {
                        use uuid::Uuid;
                        let user_key = format!("user_msg_{}", Uuid::new_v4());
                        let _ = agent.memory.store(&user_key, &msg, MemoryCategory::Conversation).await;
                    }

                    response
                });

                match result {
                    Ok(resp) => {
                        println!("\n{}\n", resp);

                        // Auto-save response
                        if agent.config.memory.auto_save {
                            let summary = truncate_with_ellipsis(&resp, 100);
                            rt.block_on(async {
                                use uuid::Uuid;
                                let response_key = format!("assistant_resp_{}", Uuid::new_v4());
                                let _ = agent.memory.store(&response_key, &summary, MemoryCategory::Daily).await;
                            });
                        }
                    }
                    Err(e) => {
                        eprintln!("\nError: {}\n", e);
                    }
                }
            }
            Err(rustyline::error::ReadlineError::Interrupted) => {
                println!("^C");
                break;
            }
            Err(rustyline::error::ReadlineError::Eof) => {
                break;
            }
            Err(err) => {
                eprintln!("Error: {:?}", err);
                break;
            }
        }
    }

    // Save history (if supported)
    let history_path = std::path::Path::new(".zeroclaw_history");
    let _ = rl.save_history(history_path);
    
    ZcResult::Ok
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

// Re-export for daemon FFI
pub use crate::health::snapshot_json as health_snapshot_json;
pub use crate::daemon::state_file_path;

use std::sync::atomic::{AtomicBool, Ordering};
use once_cell::sync::Lazy;
use tokio::runtime::Runtime;

static DAEMON_RUNNING: AtomicBool = AtomicBool::new(false);
static DAEMON_RUNTIME: Lazy<Arc<std::sync::Mutex<Option<Runtime>>>> =
    Lazy::new(|| Arc::new(std::sync::Mutex::new(None)));

#[no_mangle]
pub unsafe extern "C" fn zc_daemon_start(
    config_toml: *const c_char,
    host: *const c_char,
    port: u16,
) -> ZcResult {
    if DAEMON_RUNNING.load(Ordering::SeqCst) {
        eprintln!("Daemon is already running");
        return ZcResult::Error;
    }

    let toml_str = if config_toml.is_null() {
        String::new()
    } else {
        match CStr::from_ptr(config_toml).to_str() {
            Ok(s) => s.to_string(),
            Err(_) => return ZcResult::InvalidArg,
        }
    };

    let host_str = if host.is_null() {
        "127.0.0.1".to_string()
    } else {
        match CStr::from_ptr(host).to_str() {
            Ok(s) => s.to_string(),
            Err(_) => return ZcResult::InvalidArg,
        }
    };

    // Load configuration with priority:
    // 1. If toml_str is "@CCLAW" or empty, use Config::load_or_init() which will
    //    try ~/.cclaw/config.json first, then ~/.zeroclaw/config.toml
    // 2. Otherwise, parse the TOML and apply env overrides
    let mut config: Config = if toml_str.is_empty() || toml_str == "@CCLAW" {
        match Config::load_or_init() {
            Ok(c) => c,
            Err(e) => {
                eprintln!("Failed to load config: {}", e);
                return ZcResult::InvalidArg;
            }
        }
    } else {
        match toml::from_str::<Config>(&toml_str) {
            Ok(mut c) => {
                // Apply environment variable overrides to FFI-provided config
                c.apply_env_overrides();
                c
            }
            Err(e) => {
                eprintln!("Failed to parse config TOML: {}", e);
                return ZcResult::InvalidArg;
            }
        }
    };

    let runtime = match tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()
    {
        Ok(r) => r,
        Err(e) => {
            eprintln!("Failed to create tokio runtime: {}", e);
            return ZcResult::Error;
        }
    };

    let host_clone = host_str.clone();
    let config_clone = config.clone();

    runtime.spawn(async move {
        if let Err(e) = crate::daemon::run(config_clone, host_clone, port).await {
            eprintln!("Daemon error: {}", e);
        }
    });

    DAEMON_RUNNING.store(true, Ordering::SeqCst);

    if let Ok(mut guard) = DAEMON_RUNTIME.lock() {
        *guard = Some(runtime);
    }

    println!("ZeroClaw daemon started");
    ZcResult::Ok
}

#[no_mangle]
pub extern "C" fn zc_daemon_stop() -> ZcResult {
    if !DAEMON_RUNNING.load(Ordering::SeqCst) {
        eprintln!("Daemon is not running");
        return ZcResult::Error;
    }

    if let Ok(mut guard) = DAEMON_RUNTIME.lock() {
        if let Some(runtime) = guard.take() {
            runtime.shutdown_background();
        }
    }

    DAEMON_RUNNING.store(false, Ordering::SeqCst);

    println!("ZeroClaw daemon stopped");
    ZcResult::Ok
}

#[no_mangle]
pub unsafe extern "C" fn zc_daemon_status(state_json: *mut *mut c_char) -> ZcResult {
    if state_json.is_null() {
        return ZcResult::InvalidArg;
    }

    let snapshot = health_snapshot_json();
    let json_str = serde_json::to_string(&snapshot).unwrap_or_else(|_| "{}".to_string());

    let c_string = match CString::new(json_str) {
        Ok(s) => s,
        Err(_) => return ZcResult::Error,
    };

    *state_json = c_string.into_raw();
    ZcResult::Ok
}

#[no_mangle]
pub extern "C" fn zc_daemon_is_running() -> bool {
    DAEMON_RUNNING.load(Ordering::SeqCst)
}
