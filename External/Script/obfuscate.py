#!/usr/bin/env python3
"""
SafeCppObfuscator � ONLY wrap strings used with:
  � std::cout/cerr/clog (via <<)  ? uses OBS_CSTR(...) to keep ostream overloads happy
  � printf-family calls            ? uses OBS/OBS_* by literal prefix
  � Windows MessageBox* calls      ? uses OBS_CSTR(...) for all MessageBox variants

Also:
  � Adds #include <Junk.h> and #include <StringObfuscator.h> to sources
  � Braces single-statement if/else bodies
  � Injects junk macros inside function/method/lambda bodies and before return

Dry-run by default; use --write to modify files.

Usage:
  python obfuscate.py <project_root> [--write] [--whitelist src Include] [--exclude src/hmac src/SHA] [--debug]
"""

import re
import sys
import argparse
import random
from pathlib import Path
from typing import Set, Tuple

# ---------------- debug helpers ----------------
DEBUG = False
CURRENT_FILE = ""

def _dbg(msg: str):
    if DEBUG:
        print(msg)

def _sanitize_preview(s: str, max_len: int = 80) -> str:
    # Strip newlines, compress spaces, and limit length to keep logs tidy
    one_line = ''.join(ch if ch not in '\r\n' else ' ' for ch in s)
    one_line = re.sub(r'\s+', ' ', one_line).strip()
    if len(one_line) > max_len:
        return one_line[:max_len-3] + '...'
    return one_line

def _line_col_from_pos(text: str, pos: int) -> Tuple[int, int]:
    # 1-based line/column
    line = text.count('\n', 0, pos) + 1
    last_nl = text.rfind('\n', 0, pos)
    col = pos - (last_nl + 1) + 1
    return line, col

# ---------------- generic helpers ----------------
def _skip_ws(s, i):
    n = len(s)
    while i < n and s[i].isspace():
        i += 1
    return i

def _is_ident_char(ch):
    return ch.isalnum() or ch == '_'

def _read_ident(s, i):
    n = len(s)
    j = i
    if j < n and (s[j].isalpha() or s[j] == '_'):
        j += 1
        while j < n and (s[j].isalnum() or s[j] == '_'):
            j += 1
        return s[i:j], j
    return "", i

def _bol(s, i):
    while i > 0 and s[i-1] not in '\r\n':
        i -= 1
    return i

def _is_preproc_bol(s, pos):
    b = _bol(s, pos)
    k = b
    while k < len(s) and s[k] in ' \t':
        k += 1
    return k < len(s) and s[k] == '#'

def _skip_preproc_line(s, i):
    if not _is_preproc_bol(s, i):
        return i
    n = len(s)
    while i < n:
        while i < n and s[i] not in '\r\n':
            i += 1
        back = i - 1
        while back >= 0 and s[back] in ' \t':
            back -= 1
        if back >= 0 and s[back] == '\\':
            if i < n and s[i] == '\r': i += 1
            if i < n and s[i] == '\n': i += 1
            continue
        break
    if i < n and s[i] == '\r': i += 1
    if i < n and s[i] == '\n': i += 1
    return i

def _scan_to_matching_paren(s, i):
    n = len(s)
    if i >= n or s[i] != '(':
        return -1
    depth = 0
    while i < n:
        ch = s[i]
        # // comment
        if ch == '/' and i+1 < n and s[i+1] == '/':
            i += 2
            while i < n and s[i] != '\n':
                i += 1
            continue
        # /* */ comment
        if ch == '/' and i+1 < n and s[i+1] == '*':
            i += 2
            while i+1 < n and not (s[i] == '*' and s[i+1] == '/'):
                i += 1
            i += 2
            continue
        # string/char
        if ch in ('"', "'"):
            q = ch
            i += 1
            while i < n:
                if s[i] == '\\':
                    i += 2
                    continue
                if s[i] == q:
                    i += 1
                    break
                i += 1
            continue
        if ch == '(':
            depth += 1
            i += 1
            continue
        if ch == ')':
            depth -= 1
            i += 1
            if depth == 0:
                return i - 1
            continue
        i += 1
    return -1

