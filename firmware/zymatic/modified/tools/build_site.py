"""Compatibility entry point for the local website build (npm ci --prefix tools)."""
from pathlib import Path
import subprocess

if __name__ == "__main__":
    subprocess.run(["node", Path(__file__).with_suffix(".cjs")], check=True)
