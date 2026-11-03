#!/usr/bin/env python3
"""T0 test of the MO2 plugin against stub mobase / PyQt6 / basic_game modules (no MO2 needed).
  test_game_thewitness.py                run
  test_game_thewitness.py --mutant root|exe|steam|launcher|checker   run on a deliberately broken copy: must FAIL (exit 1)"""
import importlib.util
from pathlib import Path
import sys
import types

HERE = Path(__file__).resolve().parent
MUTANTS = {
    'root': ('GameDataPath = ""', 'GameDataPath = "data"'),
    'exe': ('GameBinary = "witness64_d3d11.exe"', 'GameBinary = "witness.exe"'),
    'steam': ('GameSteamId = 210970', 'GameSteamId = 210971'),
    'launcher': ('"The Witness Co-op launcher",\n                QFileInfo(self.gameDirectory(), "witness_launcher.exe")',
                 '"The Witness Co-op launcher",\n                QFileInfo(self.gameDirectory(), "witness64_d3d11.exe")'),
    'checker': ('if filetree.exists(PROXY, mobase.IFileTree.FILE):\n            return mobase.ModDataChecker.VALID',
                'if False:\n            return mobase.ModDataChecker.VALID'),
}


class Tree(list):
    """stub mobase.IFileTree: a folder with entries; a str entry is a file"""
    FILE = 'file'

    def __init__(self, name, entries):
        super().__init__(entries)
        self._name = name

    def exists(self, name, kind=None):
        return name in [e for e in self if isinstance(e, str)]


def install_stubs():
    mobase = types.ModuleType('mobase')

    class ModDataChecker:
        VALID, FIXABLE, INVALID = 'valid', 'fixable', 'invalid'
        CheckReturn = str

    class ExecutableInfo:
        def __init__(self, title, info):
            self.title, self.info = title, info

    mobase.ModDataChecker, mobase.ExecutableInfo, mobase.IFileTree = ModDataChecker, ExecutableInfo, Tree
    mobase.IOrganizer = object
    qt = types.ModuleType('PyQt6'); core = types.ModuleType('PyQt6.QtCore')

    class QFileInfo:
        def __init__(self, directory, name):
            self.path = '%s/%s' % (directory, name)

    core.QFileInfo = QFileInfo
    pkg = types.ModuleType('basic_games'); pkg.__path__ = []
    games = types.ModuleType('basic_games.games'); games.__path__ = []
    bg = types.ModuleType('basic_games.basic_game')

    class BasicGame:
        def init(self, organizer):
            self.features = []
            return True

        def _register_feature(self, feature):
            self.features.append(feature)
            return True

        def gameDirectory(self):
            return 'C:/Games/Witness'

    bg.BasicGame = BasicGame
    sys.modules.update({'mobase': mobase, 'PyQt6': qt, 'PyQt6.QtCore': core, 'basic_games': pkg, 'basic_games.games': games, 'basic_games.basic_game': bg})
    return mobase


def main():
    mutant = sys.argv[2] if len(sys.argv) > 2 and sys.argv[1] == '--mutant' else None
    source = (HERE / 'game_thewitness.py').read_text()
    if mutant:
        old, new = MUTANTS[mutant]
        assert old in source, 'mutant does not apply: ' + mutant
        source = source.replace(old, new)
    mobase = install_stubs()
    module = types.ModuleType('basic_games.games.game_thewitness')
    module.__package__ = 'basic_games.games'
    exec(compile(source, 'game_thewitness.py', 'exec'), module.__dict__)

    cls = module.WitnessGame
    assert cls.GameName == 'The Witness'
    assert cls.GameShortName == 'thewitness' and cls.GameNexusName == 'thewitness'
    assert cls.GameSteamId == 210970
    assert cls.GameBinary == 'witness64_d3d11.exe'
    assert cls.GameLauncher == 'witness_launcher.exe'
    assert cls.GameDataPath == '', 'the package goes into the game folder'
    game = cls()
    assert game.init(object()) is True and len(game.features) == 1
    names = {e.title: e.info.path for e in game.executables()}
    assert names == {'The Witness Co-op launcher': 'C:/Games/Witness/witness_launcher.exe', 'The Witness': 'C:/Games/Witness/witness64_d3d11.exe'}, names

    checker = game.features[0]
    flat = Tree('mod', ['dsound.dll', 'witness_launcher.exe'])
    boxed = Tree('mod', [Tree('witness-coop-1', ['dsound.dll', 'Witness-Coop-Setup.exe']), 'coop-mod.json'])
    other = Tree('mod', ['readme.txt'])
    assert checker.dataLooksValid(flat) == 'valid'
    assert checker.dataLooksValid(boxed) == 'fixable' and checker.fix(boxed) is boxed[0]
    assert checker.dataLooksValid(other) == 'invalid'
    print('mo2 plugin: ok')


try:
    main()
except AssertionError as e:
    print('FAIL:', e)
    sys.exit(1)
