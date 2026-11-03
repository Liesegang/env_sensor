"""Compatibility entry point for the shared environment sensor protocol."""
import runpy
from pathlib import Path
runpy.run_path(str(Path(__file__).resolve().parents[2] / "shared/protocol/generate.py"), run_name="__main__")
