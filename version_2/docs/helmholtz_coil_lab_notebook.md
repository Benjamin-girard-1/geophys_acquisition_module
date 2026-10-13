# Helmholtz coil lab notebook

Started 2026-10-13. This notebook records the coil construction and experiments
for HMC1001/HMC1002 sensitivity and minimum detectable AC field. Bench dates
for observations reported before this entry were not recorded. The wire-spool
photo stays outside the repository.

## How to use this notebook

- **Observed** means reported from the bench or read from the spool.
- **Specified** means taken from a manufacturer document.
- **Calculated** means derived from stated inputs, not measured.
- **Proposed** means a hypothesis, setting, or test awaiting bench evidence.
- Keep raw observations with units. Add dated corrections and subsequent
  results; do not silently turn a prediction into a result.

## Apparatus and open build details

| Item | Current record | Evidence/status |
| --- | --- | --- |
| Coil structure | Two Prusa Core One L printed forms; 290 mm nominal design diameter | User reported; design dimension, not a measured wire-center diameter |
| Winding | Alpha Wire AZ181930, red, 18 AWG stranded tinned copper | Spool marking; Entry 001 |
| Turn count and polarity | Five turns on each coil; additive wiring is intended | Turn count user reported; field polarity not yet checked |
| Coil separation | 145 mm is the target if the mean winding radius is 145 mm | Actual winding-center separation not yet recorded |
| Permanent ballast | Nominal 100 Ω in series | Installed; individual value, type, and power rating not recorded |
| Coil free leads | About 0.50 m each, or 1.0 m combined | User reported; separate from long feed cable |
| Feed cable | 0.246 Ω total loop DC resistance | User reported; cable length and routing not recorded |
| AC source | Agilent/Keysight 33500B | User reported; actual current versus frequency not yet measured |
| Magnetic readout | HMC1001/HMC1002, INA851 gain 51, AD7779 PGA up to 8 | User reported; transfer function not yet measured |

## Entry 001 — Wire identification

**Recorded:** 2026-10-13. **Status:** spool inspected and manufacturer data
reviewed. **Purpose:** identify conductor and insulation dimensions relevant
to winding geometry. No electrical test was performed in this entry.

### Observed on the spool

- Alpha Wire AZ181930, red, 100 ft; lot 156242-AW and DOM code 1218.
- Marked 1/C: 18 AWG (19/30) TC: one conductor, 19 strands of 30 AWG
  tinned copper. This is ordinary stranded wire, not individually insulated
  Litz wire.
- Marked .011 (.28MM) ETFE INS. and MIL-W-22759/16 150C 600V. These are
  markings on the spool; the DOM code and assembled-coil ratings are not
  inferred from them.

### Specified by Alpha Wire

Conductor diameter 0.048 in (1.22 mm); nominal ETFE insulation wall
0.0115 in (0.292 mm); finished diameter **0.071 ± 0.002 in
(1.80 ± 0.05 mm)**. The manufacturer catalog gives approximately
0.96 mm² conductor area for this 19/30 construction. Nominal DC resistance:
**5.9 Ω/1000 ft at 20 °C = 0.01936 Ω/m**. Listed range -55 to 150 °C
and 600 V RMS, under SAE-AS22759/16. These are wire specifications, not
measured properties or ratings of the complete assembly.

### Calculated implication

Ten ideal turns at 290 mm diameter contain 9.11 m of circular conductor
and would have about **0.176 Ω** nominal resistance before intercoil,
termination, and feed lengths. This replaces an earlier 0.19 Ω estimate
based on generic 18 AWG data. If 290 mm is the outside diameter of a form
and the first layer rests on it, that layer's conductor-center diameter is
about **291.8 mm**. Actual winding-center dimensions remain to be measured.

## Entry 002 — DC resistance survey

**Bench date:** not recorded. **Logged:** 2026-10-13. **Status:** measurements
reported by the user. **Question:** how much resistance do the winding and
feed cable add to the permanent ballast?

| Raw observation | Resistance |
| --- | ---: |
| Long feed cable, total loop | 0.246 Ω |
| Coil measured through short leads, without 100 Ω ballast | About 0.300 Ω |
| Short measurement leads | About 0.070 Ω |
| Complete coil, nominal 100 Ω ballast, and long feed cable | 100.35 Ω |

**Calculated:** 0.300 - 0.070 = about **0.230 Ω** for winding and associated
connections, about 0.054 Ω above Entry 001's ideal-turn estimate. Extra
wire length, contacts, and two-wire measurement uncertainty are unresolved.
If the ballast were exactly 100.000 Ω, separately measured values would
sum to **100.476 Ω**, 0.126 Ω above the reported complete-loop value. This
does not establish the ballast's individual resistance.

