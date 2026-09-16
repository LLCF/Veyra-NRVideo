# Feasibility probe for the RTX 30 (sm_86) native DLSS-G path.
#
# dashdogy's Ampere backend re-compiles the provider's sm_89 PTX for sm_86 and
# loads it through the CUDA driver; he does not ship the compiled kernels.
# This script checks the one thing we can check without a 30-series card:
# whether the provider's PTX is a *legal sm_86 program* at all.
#
# Method: extract every PTX entry from the audited nvngx_dlssg.dll 310.7,
# rewrite ".target sm_89" to ".target sm_86", and hand each module to the
# NVIDIA driver JIT on this machine (RTX 5070, sm_120). The JIT compiles the
# module for the local device while enforcing the declared target's feature
# set, so an sm_89-only instruction fails right here. Success means the
# instruction set is carryable; it does NOT prove sm_86 hardware behaviour.
import ctypes
import struct
import sys
from pathlib import Path

DLL = Path(sys.argv[1] if len(sys.argv) > 1 else r"runtime_local/nvidia/nvngx_dlssg.dll")
OUT = Path("out/build/sm86-ptx")
OUT.mkdir(parents=True, exist_ok=True)


def lz4_block(src, dst_size):
    out = bytearray()
    i = 0
    while i < len(src):
        token = src[i]
        i += 1
        literals = token >> 4
        if literals == 15:
            while True:
                ext = src[i]
                i += 1
                literals += ext
                if ext != 0xFF:
                    break
        out += src[i:i + literals]
        i += literals
        if i >= len(src):
            break
        back = src[i] | (src[i + 1] << 8)
        i += 2
        match = 4 + (token & 0x0F)
        if (token & 0x0F) == 15:
            while True:
                ext = src[i]
                i += 1
                match += ext
                if ext != 0xFF:
                    break
        for _ in range(match):
            out.append(out[-back])
    return bytes(out) if len(out) == dst_size else None


def extract():
    data = DLL.read_bytes()
    magic = struct.pack("<I", 0xBA55ED50)
    modules = []
    pos = 0
    while True:
        i = data.find(magic, pos)
        if i < 0:
            break
        pos = i + 1
        hdr = struct.unpack_from("<H", data, i + 6)[0]
        declared = struct.unpack_from("<Q", data, i + 8)[0]
        total = declared + 16
        if hdr != 16 or total < 1024 or i + total > len(data):
            continue
        p = i + 16
        while p + 64 <= i + total:
            kind = struct.unpack_from("<H", data, p)[0]
            ehdr = struct.unpack_from("<I", data, p + 4)[0]
            payload = struct.unpack_from("<Q", data, p + 8)[0]
            comp = struct.unpack_from("<I", data, p + 16)[0]
            arch = struct.unpack_from("<I", data, p + 28)[0]
            raw = struct.unpack_from("<Q", data, p + 56)[0]
            if ehdr < 64 or payload == 0 or p + ehdr + payload > i + total:
                break
            if kind == 1 and arch == 89 and comp:
                blob = data[p + ehdr:p + ehdr + comp]
                ptx = lz4_block(blob, raw)
                if ptx and (ptx[:1] == b"/" or ptx[:2] == b"\r\n"):
                    modules.append((i, p, ptx))
            p += ehdr + payload
    return modules


modules = extract()
print(f"extracted {len(modules)} sm_89 PTX modules from {DLL}")

entries = []
for fat, p, ptx in modules:
    text = ptx.decode("utf-8", errors="replace")
    names = []
    for line in text.splitlines():
        line = line.strip()
        if ".entry " in line:
            after = line.split(".entry ", 1)[1]
            name = after.split("(", 1)[0].strip().split()[-1]
            names.append(name)
    entries.append((fat, names))
total_entries = sum(len(n) for _, n in entries)
print(f"declared .entry kernels: {total_entries}")

# CUDA driver via ctypes.
cuda = ctypes.WinDLL("nvcuda.dll")
for fn, argtypes in (
    ("cuInit", [ctypes.c_uint]),
    ("cuDeviceGet", [ctypes.POINTER(ctypes.c_int), ctypes.c_int]),
    ("cuCtxCreate_v2", [ctypes.POINTER(ctypes.c_void_p), ctypes.c_uint, ctypes.c_int]),
    ("cuCtxDestroy_v2", [ctypes.c_void_p]),
    ("cuModuleLoadDataEx", [ctypes.POINTER(ctypes.c_void_p), ctypes.c_void_p,
                            ctypes.c_uint, ctypes.c_void_p, ctypes.c_void_p]),
    ("cuModuleUnload", [ctypes.c_void_p]),
    ("cuGetErrorString", [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]),
):
    getattr(cuda, fn).argtypes = argtypes
    getattr(cuda, fn).restype = ctypes.c_int


def err(code):
    s = ctypes.c_char_p()
    cuda.cuGetErrorString(code, ctypes.byref(s))
    return (s.value or b"?").decode()


assert cuda.cuInit(0) == 0, "cuInit failed"
dev = ctypes.c_int()
assert cuda.cuDeviceGet(ctypes.byref(dev), 0) == 0
ctx = ctypes.c_void_p()
assert cuda.cuCtxCreate_v2(ctypes.byref(ctx), 0, dev) == 0, "cuCtxCreate failed"

ok = 0
fail = []
for idx, (fat, p, ptx) in enumerate(modules):
    text = ptx.decode("utf-8", errors="replace")
    if ".target sm_89" not in text:
        fail.append((idx, fat, p, "no .target sm_89 marker", 0))
        continue
    rewritten = text.replace(".target sm_89", ".target sm_86")
    buf = rewritten.encode("utf-8") + b"\x00"
    mod = ctypes.c_void_p()
    rc = cuda.cuModuleLoadDataEx(ctypes.byref(mod), buf, 0, None, None)
    if rc == 0:
        ok += 1
        cuda.cuModuleUnload(mod)
    else:
        fail.append((idx, fat, p, err(rc), rc))

cuda.cuCtxDestroy_v2(ctx)
print(f"JIT as sm_86: {ok}/{len(modules)} modules compiled, {len(fail)} failed")
for idx, fat, p, message, rc in fail[:20]:
    print(f"  FAIL[{idx}] fatbin@0x{fat:x} entry@0x{p:x}: rc={rc} {message}")
