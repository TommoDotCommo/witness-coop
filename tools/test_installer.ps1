# Installer checks on disposable fixtures only. The source game folder is read (its exe and its original Ansel DLL), never changed.
#   powershell -ExecutionPolicy Bypass -File tools\test_installer.ps1 -GameDir <a copy of the supported build> -PackageDir <unpacked release> [-TestRoot <scratch on NTFS>]
param(
    [Parameter(Mandatory=$true)][string]$GameDir,
    [Parameter(Mandatory=$true)][string]$PackageDir,
    [string]$TestRoot
)
$ErrorActionPreference = 'Stop'
$base = if ($TestRoot) { $TestRoot } else { Join-Path (Split-Path -Parent $PSScriptRoot) 'local\tests' }
$root = Join-Path $base "witness-coop-installer-$PID"
New-Item -ItemType Directory -Force $root | Out-Null
. (Join-Path $PackageDir 'install-state.ps1')
$script:checks = 0
function Check($condition, $description) {
    if (-not $condition) { throw "FAIL: $description" }
    $script:checks++
}
$sourceExe = Join-Path $GameDir 'witness64_d3d11.exe'
Check ((Get-CoopHash $sourceExe) -eq $script:WitnessExeSha) 'the source game is the supported build'
$sourceAnsel = $null
foreach ($name in 'AnselSDK64.dll', 'AnselSDK64_real.dll', 'AnselSDK64.dll.coop-original') {
    $p = Join-Path $GameDir $name
    if ((Test-Path $p) -and (Get-CoopHash $p) -eq $script:WitnessAnselSha) { $sourceAnsel = $p; break }
}
Check ($sourceAnsel -ne $null) 'the source game has its original Ansel DLL'
$proxyHash = Get-CoopHash (Join-Path $PackageDir 'AnselSDK64.dll')
Check ($proxyHash -ne $script:WitnessAnselSha) 'the package ships the proxy, not the original'
function Fixture($name, $withAnsel = $true) {
    $dir = Join-Path $root $name
    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
    New-Item -ItemType Directory $dir | Out-Null
    Copy-Item $sourceExe (Join-Path $dir 'witness64_d3d11.exe')
    if ($withAnsel) { Copy-Item $sourceAnsel (Join-Path $dir 'AnselSDK64.dll') }
    return $dir
}
function Install($dir, [string[]]$extra, [int]$expected=0) {
    $output = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PackageDir 'witness-coop.ps1') -GameDir $dir -Yes @extra 2>&1
    if ($LASTEXITCODE -ne $expected) { throw "Installer exit $LASTEXITCODE, expected ${expected}: $output" }
    return $output
}
function Status($dir) {
    $json = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PackageDir 'witness-coop.ps1') -GameDir $dir -StatusOnly -Yes
    if ($LASTEXITCODE -ne 0) { throw 'Status command failed' }
    return ($json | ConvertFrom-Json)
}
function Installed($dir) {
    (Get-CoopHash (Join-Path $dir 'AnselSDK64.dll')) -eq $proxyHash -and
    (Get-CoopHash (Join-Path $dir 'AnselSDK64_real.dll')) -eq $script:WitnessAnselSha -and
    (Get-CoopHash (Join-Path $dir 'witness_launcher.exe')) -eq (Get-CoopHash (Join-Path $PackageDir 'witness_launcher.exe')) -and
    (Get-CoopHash (Join-Path $dir 'witness_models\adventurer.glb')) -eq (Get-CoopHash (Join-Path $PackageDir 'witness_models\adventurer.glb')) -and
    (Test-Path (Join-Path $dir 'witness_models\adventurer.glb.txt'))
}

# The shipped native bootstrap: starts PowerShell without a console window (a headless script stands in for the GUI).
if (Test-Path (Join-Path $PackageDir 'Witness-Coop-Setup.exe')) {
    $startupDir = Join-Path $root 'setup startup'
    New-Item -ItemType Directory $startupDir | Out-Null
    $setup = Join-Path $startupDir 'Witness-Coop-Setup.exe'
    Copy-Item (Join-Path $PackageDir 'Witness-Coop-Setup.exe') $setup
    [IO.File]::WriteAllText((Join-Path $startupDir 'installer-gui.ps1'), @'
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class StartupProbe {
    [DllImport("kernel32.dll")] public static extern IntPtr GetConsoleWindow();
}
"@
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'console.txt'), [StartupProbe]::GetConsoleWindow().ToInt64().ToString())
'@)
    $process = Start-Process -FilePath $setup -PassThru -Wait
    Check ($process.ExitCode -eq 0) 'native setup starts successfully'
    $resultFile = Join-Path $startupDir 'console.txt'
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    while (-not (Test-Path $resultFile) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 50 }
    Check ((Test-Path $resultFile) -and [IO.File]::ReadAllText($resultFile) -eq '0') 'native setup starts PowerShell without a console window'
}
foreach ($mode in 'install', 'uninstall') {
    $completion = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PackageDir 'installer-gui.ps1') -TestCompletion $mode
    Check ($LASTEXITCODE -eq 0 -and $completion -eq "completion $mode OK") "GUI $mode completion choices"
}