**Interpretation and limits:** The readings are consistent with a nominal
100 Ω ballast. They do not measure AC current or field. The generator is
assumed disconnected during the 100.35 Ω reading, so its internal 50 Ω is
excluded. Meter model, lead-zero method, contacts, and temperature were
not recorded.

**Next:** Record measurement conditions if the sub-ohm values are repeated.
Use a four-wire method if individual winding or cable resistances matter.
Calibrate field from measured current at each frequency.

## Entry 003 — As-built winding report

**Reported:** 2026-10-13. **Status:** user description of construction;
geometry has not been measured after winding.

- Each coil has **five turns**. The forms were printed on a Prusa Core One L.
  The reported 290 mm nominal diameter came from the printer's 300 mm
  maximum diameter allowance minus 10 mm for a wire-retaining ring. This is
  a design dimension; printer accuracy and the exact reference surface have
  not been checked with a diameter measurement.
- The first insulated wire lies against the printed form. Its conductor
  center is displaced from the contact surface by about half the specified
  1.80 mm finished wire diameter, approximately **0.90 mm**, if the contact
  surface itself is at 290 mm diameter.
- Later turns are semi-stacked diagonally. The user estimates that stacking
  may add roughly **1–2 mm of radius**; the reference point for that
  estimate has not been measured. Individual turn centers, the mean radius
  of each five-turn pack, axial positions, and the separation between coil
  winding centroids remain unmeasured.
- Each free lead from the coil assembly is approximately **0.50 m**, adding
  about **1.0 m total** of the AZ181930 wire beyond the circular turns.
  These leads are distinct from the long feed cable in Entry 002.

**Implication:** the form design may be precise, but the magnetic field
calculation needs the copper centerline geometry after winding. The
ideal-radius coefficient in H1 remains provisional.

**Resistance addendum, 2026-10-13:** At Alpha Wire's nominal 0.01936 Ω/m,
the two free leads add about **0.0194 Ω**. Ideal circular turns plus those
leads total about **10.11 m and 0.196 Ω**, before the intercoil link,
diagonal stacking, solder joints, and measurement error. Entry 002's
lead-corrected 0.230 Ω coil reading is about **0.034 Ω** higher than this
updated estimate. The measured 100.35 Ω complete-loop resistance already
includes the leads, so H2's drive-setting estimate does not change.

## Working hypotheses and calculations

These are predictions for comparison with later results, not results of a
coil field test.

### H1 — The series-aiding pair produces the predicted axial field

For two identical thin circular coils, with $N$ turns each, mean radius
$R$, center separation $s$, and the same series current $I$, the midpoint
field is

$$
B(0)=\frac{\mu_0 N I R^2}{\left(R^2+(s/2)^2\right)^{3/2}}.
$$

At ideal Helmholtz spacing $s=R=0.145$ m and the reported $N=5$ turns on
each coil, this predicts **31.006 µT/A**. If turns have different radii or
axial positions, sum their contributions rather than treating the form
diameter as the winding-center diameter. Winding-center dimensions,
axial positions, and pair separation have not yet been measured.

### H2 — The 33500B can drive the planned AC ranges

The generator has a physical 50 Ω series output impedance. Its High Z
load setting changes displayed voltage, not that impedance. At low
frequency, using the reported 100.35 Ω assembly DC resistance and an
external current-reference resistor $R_{\rm ref}$,

$$
I_{\rm pk}\approx\frac{V_{\rm HighZ,pp}}
 {2(50\ \Omega+100.35\ \Omega+R_{\rm ref})}.
$$

This prediction omits coil reactance and generator amplitude error. For
reported five-turn count and idealized 290 mm winding-center geometry:

| Reference resistor | Intended peak field | Peak current | Approximate High Z setting |
| --- | ---: | ---: | ---: |
| 10 Ω | 1 µT | 32.3 mA | 10.34 Vpp |
| 10 kΩ | 1 nT | 32.3 µA | 0.655 Vpp |

The accessible reference resistor goes near the generator return in series
with the complete coil assembly. At each frequency measure its actual
resistance and AC voltage, then calculate
$I_{\rm pk}=V_{R,\rm pk}/R_{\rm ref}$. The hidden 100 Ω ballast limits
current but is not an accessible current measurement point. At the
33500B's 20 Vpp open-circuit maximum, the idealized geometry reaches
about 1.93 µT peak with a 10 Ω reference. The installed ballast's power
rating and heating under insulation still need checking.

### H3 — Nominal sensor response predicts a measurable tone

