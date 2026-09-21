"""Usage of the other coding tools, read from what they already keep on disk.

Nothing here touches the network or any credential:

* Codex writes its rate limits (5-hour and weekly windows, % used and reset
  time) into every session log under ~/.codex/sessions. The newest log holds
  the latest numbers; a window whose reset time has passed is back at 0 %.
* GitHub Copilot has no limit worth watching on a company seat, so the device
  shows AI credits used today and since the 1st — the unit GitHub's own usage
  page reports (1 credit = $0.01). OpenCode (and OpenChamber, which drives
  it) records every reply's tokens by type; they are priced with GitHub's
  published per-model rates in copilot_rates.json, including long-context
  tiers and promotions. OpenCode's own dollar cost uses list prices with no
  promotions — 6 % over GitHub in September 2026, when GPT-5.6 Sol was half
  price until the 3rd — so it is only the fallback for models missing from
  the table. The Copilot app/CLI records nano-credits per request directly.
  Prompts sent come along as a secondary count.

Each reader returns None when its tool isn't installed or has nothing yet.
"""
from __future__ import annotations

import calendar
import datetime as dt
import json
import sqlite3
import time
from pathlib import Path

RATES_FILE = Path(__file__).with_name("copilot_rates.json")
CODEX_DIR = Path.home() / ".codex"
OPENCODE_DB = Path.home() / ".local" / "share" / "opencode" / "opencode.db"
COPILOT_CLI_DB = Path.home() / ".copilot" / "session-store.db"

_TAIL_BYTES = 512 * 1024


# ---------------------------------------------------------------------------
# Codex
# ---------------------------------------------------------------------------

def _newest_codex_log(codex_dir: Path) -> Path | None:
    candidates = list((codex_dir / "sessions").glob("*/*/*/rollout-*.jsonl"))
    candidates += list((codex_dir / "archived_sessions").glob("rollout-*.jsonl"))
    if not candidates:
        return None
    return max(candidates, key=lambda p: p.stat().st_mtime)


def _find_key(obj, key):
    if isinstance(obj, dict):
        if key in obj:
            return obj[key]
        for v in obj.values():
            found = _find_key(v, key)
            if found is not None:
                return found
    elif isinstance(obj, list):
        for v in obj:
            found = _find_key(v, key)
            if found is not None:
                return found
    return None


def _last_rate_limits(log: Path) -> dict | None:
    with log.open("rb") as f:
        f.seek(0, 2)
        size = f.tell()
        f.seek(max(0, size - _TAIL_BYTES))
        tail = f.read().decode("utf-8", errors="replace")
    for line in reversed(tail.splitlines()):
        if '"rate_limits"' not in line:
            continue
        try:
            limits = _find_key(json.loads(line), "rate_limits")
        except ValueError:
            continue        # the first line of the tail may be cut
        if isinstance(limits, dict):
            return limits
    return None