def _scan_simple_stmt_end(s, i):
    n = len(s)
    dp = db = dc = 0
    while i < n:
        ch = s[i]
        if ch == '/' and i+1 < n and s[i+1] == '/':
            i += 2
            while i < n and s[i] != '\n':
                i += 1
            continue
        if ch == '/' and i+1 < n and s[i+1] == '*':
            i += 2
            while i+1 < n and not (s[i] == '*' and s[i+1] == '/'):
                i += 1
            i += 2
            continue
        if ch in ('"', "'"):
            q = ch
            i += 1
            while i < n:
                if s[i] == '\\':
                    i += 2
                    continue
                if s[i] == q:
                    i += 1
                    break
                i += 1
            continue
        if ch == '(':
            dp += 1
        elif ch == ')':
            dp = max(0, dp-1)
        elif ch == '[':
            db += 1
        elif ch == ']':
            db = max(0, db-1)
        elif ch == '{':
            dc += 1
        elif ch == '}':
            dc = max(0, dc-1)
        elif ch == ';' and dp == db == dc == 0:
            return i
        i += 1
    return -1

def brace_single_line_if_else_safe(content, indent_with="    ", passes=1):
    s = content
    for _ in range(max(1, passes)):
        n = len(s)
        i = 0
        out = []
        changed = False
        def leading_indent(pos):
            b = _bol(s, pos)
            j = b
            while j < n and s[j] in ' \t':
                j += 1
            return s[b:j]
        while i < n:
            if _is_preproc_bol(s, i):
                ni = _skip_preproc_line(s, i)
                out.append(s[i:ni])
                i = ni
                continue
            if s.startswith('if', i) and (i+2 == len(s) or not _is_ident_char(s[i+2])):
                j = _skip_ws(s, i+2)
                if j < n and s[j] == '(':
                    close = _scan_to_matching_paren(s, j)
                    if close != -1:
                        k = _skip_ws(s, close+1)
                        if k < n and s[k] == '{':
                            out.append(s[i:k]); i = k; continue
                        end = _scan_simple_stmt_end(s, k)
                        if end != -1:
                            indent = leading_indent(i)
                            body = s[k:end].rstrip()
                            out.append(s[i:k]); out.append('{')
                            out.append('\n' + indent + indent_with + (body if body.endswith(';') else body + ';'))
                            out.append('\n' + indent + '}')
                            i = end + 1; changed = True; continue
            if s.startswith('else', i) and (i+4 == len(s) or not _is_ident_char(s[i+4])):
                j = _skip_ws(s, i+4)
                if s.startswith('if', j) and (j+2 == len(s) or not _is_ident_char(s[j+2])):
                    out.append(s[i:j]); i = j; continue
                if j < n and s[j] == '{':
                    out.append(s[i:j]); i = j; continue
                end = _scan_simple_stmt_end(s, j)
                if end != -1:
                    indent = leading_indent(i)
                    body = s[j:end].rstrip()
                    out.append(s[i:j]); out.append('{')
                    out.append('\n' + indent + indent_with + (body if body.endswith(';') else body + ';'))
                    out.append('\n' + indent + '}')
                    i = end + 1; changed = True; continue
            out.append(s[i]); i += 1
        joined = ''.join(out)
        if not changed:
            return joined
        s = joined
    return s

# ---------- literal helpers ----------
_STRING_PREFIXES = ('u8R"', 'LR"', 'uR"', 'UR"', 'u8"', 'L"', 'u"', 'U"', 'R"', '"')

def _classify_prefix(s, pos):
    for p in _STRING_PREFIXES:
        if s.startswith(p, pos):
            is_raw = p.endswith('R"') or p == 'R"'
            return (p[:-1] if p != '"' else ''), pos + len(p), is_raw, True
    return None, pos, False, False

def _macro_for_prefix(pfx):
    return {
        '':   'OBS',
        'u8': 'OBS_U8',
        'L':  'OBS_W',
        'u':  'OBS_U16',
        'U':  'OBS_U32',
        'R':  'OBS_R',
        'u8R':'OBS_RU8',
        'LR': 'OBS_RW',
        'uR': 'OBS_RU16',
        'UR': 'OBS_RU32',
    }.get(pfx, 'OBS')

