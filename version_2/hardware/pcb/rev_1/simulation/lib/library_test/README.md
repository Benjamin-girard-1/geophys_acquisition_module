# SPICE models used by the magnetic board

Run `python3 test_models.py` here to check the nominal model parameters with
ngspice. KiCad 10 loads the four `.lib` files from the parent `lib/` directory.
The `lib_old/` directory contains the previous versions.

| Model | Verified nominal behavior | Scope |
| --- | --- | --- |
| HMC1001 | 850 Ω bridge, 3.2 mV/V/gauss sensitivity, 1.5 Ω set/reset strap, 2.5 Ω offset strap | FIELD input: 1 V = 1 µT. Strap magnetic coupling and magnetic saturation are omitted. |
| Helmholtz pair | 0.23 Ω terminal resistance; midpoint field 30.782 µT/A for N=5 per coil, D=295.2 mm, S=145 mm | 45 µH terminal inductance is an estimate, not a measured or datasheet value. |
| INA851 | Differential gain `1 + 6000/RG = 51` with RG=120 Ω; 10 µV input offset | Fixed 15 MHz pole corresponds to the G=1 datasheet figure. Gain-dependent high-frequency response is omitted. |
| OPA2197 | 134 dB open-loop gain, 10 MHz gain-bandwidth, 5 pA input bias, 1 mA quiescent current per channel | `OPA1197_FUNC` models one channel. Linear AC/DC model; output clipping, slew rate, current limit, and noise are omitted. |

The OPA model is intentionally linear so ngspice finds the true DC equilibrium
of the bridge-balancing loop before computing the AC response. It is suitable
for the board's nominal low-frequency transfer-function reference; it should
not be used to predict startup or overload behavior. Output offset `VOS`
defaults to zero because its sign is part-dependent. Set it explicitly for
offset studies.

Source datasheets:

- [HMC1001/HMC1002](https://prod-edam.honeywell.com/content/dam/honeywell-edam/aero/en-us/products/navigation-and-sensors/sensors/low-field-high-precision-linear-1-and-2-axis-analog-magnetic-sensors/documents/hon-aero-n61-2056-000-000-magnetic-sensors-hmc-ds-en.pdf?download=false)
- [INA851](https://www.ti.com/lit/ds/symlink/ina851.pdf)
- [OPAx197, including OPA2197](https://www.ti.com/lit/ds/symlink/opa2197.pdf)
