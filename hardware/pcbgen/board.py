"""The board these scripts work on: the current directory, or $BOARD.

Each board directory holds a netlist.py with its parts and the board's own
choices (NAME, TITLE, SCH_LAYOUT, SILK ...); the scripts write next to it.
"""
import os
import sys
from pathlib import Path

HERE = Path(os.environ.get("BOARD", ".")).resolve()
sys.path.insert(0, str(HERE))
import netlist as N  # noqa: E402

NAME = N.NAME