def _collect_one_literal(s, i, pfx, is_raw):
    n = len(s)
    if is_raw:
        delim = ''
        j = i
        while j < n and s[j] != '(':
            delim += s[j]; j += 1
        term = ')' + delim + '"'
        if j < n and s[j] == '(':
            j += 1
        while j < n:
            if s.startswith(term, j):
                return j + len(term), term
            j += 1
        return n, term
    else:
        j = i
        while j < n:
            ch = s[j]
            if ch == '\\' and j+1 < n:
                j += 2
                continue
            if ch == '"':
                return j + 1, None
            j += 1
        return j, None

def _wrap_group_text(group_text, pfx):
    return f"{_macro_for_prefix(pfx)}({group_text})"

def _wrap_iostream_group_text(group_text, pfx):
    """
    For iostream chains, prefer C-string so operator<< is unambiguous.
    Narrow prefixes -> OBS_CSTR(...). Others fall back to generic wrapper.
    """
    if pfx in {'', 'u8', 'R', 'u8R'}:
        return f"OBS_CSTR({group_text})"
    return _wrap_group_text(group_text, pfx)

# MessageBox* ? ALWAYS OBS_CSTR(...) per request
def _wrap_msgbox_group_text(group_text, pfx, fname):
    return f"OBS_CSTR({group_text})"

# ---------- targeted wrappers ----------
_IOSTREAM_STREAMS = {'std::cout', 'std::cerr', 'std::clog', 'cout', 'cerr', 'clog'}
_PRINTF_FUNCS     = {'printf', 'fprintf', 'sprintf', 'snprintf', '_snprintf', 'puts', 'fputs'}
_WIN_MSGBOX_FUNCS = {
    'MessageBox', 'MessageBoxA', 'MessageBoxW',
    'MessageBoxExA', 'MessageBoxExW',
    'MessageBoxTimeoutA', 'MessageBoxTimeoutW',
}

