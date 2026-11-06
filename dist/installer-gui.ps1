# User-facing installer for The Witness co-op. Only the game-folder operation is ever elevated (and only when the folder needs it).
# The window (owner, 2026-11-06: "laggy, glitchy, half loaded when it opens, flickery"): every install's state is read once, before the window is shown, and
# kept; picking a card, ticking an option or typing a folder (after a pause) reuses it, and only Refresh, Browse to a new folder and an install or uninstall
# read the folder again. Nothing is read when the window gets the focus. Everything is double-buffered and drawn in one dark style.
param([string]$ProbePath, [ValidateSet('', 'install', 'uninstall')][string]$TestCompletion = '', [switch]$TestLayout, [string]$TestActionsPath,
      [string]$TestOwnerPath, [switch]$TestNoOptions)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()
$backend = Join-Path $PSScriptRoot 'witness-coop.ps1'
. (Join-Path $PSScriptRoot 'install-state.ps1')
function Hex($h) { return [Drawing.ColorTranslator]::FromHtml($h) }
$C = @{
    bg = Hex '#11181d'; header = Hex '#0c1216'; surface = Hex '#18222a'; surfaceAlt = Hex '#1b262e'; surface2 = Hex '#22303a'; line = Hex '#2a3943'; lineHot = Hex '#4a5f6b'
    text = Hex '#e8eef0'; muted = Hex '#8fa1aa'; accent = Hex '#f2b24a'; accentHot = Hex '#ffc766'; accentDown = Hex '#d99a35'; ink = Hex '#15191c'
    ok = Hex '#5cd68c'; warn = Hex '#f2b24a'; bad = Hex '#ff7a6b'; cardSel = Hex '#1f2d37'; cardHot = Hex '#1c2830'
}
$F = @{
    body = New-Object Drawing.Font 'Segoe UI', 10; small = New-Object Drawing.Font 'Segoe UI', 9; smallBold = New-Object Drawing.Font 'Segoe UI Semibold', 9
    title = New-Object Drawing.Font 'Segoe UI Semibold', 10.5; button = New-Object Drawing.Font 'Segoe UI Semibold', 10; brand = New-Object Drawing.Font 'Segoe UI', 20, ([Drawing.FontStyle]::Bold)
    big = New-Object Drawing.Font 'Segoe UI Semibold', 16
}
function Set-DoubleBuffered($control) {      # (a protected property: set through reflection; no flicker when a control repaints)
    $p = $control.GetType().GetProperty('DoubleBuffered', [Reflection.BindingFlags]'NonPublic,Instance'); if ($p) { $p.SetValue($control, $true, $null) }
}
function New-RoundRect([single]$x, [single]$y, [single]$w, [single]$h, [single]$r) {
    $p = New-Object Drawing.Drawing2D.GraphicsPath; $d = 2 * $r
    $p.AddArc($x, $y, $d, $d, 180, 90); $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90); $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90); $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure(); return $p
}
$form = New-Object Windows.Forms.Form
$form.Text = 'The Witness Co-op Setup'
$form.ClientSize = New-Object Drawing.Size(760, 690)
$form.FormBorderStyle = 'Sizable'
$form.MinimumSize = New-Object Drawing.Size(776, 729)
$form.AutoScaleMode = [Windows.Forms.AutoScaleMode]::Dpi
$form.MaximizeBox = $false
$form.StartPosition = 'CenterScreen'
$form.BackColor = $C.bg
$form.ForeColor = $C.text
$form.Font = $F.body
Set-DoubleBuffered $form
$exe = Join-Path $PSScriptRoot 'Witness-Coop-Setup.exe'
if (Test-Path $exe) { try { $form.Icon = [Drawing.Icon]::ExtractAssociatedIcon($exe) } catch { } }
$form.SuspendLayout()
# ---- the header: the name, "Setup", a thin line in the game's yellow
$header = New-Object Windows.Forms.Panel
$header.BackColor = $C.header
$header.SetBounds(0, 0, 760, 70)
$header.Anchor = 'Top, Left, Right'
$header.Add_Paint({ param($s, $e) $b = New-Object Drawing.SolidBrush $C.accent; $e.Graphics.FillRectangle($b, 0, $s.ClientSize.Height - 2, $s.ClientSize.Width, 2); $b.Dispose() })
$brand = New-Object Windows.Forms.Label
$brand.Text = 'THE WITNESS  CO-OP'; $brand.Font = $F.brand; $brand.ForeColor = $C.accent; $brand.BackColor = $C.header
$brand.SetBounds(26, 8, 440, 36)
$header.Controls.Add($brand)
$subtitle = New-Object Windows.Forms.Label
$subtitle.Text = 'Setup  -  play the island together'; $subtitle.Font = $F.small; $subtitle.ForeColor = $C.muted; $subtitle.BackColor = $C.header
$subtitle.SetBounds(29, 42, 420, 20)
$header.Controls.Add($subtitle)
$form.Controls.Add($header)
# ---- buttons: flat, one primary (the game's yellow) per screen, hover and press shades, a quiet look when disabled
function Style-Button($button, $primary) {
    $button.FlatStyle = [Windows.Forms.FlatStyle]::Flat
    $button.UseVisualStyleBackColor = $false
    $button.Font = $F.button
    $button.Cursor = [Windows.Forms.Cursors]::Hand
    $button.Tag = [bool]$primary
    Paint-Button $button
}
function Paint-Button($button) {
    $primary = [bool]$button.Tag
    if (-not $button.Enabled) {
        $button.BackColor = $C.surface; $button.ForeColor = $C.muted; $button.FlatAppearance.BorderSize = 1; $button.FlatAppearance.BorderColor = $C.line
    } elseif ($primary) {
        $button.BackColor = $C.accent; $button.ForeColor = $C.ink; $button.FlatAppearance.BorderSize = 0
        $button.FlatAppearance.MouseOverBackColor = $C.accentHot; $button.FlatAppearance.MouseDownBackColor = $C.accentDown
    } else {
        $button.BackColor = $C.surface2; $button.ForeColor = $C.text; $button.FlatAppearance.BorderSize = 1; $button.FlatAppearance.BorderColor = $C.lineHot
        $button.FlatAppearance.MouseOverBackColor = Hex '#2b3b46'; $button.FlatAppearance.MouseDownBackColor = $C.surface
    }
}
function Set-Button($button, [bool]$enabled, [bool]$primary, [string]$text) {      # only what changed is touched (no repaint for nothing)
    if ($text -and $button.Text -ne $text) { $button.Text = $text }
    if ($button.Enabled -ne $enabled -or [bool]$button.Tag -ne $primary) { $button.Enabled = $enabled; $button.Tag = $primary; Paint-Button $button }
}
function Set-Text($control, [string]$text, $color) {
    if ($control.Text -ne $text) { $control.Text = $text }
    if ($color -and $control.ForeColor -ne $color) { $control.ForeColor = $color }
}
$label = New-Object Windows.Forms.Label
$label.Text = 'Your copy of The Witness'; $label.Font = $F.title
$label.SetBounds(26, 82, 500, 24)
$form.Controls.Add($label)
# Every install found gets a card with its build check, including builds the mod does not support (radio buttons drawn as cards: arrows and Tab still work).
$cards = New-Object Windows.Forms.FlowLayoutPanel
$cards.SetBounds(28, 110, 704, 72)
$cards.Anchor = 'Top, Left, Right'; $cards.AutoScroll = $true
$cards.FlowDirection = 'TopDown'; $cards.WrapContents = $false
$cards.BackColor = $C.bg
Set-DoubleBuffered $cards
$form.Controls.Add($cards)
$path = New-Object Windows.Forms.TextBox
$path.SetBounds(28, 192, 584, 30)
$path.BackColor = $C.surface
$path.ForeColor = $C.text
$path.BorderStyle = [Windows.Forms.BorderStyle]::FixedSingle
$form.Controls.Add($path)
$browse = New-Object Windows.Forms.Button
$browse.Text = 'Browse...'; $browse.SetBounds(622, 190, 110, 32)
Style-Button $browse $false
$form.Controls.Add($browse)
$status = New-Object Windows.Forms.Label
$status.Text = 'Close the game before installing. Your saves and co-op settings are kept on uninstall.'
$status.ForeColor = $C.muted
$status.SetBounds(26, 232, 706, 24)
$form.Controls.Add($status)
$changesLabel = New-Object Windows.Forms.Label
$changesLabel.Text = 'WHAT SETUP CHECKS'; $changesLabel.Font = $F.smallBold; $changesLabel.ForeColor = $C.muted
$changesLabel.SetBounds(26, 262, 400, 20)
$form.Controls.Add($changesLabel)
# The files Setup looks after and what it found: a dark list, the state coloured (green fine, yellow to do, red needs a look), the whole text as a tooltip.
$inventory = New-Object Windows.Forms.ListView
$inventory.View = 'Details'; $inventory.FullRowSelect = $true; $inventory.OwnerDraw = $true; $inventory.ShowItemToolTips = $true
$inventory.HeaderStyle = 'Nonclickable'; $inventory.BorderStyle = 'None'; $inventory.MultiSelect = $false
$inventory.BackColor = $C.surface; $inventory.ForeColor = $C.text; $inventory.Font = $F.small
$inventory.SetBounds(28, 286, 704, 236); $inventory.Anchor = 'Top, Bottom, Left, Right'
$rowHeight = New-Object Windows.Forms.ImageList; $rowHeight.ImageSize = New-Object Drawing.Size(1, 21); $inventory.SmallImageList = $rowHeight      # (taller rows)
[void]$inventory.Columns.Add('Item', 230)
[void]$inventory.Columns.Add('State', 130)
[void]$inventory.Columns.Add('Details', 340)
Set-DoubleBuffered $inventory
function Fit-Columns { $w = $inventory.ClientSize.Width - $inventory.Columns[0].Width - $inventory.Columns[1].Width; if ($w -gt 60 -and $inventory.Columns[2].Width -ne $w) { $inventory.Columns[2].Width = $w } }
$inventory.Add_Resize({ Fit-Columns })
function State-Color([string]$s) {
    switch -regex ($s) {
        '^(current|supported build|present|installed)$' { return $C.ok }
        '^(missing|outdated|unmanaged)$' { return $C.warn }
        '^(conflict|modified|unknown|unsupported build)$' { return $C.bad }
        default { return $C.muted }
    }
}
$inventory.Add_DrawColumnHeader({ param($s, $e)
    $b = New-Object Drawing.SolidBrush $C.surface2; $e.Graphics.FillRectangle($b, $e.Bounds); $b.Dispose()
    $r = $e.Bounds; $r.X += 10; $r.Width -= 12
    [Windows.Forms.TextRenderer]::DrawText($e.Graphics, $e.Header.Text, $F.smallBold, $r, $C.muted, [Windows.Forms.TextFormatFlags]'Left, VerticalCenter, EndEllipsis, NoPrefix')
})
$inventory.Add_DrawItem({ param($s, $e) })      # (each cell is drawn below)
$inventory.Add_DrawSubItem({ param($s, $e)
    $back = if ($e.Item.Selected) { $C.surface2 } elseif ($e.ItemIndex % 2) { $C.surfaceAlt } else { $C.surface }
    $b = New-Object Drawing.SolidBrush $back; $e.Graphics.FillRectangle($b, $e.Bounds); $b.Dispose()
    $color = if ($e.ColumnIndex -eq 1) { State-Color $e.SubItem.Text } elseif ($e.ColumnIndex -eq 2) { $C.muted } else { $C.text }
    $r = $e.Bounds; $r.X += 10; $r.Width -= 14
    [Windows.Forms.TextRenderer]::DrawText($e.Graphics, $e.SubItem.Text, $F.small, $r, $color, [Windows.Forms.TextFormatFlags]'Left, VerticalCenter, EndEllipsis, NoPrefix, SingleLine')
})
$form.Controls.Add($inventory)
# the three optional integrations (ticked by default, removed again by Uninstall)
$desktopOption = New-Object Windows.Forms.CheckBox
$desktopOption.Text = 'Desktop shortcut'
$hostingOption = New-Object Windows.Forms.CheckBox
$hostingOption.Text = 'Allow hosting through Windows Firewall (Private and Public; one administrator prompt)'
$inviteOption = New-Object Windows.Forms.CheckBox
$inviteOption.Text = 'Open invite links in the launcher (witness-coop://)'
$optionY = 536
foreach ($option in @($desktopOption, $hostingOption, $inviteOption)) {
    $option.Checked = $true; $option.SetBounds(28, $optionY, 704, 25); $option.AutoSize = $false; $option.Anchor = 'Bottom, Left, Right'
    $option.FlatStyle = 'Flat'; $option.FlatAppearance.BorderColor = $C.lineHot; $option.FlatAppearance.CheckedBackColor = $C.surface2; $option.FlatAppearance.MouseOverBackColor = $C.surface2
    $option.ForeColor = $C.text; $option.Cursor = [Windows.Forms.Cursors]::Hand
    $form.Controls.Add($option); $optionY += 27
}
# ---- the bottom bar: Refresh on the left; Play, Install (or Update, Repair) and Uninstall on the right; the one that fits the install is the yellow one
$bar = New-Object Windows.Forms.Panel
$bar.BackColor = $C.line; $bar.SetBounds(0, 624, 760, 1); $bar.Anchor = 'Bottom, Left, Right'
$form.Controls.Add($bar)
$refresh = New-Object Windows.Forms.Button
$refresh.Text = 'Refresh'; $refresh.SetBounds(28, 636, 104, 40); $refresh.Anchor = 'Bottom, Left'
Style-Button $refresh $false
$form.Controls.Add($refresh)
$playExisting = New-Object Windows.Forms.Button
$playExisting.Name = 'PlayExisting'; $playExisting.Text = 'Play (launcher)'
$playExisting.SetBounds(332, 636, 140, 40); $playExisting.Anchor = 'Bottom, Right'; $playExisting.Enabled = $false
Style-Button $playExisting $false
$form.Controls.Add($playExisting)
$install = New-Object Windows.Forms.Button
$install.Text = 'Install'; $install.SetBounds(482, 636, 124, 40)
$install.Anchor = 'Bottom, Right'
Style-Button $install $true
$form.Controls.Add($install)
$uninstall = New-Object Windows.Forms.Button
$uninstall.Text = 'Uninstall'; $uninstall.SetBounds(616, 636, 116, 40)
$uninstall.Anchor = 'Bottom, Right'
Style-Button $uninstall $false
$form.Controls.Add($uninstall)
$form.ResumeLayout()
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
# ---- what an install is: read once per folder (the cards' own reads fill this too) and kept until Refresh or an install or uninstall
$script:snapshots = @{}
function Get-Snapshot($dir, [bool]$fresh) {
    $key = $dir.ToLowerInvariant()
    if (-not $fresh -and $script:snapshots.ContainsKey($key)) { return $script:snapshots[$key] }
    $snapshot = Get-WitnessInstallStatus $dir $PSScriptRoot; $script:snapshots[$key] = $snapshot; return $snapshot
}
$script:inventoryKey = $null
function Show-Inventory($rows) {      # the list is rebuilt only when what it shows changed
    $key = (@($rows | ForEach-Object { "$($_.item)|$($_.status)|$($_.detail)" }) -join "`n")
    if ($key -eq $script:inventoryKey) { return }
    $script:inventoryKey = $key
    $inventory.BeginUpdate()
    try {
        $inventory.Items.Clear()
        foreach ($row in $rows) {
            $item = New-Object Windows.Forms.ListViewItem($row.item)
            [void]$item.SubItems.Add($row.status); [void]$item.SubItems.Add($row.detail); $item.ToolTipText = "$($row.item): $($row.status)`n$($row.detail)"
            [void]$inventory.Items.Add($item)
        }
    } finally { $inventory.EndUpdate() }
}
# the buttons on the right of the bar sit together from the right edge, whichever are shown
function Layout-Buttons {
    $k = $browse.Height / 32.0; $x = $form.ClientSize.Width - [int](28 * $k)
    foreach ($b in @($uninstall, $install, $playExisting)) {      # (.Visible reads false before the window is shown: Install's own flag says whether it is meant to be)
        if ($b -eq $install -and -not $script:installShown) { continue }
        $x -= $b.Width; if ($b.Left -ne $x) { $b.Left = $x }; $x -= [int](10 * $k)
    }
}
# the body under the cards follows their number (one install: no empty space), and the list takes what is left above the options
function Layout-Body {
    $k = $browse.Height / 32.0; $n = [Math]::Max(1, [Math]::Min(3, $cards.Controls.Count))
    $each = if ($cards.Controls.Count) { $cards.Controls[0].Height + $cards.Controls[0].Margin.Bottom } else { [int](70 * $k) }
    $cards.Height = $n * $each + [int](2 * $k)
    $y = $cards.Bottom + [int](8 * $k)
    $browse.Top = $y; $path.Top = $y + [int](2 * $k)
    $status.Top = $browse.Bottom + [int](10 * $k)
    $changesLabel.Top = $status.Bottom + [int](6 * $k)
    $inventory.Top = $changesLabel.Bottom + [int](4 * $k); $inventory.Height = [Math]::Max([int](80 * $k), $desktopOption.Top - [int](14 * $k) - $inventory.Top)
}
function Refresh-Status([switch]$Fresh) {
    if ($script:busy -or $script:refreshing) { return }
    $script:refreshing = $true
    try {
        # everything is worked out first and put on screen at once (no cleared list, no "Install" that becomes "Up to date" a moment later)
        $rows = @(); $canInstall = $false; $installText = 'Install'; $canUninstall = $false; $text = 'Choose a game folder to inspect its installation.'; $color = $C.muted
        $dir = $path.Text.Trim().Trim('"')
        $valid = $dir -and (Test-Path -LiteralPath (Join-Path $dir 'witness64_d3d11.exe') -PathType Leaf)
        $canPlay = $valid -and (Test-Path -LiteralPath (Join-Path $dir 'witness_launcher.exe') -PathType Leaf)
        if ($valid) {
            try {
                $snapshot = Get-Snapshot $dir ([bool]$Fresh)
                $rows = @($snapshot.items)
                $canInstall = [bool]$snapshot.actions.install
                $installText = if ($snapshot.actions.install) { $snapshot.actions.install } elseif ($snapshot.actions.blocked) { 'Needs attention' } else { 'Up to date' }
                $canUninstall = [bool]$snapshot.actions.uninstall
                # a current install can still gain the ticked integrations (Repair); unticked ones need nothing
                if ($snapshot.owner -ne 'vortex' -and $snapshot.build -eq 'current' -and -not $snapshot.actions.blocked -and -not $snapshot.actions.install -and
                    (($desktopOption.Checked -and -not $snapshot.options.desktop_shortcut) -or
                     ($hostingOption.Checked -and -not $snapshot.options.allow_hosting) -or
                     ($inviteOption.Checked -and -not $snapshot.options.invite_links))) {
                    $canInstall = $true; $installText = 'Repair'
                }
                if ($snapshot.build -ne 'current') { $text = Format-WitnessBuildShort $snapshot.report; $color = $C.bad }
                elseif ($snapshot.owner -eq 'vortex') {
                    # not Setup's files: the mod manager updates and removes them; playing works as it is (J6)
                    $installText = 'By Vortex'; $canInstall = $false; $canUninstall = $false
                    $text = 'Installed by Vortex: update or remove it there. Click Play to open the launcher.'; $color = $C.warn
                }
                elseif ($snapshot.owner -eq 'manual') { $text = 'Copied by hand. Setup will look after these files from now on.'; $color = $C.warn }
                elseif ($snapshot.actions.blocked) { $text = 'Conflicting or unreadable files need attention. Modified files are kept.'; $color = $C.bad }
                elseif (-not $snapshot.actions.install) { $text = 'Co-op is installed. Click Play to open the launcher: host a lobby or join a friend there.'; $color = $C.ok }
                else { $text = 'Supported build found. Install co-op below; the launcher then starts the game for you.'; $color = $C.text }
            } catch { $text = 'Could not inspect this folder. No changes were made.'; $color = $C.bad; $rows = @() }
        }
        $form.SuspendLayout()
        Show-Inventory $rows
        Set-Text $status $text $color
        # the yellow button is the next step: Install / Update / Repair when there is one, else Play
        Set-Button $install $canInstall $canInstall $installText
        $script:installShown = $installText -ne 'Up to date'; $install.Visible = $script:installShown      # (nothing to do: no dimmed button; the green line says it is installed)
        Layout-Buttons
        $k = $browse.Height / 32.0; $need = [Windows.Forms.TextRenderer]::MeasureText($status.Text, $status.Font, (New-Object Drawing.Size($status.Width, 0)), [Windows.Forms.TextFormatFlags]'WordBreak').Height + [int](4 * $k)
        if ($status.Height -ne $need) { $status.Height = $need; Layout-Body }      # (one line of status takes one line)
        Set-Button $playExisting ([bool]$canPlay) (-not $canInstall -and [bool]$canPlay) ''
        Set-Button $uninstall $canUninstall $false ''
        $form.ResumeLayout()
    } finally {
        $script:refreshing = $false
    }
}
$refresh.Add_Click({ $script:snapshots = @{}; Refresh-Cards; Refresh-Status })      # (the cards read every install again; a browsed folder is read again too)
foreach ($option in @($desktopOption, $hostingOption, $inviteOption)) { $option.Add_CheckedChanged({ Refresh-Status }) }      # (the kept state: instant)
# a typed folder is read after a pause in the typing, not on every key
$typing = New-Object Windows.Forms.Timer; $typing.Interval = 450
$typing.Add_Tick({ $typing.Stop(); Refresh-Status })
$path.Add_TextChanged({ if (-not $script:settingPath) { $typing.Stop(); $typing.Start() } })
function Set-Path([string]$dir) { $script:settingPath = $true; try { $path.Text = $dir } finally { $script:settingPath = $false }; $typing.Stop(); Refresh-Status }
$finish = New-Object Windows.Forms.Panel
$finish.SetBounds(0, 70, 760, 620)
$finish.Anchor = 'Top, Bottom, Left, Right'
$finishTitle = New-Object Windows.Forms.Label
$finishTitle.Font = $F.big; $finishTitle.ForeColor = $C.accent
$finishTitle.SetBounds(26, 40, 700, 36)
$finish.Controls.Add($finishTitle)
$finishText = New-Object Windows.Forms.Label
$finishText.SetBounds(28, 86, 700, 80)
$finish.Controls.Add($finishText)
$close = New-Object Windows.Forms.Button
$close.Name = 'Close'; $close.Text = 'Close'; $close.SetBounds(210, 180, 120, 42)
Style-Button $close $false
$finish.Controls.Add($close)
$play = New-Object Windows.Forms.Button
$play.Name = 'Play'; $play.Text = 'Play (launcher)'; $play.SetBounds(28, 180, 170, 42)      # the last screen's primary button is Play (G4)
Style-Button $play $true
$script:installedDir = ''
$close.Add_Click({ $form.Close() })
$play.Add_Click({ Play-Game $script:installedDir })
function Show-Completion($remove) {
    $form.SuspendLayout()
    foreach ($control in @($label, $cards, $path, $browse, $status, $changesLabel, $install, $uninstall, $inventory, $refresh, $playExisting, $desktopOption, $hostingOption, $inviteOption, $bar)) { $form.Controls.Remove($control) }
    $finish.Controls.Remove($play)
    if ($remove) {
        $finishTitle.Text = 'Co-op removed'
        $finishText.Text = 'Co-op has been uninstalled. The game''s own files are back; your saves and your co-op settings (in %LOCALAPPDATA%\witness-coop) were kept.'
        $close.Left = 28; Paint-Button $close
    } else {
        $finishTitle.Text = 'Co-op is installed'
        $finishText.Text = 'Installation complete. Open the launcher to host a lobby (send your friend the invite) or to join one. The lobby starts the game for everyone.'
        $finish.Controls.Add($play)
        $form.AcceptButton = $play
    }
    $form.Controls.Add($finish)
    if ($remove) { $form.AcceptButton = $close }
    $form.ResumeLayout()
}
if ($TestLayout) {
    foreach ($scale in @(1.0, 1.25, 1.5, 2.0)) {
        $factor = $scale / $(if ($script:previousScale) { $script:previousScale } else { 1.0 })
        $form.Scale((New-Object Drawing.SizeF($factor, $factor)))
        $script:previousScale = $scale
        $form.PerformLayout()
        foreach ($c in @($cards, $inventory, $refresh, $install, $uninstall, $playExisting, $desktopOption, $hostingOption, $inviteOption, $path, $browse, $status)) {
            if ($c.Left -lt 0 -or $c.Top -lt 0 -or $c.Right -gt $form.ClientSize.Width -or $c.Bottom -gt $form.ClientSize.Height) { throw 'Control outside setup client area' }
        }
        if ($cards.Bottom -gt $path.Top -or $path.Bottom -gt $status.Top -or $status.Bottom -gt $inventory.Top -or $inventory.Bottom -gt $desktopOption.Top -or $inviteOption.Bottom -gt $bar.Top) { throw 'Cards, list or options overlap' }
        if ($bar.Bottom -gt $install.Top -or $refresh.Right -gt $playExisting.Left -or $playExisting.Right -gt $install.Left -or $install.Right -gt $uninstall.Left) { throw 'Setup controls overlap' }
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
    if ($dialog.ShowDialog($form) -eq 'OK') { Set-Path $dialog.SelectedPath }
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
        foreach ($control in @($path, $browse, $cards, $install, $uninstall, $refresh, $playExisting, $desktopOption, $hostingOption, $inviteOption)) { $control.Enabled = $false; if ($control -is [Windows.Forms.Button]) { Paint-Button $control } }
        Set-Text $status 'Working...' $C.text; $form.Refresh()
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
        $script:installedDir = $dir; $script:done = $true
        Show-Completion $remove
    } catch {
        Set-Text $status 'Setup did not complete. You can try again.' $C.bad
        [Windows.Forms.MessageBox]::Show($form, $_.Exception.Message, 'The Witness Co-op', 'OK', 'Error') | Out-Null
    } finally {
        foreach ($control in @($path, $browse, $cards, $install, $uninstall, $refresh, $playExisting, $desktopOption, $hostingOption, $inviteOption)) { $control.Enabled = $true; if ($control -is [Windows.Forms.Button]) { Paint-Button $control } }
        $form.ControlBox = $true
        $script:busy = $false
        if (-not $script:done) { $script:snapshots = @{}; Refresh-Cards; Refresh-Status }      # (the folder changed or may have: read again)
        $form.Activate()
    }
}
$install.Add_Click({ Perform $false })
$uninstall.Add_Click({ Perform $true })
# ---- the cards: one per install, drawn here (the stock toggle-button look had a grey-blue highlight and an odd hover). A dot (green: a supported build,
# yellow: not), the store and the build, whether co-op is in it, the folder; the chosen card has a yellow edge, the card under the pointer a lighter one.
$script:hotCard = $null
$ownerShort = @{ setup = 'Co-op installed'; vortex = 'Co-op by Vortex'; manual = 'Co-op copied by hand'; none = 'Co-op not installed' }
$cardPaint = {
    param($s, $e)
    $g = $e.Graphics; $g.SmoothingMode = 'AntiAlias'
    $w = $s.ClientSize.Width; $h = $s.ClientSize.Height; $g.Clear($C.bg); $k = $h / 62.0      # (positions follow the card's size: Windows' display scaling)
    $entry = $s.Tag; $sel = $s.Checked; $hot = [object]::ReferenceEquals($script:hotCard, $s)
    $shape = New-RoundRect 0.5 0.5 ($w - 1.5) ($h - 1.5) (8 * $k)
    $fill = New-Object Drawing.SolidBrush $(if ($sel) { $C.cardSel } elseif ($hot) { $C.cardHot } else { $C.surface })
    $edge = New-Object Drawing.Pen $(if ($sel) { $C.accent } elseif ($hot) { $C.lineHot } else { $C.line }), $(if ($sel) { 1.6 } else { 1 })
    $g.FillPath($fill, $shape); $g.DrawPath($edge, $shape); $fill.Dispose(); $edge.Dispose(); $shape.Dispose()
    $dot = New-Object Drawing.SolidBrush $(if ($entry.supported) { $C.ok } else { $C.warn }); $g.FillEllipse($dot, (16 * $k), (17 * $k), (10 * $k), (10 * $k)); $dot.Dispose()
    $store = if ($entry.store -and $entry.store -ne 'unknown') { $entry.store } else { 'The Witness' }
    $build = if ($entry.supported) { 'supported build' } elseif ($entry.build -eq 'conflict') { 'a build co-op does not support' } else { 'build not recognised' }
    $chip = $ownerShort[[string]$entry.state.owner]; if (-not $chip) { $chip = '' }
    $chipColor = if ($entry.state.owner -eq 'setup') { $C.ok } elseif ($entry.state.owner -eq 'none') { $C.muted } else { $C.warn }
    $flags = [Windows.Forms.TextFormatFlags]'Left, VerticalCenter, EndEllipsis, NoPrefix, SingleLine'
    $chipW = [Windows.Forms.TextRenderer]::MeasureText($chip, $F.small).Width
    [Windows.Forms.TextRenderer]::DrawText($g, $chip, $F.small, (New-Object Drawing.Rectangle(($w - 14 * $k - $chipW), (10 * $k), ($chipW + 4), (24 * $k))), $chipColor, $flags)
    [Windows.Forms.TextRenderer]::DrawText($g, "$store  -  $build", $F.title, (New-Object Drawing.Rectangle((36 * $k), (10 * $k), ($w - 60 * $k - $chipW), (24 * $k))), $(if ($sel) { $C.text } else { Hex '#d5dde0' }), $flags)
    [Windows.Forms.TextRenderer]::DrawText($g, [string]$entry.path, $F.small, (New-Object Drawing.Rectangle((36 * $k), (34 * $k), ($w - 50 * $k), (22 * $k))), $C.muted, [Windows.Forms.TextFormatFlags]'Left, VerticalCenter, PathEllipsis, NoPrefix, SingleLine')
}
function Refresh-Cards {
    $current = $path.Text.Trim().Trim('"')
    $cards.SuspendLayout()
    try {
        foreach ($old in @($cards.Controls)) { $old.Dispose() }
        $cards.Controls.Clear()
        $selected = $null; $first = $null; $same = $null
        foreach ($entry in @(Get-CoopInstallCards $PSScriptRoot)) {
            $script:snapshots[$entry.path.ToLowerInvariant()] = $entry.state      # (the card's read is the folder's state: not read again when it is picked)
            $card = New-Object Windows.Forms.RadioButton
            $k = $browse.Height / 32.0
            $card.Appearance = 'Button'; $card.AutoSize = $false; $card.Size = New-Object Drawing.Size(($cards.ClientSize.Width - 4), [int](62 * $k))
            $card.Margin = New-Object Windows.Forms.Padding(0, 0, 0, [int](8 * $k)); $card.FlatStyle = 'Flat'; $card.FlatAppearance.BorderSize = 0
            $card.Text = [string]$entry.path; $card.Tag = $entry; $card.Cursor = [Windows.Forms.Cursors]::Hand
            Set-DoubleBuffered $card
            $card.Add_Paint($cardPaint)
            $card.Add_MouseEnter({ $script:hotCard = $this; $this.Invalidate() })
            $card.Add_MouseLeave({ if ([object]::ReferenceEquals($script:hotCard, $this)) { $script:hotCard = $null }; $this.Invalidate() })
            $card.Add_CheckedChanged({ $this.Invalidate(); if ($this.Checked) { Set-Path ([string]$this.Tag.path) } })
            $cards.Controls.Add($card)
            if (-not $first) { $first = $card }
            if ($entry.selected) { $selected = $card }
            if ($current -and $entry.path -eq $current) { $same = $card }
        }
    } finally { $cards.ResumeLayout(); Layout-Body }
    if ($same) { $selected = $same }      # (a Refresh keeps the card that was chosen)
    if (-not $selected) { $selected = $first }
    if ($selected) { $selected.Checked = $true; Set-Path ([string]$selected.Tag.path) }
}
$cards.Add_Resize({ foreach ($c in $cards.Controls) { $w = $cards.ClientSize.Width - 4; if ($c.Width -ne $w) { $c.Width = $w } } })
# everything is read and laid out before the window is shown: it opens complete and nothing moves afterwards
$form.SuspendLayout()
try { Refresh-Cards } catch { Set-Text $status 'Some installs could not be inspected. Browse to your game folder.' $C.warn }
Refresh-Status
Fit-Columns
$form.ResumeLayout()
[void]$form.ShowDialog()
$form.Dispose()
