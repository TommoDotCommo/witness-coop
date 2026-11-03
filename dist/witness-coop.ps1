# witness-coop installer for The Witness (Steam build 11370412). Windows PowerShell 5.1, no other tools.
# Run by Witness-Coop-Setup.exe (the player-facing installer); the command line below is for scripts and tests:
#   powershell -ExecutionPolicy Bypass -File witness-coop.ps1 [-Uninstall] [-GameDir <folder>] [-Yes]
# What it changes, all in the game folder: dsound.dll (the co-op proxy; it forwards to Windows' own dsound.dll), witness_launcher.exe
# and witness_models\ are added. The game's own files are not changed. A dsound.dll that was there before (another mod) is kept as
# dsound.chain.dll and loaded by the proxy. An older co-op install (the proxy in AnselSDK64.dll, the game's file renamed
# AnselSDK64_real.dll) is migrated: the game's own AnselSDK64.dll goes back, verified by its SHA-256.
# Nothing else: no registry, no saves, no files outside the game folder. witness_coop.cfg is written by the launcher.
param(
    [switch]$Uninstall,
    [switch]$StatusOnly,     # read-only JSON inventory; no changes
    [string]$GameDir,
    [switch]$GameFilesOnly,  # GUI elevation helper (the only step there is; accepted for symmetry with the setup window)
    [switch]$FindOnly,       # print the game folder that would be used and stop
    [switch]$Yes             # no questions
)
$ErrorActionPreference = 'Stop'
$AppId = '210970'
$Here  = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $Here 'install-state.ps1')

function Say($msg, $color = 'Gray') { Write-Host $msg -ForegroundColor $color }
function Fail($msg) { Say "`n$msg" Red; if (-not $Yes) { Read-Host 'Press Enter to close' | Out-Null }; exit 1 }

# ---- finding the game ----