# A plain install into a fresh copy of the game.
$fresh = Fixture 'fresh'
Check ((Status $fresh).actions.install -eq 'Install') 'a fresh game offers Install'
$found = Install $fresh @('-FindOnly')
Check (($found | Out-String) -match 'Game folder: ') 'FindOnly reports the folder'
Check (-not (Test-Path (Join-Path $fresh 'AnselSDK64_real.dll'))) 'FindOnly changes nothing'
Install $fresh @()
Check (Installed $fresh) 'proxy, original under the proxy''s name, launcher and model installed'
Check ((Get-CoopHash (Join-Path $fresh 'witness64_d3d11.exe')) -eq $script:WitnessExeSha) 'the game executable is untouched'
Check ((Get-CoopHash (Join-Path $fresh 'AnselSDK64.dll.coop-original')) -eq $script:WitnessAnselSha) 'the original Ansel DLL is kept for uninstall'
Check (-not (Test-Path (Join-Path $fresh 'witness_coop.cfg'))) 'no cfg is written: the launcher owns it'
foreach ($relative in $script:WitnessManaged) { Check (Test-Path (Join-Path $fresh "$relative.coop-receipt.json")) "receipt for $relative" }
$status = Status $fresh
Check (-not $status.actions.install -and $status.actions.uninstall -and -not $status.actions.blocked) 'after install: up to date, uninstall offered'
Install $fresh @()
Check (Installed $fresh) 'a second install is harmless'
# Update: an older owned proxy is replaced; Repair: a missing managed file is restored.
Install-CoopFile (Join-Path $fresh 'AnselSDK64.dll') ([Text.Encoding]::ASCII.GetBytes('previous release fixture'))
Check ((Status $fresh).actions.install -eq 'Update') 'an older owned proxy offers Update'
Install $fresh @()
Check (Installed $fresh) 'update deploys the current proxy'
Remove-Item (Join-Path $fresh 'witness_launcher.exe')
Check ((Status $fresh).actions.install -eq 'Repair') 'a missing managed file offers Repair'
Install $fresh @()
Check (Installed $fresh) 'repair restores the launcher'
# A file changed by hand is never replaced.
[IO.File]::WriteAllText((Join-Path $fresh 'AnselSDK64.dll'), 'user modification')
Check ((Status $fresh).actions.blocked) 'a modified proxy blocks setup'
Install $fresh @() 1
Check ([IO.File]::ReadAllText((Join-Path $fresh 'AnselSDK64.dll')) -eq 'user modification') 'the modified file is kept'
[IO.File]::WriteAllBytes((Join-Path $fresh 'AnselSDK64.dll'), [IO.File]::ReadAllBytes((Join-Path $PackageDir 'AnselSDK64.dll')))
# Uninstall puts the game's own DLL back and keeps the player's settings and log.
Set-Content (Join-Path $fresh 'witness_coop.cfg') "name=Tester`r`n" -Encoding ASCII
Set-Content (Join-Path $fresh 'witness_proxy.log') 'log line' -Encoding ASCII
Install $fresh @('-Uninstall')
Check ((Get-CoopHash (Join-Path $fresh 'AnselSDK64.dll')) -eq $script:WitnessAnselSha) 'uninstall restores the original Ansel DLL'
Check (-not (Test-Path (Join-Path $fresh 'AnselSDK64_real.dll'))) 'uninstall removes the renamed copy it made'
Check (-not (Test-Path (Join-Path $fresh 'witness_launcher.exe'))) 'uninstall removes the launcher'
Check (-not (Test-Path (Join-Path $fresh 'witness_models'))) 'uninstall removes the model folder'
Check (-not (Test-Path (Join-Path $fresh 'AnselSDK64.dll.coop-receipt.json')) -and -not (Test-Path (Join-Path $fresh 'AnselSDK64.dll.coop-original'))) 'uninstall retires receipts and backups'
Check ((Get-Content (Join-Path $fresh 'witness_coop.cfg') -Raw) -match 'name=Tester') 'uninstall keeps witness_coop.cfg'
Check (Test-Path (Join-Path $fresh 'witness_proxy.log')) 'uninstall keeps the log'
Check ((Status $fresh).actions.install -eq 'Install') 'after uninstall the game offers Install again'

