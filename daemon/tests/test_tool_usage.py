#!/usr/bin/env python3
"""Codex rate limits and Copilot usage, read from the tools' own files.

Run: python -m pytest daemon/tests/test_tool_usage.py -x -q
"""
import datetime as dt
import json
import sqlite3

from daemon.tool_usage import read_codex, read_copilot


def _codex_log(root, name, limits, mtime=None):
    d = root / "sessions" / "2026" / "09" / "19"
    d.mkdir(parents=True, exist_ok=True)
    f = d / name
    lines = [json.dumps({"type": "session_meta"}),
             json.dumps({"type": "event_msg", "payload": {"type": "token_count", "rate_limits": limits}})]
    f.write_text("\n".join(lines) + "\n")
    return f


def test_codex_reads_newest_log_and_expires_windows(tmp_path):
    now = 1_000_000.0
    _codex_log(tmp_path, "rollout-a.jsonl", {
        "primary": {"used_percent": 40.0, "window_minutes": 300, "resets_at": now + 3600},
        "secondary": {"used_percent": 90.0, "window_minutes": 10080, "resets_at": now - 1},
    })
    assert read_codex(tmp_path, now=now) == {"p": 40, "pr": 60, "w": 0, "wr": -1}


def test_codex_absent_is_none(tmp_path):
    assert read_codex(tmp_path / "nope") is None


def _opencode_db(path, rows):
    con = sqlite3.connect(path)
    con.execute("CREATE TABLE session (id TEXT PRIMARY KEY, parent_id TEXT)")
    con.execute("CREATE TABLE message (id TEXT, session_id TEXT, time_created INTEGER, data TEXT)")
    con.execute("INSERT INTO session VALUES ('s', NULL), ('sub', 's')")
    for i, (sid, ts, data) in enumerate(rows):
        con.execute("INSERT INTO message VALUES (?, ?, ?, ?)", (str(i), sid, ts, json.dumps(data)))
    con.commit()
    con.close()


def test_copilot_counts_prompts_and_tokens_by_local_day(tmp_path):
    now = dt.datetime(2026, 9, 3, 15, 0).timestamp()
    ms = lambda d, h: int(dt.datetime(2026, 9, d, h).timestamp() * 1000)
    user = {"role": "user", "model": {"providerID": "github-copilot"}}
    reply = {"role": "assistant", "providerID": "github-copilot", "cost": 0.5}      # 50 credits
    _opencode_db(tmp_path / "oc.db", [
        ("s", ms(1, 10), user), ("s", ms(1, 10), reply),
        ("s", ms(3, 9), user), ("s", ms(3, 9), reply), ("s", ms(3, 9), reply),
        ("sub", ms(3, 9), user),                                  # subagent: not a prompt
        ("s", ms(3, 9), {"role": "user", "model": {"providerID": "anthropic"}}),
        ("s", dt.datetime(2026, 8, 31, 12).timestamp() * 1000, user),   # last month
    ])
    got = read_copilot(tmp_path / "oc.db", tmp_path / "missing.db", now=now)
    assert got["summary"] == {"tc": 100, "td": 1, "mc": 150, "md": 2}
    assert got["grid"] == {"mo": 9, "wd": dt.date(2026, 9, 1).weekday(), "dim": 30, "d": [50, 0, 100]}


def test_copilot_absent_is_none(tmp_path):
    assert read_copilot(tmp_path / "a.db", tmp_path / "b.db") is None
