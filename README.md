# Kickforge

An open-source kick-drum synthesizer plugin (VST3 + Standalone) for Windows, built with JUCE 8.

- **Sub / body oscillator** with drawable pitch and amp curves, key tracking, fine tune, phase control
- **Click layers**: load your own samples or use the built-in synthesized clicks (noise burst, impulse, filtered transient)
- **FX chain**: distortion (several types), EQ, compressors, brickwall limiter, oversampling
- **Presets**, render-to-WAV and drag-out of the rendered kick
- **AI link**: `kickforge-cli` can analyse a reference kick and push matching settings into a running plugin (see [docs/AI-LINK.md](docs/AI-LINK.md))

> Status: early (v0.1.0). The engine has an automated test suite, but the plugin has had little real-world DAW use yet — bug reports welcome.

## Install (Windows x64)

1. Download `Kickforge-<version>-win64.zip` from [Releases](https://github.com/SergioAlexo/kickforge/releases).
2. Copy `Kickforge.vst3` to `C:\Program Files\Common Files\VST3`.
3. Rescan plugins in your DAW (REAPER: Preferences → Plug-ins → VST → Re-scan).

The zip also contains `Kickforge.exe` (standalone, no DAW needed) and `kickforge-cli.exe`.

## Build from source

Requires CMake 3.22+ and Visual Studio 2022 (or the Build Tools) with the C++ workload. JUCE 8.0.8 and Catch2 are fetched automatically.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target Kickforge_VST3 Kickforge_Standalone KickforgeCli -- /m
cmake --build build --config Release --target KickforgeTests && build\Release\KickforgeTests.exe
```

Outputs land in `build\Kickforge_artefacts\Release\`. `scripts\install-vst3.ps1` (elevated PowerShell) copies the VST3 into the system folder.

## License

Kickforge is free software under the [GNU Affero General Public License v3.0](LICENSE) (AGPL-3.0), the license JUCE 8 requires for open-source projects.

VST is a registered trademark of Steinberg Media Technologies GmbH. Kickforge is an independent project, not affiliated with or endorsed by Sonic Academy or any other plugin vendor; product names mentioned in the docs are used only to describe features.
