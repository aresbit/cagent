// FFI module - Expose ZeroClaw agent to C
// This module provides a C-compatible interface for the ZeroClaw agent

use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_double};
use std::path::PathBuf;
use std::sync::Arc;
use std::sync::Mutex;
use std::sync::atomic::{AtomicBool, AtomicU64};
use std::collections::VecDeque;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use serde::Deserialize;

use crate::agent;
use crate::config::Config;
use crate::memory::{self, Memory, MemoryCategory};
use crate::observability::{self, Observer};
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
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum ZcResult {
    Ok = 0,
    Error = -1,
    InvalidArg = -2,
    NotInitialized = -3,
    OutOfMemory = -4,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum ZcEventType {
    None = 0,
    Thinking = 1,
    AssistantText = 2,
    ToolStart = 3,
    ToolEnd = 4,
    Error = 5,
    TurnDone = 6,
    Cancelled = 7,
}

#[repr(C)]
pub struct ZcEvent {
    pub type_: ZcEventType,
    pub turn_id: u64,
    pub role: *const c_char,
    pub name: *const c_char,
    pub payload: *const c_char,
    pub ts_ms: u64,
}

struct SessionEvent {
    type_: ZcEventType,
    turn_id: u64,
    role: Option<String>,
    name: Option<String>,
    payload: Option<String>,
    ts_ms: u64,
}

#[repr(C)]
pub struct ZcSessionHandle {
    runtime: *mut AgentRuntime,
    provider_override: Option<String>,
    model_override: Option<String>,
    temperature: f64,
    system_prompt: String,
    next_turn_id: AtomicU64,
    shared: Arc<SessionShared>,
}

struct SessionShared {
    history: Mutex<Vec<ChatMessage>>,
    queue: Mutex<VecDeque<SessionEvent>>,
    running: AtomicBool,
    cancel_requested: AtomicBool,
    active_turn_id: AtomicU64,
}

fn now_unix_ms() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis()
        .try_into()
        .unwrap_or(u64::MAX)
}

fn queue_event(
    queue: &Mutex<VecDeque<SessionEvent>>,
    type_: ZcEventType,
    turn_id: u64,
    role: Option<&str>,
    name: Option<&str>,
    payload: Option<&str>,
) {
    if let Ok(mut q) = queue.lock() {
        q.push_back(SessionEvent {
            type_,
            turn_id,
            role: role.map(std::string::ToString::to_string),
            name: name.map(std::string::ToString::to_string),
            payload: payload.map(std::string::ToString::to_string),
            ts_ms: now_unix_ms(),
        });
    }
}

fn event_string_ptr(value: Option<String>) -> *const c_char {
    match value {
        Some(v) => match CString::new(v) {
            Ok(s) => s.into_raw(),
            Err(_) => std::ptr::null(),
        },
        None => std::ptr::null(),
    }
}

struct FfiTurnSink<'a> {
    shared: Arc<SessionShared>,
    turn_id: u64,
    saw_assistant_text: bool,
    _marker: std::marker::PhantomData<&'a ()>,
}

impl<'a> crate::agent::loop_::AgentTurnEventSink for FfiTurnSink<'a> {
    fn on_text(&mut self, text: &str) {
        if text.is_empty() {
            return;
        }
        if self
            .shared
            .cancel_requested
            .load(std::sync::atomic::Ordering::Relaxed)
        {
            return;
        }
        self.saw_assistant_text = true;
        queue_event(
            &self.shared.queue,
            ZcEventType::AssistantText,
            self.turn_id,
            Some("assistant"),
            None,
            Some(text),
        );
    }

    fn on_tool_start(&mut self, tool_name: &str, arguments: &serde_json::Value) {
        if self
            .shared
            .cancel_requested
            .load(std::sync::atomic::Ordering::Relaxed)
        {
            return;
        }
        queue_event(
            &self.shared.queue,
            ZcEventType::ToolStart,
            self.turn_id,
            Some("system"),
            Some(tool_name),
            Some(&arguments.to_string()),
        );
    }

    fn on_tool_end(&mut self, tool_name: &str, result: &str) {
        if self
            .shared
            .cancel_requested
            .load(std::sync::atomic::Ordering::Relaxed)
        {
            return;
        }
        queue_event(
            &self.shared.queue,
            ZcEventType::ToolEnd,
            self.turn_id,
            Some("system"),
            Some(tool_name),
            Some(result),
        );
    }

    fn should_cancel(&self) -> bool {
        self.shared
            .cancel_requested
            .load(std::sync::atomic::Ordering::Relaxed)
    }
}

