// Clawdmeter plugin for OpenCode (and OpenChamber, which runs OpenCode).
//
// Records what each session is doing so the device's Copilot page can mirror
// it, the same way the Claude Code / Codex hooks do (claude_state_hook.py):
//
//     work  the session is busy
//     wait  it is blocked on you (a permission prompt or a question)
//     done  it finished its turn
//
// One file per session in ~/.config/claude-usage-monitor/claude-state/opencode/,
// {"state": ..., "ts": seconds}; the daemon folds them into one state and
// sends it to the device. Install by listing this file in the "plugin" array
// of ~/.config/opencode/config.json (see daemon/opencode/README.md). Local
// files only, no network; any failure is swallowed so OpenCode never notices.
import { mkdirSync, renameSync, unlinkSync, writeFileSync } from "fs";
import { homedir } from "os";
import { join } from "path";

const STATE_DIR = join(homedir(), ".config", "claude-usage-monitor", "claude-state", "opencode");

// A long "work" stretch keeps its file fresh, or the daemon's 15-minute
// time-to-live would drop it mid-task.
const REFRESH_S = 60;

const last = new Map();   // sessionID -> { state, ts }

function safeId(id) {
  return String(id).replace(/[^A-Za-z0-9_-]/g, "").slice(0, 80);
}

function record(sessionID, state) {
  const id = safeId(sessionID || "");
  if (!id) return;
  const now = Date.now() / 1000;
  const prev = last.get(id);
  if (prev && prev.state === state && now - prev.ts < REFRESH_S) return;
  last.set(id, { state, ts: now });
  try {
    mkdirSync(STATE_DIR, { recursive: true });
    // Atomic replace, so the daemon never reads a half-written file.
    const tmp = join(STATE_DIR, `.tmp-${id}`);
    writeFileSync(tmp, JSON.stringify({ state, ts: now }));
    renameSync(tmp, join(STATE_DIR, `${id}.json`));
  } catch {}
}

function forget(sessionID) {
  const id = safeId(sessionID || "");
  if (!id) return;
  last.delete(id);
  try { unlinkSync(join(STATE_DIR, `${id}.json`)); } catch {}
}

export default async () => ({
  event: async ({ event }) => {
    try {
      const t = event.type;
      const p = event.properties || {};
      switch (t) {
        case "session.status": {
          const s = p.status?.type;
          if (s === "busy" || s === "retry") record(p.sessionID, "work");
          else if (s === "idle") record(p.sessionID, "done");
          break;
        }
        case "permission.asked":
        case "question.asked":
          record(p.sessionID, "wait");
          break;
        case "permission.replied":
        case "question.replied":
        case "question.rejected":
          record(p.sessionID, "work");
          break;
        case "message.part.updated":
          // Tools running: still busy (refreshes the file on long tasks).
          if (p.part?.type === "tool" && last.get(safeId(p.part.sessionID || ""))?.state === "work") {
            record(p.part.sessionID, "work");
          }
          break;
        case "session.deleted":
          forget(p.info?.id);
          break;
      }
    } catch {}
  },
});
