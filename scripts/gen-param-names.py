#!/usr/bin/env python3
"""Generate src/core/DifficultyParamNames.inc from the SDK's Glacier/Enums.h.

Usage: gen-param-names.py <Enums.h> <out.inc>

Emits one line per EDifficultyParameter enumerator (except EDP_Invalid and
EDP_COUNT) in the form: {"EDP_Name", 12},
"""
import re
import sys

SKIP = {"EDP_Invalid", "EDP_COUNT"}


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    src, out = sys.argv[1], sys.argv[2]
    with open(src, encoding="utf-8") as f:
        text = f.read()

    block = re.search(r"enum\s+class\s+EDifficultyParameter\s*:\s*int32_t\s*\{(.*?)\};", text, re.S)
    if not block:
        print(f"EDifficultyParameter not found in {src}", file=sys.stderr)
        return 1

    entries = []
    for name, value in re.findall(r"^\s*(EDP_\w+)\s*=\s*(-?\d+)\s*,?", block.group(1), re.M):
        if name not in SKIP:
            entries.append((name, int(value)))
    if not entries:
        print("no enumerators parsed", file=sys.stderr)
        return 1

    with open(out, "w", encoding="utf-8", newline="\n") as f:
        for name, value in entries:
            f.write(f'{{"{name}", {value}}},\n')
    print(f"wrote {len(entries)} entries to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
