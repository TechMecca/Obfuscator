# Obfuscator

Lightweight CMake + Python tool that applies **junk-code injection** and **string obfuscation** to selected C/C++ sources during your build. Obfuscation is **opt-in** and scoped via whitelist/exclude so you control exactly where it applies.

---

## Features
- 🧱 Junk-code injection 
- 🔐 String obfuscation for string literals (encode at build, decode at runtime)
- 🎯 Scoped via `--whitelist` and `--exclude`
- 🛠️ Works with Visual Studio / Ninja / Make through CMake
- 🐍 Python CLI with dry-run or in-place writes

---

## Requirements
- CMake
- C++ toolchain (MSVC / Clang / GCC)
- Python 3.x

---

## Quick Start
```bash
git clone https://github.com/TechMecca/Obfuscator.git
cd Obfuscator
cmake -S . -B out/build -DCMAKE_BUILD_TYPE=Release
cmake --build out/build --config Release
```

> Visual Studio (Windows): open the folder in **VS 2022** (CMake project) or choose a preset like `x86-release`.

---

## Usage

**Script path:** `External/Script/obfuscate.py`

Show all options (including string-obfuscation flags):
```bash
python External/Script/obfuscate.py --help
```

Dry run (no file writes):
```bash
python External/Script/obfuscate.py Obfuscator
```

Apply in place with scoping (example):
```bash
python External/Script/obfuscate.py Obfuscator --write   --whitelist src Include   --exclude src/hmac src/SHA
```

Windows (PowerShell):
```powershell
python .\External\Script\obfuscate.py .\Obfuscator --write `
  --whitelist src Include `
  --exclude src\hmac src\SHA
```

> **String obfuscation:** enable/configure it with the script’s flags shown in `--help` (e.g., scheme, keying, exclusions).

---

## Tips
- Commit before running with `--write` so you can review diffs or revert.
- Keep hot paths (crypto/tight loops) out of the whitelist.
- Re-runs should be idempotent (no repeated mangling).

---

## Troubleshooting

**`SyntaxError: Non-UTF-8 code starting with '\x96'`**  
Your Python file isn’t UTF-8 (often Windows-1252). Fix one of these ways:

- **Resave as UTF-8** in your editor and replace “smart” quotes/dashes with ASCII.
- **Temporary workaround** — add this at the top of `obfuscate.py`:
  ```python
  # -*- coding: cp1252 -*-
  ```

Changes not appearing?  
- Ensure `--write` is used.  
- Verify whitelist/exclude paths (names/separators/casing).  
- Confirm the transformation you expect is implemented (junk-code vs string obfuscation).  

---

## Structure
```
.
├─ CMakeLists.txt
├─ CMakePresets.json
├─ External/
│  └─ Script/
│     └─ obfuscate.py
└─ Obfuscator/
   └─ ... (your C/C++ sources, e.g., Warden/)
```

---

## Roadmap
- Additional string schemes (per-file keys, mixed transforms)
- More injection patterns beyond I/O paths
- Config file support (YAML/JSON)
- Per-file opt-out via comment pragmas
- Unit tests for transformation rules

---

## Contributing
PRs/issues welcome. For new patterns or string schemes, include a minimal before/after snippet and any perf notes.

---

## License (MIT)

Copyright (c) 2025 TechMecca

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
