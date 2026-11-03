# Installer checks on disposable fixtures only. The source game folder is read (its exe and its original Ansel DLL), never changed.
#   powershell -ExecutionPolicy Bypass -File tools\test_installer.ps1 -GameDir <a copy of the supported build> -PackageDir <unpacked release> [-TestRoot <scratch on NTFS>]
param(
    [Parameter(Mandatory=$true)][string]$GameDir,
    [Parameter(Mandatory=$true)][string]$PackageDir,
    [string]$TestRoot,
    [switch]$OnlyRefusals,   # only the refusal checks (qa_installer.sh runs them against a mutant package that must fail)
    [switch]$OnlyMigration   # only the old-layout migration and chain checks (the same, for the migration mutants)
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
$proxyHash = Get-CoopHash (Join-Path $PackageDir 'dsound.dll')
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
    (Get-CoopHash (Join-Path $dir 'dsound.dll')) -eq $proxyHash -and
    (Get-CoopHash (Join-Path $dir 'AnselSDK64.dll')) -eq $script:WitnessAnselSha -and
    -not (Test-Path (Join-Path $dir 'AnselSDK64_real.dll')) -and -not (Test-Path (Join-Path $dir 'AnselSDK64.dll.coop-receipt.json')) -and
    (Get-CoopHash (Join-Path $dir 'witness_launcher.exe')) -eq (Get-CoopHash (Join-Path $PackageDir 'witness_launcher.exe')) -and
    (Get-CoopHash (Join-Path $dir 'witness_models\adventurer.glb')) -eq (Get-CoopHash (Join-Path $PackageDir 'witness_models\adventurer.glb')) -and
    (Test-Path (Join-Path $dir 'witness_models\adventurer.glb.txt'))
}

# Another build of the game, a truncated exe and other stores' copies are refused before anything is written, in plain words.
function Test-Refusals {
    $other = Fixture 'other-build'
    $bytes = [IO.File]::ReadAllBytes((Join-Path $other 'witness64_d3d11.exe')); $bytes[$bytes.Length - 1] = $bytes[$bytes.Length - 1] -bxor 1
    [IO.File]::WriteAllBytes((Join-Path $other 'witness64_d3d11.exe'), $bytes)
    $status = Status $other
    Check ($status.build -eq 'conflict' -and $status.actions.blocked) 'another build is reported and blocks setup'
    Check ($status.report.sha -and $status.report.store -eq 'unknown') 'the status says what was found'
    $text = (Install $other @() 3 | Out-String)
    Check (-not (Test-Path (Join-Path $other 'dsound.dll')) -and (Get-CoopHash (Join-Path $other 'AnselSDK64.dll')) -eq $script:WitnessAnselSha -and -not (Test-Path (Join-Path $other 'witness_launcher.exe'))) 'another build: nothing changed'
    Check ($text -match 'Found:' -and $text -match 'SHA-256 starting [0-9a-f]{12}' -and $text -match 'Needed: Steam build 11370412' -and $text -match 'What to do:' -and $text -match 'faq-not-the-supported-build') 'another build: the refusal says what was found, what is needed, what to do, and links the FAQ'
    $truncated = Fixture 'truncated'
    $head = New-Object byte[] 4096; $fs = [IO.File]::OpenRead($sourceExe); [void]$fs.Read($head, 0, 4096); $fs.Dispose()
    [IO.File]::WriteAllBytes((Join-Path $truncated 'witness64_d3d11.exe'), $head)
    $text = (Install $truncated @() 3 | Out-String)
    Check ($text -match 'only 4096 bytes' -and $text -match 'Verify integrity' -and -not (Test-Path (Join-Path $truncated 'dsound.dll'))) 'a truncated exe is refused as damaged, with the Steam advice'
    # a Steam library layout: the build id comes from the store's manifest
    $steamRoot = Join-Path $root 'steam-lib'; $steamGame = Join-Path $steamRoot 'steamapps\common\The Witness'
    New-Item -ItemType Directory -Force $steamGame | Out-Null
    Copy-Item (Join-Path $other 'witness64_d3d11.exe') $steamGame; Copy-Item $sourceAnsel (Join-Path $steamGame 'AnselSDK64.dll')
    Set-Content (Join-Path $steamRoot 'steamapps\appmanifest_210970.acf') '"AppState" { "appid" "210970" "buildid" "99999999" }' -Encoding ASCII
    $text = (Install $steamGame @() 3 | Out-String)
    Check ($text -match 'Steam copy \(Steam build 99999999\)' -and $text -match 'Properties > Betas > choose None') 'a Steam copy: its build id and the Betas > None advice'
    $gog = Fixture 'gog-copy'; [IO.File]::WriteAllBytes((Join-Path $gog 'witness64_d3d11.exe'), $bytes); Set-Content (Join-Path $gog 'goggame-1234.info') '{}' -Encoding ASCII
    $text = (Install $gog @() 3 | Out-String)
    Check ($text -match 'GOG copy' -and $text -match 'not supported yet' -and -not (Test-Path (Join-Path $gog 'dsound.dll'))) 'a GOG copy: named, and told it is not supported yet'
    $status = Status $gog
    Check ($status.actions.blocked -and $status.build -eq 'conflict') 'a GOG copy blocks setup'
}
if ($OnlyRefusals) { Test-Refusals; Write-Output "PASS: $script:checks refusal checks"; return }

