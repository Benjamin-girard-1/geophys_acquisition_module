"""Shared setup for repository-local Python application launchers."""

from __future__ import annotations

import os
from pathlib import Path
import sys


def prepare_launcher(launcher: Path) -> None:
    """Use the repository environment and make the recording package importable."""
    launcher = launcher.resolve()
    repository = next(
        (
            directory for directory in launcher.parents
            if (directory / "ARCHITECTURE.md").is_file()
            and (directory / "version_2").is_dir()
        ),
        None,
    )
    if repository is None:
        raise SystemExit(f"Cannot find the repository root above {launcher}")

    environment = repository / ".venv"
    python = environment / "bin" / "python"
    if python.is_file() and Path(sys.prefix).resolve() != environment.resolve():
        os.execv(str(python), [str(python), *sys.orig_argv[1:]])

    recording_package = launcher.parent.parent.parent / "packages" / "recording"
    sys.path.insert(0, str(recording_package))
