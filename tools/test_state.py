import os, subprocess, sys
"""Run a scripted test build in Xemu and print the game's state at the end: the enemies, headstones, lives and so on.

  python3 tools/test_state.py "-DTEST_LEVEL=9 -DTEST_BOSS" 40 "N1 A1 N400"

Arguments: the compiler defines (see src/main.c for the TEST_ ones: TEST_REVIVE, TEST_STOMP, TEST_ALONE, TEST_BOSS,
TEST_BOSS_STRIKES, TEST_BOSS_DIST, TEST_LEVEL, TEST_LIVES), the number of ticks to run, and optionally a button script like
tools/test_script.py takes. Needs the same tools as that one (CC65_HOME, XEMU, M65_ROM).
"""
defs, ticks = sys.argv[1].split(), sys.argv[2]
spec = sys.argv[3] if len(sys.argv) > 3 else None
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
S = os.path.join(root, 'build')
env = dict(os.environ)
if 'CC65_HOME' in env: env['PATH'] = env['CC65_HOME'] + '/bin:' + env['PATH']
subprocess.run(['make', 'build/data.d81'], cwd=root, env=env, check=True, capture_output=True)
if spec:
    M = {'U': 1, 'D': 2, 'L': 4, 'R': 8, 'A': 16, 'S': 32, 'N': 0}
    pairs = []
    for tok in spec.split():
        m = 0
        for ch in tok.rstrip('0123456789'): m |= M[ch]
        pairs.append((m, int(tok.lstrip('UDLRASN'))))
    os.makedirs(root + '/build/inc', exist_ok=True)
    open(root + '/build/inc/testscript.h', 'w').write('static const unsigned char script[][2] = {\n' + ''.join('{%d,%d},' % p for p in pairs) + '{0,0}};\n')
    defs = defs + ['-DTEST_SCRIPT', '-I', 'build/inc']
r = subprocess.run(['cl65', '-t', 'mega65', '-C', 'cfg/dugout.cfg', '-O', '-DTEST_EXIT', '-DTEST_TICKS=' + ticks] + defs + ['-Ln', S + '/labels.txt', '-o', 'build/vars.prg', 'src/early.s', 'src/blit.s', 'src/sound.s', 'src/main.c', 'src/platform.c', 'src/render.c', 'src/game.c', 'src/data.c', 'src/sprites.c'], cwd=root, env=env, capture_output=True, text=True)
if r.returncode: print(r.stderr); sys.exit(1)
if os.path.exists(S + '/mem.bin'): os.remove(S + '/mem.bin')
subprocess.run(['timeout', '-s', 'KILL', '120', os.environ.get('XEMU', os.path.expanduser('~/Desktop/xemu-master/build/apps/xmega65.app/Contents/MacOS/xmega65')), '-besure', '-fastboot', '-testing', '-sleepless', '-rom', os.environ.get('M65_ROM', os.path.expanduser('~/Desktop/920413.bin')), '-8', 'build/data.d81', '-prg', 'build/vars.prg', '-dumpmem', S + '/mem.bin'], cwd=root, capture_output=True)
m = {}
for l in open(S + '/labels.txt'):
    p = l.split()
    if len(p) >= 3 and p[0] == 'al': m[p[2]] = int(p[1], 16)
d = open(S + '/mem.bin', 'rb').read()
g = lambda n, i=0: d[m['._' + n] + i]
ES = ['NONE', 'WALK', 'GHOST', 'INFL', 'POP', 'SQUASH', 'FLAME']
print('score', (g('score_h') | (g('score_h', 1) << 8)) * 100 + g('score_t') * 10, 'state', g('state'), 'level', g('level'), 'lives', g('lives'), 'enemies_left', g('enemies_left'), 'Doug', g('px'), g('py'))
for i in range(6):
    print(' enemy %d type %d %-6s at (%3d,%3d) strikes %d timer %d' % (i, g('e_type', i), ES[g('e_state', i)], g('e_x', i), g('e_y', i), g('e_infl', i), g('e_timer', i)))
if os.environ.get('PRINT_MAP'):
    print(' map (. = dug, # = dirt, 2 = boulder cell):')
    for r in range(14):
        print('  ' + ''.join('.' if g('map', r * 16 + c) == 0 else '#' if g('map', r * 16 + c) == 1 else '2' for c in range(14)))
for i in range(3):
    if g('c_on', i): print(' headstone', i, 'of enemy', g('c_slot', i), 'at', g('c_x', i), g('c_y', i))
