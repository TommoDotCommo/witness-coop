#!/usr/bin/env python3
"""Read diagnostics bundles (the zips the launcher's "Send diagnostics" makes, or a logs folder) and say what happened: sessions, crashes
(module and offset, mapped to the proxy's linker map when it is the proxy), desyncs, markers, link quality from the flight recorder, and one timeline.

    tools/diag_report.py a.zip b.zip                      summary of each, then what the pair shows
    tools/diag_report.py a.zip b.zip --timeline merged.txt [--align]
    tools/diag_report.py a.zip --map proxy/AnselSDK64.map [--dll proxy/AnselSDK64.dll]
    tools/diag_report.py some/logs --raw                  a live logs folder: the hash chains are checked here
    tools/diag_report.py bundle.zip --json                the same summary as JSON

Session log lines are "<n> <utc ms> <chain> <text>", chain = FNV-1a/64 of "<n> <utc ms> <text>" continued from the previous line's chain
(common/diag_log.h). A bundle was redacted after the chain was checked on the sender's machine, so there the chain result comes from
manifest.json; a raw folder is checked here. This shows lost, cut or edited lines, not forgery.
"""
import argparse
import hashlib
import json
import os
import re
import statistics
import struct
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import map_symbols
import minidump

FNV_BASIS = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
MASK = (1 << 64) - 1
CLEAN_END = b"session end: clean exit"


def fnv(h, data):
    for b in data:
        h = ((h ^ b) * FNV_PRIME) & MASK
    return h


def chain_line(prev, n, utc, text):
    """The chain value for one line, given the previous one."""
    return fnv(prev, b"%d %s %s" % (n, utc, text))


def format_line(prev, n, utc_ms, text):
    """One session-log line as common/diag_log.h writes it; returns (line bytes, chain). For tests."""
    utc = b"%d" % utc_ms
    text = text.replace(b"\r", b" ").replace(b"\n", b" ")
    c = chain_line(prev, n, utc, text)
    return b"%d %s %016x %s\n" % (n, utc, c, text), c


def parse_log(data):
    """Lines of a session log -> (lines, problem). A line is (n, utc ms, text bytes); the chain is checked."""
    lines, prev, problem = [], FNV_BASIS, None
    pos = 0
    while pos < len(data):
        end = data.find(b"\n", pos)
        if end < 0:
            break                   # being written when copied, or cut off by a crash: not a broken chain
        raw = data[pos:end]
        pos = end + 1
        parts = raw.split(b" ", 3)
        if len(parts) < 4:
            problem = problem or "malformed line %d" % len(lines)
            break
        n, utc, chain, text = parts
        if n != b"%d" % len(lines):
            problem = problem or "line %s where %d was expected (lines missing)" % (n.decode("ascii", "replace"), len(lines))
            break
        prev = chain_line(prev, len(lines), utc, text)
        if chain != b"%016x" % prev:
            problem = problem or "chain mismatch at line %d (edited or damaged)" % len(lines)
        try:
            lines.append((len(lines), int(utc), text))
        except ValueError:
            problem = problem or "bad time at line %d" % len(lines)
            break
    return lines, problem


