#pragma once

// What Claude Code is doing on the host, as reported by the daemon (field "cc"
// in the BLE payload, fed by Claude Code hooks): "work", "wait", "done", or
// absent/empty for nothing going on. The device turns it into Clawd's
// animation, a sound, and — when Claude is blocked on the user — a wake-up.
//
// Only changes act, so the daemon repeating the same state every poll is free.
void claude_state_update(const char* cc);

enum ClaudeActivity { CLAUDE_IDLE, CLAUDE_WORK, CLAUDE_WAIT, CLAUDE_DONE };

// The last state received, for the usage page's status line.
ClaudeActivity claude_state_current(void);