/// Create an event-driven session handle for TUI integration.
///
/// # Safety
/// Caller must pass valid pointers for runtime and out_session.
#[no_mangle]
pub unsafe extern "C" fn zc_session_create(
    runtime: *mut AgentRuntime,
    provider_override: *const c_char,
    model_override: *const c_char,
    temperature: c_double,
    out_session: *mut *mut ZcSessionHandle,
) -> ZcResult {
    if runtime.is_null() || out_session.is_null() {
        return ZcResult::InvalidArg;
    }

    let provider_override = if provider_override.is_null() {
        None
    } else {
        match CStr::from_ptr(provider_override).to_str() {
            Ok(s) => Some(s.to_string()),
            Err(_) => return ZcResult::InvalidArg,
        }
    };

    let model_override = if model_override.is_null() {
        None
    } else {
        match CStr::from_ptr(model_override).to_str() {
            Ok(s) => Some(s.to_string()),
            Err(_) => return ZcResult::InvalidArg,
        }
    };

    let system_prompt = build_system_prompt(&(*runtime).config, &(*runtime).tools);
    let shared = Arc::new(SessionShared {
        history: Mutex::new(Vec::new()),
        queue: Mutex::new(VecDeque::new()),
        running: AtomicBool::new(false),
        cancel_requested: AtomicBool::new(false),
        active_turn_id: AtomicU64::new(0),
    });

    let session = Box::new(ZcSessionHandle {
        runtime,
        provider_override,
        model_override,
        temperature,
        system_prompt: system_prompt.clone(),
        next_turn_id: AtomicU64::new(1),
        shared: shared.clone(),
    });

    if let Ok(mut h) = shared.history.lock() {
        h.push(ChatMessage::system(&session.system_prompt));
    }

    *out_session = Box::into_raw(session);
    ZcResult::Ok
}

/// Send a user message into the session.
/// Starts background execution and streams lifecycle/tool/text events into queue.
///
/// # Safety
/// Caller must pass valid pointers.
#[no_mangle]
pub unsafe extern "C" fn zc_session_send(
    session: *mut ZcSessionHandle,
    user_message: *const c_char,
    out_turn_id: *mut u64,
) -> ZcResult {
    if session.is_null() || user_message.is_null() || out_turn_id.is_null() {
        return ZcResult::InvalidArg;
    }

    let message = match CStr::from_ptr(user_message).to_str() {
        Ok(s) => s,
        Err(_) => return ZcResult::InvalidArg,
    };

    let s = &*session;
    if s.runtime.is_null() {
        return ZcResult::NotInitialized;
    }

    if s
        .shared
        .running
        .swap(true, std::sync::atomic::Ordering::AcqRel)
    {
        queue_event(
            &s.shared.queue,
            ZcEventType::Error,
            0,
            Some("system"),
            None,
            Some("A turn is already running in this session"),
        );
        return ZcResult::Error;
    }

    let turn_id = s
        .next_turn_id
        .fetch_add(1, std::sync::atomic::Ordering::Relaxed);
    *out_turn_id = turn_id;
    s.shared
        .active_turn_id
        .store(turn_id, std::sync::atomic::Ordering::Relaxed);
    s.shared
        .cancel_requested
        .store(false, std::sync::atomic::Ordering::Relaxed);

    queue_event(
        &s.shared.queue,
        ZcEventType::Thinking,
        turn_id,
        Some("system"),
        None,
        Some("thinking"),
    );

    let runtime_ptr = s.runtime as usize;
    let provider_override = s.provider_override.clone();
    let model_override = s.model_override.clone();
    let requested_temp = s.temperature;
    let msg_owned = message.to_string();
    let shared = s.shared.clone();

    std::thread::spawn(move || {
        let runtime_ref = unsafe { &*(runtime_ptr as *const AgentRuntime) };
        let config = &runtime_ref.config;
        let temp = if requested_temp == 0.0 {
            config.default_temperature
        } else {
            requested_temp
        };

        let mut history = match shared.history.lock() {
            Ok(h) => h.clone(),
            Err(_) => {
                queue_event(
                    &shared.queue,
                    ZcEventType::Error,
                    turn_id,
                    Some("system"),
                    None,
                    Some("history lock poisoned"),
                );
                shared
                    .running
                    .store(false, std::sync::atomic::Ordering::Release);
                return;
            }
        };

        let rt = match tokio::runtime::Runtime::new() {
            Ok(r) => r,
            Err(_) => {
                queue_event(
                    &shared.queue,
                    ZcEventType::Error,
                    turn_id,
                    Some("system"),
                    None,
                    Some("failed to create tokio runtime"),
                );
                shared
                    .running
                    .store(false, std::sync::atomic::Ordering::Release);
                return;
            }
        };

        let shared_for_async = shared.clone();
        let result = rt.block_on(async {
            let observer: Arc<dyn Observer> =
                Arc::from(observability::create_observer(&config.observability));

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

            let context = build_context(runtime_ref.memory.as_ref(), &msg_owned).await;
            let enriched = if context.is_empty() {
                msg_owned.clone()
            } else {
                format!("{context}{msg_owned}")
            };

            history.push(ChatMessage::user(&enriched));
            let mut sink = FfiTurnSink {
                shared: shared_for_async.clone(),
                turn_id,
                saw_assistant_text: false,
                _marker: std::marker::PhantomData,
            };
            let response = agent::loop_::agent_turn_with_sink(
                provider.as_ref(),
                &mut history,
                &runtime_ref.tools,
                observer.as_ref(),
                model_name,
                temp,
                &mut sink,
            )
            .await?;

            if config.memory.auto_save {
                let user_key = format!("user_msg_{}", uuid::Uuid::new_v4());
                let _ = runtime_ref
                    .memory
                    .store(&user_key, &msg_owned, MemoryCategory::Conversation)
                    .await;
                let summary = truncate_with_ellipsis(&response, 100);
                let response_key = format!("assistant_resp_{}", uuid::Uuid::new_v4());
                let _ = runtime_ref
                    .memory
                    .store(&response_key, &summary, MemoryCategory::Daily)
                    .await;
            }

            Ok::<(String, bool), anyhow::Error>((response, sink.saw_assistant_text))
        });

        match result {
            Ok((resp, saw_assistant_text)) => {
                if let Ok(mut h) = shared.history.lock() {
                    *h = history;
                }
                if !shared
                    .cancel_requested
                    .load(std::sync::atomic::Ordering::Relaxed)
                {
                    if !saw_assistant_text {
                        queue_event(
                            &shared.queue,
                            ZcEventType::AssistantText,
                            turn_id,
                            Some("assistant"),
                            None,
                            Some(&resp),
                        );
                    }
                    queue_event(
                        &shared.queue,
                        ZcEventType::TurnDone,
                        turn_id,
                        Some("system"),
                        None,
                        Some("done"),
                    );
                }
            }
            Err(e) => {
                if !shared
                    .cancel_requested
                    .load(std::sync::atomic::Ordering::Relaxed)
                {
                    queue_event(
                        &shared.queue,
                        ZcEventType::Error,
                        turn_id,
                        Some("system"),
                        None,
                        Some(&e.to_string()),
                    );
                }
            }
        }

        shared
            .running
            .store(false, std::sync::atomic::Ordering::Release);
    });

    ZcResult::Ok
}

