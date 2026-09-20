"""Own-process display/DPI regression; reports physical monitor coverage separately."""
import ctypes as c
import ctypes.wintypes as w
import json
import os
from pathlib import Path
import subprocess
import sys
import time

exe, output = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
output.mkdir(parents=True, exist_ok=True)
u = c.WinDLL('user32', use_last_error=True)
u.SetProcessDpiAwarenessContext.argtypes = [w.HANDLE]
u.SetProcessDpiAwarenessContext(c.c_void_p(-4))
u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
u.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
u.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
u.SetWindowPos.argtypes = [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT]
u.SendMessageTimeoutW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM, w.UINT, w.UINT, c.POINTER(c.c_size_t)]
u.GetPropW.argtypes = [w.HWND, w.LPCWSTR]
u.GetPropW.restype = w.HANDLE
visit_type = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
monitor_type = c.WINFUNCTYPE(w.BOOL, w.HANDLE, w.HDC, c.POINTER(w.RECT), w.LPARAM)
monitors = []

@monitor_type
def monitor_visit(handle, dc, rect, data):
    r = rect.contents
    monitors.append([r.left, r.top, r.right, r.bottom])
    return True

u.EnumDisplayMonitors(None, None, monitor_visit, 0)
result = {'passed': False, 'monitors': monitors, 'physicalCrossMonitorTested': len(monitors) > 1}
env = os.environ.copy()
env['VEYRA_LOG_FILE'] = str(output / 'app.log')
with (output / 'stdout.log').open('w') as stdout, (output / 'stderr.log').open('w') as stderr:
    process = subprocess.Popen([str(exe), '--smoke-view', 'pro', '--smoke-seconds', '60', *sys.argv[3:]], env=env, stdout=stdout, stderr=stderr)
    try:
        found = []

        @visit_type
        def visit(hwnd, data):
            pid = w.DWORD()
            u.GetWindowThreadProcessId(hwnd, c.byref(pid))
            name = c.create_unicode_buffer(128)
            u.GetClassNameW(hwnd, name, 128)
            if pid.value == process.pid and name.value == 'VeyraApp':
                found.append(hwnd)
            return True

        deadline = time.monotonic() + 10
        while not found and time.monotonic() < deadline:
            u.EnumWindows(visit, 0)
            time.sleep(.1)
        assert found, 'main window missing'
        main = found[0]

        def send(message=0, wp=0, lp=0):
            reply = c.c_size_t()
            assert process.poll() is None, f'app exited {process.returncode}'
            assert u.SendMessageTimeoutW(main, message, wp, lp, 2, 3000, c.byref(reply)), 'UI timeout'

        time.sleep(5)
        for cycle, dpi in enumerate([144, 192, 96, 0]):
            send(0x231)  # WM_ENTERSIZEMOVE
            if len(monitors) > 1:
                r = monitors[cycle % len(monitors)]
                assert u.SetWindowPos(main, None, r[0]+20, r[1]+20, 0, 0, 0x15)
            # Test both the real WM_DPICHANGED suggested-rectangle path and
            # the explicit layout-DPI harness (not physical HDR/adapter coverage).
            rect = w.RECT()
            u.GetWindowRect(main, c.byref(rect))
            actual = dpi or 96
            send(0x2E0, actual | (actual << 16), c.addressof(rect))
            send(0x8000+90, dpi)
            time.sleep(1)
            send(0x232)  # WM_EXITSIZEMOVE
            time.sleep(1)
            send()
        send(0x10)
        assert process.wait(timeout=12) == 0
        log = (output / 'app.log').read_text(encoding='utf-8', errors='replace')
        segments = log.split('interactive move begin;')[1:]
        assert len(segments) == 4, 'missing move cycles'
        for segment in segments:
            assert 'DPI transition complete' in segment, 'missing deferred DPI completion'
            settled = segment.rsplit('DPI transition complete', 1)[1]
            assert '[present-cost]' in settled, 'presentation stopped after DPI cycle'
        assert 'ResizeBuffers FAILED' not in log and 'DEVICE_REMOVED' not in log
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