class Source:
    """One bundle or coop folder: its files by relative path (forward slashes) and what is known about them."""

    def __init__(self, path, raw):
        self.path = path
        self.name = os.path.basename(path.rstrip("/\\")).replace(".zip", "")
        self.files = {}
        self.manifest = None
        self.problems = []
        if os.path.isdir(path):
            raw = True
            for root, _dirs, names in os.walk(path):
                for f in names:
                    full = os.path.join(root, f)
                    self.files[os.path.relpath(full, path).replace(os.sep, "/")] = open(full, "rb").read()
            if not any(f.startswith("logs/") for f in self.files):      # a logs folder itself: its files are the "logs/" of a bundle
                self.files = {"logs/" + f if "/" not in f else f: d for f, d in self.files.items()}
        else:
            with zipfile.ZipFile(path) as z:
                for info in z.infolist():
                    if not info.is_dir():
                        self.files[info.filename.replace("\\", "/")] = z.read(info)
            if "manifest.json" in self.files:
                self.manifest = json.loads(self.files["manifest.json"].decode("utf-8-sig"))
            else:
                self.problems.append("no manifest.json: not a bundle from the launcher")
        self.raw = raw
        self.check_manifest()

    def check_manifest(self):
        if not self.manifest:
            return
        listed = {}
        for e in self.manifest.get("files", []):
            listed[e["file"]] = e
            data = self.files.get(e["file"])
            if data is None:
                self.problems.append("%s is listed in the manifest but missing from the zip" % e["file"])
            elif hashlib.sha256(data).hexdigest() != e["sha256"]:
                self.problems.append("%s does not match its SHA-256 in the manifest (changed after the bundle was made)" % e["file"])
        for f in self.files:
            if f != "manifest.json" and f != "README.txt" and f not in listed:
                self.problems.append("%s is in the zip but not in the manifest" % f)
        for p in self.manifest.get("problems", []):
            self.problems.append("collection: %s" % p)

    def chain_info(self, rel):
        """(status, problem, clean exit) for a session log: from the manifest for a bundle, else checked here."""
        if self.manifest and not self.raw:
            for e in self.manifest.get("files", []):
                if e["file"] == rel and "chain" in e:
                    c = e["chain"]
                    return c["status"], c.get("problem") or None, c.get("clean_exit")
        lines, problem = parse_log(self.files[rel])
        clean = bool(lines) and lines[-1][2] == CLEAN_END
        return ("broken" if problem else "ok"), problem, clean


def lines_of(src, rel):
    """Lines of a session log. In a redacted bundle the chain no longer matches the text, so it is not re-checked."""
    out = []
    for raw in src.files[rel].split(b"\n"):
        parts = raw.split(b" ", 3)
        if len(parts) == 4 and parts[0].isdigit() and parts[1].isdigit():
            out.append((int(parts[0]), int(parts[1]), parts[3]))
    return out


def fmt_time(ms):
    t = ms / 1000.0
    return "%s.%03d" % (__import__("time").strftime("%Y-%m-%d %H:%M:%S", __import__("time").gmtime(t)), ms % 1000)


