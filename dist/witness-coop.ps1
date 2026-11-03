# witness-coop installer for The Witness (Steam build 11370412). Windows PowerShell 5.1, no other tools.
# Run by Witness-Coop-Setup.exe (the player-facing installer); the command line below is for scripts and tests:
#   powershell -ExecutionPolicy Bypass -File witness-coop.ps1 [-Uninstall] [-GameDir <folder>] [-Yes]
# The verbs every co-op Setup takes (Witness-Coop-Setup.exe passes them on unchanged; no window):
#   witness-coop.ps1 status [--json] [<game folder>]     every install found (or the one given): build, who installed co-op, options
#   witness-coop.ps1 install <game folder> --yes
#   witness-coop.ps1 uninstall <game folder> --yes
#   exit codes: 0 done, 1 failed, 2 usage, 3 unsupported game build, 4 the game is running
# What it changes, all in the game folder: dsound.dll (the co-op proxy; it forwards to Windows' own dsound.dll), witness_launcher.exe
# and witness_models\ are added. The game's own files are not changed. A dsound.dll that was there before (another mod) is kept as
# dsound.chain.dll and loaded by the proxy. An older co-op install (the proxy in AnselSDK64.dll, the game's file renamed
# AnselSDK64_real.dll) is migrated: the game's own AnselSDK64.dll goes back, verified by its SHA-256.
# Optional Windows integrations, only when asked for (the window's three ticks; the verbs leave them alone): a desktop shortcut, the Windows Firewall rule
# "The Witness Co-op (UDP)" for the game and the launcher on Private and Public networks, and the witness-coop:// link handler (HKCU). Uninstall removes all three.
# Files that a mod manager (Vortex: hard links) put into the game folder are its own: Setup says so and changes nothing. A release copied in by hand is
# adopted (a receipt is written) and then updated like any Setup install.
# Nothing else: no saves, no files outside the game folder. The settings (witness_coop.cfg) and logs live in %LOCALAPPDATA%\witness-coop and are written by the launcher.
param(
    [Parameter(Position = 0)][string]$Verb,       # status | install | uninstall (empty: the switches below decide)
    [Parameter(Position = 1)][string]$GameDir,
    [switch]$Json,           # status: one JSON object
    [switch]$Uninstall,
    [switch]$StatusOnly,     # read-only JSON inventory of one folder (the older switch); no changes
    [switch]$GameFilesOnly,  # GUI elevation helper: the game folder and the firewall rule (the administrator's own profile is never touched)
    [switch]$UserOptionsOnly, # GUI: the shortcut and the link handler, in the player's own session
    [switch]$DesktopShortcut,
    [switch]$AllowHosting,
    [switch]$InviteLinks,
    [switch]$FindOnly,       # print the game folder that would be used and stop
    [switch]$Yes             # no questions
)
$ErrorActionPreference = 'Stop'
$ExitFailed = 1; $ExitUsage = 2; $ExitUnknownBuild = 3; $ExitRunning = 4
if ($Verb -and $Verb -notin 'status', 'install', 'uninstall') {
    Write-Host "Unknown command '$Verb'. Use: status [--json] [<game folder>] | install <game folder> --yes | uninstall <game folder> --yes"
    exit $ExitUsage
}
if ($Verb -in 'install', 'uninstall' -and -not $GameDir) { Write-Host "$Verb needs the game folder: $Verb <game folder> --yes"; exit $ExitUsage }
if ($Verb -eq 'uninstall') { $Uninstall = $true }
if ($Verb) { $Yes = $true }     # the verbs never ask (--yes is accepted for every co-op Setup's same words)
$AppId = '210970'
$Here  = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $Here 'install-state.ps1')

function Say($msg, $color = 'Gray') { Write-Host $msg -ForegroundColor $color }
function Fail($msg, $code = $ExitFailed) { Say "`n$msg" Red; if (-not $Yes) { Read-Host 'Press Enter to close' | Out-Null }; exit $code }

# ---- finding the game (install-state.ps1 knows Steam libraries, GOG, Epic) ----

function Find-Game { return @(Find-AllGames | Select-Object -First 1)[0] }

