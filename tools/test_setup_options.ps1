# Disposable option integrations and discovery fixtures (co-op standard G1-G4). The firewall uses a fake netsh, never UAC or real firewall rules; the
# registry handler lives under a throw-away HKCU key; the desktop is a scratch folder.
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\test_setup_options.ps1 -TestRoot <scratch> [-Mutants]
# Checks: the three options (desktop shortcut to the launcher, one firewall rule per program - the game and the launcher - on Private AND Public,
# the witness-coop:// handler that starts the launcher with the link), their exact removal, every install found (Steam libraries with
# appmanifest_210970.acf, GOG registry and known paths, Epic manifests, the uninstall list) as cards, and the card preselection.
param([Parameter(Mandatory=$true)][string]$TestRoot, [switch]$Mutants)
$ErrorActionPreference = 'Stop'
$source = [IO.File]::ReadAllText((Join-Path (Split-Path -Parent $PSScriptRoot) 'dist\install-state.ps1'))
$source = $source.Replace("Join-Path `$PSScriptRoot 'install-policy.exe'", "'unused-policy.exe'")
function Require($condition, $message) { if (-not $condition) { throw "ASSERT: $message" } }
function Run-Checks($code) {
    $root = Join-Path $TestRoot ('options-' + [guid]::NewGuid().ToString('N'))
    $registry = 'HKCU:\Software\witness-coop-test-' + [guid]::NewGuid().ToString('N')
    New-Item -ItemType Directory $root | Out-Null
    $code = $code.Replace("'HKCU:\Software\Classes\witness-coop'", "'$registry'").Replace("'HKCU:\Software\Classes\witness-coop\shell\open\command'", "'$registry\shell\open\command'")
    $code = $code.Replace("[Environment]::GetFolderPath('DesktopDirectory')", "'$($root.Replace("'", "''"))'")
    . ([scriptblock]::Create($code))
    try {
        $game = Join-Path $root 'game with spaces'; New-Item -ItemType Directory $game | Out-Null
        [IO.File]::WriteAllText((Join-Path $game 'witness64_d3d11.exe'), 'fixture')
        $programs = @(Get-CoopFirewallPrograms $game)
        Require ($programs.Count -eq 2 -and $programs[0] -eq (Join-Path $game 'witness64_d3d11.exe') -and $programs[1] -eq (Join-Path $game 'witness_launcher.exe')) 'the rule covers the game and the launcher'
        $rule = [pscustomobject]@{ Name='The Witness Co-op (UDP)'; ApplicationName=$programs[0]; Protocol=17; Direction=1 }
        Require (Test-CoopFirewallRule $rule $programs[0]) 'our firewall rule recognised'
        foreach ($property in 'Name', 'ApplicationName', 'Protocol', 'Direction') {
            $originalValue = $rule.$property
            $rule.$property = if ($property -in 'Name', 'ApplicationName') { 'other' } else { 0 }
            Require (-not (Test-CoopFirewallRule $rule $programs[0])) "other firewall $property excluded"
            $rule.$property = $originalValue
        }
        # Fake external firewall command: checks the arguments sent to netsh and models the rules, one per program.
        $script:rules = @{}; $script:netshCalls = @()
        function Get-CoopFirewallRules($program) { if ($script:rules.ContainsKey($program)) { return @($script:rules[$program]) } else { return @() } }
        function Invoke-CoopHiddenProcess($file, $arguments) {
            Require ($file -eq 'netsh.exe') 'only netsh for firewall changes'
            $script:netshCalls += $arguments
            $program = [regex]::Match($arguments, 'program="([^"]+)"').Groups[1].Value
            if ($arguments -match ' add rule ') {
                Require ($arguments -match 'name="The Witness Co-op \(UDP\)"') 'stable firewall name'
                Require ($programs -contains $program) 'firewall exact executable (the game or the launcher)'
                Require ($arguments -match 'dir=in action=allow' -and $arguments -match 'protocol=UDP' -and $arguments -match 'profile=private,public' -and $arguments -match 'enable=yes') 'inbound UDP, both profiles, enabled'
                $script:rules[$program] = [pscustomobject]@{ Enabled=$true; Action=1; Profiles=6 }
            } elseif ($arguments -match ' delete rule ') {
                Require ($arguments -match 'program="' -and $arguments -match 'protocol=UDP dir=in') 'scoped firewall removal'
                $script:rules.Remove($program)
            } else { throw 'Unexpected firewall operation' }
            return [pscustomobject]@{ ExitCode=0 }
        }
        $before = Get-CoopOptions $game
        Require (-not $before.desktop_shortcut -and -not $before.invite_links -and -not $before.allow_hosting) 'options initially absent'
        Set-CoopUserOptions $game $true $true
        Set-CoopFirewall $game $false
        Set-CoopFirewall $game $false
        $after = Get-CoopOptions $game
        Require ($after.desktop_shortcut -and $after.invite_links -and $after.allow_hosting) 'options reported installed'
        Require ($script:rules.Count -eq 2 -and $script:rules.ContainsKey($programs[0]) -and $script:rules.ContainsKey($programs[1])) 'a rule for the game and one for the launcher'
        Require ((Get-CoopShortcutTarget) -eq (Join-Path $game 'witness_launcher.exe')) 'the shortcut starts the launcher'
        foreach ($profiles in 2, 4) {
            $script:rules[$programs[1]].Profiles = $profiles
            Require (-not (Get-CoopOptions $game).allow_hosting) 'both firewall profiles required'
        }
        $script:rules[$programs[1]].Profiles = 6
        $script:rules[$programs[1]].Enabled = $false
        Require (-not (Get-CoopOptions $game).allow_hosting) 'a disabled firewall rule is not hosting allowed'
        $script:rules[$programs[1]].Enabled = $true
        $script:rules[$programs[1]].Action = 0
        Require (-not (Get-CoopOptions $game).allow_hosting) 'a blocking firewall rule is not hosting allowed'
        $script:rules[$programs[1]].Action = 1
        $saved = $script:rules[$programs[0]]; $script:rules.Remove($programs[0])
        Require (-not (Get-CoopOptions $game).allow_hosting) 'both programs need a rule'
        $script:rules[$programs[0]] = $saved
        Require ((Get-ItemProperty $registry).'URL Protocol' -eq '') 'URL Protocol marker'
        Require ((Get-Item "$registry\shell\open\command").GetValue('') -eq ('"{0}" --coop-invite "%1"' -f (Join-Path $game 'witness_launcher.exe'))) 'URL command quotes path and invite and starts the launcher'
        $other = Join-Path $root 'other'; New-Item -ItemType Directory $other | Out-Null
        Remove-CoopUserOptions $other
        Require ((Get-CoopOptions $game).invite_links -and (Get-CoopOptions $game).desktop_shortcut) 'another install cannot remove the selected integrations'
        Remove-CoopUserOptions $game
        Set-CoopFirewall $game $true
        $after = Get-CoopOptions $game
        Require (-not $after.desktop_shortcut -and -not $after.invite_links -and -not $after.allow_hosting -and $script:rules.Count -eq 0) 'all three integrations removed'
        # Read-only discovery: two Steam libraries (modern VDF), GOG by registry and by folder, Epic, the uninstall list; other games' folders are ignored.
        $steam = Join-Path $root 'Steam'; $library = Join-Path $root 'Library'
        $programRoot = Join-Path $root 'Programs'; $gogReg = Join-Path $root 'GOG registry'; $other2 = Join-Path $root 'Other game'
        $custom = Join-Path $root 'Custom Witness'; $epicGame = Join-Path $root 'Epic Witness'; $programData = Join-Path $root 'ProgramData'
        $dirs = @((Join-Path $steam 'steamapps\common\The Witness'), (Join-Path $library 'steamapps\common\Custom Witness'), (Join-Path $programRoot 'GOG Galaxy\Games\The Witness'), $gogReg, $custom, $epicGame)
        foreach ($dir in $dirs) { New-Item -ItemType Directory -Force $dir | Out-Null; [IO.File]::WriteAllText((Join-Path $dir 'witness64_d3d11.exe'), 'fixture') }
        New-Item -ItemType Directory $other2 | Out-Null; [IO.File]::WriteAllText((Join-Path $other2 'other.exe'), 'another game')
        [IO.File]::WriteAllText((Join-Path $steam 'steamapps\libraryfolders.vdf'), ('"libraryfolders" {{ "0" {{ "path" "{0}" }} "1" {{ "path" "{1}" }} }}' -f $steam.Replace('\','\\'), $library.Replace('\','\\')))
        [IO.File]::WriteAllText((Join-Path $library 'steamapps\appmanifest_210970.acf'), '"AppState" { "installdir" "Custom Witness" }')
        $epicManifests = Join-Path $programData 'Epic\EpicGamesLauncher\Data\Manifests'
        New-Item -ItemType Directory -Force $epicManifests | Out-Null
        $epicManifest = Join-Path $epicManifests 'witness.item'
        [IO.File]::WriteAllText($epicManifest, (@{ InstallLocation=$epicGame } | ConvertTo-Json))
        function Get-ItemProperty($Path, $ErrorAction) {
            if ($Path -like '*Valve\Steam') { return [pscustomobject]@{ SteamPath=$steam } }
            if ($Path -eq 'fixture-gog') { return [pscustomobject]@{ path=$gogReg } }
            if ($Path -eq 'fixture-uninstall') { return [pscustomobject]@{ DisplayName='The Witness'; InstallLocation=$custom } }
            if ($Path -eq 'fixture-other') { return [pscustomobject]@{ path=$other2 } }
        }
        function Get-ChildItem($Path, $LiteralPath, $Filter, $ErrorAction) {
            if ($LiteralPath -eq $epicManifests) { return [pscustomobject]@{ FullName=$epicManifest } }
            if ($Path -eq 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall') { return [pscustomobject]@{ PSPath='fixture-uninstall' } }
            if ($Path -eq 'HKLM:\SOFTWARE\GOG.com\Games') { return @([pscustomobject]@{ PSPath='fixture-gog' }, [pscustomobject]@{ PSPath='fixture-other' }) }
        }
        $oldSystemDrive = $env:SystemDrive; $oldProgram64 = $env:ProgramW6432; $oldData = $env:ProgramData; $oldProgram = $env:ProgramFiles; $oldProgram32 = ${env:ProgramFiles(x86)}
        try {
            $env:SystemDrive = $root; $env:ProgramW6432 = $programRoot; $env:ProgramData = $programData; $env:ProgramFiles = $programRoot; ${env:ProgramFiles(x86)} = $programRoot
            $found = @(Find-AllGames)
            Require ($found.Count -eq 6) "six distinct installs discovered, got $($found.Count)"
            foreach ($dir in $dirs) { Require ($found -contains $dir) "discovered $dir" }
        } finally { $env:SystemDrive = $oldSystemDrive; $env:ProgramW6432 = $oldProgram64; $env:ProgramData = $oldData; $env:ProgramFiles = $oldProgram; ${env:ProgramFiles(x86)} = $oldProgram32 }
        function Get-WitnessInstallStatus($dir, $package) {
            return [pscustomobject]@{ owner=$(if ($dir -eq $script:installedDir) { 'manual' } else { 'none' }); build=$(if ($dir.StartsWith($steam)) { 'current' } else { 'conflict' }) }
        }
        function Get-WitnessBuildReport($dir) { return [pscustomobject]@{ store=$(if ($dir.StartsWith($steam)) { 'Steam' } else { 'unknown' }); buildId=$null } }
        $script:installedDir = ''
        $cards = @(Get-CoopInstallCards '' $dirs)
        Require ($cards.Count -eq 6 -and @($cards | Where-Object selected).Count -eq 1 -and $cards[0].selected) 'all build cards, the supported Steam build preselected'
        Require ($cards[0].supported -and -not $cards[4].supported) 'a card says whether the build is supported'
        [IO.File]::WriteAllText((Join-Path $custom 'dsound.dll'), 'MZ witness-coop:proxy:dsound an old release')
        $cards = @(Get-CoopInstallCards '' $dirs)
        Require ($cards[4].selected -and $cards[4].has_coop) 'an install that holds a co-op proxy is preselected and reported as found'
        Remove-Item (Join-Path $custom 'dsound.dll')
        $script:installedDir = $gogReg
        $cards = @(Get-CoopInstallCards '' $dirs)
        Require ($cards[3].selected -and @($cards | Where-Object selected).Count -eq 1) 'an installed co-op is preselected'
    } finally {
        Remove-Item -LiteralPath $registry -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $root -Recurse -Force
    }
}
Run-Checks $source
Write-Output 'PASS: three integrations (a rule per program on Private and Public, scoped removal, the link command that starts the launcher), all install cards; fake netsh (no elevation)'
if ($Mutants) {
    foreach ($twin in @(
        @('profile=private,public', 'profile=private'),
        @('$rule.Name -eq $script:CoopFirewallName', '$true'),
        @('$rule.ApplicationName -eq $program', '$true'),
        @('$rule.Protocol -eq 17', '$true'),
        @('$rule.Direction -eq 1', '$true'),
        @('($profiles -band 6) -ne 6', '($profiles -band 2) -ne 2'),
        @('$rule.Enabled -and $rule.Action -eq 1', '$true'),
        @('protocol=UDP profile=', 'protocol=TCP profile='),
        @("return @((Join-Path `$game 'witness64_d3d11.exe'), (Join-Path `$game 'witness_launcher.exe'))", "return @((Join-Path `$game 'witness64_d3d11.exe'))"),
        @(' --coop-invite "%1"', ' %1'),
        @('if ($options.invite_links)', 'if ($false)'),
        @("        `$dirs += Join-Path `$lib 'steamapps\common\The Witness'", ''),
        @('if (-not $selected -and $hasCoop)', 'if ($false)'),
        @("(Test-WitnessProxy (Join-Path `$dir 'dsound.dll'))", '$false'),
        @("'GOG Galaxy\Games\The Witness', 'The Witness'", "'The Witness'")
    )) {
        Require ($source.Contains($twin[0])) "mutant applies: $($twin[0])"
        $failed = $false
        try { Run-Checks ($source.Replace($twin[0], $twin[1])) } catch { $failed = $true }
        Require $failed "mutant must fail: $($twin[0])"
        Write-Output "KILLED: $($twin[0])"
    }
}
