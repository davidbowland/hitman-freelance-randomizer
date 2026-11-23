"""Disassemble HITMAN3.exe around an RVA (capstone + pefile).

usage: python3 -I tools/re/disasm.py EXE RVA [--before N] [--after N] [--func]
  RVA in hex (0xF2DE19). --func: start at the function containing RVA (from .pdata).
  --callers RVA: list direct E8 calls to the function at RVA.
"""
import argparse, struct, sys
import pefile, capstone

def load(path):
    pe = pefile.PE(path, fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXCEPTION']])
    return pe

def func_bounds(pe, rva):
    for e in getattr(pe, 'DIRECTORY_ENTRY_EXCEPTION', []):
        s = e.struct
        if s.BeginAddress <= rva < s.EndAddress:
            return s.BeginAddress, s.EndAddress
    return None

def read(pe, rva, n):
    return pe.get_data(rva, n)

def dis(pe, start, end, mark=None):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    base = pe.OPTIONAL_HEADER.ImageBase
    code = read(pe, start, end - start)
    for i in md.disasm(code, base + start):
        m = '=>' if mark is not None and i.address - base == mark else '  '
        print(f"{m} +{i.address-base:07X} {i.bytes.hex():<24} {i.mnemonic} {i.op_str}")

def callers(pe, target):
    text = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
    data = text.get_data(); va = text.VirtualAddress
    out = []
    i = data.find(b'\xe8')
    while i != -1 and i + 5 <= len(data):
        rel = struct.unpack_from('<i', data, i + 1)[0]
        if va + i + 5 + rel == target:
            out.append(va + i)
        i = data.find(b'\xe8', i + 1)
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('exe'); ap.add_argument('rva')
    ap.add_argument('--before', type=lambda x: int(x, 0), default=0x40)
    ap.add_argument('--after', type=lambda x: int(x, 0), default=0x40)
    ap.add_argument('--func', action='store_true')
    ap.add_argument('--callers', action='store_true')
    a = ap.parse_args()
    pe = load(a.exe); rva = int(a.rva, 16)
    fb = func_bounds(pe, rva)
    print(f"function containing +{rva:X}: {('+%X-+%X' % fb) if fb else 'unknown'}")
    if a.callers:
        t = fb[0] if fb else rva
        for c in callers(pe, t):
            f = func_bounds(pe, c)
            print(f"  call at +{c:X} in function +{f[0]:X}" if f else f"  call at +{c:X}")
        return
    if a.func and fb:
        dis(pe, fb[0], fb[1], rva)
    else:
        dis(pe, max(rva - a.before, 0), rva + a.after, rva)

if __name__ == '__main__':
    main()
