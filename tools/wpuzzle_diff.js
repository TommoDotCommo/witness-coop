// jbzdarkid's own solver (witnesspuzzles.com's engine) on the same puzzles as wpuzzle-cli, for tools/wpuzzle_diff.py.
//   node tools/wpuzzle_diff.js <his repository>/engine MAX < puzzles.jsonl
// Prints per puzzle: the number of solutions (up to MAX) and the first one as positions, as wpuzzle-cli does.
const fs = require('fs'), vm = require('vm'), path = require('path'), readline = require('readline')
const dir = process.argv[2], MAX = parseInt(process.argv[3] || '100')
global.window = global
global.namespace = f => f()
for (const k of ['log', 'debug', 'info', 'group', 'groupEnd', 'spam', 'warn']) console[k] = () => {}
Object.assign(global, {LINE_NONE: 0, LINE_BLACK: 1, LINE_BLUE: 2, LINE_YELLOW: 3, DOT_NONE: 0, DOT_BLACK: 1, DOT_BLUE: 2, DOT_YELLOW: 3, DOT_INVISIBLE: 4,
  GAP_NONE: 0, GAP_BREAK: 1, GAP_FULL: 2})
for (const f of ['puzzle.js', 'polyominos.js', 'validate.js', 'solve.js']) vm.runInThisContext(fs.readFileSync(path.join(dir, f), 'utf8'), {filename: f})
function solveOne(json) {
  return new Promise(resolve => {
    let p
    try { p = Puzzle.deserialize(json) } catch (e) { return resolve('error') }
    window.MAX_SOLUTIONS = MAX
    window.solve(p, null, paths => {
      let out = '' + paths.length
      if (paths.length) {
        const s = paths[0]; let x = s[0].x, y = s[0].y; const pos = [x + ',' + y]
        for (let i = 1; i < s.length; i++) {
          const d = s[i]; if (d === 0) break
          if (d === 1) x--; else if (d === 2) x++; else if (d === 3) y--; else if (d === 4) y++
          pos.push(p._mod(x) + ',' + y)
        }
        out += ' ' + pos.join(' ')
      }
      resolve(out)
    })
  })
}
;(async () => {
  const rl = readline.createInterface({input: process.stdin})
  for await (const line of rl) process.stdout.write((await solveOne(line)) + '\n')
})()
