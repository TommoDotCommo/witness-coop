# Installer policy and file ownership journal for The Witness co-op. No UI or registry writes.
# A receipt beside each managed file records what was there before and what was installed, so an
# uninstall can put the original back and never removes a file it did not install.
$script:WitnessExeSha = '8d672d444df6a6df7130f25a517bdab1af92731fe2138dc5b324d519561d55a5'      # witness64_d3d11.exe, Steam build 11370412
$script:WitnessAnselSha = '3b0677ae1e01fc17645c903c8322497785768145abbe3758364299fdaa8aff15'    # the game's own AnselSDK64.dll
# Installed in this order; AnselSDK64_real.dll is the game's own Ansel DLL (the proxy loads it by name).
$script:WitnessManaged = @('AnselSDK64_real.dll', 'AnselSDK64.dll', 'witness_launcher.exe', 'witness_models\adventurer.glb', 'witness_models\adventurer.glb.txt')

function Get-CoopHash($file) {
    if (-not (Test-Path -LiteralPath $file)) { return $null }
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw 'Expected a regular file' }
    return (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
}

# Pure decisions run in a small native helper built from install-policy.h.
# No path, address or player data is sent anywhere; this process has no networking.
$script:CoopPolicy = Join-Path $PSScriptRoot 'install-policy.exe'
$script:CoopStates = @('current', 'missing', 'outdated', 'modified', 'conflict', 'unknown', 'unmanaged')
# Hidden window style alone can still create a console briefly. Suppress console
# creation and capture both streams so callers never borrow the desktop.
function Invoke-CoopHiddenProcess([string]$file, [string]$arguments) {
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $file
    $info.Arguments = $arguments
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $info
    try {
        [void]$process.Start()
        $output = $process.StandardOutput.ReadToEndAsync()
        $errors = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        return [pscustomobject]@{ ExitCode=$process.ExitCode; Output=$output.Result; Error=$errors.Result }
    } finally { $process.Dispose() }
}
function Invoke-CoopPolicy([string[]]$values) {
    # The policy protocol contains only words, numbers and hashes.
    foreach ($value in $values) {
        if ($value -notmatch '^[a-zA-Z0-9-]+$') { throw 'Invalid setup policy argument.' }
    }
    $result = Invoke-CoopHiddenProcess $script:CoopPolicy ($values -join ' ')
    if ($result.ExitCode -ne 0 -or $result.Output.Trim() -notmatch '^\d+$') { throw 'Setup policy helper failed; unpack the whole release again.' }
    return [int]$result.Output.Trim()
}
function Get-CoopFileState($expected, $observed, $receipt, $backup, $unknown = $false) {
    $schema = if ($receipt) { if ($receipt.schema -eq 1) { 1 } else { -1 } } else { 0 }
    $values = @('file', "$expected", "$observed", "$schema", "$($receipt.installed)", "$($receipt.original)", "$($receipt.pending)", "$backup", "$([int][bool]$unknown)")
    for ($i = 1; $i -lt $values.Count; $i++) { if (-not $values[$i]) { $values[$i] = '-' } }
    return $script:CoopStates[(Invoke-CoopPolicy $values)]
}

