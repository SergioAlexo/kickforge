---
name: match-kick
description: Match Kickforge's synthesized kick to a real reference kick (WAV/AIFF/FLAC) by measuring with kickforge-cli and adjusting params/curves, live in the running plugin or offline. Use when the user asks to match, recreate, clone, reverse-engineer or "sound like" a kick sample, or mentions the Kickforge reference/Live link.
---

# Match a reference kick

Tool: `build\Release\kickforge-cli.exe` (build: `cmake --build build --config Release --target KickforgeCli -- /m` from PowerShell).
Everything prints JSON. `kickforge-cli help <command>` documents each command; the folder contract is in `docs/AI-LINK.md`.

## 1. Find out where we are
```
kickforge-cli status
```
- `linked: true` + `reference` set → **live mode**: the user has the plugin open with a Reference loaded and Live link on. Work on the default state (`Live/state.kfpreset`) and push with `--apply`; the user hears every applied step in REAPER.
- Otherwise → **offline mode**: ask for the reference file path, then `kickforge-cli init [--preset "<name>"] --out work.kfpreset` and pass `--state work.kfpreset --ref <wav>` to every command. Offer `apply` at the end once they enable Live link.
- If the reference is a loop or has several hits, ask which one (or use `analyze` and pick the strongest) — the plugin's Reference region and `--region a:b` handle this.

## 2. Look before changing anything
```
kickforge-cli analyze <ref.wav>          # tail note/Hz, pitch drop, envelope, spectrum, transient share
kickforge-cli compare                    # score 0..100 + per-part scores + hints
kickforge-cli plot --out <scratch>\plot.png   # then Read the PNG
```
Tell the user in plain words what the reference is (e.g. "G1 tail ≈ 48 Hz, ~16 st pitch drop over 50 ms, 0.5 s long, very little click").

## 3. Get close fast, then refine
1. `kickforge-cli import --out <state>` (offline) or `import --apply` (live): fits pitch/amp curves, root/tune, harmonics and puts the top end into click layer 1. Usually lands at 85–92.
2. `kickforge-cli fit --out|--apply` — optimiser (default budget 200 renders ≈ 2 s): tune, length, gain, curve points. Use `--params`/`--curves` to focus on what `compare` says is worst.
3. Hand-edit what the optimiser can't reach, guided by `hints` and the plot:
   - pitch wrong at specific times → `curve get pitch`, edit the points/tension in that span, `curve set pitch @file.json`
   - tail pitch → `sub_tune` / `sub_fine` (not the curve)
   - body too thin/fat → `sub_hN_*` harmonics, `sub_drive` + Drive curve, `sub_wave`
   - click/top end → click layer gen type, tone, decay, filter (`params --filter click_1`)
   - level/tail → Amp curve, `sub_length`, master FX are usually off for matching — keep them off unless the reference is clearly saturated/limited.
4. Re-run `compare` after every change; keep a change only if the score goes up (or the user prefers it by ear). Stop at ~95 or when the user is happy — the score is a guide, the user's ears decide.

## Rules
- Without `--out` or `--apply` nothing is written (dry run) — use that to try ideas.
- Live mode: apply small steps and say what changed ("lowered the pitch drop 20–35 ms by ~1.5 st"), so the user can A/B in the plugin (Synth / Reference / Both).
- Never overwrite the user's presets; save results with `--out` under a name they choose, or let them save in the plugin.
- `import` writes the extracted top-end WAV to `%APPDATA%\Kickforge\Imports` — mention it if they later move the project.
