#!/usr/bin/env python3
"""Compare repeated partial repaints with a full repaint in nested Hyprland.
Requires Hyprland/Kitty, clang++, pkg-config and numpy; uses the existing Wayland
session only as a host. All control commands target the child compositor.
Usage: python tests/static-wallpaper.py /absolute/path/to/hyprglass.so
"""
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import time
import numpy as np

plugin = Path(sys.argv[1]).resolve()
root = Path(tempfile.mkdtemp(prefix='hyprglass-damage-test-'))
print('Artifacts:', root, flush=True)
probe = root / 'probe.so'
flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'hyprland', 'pixman-1', 'libdrm'], text=True))
subprocess.run(['clang++', '-shared', '-fPIC', '-std=c++23', str(Path(__file__).with_name('render-probe.cpp')), '-o', str(probe), *flags], check=True)
config = root / 'hyprland.lua'
config.write_text('''
hl.monitor({output="",mode="preferred",position="0x0",scale=1})
hl.config({animations={enabled=false},general={border_size=0},debug={disable_logs=false},
 decoration={rounding=14,blur={size=6,passes=3,new_optimizations=false}},
 misc={disable_hyprland_logo=false,disable_splash_rendering=true}})
hl.window_rule({match={class="^hyprglass-flicker-test$"},float=true,size="420 450",move="100 100"})
if hl.plugin.hyprglass then
 hl.plugin.hyprglass.config({enabled=1,blur_strength=0.4,blur_iterations=1,
  refraction_strength=5.0,chromatic_aberration=0.45,lens_distortion=0.95,
  edge_thickness=0.05,fresnel_strength=0.4,specular_strength=1.0,tint_color=0,
  glass_opacity=1,brightness=1,contrast=1,saturation=1,vibrancy=0,
  adaptive_dim=0.1,adaptive_boost=0,layers={enabled=false}})
end
''')
env = os.environ.copy()
assert env.get('WAYLAND_DISPLAY'), 'Run inside the existing Wayland session'
env.update(HYPRLAND_NO_SD_VARS='1', HYPRLAND_NO_SD_NOTIFY='1', HYPRLAND_NO_RT='1', HYPRGLASS_PROBE_DIR=str(root))
log = open(root / 'compositor.log', 'w')
child = subprocess.Popen(['Hyprland', '-c', str(config)], env=env, stdout=log, stderr=subprocess.STDOUT)
kitty = None
try:
    for _ in range(100):
        assert child.poll() is None, f'Nested compositor exited: {child.returncode}'
        instances = json.loads(subprocess.check_output(['hyprctl', 'instances', '-j'], text=True))
        instance = next((i for i in instances if i['pid'] == child.pid), None)
        if instance:
            break
        time.sleep(0.1)
    else:
        raise AssertionError('Nested compositor did not start')
    def ctl(*args):
        return subprocess.check_output(['hyprctl', '-i', instance['instance'], *args], text=True, timeout=10).strip()
    time.sleep(1)
    assert ctl('plugin', 'load', str(plugin)) == 'ok'
    assert ctl('plugin', 'load', str(probe)) == 'ok'
    assert not ctl('configerrors'), ctl('configerrors')
    assert json.loads(ctl('-j', 'getoption', 'debug:damage_tracking'))['int'] == 2
    env.update(WAYLAND_DISPLAY=instance['wl_socket'], HYPRLAND_INSTANCE_SIGNATURE=instance['instance'])
    env.pop('DISPLAY', None)
    kitty = subprocess.Popen(['kitty', '--config', 'NONE', '--class', 'hyprglass-flicker-test',
        '--override', 'background_opacity=0.7', '--override', 'cursor_blink_interval=0',
        'sh', '-c', 'printf "Static glass test"; sleep 120'], env=env, stdout=log, stderr=subprocess.STDOUT)
    for _ in range(100):
        windows = json.loads(ctl('-j', 'clients'))
        window = next((w for w in windows if w['class'] == 'hyprglass-flicker-test'), None)
        if window:
            break
        time.sleep(0.1)
    assert window
    time.sleep(3)
    window = next(w for w in json.loads(ctl('-j', 'clients')) if w['class'] == 'hyprglass-flicker-test')
    print('Window:', window['at'], window['size'], flush=True)
    def capture(mode):
        ident = ctl('glassprobe', mode)
        meta = root / (ident + '.txt')
        for _ in range(100):
            if meta.exists() and meta.stat().st_size:
                break
            time.sleep(0.02)
        lines = meta.read_text().splitlines()
        width, height = map(int, lines[0].split())
        pixels = np.fromfile(root / (ident + '.rgba'), dtype=np.uint8).reshape(height, width, 4)[::-1, :, :3]
        return pixels
    capture('full')
    reference = capture('full')
    x, y = window['at']; w, h = window['size']
    # This rim lies in the presented repaint halo around the 8x8 damage.
    # No content changed, so refraction must reproduce the full repaint.
    region = np.s_[y+25:y+85, x+8:x+38, :]
    assert reference[region].size, 'Test window must fit inside the nested output'
    diffs = []
    for _ in range(6):
        frame = capture('partial')
        diff = np.abs(frame[region].astype(int) - reference[region].astype(int))
        diffs.append(int(diff.max()))
        time.sleep(0.1)
    print('Maximum RGB change on stationary glass rim:', diffs, flush=True)
    assert max(diffs) <= 1, 'Partial redraw corrupts stationary glass in the presented repaint area'
    assert ctl('plugin', 'unload', str(probe)) == 'ok'
    assert ctl('plugin', 'unload', str(plugin)) == 'ok'
    assert ctl('reload') == 'ok'
    assert not ctl('configerrors')
    print('PASS: partial repaints preserve stationary glass; unload/reload clean', flush=True)
finally:
    if kitty:
        kitty.terminate()
        kitty.wait(timeout=5)
    child.terminate()
    try:
        child.wait(timeout=8)
    except subprocess.TimeoutExpired:
        child.kill(); child.wait()
    log.close()
