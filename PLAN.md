# Kickforge — kick drum synthesizer (VST3 for REAPER)

Goal: an original kick-drum synth plugin with feature parity to Sonic Academy KICK 3
(functionality only — own name, own UI, own presets, no copied samples or artwork).

## Stack
- C++20, JUCE 8 (pinned tag via CMake FetchContent), CMake + VS 2022 Build Tools (MSVC 14.44, SDK 10.0.26100).
- Formats: VST3 (REAPER) + Standalone (quick testing without a DAW).
- Generator: `cmake -G "Visual Studio 17 2022" -A x64` — finds MSVC itself, no vcvars needed.
- Build: `cmake --build build --config Release --target Kickforge_VST3`.
- Tests: Catch2 (or JUCE UnitTest) console target `KickforgeTests` for DSP — render offline, assert on numbers.

## Architecture (module ownership = parallel-agent boundaries)
```
Source/
  PluginProcessor.*      host glue, param tree, MIDI → voice           (scaffold, then shared)
  Params.*               calls addXxxParams(layout) from each module   (scaffold)
  core/Curve.*           breakpoint envelope: points + per-segment tension, eval(t), serialize   (scaffold)
  core/Tuning.*          note ↔ Hz ↔ name, A4 ref                     (scaffold)
  synth/                 Sub/body oscillator, pitch+amp curves, voice  (Agent: DSP core)
  click/                 sample layers + synthesized clicks            (Agent: Click)
  fx/                    distortion, EQ, comp, limiter, oversampling   (Agent: FX)
  ui/                    editor, curve editor, scope, panels           (Agent: UI)
  presets/, export/      preset browser, render-to-WAV / drag-out      (Agent: Presets/Export)
tests/
docs/SPEC.md             feature parity checklist (from research)
```
Each module registers its own params with its own ID prefix (`sub_`, `click_`, `fx_`, `glob_`) in its own file, so agents never edit the same file.

## Phases
### Phase 0 — foundation (parallel)
- **P0-A Spec**: research KICK 3 feature set → `docs/SPEC.md` as a checklist with IDs (S-xx sub, C-xx click, F-xx fx, U-xx UI, G-xx global/MIDI, P-xx presets/export).
- **P0-B Scaffold**: JUCE CMake project, VST3 + Standalone build green, param plumbing, `Curve`, `Tuning`, test target, module stubs, `CLAUDE.md` with build/test commands.

### Phase 1 — engine (≤3 parallel worktree agents)
- **DSP core**: phase-accurate sub oscillator (sine + shapes), pitch curve (Hz/semitone, key-tracked to MIDI note, fine tune), amp curve, length/decay scaling, velocity, retrigger/legato, mono voice with declick, phase reset/start phase.
- **Click**: up to N sample layers (load WAV/AIFF, start/length/pitch/gain/pan/filter/env), synthesized click generators (noise burst, impulse, filtered transient) so it works with no library; drag-drop samples.
- **FX**: distortion (several types: soft/hard clip, tube, foldback, bitcrush, etc.), pre/post routing, EQ (low-shelf / peak / high-shelf / HPF), compressor, brickwall limiter, oversampling quality modes, output gain.

### Phase 2 — UI + workflow
- **UI**: curve editor (add/drag/delete points, tension drag, snap, zoom), live waveform + spectrum, note/frequency readout of fundamental, layer panels, resizable.
- **Presets/Export**: preset save/load/browser + factory presets (our own), render kick to WAV (+ drag out to REAPER), MIDI learn / host automation sanity.

### Phase 3 — polish
- Parity pass against SPEC.md, CPU profiling, pluginval strictness 5+, REAPER smoke test, installer/copy script.

## Rules for agents
- Read `CLAUDE.md` + this file + your section of `docs/SPEC.md` only.
- Stay in your module folder; shared files (`PluginProcessor`, `Params`, `CMakeLists.txt`) only via small, clearly-scoped additions.
- One commit per checklist item: `ID: summary`; tick the item in `docs/SPEC.md` in the same commit.
- Build must stay green + tests pass before each commit. Final report ≤12 lines.
