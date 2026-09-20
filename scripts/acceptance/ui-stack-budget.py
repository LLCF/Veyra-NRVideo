"""Check Release x64 callback stack allocations using PE unwind data and a /MAP file."""
import json
from pathlib import Path
import re
import struct
import sys

data = Path(sys.argv[1]).read_bytes()
def unpack(fmt, offset):
    return struct.unpack_from('<' + fmt, data, offset)

pe, = unpack('I', 0x3c)
assert data[pe:pe+4] == b'PE\0\0'
machine, count = unpack('HH', pe+4)
assert machine == 0x8664, 'only AMD64 unwind records are supported'
optional_size, = unpack('H', pe+20)
optional = pe+24
assert unpack('H', optional)[0] == 0x20b
base, = unpack('Q', optional+24)
sections = [unpack('IIII', optional+optional_size+i*40+8) for i in range(count)]

def offset(rva):
    for virtual_size, start, raw_size, raw_start in sections:
        if start <= rva < start+max(virtual_size, raw_size):
            assert rva-start < raw_size, 'unbacked RVA'
            return raw_start+rva-start
    raise ValueError(f'unmapped RVA {rva:x}')

exception_rva, exception_size = unpack('II', optional+112+3*8)
functions = dict((start, unwind) for start, end, unwind in
                 (unpack('III', offset(exception_rva)+i) for i in range(0, exception_size, 12)))

def stack_bytes(rva):
    position = offset(functions[rva])
    version, _, slots, _ = unpack('BBBB', position)
    assert version & 7 == 1 and not version & 0x20, 'unsupported/chained unwind info'
    i, total = 0, 0
    while i < slots:
        _, operation = unpack('BB', position+4+i*2)
        op, info = operation & 15, operation >> 4
        i += 1
        if op == 0:
            total += 8
        elif op == 1:
            assert info in (0, 1)
            total += unpack('H' if info == 0 else 'I', position+4+i*2)[0] * (8 if info == 0 else 1)
            i += 1 if info == 0 else 2
        elif op == 2:
            total += info*8+8
        elif op == 3:
            pass
        elif op in (4, 8):
            i += 1
        elif op in (5, 9):
            i += 2
        elif op == 10:
            total += 40+info*8
        else:
            raise ValueError(f'unsupported unwind operation {op}')
    return total

results = {}
for line in Path(sys.argv[2]).read_text().splitlines():
    match = re.match(r'\s+\w+:\w+\s+(\?proc@\S+)\s+([0-9a-fA-F]{16})\s+f\s+(AppShell|SettingsWindow)\.cpp\.obj', line)
    if match:
        results[match[3]] = stack_bytes(int(match[2], 16)-base)
assert len(results) == 2, 'both callback symbols must be present'
print(json.dumps(results, indent=2))
assert all(size <= 32768 for size in results.values()), 'reentrant UI callback exceeds 32 KiB stack budget'
