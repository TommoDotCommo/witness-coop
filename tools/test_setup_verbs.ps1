# Setup's scriptable verbs (co-op standard J6, J9) against disposable fixture game folders; no game is started, nothing outside -TestRoot is changed.
#   Witness-Coop-Setup.exe status [--json] [<game folder>] | install <game folder> --yes | uninstall <game folder> --yes
#   exit codes 0 done, 1 failed, 2 usage, 3 unsupported game build, 4 the game is running
# Also: the older -StatusOnly switch, adoption of a release copied in by hand, a mod manager's hard-linked files left alone (Vortex), the window's buttons
# for each of those, and the one-file Setup.
# Mutants (-Mutants), each must fail:  running-code (a running game gives exit 1, not 4), build-code (an unknown build gives exit 1, not 3),
# manager-overwrite (Setup installs over a mod manager's files), manual-blocked (a hand copy is treated like a manager's), options-blocked (the window).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\test_setup_verbs.ps1 -PackageDir <unpacked release> -GameDir <supported build, read only>
#       -TestRoot <scratch on NTFS> [-SingleFile <Witness-Coop-Setup.exe>] [-Mutants]
param([Parameter(Mandatory = $true)][string]$PackageDir, [Parameter(Mandatory = $true)][string]$GameDir,
      [Parameter(Mandatory = $true)][string]$TestRoot, [string]$SingleFile, [switch]$Mutants)
$ErrorActionPreference = 'Stop'
$root = Join-Path $TestRoot ('verbs-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory $root | Out-Null
$script:failures = @()
function Check($cond, $what) { if (-not $cond) { $script:failures += $what } }
$exeSource = Join-Path $GameDir 'witness64_d3d11.exe'

function Run-Setup($exe, [string[]]$arguments) {
    $out = Join-Path $root ([guid]::NewGuid().ToString('N') + '.out')
    $quoted = @($arguments | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } })
    $p = Start-Process -FilePath $exe -ArgumentList $quoted -Wait -PassThru -WindowStyle Hidden -RedirectStandardOutput $out
    $text = if (Test-Path $out) { [IO.File]::ReadAllText($out) } else { '' }
    return [pscustomobject]@{ code = $p.ExitCode; text = $text }
}
function New-Game($name, $exe = $exeSource) {
    $g = Join-Path $root $name
    New-Item -ItemType Directory $g | Out-Null
    New-Item -ItemType HardLink -Path (Join-Path $g 'witness64_d3d11.exe') -Target $exe | Out-Null      # (the exe is only read)
    return $g
}
function Json-Of($r) { try { return $r.text | ConvertFrom-Json } catch { return $null } }
function Gui($pkg, $dir, [switch]$NoOptions) {
    $a = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $pkg 'installer-gui.ps1'), '-TestOwnerPath', $dir)
    if ($NoOptions) { $a += '-TestNoOptions' }
    return (& powershell.exe @a) -join ' '
}
$files = @('dsound.dll', 'witness_launcher.exe', 'witness_models\adventurer.glb', 'witness_models\adventurer.glb.txt')

