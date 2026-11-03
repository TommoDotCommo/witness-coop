# User-facing installer for The Witness co-op. Only the game-folder operation is ever elevated (and only when the folder needs it).
param([string]$ProbePath, [ValidateSet('', 'install', 'uninstall')][string]$TestCompletion = '', [switch]$TestLayout, [string]$TestActionsPath,
      [string]$TestOwnerPath, [switch]$TestNoOptions)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()
$backend = Join-Path $PSScriptRoot 'witness-coop.ps1'
. (Join-Path $PSScriptRoot 'install-state.ps1')
$form = New-Object Windows.Forms.Form
$form.Text = 'The Witness Co-op Setup'
$form.ClientSize = New-Object Drawing.Size(760, 690)
$form.FormBorderStyle = 'Sizable'
$form.MinimumSize = New-Object Drawing.Size(776, 729)
$form.AutoScaleMode = [Windows.Forms.AutoScaleMode]::Dpi
$form.MaximizeBox = $false
$form.StartPosition = 'CenterScreen'
$form.BackColor = [Drawing.ColorTranslator]::FromHtml('#141c22')
$form.ForeColor = [Drawing.ColorTranslator]::FromHtml('#ece9e1')
$form.Font = New-Object Drawing.Font 'Segoe UI', 10
$exe = Join-Path $PSScriptRoot 'Witness-Coop-Setup.exe'
if (Test-Path $exe) { try { $form.Icon = [Drawing.Icon]::ExtractAssociatedIcon($exe) } catch { } }
$header = New-Object Windows.Forms.Panel
$header.BackColor = [Drawing.ColorTranslator]::FromHtml('#1d2a33')
$header.SetBounds(0, 0, 760, 91)
$header.Anchor = 'Top, Left, Right'
$brand = New-Object Windows.Forms.Label
$brand.Text = 'THE WITNESS  /  CO-OP'
$brand.Font = New-Object Drawing.Font 'Segoe UI', 19, ([Drawing.FontStyle]::Bold)
$brand.ForeColor = [Drawing.ColorTranslator]::FromHtml('#f2b24a')
$brand.SetBounds(24, 13, 420, 37)
$header.Controls.Add($brand)
$subtitle = New-Object Windows.Forms.Label
$subtitle.Text = 'PLAY THE ISLAND TOGETHER  //  SETUP'
$subtitle.Font = New-Object Drawing.Font 'Segoe UI', 9, ([Drawing.FontStyle]::Bold)
$subtitle.ForeColor = [Drawing.ColorTranslator]::FromHtml('#b7c3c9')
$subtitle.SetBounds(27, 53, 360, 24)
$header.Controls.Add($subtitle)
$stripe = New-Object Windows.Forms.Panel
$stripe.BackColor = [Drawing.ColorTranslator]::FromHtml('#f2b24a')
$stripe.SetBounds(0, 88, 760, 3)
$stripe.Anchor = 'Top, Left, Right'
$header.Controls.Add($stripe)
$form.Controls.Add($header)
function Style-Button($button, $primary) {
    $button.FlatStyle = [Windows.Forms.FlatStyle]::Flat
    $button.UseVisualStyleBackColor = $false
    $button.Font = New-Object Drawing.Font 'Segoe UI', 10, ([Drawing.FontStyle]::Bold)
    if ($primary) {
        $button.BackColor = [Drawing.ColorTranslator]::FromHtml('#f2b24a')
        $button.ForeColor = [Drawing.ColorTranslator]::FromHtml('#16191c')
        $button.FlatAppearance.BorderColor = [Drawing.ColorTranslator]::FromHtml('#ffcf7a')
    } else {
        $button.BackColor = [Drawing.ColorTranslator]::FromHtml('#263641')
        $button.ForeColor = [Drawing.ColorTranslator]::FromHtml('#eef2f3')
        $button.FlatAppearance.BorderColor = [Drawing.ColorTranslator]::FromHtml('#5d7682')
    }
}
$label = New-Object Windows.Forms.Label
$label.Text = 'Installs of The Witness found on this PC. Pick one (or Browse), then install or uninstall co-op.'
$label.SetBounds(26, 103, 700, 24)
$form.Controls.Add($label)
# Every install found gets a selectable card with its build check, including builds the mod does not support.
$cards = New-Object Windows.Forms.FlowLayoutPanel
$cards.SetBounds(26, 130, 702, 112)
$cards.Anchor = 'Top, Left, Right'; $cards.AutoScroll = $true
$cards.FlowDirection = 'TopDown'; $cards.WrapContents = $false
$form.Controls.Add($cards)
$path = New-Object Windows.Forms.TextBox
$path.SetBounds(26, 252, 470, 28)
$path.BackColor = [Drawing.ColorTranslator]::FromHtml('#243340')
$path.ForeColor = [Drawing.ColorTranslator]::FromHtml('#ffffff')
$path.BorderStyle = [Windows.Forms.BorderStyle]::FixedSingle
$form.Controls.Add($path)
$browse = New-Object Windows.Forms.Button
$browse.Text = 'Browse...'; $browse.SetBounds(505, 249, 108, 32)
Style-Button $browse $false
$form.Controls.Add($browse)
$status = New-Object Windows.Forms.Label
$status.Text = 'Close the game before installing. Your saves and co-op settings are kept on uninstall.'
$status.ForeColor = [Drawing.ColorTranslator]::FromHtml('#b2c2c9')
$status.SetBounds(26, 290, 700, 45)
$form.Controls.Add($status)
$install = New-Object Windows.Forms.Button
$install.Text = 'Install'; $install.SetBounds(430, 630, 143, 40)
$install.Anchor = 'Bottom, Right'
Style-Button $install $true
$form.Controls.Add($install)
$uninstall = New-Object Windows.Forms.Button
$uninstall.Text = 'Uninstall'; $uninstall.SetBounds(585, 630, 143, 40)
$uninstall.Anchor = 'Bottom, Right'
Style-Button $uninstall $false
$form.Controls.Add($uninstall)
$inventory = New-Object Windows.Forms.ListView
$inventory.View = 'Details'; $inventory.FullRowSelect = $true
$inventory.SetBounds(26, 390, 702, 130); $inventory.Anchor = 'Top, Bottom, Left, Right'
[void]$inventory.Columns.Add('Change', 230)
[void]$inventory.Columns.Add('Status', 120)
[void]$inventory.Columns.Add('Details', 800)
$form.Controls.Add($inventory)
$refresh = New-Object Windows.Forms.Button
$refresh.Text = 'Refresh'; $refresh.SetBounds(26, 630, 110, 40); $refresh.Anchor = 'Bottom, Left'
Style-Button $refresh $false
$form.Controls.Add($refresh)
$playExisting = New-Object Windows.Forms.Button
$playExisting.Name = 'PlayExisting'; $playExisting.Text = 'Play (launcher)'
$playExisting.SetBounds(26, 340, 160, 40); $playExisting.Enabled = $false
Style-Button $playExisting $true
$form.Controls.Add($playExisting)
# the three optional integrations (ticked by default, removed again by Uninstall)
$desktopOption = New-Object Windows.Forms.CheckBox
$desktopOption.Text = 'Desktop shortcut'
$hostingOption = New-Object Windows.Forms.CheckBox
$hostingOption.Text = 'Allow hosting through Windows Firewall (Private and Public; one administrator prompt)'
$inviteOption = New-Object Windows.Forms.CheckBox
$inviteOption.Text = 'Open invite links in the launcher (witness-coop://)'
$optionY = 530
foreach ($option in @($desktopOption, $hostingOption, $inviteOption)) {
    $option.Checked = $true; $option.SetBounds(26, $optionY, 702, 29); $option.AutoSize = $false
    $form.Controls.Add($option); $optionY += 31
}
function Start-WitnessLauncher($dir) {
    if (-not (Test-Path -LiteralPath (Join-Path $dir 'witness64_d3d11.exe') -PathType Leaf)) { throw 'Choose the folder containing witness64_d3d11.exe.' }
    if (-not (Test-Path -LiteralPath (Join-Path $dir 'witness_launcher.exe') -PathType Leaf)) { throw 'Install co-op first: witness_launcher.exe is not in this folder yet.' }
    Start-Process -FilePath (Join-Path $dir 'witness_launcher.exe') -WorkingDirectory $dir -ErrorAction Stop | Out-Null
}
function Play-Game($dir) {
    try { Start-WitnessLauncher $dir; $form.Close() }
    catch { [Windows.Forms.MessageBox]::Show($form, "Could not start the co-op launcher.`n`n$($_.Exception.Message)", 'The Witness Co-op', 'OK', 'Error') | Out-Null }
}
function Add-DesktopShortcut($dir) {
    try {
        New-WitnessDesktopShortcut $dir
        [Windows.Forms.MessageBox]::Show($form, 'The Witness Co-op shortcut added to your desktop.', 'The Witness Co-op', 'OK', 'Information') | Out-Null
    } catch { [Windows.Forms.MessageBox]::Show($form, "Could not create the shortcut.`n`n$($_.Exception.Message)", 'The Witness Co-op', 'OK', 'Error') | Out-Null }
}
$playExisting.Add_Click({ Play-Game ($path.Text.Trim().Trim('"')) })
function Refresh-Status {
    if ($script:busy -or $script:refreshing) { return }
    $script:refreshing = $true
    $inventory.BeginUpdate()
    try {
        $inventory.Items.Clear(); $install.Enabled = $false; $uninstall.Enabled = $false
        $install.Text = 'Install'
        $playExisting.Enabled = $false
        $status.Text = 'Choose a game folder to inspect its installation.'
        $dir = $path.Text.Trim().Trim('"')
        if (-not $dir -or -not (Test-Path -LiteralPath (Join-Path $dir 'witness64_d3d11.exe') -PathType Leaf)) { return }
        $playExisting.Enabled = Test-Path -LiteralPath (Join-Path $dir 'witness_launcher.exe') -PathType Leaf
        try {
            $snapshot = Get-WitnessInstallStatus $dir $PSScriptRoot
            foreach ($row in $snapshot.items) {
                $item = New-Object Windows.Forms.ListViewItem($row.item)
                [void]$item.SubItems.Add($row.status); [void]$item.SubItems.Add($row.detail)
                [void]$inventory.Items.Add($item)
            }
            $install.Enabled = [bool]$snapshot.actions.install
            $install.Text = if ($snapshot.actions.install) { $snapshot.actions.install } elseif ($snapshot.actions.blocked) { 'Needs attention' } else { 'Up to date' }
            $uninstall.Enabled = $snapshot.actions.uninstall
            # a current install can still gain the ticked integrations (Repair); unticked ones need nothing
            if ($snapshot.owner -ne 'vortex' -and $snapshot.build -eq 'current' -and -not $snapshot.actions.blocked -and -not $snapshot.actions.install -and
                (($desktopOption.Checked -and -not $snapshot.options.desktop_shortcut) -or
                 ($hostingOption.Checked -and -not $snapshot.options.allow_hosting) -or
                 ($inviteOption.Checked -and -not $snapshot.options.invite_links))) {
                $install.Enabled = $true; $install.Text = 'Repair'
            }
            if ($snapshot.build -ne 'current') { $status.Text = Format-WitnessBuildShort $snapshot.report }
            elseif ($snapshot.owner -eq 'vortex') {
                # not Setup's files: the mod manager updates and removes them; playing works as it is (J6)
                $install.Text = 'By Vortex'; $install.Enabled = $false; $uninstall.Enabled = $false
                $status.Text = 'Installed by Vortex: update or remove it there. Click Play to open the launcher.'
            }
            elseif ($snapshot.owner -eq 'manual') { $status.Text = 'Copied by hand. Setup will look after these files from now on.' }
            elseif ($snapshot.actions.blocked) { $status.Text = 'Conflicting or unreadable files need attention. Modified files are kept.' }
            elseif (-not $snapshot.actions.install) { $status.Text = 'Co-op is installed. Click Play to open the launcher: host a lobby or join a friend there.' }
            else { $status.Text = 'Supported build found. Install co-op below; the launcher then starts the game for you.' }
        } catch { $status.Text = 'Could not inspect this folder. No changes were made.' }
    } finally {
        $inventory.EndUpdate()
        $script:refreshing = $false
    }
}
$refresh.Add_Click({ Refresh-Status })
foreach ($option in @($desktopOption, $hostingOption, $inviteOption)) { $option.Add_CheckedChanged({ Refresh-Status }) }
$path.Add_TextChanged({ Refresh-Status })
$form.Add_Activated({ Refresh-Status })
$finish = New-Object Windows.Forms.Panel
$finish.SetBounds(0, 91, 760, 599)
$finish.Anchor = 'Top, Bottom, Left, Right'
$finishText = New-Object Windows.Forms.Label
$finishText.SetBounds(26, 35, 700, 100)
$finish.Controls.Add($finishText)
$close = New-Object Windows.Forms.Button
$close.Name = 'Close'; $close.Text = 'Close'; $close.SetBounds(490, 175, 143, 40)
Style-Button $close $false
$finish.Controls.Add($close)
$play = New-Object Windows.Forms.Button
$play.Name = 'Play'; $play.Text = 'Play (launcher)'; $play.SetBounds(318, 175, 160, 40)      # the last screen's primary button is Play (G4)
Style-Button $play $true
$script:installedDir = ''
$close.Add_Click({ $form.Close() })
$play.Add_Click({ Play-Game $script:installedDir })
function Show-Completion($remove) {
    foreach ($control in @($label, $cards, $path, $browse, $status, $install, $uninstall, $inventory, $refresh, $playExisting, $desktopOption, $hostingOption, $inviteOption)) { $form.Controls.Remove($control) }
    $finish.Controls.Remove($play)
    if ($remove) {
        $finishText.Text = 'Co-op has been uninstalled. The game''s own files are back; your saves and your co-op settings (in %LOCALAPPDATA%\witness-coop) were kept.'
    } else {
        $finishText.Text = 'Installation complete. Open the launcher to host a lobby (send your friend the invite) or to join one. The lobby starts the game for everyone.'
        $finish.Controls.Add($play)
        $form.AcceptButton = $play
    }
    $form.Controls.Add($finish)
    if ($remove) { $form.AcceptButton = $close }
}
if ($TestLayout) {
    foreach ($scale in @(1.0, 1.25, 1.5, 2.0)) {
        $factor = $scale / $(if ($script:previousScale) { $script:previousScale } else { 1.0 })
        $form.Scale((New-Object Drawing.SizeF($factor, $factor)))
        $script:previousScale = $scale
        $form.PerformLayout()
        foreach ($c in @($cards, $inventory, $refresh, $install, $uninstall, $playExisting, $desktopOption, $hostingOption, $inviteOption)) {
            if ($c.Left -lt 0 -or $c.Top -lt 0 -or $c.Right -gt $form.ClientSize.Width -or $c.Bottom -gt $form.ClientSize.Height) { throw 'Control outside setup client area' }
        }
        if ($cards.Bottom -gt $path.Top -or $playExisting.Bottom -gt $inventory.Top -or $inventory.Bottom -gt $desktopOption.Top -or $inviteOption.Bottom -gt $install.Top) { throw 'Cards or options overlap' }
        if ($inventory.Bottom -gt $install.Top -or $refresh.Right -gt $install.Left -or $install.Right -gt $uninstall.Left) { throw 'Setup controls overlap' }
    }
    Write-Output 'PASS: setup control geometry at 100, 125, 150 and 200 percent'
    $form.Dispose(); exit 0
}
if ($TestCompletion) {
    Show-Completion ($TestCompletion -eq 'uninstall')
    if (-not $finish.Controls.Contains($close) -or ($finish.Controls.Contains($play) -ne ($TestCompletion -eq 'install'))) { exit 1 }
    Write-Output "completion $TestCompletion OK"
    $form.Dispose()
    exit 0
}
if ($TestOwnerPath) {     # tools/test_setup_verbs.ps1: what Setup shows for a folder, read back from the controls
    $path.Text = $TestOwnerPath
    if ($TestNoOptions) { $desktopOption.Checked = $false; $hostingOption.Checked = $false; $inviteOption.Checked = $false }
    Refresh-Status
    Write-Output ("button={0}|enabled={1}|uninstall={2}|play={3}|status={4}" -f $install.Text, $install.Enabled, $uninstall.Enabled, $playExisting.Enabled, $status.Text)
    $form.Dispose(); exit 0
}
if ($TestActionsPath) {
    $path.Text = $TestActionsPath
    Refresh-Status
    if (-not $playExisting.Enabled) { throw 'Existing game actions disabled' }
    function Start-Process($FilePath, $WorkingDirectory, $ErrorAction) {
        $script:startedFile = $FilePath; $script:startedDir = $WorkingDirectory
    }
    Start-WitnessLauncher $TestActionsPath
    if ($script:startedFile -ne (Join-Path $TestActionsPath 'witness_launcher.exe') -or $script:startedDir -ne $TestActionsPath) { throw 'Wrong launch target' }
    New-WitnessDesktopShortcut $TestActionsPath $TestActionsPath
    New-WitnessDesktopShortcut $TestActionsPath $TestActionsPath
    $shell = New-Object -ComObject WScript.Shell
    $link = $shell.CreateShortcut((Join-Path $TestActionsPath 'The Witness Co-op.lnk'))
    if ($link.TargetPath -ne (Join-Path $TestActionsPath 'witness_launcher.exe') -or $link.WorkingDirectory -ne $TestActionsPath -or $link.IconLocation -ne ((Join-Path $TestActionsPath 'witness64_d3d11.exe') + ',0')) { throw 'Wrong shortcut target, working directory or icon' }
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($link)
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell)
    $path.Text = Join-Path $TestActionsPath 'missing'
    Refresh-Status
    if ($playExisting.Enabled) { throw 'Invalid folder actions enabled' }
    $rejected = $false
    try { New-WitnessDesktopShortcut $path.Text $TestActionsPath } catch { $rejected = $true }
    if (-not $rejected) { throw 'Invalid shortcut accepted' }
    Write-Output 'PASS: existing game actions and desktop shortcut'
    $form.Dispose(); exit 0
}
$browse.Add_Click({
    $dialog = New-Object Windows.Forms.FolderBrowserDialog
    $dialog.Description = 'Choose the folder containing witness64_d3d11.exe'
    if ($dialog.ShowDialog($form) -eq 'OK') { $path.Text = $dialog.SelectedPath }
    $dialog.Dispose()
})

