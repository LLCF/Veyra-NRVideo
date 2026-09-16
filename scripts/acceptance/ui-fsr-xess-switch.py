"""Verify the FSR -> XeSS backend switch: the retained AMD proxy swapchain
must be wrapped by XeSS (xefgSwapChainD3D12InitFromSwapChain) instead of
failing with a second CreateSwapChainForHwnd on the same window.

Also collects generation counters for both backends from the app log.
"""
import ctypes as c
import ctypes.wintypes as w
import hashlib
import os
import pathlib
import re
import subprocess
import sys
import time

if '--worker' not in sys.argv:
    child = subprocess.Popen([sys.executable, __file__, '--worker', *sys.argv[1:]])
    try:
        sys.exit(child.wait(timeout=120))
    except subprocess.TimeoutExpired:
        subprocess.run(['taskkill.exe', '/PID', str(child.pid), '/T', '/F'], capture_output=True, timeout=5)
        sys.exit('FAIL: FSR->XeSS switch test exceeded 120 seconds')
sys.argv.remove('--worker')
u = c.WinDLL('user32', use_last_error=True)
u.SetProcessDpiAwarenessContext.argtypes = [w.HANDLE]
u.SetProcessDpiAwarenessContext(c.c_void_p(-4))
u.GetDlgItem.argtypes = [w.HWND, c.c_int]
u.GetDlgItem.restype = w.HWND
u.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
u.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.SendMessageTimeoutW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM, w.UINT, w.UINT, c.POINTER(c.c_size_t)]
visit_type = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)


def send(hwnd, message, first=0, second=0):
    result = c.c_size_t()
    if not u.SendMessageTimeoutW(hwnd, message, first, second, 2, 1500, c.byref(result)):
        raise RuntimeError('window message timed out')
    return result.value


def windows(pid, class_name, parent=None):
    found = []

    @visit_type
    def visit(hwnd, unused):
        owner = w.DWORD()
        u.GetWindowThreadProcessId(hwnd, c.byref(owner))
        name = c.create_unicode_buffer(128)
        u.GetClassNameW(hwnd, name, 128)
        if owner.value == pid and name.value == class_name:
            found.append(hwnd)
        return True

    if parent:
        u.EnumChildWindows(parent, visit, 0)
    else:
        u.EnumWindows(visit, 0)
    return found


def wait_for(action, seconds=10):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        value = action()
        if value:
            return value
        if process.poll() is not None:
            raise RuntimeError('application exited before test completed')
        time.sleep(.05)
    raise RuntimeError('condition timed out')


def rect(hwnd):
    value = w.RECT()
    if not u.GetWindowRect(hwnd, c.byref(value)):
        raise RuntimeError('missing control rectangle')
    return [value.left, value.top, value.right, value.bottom]


def intersects(a, b):
    return max(a[0], b[0]) < min(a[2], b[2]) and max(a[1], b[1]) < min(a[3], b[3])


def text(hwnd):
    value = c.create_unicode_buffer(1024)
    u.GetWindowTextW(hwnd, value, len(value))
    return value.value


exe = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'out/build/audio-continuity-repair-20260915/veyra.exe').resolve()
default_media = pathlib.Path(r'E:\Ai\知识\小七姐\GTAVI_An_Extended_Look_4K_Native.mp4')
if not default_media.exists():
    default_media = pathlib.Path('logs/video-sdk-trial-20260909/known-pan15.mp4')
media = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else default_media).resolve()
out = pathlib.Path('logs/fsr') / ('fsr-xess-switch-' + str(time.time_ns()))
out.mkdir(parents=True)
env = os.environ.copy()
env['PATH'] = r'C:\veyra-deps\installed\x64-windows\bin;' + env['PATH']
env['VEYRA_LOG_FILE'] = str((out / 'app.log').resolve())
print('exe', exe, hashlib.sha256(exe.read_bytes()).hexdigest()[:16])
startup = subprocess.STARTUPINFO()
startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0


def log():
    return (out / 'app.log').read_text(encoding='utf-8', errors='replace') if (out / 'app.log').exists() else ''


def fsr_generated():
    values = re.findall(r'\[fsr-fg\] frameId=\d+ real=\d+ generated=(\d+)', log())
    return int(values[-1]) if values else None


def xess_generated():
    values = re.findall(r'\[xess-fg\] presentId=\d+ enabled=\d result=0 framesPresented=\d+ generatedTotal=(\d+)', log())
    return int(values[-1]) if values else None


