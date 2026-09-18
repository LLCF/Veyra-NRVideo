"""Own-process UI acceptance for authored bitmap positions and named presets."""
import ctypes as c
import ctypes.wintypes as w
import json
import os
import pathlib
import subprocess
import sys
import time
from PIL import ImageGrab, ImageChops

build, out, media = (pathlib.Path(p).resolve() for p in sys.argv[1:4])
assert build.name == 'frame-pacing-20260918'
out.mkdir(parents=True, exist_ok=True)
u = c.WinDLL('user32', use_last_error=True)
u.SetProcessDpiAwarenessContext.argtypes = [w.HANDLE]
u.SetProcessDpiAwarenessContext(c.c_void_p(-4))
for name, args, result in [
    ('GetWindowThreadProcessId', [w.HWND, c.POINTER(w.DWORD)], w.DWORD),
    ('GetClassNameW', [w.HWND, w.LPWSTR, c.c_int], c.c_int),
    ('GetDlgItem', [w.HWND, c.c_int], w.HWND),
    ('GetWindowRect', [w.HWND, c.POINTER(w.RECT)], w.BOOL),
    ('IsWindowVisible', [w.HWND], w.BOOL),
    ('SetWindowTextW', [w.HWND, w.LPCWSTR], w.BOOL),
    ('SetForegroundWindow', [w.HWND], w.BOOL),
    ('SetWindowPos', [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT], w.BOOL),
    ('ChildWindowFromPointEx', [w.HWND, w.POINT, w.UINT], w.HWND),
    ('ScreenToClient', [w.HWND, c.POINTER(w.POINT)], w.BOOL),
    ('SendMessageTimeoutW', [w.HWND, w.UINT, w.WPARAM, w.LPARAM, w.UINT, w.UINT, c.POINTER(c.c_size_t)], w.LPARAM),
]:
    fn = getattr(u, name); fn.argtypes = args; fn.restype = result
callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
u.EnumWindows.argtypes = [callback, w.LPARAM]
u.EnumChildWindows.argtypes = [w.HWND, callback, w.LPARAM]
env = os.environ.copy()
env['TEMP'] = env['TMP'] = 'E:/项目/Veyra/tmp/5060-presets-subtitles-20260918'
process = None


def send(h, msg, a=0, b=0):
    value = c.c_size_t()
    assert u.SendMessageTimeoutW(h, msg, a, b, 2, 1500, c.byref(value)), 'UI timeout'
    return value.value


def find(cls, parent=None):
    found = []
    @callback
    def visit(h, unused):
        pid = w.DWORD(); u.GetWindowThreadProcessId(h, c.byref(pid))
        name = c.create_unicode_buffer(80); u.GetClassNameW(h, name, 80)
        if pid.value == process.pid and name.value == cls: found.append(h)
        return True
    if parent: u.EnumChildWindows(parent, visit, 0)
    else: u.EnumWindows(visit, 0)
    return found[0] if found else None


def wait(predicate, seconds=8):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        value = predicate()
        if value: return value
        assert process.poll() is None, 'Application exited'
        time.sleep(.03)
    raise AssertionError('UI condition timeout')


def rect(h):
    r = w.RECT(); assert u.GetWindowRect(h, c.byref(r))
    return [r.left, r.top, r.right, r.bottom]


