# Helmholtz coil calibration plan

This is a bench plan for measuring the magnetic acquisition chain's AC
sensitivity and its minimum detectable field versus frequency. The electrical
readings below are reported bench measurements; geometry, gain, and field
values remain design estimates until measured.

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
- The selected wire is **Alpha Wire AZ181930**, a single-conductor red hookup
  wire. The spool is marked `18 AWG (19/30) TC`, identifying 19 strands of
  30 AWG tinned copper, and `.011 (.28MM) ETFE INS.`. Its printed rating is
  `MIL-W-22759/16 150C 600V`; Alpha Wire lists the applicable specification
  as SAE-AS22759/16. The spool is marked `100 FT`, lot `156242-AW`, and
  `DOM: 1218`. These markings are transcribed from the spool; the image is
  not stored in this repository.
- Alpha Wire specifies a nominal **0.048 in (1.22 mm) conductor diameter**,
  **0.0115 in (0.292 mm) ETFE wall**, and **0.071 ± 0.002 in
  (1.80 ± 0.05 mm) finished diameter**. Its catalog lists about **0.96 mm²**
  of conductor cross-section, reflecting the actual 19/30 construction.
  The product's nominal DC resistance is **5.9 Ω/1000 ft at 20 °C**,
  or **0.01936 Ω/m**. The temperature range is -55 to 150 °C and the voltage
  rating is 600 V RMS. These are wire specifications, not ratings of the
  assembled coil or its ballast resistor.
- Ten ideal 290 mm-diameter turns use approximately 9.11 m of wire and would
  have about **0.176 Ω** nominal DC resistance from this manufacturer's wire
  data, excluding the intercoil link, terminations, and feed leads. The
  previously used generic 18 AWG area and 0.19 Ω estimate have been replaced
  by the part-specific data. Measure the finished winding rather than using
  the ideal-turn estimate for calibration.
- Keep the winding arrangement the same on both frames. Measure the **mean
  conductor centerline diameter**, not merely the form diameter. If 290 mm
  is the outside diameter of a form with a single layer of this wire on it,
  the first-layer conductor centerline diameter is approximately 291.8 mm.
  Radial stacking or axial turn spacing changes the field geometry; measure
  the actual winding centroids and their separation.

### Reported DC resistance readings

| Measurement | Reading |
| --- | ---: |
| Long feed cable, total loop resistance | 0.246 Ω |
| Coil measured with short leads | About 0.300 Ω |
| Short measurement leads alone | About 0.070 Ω |
| Complete coil, permanent 100 Ω ballast, and long feed cable | 100.35 Ω |

Subtracting the short-lead reading suggests about **0.230 Ω** for the
winding and its connections, roughly 0.054 Ω above the 0.176 Ω nominal
ideal-turn estimate. The difference may include the intercoil link, other
wire length, contacts, and measurement uncertainty. If the ballast were
exactly 100.000 Ω, the separately measured parts would sum to **100.476 Ω**,
0.126 Ω above the
reported complete-loop reading. That small discrepancy can arise from
ballast tolerance, contact resistance, lead compensation, or meter accuracy;
it does not establish the ballast's individual resistance. Confirm the
measurement setup and zero the meter leads if individual low-ohm values are
needed. The 100.35 Ω complete-loop reading is assumed to have been made with
the generator disconnected, so it excludes the generator's internal 50 Ω
output resistance.

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
33500B output ── coil 1 ── coil 2 ── fixed 100 Ω ── Rreference ── 33500B return
                                       on frame       near generator
```

Solder a **100 Ω, at least 2 W** non-inductive resistor in series with the
coil as a permanent ballast. Place and insulate it at the frame's outer edge,
away from the sensor. It dissipates at most about 0.22 W with the specified
33500B voltage range; the larger rating gives margin when covered by
heat-shrink. Use strain relief at the frame so the resistor leads do not carry
cable tension. Record its measured resistance before covering it.

A buried two-terminal ballast is **not an accessible current reference**.
For calibration, put a measured precision resistor in series at the generator
return: approximately 10 Ω for microtesla tests and 10 kΩ for nanotesla
tests. Measure the sine voltage across that external resistor and calculate
$I_{\rm pk}=V_{R,\rm pk}/R_{\rm reference}$. Use a sufficiently high-impedance
voltage probe, especially on the 10 kΩ range. Record the reference voltage at
every test frequency. An alternative is to bring out high-impedance Kelvin
sense leads from across the fixed 100 Ω resistor before insulating it.

The generator's output has a **physical 50 Ω series impedance**. Its `High Z`
load menu setting changes the displayed voltage but does not remove that
impedance. The following `High Z` settings are approximate starting points,
using the reported 100.35 Ω complete-loop DC resistance and the ideal field
coefficient; use measured current to assign the actual field. The listed
external resistor is both added series resistance and the accessible current
reference.

| External reference resistor | Peak field | Peak current | Approximate generator setting |
| --- | ---: | ---: | ---: |
| 10 Ω | 0.5 µT | 16.1 mA | 5.17 Vpp |
| 10 Ω | 1 µT | 32.3 mA | 10.34 Vpp |
| 10 Ω | 1.5 µT | 48.4 mA | 15.51 Vpp |
| 10 kΩ | 1 nT | 32.3 µA | 0.655 Vpp |
| 10 kΩ | 0.1 nT | 3.23 µA | 65.5 mVpp |

The specified 33500B maximum is 20 Vpp open circuit; consequently five turns
with the permanent 100 Ω ballast and external 10 Ω reference reach
approximately 1.93 µT peak. Without the external reference, the limit is
approximately 2.06 µT peak. A permanent 1 kΩ ballast would instead limit the
coil to approximately 0.30 µT peak. More field requires more turns or a
separately characterized current driver. Source
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
3. Start near 2 Hz with a 0.5–1.5 µT peak sine. At each frequency, measure the
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
- [Alpha Wire AZ181930 product specification](https://www.alphawire.com/products/wire/hook-up-wire/premium/az181930)
- [Alpha Wire catalog, nominal conductor area](https://www.alphawire.com/-/media/Project/AlphaWire/AlphaWire/2023-Master-Catalog/2023-MasterCat-English-20230609.pdf?rev=08a9584660404c8dad92412e1b961875)
- [Honeywell HMC1001/HMC1002 data sheet](https://prod-edam.honeywell.com/content/dam/honeywell-edam/aero/en-us/products/navigation-and-sensors/sensors/low-field-high-precision-linear-1-and-2-axis-analog-magnetic-sensors/documents/hon-aero-1-and-2-axis-magnetic-sensors.pdf?download=false)
- [Keysight 33500 Series user guide](https://www.keysight.com/us/en/assets/9018-03290/user-manuals/9018-03290.pdf)
- [Keysight 33500B/33600A specifications](https://www.keysight.com/in/en/assets/7018-05928/data-sheets/5992-2572.pdf)
