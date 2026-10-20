#!/usr/bin/env python3
"""Developer CLI for the same engine used by the graphical Flatpak installer."""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from replay_installer import *  # Compatibility for existing imports and CLI users.
from replay_installer import main

if __name__ == "__main__":
    main()