def wrap_only_iostream_printf_msgbox(text: str) -> str:
    """
    Wrap string literals only in:
      - insertion chains that start with std::cout/cerr/clog (literals after '<<') ? OBS_CSTR
      - printf-family calls (any literal args inside (...))                       ? OBS/OBS_*
      - MessageBox* calls (any literal args inside (...))                         ? OBS_CSTR
    """
    s = text
    n = len(s)
    i = 0
    out = []
    changed = False
    in_line = False
    in_block = False
    in_char = False

    def at_stream_start(idx):
        j = idx
        name, j2 = _read_ident(s, j)
        if name == 'std':
            j = j2
            if j < n and s[j] == ':' and j+1 < n and s[j+1] == ':':
                j += 2
                name2, j3 = _read_ident(s, j)
                if name2:
                    return 'std::' + name2, j3
            return '', idx
        elif name:
            return name, j2
        return '', idx

    while i < n:
        ch = s[i]

        # comments
        if in_line:
            out.append(ch)
            if ch == '\n':
                in_line = False
            i += 1
            continue
        if in_block:
            out.append(ch)
            if ch == '*' and i+1 < n and s[i+1] == '/':
                out.append('/')
                i += 2
                in_block = False
            else:
                i += 1
            continue

        # start comment
        if ch == '/' and i+1 < n and s[i+1] == '/':
            out.append('//'); i += 2; in_line = True; continue
        if ch == '/' and i+1 < n and s[i+1] == '*':
            out.append('/*'); i += 2; in_block = True; continue

        # char literal
        if not in_char and ch == "'":
            out.append(ch); i += 1; in_char = True; continue
        if in_char:
            if ch == '\\' and i+1 < n:
                out.append(s[i:i+2]); i += 2; continue
            out.append(ch); i += 1
            if ch == "'":
                in_char = False
            continue

        # preprocessor line
        if _is_preproc_bol(s, i):
            start = i
            while i < n and s[i] not in '\r\n':
                out.append(s[i]); i += 1
            if i < n and s[i] == '\r': out.append('\r'); i += 1
            if i < n and s[i] == '\n': out.append('\n'); i += 1
            continue

        # ---- iostream chains ----
        name, pos_after = at_stream_start(i)
        if name in _IOSTREAM_STREAMS:
            out.append(s[i:pos_after]); i = pos_after
            stmt_end = _scan_simple_stmt_end(s, i)
            if stmt_end == -1: stmt_end = n - 1
            j = i
            while j <= stmt_end:
                if j + 1 <= stmt_end and s[j] == '<' and s[j+1] == '<':
                    out.append('<<'); j += 2
                    k = _skip_ws(s, j)
                    pfx, nextpos, is_raw, opened = _classify_prefix(s, k)
                    if opened:
                        lit_end, _ = _collect_one_literal(s, nextpos, pfx, is_raw)
                        j2 = lit_end
                        # adjacent literals
                        while True:
                            ws = j2
                            while ws <= stmt_end and s[ws].isspace():
                                ws += 1
                            p2, n2, r2, o2 = _classify_prefix(s, ws)
                            if o2:
                                lit_end2, _ = _collect_one_literal(s, n2, p2, r2)
                                j2 = lit_end2
                                continue
                            break
                        group_text = s[k:j2]
                        out.append(_wrap_iostream_group_text(group_text, pfx))
                        changed = True
                        if DEBUG:
                            line, col = _line_col_from_pos(s, k)
                            _dbg(f"   [wrap] iostream  {CURRENT_FILE}:{line}:{col}  {_sanitize_preview(group_text)}")
                        j = j2
                        continue
                    else:
                        out.append(s[j:k]); j = k; continue
                out.append(s[j]); j += 1
            i = j
            continue

        # ---- printf-family & MessageBox* calls ----
        ident, id_end = _read_ident(s, i)
        if ident in _PRINTF_FUNCS or ident in _WIN_MSGBOX_FUNCS:
            k = _skip_ws(s, id_end)
            if k < n and s[k] == '(':
                out.append(s[i:k+1])  # up to '(' inclusive
                paren_end = _scan_to_matching_paren(s, k)
                j = k + 1
                while j <= paren_end:
                    if s[j].isspace():
                        out.append(s[j]); j += 1; continue
                    pfx, nxt, is_raw, opened = _classify_prefix(s, j)
                    if opened:
                        lit_end, _ = _collect_one_literal(s, nxt, pfx, is_raw)
                        j2 = lit_end
                        # include adjacent literals
                        while True:
                            ws = j2
                            while ws <= paren_end and s[ws].isspace():
                                ws += 1
                            p2, n2, r2, o2 = _classify_prefix(s, ws)
                            if o2:
                                lit_end2, _ = _collect_one_literal(s, n2, p2, r2)
                                j2 = lit_end2
                                continue
                            break
                        group = s[j:j2]
                        if ident in _WIN_MSGBOX_FUNCS:
                            wrapped = _wrap_msgbox_group_text(group, pfx, ident)
                            out.append(wrapped)
                            if DEBUG:
                                line, col = _line_col_from_pos(s, j)
                                _dbg(f"   [wrap] msgbox    {CURRENT_FILE}:{line}:{col}  {_sanitize_preview(group)}")
                        else:
                            wrapped = _wrap_group_text(group, pfx)
                            out.append(wrapped)
                            if DEBUG:
                                line, col = _line_col_from_pos(s, j)
                                _dbg(f"   [wrap] printf    {CURRENT_FILE}:{line}:{col}  {_sanitize_preview(group)}")
                        changed = True
                        j = j2
                        continue
                    out.append(s[j]); j += 1
                i = paren_end + 1
                continue

        # default
        out.append(ch); i += 1

    return ''.join(out) if changed else text

