#!/usr/bin/env python3
"""Game-free test of tools/t0_select.py (what `tools/t0.sh --changed` runs and `--uncovered` reports) and of t0.sh's list of suites.

  * made-up trees: an #include chain reaches a make suite through a header it does not include itself; a comment naming a file is no
    dependency; a Python import reaches the test that imports the importer; a Makefile's `$(MAKE) -C ../x` gives it x; a basename two files
    share counts only with its folder; a documentation file reaches nothing; --uncovered lists what no test reaches and not what a build does;
  * the real tree: a few changes and the suites they must (and must not) reach;
  * every Makefile with a test target is a suite of t0.sh or run by one (a new suite cannot be forgotten).
Mutants (the selector's functions replaced in this process): comments count, no transitivity, sub-makes ignored, ambiguous names kept, a
build counted as coverage; each must fail a check.   usage: python3 tools/test_t0_select.py"""
import os, re, shutil, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import t0_select as ts

fails = []
def check(name, ok, detail=''):
    if not ok: fails.append(name)
    if not QUIET: print('%-58s %s %s' % (name, 'ok' if ok else 'FAILED', '' if ok else str(detail)[:200]))

TREE = {
    'lib/a.h': '#include "b.h"\nint a();\n',
    'lib/b.h': '// see tools/tool.py and docs/x.md\nint b();\n',
    'lib/Makefile': 'test:\n\tg++ tests/t.cpp\n',
    'lib/tests/t.cpp': '#include "../b.h"\n',
    'app/main.cpp': '#include "../lib/a.h"\n// lonely.h is mentioned here only in a comment\n',
    'app/Makefile': 'test:\n\t$(MAKE) -C ../plug test\n\tg++ main.cpp\n',
    'plug/p.cpp': 'int p;\n',
    'plug/Makefile': 'test:\n\tg++ p.cpp\n',
    'plug/x/test.cpp': '#include "../p.cpp"\n',
    'other/test.cpp': 'int o;\n',
    'other/Makefile': 'test:\n\tg++ test.cpp\n',
    'tools/helper.py': 'import os\n',
    'tools/tool.py': '"""Uses lonely.h? no."""\nimport helper\n',
    'tools/test_tool.py': 'import tool\n',
    'tools/test_other.py': '# tool.py is only named in this comment\nimport os\n',
    'tools/lonely.h': 'int lonely;\n',
    'build/b.cpp': 'int z;\n',
}
SUITES = ['make lib', 'make app', 'make other', 'tools/test_tool.py', 'tools/test_other.py', 'build build']

def fake_tree():
    d = tempfile.mkdtemp(prefix='t0sel-')
    for f, t in TREE.items():
        os.makedirs(os.path.join(d, os.path.dirname(f)), exist_ok=True); open(os.path.join(d, f), 'w').write(t)
    return d

def made_up():
    d = fake_tree(); old = ts.ROOT; ts.ROOT = d
    try:
        g = ts.Graph(list(TREE))
        sel = lambda *f: set(ts.select(g, list(f), SUITES))
        check('an include chain reaches a suite (b.h -> a.h -> app)', {'make lib', 'make app'} <= sel('lib/b.h'), sel('lib/b.h'))
        check('a comment naming a file is no dependency', 'make lib' not in sel('tools/tool.py') and 'tools/test_other.py' not in sel('tools/tool.py'), sel('tools/tool.py'))
        check('an import chain reaches the test (helper -> tool -> test)', sel('tools/helper.py') == {'tools/test_tool.py'}, sel('tools/helper.py'))
        check('a sub-make gives its folder (plug -> make app)', 'make app' in sel('plug/p.cpp'), sel('plug/p.cpp'))
        check('a shared basename counts only with its folder', 'make other' not in sel('plug/x/test.cpp') and sel('other/test.cpp') == {'make other'}, (sel('plug/x/test.cpp'), sel('other/test.cpp')))
        check('a documentation file reaches nothing', sel('docs/x.md') == set(), sel('docs/x.md'))
        un = set(ts.uncovered(g, SUITES))
        check('--uncovered: what no test reaches, not what only a build does', un == {'tools/lonely.h', 'build/b.cpp'}, un)
    finally:
        ts.ROOT = old; shutil.rmtree(d, ignore_errors=True)

