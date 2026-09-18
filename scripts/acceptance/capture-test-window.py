"""Capture only the visible latency harness window, outside timed measurements."""
import ctypes as c
import ctypes.wintypes as w
import json
import sys
import time
from pathlib import Path
from PIL import ImageGrab, ImageStat

u = c.WinDLL("user32", use_last_error=True)
u.SetProcessDpiAwarenessContext.argtypes = [w.HANDLE]
u.SetProcessDpiAwarenessContext(c.c_void_p(-4))
u.FindWindowW.argtypes = [w.LPCWSTR, w.LPCWSTR]
u.FindWindowW.restype = w.HWND
u.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
u.GetAncestor.argtypes = [w.HWND, w.UINT]
u.GetAncestor.restype = w.HWND
u.WindowFromPoint.argtypes = [w.POINT]
u.WindowFromPoint.restype = w.HWND
window = u.FindWindowW("STATIC", "Veyra pacing acceptance")
assert window, "Capture harness window missing"
u.SetWindowPos.argtypes = [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT]
u.SetForegroundWindow.argtypes = [w.HWND]
u.SetWindowPos(window, c.c_void_p(-1), 0, 0, 0, 0, 0x43)
u.SetForegroundWindow(window)
time.sleep(.3)
rect = w.RECT()
assert u.GetWindowRect(window, c.byref(rect))
points = [(rect.left + 15, rect.top + 40), (rect.right - 15, rect.bottom - 15),
          ((rect.left + rect.right) // 2, (rect.top + rect.bottom) // 2)]
assert all(u.GetAncestor(u.WindowFromPoint(w.POINT(x, y)), 2) == window for x, y in points), "Harness is occluded"
image = ImageGrab.grab(bbox=(rect.left, rect.top, rect.right, rect.bottom), all_screens=True).convert("RGB")
out = Path(sys.argv[1])
image.save(out)
sample = image.crop((20, 60, image.width - 20, image.height - 20))
result = {"image": str(out), "mean": ImageStat.Stat(sample).mean,
          "extrema": sample.getextrema(), "bounds": [rect.left, rect.top, rect.right, rect.bottom]}
out.with_suffix(".json").write_text(json.dumps(result, indent=2), encoding="utf-8")
print(json.dumps(result))
