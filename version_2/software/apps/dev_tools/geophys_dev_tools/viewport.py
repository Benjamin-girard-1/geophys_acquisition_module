"""Choose visible curve points without hiding extrema at wide zoom levels."""

from __future__ import annotations

import math

import numpy as np
from numpy.typing import NDArray


def select_visible_extrema(
        x: NDArray[np.float64],
        y: NDArray[np.number],
        x_min: float,
        x_max: float,
        pixel_width: int,
) -> tuple[NDArray[np.float64], NDArray[np.number]]:
    """Return exact visible points or ordered minima/maxima for a dense view.

    ``x`` must be sorted in increasing order. The dense path divides the
    visible samples into at most ``pixel_width`` consecutive groups and keeps
    each group's true minimum and maximum at their original x positions. The
    first and last visible samples are included to preserve the curve edges.
    This function only reduces display data; callers retain the original
    samples or spectrum for calculations and closer zoom levels.
    """
    if x.ndim != 1 or y.ndim != 1 or x.size != y.size:
        raise ValueError("x and y must be one-dimensional arrays of equal size")
    if not math.isfinite(x_min) or not math.isfinite(x_max) or x_min > x_max:
        raise ValueError("visible x bounds must be finite and ordered")
    if pixel_width < 1:
        raise ValueError("pixel width must be positive")

    start = int(np.searchsorted(x, x_min, side="left"))
    stop = int(np.searchsorted(x, x_max, side="right"))
    count = stop - start
    if count <= 2 * pixel_width:
        return x[start:stop], y[start:stop]

    # Each group holds a similar number of samples. Recording times and FFT
    # frequencies are regularly spaced within uninterrupted segments.
    edges = start + np.arange(pixel_width + 1, dtype=np.int64) * count // pixel_width
    selected = np.empty(2 * pixel_width + 2, dtype=np.int64)
    selected[0] = start
    used = 1
    for left, right in zip(edges[:-1], edges[1:]):
        group = y[left:right]
        minimum = int(left + np.argmin(group))
        maximum = int(left + np.argmax(group))
        selected[used] = minimum
        selected[used + 1] = maximum
        used += 2
    selected[used] = stop - 1
    indices = np.unique(selected[:used + 1])
    return x[indices], y[indices]
