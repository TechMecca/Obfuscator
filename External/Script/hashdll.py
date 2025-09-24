#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
hashdll.py - Print MD5, SHA1, and SHA256 for one or more files (e.g., DLLs).

Usage:
  python hashdll.py path\to\file.dll
  python hashdll.py file1.dll file2.dll
"""

import argparse
import hashlib
import sys
from pathlib import Path
from typing import Iterable

CHUNK_SIZE = 1024 * 1024  # 1 MiB


def iter_paths(inputs: Iterable[str]) -> Iterable[Path]:
    for raw in inputs:
        p = Path(raw)
        if p.is_dir():
            yield from (c for c in p.rglob("*") if c.is_file())
        else:
            if not p.exists():
                for g in Path().glob(raw):
                    if g.is_file():
                        yield g
            else:
                yield p


def hash_file(path: Path) -> dict:
    md5 = hashlib.md5()
    sha1 = hashlib.sha1()
    sha256 = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(CHUNK_SIZE), b""):
            md5.update(chunk)
            sha1.update(chunk)
            sha256.update(chunk)
    st = path.stat()
    return {
        "path": str(path.resolve()),
        "size": st.st_size,
        "md5": md5.hexdigest(),
        "sha1": sha1.hexdigest(),
        "sha256": sha256.hexdigest(),
    }


def print_compact(h: dict) -> None:
    lines = [
        #f"[*]File  : {h['path']}",
        f"[*]Size  : {h['size']} bytes",
        f"[*]MD5   : {h['md5']}",
        f"[*]SHA1  : {h['sha1']}",
        f"[*]SHA256: {h['sha256']}",
    ]
    # Exactly one blank line between items; final CRLF at end.
    out = ("\n".join(lines) + "\n").encode("utf-8", "replace")
    sys.stdout.buffer.write(out)


def main():
    ap = argparse.ArgumentParser(description="Print MD5, SHA1, and SHA256 for files (e.g., DLLs).")
    ap.add_argument("files", nargs="+", help="File(s) or directories (recursively scanned). Globs allowed.")
    ap.add_argument("--pretty", action="store_true", help="Use multi-line labeled output.")
    args = ap.parse_args()

    any_done = False
    for p in iter_paths(args.files):
        if not p.exists() or not p.is_file():
            continue
        try:
            h = hash_file(p)
        except (PermissionError, OSError) as e:
            print(f"[error] {p}: {e}")
            continue

        any_done = True
        (print_pretty if args.pretty else print_compact)(h)

    if not any_done:
        print("No files hashed. Check your paths or permissions.")


if __name__ == "__main__":
    main()
