#!/usr/bin/env python3
import hashlib
import sys
from pathlib import Path
import tempfile
import warnings
import zipfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from verify_preview_package import verify, BINARY, REQUIRED

files = {name: name.encode() for name in REQUIRED - {"SHA256SUMS.txt"}}
files["SHA256SUMS.txt"] = "".join(f"{hashlib.sha256(data).hexdigest()}  {name}\n" for name, data in sorted(files.items())).encode()
with tempfile.TemporaryDirectory() as td:
    base = Path(td)
    stage = base/"dist"
    for name, data in files.items():
        dest = stage/name
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
    assert verify(stage) == hashlib.sha256(files[BINARY]).hexdigest()
    path = base/"package.zip"
    for kind in ("valid", "duplicate", "case-duplicate", "traversal", "tamper", "extra", "missing"):
        with warnings.catch_warnings(), zipfile.ZipFile(path, "w") as out:
            warnings.simplefilter("ignore", UserWarning)
            for name, data in files.items():
                if kind == "missing" and name == BINARY:
                    continue
                out.writestr(name, data+b"x" if kind == "tamper" and name == BINARY else data)
            if kind == "duplicate": out.writestr(BINARY, files[BINARY])
            if kind == "case-duplicate": out.writestr(BINARY.upper(), files[BINARY])
            if kind == "traversal": out.writestr("../escape", "x")
            if kind == "extra": out.writestr("old.vst3", "x")
        try:
            verify(path)
        except ValueError:
            assert kind != "valid", "valid package rejected"
        else:
            assert kind == "valid", f"accepted {kind} package"
        print(f"PASS: {kind}")
