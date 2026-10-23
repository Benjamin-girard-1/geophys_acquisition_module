#!/usr/bin/env python3
"""Check the nominal, low-frequency behavior of the local SPICE models."""

from __future__ import annotations

import math
import re
import subprocess
import tempfile
from pathlib import Path


LIB = Path(__file__).resolve().parent.parent
NGSPICE = "ngspice"


def run(deck: str) -> str:
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary) / "model_check.cir"
        path.write_text(deck, encoding="utf-8")
        completed = subprocess.run(
            [NGSPICE, "-b", str(path)],
            capture_output=True,
            text=True,
            timeout=15,
            check=False,
        )
    output = completed.stdout + completed.stderr
    if completed.returncode or re.search(r"(?im)^(?:error|warning):", output):
        raise AssertionError(output)
    return output


def value(output: str, name: str) -> float:
    match = re.search(
        rf"(?m)^{re.escape(name)}\s*=\s*([+-]?\d+(?:\.\d+)?e[+-]?\d+)",
        output,
    )
    if not match:
        raise AssertionError(f"Missing {name} in ngspice output:\n{output}")
    return float(match.group(1))


def close(name: str, actual: float, expected: float, relative: float = 0.01) -> None:
    if not math.isclose(actual, expected, rel_tol=relative, abs_tol=1e-12):
        raise AssertionError(f"{name}: {actual:.9g}, expected {expected:.9g}")
    print(f"PASS {name}: {actual:.9g} (expected {expected:.9g})")


includes = "\n".join(
    f'.include "{LIB / filename}"'
    for filename in (
        "HMC1001_functional.lib",
        "Helmholtz_pair.lib",
        "INA851_functional.lib",
        "OPA1197_functional.lib",
    )
)

dc = run(
    f"""Nominal model checks
{includes}
VBRIDGE vb 0 5.67
VFIELD field 0 1
XH vb hp hn 0 field HMC1001_SPICE
VSRC drive 0 0.23
XC drive 0 coilfield HELMHOLTZ_PAIR
VCC vcc 0 9
VOCM vocm 0 2.835
VINP inp 0 2.83505
VINN inn 0 2.83495
RGAIN rgp rgn 120
XI inp inn rgp rgn vcc 0 outp outn vocm fdp fdn g2p g2n vcc 0 INA851_FUNC
VINOP opin 0 2.835
XO opin opout vcc 0 opout OPA1197_FUNC
RLOAD opout 0 10k
.control
op
print v(hp,hn) i(vbridge) v(coilfield) i(vsrc) v(outp,outn) v(opout)
quit
.endc
.end
"""
)
close("HMC1001 sensitivity at 5.67 V, 1 uT", value(dc, "v(hp,hn)"), 5.67 * 3.2e-3 / 100)
close("HMC1001 bridge resistance", -5.67 / value(dc, "i(vbridge)"), 850)
radius = 0.2952 / 2
separation = 0.145
field_gain = (
    1e6
    * 4
    * math.pi
    * 1e-7
    * 5
    * radius**2
    / (radius**2 + (separation / 2) ** 2) ** 1.5
)
close("Helmholtz field, uT/A", value(dc, "v(coilfield)"), field_gain)
close("Helmholtz DC resistance", -0.23 / value(dc, "i(vsrc)"), 0.23)
close("INA851 differential output at G=51", value(dc, "v(outp,outn)"), 51 * 110e-6)
close("OPA2197 DC follower", value(dc, "v(opout)"), 2.835)

ac = run(
    f"""AC model checks
{includes}
VBRIDGE vb 0 5.67
VFIELD field 0 DC 0 AC 1
XH vb hp hn 0 field HMC1001_SPICE
VSRC drive 0 DC 0 AC 1
XC drive 0 coilfield HELMHOLTZ_PAIR
VCC vcc 0 9
VOCM vocm 0 2.835
VINP inp 0 DC 2.835 AC 1
VINN inn 0 2.835
RGAIN rgp rgn 120
XI inp inn rgp rgn vcc 0 outp outn vocm fdp fdn g2p g2n vcc 0 INA851_FUNC
VINOP opin 0 DC 2.835 AC 1
XO opin opout vcc 0 opout OPA1197_FUNC
RLOAD opout 0 10k
.control
ac lin 1 1 1
print mag(v(hp,hn)) mag(v(outp,outn))
ac lin 1 1k 1k
print mag(i(vsrc))
ac lin 1 10Meg 10Meg
print mag(v(opout))
quit
.endc
.end
"""
)
close("HMC1001 AC sensitivity", value(ac, "mag(v(hp,hn))"), 5.67 * 3.2e-3 / 100)
close("INA851 AC differential gain at G=51", value(ac, "mag(v(outp,outn))"), 51)
coil_impedance = math.hypot(0.23, 2 * math.pi * 1e3 * 45e-6)
close("Helmholtz 45 uH terminal impedance at 1 kHz", 1 / value(ac, "mag(i(vsrc))"), coil_impedance)
close("OPA2197 unity gain at 10 MHz", value(ac, "mag(v(opout))"), 1 / math.sqrt(2), 0.02)

strap = run(
    f"""HMC1001 strap resistance
.include "{LIB / "HMC1001_functional.lib"}"
VBRIDGE vb 0 5.67
VOFFSET offsetp 0 2.5
VSET srp 0 1.5
XH srp offsetp 0 0 hp 0 vb hn field HMC1001_CTRL
VFIELD field 0 0
.control
op
print i(voffset) i(vset)
quit
.endc
.end
"""
)
close("HMC1001 offset strap resistance", -2.5 / value(strap, "i(voffset)"), 2.5)
close("HMC1001 set/reset strap resistance", -1.5 / value(strap, "i(vset)"), 1.5)