def _window(w: dict | None, now: float) -> tuple[float, int]:
    """(% used, minutes to reset or -1) for one window, 0 % once it has reset."""
    if not isinstance(w, dict):
        return 0.0, -1
    used = float(w.get("used_percent") or 0.0)
    resets_at = w.get("resets_at")
    if not resets_at:
        return used, -1
    left = float(resets_at) - now
    if left <= 0:
        return 0.0, -1
    return used, int(left // 60)


def read_codex(codex_dir: Path = CODEX_DIR, now: float | None = None) -> dict | None:
    now = time.time() if now is None else now
    try:
        log = _newest_codex_log(codex_dir)
        limits = _last_rate_limits(log) if log else None
    except OSError:
        return None
    if not limits:
        return None
    p, pr = _window(limits.get("primary"), now)
    w, wr = _window(limits.get("secondary"), now)
    return {"p": round(p), "pr": pr, "w": round(w), "wr": wr}


# ---------------------------------------------------------------------------
# GitHub Copilot
# ---------------------------------------------------------------------------

def _connect_ro(db: Path) -> sqlite3.Connection | None:
    if not db.exists():
        return None
    try:
        return sqlite3.connect(f"file:{db}?mode=ro", uri=True, timeout=2)
    except sqlite3.Error:
        return None


def load_rates(path: Path = RATES_FILE) -> dict:
    try:
        return json.loads(path.read_text())
    except (OSError, ValueError):
        return {"models": {}, "promos": []}


def reply_credits(rates: dict, model: str, ts_ms: int, i: int, cr: int, cw: int, o: int,
                  fallback_usd: float) -> float:
    """AI credits for one reply, from its tokens and GitHub's rate for the model."""
    spec = rates.get("models", {}).get(model)
    if not spec:
        return fallback_usd * 100
    r = spec["rates"]
    thr = spec.get("long_threshold")
    if thr and i + cr + cw > thr:
        r = spec["long_rates"]
    credits = (i * r[0] + cr * r[1] + cw * r[2] + o * r[3]) / 1e6 * 100
    when = dt.datetime.fromtimestamp(ts_ms / 1000, dt.timezone.utc)
    for promo in rates.get("promos", []):
        if promo.get("model") == model and when < dt.datetime.fromisoformat(promo["until"].replace("Z", "+00:00")):
            credits *= promo.get("factor", 1.0)
    return credits


def _opencode_days(db: Path, since_ms: int) -> dict[str, list[float]]:
    """{local YYYY-MM-DD: [prompts, credits]} for Copilot traffic in OpenCode."""
    out: dict[str, list[float]] = {}
    con = _connect_ro(db)
    if con is None:
        return out
    try:
        prompts = con.execute(
            """
            SELECT date(m.time_created / 1000, 'unixepoch', 'localtime'), count(*)
            FROM message m JOIN session s ON s.id = m.session_id
            WHERE m.time_created >= ? AND s.parent_id IS NULL
              AND json_extract(m.data, '$.role') = 'user'
              AND json_extract(m.data, '$.model.providerID') = 'github-copilot'
            GROUP BY 1
            """, (since_ms,)).fetchall()
        replies = con.execute(
            """
            SELECT date(time_created / 1000, 'unixepoch', 'localtime'), time_created,
                   json_extract(data, '$.modelID'),
                   coalesce(json_extract(data, '$.tokens.input'), 0),
                   coalesce(json_extract(data, '$.tokens.cache.read'), 0),
                   coalesce(json_extract(data, '$.tokens.cache.write'), 0),
                   coalesce(json_extract(data, '$.tokens.output'), 0)
                     + coalesce(json_extract(data, '$.tokens.reasoning'), 0),
                   coalesce(json_extract(data, '$.cost'), 0)
            FROM message
            WHERE time_created >= ?
              AND json_extract(data, '$.role') = 'assistant'
              AND json_extract(data, '$.providerID') = 'github-copilot'
            """, (since_ms,)).fetchall()
    except sqlite3.Error:
        return out
    finally:
        con.close()
    for day, n in prompts:
        out.setdefault(day, [0, 0.0])[0] += int(n or 0)
    rates = load_rates()
    for day, ts, model, i, cr, cw, o, usd in replies:
        out.setdefault(day, [0, 0.0])[1] += reply_credits(
            rates, model or "", int(ts), int(i), int(cr), int(cw), int(o), float(usd or 0))
    return out


def _copilot_cli_days(db: Path, since_utc: str) -> dict[str, list[float]]:
    """Same, from the Copilot app/CLI's own request log (UTC timestamps)."""
    out: dict[str, list[float]] = {}
    con = _connect_ro(db)
    if con is None:
        return out
    try:
        rows = con.execute(
            """
            SELECT date(created_at, 'localtime'),
                   sum(initiator = 'user'),
                   sum(coalesce(total_nano_aiu, 0)) / 1e9
            FROM assistant_usage_events
            WHERE created_at >= ?
            GROUP BY 1
            """, (since_utc,)).fetchall()
    except sqlite3.Error:
        return out
    finally:
        con.close()
    for day, n, cred in rows:
        entry = out.setdefault(day, [0, 0.0])
        entry[0] += int(n or 0)
        entry[1] += float(cred or 0)
    return out


# Days before today never change, so they are priced once. Only today is
# re-read on each poll: a full month of OpenCode messages costs ~400 ms on a
# busy month, against a few ms for one day.
_FINISHED: dict[str, list[float]] = {}
_FINISHED_MONTH: str | None = None
_FINISHED_THROUGH: str | None = None


def _read_days(opencode_db: Path, copilot_db: Path, start: dt.datetime) -> dict[str, list[float]]:
    since_ms = int(start.timestamp() * 1000)
    since_utc = dt.datetime.fromtimestamp(start.timestamp(), dt.timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
    days: dict[str, list[float]] = {}
    for source in (_opencode_days(opencode_db, since_ms), _copilot_cli_days(copilot_db, since_utc)):
        for day, (n, cred) in source.items():
            entry = days.setdefault(day, [0, 0.0])
            entry[0] += n
            entry[1] += cred
    return days


def read_copilot(opencode_db: Path = OPENCODE_DB, copilot_db: Path = COPILOT_CLI_DB,
                 now: float | None = None) -> dict | None:
    """Credits and prompts today and month-to-date, plus credits per day for the grid."""
    global _FINISHED, _FINISHED_MONTH, _FINISHED_THROUGH
    now = time.time() if now is None else now
    today = dt.datetime.fromtimestamp(now)
    first = today.replace(day=1, hour=0, minute=0, second=0, microsecond=0)
    midnight = today.replace(hour=0, minute=0, second=0, microsecond=0)

    if not opencode_db.exists() and not copilot_db.exists():
        return None

    # The days already over: priced once per day, then reused.
    month, yesterday = first.strftime("%Y-%m"), midnight.strftime("%Y-%m-%d")
    if _FINISHED_MONTH != month or _FINISHED_THROUGH != yesterday:
        _FINISHED = {d: v for d, v in _read_days(opencode_db, copilot_db, first).items()
                     if d < yesterday}
        _FINISHED_MONTH, _FINISHED_THROUGH = month, yesterday
    days: dict[str, list[float]] = {d: list(v) for d, v in _FINISHED.items()}
    for day, (n, cred) in _read_days(opencode_db, copilot_db, midnight).items():
        entry = days.setdefault(day, [0, 0.0])
        entry[0] += n
        entry[1] += cred

    key = today.strftime("%Y-%m-%d")
    td, tc = days.get(key, [0, 0.0])
    per_day = [round(days.get(first.replace(day=d).strftime("%Y-%m-%d"), [0, 0.0])[1])
               for d in range(1, today.day + 1)]
    return {
        "summary": {"tc": round(tc), "td": td,
                    "mc": round(sum(v[1] for v in days.values())),
                    "md": sum(v[0] for v in days.values())},
        "grid": {"mo": today.month,
                 "wd": first.weekday(),                        # 0 = Monday
                 "dim": calendar.monthrange(today.year, today.month)[1],
                 "d": per_day},
    }