# ---------------- obfuscator core ----------------
class SafeCppObfuscator:
    def __init__(self, junk_header="Junk.h", obf_header="StringObfuscator.h"):
        self.junk_header = junk_header
        self.obf_header  = obf_header
        self.stats = {'functions_obfuscated':0,'returns_obfuscated':0,'strings_wrapped':0,'files_processed':0}
        self.SRC_EXTS = {'.cpp','.cxx','.cc','.c'}
        self.HDR_EXTS = {'.h','.hpp','.hxx','.hh'}
        self.junk_macros = [
            "JUNK_CODE_BLOCK();",
            "JUNK_CODE_BLOCK_ADVANCED();",
            "JUNK_CODE_BLOCK(); JUNK_CODE_BLOCK();",
            "JUNK_CODE_BLOCK_ADVANCED(); JUNK_CODE_BLOCK_ADVANCED();",
        ]
        self.exclude_functions = {'malloc','free','new','delete','operatornew','operatordelete'}
        self.function_pattern = re.compile(r'''( (?: (?:[A-Za-z_]\w*\s+)+ [A-Za-z_]\w* ) \s*\([^)]*\)\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?\s*\{ )''', re.VERBOSE | re.MULTILINE)
        self.method_pattern   = re.compile(r'''( (?:[A-Za-z_]\w*::)+ ~?[A-Za-z_]\w* \s*\([^)]*\)\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?\s*\{ )''', re.VERBOSE | re.MULTILINE)
        self.lambda_pattern   = re.compile(r'''( \[[^\]]*\]\s*\([^)]*\)\s*(?:mutable\s*)?(?:noexcept\s*)?(?:->\s*[^{]*)?\s*\{ )''', re.VERBOSE | re.MULTILINE)
        self.return_pattern   = re.compile(r'(^[ \t]*)return\s+([^;]+);', re.MULTILINE)

    def _should_obf_fn(self, decl: str) -> bool:
        m = re.search(r'(\w+)\s*\(', decl)
        if m and m.group(1) in self.exclude_functions:
            return False
        return len(decl.strip()) >= 20

    def _junk(self) -> str:
        j = random.choice(self.junk_macros)
        if random.random() < 0.2:
            j += " " + random.choice(self.junk_macros)
        return j

    def _obf_fn_body(self, m: re.Match) -> str:
        decl = m.group(1)
        if not self._should_obf_fn(decl):
            return decl
        self.stats['functions_obfuscated'] += 1
        return decl + "\n    " + self._junk() + "\n"

    def _obf_returns(self, s: str) -> str:
        def repl(m: re.Match) -> str:
            indent, expr = m.group(1), m.group(2)
            self.stats['returns_obfuscated'] += 1
            return f"{indent}{self._junk()}\n{indent}return {expr};"
        return self.return_pattern.sub(repl, s)

    def _add_header(self, content: str, header: str, insert_if_missing=True) -> str:
        base = Path(header).name
        any_inc = re.compile(rf'^\s*#\s*include\s*[<"](?:[^>"/]*/)*{re.escape(base)}[>"]\s*$', re.MULTILINE)
        content = any_inc.sub(f'#include <{base}>', content)
        has = re.search(rf'^\s*#\s*include\s*<\s*{re.escape(base)}\s*>\s*$', content, re.MULTILINE)
        if has or not insert_if_missing:
            return content
        lines = content.splitlines()
        insert_line = 0
        for i, line in enumerate(lines):
            st = line.strip()
            if st.startswith('#include') or st.startswith('#pragma once'):
                insert_line = i + 1
            elif st and not st.startswith('//'):
                break
        lines.insert(insert_line, f'#include <{base}>')
        return '\n'.join(lines)

    def process_file(self, path: Path, *, write=False, max_bytes: int = 524288) -> bool:
        global CURRENT_FILE
        try:
            if path.is_symlink(): return False
            if path.stat().st_size > max_bytes: return False
        except Exception:
            return False

        CURRENT_FILE = str(path)
        print(f" Processing: {path}")
        try:
            txt = path.read_text(encoding='utf-8', errors='ignore')
        except Exception as e:
            print(f"   Skip (read error): {e}")
            return False

        orig = txt
        s0 = dict(self.stats)
        suf = path.suffix.lower()
        is_src = suf in self.SRC_EXTS
        is_hdr = suf in self.HDR_EXTS

        if is_hdr:
            txt = self._add_header(txt, self.junk_header, insert_if_missing=False)
            txt = self._add_header(txt, self.obf_header,  insert_if_missing=False)
        elif is_src:
            txt = self._add_header(txt, self.junk_header, insert_if_missing=True)
            txt = self._add_header(txt, self.obf_header,  insert_if_missing=True)
            txt = brace_single_line_if_else_safe(txt, passes=1)
            txt = self.function_pattern.sub(self._obf_fn_body, txt)
            txt = self.method_pattern.sub(self._obf_fn_body, txt)
            txt = self.lambda_pattern.sub(self._obf_fn_body, txt)
            txt = self._obf_returns(txt)
            wrapped = wrap_only_iostream_printf_msgbox(txt)
            if wrapped != txt:
                self.stats['strings_wrapped'] += max(0,
                    wrapped.count('OBS(')+wrapped.count('OBS_U8(')+wrapped.count('OBS_W(')+
                    wrapped.count('OBS_U16(')+wrapped.count('OBS_U32(')+wrapped.count('OBS_R(')+
                    wrapped.count('OBS_RU8(')+wrapped.count('OBS_RW(')+wrapped.count('OBS_RU16(')+
                    wrapped.count('OBS_RU32(')+wrapped.count('OBS_CSTR(')
                    - (txt.count('OBS(')+txt.count('OBS_U8(')+txt.count('OBS_W(')+
                       txt.count('OBS_U16(')+txt.count('OBS_U32(')+txt.count('OBS_R(')+
                       txt.count('OBS_RU8(')+txt.count('OBS_RW(')+txt.count('OBS_RU16(')+
                       txt.count('OBS_RU32(')+txt.count('OBS_CSTR(')))
                txt = wrapped
        else:
            return False

        if txt != orig:
            if write:
                try:
                    path.with_suffix(path.suffix + '.bak').write_text(orig, encoding='utf-8')
                    path.write_text(txt, encoding='utf-8')
                    print("   WROTE (backup .bak created)")
                except Exception as e:
                    print(f"   ERROR write: {e}")
                    return False
            else:
                print("   (dry-run) would modify")
            if is_src:
                df = self.stats['functions_obfuscated'] - s0['functions_obfuscated']
                dr = self.stats['returns_obfuscated']   - s0['returns_obfuscated']
                ds = self.stats['strings_wrapped']      - s0['strings_wrapped']
                print(f"   Functions:+{df} Returns:+{dr} Strings:+{ds}")
            self.stats['files_processed'] += 1
            return True
        else:
            print("   No changes")
            return False

    def process_tree(self, root: Path, *, write=False, max_bytes: int = 524288,
                     whitelist: Set[str] = None, excludes: Set[str] = None) -> int:
        processed = 0
        wl = {w.strip('/\\') for w in (whitelist or {'src', 'Include'})}
        ex = {e.strip('/\\') for e in (excludes or set())}
        hard_skip_names = {
            '.git','.hg','.svn','.vs','.idea','__pycache__','out','build','Build',
            'CMakeFiles','cmake-build-debug','cmake-build-release','source_backup',
            'External','third_party','3rdparty'
        }

        root = root.resolve()
        for p in root.rglob('*'):
            try:
                if p.is_symlink():
                    continue
            except Exception:
                continue
            try:
                rel = p.relative_to(root)
            except Exception:
                continue
            parts = list(rel.parts)
            if not parts:
                continue
            top = parts[0]
            if wl and top not in wl:
                continue
            if any(name in hard_skip_names for name in parts):
                continue
            rel_posix = rel.as_posix()
            if any(seg in rel_posix for seg in (ex or set())):
                continue
            if p.is_file() and p.suffix.lower() in (self.SRC_EXTS | self.HDR_EXTS):
                if self.process_file(p, write=write, max_bytes=max_bytes):
                    processed += 1
        return processed

    def print_stats(self):
        print("\nStats:")
        for k, v in self.stats.items():
            print(f"  {k}: {v}")

# ---------------- CLI ----------------
def main():
    global DEBUG
    ap = argparse.ArgumentParser(description="Safe C++ obfuscator (iostream/printf/MessageBox; dry-run by default)")
    ap.add_argument('path', help='Project root to scan')
    ap.add_argument('--write', action='store_true', help='Apply changes (default: dry-run)')
    ap.add_argument('--max-bytes', type=int, default=524288, help='Skip files larger than this')
    ap.add_argument('--whitelist', nargs='*', default=['src','Include'],
                    help='Top-level dirs to process under root')
    ap.add_argument('--exclude', nargs='*', default=['src/hmac','src/SHA'],
                    help='Subpath substrings to exclude')
    ap.add_argument('--debug', action='store_true', help='Print which strings are being wrapped')
    args = ap.parse_args()

    DEBUG = bool(args.debug)

    root = Path(args.path)
    if not root.exists():
        print(f"Path not found: {root}")
        sys.exit(1)

    obf = SafeCppObfuscator()
    count = obf.process_tree(root, write=args.write, max_bytes=args.max_bytes,
                             whitelist=set(args.whitelist), excludes=set(args.exclude))
    print(f"\nProcessed files: {count}")
    obf.print_stats()

if __name__ == '__main__':
    main()