def launch(stage, extra):
    global process
    env['VEYRA_LOG_FILE'] = str(out / (stage + '.log'))
    process = subprocess.Popen([str(build/'veyra.exe'), str(media), '--smoke-seconds', '90', *extra],
                               cwd=build, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    main = wait(lambda: find('VeyraApp'))
    u.SetWindowPos(main, c.c_void_p(-1), 10, 10, 1180, 800, 0x40)
    u.SetForegroundWindow(main)
    return main


def close(main):
    send(main, 0x10)
    assert process.wait(timeout=15) == 0


result = {'bitmap': [], 'presetLayouts': []}
store = build/'runtime_local'/'color-looks.v1'
saved_store = store.read_bytes() if store.exists() else None
try:
    main = launch('bitmap-ui', ['--subtitle-primary', '0'])
    overlay = wait(lambda: find('VeyraSubtitleOverlay', main))
    wait(lambda: u.IsWindowVisible(overlay))
    send(main, 0x111, 102)  # pause at first display
    time.sleep(.3)
    for mode in ['window', 'resize', 'fullscreen']:
        if mode == 'resize': u.SetWindowPos(main, None, 10, 10, 900, 640, 0x14)
        if mode == 'fullscreen': send(main, 0x111, 120)
        time.sleep(.4)
        bounds = rect(overlay)
        on = ImageGrab.grab(bbox=tuple(bounds), all_screens=True).convert('RGB')
        on.save(out/(mode+'-on.png'))
        send(main, 0x111, 225)
        wait(lambda: not u.IsWindowVisible(overlay))
        time.sleep(.15)
        off = ImageGrab.grab(bbox=tuple(bounds), all_screens=True).convert('RGB')
        off.save(out/(mode+'-off.png'))
        diff = ImageChops.difference(on, off)
        width, height = on.size
        fit = min(width/1280, height/720)
        ox, oy = (width-1280*fit)/2, (height-720*fit)/2
        regions = []
        for x, y in [(100, 600), (900, 100)]:
            # The opaque white half differs from both source colors. Red over
            # the source's red bar can legitimately produce no difference.
            box = (int(ox+(x+6)*fit), int(oy+(y+2)*fit), int(ox+(x+30)*fit), int(oy+(y+22)*fit))
            pixels = list(diff.crop(box).getdata())
            changed = sum(max(p)>20 for p in pixels)
            assert changed > len(pixels)*.6, (mode, box, changed, len(pixels))
            regions.append({'box': box, 'changedPixels': changed, 'totalPixels': len(pixels)})
        diff.save(out/(mode+'-diff.png'))
        result['bitmap'].append({'mode': mode, 'regions': regions})
        send(main, 0x111, 225)
        wait(lambda: u.IsWindowVisible(overlay))
    send(main, 0x111, 120)  # restore window
    send(main, 0x111, 102)  # resume and observe timed clear
    wait(lambda: not u.IsWindowVisible(overlay), 5)
    result['timedClear'] = True
    close(main)

    name = 'UI-验收命名-' + str(time.time_ns())
    for stage in ['save', 'reload']:
        main = launch('preset-'+stage, ['--colour-page'])
        body = wait(lambda: find('VeyraInspectorBody', main))
        control = lambda ident: u.GetDlgItem(body, ident)
        wait(lambda: u.IsWindowVisible(control(804)))
        if stage == 'save':
            for width, height in [(1180, 800), (900, 640)]:
                u.SetWindowPos(main, None, 10, 10, width, height, 0x14)
                time.sleep(.3)
                if not u.IsWindowVisible(body): send(main, 0x111, 229)
                for ident in range(805, 810):
                    for unused in range(100):
                        box, viewport = rect(control(ident)), rect(body)
                        if viewport[1]<=box[1] and box[3]<=viewport[3]: break
                        send(body, 0x115, 0 if box[1]<viewport[1] else 1)
                    else: raise AssertionError(('Unreachable preset action', ident))
                    point = w.POINT((box[0]+box[2])//2, (box[1]+box[3])//2)
                    u.ScreenToClient(body, c.byref(point))
                    hit = u.ChildWindowFromPointEx(body, point, 1)
                    if hit != control(ident):
                        ImageGrab.grab(bbox=tuple(rect(main)), all_screens=True).save(out/'preset-failure.png')
                        raise AssertionError(('Occluded button', width, ident, box, viewport, point.x, point.y, hit, control(ident)))
                boxes = [rect(control(i)) for i in range(805, 810)]
                result['presetLayouts'].append({'width': width, 'boxes': boxes})
                ImageGrab.grab(bbox=tuple(rect(main)), all_screens=True).save(out/f'preset-layout-{width}.png')
            name_buffer = c.create_unicode_buffer(name)
            send(control(804), 0xc, 0, c.addressof(name_buffer))
            readback = c.create_unicode_buffer(256)
            send(control(804), 0xd, 256, c.addressof(readback))
            assert readback.value == name, ('Name edit', name, readback.value)
            try:
                send(control(805), 0xf5)
            except AssertionError:
                dialog = find('#32770')
                if dialog:
                    labels = []
                    @callback
                    def collect(h, unused):
                        buf = c.create_unicode_buffer(512)
                        send(h, 0xd, 512, c.addressof(buf)); labels.append(buf.value)
                        return True
                    u.EnumChildWindows(dialog, collect, 0)
                    print('Modal:', labels, flush=True)
                raise
            wait(lambda: store.exists() and name in store.read_text(encoding='utf-8'))
        else:
            count = send(control(803), 0x146)
            found = False
            for index in range(count):
                text = c.create_unicode_buffer(256)
                send(control(803), 0x148, index, c.addressof(text))
                found |= text.value == name
            assert found, 'Named preset missing after actual process restart'
            result['presetRestart'] = True
        close(main)
    result['passed'] = True
    (out/'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print('PASS bitmap pixels, authored positions, resize, fullscreen, toggle, timed clear; preset hit targets, naming and process restart')
finally:
    if process and process.poll() is None: process.kill(); process.wait()
    if saved_store is None:
        store.unlink(missing_ok=True)
    else:
        store.write_bytes(saved_store)
