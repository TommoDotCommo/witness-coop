#!/usr/bin/env python3
"""T0 for dist/steam-input/witness-coop_deck.vdf (game-free): parses the Steam Input layout and checks that it presents an Xbox gamepad for every
physical button and that the Deck's back buttons are what the README says (L4 = View, R4 = keyboard B = push to talk, L5/R5 unbound).
The file is checked, then a mutant copy (R4 moved to another key, the A button unbound) must be refused. Usage: test_steam_input.py [file]"""
import re, sys, os

def parse(text):
    """Steam's KeyValues text: "key" "value" pairs and "key" { ... } blocks -> dict; a repeated key becomes a list."""
    toks = re.findall(r'"((?:[^"\\]|\\.)*)"|([{}])', text)
    pos = 0
    def block():
        nonlocal pos
        d = {}
        while pos < len(toks):
            s, br = toks[pos]; pos += 1
            if br == '}': return d
            key = s
            s2, br2 = toks[pos]; pos += 1
            val = block() if br2 == '{' else s2
            if key in d: d[key] = (d[key] if isinstance(d[key], list) else [d[key]]) + [val]
            else: d[key] = val
        return d
    return block()

def as_list(x): return x if isinstance(x, list) else [x]

def bindings(group_input):
    out = []
    for act in as_list(group_input.get("activators", {}).get("Full_Press", {})) + as_list(group_input.get("activators", {}).get("Soft_Press", {})):
        b = act.get("bindings", {}).get("binding") if isinstance(act, dict) else None
        out += [x.split(",")[0].strip() for x in as_list(b)] if b else []
    return out

def check(text):
    errs = []
    cm = parse(text).get("controller_mappings")
    if not cm: return ["no controller_mappings"]
    if cm.get("title") != "The Witness Co-op (pad layer)": errs.append("title")
    for w in ("View", "push to talk", "L4", "R4"):
        if w not in cm.get("description", ""): errs.append("description lacks " + w)
    if cm.get("controller_type") != "controller_neptune": errs.append("not the Deck's controller type")
    groups = as_list(cm.get("group", []))
    bound = {}                       # input name -> bindings, over all groups
    modes = set()
    for g in groups:
        modes.add(g.get("mode"))
        for name, inp in g.get("inputs", {}).items():
            if isinstance(inp, dict): bound.setdefault(name, []).extend(bindings(inp))
    allb = [b.lower() for v in bound.values() for b in v]
    for need in ("A", "B", "X", "Y", "dpad_up", "dpad_down", "dpad_left", "dpad_right", "shoulder_left", "shoulder_right", "start", "select", "joystick_left", "joystick_right"):
        if "xinput_button " + need.lower() not in allb: errs.append("gamepad button not presented: " + need)
    triggers = sorted(g.get("settings", {}).get("output_trigger", "") for g in groups if g.get("mode") == "trigger")
    if triggers != ["1", "2"]: errs.append("triggers: %s" % triggers)
    if "joystick_move" not in modes: errs.append("no stick group")
    # the back buttons: L4 = View (xinput SELECT), R4 = push to talk key B, L5 and R5 unbound
    if bound.get("button_back_left_upper") != ["xinput_button SELECT"]: errs.append("L4 is not View: %s" % bound.get("button_back_left_upper"))
    if bound.get("button_back_right_upper") != ["key_press B"]: errs.append("R4 is not key B: %s" % bound.get("button_back_right_upper"))
    for n in ("button_back_left", "button_back_right"):
        if bound.get(n) != []: errs.append(n + " should be unbound: %s" % bound.get(n))
    # no other keyboard or mouse binding: the layout is a gamepad, and the game must not see keys except the one push to talk key
    kb = [b for b in (x for v in bound.values() for x in v) if re.match(r"(key_press|mouse_)", b)]
    if kb != ["key_press B"]: errs.append("keyboard/mouse bindings besides R4: %s" % kb)
    return errs

def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "dist", "steam-input", "witness-coop_deck.vdf")
    text = open(path, encoding="utf-8").read()
    errs = check(text)
    for e in errs: print("FAIL:", e)
    mutants = {"R4 on another key": text.replace("key_press B", "key_press V"),
               "A unbound": text.replace("xinput_button A\"", "\""),
               "L4 not View": re.sub(r'(button_back_left_upper".*?binding"\t\t")xinput_button SELECT', r'\1xinput_button Y', text, count=1, flags=re.S)}
    for name, m in mutants.items():
        if m == text or not check(m): errs.append("mutant not caught: " + name); print("FAIL: mutant not caught:", name)
        else: print("mutant caught:", name)
    print("steam input layout:", "FAILED" if errs else "ok")
    return 1 if errs else 0

if __name__ == "__main__": sys.exit(main())