function Resolve-GameDir {
    $d = $GameDir
    if (-not $d) { $d = Find-Game }
    if (-not $d) {
        Say 'The Witness was not found in your Steam libraries, GOG or Epic folders.' Yellow
        if ($Yes) { Fail 'Give the game folder.' }
        $d = Read-Host 'Paste the folder of The Witness (the one with witness64_d3d11.exe)'
    }
    $d = $d.Trim('"', ' ')
    if (-not (Test-Path (Join-Path $d 'witness64_d3d11.exe'))) { Fail "No witness64_d3d11.exe in '$d'." }
    return (Resolve-Path $d).ProviderPath
}

function Check-NotRunning($dir) {
    $running = Get-Process witness64_d3d11, witness_launcher -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($dir, 'OrdinalIgnoreCase') }
    if ($running) { Fail 'The Witness (or its co-op launcher) is running from that folder. Close it and run this again.' $ExitRunning }
}

# ---- install / uninstall ----

function Do-Install {
    Say "`nwitness-coop installer" Cyan
    $game = Resolve-GameDir
    Say "Game folder: $game"
    if ($FindOnly) { return }
    $build = Get-WitnessBuildState $game
    if ($build -ne 'current') { Fail (Format-WitnessBuildRefusal (Get-WitnessBuildReport $game)) $ExitUnknownBuild }
    if ($UserOptionsOnly) { Set-CoopUserOptions $game ([bool]$DesktopShortcut) ([bool]$InviteLinks); return }
    Check-NotRunning $game
    foreach ($f in Get-CoopPackageFiles $Here) {
        if (-not (Test-Path (Join-Path $Here $f))) { Fail "$f is missing next to this script. Unpack the whole zip first." }
    }
    $owner = Get-CoopOwner $game $Here
    if ($owner -eq 'vortex') {
        Fail 'Installed by Vortex: update or remove it there. The files in this game folder are linked from the mod manager, so Setup leaves them alone (the launcher still starts).'
    }
    if ($owner -eq 'manual') { Adopt-CoopInstall $game $Here; Say 'These files were copied in by hand and match this release. Setup looks after them from now on.' }
    $legacy = Get-WitnessLegacy $game
    if ($legacy.blocked) { Fail "An older co-op install is in $game, and Setup cannot undo it safely:`n$($legacy.blocked)" }

    Say "`nInstalling into $game"
    if (Invoke-WitnessMigration $game) { Say '  AnselSDK64.dll: the game''s own file is back (verified); the old co-op proxy is removed' }
    if (Install-WitnessChain $game) { Say "  $($script:WitnessChain): the dsound.dll that was here (another mod) is kept and loaded by the co-op proxy" }
    foreach ($relative in Get-CoopPackageFiles $Here) {
        Install-CoopFile (Join-Path $game $relative) ([IO.File]::ReadAllBytes((Join-Path $Here $relative)))
        Say "  $relative"
    }
    if ($AllowHosting) { Set-CoopFirewall $game $false; Say '  Windows Firewall: The Witness Co-op (UDP) allowed for the game and the launcher (Private and Public)' }
    if (-not $GameFilesOnly) { Set-CoopUserOptions $game ([bool]$DesktopShortcut) ([bool]$InviteLinks) }
    Say "`nDone. Start witness_launcher.exe in the game folder (or Play from setup): host a lobby and send the invite," Green
    Say 'or paste a friend''s invite and join. The lobby starts the game for everyone.' Green
    if (-not $Yes) { Read-Host "`nPress Enter to close" | Out-Null }
}