# Another build of the game is refused before anything is written.
$other = Fixture 'other-build'
$bytes = [IO.File]::ReadAllBytes((Join-Path $other 'witness64_d3d11.exe')); $bytes[$bytes.Length - 1] = $bytes[$bytes.Length - 1] -bxor 1
[IO.File]::WriteAllBytes((Join-Path $other 'witness64_d3d11.exe'), $bytes)
$status = Status $other
Check ($status.build -eq 'conflict' -and $status.actions.blocked) 'another build is reported and blocks setup'
Install $other @() 1
Check (-not (Test-Path (Join-Path $other 'AnselSDK64_real.dll')) -and (Get-CoopHash (Join-Path $other 'AnselSDK64.dll')) -eq $script:WitnessAnselSha -and -not (Test-Path (Join-Path $other 'witness_launcher.exe'))) 'another build: nothing changed'

# A game whose Ansel DLL is not the original (and no renamed original anywhere) is refused.
$noAnsel = Fixture 'no-original' $false
[IO.File]::WriteAllText((Join-Path $noAnsel 'AnselSDK64.dll'), 'something else')
Install $noAnsel @() 1
Check ([IO.File]::ReadAllText((Join-Path $noAnsel 'AnselSDK64.dll')) -eq 'something else' -and -not (Test-Path (Join-Path $noAnsel 'AnselSDK64_real.dll'))) 'missing original: nothing changed'

# A copy prepared by hand (original already renamed, an older proxy in place) installs, and uninstall puts back what was there.
$manual = Fixture 'manual' $false
Copy-Item $sourceAnsel (Join-Path $manual 'AnselSDK64_real.dll')
[IO.File]::WriteAllText((Join-Path $manual 'AnselSDK64.dll'), 'old proxy by hand')
Install $manual @()
Check (Installed $manual) 'a hand-prepared copy installs'
Install $manual @('-Uninstall')
Check ([IO.File]::ReadAllText((Join-Path $manual 'AnselSDK64.dll')) -eq 'old proxy by hand') 'uninstall restores exactly what was there'
Check ((Get-CoopHash (Join-Path $manual 'AnselSDK64_real.dll')) -eq $script:WitnessAnselSha) 'a renamed original that was already there stays'

# Uninstall never removes files it did not install.
$unowned = Fixture 'unowned'
[IO.File]::WriteAllText((Join-Path $unowned 'witness_launcher.exe'), 'unowned launcher')
Install $unowned @('-Uninstall')
Check ([IO.File]::ReadAllText((Join-Path $unowned 'witness_launcher.exe')) -eq 'unowned launcher' -and (Get-CoopHash (Join-Path $unowned 'AnselSDK64.dll')) -eq $script:WitnessAnselSha) 'unowned files are kept'

# The setup window: writable probe, layout, play and shortcut actions.
$probe = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PackageDir 'installer-gui.ps1') -ProbePath $unowned
Check ($LASTEXITCODE -eq 0 -and $probe -eq 'writable') 'GUI detects a writable game folder'
Check (@(Get-ChildItem -Force $unowned).Count -eq 3) 'GUI permission probe cleans up its temporary file'
$probe = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PackageDir 'installer-gui.ps1') -ProbePath (Join-Path $root 'missing')
Check ($LASTEXITCODE -ne 0 -and $probe -eq 'not writable') 'GUI rejects a missing folder'
$protected = Fixture 'protected'
$originalAcl = Get-Acl -LiteralPath $protected
try {
    $restrictedAcl = Get-Acl -LiteralPath $protected
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent().User
    $deny = New-Object Security.AccessControl.FileSystemAccessRule($identity, 'CreateFiles', 'None', 'None', 'Deny')
    $restrictedAcl.AddAccessRule($deny)
    Set-Acl -LiteralPath $protected -AclObject $restrictedAcl
    $probe = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PackageDir 'installer-gui.ps1') -ProbePath $protected
    Check ($LASTEXITCODE -ne 0 -and $probe -eq 'not writable') 'GUI detects a protected game folder'
} finally { Set-Acl -LiteralPath $protected -AclObject $originalAcl }
$layout = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PackageDir 'installer-gui.ps1') -TestLayout
Check ($LASTEXITCODE -eq 0) 'setup layout geometry'
$installed = Fixture 'installed'
Install $installed @()
$actions = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PackageDir 'installer-gui.ps1') -TestActionsPath $installed
Check ($LASTEXITCODE -eq 0 -and $actions -match 'PASS: existing game actions') 'play (launcher) and desktop shortcut actions'
Write-Output "PASS: $script:checks installer checks (disposable fixtures only)"
