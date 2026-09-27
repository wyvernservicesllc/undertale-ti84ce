#!/usr/bin/env python3
"""Names of code entries: codename.py <vmout dir> id..."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from cepack import CodeBin

cb = CodeBin(Path(sys.argv[1]) / "code.bin")
for a in sys.argv[2:]:
    print(a, cb.code[int(a)][2])
