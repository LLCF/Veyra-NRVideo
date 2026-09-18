"""Own-process controls, persistence and layout acceptance; isolated build only."""
import ctypes as c
import ctypes.wintypes as w
import json
import os
import pathlib
import subprocess
import sys
import time
from PIL import ImageGrab

build, out, media = map(lambda p:pathlib.Path(p).resolve(), sys.argv[1:4])
assert build.name == 'frame-pacing-20260918', 'Use the isolated test build'
out.mkdir(parents=True, exist_ok=True)
u=c.WinDLL('user32', use_last_error=True)
u.SetProcessDpiAwarenessContext.argtypes=[w.HANDLE]
u.SetProcessDpiAwarenessContext(c.c_void_p(-4))
u.GetWindowThreadProcessId.argtypes=[w.HWND,c.POINTER(w.DWORD)]
u.GetClassNameW.argtypes=[w.HWND,w.LPWSTR,c.c_int]
u.GetDlgItem.argtypes=[w.HWND,c.c_int];u.GetDlgItem.restype=w.HWND
u.GetWindowRect.argtypes=[w.HWND,c.POINTER(w.RECT)]
u.GetParent.argtypes=[w.HWND];u.GetParent.restype=w.HWND
u.IsWindowEnabled.argtypes=[w.HWND]
u.IsWindowVisible.argtypes=[w.HWND]
u.SetForegroundWindow.argtypes=[w.HWND]
u.WindowFromPoint.argtypes=[w.POINT];u.WindowFromPoint.restype=w.HWND
u.GetAncestor.argtypes=[w.HWND,w.UINT];u.GetAncestor.restype=w.HWND
u.RedrawWindow.argtypes=[w.HWND,c.c_void_p,w.HANDLE,w.UINT]
u.SetWindowPos.argtypes=[w.HWND,w.HWND,c.c_int,c.c_int,c.c_int,c.c_int,w.UINT]
u.SendMessageTimeoutW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM,w.UINT,w.UINT,c.POINTER(c.c_size_t)]
callback=c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
u.EnumWindows.argtypes=[callback,w.LPARAM];u.EnumChildWindows.argtypes=[w.HWND,callback,w.LPARAM]


def send(h,m,a=0,b=0):
    result=c.c_size_t()
    assert u.SendMessageTimeoutW(h,m,a,b,2,1500,c.byref(result)), 'UI timeout'
    return result.value


def find(name,parent=None):
    found=[]
    @callback
    def visit(h,unused):
        pid=w.DWORD();u.GetWindowThreadProcessId(h,c.byref(pid))
        cls=c.create_unicode_buffer(80);u.GetClassNameW(h,cls,80)
        if pid.value==process.pid and cls.value==name:found.append(h)
        return True
    if parent:u.EnumChildWindows(parent,visit,0)
    else:u.EnumWindows(visit,0)
    return found[0] if found else None


def wait(predicate):
    until=time.monotonic()+8
    while time.monotonic()<until:
        value=predicate()
        if value:return value
        assert process.poll() is None, 'Application exited'
        time.sleep(.05)
    raise AssertionError('UI condition timeout')


def rect(h):
    r=w.RECT();assert u.GetWindowRect(h,c.byref(r))
    return [r.left,r.top,r.right,r.bottom]


env=os.environ.copy();env['TEMP']=env['TMP']='E:/项目/Veyra/tmp/frame-pacing-20260918'
results=[]
process=None
try:
    # Real startup/close path, not smoke mode (which deliberately disables saves).
    for stage in ['configure','restart-on','restart-off']:
        env['VEYRA_LOG_FILE']=str(out/(stage+'.log'))
        process=subprocess.Popen([str(build/'veyra.exe'),str(media)],cwd=build,env=env,
                                 stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        main=wait(lambda:find('VeyraApp'))
        body=wait(lambda:find('VeyraInspectorBody',main))
        control=lambda ident:u.GetDlgItem(body,ident)
        wait(lambda:control(240))
        # Restore professional panel and select FG tab through real command handlers.
        send(main,0x111,115)
        send(main,0x111,231)
        time.sleep(.5)
        if stage=='configure':
            if send(control(240),0xf0):send(control(240),0xf5)
            assert send(control(240),0xf0)==0
            assert not u.IsWindowEnabled(control(241)) and not u.IsWindowEnabled(control(242))
            send(control(240),0xf5)
            for ident,value in [(241,1),(242,2)]:
                send(control(ident),0x14e,value);send(body,0x111,ident|(1<<16),control(ident))
            wait(lambda:'effective=true/1/2' in (out/(stage+'.log')).read_text(encoding='utf-8',errors='replace'))
            for width,height in [(1280,900),(800,600)]:
                u.SetWindowPos(main,None,20,20,width,height,0x14)
                time.sleep(.3)
                if not u.IsWindowVisible(body):send(main,0x111,229)
                wait(lambda:u.IsWindowVisible(body))
                for expanded in [False,True]:
                    if expanded:send(control(221),0xf5)
                    send(body,0x115,7)  # SB_BOTTOM
                    for unused in range(20):send(body,0x115,1)  # body handles line scroll
                    time.sleep(.3)
                    boxes=[rect(control(i)) for i in [240,241,242,1150]]
                    assert all(a[3]<=b[1] for a,b in zip(boxes,boxes[1:])), boxes
                    viewport=rect(body)
                    assert all(viewport[0]<=b[0] and b[2]<=viewport[2] for b in boxes), boxes
                    # Compact drawers scroll; each control must be reachable,
                    # not necessarily visible together in a 140-pixel viewport.
                    for ident in [240,241,242,1150]:
                        for unused in range(50):
                            box=rect(control(ident));viewport=rect(body)
                            if viewport[1]<=box[1] and box[3]<=viewport[3]:break
                            send(body,0x115,0 if box[1]<viewport[1] else 1)
                        else:raise AssertionError(('Unreachable control',ident,viewport,box))
                        assert u.IsWindowVisible(control(ident))
                    u.SetWindowPos(main,c.c_void_p(-1),0,0,0,0,0x43)
                    u.SetForegroundWindow(main)
                    u.RedrawWindow(main,None,None,0x185)
                    time.sleep(.5)
                    bounds=rect(main)
                    points=[w.POINT(bounds[0]+10,bounds[1]+10),w.POINT(bounds[2]-10,bounds[3]-10)]
                    if all(u.GetAncestor(u.WindowFromPoint(point),2)==main for point in points):
                        image=ImageGrab.grab(bbox=tuple(bounds),all_screens=True).convert('RGB')
                        image.save(out/f'layout-{width}-{expanded}.png')
                    else:
                        print('SKIP screenshot: test window is occluded')
                    results.append(dict(width=width,height=height,helpExpanded=expanded,boxes=boxes))
                    if expanded:send(control(221),0xf5)
        else:
            assert send(control(241),0x147)==1 and send(control(242),0x147)==2
            assert send(control(240),0xf0)==(1 if stage=='restart-on' else 0), stage
            if stage=='restart-on':
                send(control(240),0xf5)
                wait(lambda:'effective=false/' in (out/(stage+'.log')).read_text(encoding='utf-8',errors='replace'))
                assert not u.IsWindowEnabled(control(241))
        send(main,0x10)
        assert process.wait(timeout=15)==0
    (out/'result.json').write_text(json.dumps(dict(passed=True,restartOn=True,restartOff=True,layout=results),indent=2),encoding='utf-8')
    print('PASS actual UI controls, applied state, restart-on, restart-off, help expansion and two window sizes')
finally:
    if process and process.poll() is None:process.kill();process.wait()
