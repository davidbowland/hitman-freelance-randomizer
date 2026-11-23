"""Name CRC-32 ids (pins, properties) in a log or dump by the identifiers of some text files.

usage: python3 -I tools/re/crc_names.py LOG [--words FILE ...] [--grep TEXT]

Every identifier in the word files (default: the SDK's Glacier/Pins.h and Enums.h, and Reflection.h) is hashed with
CRC-32, the way the engine names pins and properties; in LOG, "pin xxxxxxxx" (the mods' unnamed pins) and
"0xXXXXXXXX" (tools/rpkg's unnamed properties) are replaced by the name when one hashes to it. Prints the result
(only the lines containing --grep's text, when given). Hex ids nothing names are left as they are. Property names
(m_fRadius, m_eInvestigateSoundDef, ...) are mostly not in the SDK headers: add a strings dump of HITMAN3.exe
(`strings -n 4 HITMAN3.exe > hm3strings.txt`) to --words for them.
"""
import argparse
import os
import re
import sys
import zlib

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GLACIER = os.path.join(REPO, "third_party", "devpkg", "include", "Glacier")
DEFAULT_WORDS = [os.path.join(GLACIER, n) for n in ("Pins.h", "Enums.h", "Reflection.h")]


def names_from(paths):
    names = {}
    for path in paths:
        if not os.path.exists(path):
            print("crc_names: %s not found; skipped" % path, file=sys.stderr)
            continue
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                for word in re.findall(r"[A-Za-z_][A-Za-z0-9_]*", line):
                    names.setdefault(zlib.crc32(word.encode()), word)
    return names


def rename(text, names):
    def pin(m):
        n = names.get(int(m.group(1), 16))
        return n if n else m.group(0)

    def hexid(m):
        n = names.get(int(m.group(1), 16))
        return n if n else m.group(0)

    text = re.sub(r"pin ([0-9a-f]{8})\b", pin, text)
    return re.sub(r"0x([0-9A-Fa-f]{8})\b", hexid, text)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--words", nargs="*", default=DEFAULT_WORDS)
    ap.add_argument("--grep")
    a = ap.parse_args(argv)
    names = names_from(a.words)
    with open(a.log, encoding="utf-8", errors="replace") as f:
        for line in f:
            if a.grep and a.grep not in line:
                continue
            sys.stdout.write(rename(line, names))


if __name__ == "__main__":
    main()
