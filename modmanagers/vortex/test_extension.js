// T0 test of the Vortex extension against a stub vortex-api: what registerGame gets, and what the installer does with the release zip's layout.
//   node test_extension.js                run
//   node test_extension.js --mutant modpath|exe|steam|tool|installer   run with a deliberately broken copy of index.js: must FAIL (exit 1)
const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const vm = require('vm');

const dir = path.join(__dirname, 'game-thewitness');
let source = fs.readFileSync(path.join(dir, 'index.js'), 'utf8');
const mutant = process.argv[2] === '--mutant' ? process.argv[3] : null;
const mutants = {
  modpath: [`queryModPath: () => '.'`, `queryModPath: () => 'mods'`],
  exe: [`requiredFiles: [GAME_EXE]`, `requiredFiles: ['witness.exe']`],
  steam: [`const STEAM_ID = '210970'`, `const STEAM_ID = '210971'`],
  tool: [`executable: () => LAUNCHER_EXE`, `executable: () => GAME_EXE`],
  installer: [`destination: entry.path`, `destination: path.join(top, entry.path)`],
};
if (mutant) {
  const [from, to] = mutants[mutant];
  assert(source.includes(from), 'mutant does not apply: ' + mutant);
  source = source.replace(from, to);
}

const vortexApi = {
  util: { GameStoreHelper: { findByAppId: ids => Promise.resolve({ gamePath: 'C:/Games/Witness', ids }) } },
  fs: { readFileAsync: (file, opts) => fs.promises.readFile(file, opts) },
};
const mod = { exports: {} };
const sandboxRequire = name => (name === 'vortex-api' ? vortexApi : require(name));
vm.runInNewContext(`(function (module, exports, require, __dirname) {${source}\n})`, { Promise, process })(mod, mod.exports, sandboxRequire, dir);

(async () => {
  const games = [];
  const installers = [];
  const context = { registerGame: g => games.push(g), registerInstaller: (...a) => installers.push(a) };
  assert.strictEqual(mod.exports.default(context), true);

  assert.strictEqual(games.length, 1);
  const game = games[0];
  assert.strictEqual(game.id, 'thewitness');
  assert.strictEqual(game.name, 'The Witness');
  assert.strictEqual(game.mergeMods, true);
  assert.strictEqual(JSON.stringify(game.requiredFiles), '["witness64_d3d11.exe"]');
  assert.strictEqual(game.environment.SteamAPPId, '210970');
  assert.strictEqual(game.details.steamAppId, 210970);
  assert.strictEqual(game.queryModPath('C:/Games/Witness'), '.');
  assert(fs.existsSync(path.join(dir, game.logo)), 'logo file is missing');
  const found = await game.queryPath();
  assert.strictEqual(found, 'C:/Games/Witness');
  const tool = game.supportedTools.find(t => t.id === 'witness-coop-launcher');
  assert(tool, 'launcher tool');
  assert.strictEqual(tool.executable(), 'witness_launcher.exe');
  assert.strictEqual(JSON.stringify(tool.requiredFiles), '["witness_launcher.exe"]');

  // Play runs the launcher once the package is installed, the game itself before that
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'witness-vortex-'));
  assert.strictEqual(game.executable(tmp), 'witness64_d3d11.exe');
  fs.writeFileSync(path.join(tmp, 'witness_launcher.exe'), '');
  assert.strictEqual(game.executable(tmp), 'witness_launcher.exe');

  // the installer, on the release zip's layout: top folder + coop-mod.json at the root
  assert.strictEqual(installers.length, 1);
  const [, priority, test, install] = installers[0];
  assert(priority < 100, 'must run before the fallback installer');
  const zip = ['coop-mod.json', 'witness-coop-1/', 'witness-coop-1/coop-mod.json', 'witness-coop-1/dsound.dll', 'witness-coop-1/Witness-Coop-Setup.exe',
    'witness-coop-1/witness_launcher.exe', 'witness-coop-1/witness_models/', 'witness-coop-1/witness_models/adventurer.glb'];
  assert.strictEqual((await test(zip, 'thewitness')).supported, true);
  assert.strictEqual((await test(zip, 'skyrim')).supported, false);
  assert.strictEqual((await test(['readme.txt'], 'thewitness')).supported, false);
  fs.writeFileSync(path.join(tmp, 'coop-mod.json'), JSON.stringify({ files: [{ path: 'dsound.dll' }, { path: 'witness_launcher.exe' }, { path: 'witness_models/adventurer.glb' }] }));
  const { instructions } = await install(zip, tmp, 'thewitness', () => {});
  const norm = p => p.split(path.sep).join('/');
  assert.strictEqual(JSON.stringify(instructions.map(i => [norm(i.source), norm(i.destination), i.type])), JSON.stringify([
    ['witness-coop-1/dsound.dll', 'dsound.dll', 'copy'],
    ['witness-coop-1/witness_launcher.exe', 'witness_launcher.exe', 'copy'],
    ['witness-coop-1/witness_models/adventurer.glb', 'witness_models/adventurer.glb', 'copy'],
  ]));
  fs.rmSync(tmp, { recursive: true });
  console.log('vortex extension: ok' + (mutant ? ' (MUTANT SURVIVED: ' + mutant + ')' : ''));
})().catch(e => { console.error('FAIL:', e.message); process.exit(1); });
