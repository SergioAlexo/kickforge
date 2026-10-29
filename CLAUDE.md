# Kickforge

Original kick-drum synth plugin (VST3 + Standalone), JUCE 8.0.8 via FetchContent, C++20. Read `PLAN.md` first, then your section of `docs/SPEC.md`.

## Commands (run from repo root; PowerShell or Git Bash)
```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64          # configure (first run fetches JUCE + Catch2)
cmake --build build --config Release --target Kickforge_VST3
cmake --build build --config Release --target Kickforge_Standalone
cmake --build build --config Release --target KickforgeTests
build\Release\KickforgeTests.exe                                  # or: ctest --test-dir build -C Release --output-on-failure
```
Add `-- /m` to the build for parallel MSVC. `build/` is gitignored.

## Outputs
- VST3: `build\Kickforge_artefacts\Release\VST3\Kickforge.vst3`
- Standalone: `build\Kickforge_artefacts\Release\Standalone\Kickforge.exe`
- `COPY_PLUGIN_AFTER_BUILD` is FALSE: nothing is installed automatically. REAPER is NOT installed on this PC yet; when it is, either add `build\Kickforge_artefacts\Release\VST3` to REAPER's VST scan paths or copy `Kickforge.vst3` to `C:\Program Files\Common Files\VST3`.

## Layout / module ownership
- `Source/PluginProcessor.*`, `Params.*`, `CMakeLists.txt`: shared; only small, scoped additions.
- `Source/core/` Curve (breakpoint envelope), Tuning, GlobParams (`glob_`): scaffold.
- `Source/synth/` SubVoice + `addSubParams` (`sub_`): DSP-core agent.
- `Source/click/` ClickLayer + `addClickParams` (`click_`): Click agent.
- `Source/fx/` FxChain + `addFxParams` (`fx_`): FX agent.
- `Source/ui/`: UI agent. `Source/presets/`: Presets/Export agent.
- Each module owns its param IDs (prefix) in its own `*Params.cpp`; never edit another module's files.
- Engine interface: `prepare(sr, maxBlock)`, `noteOn(note, vel01)`, `noteOff()`, `process(AudioBuffer&, start, num)`. Engines ADD into channel 0 (mono); the processor copies to the other channels. FxChain processes in place.
- New `.cpp` files must be added to `KICKFORGE_ENGINE_SOURCES` in CMakeLists.txt (plugin and tests share the list); tests go in `tests/` and are added to `KickforgeTests`.

## Rules
- Build green + tests pass before each commit. One commit per checklist item: `ID: summary`, tick it in `docs/SPEC.md`.
- Only `git add` your own paths, never `git add -A` (parallel agents share the repo).
- No real-time allocations/locks in `process()`. Param IDs are stable once released (JUCE ParameterID version 1).
