#!/usr/bin/env python3
"""Game-free guard: the audience TV screens and seats are drawn at full opacity, so the
overlap fade (proxy/ghost) can never touch them. A mutant that feeds the ghost alpha
into that pass must be rejected."""
import re, sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "proxy" / "model_gpu.cpp"

def audience_body(text):
    m = re.search(r"bool ModelGpuDrawAudience\(.*?\n}\n", text, re.S)
    return m.group(0) if m else ""

def check(text):
    body = audience_body(text)
    if not body:
        return "ModelGpuDrawAudience not found"
    if "g_ghost" in body or "wghost::" in body:
        return "audience pass uses the ghost fade"
    if not re.search(r"T\[4\]\s*=\s*\{1,\s*1,\s*1,\s*1\}", body):
        return "audience pass tint is not fully opaque"
    return None

text = SRC.read_text()
err = check(text)
if err:
    print("FAIL", err); sys.exit(1)
print("PASS audience screens and seats are not faded by the overlap rule")
mutant = text.replace("memcpy(fc.tint,T,16); fc.screen[0]=float(w)", "T[3]=g_ghost.begin()->second.alpha; memcpy(fc.tint,T,16); fc.screen[0]=float(w)", 1)
if mutant == text:
    print("FAIL mutant did not apply"); sys.exit(1)
if check(mutant) is None:
    print("FAIL mutant survived"); sys.exit(1)
print("PASS fade-into-audience mutant rejected")