def spark(values, width=60):
    bars = " .:-=+*#%@"
    if not values:
        return ""
    step = max(1, len(values) // width)
    cols = [max(values[i:i + step]) for i in range(0, len(values), step)]
    hi = max(cols) or 1
    return "".join(bars[min(len(bars) - 1, int(v * (len(bars) - 1) / hi))] for v in cols)


def stats(values):
    if not values:
        return None
    v = sorted(values)
    return {"min": v[0], "avg": statistics.mean(v), "p95": v[min(len(v) - 1, int(len(v) * 0.95))], "max": v[-1], "n": len(v)}


# F2 markers: "MARK n pressed here" where it was pressed, "MARK n (from player k)" where it arrived
MARK = re.compile(r"MARK (\d+)(?: pressed here| \(from player (\d+)\))")
CRASH_AT = re.compile(r"crash: (\w[\w-]*) exception (0x[0-9a-f]+) at ([^\s+]+)\+0x([0-9a-f]+)")


PROXY_MODULE = "anselsdk64.dll"
SYMBOLS = {"map": None, "stamp": None}      # --map / --dll


def crash_info(src, key, crash_lines):
    """What is known about one crash: the exception and where, from the log line and from the minidump; mapped when it is the proxy."""
    info = {"code": None, "module": None, "offset": None, "kind": None, "dump": None, "dump_error": None, "where": None, "stack": [], "map": None}
    for _utc, t in crash_lines:
        m = CRASH_AT.match(t)
        if m:
            info["kind"], info["code"], info["module"], info["offset"] = m.group(1), m.group(2), m.group(3), int(m.group(4), 16)
            break
    dmp = "logs/%s.dmp" % key
    if dmp in src.files:
        try:
            d = minidump.Minidump(src.files[dmp])
            ex = d.exception
            info["dump"] = {"size": len(src.files[dmp]), "code": "0x%08x" % ex["code"], "name": minidump.EXCEPTION_NAMES.get(ex["code"], "unknown"),
                            "where": d.locate(ex["address"]), "modules": len(d.modules), "thread": ex["thread"]}
            info["stack"] = [loc for _w, loc in d.code_pointers(96)][:12]
            m = d.module_for(ex["address"])
            if m and not info["module"]:
                info["module"], info["offset"], info["code"] = m["name"].replace("\\", "/").rsplit("/", 1)[-1], ex["address"] - m["base"], "0x%08x" % ex["code"]
            info["dump"]["module_timestamp"] = m["timestamp"] if m else None
        except (minidump.MinidumpError, struct.error) as e:
            info["dump_error"] = str(e)
    if info["module"] and info["module"].lower() == PROXY_MODULE and SYMBOLS["map"] and info["offset"] is not None:
        sm = SYMBOLS["map"]
        info["map"] = sm.describe(info["offset"])
        stamp = info["dump"].get("module_timestamp") if info["dump"] else None
        if SYMBOLS["stamp"] is not None and stamp is not None and stamp != SYMBOLS["stamp"]:
            info["map"] += "  (!! this map is from another build: the dump's DLL stamp is 0x%08x, the map's DLL 0x%08x)" % (stamp, SYMBOLS["stamp"])
    return info


def summarise(src):
    """Everything worth saying about one source, as a dict."""
    out = {"source": src.name, "problems": list(src.problems), "sessions": [], "flight": [], "system": {}}
    for rel in sorted(src.files):
        if re.fullmatch(r"logs/[^/]+\.log", rel):
            key = rel.split("/")[-1][:-4]
            status, problem, clean = src.chain_info(rel)
            lines = lines_of(src, rel)
            s = {"session": key, "program": "launcher" if "_launcher_p" in key else "game", "lines": len(lines), "chain": status, "chain_problem": problem,
                 "clean_exit": bool(clean), "crashed": ("logs/%s.crash" % key) in src.files, "version": "",
                 "start": lines[0][1] if lines else 0, "end": lines[-1][1] if lines else 0,
                 "crash_lines": [], "desync": [], "marks": [], "notes": []}
            for _n, utc, text in lines:
                t = text.decode("latin-1")
                m = re.match(r"session \S+: mod (\S+)", t)
                if m:
                    s["version"] = m.group(1)
                if t.startswith("crash:"):
                    s["crash_lines"].append((utc, t))
                if t.startswith("DESYNC"):
                    s["desync"].append((utc, t))
                if MARK.match(t):
                    s["marks"].append((utc, t))
                if t.startswith("diag: test crash requested"):
                    s["notes"].append((utc, t))
            if s["crashed"] or s["crash_lines"]:
                s["crash"] = crash_info(src, key, s["crash_lines"])
            out["sessions"].append(s)
        m = re.fullmatch(r"logs/([^/]+)\.flight-(\d+)-(\w+)\.txt", rel)
        if m:
            body = src.files[rel].decode("latin-1").split("\n")
            reason, fps, loss, rtt, unacked, queued, gens, samples = "", [], {}, {}, [], [], set(), 0
            for line in body:
                if line.startswith("# reason:"):
                    reason = line[9:].strip()
                if line.startswith("S ") and "|" in line:
                    head, tail = line.split("|", 1)
                    f = head.split()
                    if len(f) < 11:
                        continue
                    samples += 1
                    fps.append(float(f[2]))
                    gens.add(f[6])
                    unacked.append(int(f[7]))
                    queued.append(int(f[8]))
                    for rec in tail.split(";"):
                        r = rec.split()
                        if len(r) >= 5:
                            rtt.setdefault(int(r[0]), []).append(float(r[1]))
                            loss.setdefault(int(r[0]), []).append(float(r[3]))
            out["flight"].append({"file": rel, "session": m.group(1), "marker": int(m.group(2)), "kind": m.group(3), "reason": reason,
                                  "samples": samples, "fps": stats(fps), "rtt": {k: stats([x for x in v if x >= 0]) for k, v in rtt.items()},
                                  "loss": {k: stats(v) for k, v in loss.items()}, "unacked_max": max(unacked) if unacked else 0,
                                  "queued_max": max(queued) if queued else 0, "generations": len(gens), "rtt_series": rtt})
    for rel in ("system.txt", "network.txt", "game-folder.txt"):
        if rel in src.files:
            out["system"][rel] = src.files[rel].decode("utf-8", "replace")
    return out


def print_summary(r):
    p = print
    p("=" * 78)
    p("%s" % r["source"])
    for pr in r["problems"]:
        p("  !! %s" % pr)
    sysx = r["system"].get("system.txt", "")
    for key in ("mod version:", "windows:", "cpu:", "memory:", "graphics:", "witness64_d3d11.exe:", "AnselSDK64.dll:"):
        for line in sysx.splitlines():
            if line.startswith(key):
                p("  " + line)
    crashes = sysx.split("== game crashes in the Windows event log (14 days)")[-1].split("==")[0].strip() if sysx else ""
    if crashes and crashes != "none":
        p("  windows event log, game crashes:")
        for line in crashes.splitlines():
            p("    " + line.strip())
    for s in r["sessions"]:
        dur = (s["end"] - s["start"]) / 1000.0
        end = "CRASHED" if s["crashed"] else "ended normally" if s["clean_exit"] else "no clean exit (killed, or still running)"
        p("")
        p("  session %s  %s  mod %s  %.0f s  %d lines  %s" % (s["session"], s["program"], s["version"] or "?", dur, s["lines"], end))
        if s["chain"] != "ok":
            p("    !! log chain %s: %s" % (s["chain"], s["chain_problem"]))
        for utc, t in s["notes"]:
            p("    %s  %s" % (fmt_time(utc), t))
        for utc, t in s["desync"]:
            p("    %s  %s" % (fmt_time(utc), t))
        for utc, t in s["marks"]:
            p("    %s  %s" % (fmt_time(utc), t))
        c = s.get("crash")
        if c:
            p("    CRASH %s %s at %s+0x%x" % (c["kind"] or "", c["code"] or "?", c["module"] or "?", c["offset"] or 0))
            if c["map"]:
                p("      -> %s" % c["map"])
            elif c["module"] and c["module"].lower() == PROXY_MODULE:
                p("      (give --map proxy/AnselSDK64.map of the same build to name the function)")
            if c["dump"]:
                d = c["dump"]
                p("      minidump: %d bytes, %s %s at %s, thread %d, %d modules" % (d["size"], d["code"], d["name"], d["where"], d["thread"], d["modules"]))
            elif c["dump_error"]:
                p("      !! minidump unreadable: %s" % c["dump_error"])
            elif s["crashed"]:
                p("      (no minidump in this bundle)")
            if c["stack"]:
                p("      code addresses on the stack: " + " ".join(c["stack"]))
            for utc, t in s["crash_lines"]:
                if t.startswith("crash: registers"):
                    p("      " + t[7:])
    for f in r["flight"]:
        p("")
        p("  flight recorder %s marker %d (%s): %s" % (f["session"], f["marker"], f["kind"], f["reason"]))
        if f["fps"]:
            p("    %d samples (%.0f s); frame rate %.0f / %.0f / %.0f (min / avg / max); sync queue max %d unacked, %d waiting; %d world generation(s)"
              % (f["samples"], f["samples"] / 10.0, f["fps"]["min"], f["fps"]["avg"], f["fps"]["max"], f["unacked_max"], f["queued_max"], f["generations"]))
        for pl in sorted(f["rtt"]):
            rt, lo = f["rtt"][pl], f["loss"].get(pl)
            if rt:
                p("    player %d: round trip %d / %.0f / %d ms (min / avg / max)%s  [%s]" % (pl, rt["min"], rt["avg"], rt["max"],
                   ", pose loss up to %.1f%%" % lo["max"] if lo else "", spark(f["rtt_series"][pl])))


def mark_pairs(reports):
    """(mark number, sender source, sender utc, receiver source, receiver utc) for every F2 mark pressed in one
    bundle and received in another. Mark numbers count per player, so a number pressed in two bundles is ambiguous
    and left out."""
    local, recv = {}, {}
    for r in reports:
        for s in r["sessions"]:
            for utc, t in s["marks"]:
                m = MARK.match(t)
                if m:
                    (recv if m.group(2) else local).setdefault(int(m.group(1)), []).append((r["source"], utc))
    pairs = []
    for n, sent in sorted(local.items()):
        if len({src for src, _ in sent}) != 1:
            continue
        for src, utc in recv.get(n, []):
            if src != sent[0][0]:
                pairs.append((n, sent[0][0], sent[0][1], src, utc))
    return pairs


def pair_findings(reports):
    """What several sources show together: F2 marks seen on both machines and the clock offset between them."""
    return ["mark %d pressed on %s, arrived on %s: its clock reads %+d ms against %s's (includes the one-way delay, "
            "about half the round trip)" % (n, a, b, ub - ua, a) for n, a, ua, b, ub in mark_pairs(reports)]


def timeline(sources, reports, path, align):
    offsets = {r["source"]: 0 for r in reports}
    if align:
        for _n, a, ua, b, ub in mark_pairs(reports):
            if not offsets[b] and offsets[a] == 0:
                offsets[b] = ua - ub        # shift b so the mark it received lines up with the press
    rows = []
    for src in sources:
        for rel in src.files:
            if re.fullmatch(r"logs/[^/]+\.log", rel):
                for _n, utc, text in lines_of(src, rel):
                    rows.append((utc + offsets[src.name], src.name, rel.split("/")[-1][:19], text.decode("latin-1")))
    rows.sort(key=lambda x: (x[0], x[1]))
    with open(path, "w") as f:
        for utc, name, sess, text in rows:
            f.write("%s  %-12s %s\n" % (fmt_time(utc), name[:12], text))
    return len(rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inputs", nargs="+", help="bundle zips, or logs folders")
    ap.add_argument("--raw", action="store_true", help="the inputs are live (unredacted): check the hash chains here")
    ap.add_argument("--timeline", metavar="FILE", help="write every log line of every input, in time order")
    ap.add_argument("--align", action="store_true", help="in the timeline, shift each source so a shared mark lines up")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--map", help="proxy/AnselSDK64.map of the build the crash came from: names the function a proxy crash is in")
    ap.add_argument("--dll", help="that build's AnselSDK64.dll: its link time stamp is checked against the dump's, so another build's map is not trusted blindly")
    a = ap.parse_args()
    if a.map:
        SYMBOLS["map"] = map_symbols.SymbolMap(open(a.map, errors="replace").read())
    if a.dll:
        SYMBOLS["stamp"] = map_symbols.pe_timestamp(a.dll)
    sources = [Source(p, a.raw) for p in a.inputs]
    reports = [summarise(s) for s in sources]
    if a.json:
        json.dump(reports, sys.stdout, indent=1, default=lambda o: None)
        print()
    else:
        for r in reports:
            print_summary(r)
        pf = pair_findings(reports)
        if pf:
            print("")
            print("=" * 78)
            for line in pf:
                print(line)
    if a.timeline:
        n = timeline(sources, reports, a.timeline, a.align)
        print("\ntimeline: %d lines in %s" % (n, a.timeline))
    bad = any(r["problems"] or any(s["chain"] != "ok" for s in r["sessions"]) for r in reports)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
