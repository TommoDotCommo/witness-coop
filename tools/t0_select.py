#!/usr/bin/env python3
"""Which game-free suites of tools/t0.sh a change touches, and which source files no suite touches at all.

A file reaches a suite through what mentions it in code (comments and docstrings do not count): a C/C++ file through #include and the
Makefiles and scripts that name it, a Python module through `import` and the scripts that name it, and so on, transitively (a header
included by a header the launcher includes reaches `make launcher`). A make suite owns its folder and the folders its Makefile runs (`$(MAKE) -C ../model`); a Python test owns itself.
Only source files take part (C, C++, Python, PowerShell, shell, Makefiles); a basename that more than one tracked file has (Makefile, test.cpp)
counts only where it is named with its folder ("tv/test.cpp") or from its own folder.

  python3 tools/t0_select.py --changed [REF] -- SUITE...      the suites (of those given, as t0.sh names them) that the changes since the merge
                                                              base with REF (default main), plus uncommitted and untracked files, reach
  python3 tools/t0_select.py --files F... -- SUITE...        the same for the files given
  python3 tools/t0_select.py --uncovered -- SUITE...         the source files no test suite reaches, by folder (a map of what is untested)
"""
import os, re, subprocess, sys
from collections import defaultdict

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
SOURCE = re.compile(r'(\.(cpp|cc|h|hpp|c|py|ps1|sh|mk)|(^|/)Makefile)$')
TOKEN = re.compile(r'[\w.+-]+')
IMPORT = re.compile(r'^\s*(?:from\s+([\w.]+)\s+import|import\s+([\w., ]+))', re.M)
AMBIGUOUS_NEED_FOLDER = True      # a basename more than one file has counts only with its folder or from its own folder
SUB_MAKE = re.compile(r'\$\(MAKE\)\s+(?:-s\s+)?-C\s+(\S+)')
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]', re.M)
DOCSTRING = re.compile(r'"""[\s\S]*?"""')
COMMENT = re.compile(r'(^|\s)#(?!include)[^\n]*')


def code(path, text):
    """The part of a file that can make it depend on another: #include lines of C and C++ (a comment naming a tool is no dependency), the rest
    of a script or Makefile without its comments (and a Python file without its docstrings)."""
    if re.search(r'\.(cpp|cc|h|hpp|c)$', path): return '\n'.join(INCLUDE.findall(text))
    if path.endswith('.py'): text = DOCSTRING.sub('', text)
    return COMMENT.sub('', text)


def git(*args):
    return subprocess.run(['git', '-C', ROOT, *args], capture_output=True, text=True).stdout.split('\n')


class Graph:
    """Who mentions whom among the tracked source files (third_party and local/ left out)."""
    def __init__(self, files=None):
        self.files = sorted(f for f in (files if files is not None else git('ls-files'))
                            if f and SOURCE.search(f) and 'third_party/' not in f and not f.startswith('local/'))
        self.text = {}
        for f in self.files:
            try: self.text[f] = open(os.path.join(ROOT, f), errors='replace').read()
            except OSError: self.text[f] = ''
        by_base = defaultdict(list)
        for f in self.files: by_base[os.path.basename(f)].append(f)
        self.by_base = by_base
        self.mentioned_by = defaultdict(set)      # file -> the files that mention it
        for x in self.files:
            t = code(x, self.text[x]); tokens = set(TOKEN.findall(t))
            mods = set()
            if x.endswith('.py'):
                for a, b in IMPORT.findall(t):
                    for m in (a or b).split(','):
                        m = m.strip().split(' as ')[0].strip()
                        if m: mods.add(m.split('.')[-1]); mods.add(m.split('.')[0])
            for base, owners in by_base.items():
                stem = base[:-3] if base.endswith('.py') else None
                hit = base in tokens or (stem is not None and stem in mods)
                if not hit: continue
                for y in owners:
                    if y == x: continue
                    if AMBIGUOUS_NEED_FOLDER and len(owners) > 1 and os.path.dirname(y) != os.path.dirname(x) and \
                            (os.path.basename(os.path.dirname(y)) + '/' + base) not in t:
                        continue      # an ambiguous name, not named with its folder
                    self.mentioned_by[y].add(x)

    def reach(self, changed):
        """Every file a change of `changed` can affect: the files themselves and, transitively, whatever mentions them."""
        seen = set(); todo = []
        for f in changed:
            if f in self.text: todo.append(f)
            else:      # a file that is not a tracked source (deleted, or another kind): whatever names it by its basename
                seen.add(f)
                for y, t in self.text.items():
                    if os.path.basename(f) in set(TOKEN.findall(code(y, t))): todo.append(y)
        while todo:
            f = todo.pop()
            if f in seen: continue
            seen.add(f); todo.extend(self.mentioned_by.get(f, ()))
        return seen


def owned(suite):
    """(folders, files) a t0.sh suite owns: 'make D' owns D and the folders its Makefile runs ('build D', a compile check, owns D); a Python
    test owns its file."""
    if suite.startswith('build '): return {os.path.normpath(suite[6:].strip())}, set()
    if suite.startswith('make '):
        dirs, todo = set(), [suite[5:].strip()]
        while todo:
            d = os.path.normpath(todo.pop())
            if d in dirs: continue
            dirs.add(d)
            try: mk = open(os.path.join(ROOT, d, 'Makefile')).read()
            except OSError: continue
            todo += [os.path.join(d, c) for c in SUB_MAKE.findall(mk)]
        return dirs, set()
    return set(), {suite}


def select(graph, changed, suites):
    hit = graph.reach(changed); out = []
    for s in suites:
        dirs, files = owned(s)
        if files & hit or any(f.startswith(d + '/') for f in hit for d in dirs): out.append(s)
    return out


def changed_since(ref):
    base = (git('merge-base', ref, 'HEAD') or [''])[0].strip() or ref
    names = git('diff', '--name-only', base + '...HEAD') + git('diff', '--name-only', 'HEAD') + git('ls-files', '--others', '--exclude-standard')
    return sorted({n for n in names if n})


def uncovered(graph, suites):
    """The source files (tests and test scaffolding left out) that no suite reaches (a compile check, 'build D', is not a test of them)."""
    own = [owned(s) for s in suites if not s.startswith('build ')]
    def covered(f):
        hit = graph.reach([f])
        return any(fs & hit or any(h.startswith(d + '/') for h in hit for d in ds) for ds, fs in own)
    return [f for f in graph.files if '/tests/' not in f and not os.path.basename(f).startswith('test_') and not covered(f)]


def main(argv):
    if '--' not in argv: print(__doc__); return 2
    k = argv.index('--'); opts, suites = argv[:k], argv[k + 1:]
    g = Graph()
    if opts[:1] == ['--uncovered']:
        bad = uncovered(g, suites); by = defaultdict(list)
        for f in bad: by[os.path.dirname(f) or '.'].append(os.path.basename(f))
        for d in sorted(by): print('%-28s %3d  %s' % (d, len(by[d]), ' '.join(sorted(by[d]))))
        print('%d of %d source files reach no game-free suite' % (len(bad), len(g.files)))
        return 0
    if opts[:1] == ['--changed']: files = changed_since(opts[1] if len(opts) > 1 else 'main')
    elif opts[:1] == ['--files']: files = opts[1:]
    else: print(__doc__); return 2
    for s in select(g, files, suites): print(s)
    return 0


if __name__ == '__main__': sys.exit(main(sys.argv[1:]))
