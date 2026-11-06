# Installer policy and file ownership journal for The Witness co-op. No UI or registry writes.
# A receipt beside each managed file records what was there before and what was installed, so an
# uninstall can put the original back and never removes a file it did not install.
$script:WitnessExeSha = '8d672d444df6a6df7130f25a517bdab1af92731fe2138dc5b324d519561d55a5'      # witness64_d3d11.exe, Steam build 11370412
$script:WitnessAnselSha = '3b0677ae1e01fc17645c903c8322497785768145abbe3758364299fdaa8aff15'    # the game's own AnselSDK64.dll
# Installed in this order. The proxy is dsound.dll (the game imports it but does not ship it); the game's own files are not changed.
$script:WitnessManaged = @('dsound.dll', 'witness_launcher.exe', 'witness_models\adventurer.glb', 'witness_models\adventurer.glb.txt')
# A dsound.dll that was in the game folder before (another mod, an ASI loader) is kept under this name; the proxy loads it after itself.
$script:WitnessChain = 'dsound.chain.dll'
# The layout up to v0.1.0-alpha: the proxy in AnselSDK64.dll, the game's own Ansel DLL renamed AnselSDK64_real.dll. Setup migrates it.
$script:WitnessLegacy = @('AnselSDK64.dll', 'AnselSDK64_real.dll')

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
    return Get-CoopHash (Join-Path $package $relative)
}
function Test-CoopFileContains([string]$file, [string]$text) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { return $false }
    return [Text.Encoding]::GetEncoding(28591).GetString([IO.File]::ReadAllBytes($file)).Contains($text)      # Latin-1: one character per byte
}
# A dsound.dll of this mod (any version) carries this text; anything else of that name belongs to someone else and is chained.
function Test-WitnessProxy($file) { return (Test-CoopFileContains $file 'witness-coop:proxy:dsound') }
# The AnselSDK64.dll proxy of v0.1.0-alpha and earlier says this in its log text.
function Test-WitnessOldProxy($file) { return (Test-CoopFileContains $file 'real Ansel DLL') }
function Test-WitnessOriginalHash($hash) { return ($hash -and $hash -eq $script:WitnessAnselSha) }      # verify-original

