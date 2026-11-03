// Vortex game extension for The Witness (and for the Witness Co-op package). Same shape as the extensions in Nexus-Mods/vortex-games (game-*/index.js).
// The co-op package mirrors the game folder (the system-DLL proxy dsound.dll, the launcher, witness_models/), so the mod path is the game folder itself.
const path = require('path');
const fs = require('fs');
const { util, fs: vfs } = require('vortex-api');

// Nexus Mods game domain.
const GAME_ID = 'thewitness';
const STEAM_ID = '210970';
const GOG_ID = '1461060839';
// Epic: no store id known yet; an Epic install is found by pointing Vortex at the folder

const GAME_EXE = 'witness64_d3d11.exe';
const LAUNCHER_EXE = 'witness_launcher.exe';
const MANIFEST = 'coop-mod.json';

function findGame() {
  return util.GameStoreHelper.findByAppId([STEAM_ID, GOG_ID]).then(game => game.gamePath);
}

// The launcher starts the game itself (and the co-op), so with the package installed it is what "Play" runs.
function executable(discoveredPath) {
  if (discoveredPath !== undefined && fs.existsSync(path.join(discoveredPath, LAUNCHER_EXE))) {
    return LAUNCHER_EXE;
  }
  return GAME_EXE;
}

// The release zip holds the package twice: a top folder and coop-mod.json at the zip root. The top folder also carries Setup and its scripts,
// which a mod manager must not deploy. coop-mod.json lists the files that belong in the game folder: those are copied, without the top folder.
function testSupportedContent(files, gameId) {
  const supported = (gameId === GAME_ID) && files.some(file => file.toLowerCase() === MANIFEST);
  return Promise.resolve({ supported, requiredFiles: [MANIFEST] });
}

function installContent(files, destinationPath, gameId, progressDelegate) {
  return vfs.readFileAsync(path.join(destinationPath, MANIFEST), { encoding: 'utf8' }).then(text => {
    const manifest = JSON.parse(text);
    const top = files.map(file => file.split(/[\\/]/)).filter(parts => parts.length > 1 && parts[parts.length - 1].toLowerCase() === MANIFEST)
      .map(parts => parts.slice(0, -1).join(path.sep))[0] || '';
    const instructions = manifest.files.map(entry => ({
      type: 'copy',
      source: top === '' ? entry.path : path.join(top, entry.path),
      destination: entry.path,
    }));
    return { instructions };
  });
}

function main(context) {
  context.registerGame({
    id: GAME_ID,
    name: 'The Witness',
    mergeMods: true,
    queryPath: findGame,
    queryModPath: () => '.',
    logo: 'gameart.jpg',
    executable,
    requiredFiles: [GAME_EXE],
    environment: { SteamAPPId: STEAM_ID },
    details: { steamAppId: parseInt(STEAM_ID, 10) },
    supportedTools: [
      {
        id: 'witness-coop-launcher',
        name: 'The Witness Co-op launcher',
        logo: 'gameart.jpg',
        executable: () => LAUNCHER_EXE,
        requiredFiles: [LAUNCHER_EXE],
        relative: true,
      },
    ],
  });
  context.registerInstaller('witness-coop-package', 25, testSupportedContent, installContent);
  return true;
}

module.exports = { default: main };