function Run-Suite($pkg, $label) {
    $script:failures = @()
    $setup = Join-Path $pkg 'Witness-Coop-Setup.exe'
    $g1 = New-Game "$label-g1"
    $r = Run-Setup $setup @('status', '--json', $g1)
    $j = Json-Of $r
    Check ($r.code -eq 0 -and $j -and $j.installs[0].owner -eq 'none' -and $j.installs[0].known_build -and $j.installs[0].build -eq 'Steam build 11370412') "status --json before install: $($r.code) $($r.text)"
    Check ($j -and $j.installs[0].options.desktop_shortcut -is [bool] -and $j.installs[0].options.allow_hosting -is [bool] -and $j.installs[0].options.invite_links -is [bool]) 'status reports all three option states'
    Check ($j -and $j.installs[0].actions.install -eq 'Install' -and $j.package_version -ne 'unknown') 'status: install offered, package version from coop-mod.json'
    $r = Run-Setup $setup @('install', $g1, '--yes')
    Check ($r.code -eq 0) "install --yes exit $($r.code): $($r.text)"
    Check ((Get-FileHash (Join-Path $g1 'dsound.dll')).Hash -eq (Get-FileHash (Join-Path $pkg 'dsound.dll')).Hash) 'install copied dsound.dll'
    Check ($files | ForEach-Object { Test-Path (Join-Path $g1 "$_.coop-receipt.json") } | Where-Object { -not $_ } | Measure-Object | ForEach-Object { $_.Count -eq 0 }) 'install wrote a receipt for every file'
    $j = Json-Of (Run-Setup $setup @('status', '--json', $g1))
    Check ($j -and $j.installs[0].owner -eq 'setup' -and -not $j.installs[0].actions.install -and $j.installs[0].actions.uninstall) 'status --json after install: setup, nothing to do, uninstall possible'
    $gui = Gui $pkg $g1
    Check ($gui -match 'button=Repair\|enabled=True') "a current install can add the ticked options: $gui"
    $gui = Gui $pkg $g1 -NoOptions
    Check ($gui -match 'button=Up to date\|enabled=False') "unticked options do not require repair: $gui"
    $r = Run-Setup 'powershell.exe' @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $pkg 'witness-coop.ps1'), '-GameDir', $g1, '-StatusOnly', '-Yes')
    $j = Json-Of $r
    Check ($r.code -eq 0 -and $j -and $j.items -and $j.actions -and $j.owner -eq 'setup') "-StatusOnly -Yes still works: $($r.code)"
    $r = Run-Setup $setup @('uninstall', $g1, '--yes')
    Check ($r.code -eq 0 -and -not (Test-Path (Join-Path $g1 'dsound.dll')) -and -not (Test-Path (Join-Path $g1 'dsound.dll.coop-receipt.json')) -and -not (Test-Path (Join-Path $g1 'witness_launcher.exe'))) "uninstall --yes: $($r.code) $($r.text)"
    # usage
    Check ((Run-Setup $setup @('frobnicate')).code -eq 2) 'unknown verb: exit 2'
    Check ((Run-Setup $setup @('install')).code -eq 2) 'install without a folder: exit 2'
    # unsupported build: a copy of the exe with one byte changed
    $g2 = Join-Path $root "$label-g2"; New-Item -ItemType Directory $g2 | Out-Null
    $bytes = [IO.File]::ReadAllBytes($exeSource); $bytes[$bytes.Length - 1] = $bytes[$bytes.Length - 1] -bxor 1; [IO.File]::WriteAllBytes((Join-Path $g2 'witness64_d3d11.exe'), $bytes)
    $r = Run-Setup $setup @('install', $g2, '--yes')
    Check ($r.code -eq 3 -and -not (Test-Path (Join-Path $g2 'dsound.dll'))) "unknown build: exit $($r.code), want 3, nothing written"
    # the game running from that folder: a harmless 64-bit stand-in (a copy of ping.exe) whose image sits in the game folder, started and stopped here
    $g4 = Join-Path $root "$label-g4"; New-Item -ItemType Directory $g4 | Out-Null
    Copy-Item $exeSource (Join-Path $g4 'witness64_d3d11.exe')
    Copy-Item (Join-Path $env:SystemRoot 'System32\PING.EXE') (Join-Path $g4 'witness_launcher.exe')      # (the launcher's image counts as the game running, like the game's own)
    $running = Start-Process -FilePath (Join-Path $g4 'witness_launcher.exe') -ArgumentList '-n', '60', '127.0.0.1' -WindowStyle Hidden -PassThru
    try {
        Start-Sleep -Milliseconds 700
        $r = Run-Setup $setup @('install', $g4, '--yes')
        Check ($r.code -eq 4 -and -not (Test-Path (Join-Path $g4 'dsound.dll'))) "game (launcher) running: exit $($r.code), want 4, nothing written"
    } finally { if (-not $running.HasExited) { Stop-Process -Id $running.Id -Force } }
    # A hand copy is adopted; a mod manager's hard-linked deployment stays in charge of its files.
    $g5 = New-Game "$label-g5"
    New-Item -ItemType Directory (Join-Path $g5 'witness_models') | Out-Null
    foreach ($f in $files) { Copy-Item (Join-Path $pkg $f) (Join-Path $g5 $f) }
    $j = Json-Of (Run-Setup $setup @('status', '--json', $g5))
    Check ($j -and $j.installs[0].owner -eq 'manual' -and $j.installs[0].actions.install -eq 'Install') "a hand copy: adoption offered: $($j.installs[0].owner) $($j.installs[0].actions.install)"
    $gui = Gui $pkg $g5
    Check ($gui -match 'enabled=True' -and $gui -match 'Setup will look after these files from now on') "hand copy window: $gui"
    $r = Run-Setup $setup @('install', $g5, '--yes')
    $j = Json-Of (Run-Setup $setup @('status', '--json', $g5))
    Check ($r.code -eq 0 -and $j.installs[0].owner -eq 'setup' -and -not $j.installs[0].actions.install) "a hand copy is adopted: $($r.code)"
    Check ((Run-Setup $setup @('uninstall', $g5, '--yes')).code -eq 0 -and -not (Test-Path (Join-Path $g5 'dsound.dll')) -and -not (Test-Path (Join-Path $g5 'witness_launcher.exe'))) 'an adopted copy is removed on uninstall'
    $g6 = New-Game "$label-g6"
    New-Item -ItemType Directory (Join-Path $g6 'witness_models') | Out-Null
    foreach ($f in $files) { New-Item -ItemType HardLink -Path (Join-Path $g6 $f) -Target (Join-Path $pkg $f) | Out-Null }
    $before = (Get-FileHash (Join-Path $g6 'dsound.dll')).Hash
    $j = Json-Of (Run-Setup $setup @('status', '--json', $g6))
    Check ($j -and $j.installs[0].owner -eq 'vortex' -and -not $j.installs[0].actions.install -and -not $j.installs[0].actions.uninstall -and -not $j.installs[0].actions.blocked) "hard-linked files are a mod manager's: $($j.installs[0].owner)"
    $r = Run-Setup $setup @('install', $g6, '--yes')
    Check ($r.code -eq 1 -and -not (Test-Path (Join-Path $g6 'dsound.dll.coop-receipt.json')) -and (Get-FileHash (Join-Path $g6 'dsound.dll')).Hash -eq $before) "a mod manager's files are not overwritten: $($r.code)"
    $r = Run-Setup $setup @('uninstall', $g6, '--yes')
    Check ($r.code -eq 1 -and (Test-Path (Join-Path $g6 'dsound.dll'))) "a mod manager's files are not removed: $($r.code)"
    $gui = Gui $pkg $g6
    Check ($gui -match 'button=By Vortex\|enabled=False\|uninstall=False\|play=True\|status=Installed by Vortex: update or remove it there') "Vortex window: $gui"
    return $script:failures
}

