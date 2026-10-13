# Helmholtz coil calibration plan

This is a bench plan for measuring the magnetic acquisition chain's AC
sensitivity and its minimum detectable field versus frequency. Dimensions,
resistance, gain, and field values below are design estimates until measured.

## Coil construction and field

- Wind **five turns per coil** from one continuous insulated wire. Use the same
  winding sense for both coils when viewed from the **same end of their common
  axis**. The current must circulate in the same direction in both coils.
- Connect the coils in series by carrying the wire from the first winding to
  the second. The two remaining wire ends go to the drive circuit.
- Use a 290 mm **mean turn diameter** and 145 mm **center-to-center spacing**
  as the initial Helmholtz geometry. Measure and record the actual mean winding
  radius, coil separation, number of turns, and sensor position after winding.
- Keep the intercoil lead near the outer frame. Route the return from the
  second coil alongside that lead so their opposing currents cancel much of
  their stray field. Keep the two leads to the generator together or twisted.
- If the five-turn pair uses approximately 9.11 m of AWG 24 copper
  (0.205 mm²), its estimated DC resistance is 0.77 Ω at 20 °C. Measure the
  finished winding rather than using that estimate for calibration.

For the ideal pair at its center,

$$
B_{\rm pk} = \left(\frac45\right)^{3/2}\frac{\mu_0 N I_{\rm pk}}{R}
           \simeq (6.20\ {\rm \mu T/A})\,N I_{\rm pk},
$$

where $N$ is the number of turns **on each coil**, $R=0.145$ m, and
$I_{\rm pk}$ is peak current through the series pair. With five turns,
$B_{\rm pk}\simeq31.0\ {\rm \mu T/A}\,I_{\rm pk}$. This is the axial field
of the ideal geometry. Geometry error, winding placement, lead fields, and
magnetic material nearby must be considered for an absolute calibration.

## 33500B drive and current reference

```text
33500B output ── coil 1 ── coil 2 ── Rseries ── 33500B return
                                   │       │
                                   └─ measure VR ─┘
```

Use a measured, non-inductive precision resistor as both the ballast and
current reference. Put it near the generator, away from the sensor. A 50 Ω
resistor rated at least 1 W is suitable for the microtesla range; a 10 kΩ
resistor is suitable for nanotesla-level injections. Determine current from
the measured **sine amplitude** across this resistor:
$I_{\rm pk}=V_{R,\rm pk}/R_{\rm series}$. Use a sufficiently high-impedance
voltage probe, especially on the 10 kΩ range. Record the reference voltage at
every test frequency.

The generator's output has a **physical 50 Ω series impedance**. Its `High Z`
load menu setting changes the displayed voltage but does not remove that
impedance. The following `High Z` settings are approximate starting points,
assuming 0.77 Ω coil resistance and the ideal field coefficient; use measured
resistor voltage to assign the actual field.

| External resistor | Peak field | Peak current | Approximate generator setting |
| --- | ---: | ---: | ---: |
| 50 Ω | 1 µT | 32.3 mA | 6.50 Vpp |
| 50 Ω | 2 µT | 64.5 mA | 13.0 Vpp |
| 50 Ω | 3 µT | 96.8 mA | 19.5 Vpp |
| 10 kΩ | 1 nT | 32.3 µA | 0.648 Vpp |
| 10 kΩ | 0.1 nT | 3.23 µA | 64.8 mVpp |

The specified 33500B maximum is 20 Vpp open circuit; consequently five turns
and the external 50 Ω resistor reach approximately 3.1 µT peak. More field
requires more turns or a separately characterized current driver. Source
amplitude accuracy and coil impedance may vary with frequency, so the front
panel amplitude alone is not a calibration standard.

## Measurement sequence

1. Check winding polarity with a small current: each coil separately must
   produce a field pointing in the same direction at the pair's center. Verify
   the sensor is centered and aligned to the intended coil axis. Record the
   magnetic-card SET/RESET state.
2. Record coil-off data with the final cable arrangement and acquisition
   settings. Note sample rate, AD7779 PGA gain, bridge excitation, analog
   configuration, and environmental conditions. The balancing servo removes
   DC and very slow field components, so use an AC tone for calibration.
3. Start near 2 Hz with a 1–2 µT peak sine. At each frequency, measure the
   resistor's amplitude and phase and fit the corresponding ADC channel to the
   applied sine. The ratio of ADC response to measured coil field is the
   **complete sensor-to-ADC transfer function** at that frequency.
4. Repeat with several field amplitudes to check linearity and clipping.
   Repeat for each sensor axis, orienting the coil axis appropriately. Characterize
   the electronic gain independently or measure the bridge output if the goal
   is the **sensor's own** sensitivity rather than the complete chain's.
5. With excitation off, estimate the ADC noise spectrum using recordings free
   of known invalid frames and storage/radio disturbances. Divide its
   amplitude spectral density by the measured response magnitude at each
   frequency to get field-equivalent noise in nT/√Hz. State the detection
   bandwidth or integration time and the chosen signal-to-noise criterion.
   Confirm estimated limits by injecting progressively smaller sine fields,
   including coil-off control runs.

At 5.6 V bridge excitation, the nominal HMC sensitivity of
3.2 mV/V/gauss gives **17.92 mV/gauss**, or 179.2 µV/µT at the bridge.
With INA851 gain 51 and AD7779 gain 8, the nominal slope after both gain
stages is 73.1 mV/µT, or about 245,000 signed
24-bit codes/µT with a 2.5 V reference. These are predictions, not measured
sensor or system calibration factors. The developer plotter currently uses
this nominal scale; see [its notes](../software/apps/dev_tools/PLOTTER_UPDATE_TEMP.md).

## References

- [NIST, Helmholtz-coil field geometry](https://nvlpubs.nist.gov/nistpubs/Legacy/SP/nbsspecialpublication300v3.pdf)
- [NIST, copper resistivity at 20 °C](https://nvlpubs.nist.gov/nistpubs/Legacy/circ/nbscircular31e2.pdf)
- [Honeywell HMC1001/HMC1002 data sheet](https://prod-edam.honeywell.com/content/dam/honeywell-edam/aero/en-us/products/navigation-and-sensors/sensors/low-field-high-precision-linear-1-and-2-axis-analog-magnetic-sensors/documents/hon-aero-1-and-2-axis-magnetic-sensors.pdf?download=false)
- [Keysight 33500 Series user guide](https://www.keysight.com/us/en/assets/9018-03290/user-manuals/9018-03290.pdf)
- [Keysight 33500B/33600A specifications](https://www.keysight.com/in/en/assets/7018-05928/data-sheets/5992-2572.pdf)
