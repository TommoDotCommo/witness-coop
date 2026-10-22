"""Command-file input shared by Windows-side bots. No desktop input or focus changes."""
import os, time

class Control:
    def __init__(self, game_dir):
        self.path = os.path.join(game_dir, 'witness_cmd.txt')

    def wait_taken(self):
        end = time.monotonic() + 8
        while os.path.exists(self.path):
            if time.monotonic() >= end: raise TimeoutError('proxy did not take command file')
            time.sleep(0.05)

    def send(self, lines, settle=0.0):
        self.wait_taken()
        tmp = self.path + '.tmp'
        with open(tmp, 'w') as f: f.write('\n'.join(lines) + '\n')
        os.replace(tmp, self.path)
        self.wait_taken()
        time.sleep(settle)

    def click(self, hold=0.15, settle=0.3):
        self.send(['button L down'], hold)
        self.send(['button L up'], settle)