try {
    $bad = @(Run-Suite $PackageDir 'zip')
    if ($bad) { throw ('FAIL (zip Setup): ' + ($bad -join '; ')) }
    Write-Output 'PASS: zip Setup verbs: status --json, install/uninstall --yes, exit codes 2/3/4, -StatusOnly alias, a hand copy adopted, Vortex recognised and left alone, the window for each'
    if ($SingleFile) {
        $single = Join-Path $root 'single'
        New-Item -ItemType Directory $single | Out-Null
        Copy-Item $SingleFile (Join-Path $single 'Witness-Coop-Setup.exe')
        $g = New-Game 'single-g'
        $exe = Join-Path $single 'Witness-Coop-Setup.exe'
        $r = Run-Setup $exe @('install', $g, '--yes')
        $j = Json-Of (Run-Setup $exe @('status', '--json', $g))
        if ($r.code -ne 0 -or -not $j -or $j.installs[0].owner -ne 'setup') { throw "FAIL (single-file Setup): install $($r.code), status $($j | ConvertTo-Json -Compress -Depth 3)" }
        if ((Run-Setup $exe @('uninstall', $g, '--yes')).code -ne 0) { throw 'FAIL (single-file Setup): uninstall' }
        if ((Run-Setup $exe @('frobnicate')).code -ne 2) { throw 'FAIL (single-file Setup): usage exit code' }
        Write-Output 'PASS: single-file Setup runs the verbs from its unpacked payload and cleans up after itself'
    }
    if ($Mutants) {
        $twins = @(
            @('manual-blocked', 'witness-coop.ps1', "if (`$owner -eq 'vortex') {", "if (`$owner -in 'manual', 'vortex') {"),
            @('running-code', 'witness-coop.ps1', "Close it and run this again.' `$ExitRunning }", "Close it and run this again.' }"),
            @('build-code', 'witness-coop.ps1', "(Get-WitnessBuildReport `$game)) `$ExitUnknownBuild }", "(Get-WitnessBuildReport `$game)) }"),
            @('manager-overwrite', 'witness-coop.ps1', "    if (`$owner -eq 'vortex') {`n        Fail 'Installed by Vortex: update or remove it there.", "    if (`$false) {`n        Fail 'Installed by Vortex: update or remove it there."),
            @('owner-by-links', 'install-state.ps1', "-gt 1) { return 'vortex' }", "-gt 99) { return 'vortex' }")
        )
        foreach ($t in $twins) {
            $m = Join-Path $root ('mutant-' + $t[0])
            Copy-Item $PackageDir $m -Recurse
            $ps = Join-Path $m $t[1]
            $text = [IO.File]::ReadAllText($ps)
            $crlf = $text.Contains("`r`n")
            $want = $t[2]; $new = $t[3]
            if ($crlf) { $want = $want.Replace("`n", "`r`n"); $new = $new.Replace("`n", "`r`n") }
            if (-not $text.Contains($want)) { throw "mutant $($t[0]) does not apply" }
            [IO.File]::WriteAllText($ps, $text.Replace($want, $new))
            $bad = @(Run-Suite $m $t[0])
            if (-not $bad) { throw "FAIL: mutant $($t[0]) survived" }
            Write-Output "KILLED: $($t[0]) ($($bad[0]))"
        }
        $m = Join-Path $root 'mutant-options-blocked'
        Copy-Item $PackageDir $m -Recurse
        $ps = Join-Path $m 'installer-gui.ps1'
        $text = [IO.File]::ReadAllText($ps)
        $needle = "if (`$snapshot.owner -ne 'vortex' -and `$snapshot.build -eq 'current' -and -not `$snapshot.actions.blocked"
        if (-not $text.Contains($needle)) { throw 'options mutant does not apply' }
        [IO.File]::WriteAllText($ps, $text.Replace($needle, "if (`$false -and -not `$snapshot.actions.blocked"))
        $bad = @(Run-Suite $m 'options-blocked')
        if (-not $bad) { throw 'FAIL: options-blocked mutant survived' }
        Write-Output "KILLED: options-blocked ($($bad[0]))"
    }
} finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