# ---- the old layout (v0.1.0-alpha: the proxy in AnselSDK64.dll, the game's own renamed AnselSDK64_real.dll) and other mods' dsound.dll
$oldProxy = [Text.Encoding]::ASCII.GetBytes("MZ fixture: the co-op proxy of v0.1.0-alpha (real Ansel DLL %s)")
function Write-Receipt($target, $original, $installed) {
    [IO.File]::WriteAllText("$target.coop-receipt.json", (@{ schema=1; original=$original; installed=$installed; pending=$null } | ConvertTo-Json -Compress))
}
function Legacy-Fixture($name, [switch]$BySetup) {
    $dir = Fixture $name $false
    $a = Join-Path $dir 'AnselSDK64.dll'; $r = Join-Path $dir 'AnselSDK64_real.dll'
    [IO.File]::WriteAllBytes($a, $oldProxy); Copy-Item $sourceAnsel $r
    if ($BySetup) {      # what the alpha's Setup left: receipts for both, the original kept beside the proxy
        Copy-Item $sourceAnsel "$a.coop-original"
        Write-Receipt $a $script:WitnessAnselSha (Get-CoopHash $a)
        Write-Receipt $r $null $script:WitnessAnselSha
    }
    return $dir
}
function Snapshot($dir) { (Get-ChildItem -Recurse -File $dir | Sort-Object FullName | ForEach-Object { "$($_.FullName.Substring($dir.Length))=$(Get-CoopHash $_.FullName)" }) -join ';' }
function Test-Migration {
    $setupMade = Legacy-Fixture 'legacy-setup' -BySetup
    $status = Status $setupMade
    Check ($status.legacy -and $status.actions.install -eq 'Update' -and -not $status.actions.blocked) 'an alpha install made by Setup is seen as the old layout and offers Update'
    $text = (Install $setupMade @() | Out-String)
    Check (Installed $setupMade) 'alpha (Setup) -> new layout: the game''s own AnselSDK64.dll back, dsound.dll, no AnselSDK64_real.dll or old receipts'
    Check (-not (Test-Path (Join-Path $setupMade 'AnselSDK64.dll.coop-original')) -and $text -match 'the game''s own file is back') 'the old backup is retired and the migration is reported'
    $status = Status $setupMade
    Check (-not $status.legacy -and -not $status.actions.install -and $status.actions.uninstall) 'after the migration: up to date'
    $byHand = Legacy-Fixture 'legacy-hand'
    Install $byHand @() | Out-Null
    Check (Installed $byHand) 'alpha (by hand, no receipts) -> new layout'
    Install $byHand @('-Uninstall') | Out-Null
    Check ((Get-CoopHash (Join-Path $byHand 'AnselSDK64.dll')) -eq $script:WitnessAnselSha -and -not (Test-Path (Join-Path $byHand 'dsound.dll'))) 'uninstall after a migration: the game''s own files only'
    # interrupted: (a) the game's file already back, the old pieces still there; (b) the staged copy written, the old proxy not yet replaced
    $cutA = Legacy-Fixture 'legacy-cut-a' -BySetup
    Copy-Item $sourceAnsel (Join-Path $cutA 'AnselSDK64.dll') -Force
    Install $cutA @() | Out-Null
    Check (Installed $cutA -and -not (Test-Path (Join-Path $cutA 'AnselSDK64.dll.coop-original'))) 'a migration interrupted after the restore finishes on the next run'
    $cutB = Legacy-Fixture 'legacy-cut-b' -BySetup
    Copy-Item $sourceAnsel (Join-Path $cutB 'AnselSDK64.dll.coop-stage')
    Install $cutB @() | Out-Null
    Check (Installed $cutB -and -not (Test-Path (Join-Path $cutB 'AnselSDK64.dll.coop-stage'))) 'a migration interrupted with the staged copy written finishes on the next run'
    $uninstallOnly = Legacy-Fixture 'legacy-uninstall' -BySetup
    Install $uninstallOnly @('-Uninstall') | Out-Null
    Check ((Get-CoopHash (Join-Path $uninstallOnly 'AnselSDK64.dll')) -eq $script:WitnessAnselSha -and -not (Test-Path (Join-Path $uninstallOnly 'AnselSDK64_real.dll'))) 'uninstall of an alpha install puts the game''s own AnselSDK64.dll back'
    # unknown hashes block, in words, and nothing is touched
    $tampered = Legacy-Fixture 'legacy-tampered' -BySetup
    $bytes = [IO.File]::ReadAllBytes((Join-Path $tampered 'AnselSDK64_real.dll')); $bytes[100] = $bytes[100] -bxor 1
    [IO.File]::WriteAllBytes((Join-Path $tampered 'AnselSDK64_real.dll'), $bytes); Remove-Item (Join-Path $tampered 'AnselSDK64.dll.coop-original')
    $before = Snapshot $tampered
    Check ((Status $tampered).actions.blocked) 'an AnselSDK64_real.dll that is not the game''s own blocks setup'
    $text = (Install $tampered @() 1 | Out-String)
    Check ((Snapshot $tampered) -eq $before -and $text -match 'is not the game''s own Ansel DLL' -and $text -match 'Verify integrity') 'tampered original: refused in words, nothing changed (no unverified file put back as AnselSDK64.dll)'
    $foreign = Legacy-Fixture 'legacy-foreign'
    [IO.File]::WriteAllText((Join-Path $foreign 'AnselSDK64.dll'), 'another program''s AnselSDK64.dll')
    $before = Snapshot $foreign
    $text = (Install $foreign @() 1 | Out-String)
    Check ((Snapshot $foreign) -eq $before -and $text -match 'neither the game''s own file nor this mod''s old proxy') 'an AnselSDK64.dll that is not ours: refused, nothing changed'
    # another mod's dsound.dll: kept as dsound.chain.dll, put back on uninstall; an unowned dsound.dll of this mod is not chained
    $other = Fixture 'chain'
    [IO.File]::WriteAllText((Join-Path $other 'dsound.dll'), 'MZ another mod''s dsound.dll')
    $text = (Install $other @() | Out-String)
    Check ((Installed $other) -and [IO.File]::ReadAllText((Join-Path $other 'dsound.chain.dll')) -eq 'MZ another mod''s dsound.dll' -and $text -match 'dsound.chain.dll') 'another mod''s dsound.dll is kept as dsound.chain.dll'
    $status = Status $other
    Check ($status.chain -and -not $status.actions.install -and -not $status.actions.blocked) 'with a chain file: up to date'
    Install $other @() | Out-Null
    Check ([IO.File]::ReadAllText((Join-Path $other 'dsound.chain.dll')) -eq 'MZ another mod''s dsound.dll') 'a second install keeps the chain file as it is'
    Install $other @('-Uninstall') | Out-Null
    Check ([IO.File]::ReadAllText((Join-Path $other 'dsound.dll')) -eq 'MZ another mod''s dsound.dll' -and -not (Test-Path (Join-Path $other 'dsound.chain.dll')) -and -not (Test-Path (Join-Path $other 'dsound.dll.coop-original'))) 'uninstall puts the other mod''s dsound.dll back and removes the chain copy'
    $ours = Fixture 'own-unowned'
    Copy-Item (Join-Path $PackageDir 'dsound.dll') (Join-Path $ours 'dsound.dll')
    Install $ours @() | Out-Null
    Check ((Installed $ours) -and -not (Test-Path (Join-Path $ours 'dsound.chain.dll'))) 'this mod''s own dsound.dll (copied in by hand) is not chained'
    $clash = Fixture 'chain-clash'
    [IO.File]::WriteAllText((Join-Path $clash 'dsound.dll'), 'MZ mod one'); [IO.File]::WriteAllText((Join-Path $clash 'dsound.chain.dll'), 'MZ mod two')
    $before = Snapshot $clash
    $text = (Install $clash @() 1 | Out-String)
    Check ((Snapshot $clash) -eq $before -and $text -match 'dsound.chain.dll is already in the game folder') 'a different dsound.chain.dll already there: refused, nothing changed'
}
if ($OnlyMigration) { Test-Migration; Write-Output "PASS: $script:checks migration checks"; return }

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
Check (-not (Test-Path (Join-Path $fresh 'dsound.dll'))) 'FindOnly changes nothing'
Install $fresh @()
Check (Installed $fresh) 'proxy (dsound.dll), launcher and model installed'
Check ((Get-CoopHash (Join-Path $fresh 'witness64_d3d11.exe')) -eq $script:WitnessExeSha) 'the game executable is untouched'
Check (-not (Test-Path (Join-Path $fresh 'AnselSDK64.dll.coop-original')) -and -not (Test-Path (Join-Path $fresh 'dsound.dll.coop-original')) -and -not (Test-Path (Join-Path $fresh 'dsound.chain.dll'))) 'the game''s own AnselSDK64.dll is not touched, nothing is backed up or chained'
Check (-not (Test-Path (Join-Path $fresh 'witness_coop.cfg'))) 'no cfg is written: the launcher owns it'
foreach ($relative in $script:WitnessManaged) { Check (Test-Path (Join-Path $fresh "$relative.coop-receipt.json")) "receipt for $relative" }
$status = Status $fresh
Check (-not $status.actions.install -and $status.actions.uninstall -and -not $status.actions.blocked) 'after install: up to date, uninstall offered'
Install $fresh @()
Check (Installed $fresh) 'a second install is harmless'
# Update: an older owned proxy is replaced; Repair: a missing managed file is restored.
Install-CoopFile (Join-Path $fresh 'dsound.dll') ([Text.Encoding]::ASCII.GetBytes('previous release fixture'))
Check ((Status $fresh).actions.install -eq 'Update') 'an older owned proxy offers Update'
Install $fresh @()
Check (Installed $fresh) 'update deploys the current proxy'
Remove-Item (Join-Path $fresh 'witness_launcher.exe')
Check ((Status $fresh).actions.install -eq 'Repair') 'a missing managed file offers Repair'
Install $fresh @()
Check (Installed $fresh) 'repair restores the launcher'
# A file changed by hand is never replaced.
[IO.File]::WriteAllText((Join-Path $fresh 'dsound.dll'), 'user modification')
Check ((Status $fresh).actions.blocked) 'a modified proxy blocks setup'
Install $fresh @() 1
Check ([IO.File]::ReadAllText((Join-Path $fresh 'dsound.dll')) -eq 'user modification') 'the modified file is kept'
[IO.File]::WriteAllBytes((Join-Path $fresh 'dsound.dll'), [IO.File]::ReadAllBytes((Join-Path $PackageDir 'dsound.dll')))
# Uninstall puts the game's own DLL back and keeps the player's settings and log.
Set-Content (Join-Path $fresh 'witness_coop.cfg') "name=Tester`r`n" -Encoding ASCII
Set-Content (Join-Path $fresh 'witness_proxy.log') 'log line' -Encoding ASCII
Install $fresh @('-Uninstall')
Check ((Get-CoopHash (Join-Path $fresh 'AnselSDK64.dll')) -eq $script:WitnessAnselSha) 'after uninstall the game''s own Ansel DLL is as it was'
Check (-not (Test-Path (Join-Path $fresh 'dsound.dll'))) 'uninstall removes the proxy'
Check (-not (Test-Path (Join-Path $fresh 'witness_launcher.exe'))) 'uninstall removes the launcher'
Check (-not (Test-Path (Join-Path $fresh 'witness_models'))) 'uninstall removes the model folder'
Check (-not (Test-Path (Join-Path $fresh 'dsound.dll.coop-receipt.json')) -and -not (Test-Path (Join-Path $fresh 'dsound.dll.coop-original'))) 'uninstall retires receipts and backups'
Check ((Get-Content (Join-Path $fresh 'witness_coop.cfg') -Raw) -match 'name=Tester') 'uninstall keeps witness_coop.cfg'
Check (Test-Path (Join-Path $fresh 'witness_proxy.log')) 'uninstall keeps the log'
Check ((Status $fresh).actions.install -eq 'Install') 'after uninstall the game offers Install again'

Test-Refusals
Test-Migration

# A game whose AnselSDK64.dll is something else (not the game's, not the mod's old proxy) is refused: Setup cannot tell what it is.
$noAnsel = Fixture 'no-original' $false
[IO.File]::WriteAllText((Join-Path $noAnsel 'AnselSDK64.dll'), 'something else')
Install $noAnsel @() 1
Check ([IO.File]::ReadAllText((Join-Path $noAnsel 'AnselSDK64.dll')) -eq 'something else' -and -not (Test-Path (Join-Path $noAnsel 'dsound.dll'))) 'unknown AnselSDK64.dll: nothing changed'

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
