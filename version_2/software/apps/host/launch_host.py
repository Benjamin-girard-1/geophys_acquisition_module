"""Convenient repository-local launcher for the desktop host application."""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from launcher_support import prepare_launcher  # noqa: E402

prepare_launcher(Path(__file__))

from geophys_host.gui import main  # noqa: E402


if __name__ == "__main__":
    raise SystemExit(main())
