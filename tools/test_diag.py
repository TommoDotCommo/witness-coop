#!/usr/bin/env python3
"""No game, no game copies. The diagnostics chain end to end on made-up folders (needs Windows PowerShell through WSL interop):

  * tools/diag_report.py's log reader (whole, edited, missing lines, cut tail);
  * dist/diag.ps1 on a logs folder whose newest log is held open for writing the way the game holds it, with names, addresses and Steam IDs
    planted in the logs, the settings file and a binary crash dump: none of them may reach the zip; entry names use '/'; the manifest hashes
    and hash chains check out; the dump stays a readable minidump of the same size;
  * the report on the result, including a real crash dump (launcher/build/crash-out, made by `make -C launcher crash-test`) and the proxy map;
  * MUTANTS: the script with one protection removed (the computer name, the user name, the profile path, IP addresses, Steam IDs, the dump masking)
    must leak the planted string, or this test has no power. A tampered zip and a damaged log must be caught.

    python3 -B tools/test_diag.py
"""
import json, os, re, subprocess, sys, tempfile, zipfile
from pathlib import Path

here = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(here / "tools"))
import diag_report as d

failures = []


def check(name, ok, detail=""):
    print(("PASS  " if ok else "FAIL  ") + name + ("" if ok else "  " + str(detail)[:300]))
    if not ok:
        failures.append(name)


def ps(cmd):
    return subprocess.run(["powershell.exe", "-NoProfile", "-Command", cmd], capture_output=True, text=True).stdout.strip().replace("\r", "")


def win(p):
    return subprocess.run(["wslpath", "-w", str(p)], capture_output=True, text=True).stdout.strip()


user, host = ps("$env:USERNAME"), ps("$env:COMPUTERNAME")
profile = ps("$env:USERPROFILE")
if len(user) < 3 or len(host) < 3:
    print("the Windows user or computer name is too short to test with: %r %r" % (user, host)); sys.exit(2)


def session(lines, base=1761570000000, clean=True):
    prev, data = d.FNV_BASIS, b""
    texts = list(lines) + ([b"session end: clean exit"] if clean else [])
    for n, t in enumerate(texts):
        line, prev = d.format_line(prev, n, base + n * 100, t)
        data += line
    return data


good = session([b"session s1: mod v-test", b"coop: id=1 listening 47001", b"MARK 1 pressed here", b"MARK 2 (from player 2)"])
lines, problem = d.parse_log(good)
check("a whole log reads without problem", problem is None and len(lines) == 5, problem)
rows = good.split(b"\n")
check("a missing line is found", d.parse_log(b"\n".join(rows[:2] + rows[3:]))[1] is not None)
edited = good.replace(b"listening 47001", b"listening 47002")
check("an edited line is found", d.parse_log(edited)[1] is not None)
check("a half-written last line is not a broken chain", d.parse_log(good + b"7 17615 abc half")[1] is None)
check("a line break inside a message is flattened", d.format_line(d.FNV_BASIS, 0, 1, b"two\nlines")[0].count(b"\n") == 1)

out = Path(tempfile.mkdtemp(prefix="witness-diag-test-", dir=str(here / "local") if (here / "local").is_dir() else None))
gamedir, logs = out / "game", out / "logs"
logs.mkdir(parents=True); gamedir.mkdir(parents=True)
planted_path = ("C:\\Users\\" + user + "\\Documents").encode()
planted = (b"saved to " + planted_path + b" on " + host.encode() + b", peer 192.168.1.20:47002, relay 100.101.102.103, remote 8.8.4.4, "
           b"steam 76561198012345678, mail someone@example.com")
crash_dump = next((here / "launcher/build/crash-out/av").glob("*.dmp"), None)
if crash_dump is None:
    subprocess.run(["make", "-s", "-C", str(here / "launcher"), "crash-test"], capture_output=True)
    crash_dump = next((here / "launcher/build/crash-out/av").glob("*.dmp"), None)
