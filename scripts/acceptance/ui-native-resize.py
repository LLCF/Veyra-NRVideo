"""Exercise the actual Windows modal sizing loop with bounded UI probes."""
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
        sys.exit(child.wait(timeout=75))
    except subprocess.TimeoutExpired:
        subprocess.run(['taskkill.exe', '/PID', str(child.pid), '/T', '/F'], capture_output=True, timeout=5)
        sys.exit('FAIL: native resize exceeded 75 seconds')
sys.argv.remove('--worker')
exe, output = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
output.mkdir(parents=True, exist_ok=True)
u = c.WinDLL('user32', use_last_error=True)
u.SetProcessDpiAwarenessContext.argtypes = [w.HANDLE]
u.SetProcessDpiAwarenessContext(c.c_void_p(-4))
u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
u.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
u.SendMessageTimeoutW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM, w.UINT, w.UINT, c.POINTER(c.c_size_t)]
visit_type = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
env = os.environ.copy()
env['VEYRA_LOG_FILE'] = str(output / 'app.log')
result = {'passed': False, 'cycles': []}
with (output / 'stdout.log').open('w') as stdout, (output / 'stderr.log').open('w') as stderr:
    process = subprocess.Popen([str(exe), '--smoke-view', 'pro', '--smoke-seconds', '60', *sys.argv[3:]], env=env, stdout=stdout, stderr=stderr)
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

        def probe(message=0):
            reply = c.c_size_t()
            assert process.poll() is None, f'application exited {process.returncode}'
            assert u.SendMessageTimeoutW(main, message, 0, 0, 2, 3000, c.byref(reply)), 'UI message timeout'

        time.sleep(5)
        for cycle in range(4):
            before = w.RECT()
            u.GetWindowRect(main, c.byref(before))
            # SC_SIZE + WMSZ_BOTTOMRIGHT enters DefWindowProc's modal loop.
            u.PostMessageW(main, 0x112, 0xF008, 0)
            time.sleep(.2)
            for step in range(24):
                u.PostMessageW(main, 0x100, (0x27 if cycle % 2 == 0 else 0x25), 0)
                u.PostMessageW(main, 0x100, (0x28 if cycle % 2 == 0 else 0x26), 0)
                time.sleep(.06)
                probe()
            u.PostMessageW(main, 0x100, 0x0D, 0)
            time.sleep(.4)
            probe()
            after = w.RECT()
            u.GetWindowRect(main, c.byref(after))
            sizes = [(before.right-before.left, before.bottom-before.top), (after.right-after.left, after.bottom-after.top)]
            assert sizes[0] != sizes[1], 'native sizing did not change window dimensions'
            result['cycles'].append(sizes)
        probe(0x10)
        assert process.wait(timeout=10) == 0
        # Vendor/media diagnostics may contain bytes in the active ANSI codepage.
        log = (output / 'app.log').read_text(encoding='utf-8', errors='replace')
        segments = log.split('interactive move begin;')[1:]
        assert len(segments) >= 4, 'native loop not observed'
        for segment in segments:
            moving, released = segment.split('interactive move end;', 1)
            # The end log follows removal of the move property. A renderer
            # thread can resize in that gap, so log ordering is not a fence.
            assert '[present-cost]' in moving, 'no presentation during native loop'
        result['passed'] = True
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=5)
        result['exitCode'] = process.returncode
        (output / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps(result))
