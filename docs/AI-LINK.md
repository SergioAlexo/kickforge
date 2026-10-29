# AI link — matching a real kick from the command line

Goal: the user loads a real kick as a **Reference** inside the plugin (in REAPER), and an AI agent
(Claude Code CLI) iteratively adjusts Kickforge's parameters and curves until the synthesized kick
matches it. The agent never touches the GUI: it talks to the plugin through files and measures the
result with `kickforge-cli`, which renders with the exact same engine offline.

```
 REAPER ── Kickforge (Live link ON) ──writes──▶ Live/state.kfpreset, Live/reference.json
                    ▲                                         │
                    └──polls & loads── Live/apply.kfpreset ◀──┤ kickforge-cli set/fit/import --apply
                                                              │
                         kickforge-cli analyze/compare/plot ◀─┘ (offline render of the same state)
```

## Live folder contract
Folder: `%APPDATA%\Kickforge\Live\` (override with env `KICKFORGE_LIVE_DIR`, both sides honour it).

| File | Writer | Reader | Meaning |
|---|---|---|---|
| `state.kfpreset` | plugin | CLI | Full current plugin state (same XML as PresetManager presets). Rewritten ≤300 ms after any param/curve/click change and after every apply. |
| `reference.json` | plugin | CLI | `{"path": "C:\\...\\ref.wav", "regionStart": 0, "regionEnd": 12345, "sampleRate": 44100, "note": 33}` — the reference kick the user loaded; region in source samples (`regionEnd` 0 = whole file); `note` = the MIDI note the user plays it at (default = `sub_root`). Deleted when the reference is cleared. |
| `apply.kfpreset` | CLI | plugin | Full state to load. CLI writes `apply.kfpreset.tmp` then renames (atomic). Plugin polls every 250 ms on the message thread, loads it (same path as preset load: replaceState → click.loadState → subCurves.loadFromState; the Reference itself and the Live-link switch are NOT part of the applied state), deletes the file, rewrites `state.kfpreset`, and bumps `status.json`. |
| `status.json` | plugin | CLI | `{"instance": "<uuid>", "pid": 1234, "applied": <counter>, "lastApplyOk": true, "error": "", "time": "<iso>"}` — the CLI waits for `applied` to increase to confirm. |

Only one plugin instance is linked at a time: enabling Live link writes `status.json` with its
instance id; an instance that sees a different id there turns its own link off (and shows that).

## Plugin side (Source/live + UI)
- "Live link" toggle in the header (stored in plugin state, not automatable, default off).
- "Reference" slot: load a WAV/AIFF/FLAC (button + drag-drop), optional region select (reuse ImportDialog's
  waveform/hit picker), gain, and an **A/B** control: Synth / Reference / Both (reference played through
  `PreviewPlayer`-style path, retriggered on every note-on, pitch NOT shifted). Reference path + region +
  note stored in plugin state.
- The output scope overlays the reference waveform (dimmed) against the last synth kick, aligned at onset.

## CLI (`kickforge-cli.exe`, console target sharing KICKFORGE_ENGINE_SOURCES)
All output is JSON on stdout (human text only with `--text`); errors are JSON `{"error": ...}` with exit code ≠ 0.
State argument `--state <file>` defaults to `Live/state.kfpreset`; reference argument defaults to `Live/reference.json`.

| Command | Purpose |
|---|---|
| `status` | live folder, linked instance, reference info, state file age |
| `params [--filter prefix] [--changed]` | every parameter: id, name, value, min, max, default, step/choices, unit |
| `get <id>...` / `set <id>=<value>... [--apply] [--out f]` | read/write params (real units; choices by index or label) |
| `curve get <name>` / `curve set <name> <json\|@file>` | curves as `{"points":[{"t":0,"v":31,"tension":-0.4},...]}`; names: pitch, amp, drive, harm, keytrack, click1..click4 |
| `render [--note n] [--out wav]` | offline render of the state (same engine), JSON summary |
| `analyze <wav> [--region a:b]` | features: onset, length, peak/RMS, fundamental track (Hz + semitones vs tail, at fixed log-spaced times), amp envelope (dB at fixed times), octave-band spectrum early/late, transient energy share, tail Hz + note name |
| `compare [--ref wav]` | render state, align at onset, return per-feature errors (pitch st per time, env dB per time, band dB, transient, length, tail Hz), an overall score 0..100, and plain-language `hints` naming the params/curves most likely responsible |
| `plot [--ref wav] --out png` | overlay image: waveforms, pitch tracks, envelopes, spectra (for the agent to look at) |
| `import [--ref wav] [--apply]` | first-pass fit using `Source/analysis` (pitch/amp curves, root/tune, harmonics, top-end click sample) |
| `fit [--ref wav] [--params ids] [--curves names] [--iters N] [--apply]` | local optimiser on the compare loss (Nelder–Mead / coordinate descent), prints before/after scores |
| `apply [--state f]` | push a state file to the plugin and wait for confirmation (`status.json`) |

Determinism: renders use a fixed sample rate (reference's rate, default 48 kHz), fixed velocity 1.0, no randomness.