/// Poll one event from the session queue.
///
/// # Safety
/// Caller must pass valid pointers.
#[no_mangle]
pub unsafe extern "C" fn zc_session_poll_event(
    session: *mut ZcSessionHandle,
    out_event: *mut ZcEvent,
    timeout_ms: u32,
) -> ZcResult {
    if session.is_null() || out_event.is_null() {
        return ZcResult::InvalidArg;
    }

    let s = &*session;
    let deadline = Instant::now() + Duration::from_millis(u64::from(timeout_ms));

    loop {
        if let Ok(mut q) = s.shared.queue.lock() {
            if let Some(ev) = q.pop_front() {
                (*out_event).type_ = ev.type_;
                (*out_event).turn_id = ev.turn_id;
                (*out_event).role = event_string_ptr(ev.role);
                (*out_event).name = event_string_ptr(ev.name);
                (*out_event).payload = event_string_ptr(ev.payload);
                (*out_event).ts_ms = ev.ts_ms;
                return ZcResult::Ok;
            }
        }

        if timeout_ms == 0 || Instant::now() >= deadline {
            (*out_event).type_ = ZcEventType::None;
            (*out_event).turn_id = 0;
            (*out_event).role = std::ptr::null();
            (*out_event).name = std::ptr::null();
            (*out_event).payload = std::ptr::null();
            (*out_event).ts_ms = now_unix_ms();
            return ZcResult::Ok;
        }

        std::thread::sleep(Duration::from_millis(10));
    }
}

/// Cancel an in-flight turn.
/// Skeleton behavior for P0: enqueue cancelled event.
///
/// # Safety
/// Caller must pass a valid session pointer.
#[no_mangle]
pub unsafe extern "C" fn zc_session_cancel(
    session: *mut ZcSessionHandle,
    turn_id: u64,
) -> ZcResult {
    if session.is_null() {
        return ZcResult::InvalidArg;
    }

    let s = &*session;
    s.shared
        .cancel_requested
        .store(true, std::sync::atomic::Ordering::Relaxed);
    let active_turn_id = s
        .shared
        .active_turn_id
        .load(std::sync::atomic::Ordering::Relaxed);
    let effective_turn = if turn_id == 0 { active_turn_id } else { turn_id };
    queue_event(
        &s.shared.queue,
        ZcEventType::Cancelled,
        effective_turn,
        Some("system"),
        None,
        Some("cancelled"),
    );
    ZcResult::Ok
}

