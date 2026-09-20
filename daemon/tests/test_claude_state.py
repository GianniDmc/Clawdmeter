#!/usr/bin/env python3
"""Claude Code hook -> state files -> the daemon's "cc" field.

Run: python -m pytest daemon/tests/test_claude_state.py -x -q
"""
import json
import time

from daemon.claude_state_hook import record, state_for
from daemon.claude_usage_daemon import (opencode_payload, other_tool_payloads,
                                        read_claude_state)


def ev(name, **kw):
    return {"hook_event_name": name, "session_id": kw.pop("sid", "s1"), **kw}


# ---------------------------------------------------------------------------
# hook: event -> state
# ---------------------------------------------------------------------------

def test_prompt_and_tools_mean_work():
    for name in ("UserPromptSubmit", "PreToolUse", "PostToolUse", "PostToolUseFailure"):
        assert state_for(ev(name, tool_name="Bash")) == "work"


def test_tools_that_ask_the_user_mean_wait():
    assert state_for(ev("PreToolUse", tool_name="AskUserQuestion")) == "wait"
    assert state_for(ev("PreToolUse", tool_name="ExitPlanMode")) == "wait"


def test_notification_means_wait_and_stop_means_done():
    assert state_for(ev("Notification")) == "wait"
    assert state_for(ev("Stop")) == "done"
    assert state_for(ev("StopFailure")) == "done"


def test_unrelated_events_are_ignored(tmp_path):
    assert state_for(ev("PreCompact")) is None
    record(ev("PreCompact"), tmp_path)
    assert list(tmp_path.glob("*.json")) == []


def test_record_writes_and_session_end_removes(tmp_path):
    record(ev("UserPromptSubmit"), tmp_path, now=100.0)
    f = tmp_path / "s1.json"
    assert json.loads(f.read_text()) == {"state": "work", "ts": 100.0}
    record(ev("SessionEnd"), tmp_path)
    assert not f.exists()


def test_record_sanitizes_the_session_id(tmp_path):
    record(ev("Stop", sid="../../etc/passwd"), tmp_path, now=1.0)
    assert [p.name for p in tmp_path.iterdir()] == ["etcpasswd.json"]


# ---------------------------------------------------------------------------
# daemon: files -> one state
# ---------------------------------------------------------------------------

def test_no_sessions_is_empty(tmp_path):
    assert read_claude_state(tmp_path, now=0) == ""
    assert read_claude_state(tmp_path / "missing", now=0) == ""


def test_wait_beats_work_beats_done(tmp_path):
    record(ev("Stop", sid="a"), tmp_path, now=1000.0)
    assert read_claude_state(tmp_path, now=1001.0) == "done"
    record(ev("PreToolUse", sid="b", tool_name="Bash"), tmp_path, now=1000.0)
    assert read_claude_state(tmp_path, now=1001.0) == "work"
    record(ev("Notification", sid="c"), tmp_path, now=1000.0)
    assert read_claude_state(tmp_path, now=1001.0) == "wait"


def test_done_fades_quickly_but_work_lingers(tmp_path):
    record(ev("Stop", sid="a"), tmp_path, now=0.0)
    record(ev("PreToolUse", sid="b", tool_name="Bash"), tmp_path, now=0.0)
    assert read_claude_state(tmp_path, now=60.0) == "work"
    (tmp_path / "b.json").unlink()
    assert read_claude_state(tmp_path, now=60.0) == ""


def test_stale_files_expire_and_are_cleaned(tmp_path):
    record(ev("PreToolUse", tool_name="Bash"), tmp_path, now=0.0)
    assert read_claude_state(tmp_path, now=16 * 60) == ""
    assert (tmp_path / "s1.json").exists()
    read_claude_state(tmp_path, now=2 * 24 * 3600)
    assert not (tmp_path / "s1.json").exists()


def test_garbage_files_are_skipped(tmp_path):
    (tmp_path / "bad.json").write_text("{not json")
    (tmp_path / "odd.json").write_text('{"state": "dancing", "ts": 1}')
    record(ev("Stop"), tmp_path, now=10.0)
    assert read_claude_state(tmp_path, now=11.0) == "done"


# ---------------------------------------------------------------------------
# Codex source
# ---------------------------------------------------------------------------

def test_codex_permission_request_waits_and_interrupt_clears(tmp_path):
    record(ev("PermissionRequest"), tmp_path, now=5.0)
    assert read_claude_state(tmp_path, now=6.0) == "wait"
    record(ev("Interrupt"), tmp_path)
    assert read_claude_state(tmp_path, now=6.0) == ""


def test_opencode_payload_carries_the_state():
    # The OpenCode plugin's state rides its own tiny message, so a change
    # never waits on the Copilot database query.
    assert opencode_payload("wait") == {"k": "ocs", "st": "wait"}
    assert opencode_payload("") == {"k": "ocs", "st": ""}


def test_other_tool_payloads_include_opencode():
    msgs = other_tool_payloads("", "work")
    assert {"k": "ocs", "st": "work"} in msgs


def test_opencode_state_folds_the_plugin_files(tmp_path):
    # Same folding as Claude Code's: one file per session, wait > work > done.
    (tmp_path / "a.json").write_text(json.dumps({"state": "work", "ts": time.time()}))
    (tmp_path / "b.json").write_text(json.dumps({"state": "wait", "ts": time.time()}))
    assert read_claude_state(tmp_path) == "wait"
