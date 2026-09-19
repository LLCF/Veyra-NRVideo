"""Exercise the actual professional FG selector and applied engine transactions."""
import ctypes as c
import ctypes.wintypes as w
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
import time

if '--worker' not in sys.argv:
    child = subprocess.Popen([sys.executable, __file__, '--worker', *sys.argv[1:]])
    try:
        sys.exit(child.wait(timeout=90))
    except subprocess.TimeoutExpired:
        subprocess.run(['taskkill.exe', '/PID', str(child.pid), '/T', '/F'], capture_output=True, timeout=5)
        sys.exit('FAIL: FG selector test exceeded 90 seconds')
sys.argv.remove('--worker')
portable = '--portable' in sys.argv
if portable:
    sys.argv.remove('--portable')
reject_xess = '--reject-xess' in sys.argv
if reject_xess:
    sys.argv.remove('--reject-xess')
u = c.WinDLL('user32', use_last_error=True)
u.SetProcessDpiAwarenessContext.argtypes = [w.HANDLE]
u.SetProcessDpiAwarenessContext(c.c_void_p(-4))
u.GetDlgItem.argtypes = [w.HWND, c.c_int]
u.GetDlgItem.restype = w.HWND
u.GetParent.argtypes = [w.HWND]
u.GetParent.restype = w.HWND
u.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
u.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
u.IsWindowVisible.argtypes = [w.HWND]
u.SetWindowPos.argtypes = [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT]
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


def wait_for(action, seconds=8):
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


if len(sys.argv) != 4:
    sys.exit('Usage: ui-fg-backends.py EXE MEDIA OUTPUT_DIRECTORY [--reject-xess] [--portable]')
exe = pathlib.Path(sys.argv[1]).resolve()
media = pathlib.Path(sys.argv[2]).resolve()
out = pathlib.Path(sys.argv[3]).resolve()
out.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
env['PATH'] = (os.path.join(os.environ['SYSTEMROOT'], 'System32') + ';' + os.environ['SYSTEMROOT']
               if portable else r'C:\veyra-deps\installed\x64-windows\bin;' + env['PATH'])
env['VEYRA_LOG_FILE'] = str((out / 'app.log').resolve())
if reject_xess:
    env['VEYRA_TEST_XESS_INIT_FAILURE'] = '1'
result = {'exeHash': hashlib.sha256(exe.read_bytes()).hexdigest(), 'layouts': [], 'switches': [], 'error': ''}
startup = subprocess.STARTUPINFO()
startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0


def log():
    return (out / 'app.log').read_text(encoding='utf-8', errors='replace') if (out / 'app.log').exists() else ''


