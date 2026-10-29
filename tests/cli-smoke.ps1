# Smoke test of the real kickforge-cli.exe. Usage: pwsh tests\cli-smoke.ps1 [-Exe build\Release\kickforge-cli.exe]
# Needs a state + reference: run `KickforgeTests "[cli]"` with env KF_CLI_KEEP_PLOT=<dir>\p.png first (writes smoke_*.{kfpreset,wav}).
param([string]$Exe = "build\Release\kickforge-cli.exe", [string]$Dir = ".")
$env:KICKFORGE_LIVE_DIR = Join-Path $Dir "smoke_live"
$s = Join-Path $Dir "smoke_synth.kfpreset"; $r = Join-Path $Dir "smoke_ref.wav"
& $Exe --version
& $Exe compare --state $s --ref $r
& $Exe plot --state $s --ref $r --out (Join-Path $Dir "smoke_plot.png")
& $Exe fit --state $s --ref $r --iters 100 --out (Join-Path $Dir "smoke_fit.kfpreset")
& $Exe apply --state (Join-Path $Dir "smoke_fit.kfpreset") --timeout 0.5   # no plugin: applied=false, exit 0