# The old layout in a game folder: is it there, can it be undone safely, and where is the game's own Ansel DLL (verified by hash)?
function Get-WitnessLegacy($game) {
    $a = Join-Path $game 'AnselSDK64.dll'; $r = Join-Path $game 'AnselSDK64_real.dll'
    $result = [pscustomobject]@{ present=$false; blocked=$null; source=$null }
    try {
        foreach ($p in $a, $r, "$a.coop-original", "$a.coop-receipt.json", "$a.coop-stage", "$r.coop-receipt.json", "$r.coop-original", "$r.coop-stage") { Assert-CoopRegularPath $p }
        $ha = Get-CoopHash $a; $hr = Get-CoopHash $r; $hb = Get-CoopHash "$a.coop-original"; $hs = Get-CoopHash "$a.coop-stage"
        $result.present = [bool]($hr -or $hb -or $hs -or (Test-Path -LiteralPath "$a.coop-receipt.json") -or (Test-Path -LiteralPath "$r.coop-receipt.json") -or ($ha -and $ha -ne $script:WitnessAnselSha))
        if (-not $result.present) { return $result }
        $help = 'Nothing was changed. In Steam: right-click The Witness > Properties > Installed Files > Verify integrity of game files, then run Setup again.'
        foreach ($c in @(@($r, $hr), @("$a.coop-original", $hb), @("$a.coop-stage", $hs))) {
            if ($c[1] -and -not (Test-WitnessOriginalHash $c[1])) {
                $result.blocked = "$(Split-Path -Leaf $c[0]) is not the game's own Ansel DLL (SHA-256 starting $($c[1].Substring(0, 12)), expected $($script:WitnessAnselSha.Substring(0, 12))). Setup will not put it back as AnselSDK64.dll. $help"
                return $result
            }
        }
        if ($ha -and -not (Test-WitnessOriginalHash $ha)) {
            $receipt = $null; try { $receipt = Read-CoopReceipt $a } catch { }
            if (-not ((Test-WitnessOldProxy $a) -or ($receipt -and $receipt.installed -eq $ha))) {
                $result.blocked = "AnselSDK64.dll is neither the game's own file nor this mod's old proxy (SHA-256 starting $($ha.Substring(0, 12))). $help"
                return $result
            }
        }
        foreach ($c in @(@($a, $ha), @($r, $hr), @("$a.coop-original", $hb), @("$a.coop-stage", $hs))) {
            if (Test-WitnessOriginalHash $c[1]) { $result.source = $c[0]; break }
        }
        if (-not $result.source) { $result.blocked = "The game's own AnselSDK64.dll was not found (expected SHA-256 starting $($script:WitnessAnselSha.Substring(0, 12))), so the old co-op layout cannot be undone. $help" }
    } catch { $result.present = $true; $result.blocked = "The old co-op files (AnselSDK64.dll, AnselSDK64_real.dll) could not be inspected: $($_.Exception.Message)" }
    return $result
}
# Old layout -> the game's own AnselSDK64.dll back under its name (staged and verified), then the old proxy, the renamed copy and their
# receipts go. Every step can be repeated: an interrupted migration finishes on the next run. Returns $true when there was something to do.
function Invoke-WitnessMigration($game) {
    $l = Get-WitnessLegacy $game
    if (-not $l.present) { return $false }
    if ($l.blocked) { throw $l.blocked }
    $a = Join-Path $game 'AnselSDK64.dll'; $r = Join-Path $game 'AnselSDK64_real.dll'
    if (-not (Test-WitnessOriginalHash (Get-CoopHash $a))) {
        $bytes = [IO.File]::ReadAllBytes($l.source)
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $hash = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() } finally { $sha.Dispose() }
        if (-not (Test-WitnessOriginalHash $hash)) { throw "$(Split-Path -Leaf $l.source) changed while Setup read it; nothing was replaced." }
        if ($l.source -ne "$a.coop-stage") {
            [IO.File]::WriteAllBytes("$a.coop-stage", $bytes)
            if (-not (Test-WitnessOriginalHash (Get-CoopHash "$a.coop-stage"))) { throw 'Staged AnselSDK64.dll verification failed' }
        }
        Move-Item -LiteralPath "$a.coop-stage" -Destination $a -Force      # remove-old-proxy
    }
    if (-not (Test-WitnessOriginalHash (Get-CoopHash $a))) { throw 'AnselSDK64.dll could not be restored; the old files were kept.' }
    # the game's own file is in place and verified: what the old layout added goes
    foreach ($p in "$a.coop-receipt.json", "$r.coop-receipt.json", "$a.coop-receipt.json.tmp", "$r.coop-receipt.json.tmp") { if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p } }
    foreach ($p in $r, "$a.coop-original", "$a.coop-stage") {
        if ((Test-Path -LiteralPath $p) -and (Test-WitnessOriginalHash (Get-CoopHash $p))) { Remove-Item -LiteralPath $p }
    }
    return $true
}
# Before the proxy goes in: a dsound.dll that is not this mod's (and not installed by this Setup) is kept as dsound.chain.dll, which the
# proxy loads after itself. Uninstall removes the chain copy and puts the file back as dsound.dll (its receipt keeps the original).
function Install-WitnessChain($game) {
    $target = Join-Path $game 'dsound.dll'; $chain = Join-Path $game $script:WitnessChain
    Assert-CoopRegularPath $target; Assert-CoopRegularPath $chain
    if (-not (Test-Path -LiteralPath $target -PathType Leaf)) { return $false }
    if (Test-Path -LiteralPath "$target.coop-receipt.json") { return $false }      # ours: Setup put it there
    if (Test-WitnessProxy $target) { return $false }                                  # ours, copied in by hand or by a mod manager
    $bytes = [IO.File]::ReadAllBytes($target)
    $existing = Get-CoopHash $chain
    if ($existing -and -not (Test-Path -LiteralPath "$chain.coop-receipt.json")) {
        if ($existing -ne (Get-CoopHash $target)) { throw "dsound.chain.dll is already in the game folder and differs from dsound.dll; move one of them away and run Setup again. Nothing was changed." }
    }
    Install-CoopFile $chain $bytes
    return $true
}
# 'current' = the supported Steam build, 'conflict' = another build (the mod's addresses would not fit), 'unknown' = no exe
function Get-WitnessBuildState($game) {
    try {
        $hash = Get-CoopHash (Join-Path $game 'witness64_d3d11.exe')
        if (-not $hash) { return 'unknown' }
        if ($hash -eq $script:WitnessExeSha) { return 'current' } else { return 'conflict' }
    } catch { return 'unknown' }
}
# ---- the plain-words answer when the game is not the supported build: what was found, what is needed, what to do
$script:WitnessFaq = 'https://github.com/TommoDotCommo/witness-coop#faq-not-the-supported-build'
function Get-WitnessBuildReport($game) {
    $exe = Join-Path $game 'witness64_d3d11.exe'
    $report = [ordered]@{ state = (Get-WitnessBuildState $game); store = 'unknown'; buildId = $null; version = $null; sha = $null; size = $null }
    try {
        if (Test-Path -LiteralPath $exe -PathType Leaf) {
            $report.size = (Get-Item -LiteralPath $exe).Length
            $hash = Get-CoopHash $exe
            if ($hash) { $report.sha = $hash.Substring(0, 12) }
            $v = (Get-Item -LiteralPath $exe).VersionInfo.FileVersion
            if ($v) { $report.version = "$v" }
        }
    } catch { }
    # the store, as far as the folder shows it
    try {
        $full = [IO.Path]::GetFullPath($game)
        $manifest = Join-Path (Split-Path -Parent (Split-Path -Parent $full)) 'appmanifest_210970.acf'
        if (Test-Path -LiteralPath $manifest) {
            $report.store = 'Steam'
            $m = [regex]::Match((Get-Content -LiteralPath $manifest -Raw), '"buildid"\s+"(\d+)"')
            if ($m.Success) { $report.buildId = $m.Groups[1].Value }
        } elseif ($full -match '\\steamapps\\common\\') { $report.store = 'Steam' }
        elseif (Test-Path -LiteralPath (Join-Path $game '.egstore')) { $report.store = 'Epic Games' }
        elseif (@(Get-ChildItem -LiteralPath $game -Filter 'goggame-*.info' -ErrorAction SilentlyContinue).Count -gt 0) { $report.store = 'GOG' }
    } catch { }
    return [pscustomobject]$report
}
function Format-WitnessBuildFound($report) {
    $what = if ($report.state -eq 'unknown' -and -not $report.size) { 'no readable witness64_d3d11.exe' }
            elseif ($report.size -and $report.size -lt 1MB) { "witness64_d3d11.exe is only $($report.size) bytes (damaged or incomplete)" }
            else { "witness64_d3d11.exe" + $(if ($report.version) { ", file version $($report.version)" } else { '' }) + $(if ($report.sha) { ", SHA-256 starting $($report.sha)" } else { '' }) }
    $where = if ($report.store -eq 'Steam') { 'a Steam copy' + $(if ($report.buildId) { " (Steam build $($report.buildId))" } else { '' }) }
             elseif ($report.store -eq 'unknown') { 'a copy from an unknown source' } else { "a $($report.store) copy" }
    return "$where, $what"
}
function Format-WitnessBuildRefusal($report) {
    $todo = if ($report.store -eq 'Steam') {
        "In Steam, right-click The Witness > Properties > Betas > choose None, then Properties > Installed Files > Verify integrity of game files. Then run Setup again. If Steam already shows the newest version, the game was updated after this mod was made: co-op will work again when the mod is updated."
    } elseif ($report.store -eq 'unknown') {
        "Only the Steam copy of The Witness is supported right now. If this is the Steam copy, verify its files in Steam (Properties > Installed Files > Verify integrity of game files) and pick the game folder Steam shows (Properties > Installed Files > Browse)."
    } else { "Only the Steam copy of The Witness is supported right now; $($report.store) copies are not supported yet." }
    return @"
This copy of The Witness is not the build the co-op mod is made for. Nothing was changed.

Found:  $(Format-WitnessBuildFound $report)
Needed: Steam build 11370412 (witness64_d3d11.exe, SHA-256 starting $($script:WitnessExeSha.Substring(0, 12)))

What to do: $todo

More: $($script:WitnessFaq)
"@
}
function Format-WitnessBuildShort($report) {
    # one or two lines for the setup window's status label
    $where = if ($report.store -eq 'Steam') { 'a Steam copy' + $(if ($report.buildId) { " (build $($report.buildId))" } else { '' }) } elseif ($report.store -eq 'unknown') { 'an unknown copy' } else { "a $($report.store) copy" }
    $todo = if ($report.store -eq 'Steam') { 'In Steam: Properties > Betas > None, then Verify integrity of game files.' } else { 'Only the Steam copy is supported yet.' }
    return "Not Steam build 11370412 (found $where, SHA-256 $($report.sha)...). $todo See the README FAQ."
}
# ---- coop-mod.json (tools/mod_manifest.py): the files the package puts into the game folder, with their hashes. Setup reads its file list from the
# package's copy; a copy in the game folder (a mod manager deployed the whole package) names that release.
function Read-CoopManifest($folder) {
    $file = Join-Path $folder 'coop-mod.json'
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { return $null }
    try { $m = Get-Content -LiteralPath $file -Raw | ConvertFrom-Json } catch { return $null }
    if ($m.schema -ne 1 -or -not $m.files) { return $null }
    foreach ($f in $m.files) { if ("$($f.path)" -notmatch '^[A-Za-z0-9._-]+(/[A-Za-z0-9._-]+)*$' -or "$($f.sha256)" -notmatch '^[a-f0-9]{64}$') { return $null } }
    return $m
}
function Get-CoopPackageFiles($package) {
    $m = Read-CoopManifest $package
    if (-not $m) { return $script:WitnessManaged }
    return @($m.files | ForEach-Object { "$($_.path)".Replace('/', '\') })
}
# ---- who installed the files: Setup receipts, a mod manager's hard links (Vortex deploys by hard link), a hand copy that matches a release, or nobody
function Get-CoopLinkCount($file) {
    try {
        $result = Invoke-CoopHiddenProcess 'fsutil.exe' ('hardlink list "{0}"' -f $file)
        if ($result.ExitCode -eq 0 -and $result.Output) { return @($result.Output -split '\r?\n' | Where-Object { $_.Trim() }).Count }
    } catch { }
    return 1
}
function Get-CoopOwner($game, $package) {
    $manifests = @(@(Read-CoopManifest $package; Read-CoopManifest $game) | Where-Object { $_ })
    $files = @(Get-CoopPackageFiles $package) + @('AnselSDK64.dll')      # (the receipt of the alpha's layout counts: Setup put that one there)
    foreach ($relative in $files) { if (Test-Path -LiteralPath (Join-Path $game "$relative.coop-receipt.json")) { return 'setup' } }
    foreach ($m in $manifests) {
        $match = $true
        foreach ($f in $m.files) {
            $target = Join-Path $game ("$($f.path)".Replace('/', '\'))
            try { $h = Get-CoopHash $target } catch { $h = $null }
            if ($h -ne "$($f.sha256)") { $match = $false; break }
        }
        if ($match) {
            foreach ($f in $m.files) {
                if ((Get-CoopLinkCount (Join-Path $game ("$($f.path)".Replace('/', '\')))) -gt 1) { return 'vortex' }
            }
            return 'manual'
        }
    }
    return 'none'
}
# Adopt only a complete, verified release copy, never a manager's linked deployment. These files are the mod itself, so uninstall removes them
# (nothing is restored): the receipt has no original.
function Adopt-CoopInstall($game, $package) {
    if ((Get-CoopOwner $game $package) -ne 'manual') { throw 'Only a verified manual install can be adopted.' }
    foreach ($m in @(Read-CoopManifest $package; Read-CoopManifest $game)) {
        if (-not $m) { continue }
        $matching = $true
        foreach ($f in $m.files) {
            $target = Join-Path $game ($f.path.Replace('/', '\'))
            Assert-CoopRegularPath $target
            Assert-CoopNoRecoveryArtifacts $target
            Assert-CoopRegularPath "$target.coop-receipt.json"
            if (Test-Path -LiteralPath "$target.coop-original") { throw 'An original backup needs review before adoption.' }
            if ((Get-CoopHash $target) -ne $f.sha256 -or (Get-CoopLinkCount $target) -gt 1) { $matching = $false; break }
        }
        if (-not $matching) { continue }
        foreach ($f in $m.files) {
            Write-CoopReceipt (Join-Path $game ($f.path.Replace('/', '\'))) ([pscustomobject]@{ schema=1; original=$null; installed=$f.sha256; pending=$null })
        }
        return
    }
    throw 'The manual install changed; refresh Setup and try again.'
}
$script:CoopOwnerText = @{
    setup  = 'Installed by Setup'
    vortex = 'Installed by Vortex: update or remove it there'
    manual = 'Copied by hand: Setup can take it over'
    none   = 'Not installed'
}

# ---- every install of the game on this PC: Steam libraries, GOG, Epic, the uninstall list
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
# A folder's path as spelled on disk (each name as the file system has it, the drive letter in capitals); a network path is kept as given.
function Get-TrueCasePath($path) {
    $full = (Resolve-Path -LiteralPath $path).ProviderPath.TrimEnd('\')
    if ($full.StartsWith('\\')) { return $full }
    $parts = $full.Split('\'); $out = $parts[0].ToUpperInvariant()
    for ($i = 1; $i -lt $parts.Count; $i++) {
        $match = @(([IO.DirectoryInfo]::new($out + '\')).GetFileSystemInfos($parts[$i]) | Select-Object -First 1)      # (.NET, not Get-ChildItem: an exact name, no wildcards)
        $out = $out + '\' + $(if ($match.Count) { $match[0].Name } else { $parts[$i] })
    }
    return $out
}
function Find-AllGames {
    $found = @()
    $exe = 'witness64_d3d11.exe'
    foreach ($lib in Steam-Libraries) {
        $acf = Join-Path $lib 'steamapps\appmanifest_210970.acf'
        $dirs = @()
        if (Test-Path $acf) {
            $m = [regex]::Match((Get-Content $acf -Raw), '"installdir"\s+"([^"]+)"')
            if ($m.Success) { $dirs += Join-Path $lib "steamapps\common\$($m.Groups[1].Value)" }
        }
        $dirs += Join-Path $lib 'steamapps\common\The Witness'
        foreach ($d in $dirs) { if (Test-Path (Join-Path $d $exe)) { $found += $d } }
    }
    # registered locations: the uninstall list (GOG Galaxy, other launchers, manual installs)
    foreach ($key in @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall',
                       'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall',
                       'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall')) {
        foreach ($entry in @(Get-ChildItem $key -ErrorAction SilentlyContinue)) {
            $value = Get-ItemProperty $entry.PSPath -ErrorAction SilentlyContinue
            if ($value.DisplayName -match '^The Witness' -and $value.InstallLocation -and
                (Test-Path -LiteralPath (Join-Path $value.InstallLocation $exe) -PathType Leaf)) { $found += $value.InstallLocation }
        }
    }
    if ($env:ProgramData) {      # Epic Games Launcher manifests
        $epic = Join-Path $env:ProgramData 'Epic\EpicGamesLauncher\Data\Manifests'
        foreach ($entry in @(Get-ChildItem -LiteralPath $epic -Filter '*.item' -ErrorAction SilentlyContinue)) {
            try {
                $value = Get-Content -LiteralPath $entry.FullName -Raw | ConvertFrom-Json
                if ($value.InstallLocation -and (Test-Path -LiteralPath (Join-Path $value.InstallLocation $exe) -PathType Leaf)) { $found += $value.InstallLocation }
            } catch { }      # a damaged manifest must not hide the other installs
        }
    }
    foreach ($key in @('HKLM:\SOFTWARE\GOG.com\Games', 'HKLM:\SOFTWARE\WOW6432Node\GOG.com\Games')) {
        foreach ($entry in @(Get-ChildItem $key -ErrorAction SilentlyContinue)) {
            $value = Get-ItemProperty $entry.PSPath -ErrorAction SilentlyContinue
            if ($value.path -and (Test-Path -LiteralPath (Join-Path $value.path $exe) -PathType Leaf)) { $found += $value.path }
        }
    }
    foreach ($root in @($env:ProgramFiles, ${env:ProgramFiles(x86)}, $env:ProgramW6432, (Join-Path $env:SystemDrive 'GOG Games'))) {
        if (-not $root) { continue }
        foreach ($suffix in @('GOG Galaxy\Games\The Witness', 'The Witness')) {
            $d = Join-Path $root $suffix
            if (Test-Path -LiteralPath (Join-Path $d $exe) -PathType Leaf) { $found += $d }
        }
    }
    # one card per folder: each as spelled on disk, and no folder twice in another case (Steam's libraryfolders.vdf keeps library paths in lower case, the
    # registry as created: the Steam install showed twice, differing only in case; owner, 2026-11-06)
    $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    return @($found | ForEach-Object { Get-TrueCasePath $_ } | Where-Object { $seen.Add($_) })
}

# ---- the three optional Windows integrations (G3): desktop shortcut, Windows Firewall rule, witness-coop:// links. Uninstall removes all three.
$script:CoopFirewallName = 'The Witness Co-op (UDP)'
function Get-CoopFirewallPrograms($game) { return @((Join-Path $game 'witness64_d3d11.exe'), (Join-Path $game 'witness_launcher.exe')) }
function New-WitnessDesktopShortcut($dir, $desktop = [Environment]::GetFolderPath('DesktopDirectory')) {
    if (-not (Test-Path -LiteralPath (Join-Path $dir 'witness64_d3d11.exe') -PathType Leaf)) { throw 'Choose the folder containing witness64_d3d11.exe.' }
    $shell = New-Object -ComObject WScript.Shell
    $link = $null
    try {
        $link = $shell.CreateShortcut((Join-Path $desktop 'The Witness Co-op.lnk'))
        $link.TargetPath = Join-Path $dir 'witness_launcher.exe'
        $link.WorkingDirectory = $dir
        $link.IconLocation = (Join-Path $dir 'witness64_d3d11.exe') + ',0'
        $link.Description = 'Play The Witness together: the co-op launcher'
        $link.Save()
    } finally {
        if ($link) { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($link) }
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell)
    }
}
function Get-CoopShortcutTarget($desktop = [Environment]::GetFolderPath('DesktopDirectory')) {
    $file = Join-Path $desktop 'The Witness Co-op.lnk'
    if (-not (Test-Path -LiteralPath $file)) { return '' }
    $shell = New-Object -ComObject WScript.Shell
    $link = $null
    try { $link = $shell.CreateShortcut($file); return $link.TargetPath }
    finally {
        if ($link) { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($link) }
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell)
    }
}
# The link handler starts the launcher with the link as its one argument; the launcher puts it into its join box (nothing is joined without a click).
function Get-CoopInviteCommand($game) {
    return '"{0}" --coop-invite "%1"' -f (Join-Path $game 'witness_launcher.exe')
}
function Test-CoopFirewallRule($rule, $program) {
    return $rule.Name -eq $script:CoopFirewallName -and $rule.ApplicationName -eq $program -and $rule.Protocol -eq 17 -and $rule.Direction -eq 1
}
function Get-CoopFirewallRules($program) {
    $policy = $null
    try {
        $policy = New-Object -ComObject HNetCfg.FwPolicy2
        return @($policy.Rules | Where-Object { Test-CoopFirewallRule $_ $program })
    } catch { return @() }
    finally { if ($policy) { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($policy) } }
}
function Get-CoopOptions($game) {
    $exe = Join-Path $game 'witness_launcher.exe'
    $protocol = Get-ItemProperty 'HKCU:\Software\Classes\witness-coop\shell\open\command' -ErrorAction SilentlyContinue
    $allowed = $true
    foreach ($program in Get-CoopFirewallPrograms $game) {      # both programs, on Private and Public
        $profiles = 0
        foreach ($rule in @(Get-CoopFirewallRules $program)) { if ($rule.Enabled -and $rule.Action -eq 1) { $profiles = $profiles -bor $rule.Profiles } }
        if (($profiles -band 6) -ne 6) { $allowed = $false }      # Private=2, Public=4
    }
    return [pscustomobject]@{
        desktop_shortcut=((Get-CoopShortcutTarget) -eq $exe)
        allow_hosting=$allowed
        invite_links=([bool]($protocol -and $protocol.'(default)' -eq (Get-CoopInviteCommand $game)))
    }
}
function Test-CoopAdmin {
    return ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}
function Set-CoopFirewall($game, [bool]$remove) {
    foreach ($program in Get-CoopFirewallPrograms $game) {
        # Keep all other rules, including the rules of another copy of the game.
        $delete = 'advfirewall firewall delete rule name="{0}" program="{1}" protocol=UDP dir=in' -f $script:CoopFirewallName, $program
        if ($remove) {
            if (@(Get-CoopFirewallRules $program).Count) { $result = Invoke-CoopHiddenProcess 'netsh.exe' $delete } else { continue }
        } else {
            # Replace our exact rule to make retries idempotent; one rule per program covers both profiles.
            if (@(Get-CoopFirewallRules $program).Count) { [void](Invoke-CoopHiddenProcess 'netsh.exe' $delete) }
            $add = 'advfirewall firewall add rule name="{0}" dir=in action=allow program="{1}" protocol=UDP profile=private,public enable=yes' -f $script:CoopFirewallName, $program
            $result = Invoke-CoopHiddenProcess 'netsh.exe' $add
        }
        if ($result.ExitCode -ne 0) { throw 'Windows Firewall could not be changed. Run Setup again and allow administrator permission, or untick Allow hosting.' }
    }
}
function Set-CoopUserOptions($game, [bool]$shortcut, [bool]$invites) {
    if ($shortcut) { New-WitnessDesktopShortcut $game }
    if ($invites) {
        $root = 'HKCU:\Software\Classes\witness-coop'
        New-Item -Path "$root\shell\open\command" -Force | Out-Null
        Set-Item -LiteralPath $root -Value 'URL:The Witness Co-op invite'
        New-ItemProperty -LiteralPath $root -Name 'URL Protocol' -Value '' -PropertyType String -Force | Out-Null
        Set-Item -LiteralPath "$root\shell\open\command" -Value (Get-CoopInviteCommand $game)
    }
}
function Remove-CoopUserOptions($game) {
    $options = Get-CoopOptions $game
    if ($options.desktop_shortcut) { Remove-Item -LiteralPath (Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) 'The Witness Co-op.lnk') }
    if ($options.invite_links) { Remove-Item -LiteralPath 'HKCU:\Software\Classes\witness-coop' -Recurse }
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
                'dsound.dll' { 'The co-op proxy; it hands every sound call to Windows'' own dsound.dll (the game''s files stay as they are)' }
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
    $chain = Join-Path $game $script:WitnessChain
    if ((Test-Path -LiteralPath $chain) -or (Test-Path -LiteralPath "$chain.coop-receipt.json")) {
        $o = Get-CoopObservation $chain $(if (Test-Path -LiteralPath "$chain.coop-receipt.json") { (Read-CoopReceipt $chain).installed } else { Get-CoopHash $chain })
        if ($o.receipt) { $owned = $true; $states += $o.status }
        $rows += [pscustomobject]@{ item=$script:WitnessChain; status=$o.status; detail='Another mod''s dsound.dll, kept by Setup and loaded by the proxy after itself; it goes back as dsound.dll on uninstall' }
    }
    $legacy = Get-WitnessLegacy $game
    if ($legacy.present) {
        if (Test-Path -LiteralPath (Join-Path $game 'AnselSDK64.dll.coop-receipt.json')) { $owned = $true }
        $states += $(if ($legacy.blocked) { 'conflict' } else { 'outdated' })
        $rows += [pscustomobject]@{ item='AnselSDK64.dll (old layout)'; status=$(if ($legacy.blocked) { 'conflict' } else { 'outdated' }); detail=$(if ($legacy.blocked) { $legacy.blocked } else { 'An older co-op install replaced the game''s Ansel DLL; Setup puts the game''s own file back (verified) and removes the old proxy' }) }
    }
    $actions = Get-CoopActions $states $owned
    if ($legacy.present -and -not $legacy.blocked -and $actions.install) { $actions.install = 'Update' }
    $owner = Get-CoopOwner $game $package
    if ($owner -eq 'vortex') {
        # a mod manager's files: it updates and removes them; Setup says so and offers Play (J6, J7)
        $actions = [pscustomobject]@{ install=''; uninstall=$false; blocked=$false }
    }
    if ($owner -eq 'manual' -and -not $actions.blocked -and $build -eq 'current') {
        $actions.install = 'Install'; $actions.uninstall = $false      # a hand copy: Setup takes it over (adopts it) and updates it from then on
    }
    $rows += [pscustomobject]@{ item='witness_coop.cfg'; status=$(if (Test-Path -LiteralPath (Join-Path $game 'witness_coop.cfg')) { 'present' } else { 'written by the launcher' }); detail='Your co-op settings; kept on uninstall' }
    $rows += [pscustomobject]@{ item='Saves, registry, the game''s own files'; status='untouched'; detail='Saves stay in %APPDATA%\The Witness; the game''s executable and DLLs are never changed' }
    $report = if ($build -ne 'current') { Get-WitnessBuildReport $game } else { $null }
    $options = Get-CoopOptions $game
    foreach ($name in 'desktop_shortcut', 'allow_hosting', 'invite_links') {
        $rows += [pscustomobject]@{ item=$name; status=$(if ($options.$name) { 'current' } else { 'missing' }); detail='Optional Setup integration; removed on uninstall' }
    }
    return [pscustomobject]@{ schema=1; items=$rows; actions=$actions; build=$build; report=$report; legacy=[bool]$legacy.present; chain=[bool](Test-Path -LiteralPath $chain); owner=$owner; options=$options }
}
# One card per install found: where, which build (checked against the supported one), who put co-op there, and what Setup can do. Co-op installs and
# the supported Steam build come first in the choice: the one with co-op, else the first supported build, else the first.
function Get-CoopInstallCards($package, $dirs = @(Find-AllGames)) {
    $cards = @(); $selected = $null
    foreach ($dir in $dirs) {
        $snapshot = Get-WitnessInstallStatus $dir $package
        $hasCoop = $snapshot.owner -ne 'none' -or (Test-WitnessProxy (Join-Path $dir 'dsound.dll'))
        $report = Get-WitnessBuildReport $dir
        $card = [pscustomobject]@{ path=$dir; build=$snapshot.build; supported=($snapshot.build -eq 'current'); store=$report.store; build_id=$report.buildId; state=$snapshot; has_coop=$hasCoop; selected=$false }
        $cards += $card
        if (-not $selected -and $hasCoop) { $selected = $card }
    }
    if (-not $selected) { $selected = $cards | Where-Object { $_.supported } | Select-Object -First 1 }
    if (-not $selected) { $selected = $cards | Select-Object -First 1 }
    if ($selected) { $selected.selected = $true }
    return $cards
}