dump = bytearray(crash_dump.read_bytes()) if crash_dump else bytearray(b"MDMP" + b"\0" * 4000)
# plant the strings in the dump the ways memory holds them: ASCII and UTF-16, and the profile path
cut = user[:-1]       # a name cut short by the end of a block of memory in the dump
dump += b"\0\0" + b"\\Users\\" + cut.encode() + b"\0\0" + ("\\Users\\" + cut).encode("utf-16-le") + b"\0\0" + b"\0\0" + user.encode() + b"\0" + user.encode("utf-16-le") + b"\0\0" + host.encode() + b"\0" + host.encode("utf-16-le") + b"\0" + planted_path + b"\0" + b"192.168.1.20\0" + b"76561198012345678\0"
(logs / "2026-10-26T10-00-00Z_p1.log").write_bytes(session([b"session a: mod v-test", planted, b"crash: unhandled exception 0xc0000005 at dsound.dll+0x2a30 (thread 1, writing 0000000000000000)"], clean=False))
(logs / "2026-10-26T10-00-00Z_p1.crash").write_bytes(b"")
(logs / "2026-10-26T10-00-00Z_p1.dmp").write_bytes(bytes(dump))
(logs / "2026-10-26T10-00-00Z_p1.flight-0-crash.txt").write_text(
    "# flight recorder: kind=crash marker=0\n# reason: unhandled exception\nS 1 59.9 1 2 3 abc 0 0 5 0 | 2 31 4 0.5 1 2 3 ;\nL 1 " + planted.decode() + "\n")
live = logs / "2026-10-27T10-00-00Z_p2.log"
live.write_bytes(session([b"session b: mod v-test", planted, b"MARK 1 pressed here"], clean=False))
(logs / "2026-10-27T10-00-00Z_p2.flight-1-mark.txt").write_text("# flight recorder: kind=mark marker=1\n# reason: MARK 1 pressed here\nS 1 30.0 1 2 3 abc 2 1 5 1 | 2 80 4 3.5 1 2 3 ;\n")
(logs / "2026-10-27T10-00-30Z_launcher_p3.log").write_bytes(session([b"session c: mod v-test", b"launcher started"]))
broken = logs / "2026-10-25T10-00-00Z_p0.log"
b_rows = session([b"session c: mod v-test", b"x", b"y", b"z"]).split(b"\n")
broken.write_bytes(b"\n".join(b_rows[:2] + b_rows[3:]))
(gamedir / "witness_coop.cfg").write_text("id=1\nlisten=47001\npeer=1,100.101.102.103,47002\nname=Kurt\nsteam=76561198012345678\nsave_dir=" + planted_path.decode() + "\n")
(gamedir / "witness_proxy.log").write_bytes(planted + b"\r\n")
(gamedir / "witness64_d3d11.exe").write_bytes(b"MZ" + b"\0" * 100)

script = (here / "dist/diag.ps1").read_text()
check("the script is plain ASCII (Windows PowerShell 5.1 reads it as ANSI)", all(ord(c) < 128 for c in script))


def run_bundle(text, tag):
    """Run a version of the script against the planted folders; return (zip path or None, result text)."""
    sdir = out / tag; sdir.mkdir()
    sp = sdir / "diag.ps1"; sp.write_text(text)
    # the game still holds its live log open for appending and lets others read it
    holder = subprocess.Popen(["powershell.exe", "-NoProfile", "-Command", "$f = [IO.File]::Open('%s', 'Append', 'Write', 'Read'); Start-Sleep 40" % win(live)])
    import time; time.sleep(2.5)
    res = sdir / "result.txt"
    r = subprocess.run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", win(sp), "-GameDir", win(gamedir), "-LogsDir", win(logs),
                        "-OutDir", win(sdir), "-Version", "v-test", "-ResultFile", win(res), "-NoOpen"], capture_output=True, text=True)
    holder.kill()
    result = res.read_text().strip() if res.exists() else "no result " + r.stderr[:200]
    zips = sorted(sdir.glob("witness-coop-diag_*.zip"))
    return (zips[0] if zips else None), result


