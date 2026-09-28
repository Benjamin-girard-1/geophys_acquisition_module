"""Convenient repository-local launcher for the desktop host application."""

from pathlib import Path
import sys

DATA_PACKAGE_ROOT = (
    Path(__file__).resolve().parents[2] / "packages" / "data"
)
sys.path.insert(0, str(DATA_PACKAGE_ROOT))

from geophys_host.gui import main  # noqa: E402


if __name__ == "__main__":
    raise SystemExit(main())
