#!/usr/bin/env python3
"""Claude Code / Codex hook: record what this session is doing, for the Clawdmeter.

    claude_state_hook.py            Claude Code (the default)
    claude_state_hook.py codex      Codex — same JSON on stdin, separate state

Registered for several hook events in ~/.claude/settings.json (see
daemon/claude-hooks.example.json). Claude Code pipes the event as JSON on
stdin; this maps it to one of three states and writes it to
<state dir>/<session_id>.json. The daemon folds every session's file into one
state and sends it to the device as the "cc" field.

    work  Claude is busy (prompt submitted, tools running)
    wait  Claude is blocked on you (permission prompt, a question, plan approval)
    done  Claude finished its turn

SessionEnd removes the session's file. Standard library only and no network,
so it adds a few tens of milliseconds per event; it never blocks Claude Code —
any failure exits 0 silently.
"""
from __future__ import annotations

import json
import os
import sys
import tempfile
import time
from pathlib import Path

STATE_DIR = Path.home() / ".config" / "claude-usage-monitor" / "claude-state"
# Other tools keep their sessions in a subdirectory, so each gets its own state.
SOURCES = {"claude": STATE_DIR, "codex": STATE_DIR / "codex"}

# Tools whose whole point is to hand the turn to the user.
ASKS_USER = {"AskUserQuestion", "ExitPlanMode"}

WORK_EVENTS = {"UserPromptSubmit", "PreToolUse", "PostToolUse", "PostToolUseFailure",
               "SubagentStart"}
DONE_EVENTS = {"Stop", "StopFailure"}


def state_for(event: dict) -> str | None:
    """Map one hook event to "work" / "wait" / "done", or None to leave it be."""
    name = event.get("hook_event_name", "")
    if name == "PreToolUse" and event.get("tool_name") in ASKS_USER:
        return "wait"
    if name in WORK_EVENTS:
        return "work"
    # Claude Code: registered only with the matchers that mean "the user must
    # act" (permission_prompt, elicitation_dialog, agent_needs_input).
    # Codex: PermissionRequest is its approval prompt.
    if name in ("Notification", "PermissionRequest"):
        return "wait"
    if name in DONE_EVENTS:
        return "done"
    return None


def _safe_id(session_id: str) -> str:
    return "".join(c for c in session_id if c.isalnum() or c in "-_")[:80]


def record(event: dict, state_dir: Path = STATE_DIR, now: float | None = None) -> None:
    session = _safe_id(str(event.get("session_id", "")))
    if not session:
        return
    path = state_dir / f"{session}.json"

    # Codex's Interrupt: the user stopped the turn, nothing is going on.
    if event.get("hook_event_name") in ("SessionEnd", "Interrupt"):
        path.unlink(missing_ok=True)
        return

    state = state_for(event)
    if state is None:
        return

    state_dir.mkdir(parents=True, exist_ok=True)
    body = json.dumps({"state": state, "ts": time.time() if now is None else now})
    # Atomic replace, so the daemon never reads a half-written file.
    fd, tmp = tempfile.mkstemp(dir=state_dir, prefix=".tmp-")
    with os.fdopen(fd, "w") as f:
        f.write(body)
    os.replace(tmp, path)


def main() -> int:
    source = sys.argv[1] if len(sys.argv) > 1 else "claude"
    try:
        record(json.load(sys.stdin), SOURCES.get(source, STATE_DIR))
    except Exception:
        pass    # a status light must never get in Claude Code's way
    return 0


if __name__ == "__main__":
    sys.exit(main())