function Read-CoopReceipt($target) {
    $file = "$target.coop-receipt.json"
    if (-not (Test-Path -LiteralPath $file)) { return $null }
    $r = Get-Content -LiteralPath $file -Raw | ConvertFrom-Json
    if ($r.schema -ne 1 -or "$($r.installed)" -notmatch '^[a-f0-9]{64}$' -or
        ($r.original -and "$($r.original)" -notmatch '^[a-f0-9]{64}$') -or
        ($r.pending -and "$($r.pending)" -notmatch '^[a-f0-9]{64}$')) { throw 'Invalid ownership receipt' }
    return $r
}
function Write-CoopReceipt($target, $receipt) {
    $temp = "$target.coop-receipt.json.tmp"
    [IO.File]::WriteAllText($temp, ($receipt | ConvertTo-Json -Compress))
    Move-Item -LiteralPath $temp -Destination "$target.coop-receipt.json" -Force
}
function Get-CoopObservation($target, $expected) {
    try {
        Assert-CoopRegularPath $target
        Assert-CoopRegularPath "$target.coop-receipt.json"
        Assert-CoopRegularPath "$target.coop-original"
        $receipt = Read-CoopReceipt $target
        $observed = Get-CoopHash $target
        $backup = Get-CoopHash "$target.coop-original"
        $state = Get-CoopFileState $expected $observed $receipt $backup
        return [pscustomobject]@{ path=$target; status=$state; receipt=$receipt; observed=$observed; expected=$expected; backup=$backup }
    } catch {
        return [pscustomobject]@{ path=$target; status='unknown'; receipt=$null; observed=$null; expected=$expected; backup=$null }
    }
}
function Assert-CoopRegularPath($target) {
    # Refuse junctions/symlinks, including parent directories, before any write.
    $p = [IO.Path]::GetFullPath($target)
    while ($p) {
        if (Test-Path -LiteralPath $p) {
            if ((Get-Item -LiteralPath $p -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked path refused: $p" }
        }
        $parent = Split-Path -Parent $p
        if ($parent -eq $p) { break }
        $p = $parent
    }
}
function Assert-CoopNoRecoveryArtifacts($target) {
    foreach ($suffix in '.coop-stage', '.coop-receipt.json.tmp') {
        if (Test-Path -LiteralPath "$target$suffix") { throw "Recovery artifact preserved: $target$suffix. Review before retrying." }
    }
}
function Install-CoopFile($target, [byte[]]$bytes) {
    Assert-CoopNoRecoveryArtifacts $target
    Assert-CoopRegularPath $target
    foreach ($suffix in '.coop-original', '.coop-receipt.json', '.coop-receipt.json.tmp', '.coop-stage') { Assert-CoopRegularPath "$target$suffix" }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $expected = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() } finally { $sha.Dispose() }
    $o = Get-CoopObservation $target $expected
    if ($o.status -in 'modified', 'conflict', 'unknown') { throw "Refusing to replace $target ($($o.status)). Keep the file and resolve its ownership first." }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    $r = $o.receipt
    if (-not $r) {
        # Journal intent before replacing anything. A crash leaves the original in place.
        # Preserve even an identical unowned file; equality does not establish ownership.
        if ($o.observed) {
            Copy-Item -LiteralPath $target -Destination "$target.coop-original" -ErrorAction Stop
            if ((Get-CoopHash "$target.coop-original") -ne $o.observed) { throw 'Original backup verification failed' }
        }
        $r = [pscustomobject]@{ schema=1; original=$o.observed; installed=$expected; pending=$null }
    }
    $r.pending = $expected
    Write-CoopReceipt $target $r
    [IO.File]::WriteAllBytes("$target.coop-stage", $bytes)
    if ((Get-CoopHash "$target.coop-stage") -ne $expected) { throw 'Staged file verification failed' }
    Move-Item -LiteralPath "$target.coop-stage" -Destination $target -Force
    $r.installed = $expected; $r.pending = $null
    Write-CoopReceipt $target $r
}
function Remove-CoopFile($target) {
    Assert-CoopNoRecoveryArtifacts $target
    Assert-CoopRegularPath $target
    foreach ($suffix in '.coop-original', '.coop-receipt.json', '.coop-stage') { Assert-CoopRegularPath "$target$suffix" }
    $r = Read-CoopReceipt $target
    if (-not $r) { return $false } # a file without a receipt was not installed by this setup: keep it
    $o = Get-CoopObservation $target $r.installed
    if ($o.status -in 'modified', 'conflict', 'unknown', 'unmanaged') { throw "Refusing to remove $target ($($o.status)); user changes were preserved." }
    if ($r.original) {
        # Keep the backup until restoration is complete, permitting retry after interruption.
        Copy-Item -LiteralPath "$target.coop-original" -Destination "$target.coop-stage" -Force
        Move-Item -LiteralPath "$target.coop-stage" -Destination $target -Force
        if ((Get-CoopHash $target) -ne $r.original) { throw 'Restoration verification failed' }
    } elseif (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target }
    Remove-Item -LiteralPath "$target.coop-receipt.json"
    if (Test-Path -LiteralPath "$target.coop-original") { Remove-Item -LiteralPath "$target.coop-original" }
    return $true
}
function Get-CoopActions($states, $owned) {
    $mask = 0
    foreach ($state in $states) {
        $index = [array]::IndexOf($script:CoopStates, $state)
        if ($index -lt 0) { $index = 5 }
        $mask = $mask -bor (1 -shl $index)
    }
    $decision = Invoke-CoopPolicy @('actions', "$mask", "$([int][bool]$owned)")
    return [pscustomobject]@{ install=@('', 'Install', 'Update', 'Repair')[$decision -band 15]; uninstall=[bool]($decision -band 16); blocked=[bool]($decision -band 32) }
}

# ---- The Witness: what the package expects of each managed file, and the whole picture of a game folder ----

function Get-WitnessExpectedHash($package, $relative) {
    if ($relative -eq 'AnselSDK64_real.dll') { return $script:WitnessAnselSha }
    return Get-CoopHash (Join-Path $package $relative)
}
# 'current' = the supported Steam build, 'conflict' = another build (the mod's addresses would not fit), 'unknown' = no exe
function Get-WitnessBuildState($game) {
    try {
        $hash = Get-CoopHash (Join-Path $game 'witness64_d3d11.exe')
        if (-not $hash) { return 'unknown' }
        if ($hash -eq $script:WitnessExeSha) { return 'current' } else { return 'conflict' }
    } catch { return 'unknown' }
}
function Get-WitnessInstallStatus($game, $package) {
    $rows = @(); $states = @(); $owned = $false
    $build = Get-WitnessBuildState $game
    $states += $build
    $rows += [pscustomobject]@{ item='witness64_d3d11.exe'; status=$(if ($build -eq 'current') { 'supported build' } elseif ($build -eq 'conflict') { 'unsupported build' } else { 'unknown' }); detail='Steam build 11370412 is required (SHA-256 checked); the game itself is never changed' }
    foreach ($relative in $script:WitnessManaged) {
        $target = Join-Path $game $relative
        try {
            $expected = Get-WitnessExpectedHash $package $relative
            if (-not $expected) { throw 'Missing package payload' }
            $o = Get-CoopObservation $target $expected
            if ($o.receipt) { $owned = $true }
            $states += $o.status
            $detail = switch ($relative) {
                'AnselSDK64.dll' { 'The co-op proxy, in place of the game''s Ansel DLL; the original is put back on uninstall' }
                'AnselSDK64_real.dll' { 'The game''s own Ansel DLL under the name the proxy loads' }
                'witness_launcher.exe' { 'The co-op launcher (lobby, settings); start the game from it' }
                default { 'The player model' }
            }
            $rows += [pscustomobject]@{ item=$relative; status=$o.status; detail=$detail }
            if ($o.receipt -and $o.receipt.original) {
                $rows += [pscustomobject]@{ item="$relative.coop-original"; status=$(if ($o.backup -eq $o.receipt.original) { 'current' } else { 'conflict' }); detail='Verified original; kept until restoration succeeds' }
            }
            foreach ($suffix in '.coop-stage', '.coop-receipt.json.tmp') {
                if (Test-Path -LiteralPath "$target$suffix") {
                    $states += 'conflict'
                    $rows += [pscustomobject]@{ item="$relative$suffix"; status='conflict'; detail='Recovery artifact preserved; review before continuing' }
                }
            }
        } catch { $states += 'unknown'; $rows += [pscustomobject]@{ item=$relative; status='unknown'; detail='Could not inspect payload or installed file' } }
    }
    $actions = Get-CoopActions $states $owned
    $rows += [pscustomobject]@{ item='witness_coop.cfg'; status=$(if (Test-Path -LiteralPath (Join-Path $game 'witness_coop.cfg')) { 'present' } else { 'written by the launcher' }); detail='Your co-op settings; kept on uninstall' }
    $rows += [pscustomobject]@{ item='Saves, registry, the game executable'; status='untouched'; detail='Saves stay in %APPDATA%\The Witness; nothing else on the PC is changed' }
    return [pscustomobject]@{ schema=1; items=$rows; actions=$actions; build=$build }
}
