#!/bin/bash
# A recording of real speech for the voice tests and the demo, made with the Windows voice (System.Speech): tools/make_speech.sh [out.wav] [text]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
[ ! -f "$ROOT/local/env.sh" ] || . "$ROOT/local/env.sh"
out="${1:-${WITNESS_RUN:-$ROOT/local/run}/speech.wav}"; text="${2:-Hello, I found the second door. Meet me by the stone pillar in the garden, and bring the others.}"
mkdir -p "$(dirname "$out")"; w="$(wslpath -w "$out")"
timeout 60 powershell.exe -NoProfile -NonInteractive -Command "Add-Type -AssemblyName System.Speech; \$s = New-Object System.Speech.Synthesis.SpeechSynthesizer; \$s.SetOutputToWaveFile('$w'); \$s.Speak('$text'); \$s.Dispose()"
echo "$out"