NEEDLES = (("the Windows user name", user.encode()), ("the computer name", host.encode()), ("a username path", planted_path),
           ("a LAN address", b"192.168.1.20"), ("a Tailscale address", b"100.101.102.103"), ("a public address", b"8.8.4.4"),
           ("a Steam id", b"76561198012345678"), ("an e-mail address", b"someone@example.com"), ("a cut username", ("\\Users\\" + user[:-1]).encode()))


def leaks(zpath):
    """Which planted strings are in the zip (ASCII or UTF-16), checked byte-wise over every file."""
    z = zipfile.ZipFile(zpath); blob = b"\n".join(z.read(n) for n in z.namelist()).lower(); found = []
    for what, needle in NEEDLES:
        if needle.lower() in blob or needle.decode().lower().encode("utf-16-le") in blob:
            found.append(what)
    return found


zp, result = run_bundle(script, "real")
check("the script reports the zip", zp is not None and result.startswith("OK "), result)
if zp:
    z = zipfile.ZipFile(zp); names = z.namelist()
    check("entry names use '/'", all("\\" not in n for n in names), names)
    check("the logs, the flight dumps, the crash marker and the crash dump are in it",
          all(any(n.endswith(s) for n in names) for s in ("p1.log", "p1.crash", "p1.dmp", "p1.flight-0-crash.txt", "p2.log", "p2.flight-1-mark.txt", "p3.log", "p0.log")), names)
    check("settings, last log, system and network details are in it", all(n in names for n in ("witness_coop.cfg", "witness_proxy.log", "system.txt", "network.txt", "manifest.json", "README.txt")), names)
    check("no saves and no game files", not any(n.endswith((".witness_campaign", ".exe", ".zip")) for n in names), names)
    check("no private string is in the zip (any file, ASCII or UTF-16)", not leaks(zp), leaks(zp))
    everything = b"\n".join(z.read(n) for n in names if not n.endswith(".dmp"))
    check("addresses became numbered labels", b"ip-1(lan)" in everything and b"(tailscale-or-cgnat)" in everything and b"(public)" in everything)
    check("the same address has the same label", everything.count(b"ip-1(") >= 3)
    sysx = z.read("system.txt").decode()
    check("system.txt has Windows, graphics card, the game build hash and the mod version",
          all(k in sysx for k in ("windows:", "graphics:", "witness64_d3d11.exe:", "sha256", "mod version: v-test")), sysx[:400])
    if crash_dump:
        dz = z.read("logs/2026-10-26T10-00-00Z_p1.dmp")
        check("the masked dump has the same size and is still a minidump", len(dz) == len(dump) and dz[:4] == b"MDMP", (len(dz), len(dump)))
        from minidump import Minidump
        check("the masked dump still parses and names the exception", Minidump(dz).exception["code"] == 0xC0000005)
    m = json.loads(z.read("manifest.json").decode("utf-8-sig"))
    chains = {f["file"].split("/")[-1]: f["chain"] for f in m["files"] if "chain" in f}
    check("the live log was readable while the game held it", chains.get("2026-10-27T10-00-00Z_p2.log", {}).get("status") == "ok", chains)
    check("the crashed run's chain is intact and it has no clean exit",
          chains["2026-10-26T10-00-00Z_p1.log"]["status"] == "ok" and not chains["2026-10-26T10-00-00Z_p1.log"]["clean_exit"])
    check("the launcher's log has a clean exit", chains["2026-10-27T10-00-30Z_launcher_p3.log"]["status"] == "ok")
    check("a log with a line missing is marked broken", chains["2026-10-25T10-00-00Z_p0.log"]["status"] == "broken", chains)

    # --- the report
    mp = here / "proxy/dsound.map"
    cmd = [sys.executable, str(here / "tools/diag_report.py"), str(zp), "--json"] + (["--map", str(mp)] if mp.exists() else [])
    rep = subprocess.run(cmd, capture_output=True, text=True)
    check("the report flags the broken log (exit 1)", rep.returncode == 1, rep.stdout[-300:])
    data = json.loads(rep.stdout)[0]
    sessions = {s["session"]: s for s in data["sessions"]}
    s1 = sessions["2026-10-26T10-00-00Z_p1"]
    check("the report sees the crash, its module and offset", s1["crashed"] and s1["crash"]["module"] == "dsound.dll" and s1["crash"]["offset"] == 0x2a30, s1.get("crash"))
    if mp.exists():
        check("a proxy crash is mapped to an object file of the proxy", s1["crash"]["map"] and ".o" in s1["crash"]["map"] or "in " in (s1["crash"]["map"] or ""), s1["crash"]["map"])
    check("the report reads the dump", crash_dump is None or (s1["crash"]["dump"] and s1["crash"]["dump"]["code"] == "0xc0000005"), s1["crash"])
    check("the report sees the marker", sessions["2026-10-27T10-00-00Z_p2"]["marks"])
    check("the report knows the launcher's log", sessions["2026-10-27T10-00-30Z_launcher_p3"]["program"] == "launcher")
    check("the report reads the flight recorder", any(f["kind"] == "mark" and f["rtt"].get("2") and f["loss"]["2"]["max"] == 3.5 for f in data["flight"]), data["flight"])
    check("system.txt reached the report", "windows:" in data["system"].get("system.txt", ""))

    # --- broken variants must fail
    bad = out / "tampered.zip"
    with zipfile.ZipFile(zp) as src, zipfile.ZipFile(bad, "w") as dst:
        for n in src.namelist():
            body = src.read(n)
            if n.endswith("p2.flight-1-mark.txt"):
                body = body.replace(b"80", b"10")
            dst.writestr(n, body)
    rep = subprocess.run([sys.executable, str(here / "tools/diag_report.py"), str(bad)], capture_output=True, text=True)
    check("a file changed after bundling is caught", "does not match its SHA-256" in rep.stdout and rep.returncode == 1)
    raw = out / "rawcheck"; raw.mkdir()
    (raw / "a.log").write_bytes(edited)
    rep = subprocess.run([sys.executable, str(here / "tools/diag_report.py"), "--raw", str(raw)], capture_output=True, text=True)
    check("a live logs folder with an edited log is caught", "chain mismatch" in rep.stdout and rep.returncode == 1, rep.stdout[-300:])