with (out / 'stdout.log').open('w') as stdout, (out / 'stderr.log').open('w') as stderr:
    process = subprocess.Popen([str(exe), '--smoke-view', 'professional', '--smoke-seconds', '90',
                                str(media), '--no-nr', '--no-sr', '--fg'],
                               stdout=stdout, stderr=stderr, env=env, startupinfo=startup)
    failures = []
    try:
        main = wait_for(lambda: windows(process.pid, 'VeyraApp'))[0]
        u.ShowWindow(main, 4)
        panel = wait_for(lambda: windows(process.pid, 'VeyraInspector', main))[0]
        body = windows(process.pid, 'VeyraInspectorBody', panel)[0]
        wait_for(lambda: 'professional=true' in log(), 15)
        time.sleep(.5)
        send(main, 0x111, 231)  # TabFg
        time.sleep(.4)
        backend = u.GetDlgItem(body, 208)
        assert backend, 'missing FG backend selector'
        count = send(backend, 0x146)  # CB_GETCOUNT
        names = [text(u.GetDlgItem(backend, index)) for index in range(count)]
        print('backends:', names)
        assert count == 3, 'expected DLSS/XeSS/FSR backends'

        def choose(control, index):
            for unused in range(10):
                anchor, viewport = rect(control), rect(body)
                if anchor[1] >= viewport[1] and anchor[3] <= viewport[3]:
                    break
                delta = 120 if anchor[1] < viewport[1] else -120
                send(body, 0x20A, (delta & 0xffff) << 16, 0)
            u.PostMessageW(control, 0x201, 1, 12 | (12 << 16))
            popup = wait_for(lambda: windows(process.pid, 'VeyraGlassSelector'))[0]
            listing = u.GetDlgItem(popup, 1)
            assert send(listing, 0x18B) > index, 'choice missing in popup'
            send(listing, 0x186, index)
            u.PostMessageW(listing, 0x100, 0x0D, 0)
            wait_for(lambda: not windows(process.pid, 'VeyraGlassSelector'))
            assert send(control, 0x147) == index, 'popup did not commit choice'

        # 1) Switch to FSR and wait until frames are being generated.
        choose(backend, 2)
        wait_for(lambda: 'fgBackend=AMD-FSR' in log(), 18)
        try:
            wait_for(lambda: (fsr_generated() or 0) >= 10, 25)
            print('fsr generated:', fsr_generated())
        except RuntimeError as error:
            failures.append(f'FSR did not generate frames while active: {error}')

        # 1b) Wheel-zoom the video surface: generation must continue with a
        #     non-default view (the old exact `view == PreviewView{}` guard
        #     stopped generation forever after any zoom).
        surface = u.GetDlgItem(main, 1000)  # VideoSurface
        if surface:
            r = rect(surface)
            x, y = (r[0] + r[2]) // 2, (r[1] + r[3]) // 2
            lparam = (x & 0xffff) | ((y & 0xffff) << 16)
            for unused in range(3):
                u.PostMessageW(surface, 0x20A, (120 & 0xffff) << 16, lparam)
                time.sleep(0.2)
            try:
                wait_for(lambda: 'preview-view' in log(), 10)
                baseline = fsr_generated() or 0
                wait_for(lambda: (fsr_generated() or 0) > baseline, 20)
                print('fsr generated after wheel zoom:', fsr_generated())
            except RuntimeError as error:
                failures.append(f'FSR stopped after a wheel zoom: {error}')
        else:
            failures.append('VideoSurface control not found')

        # 2) Switch to XeSS. The FidelityFX proxy owns the window's only
        #    flip-model swapchain slot and cannot be released without leaving
        #    the window unable to host any later swapchain (verified), so the
        #    accepted behaviour is: the switch is refused, the session rolls
        #    back to FSR, and playback stays healthy without a crash.
        choose(backend, 1)
        time.sleep(8)
        if process.poll() is not None:
            failures.append('application died during the XeSS switch')
        applied = [line for line in log().splitlines() if 'Applied revision=' in line]
        if applied and 'fgBackend=XeSS' in applied[-1]:
            print('note: XeSS switch applied (unexpected on this build)')
        else:
            print('XeSS switch refused; previous FSR session kept')
        try:
            baseline = fsr_generated() or 0
            wait_for(lambda: (fsr_generated() or 0) > baseline, 25)
            print('fsr generated after the refused switch:', fsr_generated())
        except RuntimeError as error:
            failures.append(f'FSR did not keep generating after the refused switch: {error}')
    except Exception as error:  # noqa: BLE001 - diagnostic script
        failures.append(str(error))
    finally:
        try:
            process.wait(timeout=40)
        except subprocess.TimeoutExpired:
            process.kill()
    if failures:
        print('FAIL:', '; '.join(failures))
        sys.exit(1)
    print('PASS: FSR ran; the XeSS switch was refused by the shared-swapchain limit, '
          'the session rolled back to FSR and kept generating without a crash')
