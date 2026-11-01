# Checks of the one-file Setup.exe and the zip's thin Setup.exe on disposable folders (no game, no real install).
#   powershell -File tools\test_setup_exe.ps1 -Exes <folder with one.exe corrupt.exe thin.exe runner.exe> -Scenario all|hash|links|reuse|zip|run -TestRoot <scratch>
# A folder of mutant builds (tools/qa_installer.sh) goes through the same scenario and the script is EXPECTED to fail there (the caller checks that).
param(
    [Parameter(Mandatory=$true)][string]$Exes,
    [Parameter(Mandatory=$true)][string]$TestRoot,
    [string]$Scenario = 'all',
    [string]$PackageDir = ''
)
$ErrorActionPreference = 'Stop'
$root = Join-Path $TestRoot "setup-exe-$PID"
New-Item -ItemType Directory -Force $root | Out-Null
$script:checks = 0
function Check($ok, $what) { if (-not $ok) { throw "FAIL: $what" }; $script:checks++ }
function Pick($name) { return (Join-Path $Exes $name) }
function Fresh($name) { $d = Join-Path $root $name; if (Test-Path $d) { Remove-Item -Recurse -Force $d }; New-Item -ItemType Directory $d | Out-Null; return $d }
function Run-Setup($exe, $localAppData, $arguments, $extraEnv = @{}) {
    $msg = Join-Path $root ([guid]::NewGuid().ToString('N') + '.txt')
    $saved = @{}; $env:LOCALAPPDATA = $localAppData; $env:WITNESS_SETUP_MESSAGE_FILE = $msg
    foreach ($k in $extraEnv.Keys) { $saved[$k] = [Environment]::GetEnvironmentVariable($k); [Environment]::SetEnvironmentVariable($k, $extraEnv[$k]) }
    try {
        $p = Start-Process -FilePath $exe -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden
        $text = if (Test-Path $msg) { [IO.File]::ReadAllText($msg) } else { '' }
        return [pscustomobject]@{ code = $p.ExitCode; message = $text }
    } finally { foreach ($k in $saved.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k]) }; $env:WITNESS_SETUP_MESSAGE_FILE = $null }
}
function Hash($f) { (Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLowerInvariant() }
function Make-Junction($link, $target) { New-Item -ItemType Directory -Force $target | Out-Null; & cmd.exe /c mklink /J "`"$link`"" "`"$target`"" | Out-Null; Check (Test-Path $link) 'junction planted' }
function Files-Under($d) { if (Test-Path $d) { @(Get-ChildItem -Recurse -Force -File $d) } else { @() } }

if ($Scenario -in 'all', 'run') {
    # the one-file setup, started for real with a stand-in window script: unpacks, runs it, removes what it unpacked
    $exe = Pick 'runner.exe'; $lad = Fresh 'lad-run'; $marker = Join-Path $root 'marker.txt'; if (Test-Path $marker) { Remove-Item $marker }
    $r = Run-Setup $exe $lad '' @{ WITNESS_SETUP_MARKER = $marker }
    Check ($r.code -eq 0) "run: exit code $($r.code)"
    Check ((Test-Path $marker) -and ([IO.File]::ReadAllText($marker)) -match 'witness-coop\\setup\\v0\.0\.0-test\\?$') 'run: the window script ran from the per-version folder under %LOCALAPPDATA%\witness-coop\setup'
    Check (-not (Test-Path (Join-Path $lad 'witness-coop\setup\v0.0.0-test'))) 'run: the unpacked folder is removed after the window closes'
}
if ($Scenario -in 'all', 'hash') {
    $exe = Pick 'one.exe'; $lad = Fresh 'lad-ok'
    $r = Run-Setup $exe $lad '--extract-only'
    Check ($r.code -eq 0 -and $r.message -match 'extracted: ') "extract: exit $($r.code) $($r.message)"
    $dir = ($r.message -replace '(?s).*extracted: ', '').Trim()
    Check ($dir -eq (Join-Path $lad 'witness-coop\setup\v0.0.0-test')) "extract: lands in %LOCALAPPDATA%\witness-coop\setup\<version> ($dir)"
    if ($PackageDir) {
        foreach ($f in 'AnselSDK64.dll', 'witness_launcher.exe', 'install-state.ps1', 'witness-coop.ps1', 'installer-gui.ps1', 'install-policy.exe', 'witness_models\adventurer.glb') {
            Check ((Test-Path (Join-Path $dir $f)) -and (Hash (Join-Path $dir $f)) -eq (Hash (Join-Path $PackageDir $f))) "extract: $f identical to the package's"
        }
    }
    $again = Run-Setup $exe $lad '--extract-only'
    Check ($again.code -eq 0 -and $again.message -match 'v0\.0\.0-test-2') 'a second run gets a fresh folder, never the old one'
    Check (@(Files-Under $dir).Count -gt 0) 'the first folder is untouched'
    # damaged payload
    $bad = Pick 'corrupt.exe'; $lad2 = Fresh 'lad-bad'
    $r = Run-Setup $bad $lad2 '--extract-only'
    Check ($r.code -ne 0 -and $r.message -match 'damaged') "a payload that does not match its checksum is refused (exit $($r.code))"
    Check (@(Files-Under $lad2).Count -eq 0) 'a damaged payload writes nothing'
}
if ($Scenario -in 'all', 'links') {
    $exe = Pick 'one.exe'; $lad = Fresh 'lad-link'; $outside = Join-Path $root 'outside-links'; if (Test-Path $outside) { Remove-Item -Recurse -Force $outside }
    Make-Junction (Join-Path $lad 'witness-coop') $outside
    $r = Run-Setup $exe $lad '--extract-only'
    Check ($r.code -ne 0) "a planted link on the way is refused (exit $($r.code))"
    Check (@(Files-Under $outside).Count -eq 0) "nothing was written through the link ($(@(Files-Under $outside).Count) files outside)"
}
if ($Scenario -in 'all', 'reuse') {
    $exe = Pick 'one.exe'; $lad = Fresh 'lad-reuse'; $outside = Join-Path $root 'outside-reuse'; if (Test-Path $outside) { Remove-Item -Recurse -Force $outside }
    New-Item -ItemType Directory -Force (Join-Path $lad 'witness-coop\setup') | Out-Null
    Make-Junction (Join-Path $lad 'witness-coop\setup\v0.0.0-test') $outside
    $r = Run-Setup $exe $lad '--extract-only'
    Check (@(Files-Under $outside).Count -eq 0) "a folder planted where the setup folder goes is not used ($(@(Files-Under $outside).Count) files outside)"
    Check ($r.code -eq 0 -and $r.message -match 'v0\.0\.0-test-2') 'the setup takes the next free folder name instead'
}
if ($Scenario -in 'all', 'zip') {
    $thin = Pick 'thin.exe'
    $tempDir = Fresh 'Temp'; $zipDir = Join-Path $tempDir 'Temp1_witness-coop-v0.1.zip'; New-Item -ItemType Directory $zipDir | Out-Null
    Copy-Item $thin (Join-Path $zipDir 'Witness-Coop-Setup.exe')
    $r = Run-Setup (Join-Path $zipDir 'Witness-Coop-Setup.exe') (Join-Path $root 'lad-none') '' @{ TEMP = $tempDir; TMP = $tempDir }
    Check ($r.code -ne 0 -and $r.message -match 'inside a zip' -and $r.message -match 'single file Witness-Coop-Setup\.exe' -and $r.message -match 'Extract All') "run from Explorer's zip temp folder: the specific message (exit $($r.code))"
    $plain = Fresh 'plain'; Copy-Item $thin (Join-Path $plain 'Witness-Coop-Setup.exe')
    $r = Run-Setup (Join-Path $plain 'Witness-Coop-Setup.exe') (Join-Path $root 'lad-none') '' @{ TEMP = $tempDir; TMP = $tempDir }
    Check ($r.code -ne 0 -and $r.message -match 'cannot find its files' -and $r.message -match 'releases') "alone in an ordinary folder: says the files are missing and where the one-file setup is (exit $($r.code))"
    Check ($r.message -notmatch 'inside a zip') 'an ordinary folder is not blamed on a zip'
    [IO.File]::WriteAllText((Join-Path $plain 'installer-gui.ps1'), "[IO.File]::WriteAllText(`$env:WITNESS_SETUP_MARKER, `$PSScriptRoot)")
    $marker = Join-Path $root 'thin-marker.txt'; if (Test-Path $marker) { Remove-Item $marker }
    $r = Run-Setup (Join-Path $plain 'Witness-Coop-Setup.exe') (Join-Path $root 'lad-none') '' @{ WITNESS_SETUP_MARKER = $marker }
    Check ($r.code -eq 0 -and (Test-Path $marker)) 'unpacked zip: the thin Setup runs the script beside it'
}
Write-Output "PASS: $script:checks setup-exe checks ($Scenario)"
