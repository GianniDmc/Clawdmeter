#!/usr/bin/env python3
"""Codex rate limits and Copilot usage, read from the tools' own files.

Run: python -m pytest daemon/tests/test_tool_usage.py -x -q
"""
import datetime as dt
import json
import sqlite3

import pytest

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
    # A model missing from the rate table falls back to OpenCode's cost: 50 credits.
    reply = {"role": "assistant", "providerID": "github-copilot", "modelID": "unlisted", "cost": 0.5}
    _opencode_db(tmp_path / "oc.db", [
        ("s", ms(1, 10), user), ("s", ms(1, 10), reply),
        ("s", ms(3, 9), user), ("s", ms(3, 9), reply), ("s", ms(3, 9), reply),
        ("sub", ms(3, 9), user),                                  # subagent: not a prompt
        ("s", ms(3, 9), {"role": "user", "model": {"providerID": "anthropic"}}),
        ("s", dt.datetime(2026, 8, 31, 12).timestamp() * 1000, user),   # last month
    ])
    got = read_copilot(tmp_path / "oc.db", tmp_path / "missing.db", now=now)
    assert got["summary"] == {"tc": 100, "td": 1, "mc": 150, "md": 2, "u": 0}
    assert got["grid"] == {"mo": 9, "wd": dt.date(2026, 9, 1).weekday(), "dim": 30, "d": [50, 0, 100]}


def test_copilot_caches_finished_days_but_not_today(tmp_path):
    # Days already over are priced once; today is re-read, so a reply that
    # lands between two polls still shows up.
    from daemon import tool_usage
    tool_usage._FINISHED_MONTH = None
    now = dt.datetime(2026, 9, 3, 15, 0).timestamp()
    ms = lambda d, h: int(dt.datetime(2026, 9, d, h).timestamp() * 1000)
    user = {"role": "user", "model": {"providerID": "github-copilot"}}
    reply = {"role": "assistant", "providerID": "github-copilot", "modelID": "unlisted", "cost": 0.5}
    db = tmp_path / "oc.db"
    _opencode_db(db, [("s", ms(1, 10), user), ("s", ms(1, 10), reply)])
    assert read_copilot(db, tmp_path / "missing.db", now=now)["summary"]["mc"] == 50

    con = sqlite3.connect(db)
    con.execute("INSERT INTO message VALUES ('x', 's', ?, ?)", (ms(3, 9), json.dumps(reply)))
    con.commit()
    con.close()
    assert read_copilot(db, tmp_path / "missing.db", now=now)["summary"]["mc"] == 100
    tool_usage._FINISHED_MONTH = None


def test_copilot_absent_is_none(tmp_path):
    assert read_copilot(tmp_path / "a.db", tmp_path / "b.db") is None


def test_reply_credits_use_github_rates_tiers_and_promos():
    from daemon.tool_usage import reply_credits
    rates = {"models": {"m": {"rates": [4.0, 0.4, 5.0, 20.0], "long_threshold": 1000,
                              "long_rates": [8.0, 0.8, 10.0, 30.0]}},
             "promos": [{"model": "m", "until": "2026-09-04T00:00:00Z", "factor": 0.5}]}
    after = int(dt.datetime(2026, 9, 10, tzinfo=dt.timezone.utc).timestamp() * 1000)
    before = int(dt.datetime(2026, 9, 2, tzinfo=dt.timezone.utc).timestamp() * 1000)
    # 500 input + 100 output tokens, short context: (500*4 + 100*20) / 1e6 $ = 0.4 credits
    assert reply_credits(rates, "m", after, 500, 0, 0, 100, 9.9) == pytest.approx(0.4)
    assert reply_credits(rates, "m", before, 500, 0, 0, 100, 9.9) == pytest.approx(0.2)
    # past the threshold: long-context rates
    assert reply_credits(rates, "m", after, 2000, 0, 0, 0, 9.9) == pytest.approx(1.6)
    # unknown model: OpenCode's dollar cost
    assert reply_credits(rates, "other", after, 1, 1, 1, 1, 0.07) == pytest.approx(7.0)


def _copilot_cli_db(path, usage_last, session_days):
    con = sqlite3.connect(path)
    con.execute("CREATE TABLE assistant_usage_events (created_at TEXT, initiator TEXT,"
                " total_nano_aiu INTEGER)")
    con.execute("CREATE TABLE sessions (id TEXT, created_at TEXT, updated_at TEXT)")
    if usage_last:
        con.execute("INSERT INTO assistant_usage_events VALUES (?, 'user', 0)", (usage_last,))
    for i, day in enumerate(session_days):
        con.execute("INSERT INTO sessions VALUES (?, ?, ?)", (str(i), day, day))
    con.commit()
    con.close()


def test_copilot_reports_cli_sessions_it_cannot_price(tmp_path):
    # The CLI stopped writing usage events in July 2026 but still records
    # sessions: those are counted so the device can say what it is missing.
    from daemon import tool_usage
    tool_usage._FINISHED_MONTH = None
    now = dt.datetime(2026, 9, 21, 15, 0).timestamp()
    _opencode_db(tmp_path / "oc.db", [])
    _copilot_cli_db(tmp_path / "cp.db", "2026-07-30T09:38:34Z",
                    ["2026-08-30T10:00:00Z",          # last month: ignored
                     "2026-09-19T10:00:00Z", "2026-09-20T11:00:00Z"])
    got = read_copilot(tmp_path / "oc.db", tmp_path / "cp.db", now=now)
    assert got["summary"]["u"] == 2
    tool_usage._FINISHED_MONTH = None