function Can-WriteGame($dir) {
    # Probe both directories and existing files; do not modify any existing file.
    try {
        if (-not (Test-Path -LiteralPath $dir -PathType Container)) { return $false }
        foreach ($folder in @($dir, (Join-Path $dir 'witness_models'))) {
            if (-not (Test-Path $folder)) { continue }
            $probe = Join-Path $folder ([IO.Path]::GetRandomFileName())
            $stream = [IO.File]::Open($probe, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write)
            $stream.Dispose(); [IO.File]::Delete($probe)
        }
        foreach ($relative in $script:WitnessManaged + $script:WitnessChain + $script:WitnessLegacy) {
            $file = Join-Path $dir $relative
            if (Test-Path $file) {
                $stream = [IO.File]::Open($file, [IO.FileMode]::Open, [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
                $stream.Dispose()
            }
        }
        return $true
    } catch { return $false }
}
if ($ProbePath) {
    if (Can-WriteGame $ProbePath) { Write-Output 'writable'; exit 0 }
    Write-Output 'not writable'; exit 1
}

# One backend run: the game folder (and the firewall rule) elevated when needed, then the player's own shortcut and link handler in this session.
function Run-Backend($dir, $remove, $elevate, [string]$extra) {
    $arguments = '-NoProfile -ExecutionPolicy Bypass -File "{0}" -GameDir "{1}" -Yes {2}' -f $backend, $dir, $extra
    if ($remove) { $arguments += ' -Uninstall' }
    if ($elevate) {
        $process = Start-Process -FilePath 'powershell.exe' -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden -Verb RunAs
    } else {
        $process = Invoke-CoopHiddenProcess 'powershell.exe' $arguments
    }
    if ($process.ExitCode -eq 3) { throw 'This copy of The Witness is not the supported build. Nothing was changed.' }
    if ($process.ExitCode -eq 4) { throw 'The Witness (or its co-op launcher) is running from that folder. Close it and try again.' }
    if ($process.ExitCode -ne 0) { throw $(if ($remove) { 'Setup could not remove co-op. Check that the game is closed and the folder is writable.' } else { 'The installer could not complete this step. Check that the game is closed and the folder is writable. If a mod manager installed the files, update or remove them there.' }) }
}
$script:busy = $false
function Perform($remove) {
    if ($script:busy) { return }
    $dir = $path.Text.Trim().Trim('"')
    if (-not $dir -or -not (Test-Path (Join-Path $dir 'witness64_d3d11.exe') -PathType Leaf)) {
        [Windows.Forms.MessageBox]::Show($form, 'Choose the folder containing witness64_d3d11.exe.', 'The Witness Co-op', 'OK', 'Warning') | Out-Null
        return
    }
    try {
        $preflight = Get-WitnessInstallStatus $dir $PSScriptRoot
        if ($preflight.build -ne 'current') { throw (Format-WitnessBuildRefusal $preflight.report) }
        if ($preflight.owner -eq 'vortex') { throw 'Installed by Vortex: update or remove it there. Setup leaves the mod manager''s files alone.' }
        if ($preflight.actions.blocked) { throw 'Conflicting or unreadable files must be resolved before setup can continue.' }
        $script:busy = $true
        $form.ControlBox = $false
        foreach ($control in @($path, $browse, $cards, $install, $uninstall, $refresh, $playExisting, $desktopOption, $hostingOption, $inviteOption)) { $control.Enabled = $false }
        $status.ForeColor = [Drawing.Color]::White
        $status.Text = 'Working...'; $form.Refresh()
        $firewall = $false
        foreach ($program in Get-CoopFirewallPrograms $dir) { if (@(Get-CoopFirewallRules $program).Count) { $firewall = $true } }
        $wantFirewall = (-not $remove -and $hostingOption.Checked) -or ($remove -and $firewall)
        $needsAdmin = (-not (Can-WriteGame $dir)) -or ($wantFirewall -and -not (Test-CoopAdmin))
        if ($needsAdmin) { $status.Text = 'Windows will ask for administrator permission once.'; $form.Refresh() }
        $files = '-GameFilesOnly'; if (-not $remove -and $hostingOption.Checked) { $files += ' -AllowHosting' }
        Run-Backend $dir $remove $needsAdmin $files
        $user = '-UserOptionsOnly'
        if (-not $remove) { if ($desktopOption.Checked) { $user += ' -DesktopShortcut' }; if ($inviteOption.Checked) { $user += ' -InviteLinks' } }
        Run-Backend $dir $remove $false $user
        $script:installedDir = $dir
        Show-Completion $remove
    } catch {
        $status.Text = 'Setup did not complete. You can try again.'
        [Windows.Forms.MessageBox]::Show($form, $_.Exception.Message, 'The Witness Co-op', 'OK', 'Error') | Out-Null
    } finally {
        foreach ($control in @($path, $browse, $cards, $install, $uninstall, $refresh, $playExisting, $desktopOption, $hostingOption, $inviteOption)) { $control.Enabled = $true }
        $form.ControlBox = $true
        $script:busy = $false
        Refresh-Status
        $form.Activate()
    }
}
$install.Add_Click({ Perform $false })
$uninstall.Add_Click({ Perform $true })
function Refresh-Cards {
    $cards.Controls.Clear()
    $selected = $null; $first = $null
    foreach ($entry in @(Get-CoopInstallCards $PSScriptRoot)) {
        $card = New-Object Windows.Forms.RadioButton
        $card.Appearance = 'Button'; $card.AutoSize = $false; $card.Size = New-Object Drawing.Size(665, 58)
        $card.FlatStyle = 'Flat'; $card.TextAlign = 'MiddleLeft'
        $owner = $script:CoopOwnerText[$entry.state.owner]
        $buildText = if ($entry.supported) { 'Steam build 11370412: supported' } elseif ($entry.build -eq 'conflict') { 'a build the mod does not support (co-op stays off)' } else { 'build could not be read' }
        $storeText = if ($entry.store -and $entry.store -ne 'unknown') { "$($entry.store)  /  " } else { '' }
        $card.Text = "$storeText$buildText  /  $owner`r`n$($entry.path)"; $card.Tag = $entry.path
        $card.Add_CheckedChanged({ if ($this.Checked) { $path.Text = [string]$this.Tag } })
        $cards.Controls.Add($card)
        if (-not $first) { $first = $card }
        if ($entry.selected) { $selected = $card }
    }
    if (-not $selected) { $selected = $first }
    if ($selected) { $selected.Checked = $true; $path.Text = [string]$selected.Tag }
}
try { Refresh-Cards } catch { $status.Text = 'Some installs could not be inspected. Browse to your game folder.' }
Refresh-Status
[void]$form.ShowDialog()
$form.Dispose()
