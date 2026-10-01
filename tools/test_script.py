#!/usr/bin/env python3
"""Run a scripted test build of the game in Xemu and save a screenshot.

  python3 tools/test_script.py "D44 U1 N1 A1 N6 A1" 60 /tmp/shot.png [level]

The script is a list of <buttons><ticks> steps: U D L R A S (S = start; combine, e.g. RA) or N for none. The game runs from the given
inning with the same caves every time, takes its input from the script, and the screenshot is taken after the given number
of ticks. Needs cl65 with the mega65 target (CC65_HOME), Xemu (XEMU) and a MEGA65 ROM image (M65_ROM), like run-xemu.sh.
"""
import subprocess, sys, os
spec, ticks, out = sys.argv[1], sys.argv[2], sys.argv[3]
level = sys.argv[4] if len(sys.argv) > 4 else '1'
M = {'U': 1, 'D': 2, 'L': 4, 'R': 8, 'A': 16, 'S': 32, 'N': 0}
pairs = []
for tok in spec.split():
    m = 0
    for ch in tok.rstrip('0123456789'):
        m |= M[ch]
    pairs.append((m, int(tok.lstrip('UDLRASN'))))
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
os.makedirs(root + '/build/inc', exist_ok=True)
open(root + '/build/inc/testscript.h', 'w').write('static const unsigned char script[][2] = {\n' + ''.join('{%d,%d},' % p for p in pairs) + '{0,0}};\n')
env = dict(os.environ)
if 'CC65_HOME' in env: env['PATH'] = env['CC65_HOME'] + '/bin:' + env['PATH']
subprocess.run(['make', 'build/data.d81'], cwd=root, env=env, check=True, capture_output=True)
subprocess.run(['cl65', '-t', 'mega65', '-C', 'cfg/dugout.cfg', '-O', '-I', 'build/inc', '-DTEST_EXIT', '-DTEST_SCRIPT', '-DTEST_TICKS=' + ticks, '-DTEST_LEVEL=' + level] + (['-DTEST_TITLE'] if os.environ.get('TEST_TITLE') else []) + (['-DTEST_START=' + os.environ['TEST_START']] if os.environ.get('TEST_START') else []) + (['-DTEST_LIVES=' + os.environ['TEST_LIVES']] if os.environ.get('TEST_LIVES') else []) + [
                '-o', 'build/script.prg', 'src/early.s', 'src/blit.s', 'src/sound.s', 'src/main.c', 'src/platform.c', 'src/render.c', 'src/game.c', 'src/data.c', 'src/sprites.c'],
               cwd=root, env=env, check=True, capture_output=True)
if os.path.exists(out): os.remove(out)
subprocess.run(['timeout', '-s', 'KILL', '60', os.environ.get('XEMU', os.path.expanduser('~/Desktop/xemu-master/build/apps/xmega65.app/Contents/MacOS/xmega65')),
                '-besure', '-fastboot', '-testing', '-sleepless', '-rom', os.environ.get('M65_ROM', os.path.expanduser('~/Desktop/920413.bin')),
                '-8', 'build/data.d81', '-prg', 'build/script.prg', '-screenshot', out], cwd=root, capture_output=True)
print('wrote', out, os.path.exists(out))
