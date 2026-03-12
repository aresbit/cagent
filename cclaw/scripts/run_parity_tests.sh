#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ZEROCLAW_DIR="$ROOT_DIR/zeroclaw"

echo "[parity] checking deterministic event sequence"
(cd "$ZEROCLAW_DIR" && cargo test -q agent_turn_with_sink_emits_tool_and_text_events)

echo "[parity] checking FFI session behavior"
(cd "$ZEROCLAW_DIR" && cargo test -q session_)

echo "[parity] done: event-sequence and session checks passed"
