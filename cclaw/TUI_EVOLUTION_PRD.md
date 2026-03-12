# CClaw TUI Evolution PRD (Align with ZeroClaw First)

## 1. Executive Summary

### Problem Statement

`cclaw agent` already uses `zeroclaw` via FFI, but `cclaw tui` still uses the legacy C-side agent path.
This causes capability drift: tool-loop behavior, memory injection, and runtime semantics are inconsistent across entrypoints.

### Proposed Solution

Make `cclaw tui` a pure interaction shell and move all turn execution/session state to `zeroclaw` through a new event-driven FFI session API.
Phase 1 aligns behavior with current `zeroclaw` interactive capabilities; later phases enhance TUI UX without forking core agent logic.

### Success Criteria (measurable)

- Behavior parity: `cclaw agent` vs `cclaw tui` tool-call sequence match rate >= 95% on the same prompt set.
- Responsiveness: first visible runtime event (`thinking` or `tool_start`) in <= 300ms for normal local conditions.
- Reliability: `cancel` works and returns terminal state in >= 99% of long-running turns.
- Stability: no regressions in existing `cclaw agent` and daemon commands.
- Test quality: new FFI session API has unit + integration coverage for success/error/cancel paths.

## 2. User Experience & Functionality

### User Personas

- Terminal-heavy developers using CClaw TUI for long coding sessions.
- Existing `cclaw` users expecting one consistent agent behavior regardless of command.
- Maintainers needing lower cross-language maintenance cost.

### User Stories

- As a TUI user, I want the same tool-use and memory behavior as `cclaw agent`, so outputs are predictable.
- As a TUI user, I want to see execution progress (`thinking`, `tool_start`, `tool_end`, `error`) so I can trust long tasks.
- As a TUI user, I want to interrupt a long turn safely so the interface remains controllable.
- As a maintainer, I want one core execution implementation to reduce duplicated bugs.

### Acceptance Criteria

- TUI message submit path no longer calls C `agent_process_message`; it routes through FFI session API only.
- TUI can render at least these events: `assistant_text`, `tool_start`, `tool_end`, `error`, `turn_done`.
- TUI supports in-flight cancel through FFI call and surfaces final status.
- TUI session switch preserves remote (Rust) session continuity.
- Existing keyboard UX remains functional (`Enter`, history, session switch, quit).

### Non-Goals (for initial alignment release)

- Token-level streaming from provider network responses.
- Replacing current TUI layout system.
- Full cross-channel event unification (Telegram/Slack/etc.) in this milestone.
- Rewriting all C-side runtime modules.

## 3. AI System Requirements

### Tool Requirements

- ZeroClaw tool loop remains source of truth (`agent::loop_::agent_turn` path).
- Observer/event signal from Rust runtime to C TUI via FFI event queue.
- Session lifecycle API exposed in FFI (create/send/poll/cancel/destroy).

### Evaluation Strategy

- Golden prompt suite (20-50 prompts) executed by:
  - `cclaw agent` baseline
  - `cclaw tui` migrated path
- Compare:
  - final text similarity (semantic)
  - tool call names/order
  - terminal status correctness (done/error/cancelled)

## 4. Technical Specifications

### 4.1 Architecture Overview

Current:

- `cmd_agent` -> `cmd_agent_zeroclaw` -> Rust FFI
- `cmd_tui` -> C agent/provider/session -> C `agent_process_message`

Target:

- `cmd_agent` remains on Rust FFI
- `cmd_tui` becomes UI client:
  - Input thread: capture keys and submit messages
  - Worker thread: poll Rust events
  - Renderer: update panels from normalized event model
- Runtime authority for turn execution/session/memory/tool-loop stays in Rust.

### 4.2 New FFI Session API (proposed)

Add to `cclaw/include/zeroclaw_ffi.h` and implement in `zeroclaw/src/ffi/mod.rs`:

```c
typedef struct zc_session_handle zc_session_handle_t;

typedef enum {
    ZC_EVT_NONE = 0,
    ZC_EVT_THINKING,
    ZC_EVT_ASSISTANT_TEXT,
    ZC_EVT_TOOL_START,
    ZC_EVT_TOOL_END,
    ZC_EVT_ERROR,
    ZC_EVT_TURN_DONE,
    ZC_EVT_CANCELLED
} zc_event_type_t;

typedef struct {
    zc_event_type_t type;
    uint64_t turn_id;
    const char* role;      // optional: "assistant"/"system"
    const char* name;      // optional: tool name
    const char* payload;   // text/json payload
    uint64_t ts_ms;
} zc_event_t;

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
```

Notes:

- `payload` is allocated by Rust; C frees with `zc_session_free_event`.
- `zc_session_poll_event` returns `ZC_OK` with `ZC_EVT_NONE` on timeout (non-fatal).
- Threading model: one producer task per turn + bounded queue per session.

### 4.3 Rust-side execution bridge

Refactor minimal internals without behavior change:

- Keep current `agent_turn` as canonical loop.
- Add `agent_turn_with_sink(...)` that emits events at:
  - before provider call (`THINKING`)
  - parsed text output (`ASSISTANT_TEXT`)
  - each tool call start/end
  - error paths
  - final completion
- `agent_turn` can call `agent_turn_with_sink` with a no-op sink to avoid duplication.

### 4.4 C TUI integration changes

Files to modify:

- `cclaw/src/cli/commands.c`
- `cclaw/src/runtime/tui.c`
- `cclaw/include/zeroclaw_ffi.h`

Changes:

- `cmd_tui` creates `zc_agent_runtime_t`, then `zc_session_handle_t`.
- On Enter:
  - send user message via `zc_session_send`
  - append local user bubble immediately
  - switch TUI state to `RUNNING`
- Worker loop polls events and appends chat/status/tool timeline entries.
- `Ctrl+C` or dedicated key triggers `zc_session_cancel`.

### 4.5 Data model in TUI (new)

Add local structures for rendering-only state:

- `tui_turn_t { turn_id, status, started_at, finished_at }`
- `tui_tool_event_t { turn_id, tool_name, status, started_at, finished_at, summary }`
- `tui_runtime_state_t { idle/running/cancelling/error }`

No business logic duplication in C.

### 4.6 Backward compatibility strategy

- Keep existing FFI `zc_agent_run_single` and `zc_agent_run_interactive` unchanged.
- New session API is additive.
- Add feature flag in `cmd_tui`:
  - default: new FFI path enabled
  - fallback env: `CCLAW_TUI_LEGACY=1` (temporary rollback switch)

## 5. Risks & Roadmap

### 5.1 Technical Risks

- Cross-thread ownership bugs for event payload memory.
- Cancel semantics in long tool chains may require cooperative checks.
- Event flood (very large tool outputs) can block UI queue.

Mitigation:

- Strict allocate/free contract + tests with ASan/valgrind in C side.
- Bounded queue + truncation policy for large event payload.
- Define per-event payload cap and summarize overflow.

### 5.2 Phased Rollout

#### Phase P0 (1-2 weeks): Parity foundation

- Add FFI session + event API.
- Emit core events from Rust loop.
- Unit tests for API lifecycle and queue behavior.

Exit criteria:

- FFI session demo can run one turn and poll end-to-end events.

#### Phase P1 (2-3 weeks): TUI alignment release

- Migrate `cmd_tui` and `tui.c` to FFI session path.
- Keep current layout; inject status/tool timeline view.
- Add cancel + recover-on-error behavior.

Exit criteria:

- TUI behavior parity targets met.

#### Phase P2 (2-4 weeks): UX enhancements

- Better session/branch UX, searchable history, tool details panel.
- Optional token-level streaming (if provider stack supports it later).

Exit criteria:

- Improved usability metrics and reduced user interrupt rate.

## 6. Implementation Backlog (execution-ready)

1. Add FFI types + function declarations in `cclaw/include/zeroclaw_ffi.h`.
2. Implement session handle struct, queue, and APIs in `zeroclaw/src/ffi/mod.rs`.
3. Introduce event sink abstraction in `zeroclaw/src/agent/loop_.rs`.
4. Emit observer+event callbacks around tool loop boundaries.
5. Add Rust tests for lifecycle: create/send/poll/destroy.
6. Add Rust tests for cancel path and timeout poll.
7. Update `cclaw/src/cli/commands.c` `cmd_tui` to initialize FFI runtime/session.
8. Update `cclaw/src/runtime/tui.c` submit path to use `zc_session_send`.
9. Add TUI worker polling loop + render mapping for event types.
10. Add temporary fallback gate `CCLAW_TUI_LEGACY=1`.
11. Add integration test script for parity runbook.
12. Document migration and rollback in README/CHANGELOG.

## 7. Test Plan

### Unit

- FFI session create/destroy without leak.
- Poll timeout returns `ZC_EVT_NONE`.
- Event ordering for one turn: `THINKING -> ... -> TURN_DONE`.
- Cancel emits terminal cancelled status.

### Integration

- Same prompt through `cclaw agent -m` and `cclaw tui` (scripted input) -> compare tool sequence.
- Large tool output does not freeze TUI.
- Invalid provider/model reports actionable error event.

### Manual smoke

- `cclaw tui` startup, send prompt, see tool progress, cancel, recover with next prompt.

## 8. Immediate Week-1 Task Slice

### Day 1-2

- Implement FFI API skeleton with no-op event sink.
- Wire create/send/poll/destroy happy path.

### Day 3-4

- Emit real events from agent loop and store in queue.
- Add tests for ordering and payload lifetime.

### Day 5

- Switch `cmd_tui` submit path to new API.
- Basic event rendering in chat/status panel.

## 9. Open Decisions (default assumptions applied)

- Assumption A: token-level streaming is deferred (not required for parity).
- Assumption B: one active in-flight turn per TUI session in v1.
- Assumption C: event payloads use UTF-8 text or compact JSON.

If these assumptions change, only FFI schema and renderer mapping need updates; core rollout stays intact.