function Steam-Libraries {
    $roots = @()
    foreach ($k in 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam', 'HKLM:\SOFTWARE\Valve\Steam', 'HKCU:\Software\Valve\Steam') {
        $p = Get-ItemProperty $k -ErrorAction SilentlyContinue
        if ($p.SteamPath)   { $roots += ($p.SteamPath -replace '/', '\') }
        if ($p.InstallPath) { $roots += $p.InstallPath }
    }
    $libs = @()
    foreach ($r in ($roots | Select-Object -Unique)) {
        $libs += $r
        $vdf = Join-Path $r 'steamapps\libraryfolders.vdf'
        if (Test-Path $vdf) {
            foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) {
                $libs += ($m.Groups[1].Value -replace '\\\\', '\')
            }
        }
    }
    $libs | Where-Object { $_ } | ForEach-Object { $_.TrimEnd('\') } | Select-Object -Unique
}

function Find-Game {
    foreach ($lib in Steam-Libraries) {
        $acf = Join-Path $lib "steamapps\appmanifest_$AppId.acf"
        $dirs = @()
        if (Test-Path $acf) {
            $m = [regex]::Match((Get-Content $acf -Raw), '"installdir"\s+"([^"]+)"')
            if ($m.Success) { $dirs += Join-Path $lib "steamapps\common\$($m.Groups[1].Value)" }
        }
        $dirs += Join-Path $lib 'steamapps\common\The Witness'
        foreach ($d in $dirs) { if (Test-Path (Join-Path $d 'witness64_d3d11.exe')) { return $d } }
    }
    return $null
}

function Resolve-GameDir {
    $d = $GameDir
    if (-not $d) { $d = Find-Game }
    if (-not $d) {
        Say 'The Witness was not found in your Steam libraries.' Yellow
        if ($Yes) { Fail 'Give the game folder with -GameDir.' }
        $d = Read-Host 'Paste the folder of The Witness (the one with witness64_d3d11.exe)'
    }
    $d = $d.Trim('"', ' ')
    if (-not (Test-Path (Join-Path $d 'witness64_d3d11.exe'))) { Fail "No witness64_d3d11.exe in '$d'." }
    return (Resolve-Path $d).ProviderPath
}

function Check-NotRunning($dir) {
    $running = Get-Process witness64_d3d11, witness_launcher -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($dir, 'OrdinalIgnoreCase') }
    if ($running) { Fail 'The Witness (or its co-op launcher) is running from that folder. Close it and run this again.' }
}

# ---- install / uninstall ----

function Do-Install {
    Say "`nwitness-coop installer" Cyan
    $game = Resolve-GameDir
    Say "Game folder: $game"
    if ($FindOnly) { return }
    Check-NotRunning $game
    $build = Get-WitnessBuildState $game
    if ($build -ne 'current') { Fail (Format-WitnessBuildRefusal (Get-WitnessBuildReport $game)) }
    foreach ($f in $script:WitnessManaged) {
        if (-not (Test-Path (Join-Path $Here $f))) { Fail "$f is missing next to this script. Unpack the whole zip first." }
    }
    $legacy = Get-WitnessLegacy $game
    if ($legacy.blocked) { Fail "An older co-op install is in $game, and Setup cannot undo it safely:`n$($legacy.blocked)" }

    Say "`nInstalling into $game"
    if (Invoke-WitnessMigration $game) { Say '  AnselSDK64.dll: the game''s own file is back (verified); the old co-op proxy is removed' }
    if (Install-WitnessChain $game) { Say "  $($script:WitnessChain): the dsound.dll that was here (another mod) is kept and loaded by the co-op proxy" }
    foreach ($relative in $script:WitnessManaged) {
        Install-CoopFile (Join-Path $game $relative) ([IO.File]::ReadAllBytes((Join-Path $Here $relative)))
        Say "  $relative"
    }
    Say "`nDone. Start witness_launcher.exe in the game folder (or Play from setup): host a lobby and send the invite," Green
    Say 'or paste a friend''s invite and join. The lobby starts the game for everyone.' Green
    if (-not $Yes) { Read-Host "`nPress Enter to close" | Out-Null }
}

function Do-Uninstall {
    Say "`nwitness-coop uninstall" Cyan
    $game = Resolve-GameDir
    Say "Game folder: $game"
    if ($FindOnly) { return }
    Check-NotRunning $game
    # the proxy first (a dsound.dll that was there before comes back under its name), then the chain copy and the rest
    foreach ($relative in @('dsound.dll', $script:WitnessChain, 'witness_launcher.exe', 'witness_models\adventurer.glb', 'witness_models\adventurer.glb.txt')) {
        $target = Join-Path $game $relative
        if (Remove-CoopFile $target) { Say "  restored/removed $relative" }
        elseif (Test-Path -LiteralPath $target) { Say "  no ownership receipt for ${relative}: kept it; install this package first to establish ownership" Yellow }
    }
    # an older co-op install's layout is undone the same way as on install (the game's own AnselSDK64.dll back, verified)
    $legacy = Get-WitnessLegacy $game
    if ($legacy.blocked) { Say "  $($legacy.blocked)" Yellow }
    elseif (Invoke-WitnessMigration $game) { Say '  restored the game''s own AnselSDK64.dll (from an older co-op install)' }
    $models = Join-Path $game 'witness_models'
    if ((Test-Path -LiteralPath $models) -and -not (Get-ChildItem -LiteralPath $models -Force)) { Remove-Item -LiteralPath $models }
    Say "`nCo-op is off. witness_coop.cfg and witness_proxy.log are still in the game folder; delete them if you like." Green
    if (-not $Yes) { Read-Host "`nPress Enter to close" | Out-Null }
}

try {
    if ($StatusOnly) {
        Get-WitnessInstallStatus (Resolve-GameDir) $Here | ConvertTo-Json -Depth 6
    } elseif ($Uninstall) { Do-Uninstall } else { Do-Install }
} catch {
    Fail "Something went wrong: $($_.Exception.Message)"
}
