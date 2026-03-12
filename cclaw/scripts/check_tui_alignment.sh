#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ZEROCLAW_DIR="$ROOT_DIR/zeroclaw"
CCLAW_DIR="$ROOT_DIR/cclaw"

echo "[1/3] zeroclaw: event-sequence and session checks"
(
  cd "$ZEROCLAW_DIR" && \
  cargo check -q && \
  cargo test -q agent_turn_with_sink_emits_tool_and_text_events && \
  cargo test -q session_
)

echo "[2/3] cclaw: TUI object compile"
(cd "$CCLAW_DIR" && make build/runtime/tui.o build/cli/commands.o -j2)

echo "[3/3] status"
cat <<'EOF'
Alignment smoke checks passed.

Notes:
- This script verifies the new event-driven session path compiles/tests.
- Full behavioral parity diff (agent vs tui tool/event sequence) is not yet automated.
EOF
