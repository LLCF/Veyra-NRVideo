"""Bounded own-process DPI and real monitor transition regression."""
import ctypes as c
import ctypes.wintypes as w
import json
import os
from pathlib import Path
import subprocess
import sys
import time

if '--worker' not in sys.argv:
    child = subprocess.Popen([sys.executable, __file__, '--worker', *sys.argv[1:]])
    try:
        sys.exit(child.wait(timeout=120))
    except subprocess.TimeoutExpired:
        subprocess.run(['taskkill.exe', '/PID', str(child.pid), '/T', '/F'], capture_output=True, timeout=5)
        sys.exit('FAIL: cross-monitor test exceeded 120 seconds')
sys.argv.remove('--worker')

exe, output = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
output.mkdir(parents=True, exist_ok=True)
u = c.WinDLL('user32', use_last_error=True)
u.SetProcessDpiAwarenessContext.argtypes = [w.HANDLE]
u.SetProcessDpiAwarenessContext(c.c_void_p(-4))
u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
u.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
u.SetWindowPos.argtypes = [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT]
u.SendMessageTimeoutW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM, w.UINT, w.UINT, c.POINTER(c.c_size_t)]
visit_type = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
monitor_type = c.WINFUNCTYPE(w.BOOL, w.HANDLE, w.HDC, c.POINTER(w.RECT), w.LPARAM)
monitors = []

@monitor_type
def monitor_visit(handle, dc, rect, unused):
    r = rect.contents
    monitors.append([r.left, r.top, r.right, r.bottom])
    return True

u.EnumDisplayMonitors(None, None, monitor_visit, 0)
env = os.environ.copy()
env['VEYRA_LOG_FILE'] = str(output / 'app.log')
result = {'monitors': monitors, 'cases': [], 'passed': False}
media_args = sys.argv[3:] or ['--smoke-empty']
with (output / 'stdout.log').open('w') as stdout, (output / 'stderr.log').open('w') as stderr:
    process = subprocess.Popen([str(exe), '--smoke-view', 'pro', '--smoke-seconds', '90', *media_args], env=env, stdout=stdout, stderr=stderr)
    try:
        found = []

        @visit_type
        def visit(hwnd, unused):
            pid = w.DWORD()
            u.GetWindowThreadProcessId(hwnd, c.byref(pid))
            name = c.create_unicode_buffer(128)
            u.GetClassNameW(hwnd, name, 128)
            if pid.value == process.pid and name.value == 'VeyraApp':
                found.append(hwnd)
            return True

        end = time.monotonic() + 10
        while not found and time.monotonic() < end:
            u.EnumWindows(visit, 0)
            time.sleep(.1)
        assert found, 'main window missing'
        main = found[0]

        def send(message, first=0):
            reply = c.c_size_t()
            assert process.poll() is None, f'application exited {process.returncode}'
            assert u.SendMessageTimeoutW(main, message, first, 0, 2, 4000, c.byref(reply)), 'UI message timeout'

        time.sleep(5 if sys.argv[3:] else .3)
        for cycle in range(3):
            send(0x231)  # Native interactive move entry.
            for dpi in [96, 144, 192, 0]:
                send(0x8000 + 90, dpi)
                assert u.SetWindowPos(main, None, 30, 30, 960+cycle*80+dpi, 640+cycle*40, 0x14)
                time.sleep(.2)
                result['cases'].append({'cycle': cycle, 'syntheticDpi': dpi})
            for rect in monitors:
                assert u.SetWindowPos(main, None, rect[0]+30, rect[1]+30, 960+cycle*80, 640+cycle*40, 0x14)
                time.sleep(.3)
                send(0)
                result['cases'].append({'cycle': cycle, 'monitor': rect})
            send(0x232)
            time.sleep(.4)
        send(0x10)
        assert process.wait(timeout=10) == 0
        if sys.argv[3:]:
            log = (output / 'app.log').read_text(encoding='utf-8')
            assert '[present-cost]' in log, 'no actual presentation evidence'
            for segment in log.split('interactive move begin;')[1:]:
                moving, released = segment.split('interactive move end;', 1)
                assert 'present-sink: resized' not in moving, 'buffers rebuilt during move'
                assert '[present-cost]' in moving, 'no presentation observed during move'
            assert 'present-sink: resized' in released, 'final resize did not resume'
        result['passed'] = True
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=5)
        result['exitCode'] = process.returncode
        (output / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps(result))