The stated HMC sensitivity, 3.2 mV/V/gauss at 5.6 V bridge excitation,
predicts **17.92 mV/gauss = 179.2 µV/µT** at the bridge. INA851 gain 51
and AD7779 PGA gain 8 give nominal combined gain 408 and about **245,000
signed 24-bit codes/µT** using a 2.5 V reference. These are predictions,
not measured sensitivity. The balancing servo attenuates DC and slow
changes; other analog stages may change amplitude and phase versus
frequency. ADC counts per field measure the whole chain. Bridge-only
sensitivity needs independent electronic-gain or bridge-output measurement.

### H4 — Detectable field depends on frequency and observation time

Coil-off noise converted through the future measured response from
Entry 006 gives a field-equivalent amplitude spectral density. A detection
limit also
requires bandwidth or integration time and a detection criterion.
Ambient magnetic signals, generator leakage, cable pickup, and recording
disturbances may raise the practical limit. No calibrated detection
threshold has yet been measured with this coil.

## Planned tests — no results entered

### Entry 004 — Geometry and polarity

**Hypothesis:** measured conductor-center geometry and same-direction coil
currents support H1's field calculation.

1. Confirm five turns per coil and map the diagonal layering. Measure
   conductor-center diameters or each turn's radial and axial position,
   winding-centroid separation, and intended sensor-center position.
2. Sketch intercoil link, return, and feed routing. Check that each coil
   produces the same axial field direction for the same series current.
3. Record nearby magnetic materials and sensor-axis alignment.

**Results:** pending. **Next calculation:** replace H1 with the measured
winding geometry.

### Entry 005 — Current versus frequency

**Hypothesis:** generator setting predicts current approximately, but
measured reference-resistor voltage is required for calibration.

1. Insert a measured 10 Ω reference near generator return for microtesla
   tests and a measured 10 kΩ reference for nanotesla tests. Record probe
   loading and generator load setting.
2. Sweep planned frequencies and amplitudes. Record reference voltage
   amplitude and phase, current, source setting, and thermal drift.
3. Check for clipping and ballast heating at maximum planned drive.

**Results:** pending. **Next calculation:** replace H2's settings with
measured current-versus-setting data.

### Entry 006 — Sensitivity and frequency response

**Hypothesis:** response is approximately linear at proposed 0.5–1.5 µT
peak, while servo and filter response vary with frequency.

1. Record sensor identity and orientation, SET/RESET state, bridge voltage,
   INA851/PGA gains, ADC rate, and reference. Begin near 2 Hz; select
   further frequencies from the actual analog passband.
2. Fit sine amplitude and phase in measured current and ADC samples.
   Repeat amplitudes for linearity, clipping, and repeatability by axis.
3. Report complex ADC-counts-per-µT response and uncertainty. Characterize
   electronic gain independently before assigning bridge sensitivity.

**Results:** pending.

### Entry 007 — Noise floor and injection threshold

**Hypothesis:** field-equivalent noise and the smallest recoverable sine
depend on frequency and observation time; source-connected and
source-disconnected baselines may differ.

1. Record coil-off baselines with final wiring and ADC settings. Compare
   generator connected with output disabled against source disconnected.
   Record ambient and storage/radio activity; reject invalid ADC frames.
2. Convert measured output-noise spectrum through Entry 006's response.
   State bandwidth or integration time and signal-to-noise criterion.
3. Inject progressively smaller measured fields near the threshold with
   interleaved zero-field controls. Record detection success and uncertainty.

**Results:** pending.

## Template for the next entry

~~~text
### Entry 00X — Title
Bench date/time and location:
Status: planned / performed / analyzed
Question and hypothesis:
Apparatus, wiring, settings, and calibration references:
Procedure:
Raw observations and data-file IDs:
Calculations and uncertainty:
Result, interpretation, and limitations:
Next action:
~~~

## Sources

- [Alpha Wire AZ181930 product specification](https://www.alphawire.com/products/wire/hook-up-wire/premium/az181930)
- [Alpha Wire catalog, conductor area](https://www.alphawire.com/-/media/Project/AlphaWire/AlphaWire/2023-Master-Catalog/2023-MasterCat-English-20230609.pdf?rev=08a9584660404c8dad92412e1b961875)
- [NIST Helmholtz-coil geometry](https://nvlpubs.nist.gov/nistpubs/Legacy/SP/nbsspecialpublication300v3.pdf)
- [Honeywell HMC1001/HMC1002 data sheet](https://prod-edam.honeywell.com/content/dam/honeywell-edam/aero/en-us/products/navigation-and-sensors/sensors/low-field-high-precision-linear-1-and-2-axis-analog-magnetic-sensors/documents/hon-aero-1-and-2-axis-magnetic-sensors.pdf?download=false)
- [Keysight 33500 Series user guide](https://www.keysight.com/us/en/assets/9018-03290/user-manuals/9018-03290.pdf)
- [Keysight 33500B/33600A specifications](https://www.keysight.com/in/en/assets/7018-05928/data-sheets/5992-2572.pdf)