def t0_suites():
    """The suite names t0.sh would pass (its list plus every Python test it globs), read from the script."""
    text = open(os.path.join(HERE, 't0.sh')).read()
    names = re.findall(r'^\s*"([^"|]+)\|', text, re.M)
    skip = re.search(r'^skip="([^"]*)"', text, re.M).group(1).split()
    import glob
    os.chdir(ts.ROOT)
    return names + [f for f in sorted(glob.glob('tools/test_*.py') + glob.glob('tools/playthrough/test_*.py')) if f not in skip]

def real_tree():
    suites = t0_suites(); g = ts.Graph()
    sel = lambda f: set(ts.select(g, [f], suites))
    cases = [
        ('tools/win/wnav.py', {'tools/test_wnav.py', 'tools/test_navsim.py'}, {'make common', 'make launcher'}),
        ('common/coop_invite.h', {'tools/test_coop_discovery.py', 'make launcher', 'make common'}, {'tools/test_navsim.py'}),
        ('dist/diag.ps1', {'tools/test_diag.py'}, {'make proxy/core'}),
        ('proxy/model/model.cpp', {'make proxy/core', 'tools/test_gestures.py', 'build proxy'}, {'make launcher'}),
        ('tools/playthrough/logic.py', {'tools/playthrough/test_playthrough.py'}, {'tools/test_wnav.py'}),
        ('proxy/frame/ribbon.h', {'make proxy/frame'}, {'make launcher'}),
        ('docs/WALKER.md', set(), set(suites)),
    ]
    for f, must, mustnot in cases:
        got = sel(f)
        check('real: %s' % f, must <= got and not (mustnot & got), 'missing %s, extra %s' % (sorted(must - got), sorted(mustnot & got)))
    return suites

def every_makefile(suites):
    run = set()
    for s in suites:
        if s.startswith('make '): run |= ts.owned(s)[0]
    lost = []
    for f in ts.git('ls-files'):
        if f.endswith('Makefile') and 'third_party/' not in f and re.search(r'^test:', open(os.path.join(ts.ROOT, f)).read(), re.M):
            if os.path.normpath(os.path.dirname(f)) not in run: lost.append(os.path.dirname(f))
    check('every Makefile with a test target is run by t0.sh', not lost, 'not run: %s' % lost)

def all_checks():
    del fails[:]
    made_up(); suites = real_tree(); every_makefile(suites)
    return list(fails)

QUIET = False
if __name__ == '__main__':
    bad = all_checks()
    QUIET = True
    orig_uncovered = ts.uncovered
    def no_transitive(self, changed): return set(changed)
    mutants = {
        'comments count': lambda: setattr(ts, 'code', lambda path, text: text),
        'no transitivity': lambda: setattr(ts.Graph, 'reach', no_transitive),
        'sub-makes ignored': lambda: setattr(ts, 'SUB_MAKE', re.compile(r'(?!x)x')),
        'a build counts as coverage': lambda: setattr(ts, 'uncovered', lambda g, s: orig_uncovered(g, [x.replace('build ', 'make ') for x in s])),
        'ambiguous names kept': lambda: setattr(ts, 'AMBIGUOUS_NEED_FOLDER', False),
    }
    for name, apply in mutants.items():
        saved = (ts.code, ts.Graph.reach, ts.SUB_MAKE, ts.uncovered, ts.AMBIGUOUS_NEED_FOLDER)
        apply()
        try: caught = bool(all_checks())
        except Exception: caught = True
        ts.code, ts.Graph.reach, ts.SUB_MAKE, ts.uncovered, ts.AMBIGUOUS_NEED_FOLDER = saved
        print('mutant %-28s %s' % (name, 'caught' if caught else 'NOT CAUGHT')); bad = bad or not caught
    print('ALL OK' if not bad else 'FAILED'); sys.exit(1 if bad else 0)
