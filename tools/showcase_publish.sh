#!/bin/bash
# Turns the showcase pictures (showcase/*.png, made by tools/showcase.sh) into the JPEGs of docs/showcase/ that docs/SHOWCASE.md and the README show on GitHub (about a tenth of the size).
# Pictures wider than SHOWCASE_MAX_WIDTH (default 1366) are scaled down to it; the launcher windows are smaller and stay as they are.
# JPEG is written by Windows' own encoder (System.Drawing through PowerShell: there is no image library in WSL). usage: tools/showcase_publish.sh [quality, default 85]

set -euo pipefail
cd "$(dirname "$0")/.."
q="${1:-85}"; mw="${SHOWCASE_MAX_WIDTH:-1366}"; src="${SHOWCASE_OUT:-$PWD/showcase}"; dst="$PWD/docs/showcase"; mkdir -p "$dst"
[ -d "$src" ] || { echo "no pictures in $src: run tools/showcase.sh first" >&2; exit 1; }
timeout 300 powershell.exe -NoProfile -Command "Add-Type -AssemblyName System.Drawing; \$codec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | ? { \$_.MimeType -eq 'image/jpeg' }; \$ps = New-Object System.Drawing.Imaging.EncoderParameters(1); \$ps.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter([System.Drawing.Imaging.Encoder]::Quality, [long]$q); Get-ChildItem '$(wslpath -w "$src")' -Filter *.png | ForEach-Object { \$img = [System.Drawing.Image]::FromFile(\$_.FullName); if (\$img.Width -gt $mw) { \$h = [int][math]::Round(\$img.Height * $mw / \$img.Width); \$bmp = New-Object System.Drawing.Bitmap($mw, \$h); \$g = [System.Drawing.Graphics]::FromImage(\$bmp); \$g.InterpolationMode = 'HighQualityBicubic'; \$g.DrawImage(\$img, 0, 0, $mw, \$h); \$g.Dispose(); \$img.Dispose(); \$img = \$bmp }; \$img.Save('$(wslpath -w "$dst")\\' + \$_.BaseName + '.jpg', \$codec, \$ps); \$img.Dispose() }; 'done'"
ls "$dst" | wc -l; du -sh "$dst"