function Do-Uninstall {
    Say "`nwitness-coop uninstall" Cyan
    $game = Resolve-GameDir
    Say "Game folder: $game"
    if ($FindOnly) { return }
    if ($UserOptionsOnly) { Remove-CoopUserOptions $game; return }
    Check-NotRunning $game
    if ((Get-CoopOwner $game $Here) -eq 'vortex') { Fail 'Installed by Vortex: remove it there. Setup leaves the mod manager''s files alone.' }
    # the proxy first (a dsound.dll that was there before comes back under its name), then the chain copy and the rest
    foreach ($relative in @('dsound.dll', $script:WitnessChain) + @(Get-CoopPackageFiles $Here | Where-Object { $_ -ne 'dsound.dll' })) {
        $target = Join-Path $game $relative
        if (Remove-CoopFile $target) { Say "  restored/removed $relative" }
        elseif (Test-Path -LiteralPath $target) { Say "  no ownership receipt for ${relative}: kept it; install this package first to establish ownership" Yellow }
    }
    # an older co-op install's layout is undone the same way as on install (the game's own AnselSDK64.dll back, verified)
    $legacy = Get-WitnessLegacy $game
    if ($legacy.blocked) { Say "  $($legacy.blocked)" Yellow }
    elseif (Invoke-WitnessMigration $game) { Say '  restored the game''s own AnselSDK64.dll (from an older co-op install)' }
    $firewall = $false; foreach ($program in Get-CoopFirewallPrograms $game) { if (@(Get-CoopFirewallRules $program).Count) { $firewall = $true } }
    if ($firewall) {
        try { Set-CoopFirewall $game $true; Say '  Windows Firewall: the rule The Witness Co-op (UDP) is removed' }
        catch { Say '  Windows Firewall: the rule The Witness Co-op (UDP) is still there (removing it needs administrator permission); delete it in Windows Security if you like.' Yellow }
    }
    if (-not $GameFilesOnly) { Remove-CoopUserOptions $game }
    $models = Join-Path $game 'witness_models'
    if ((Test-Path -LiteralPath $models) -and -not (Get-ChildItem -LiteralPath $models -Force)) { Remove-Item -LiteralPath $models }
    Say "`nCo-op is off. Your co-op settings and logs are kept in %LOCALAPPDATA%\witness-coop; delete that folder if you like." Green
    if (-not $Yes) { Read-Host "`nPress Enter to close" | Out-Null }
}

# status: every install found (or the one given), what is there and who put it there
function Show-Status {
    $dirs = if ($GameDir) {
        $d = $GameDir.Trim('"', ' ')
        if (-not (Test-Path (Join-Path $d 'witness64_d3d11.exe'))) { Write-Host "No witness64_d3d11.exe in '$d'."; exit $ExitFailed }
        @((Resolve-Path $d).ProviderPath)
    } else { Find-AllGames }
    $manifest = Read-CoopManifest $Here
    $installs = @()
    foreach ($d in $dirs) {
        $snap = Get-WitnessInstallStatus $d $Here
        $report = Get-WitnessBuildReport $d
        $installs += [pscustomobject]@{ path=$d; build=$(if ($snap.build -eq 'current') { 'Steam build 11370412' } elseif ($snap.build -eq 'conflict') { 'unsupported' } else { 'unknown' })
            known_build=($snap.build -eq 'current'); store=$report.store; owner=$snap.owner; options=$snap.options
            chain_dll=$snap.chain; actions=$snap.actions; items=$snap.items }
    }
    $result = [pscustomobject]@{ schema=1; tool='Witness-Coop-Setup'; package_version=$(if ($manifest) { $manifest.version } else { 'unknown' }); installs=$installs }
    if ($Json) { $result | ConvertTo-Json -Depth 6; return }
    if (-not $installs) { Write-Host 'The Witness was not found in the Steam libraries, GOG or Epic folders; give the game folder.' }
    foreach ($i in $installs) {
        $what = $script:CoopOwnerText[$i.owner]
        $next = if ($i.actions.blocked) { 'needs attention' } elseif ($i.actions.install) { "Setup can: $($i.actions.install)" } else { 'up to date' }
        Write-Host "$($i.path): $($i.build) ($($i.store)); $what; $next"
        Write-Host "Desktop shortcut: $($i.options.desktop_shortcut); allow hosting: $($i.options.allow_hosting); open invite links: $($i.options.invite_links)"
    }
}

try {
    if ($Verb -eq 'status') { Show-Status }
    elseif ($StatusOnly) {
        Get-WitnessInstallStatus (Resolve-GameDir) $Here | ConvertTo-Json -Depth 6
    } elseif ($Uninstall) { Do-Uninstall } else { Do-Install }
} catch {
    Fail "Something went wrong: $($_.Exception.Message)"
}
exit 0
