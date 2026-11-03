# Mod Organizer 2 basic_games plugin for The Witness and the Witness Co-op package.
# Same shape as ModOrganizer2/modorganizer-basic_games/games/game_*.py: copy this file to MO2's plugins/basic_games/games/.
from PyQt6.QtCore import QFileInfo

import mobase

from ..basic_game import BasicGame

PROXY = "dsound.dll"


class WitnessCoopModDataChecker(mobase.ModDataChecker):
    """The co-op package mirrors the game folder (dsound.dll, witness_launcher.exe, witness_models/). The release zip
    has one top folder around it: that folder is moved up, so no data directory has to be chosen by hand."""

    def dataLooksValid(
        self, filetree: mobase.IFileTree
    ) -> mobase.ModDataChecker.CheckReturn:
        if filetree.exists(PROXY, mobase.IFileTree.FILE):
            return mobase.ModDataChecker.VALID
        if self._package_folder(filetree) is not None:
            return mobase.ModDataChecker.FIXABLE
        return mobase.ModDataChecker.INVALID

    def fix(self, filetree: mobase.IFileTree) -> mobase.IFileTree:
        folder = self._package_folder(filetree)
        return folder if folder is not None else filetree

    @staticmethod
    def _package_folder(filetree: mobase.IFileTree):
        for entry in filetree:
            if isinstance(entry, mobase.IFileTree) and entry.exists(
                PROXY, mobase.IFileTree.FILE
            ):
                return entry
        return None


class WitnessGame(BasicGame):
    Name = "The Witness Support Plugin"
    Author = "The Witness Co-op"
    Version = "1.0.0"

    GameName = "The Witness"
    GameShortName = "thewitness"
    GameNexusName = "thewitness"
    GameSteamId = 210970
    GameGogId = 1461060839
    GameBinary = "witness64_d3d11.exe"
    GameLauncher = "witness_launcher.exe"
    GameDataPath = ""
    GameSupportURL = "https://github.com/TommoDotCommo/witness-coop#mod-managers"

    def init(self, organizer: mobase.IOrganizer):
        super().init(organizer)
        self._register_feature(WitnessCoopModDataChecker())
        return True

    def executables(self):
        return [
            mobase.ExecutableInfo(
                "The Witness Co-op launcher",
                QFileInfo(self.gameDirectory(), "witness_launcher.exe"),
            ),
            mobase.ExecutableInfo(
                "The Witness",
                QFileInfo(self.gameDirectory(), "witness64_d3d11.exe"),
            ),
        ]