/// Free dynamically allocated strings inside an event returned by poll.
///
/// # Safety
/// Caller must pass an event previously returned by zc_session_poll_event.
#[no_mangle]
pub unsafe extern "C" fn zc_session_free_event(event: *mut ZcEvent) {
    if event.is_null() {
        return;
    }

    let ev = &mut *event;
    if !ev.role.is_null() {
        let _ = CString::from_raw(ev.role as *mut c_char);
        ev.role = std::ptr::null();
    }
    if !ev.name.is_null() {
        let _ = CString::from_raw(ev.name as *mut c_char);
        ev.name = std::ptr::null();
    }
    if !ev.payload.is_null() {
        let _ = CString::from_raw(ev.payload as *mut c_char);
        ev.payload = std::ptr::null();
    }
}

/// Destroy session handle and release resources.
///
/// # Safety
/// Caller must pass a valid session pointer returned by zc_session_create.
#[no_mangle]
pub unsafe extern "C" fn zc_session_destroy(session: *mut ZcSessionHandle) {
    if !session.is_null() {
        let s = &*session;
        s.shared
            .cancel_requested
            .store(true, std::sync::atomic::Ordering::Relaxed);
        let wait_deadline = Instant::now() + Duration::from_secs(5);
        while s
            .shared
            .running
            .load(std::sync::atomic::Ordering::Acquire)
            && Instant::now() < wait_deadline
        {
            std::thread::sleep(Duration::from_millis(10));
        }
        let _ = Box::from_raw(session);
    }
}

#[cfg(test)]
mod session_tests {
    use super::*;

    fn poll_one(session: *mut ZcSessionHandle) -> ZcEvent {
        let mut event = ZcEvent {
            type_: ZcEventType::None,
            turn_id: 0,
            role: std::ptr::null(),
            name: std::ptr::null(),
            payload: std::ptr::null(),
            ts_ms: 0,
        };
        let res = unsafe { zc_session_poll_event(session, &mut event, 10) };
        assert_eq!(res, ZcResult::Ok);
        event
    }

    #[test]
    fn session_api_lifecycle() {
        let mut runtime: *mut AgentRuntime = std::ptr::null_mut();
        let init_res = unsafe {
            zc_agent_init(std::ptr::null(), std::ptr::null(), &mut runtime)
        };
        assert_eq!(init_res, ZcResult::Ok);
        assert!(!runtime.is_null());

        let mut session: *mut ZcSessionHandle = std::ptr::null_mut();
        let create_res = unsafe {
            zc_session_create(
                runtime,
                std::ptr::null(),
                std::ptr::null(),
                0.0,
                &mut session,
            )
        };
        assert_eq!(create_res, ZcResult::Ok);
        assert!(!session.is_null());

        let input = CString::new("hello from test").expect("valid cstring");
        let mut turn_id = 0_u64;
        let send_res = unsafe { zc_session_send(session, input.as_ptr(), &mut turn_id) };
        assert_eq!(send_res, ZcResult::Ok);
        assert!(turn_id > 0);
        let cancel_res = unsafe { zc_session_cancel(session, turn_id) };
        assert_eq!(cancel_res, ZcResult::Ok);

        let mut saw_thinking = false;
        let mut saw_cancelled = false;
        for _ in 0..256 {
            let mut ev = poll_one(session);
            match ev.type_ {
                ZcEventType::Thinking => saw_thinking = true,
                ZcEventType::Cancelled => {
                    saw_cancelled = true;
                    unsafe { zc_session_free_event(&mut ev) };
                    break;
                }
                ZcEventType::None => {}
                _ => {}
            }
            unsafe { zc_session_free_event(&mut ev) };
        }
        assert!(saw_thinking);
        assert!(saw_cancelled);

        unsafe {
            zc_session_destroy(session);
            zc_agent_shutdown(runtime);
        }
    }

    #[test]
    fn session_invalid_args() {
        let mut turn_id = 0_u64;
        let msg = CString::new("hi").expect("valid cstring");

        let send_res = unsafe {
            zc_session_send(std::ptr::null_mut(), msg.as_ptr(), &mut turn_id)
        };
        assert_eq!(send_res, ZcResult::InvalidArg);

        let mut ev = ZcEvent {
            type_: ZcEventType::None,
            turn_id: 0,
            role: std::ptr::null(),
            name: std::ptr::null(),
            payload: std::ptr::null(),
            ts_ms: 0,
        };
        let poll_res = unsafe { zc_session_poll_event(std::ptr::null_mut(), &mut ev, 0) };
        assert_eq!(poll_res, ZcResult::InvalidArg);
    }
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

use std::sync::atomic::Ordering;
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
