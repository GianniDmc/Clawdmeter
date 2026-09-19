# OpenCode plugin

`clawdmeter.js` mirrors what OpenCode is doing onto the device's Copilot page:
working, waiting on you (permission prompt or question), or done. It is the
OpenCode equivalent of the Claude Code / Codex hooks, and it covers
OpenChamber too, since that runs OpenCode underneath.

Add it to the `plugin` array of `~/.config/opencode/config.json`, with the
absolute path of your checkout:

```json
{
  "plugin": ["file:///path/to/Clawdmeter/daemon/opencode/clawdmeter.js"]
}
```

Restart OpenCode (and OpenChamber, which starts its own OpenCode server).
The plugin writes one file per session under
`~/.config/claude-usage-monitor/claude-state/opencode/`; the daemon folds them
into one state and sends it to the device. Nothing leaves the machine.