with (out / 'stdout.log').open('w') as stdout, (out / 'stderr.log').open('w') as stderr:
    process = subprocess.Popen([str(exe), '--smoke-view', 'professional', '--smoke-seconds', '80',
                                str(media), '--no-nr', '--no-sr', '--fg'],
                               stdout=stdout, stderr=stderr, env=env, startupinfo=startup,
                               cwd=exe.parent if portable else None)
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
        title = u.GetDlgItem(body, 1103)
        multiplier = u.GetDlgItem(body, 202)
        revision_label = u.GetDlgItem(panel, 400)
        assert backend and title and multiplier, 'missing FG controls'
        assert send(backend, 0x146) == 2, 'FG selector must expose DLSS and XeSS only'
        for width, height in [(1280, 900), (1040, 540), (1040, 800)]:
            u.SetWindowPos(main, None, 30, 30, width, height, 0x14)
            time.sleep(.2)
            rectangles = {'title': rect(title), 'backend': rect(backend), 'multiplier': rect(multiplier)}
            result['layouts'].append(rectangles)
            assert u.IsWindowVisible(backend), 'backend selector hidden'
            assert not intersects(rectangles['backend'], rectangles['title']), 'backend selector overlaps title'
            assert not intersects(rectangles['backend'], rectangles['multiplier']), 'backend selector overlaps multiplier'
            assert rectangles['backend'][1] >= rect(body)[1], 'backend selector clipped above viewport'

        def choose(control, index):
            for unused in range(10):
                anchor, viewport = rect(control), rect(body)
                if anchor[1] >= viewport[1] and anchor[3] <= viewport[3]:
                    break
                delta = 120 if anchor[1] < viewport[1] else -120
                send(body, 0x20A, (delta & 0xffff) << 16, 0)
            assert rect(control)[1] >= rect(body)[1] and rect(control)[3] <= rect(body)[3], 'choice clipped by scroll viewport'
            # Trigger the real custom popup through the same mouse event as a click.
            u.PostMessageW(control, 0x201, 1, 12 | (12 << 16))
            popup = wait_for(lambda: windows(process.pid, 'VeyraGlassSelector'))[0]
            listing = u.GetDlgItem(popup, 1)
            assert send(listing, 0x18B) > index, 'choice missing in popup'
            send(listing, 0x186, index)
            u.PostMessageW(listing, 0x100, 0x0D, 0)
            wait_for(lambda: not windows(process.pid, 'VeyraGlassSelector'))
            assert send(control, 0x147) == index, 'popup did not commit choice'

        def applied():
            current = text(revision_label)
            values = re.findall(r'\d+', current.splitlines()[0])
            return current != previous and len(values) >= 2 and values[0] == values[1]

        for index, name in ([(1, 'XeSS')] if reject_xess else [(1, 'XeSS'), (0, 'DLSS')]):
            previous = text(revision_label)
            choose(backend, index)
            if reject_xess and name == 'XeSS':
                # Professional mode routes warnings to its status sink, not
                # the inspector's legacy hidden status label (401).
                wait_for(lambda: send(backend, 0x147) == 0 and 'DLSS' in text(revision_label)
                         and re.search(r'\[reset-lifecycle\].*outcome=3', log()), 18)
                result['switches'].append('XeSS rejected, DLSS retained')
                continue
            wait_for(applied, 18)
            result['switches'].append(name)
            assert send(multiplier, 0x146) == (4 if name == 'XeSS' else 5), 'incorrect multiplier choices'
        if not reject_xess:
            # Prime the unlocked provider, leave it, then return at 4X. Also
            # exercise a stock 2X context before requesting unlocked 4X again.
            for control, index, label in [
                (multiplier, 3, 'DLSS 4X'), (backend, 1, 'XeSS 4X'),
                (backend, 0, 'DLSS 4X again'), (backend, 1, 'XeSS 4X again'),
                (multiplier, 1, 'XeSS 2X'), (multiplier, 3, 'XeSS 4X after 2X'),
                (backend, 0, 'DLSS return'), (multiplier, 4, 'DLSS 6X'),
                (backend, 1, 'XeSS clamps 6X to supported 4X')]:
                previous = text(revision_label)
                choose(control, index)
                wait_for(applied, 18)
                result['switches'].append(label)
        previous = text(revision_label)
        choose(multiplier, 0)
        wait_for(applied, 12)
        result['switches'].append('off')
        status_panel = windows(process.pid, 'VeyraLiveStatus', main)
        if status_panel:
            from PIL import ImageGrab
            time.sleep(.25)
            ImageGrab.grab(window=main).save(out / 'status-top.png')
            send(status_panel[0], 0x20A, ((-120 * 4) & 0xffff) << 16, 0)
            time.sleep(.25)
            ImageGrab.grab(window=main).save(out / 'status-details.png')
        result['passed'] = True
    except Exception as error:
        result['error'] = str(error)
        result['passed'] = False
    finally:
        roots = windows(process.pid, 'VeyraApp')
        if roots:
            u.PostMessageW(roots[0], 0x10, 0, 0)
        try:
            process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        result['exit'] = process.returncode
        result['appliedBackends'] = re.findall(r'Applied revision=\d+.*?fgBackend=(\w+)', log())
        if result.get('passed'):
            expected = ['DLSS'] if reject_xess else ['XeSS', 'DLSS']
            result['passed'] = all(name in result['appliedBackends'] for name in expected)
            if reject_xess:
                result['passed'] = result['passed'] and 'XeSS' not in result['appliedBackends']
        result['passed'] = result.get('passed', False) and process.returncode == 0
        (out / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
        print(out / 'result.json')
        print(json.dumps(result))
sys.exit(0 if result['passed'] else 1)
