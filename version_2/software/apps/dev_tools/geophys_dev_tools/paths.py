"""Locations shared by the recording diagnostic tools."""

from __future__ import annotations

import os
from pathlib import Path


def acquisition_data_directory() -> Path:
    """Find the sibling data folder, with an override for other installations."""
    override = os.environ.get("GEOPHYS_ACQUISITION_DATA")
    if override:
        return Path(override).expanduser().resolve()

    repository = next(
        (
            directory for directory in Path(__file__).resolve().parents
            if (directory / "ARCHITECTURE.md").is_file()
            and (directory / "version_2").is_dir()
        ),
        None,
    )
    if repository is None:
        raise FileNotFoundError(
            "Cannot find the repository root; set GEOPHYS_ACQUISITION_DATA "
            "to the recording directory"
        )
    return repository.parent / "geophys_experiments"