# --- mutants: one protection removed from the script; the leak check must then find the planted string
MUTANTS = [
    ("computer name", "$machine = [Environment]::MachineName", "$machine = ''", "the computer name"),
    ("user name", "$userName = [Environment]::UserName", "$userName = ''", "the Windows user name"),
    ("username path", None, None, "a username path"),         # needs both of the next two
    ("IP addresses", "if ($ips) {", "if ($false) {", "a LAN address"),
    ("Steam IDs", "7656119\\d{10}(?![0-9])', {\n            param($m)\n            if (-not $steamIds", "7656118\\d{10}(?![0-9])', {\n            param($m)\n            if (-not $steamIds", "a Steam id"),
    ("dump masking", "if ($rel -like '*.dmp') {", "if ($false) {", "the Windows user name"),
    ("cut name masking", "if ($lit.Length -ge 4) {", "if ($false) {", "a cut username"),
]
for name, old, new, expect in MUTANTS:
    if name == "username path":
        t = script.replace("$userName = [Environment]::UserName", "$userName = ''").replace("$profile = $env:USERPROFILE", "$profile = ''")
        applied = t != script
    else:
        applied = old in script
        t = script.replace(old, new, 1)
    check("mutant '%s' is a real change to the script" % name, applied)
    if not applied:
        continue
    mz, mres = run_bundle(t, "mut-" + re.sub(r"\W", "", name))
    check("mutant '%s' leaks (%s)" % (name, expect), mz is not None and expect in leaks(mz), (mres, leaks(mz) if mz else None))

print()
print("%d failed" % len(failures) if failures else "all passed")
sys.exit(1 if failures else 0)
