# Helmholtz coil lab notebook

Started 2026-10-13. This notebook records the coil construction and experiments
for HMC1001/HMC1002 sensitivity and minimum detectable AC field. 


## Apparatus and open build details
(2026-10-13)

| Item | Current record |
| --- | --- |
| Coil structure | Two Prusa Core One L printed forms; 290 mm nominal design diameter | 
| Winding | Alpha Wire AZ181930, red, 18 AWG stranded tinned copper |
| Turn count and polarity | Five turns on each coil |
| Coil separation | 145mm by design (should have taken the radius change cause by the winding) |
| Permanent ballast |  100.0 0.1% Ω in series |
| Coil free leads | About 0.50 m each, or 1.0 m combined |
| Feed cable | 0.246 Ω total loop DC resistance |
| AC source | Agilent/Keysight 33500B |
| Magnetic readout | HMC1001/HMC1002, INA851 gain 51, AD7779 PGA up to 8 |


## Before the experiments

### 1) Wire

- Alpha Wire AZ181930, red, 100 ft; lot 156242-AW and DOM code 1218.
- Marked 1/C: 18 AWG (19/30) TC
- Conductor diameter 0.048 in (1.22 mm); nominal ETFE insulation wall
0.292 mm; finished diameter **(1.80 ± 0.05 mm)**. 
0.96 mm² conductor area for this 19/30 construction. Nominal DC resistance:
**5.9 Ω/1000 ft at 20 °C = 0.01936 Ω/m**. Listed range -55 to 150 °C
and 600 V RMS.


### 2) Diameter printed and diameter added by wire stacking

The **290 mm printed diameter** is treated here as the nominal diameter at
the narrow bottom of the winding groove. 

![Section of the printed V groove, including its 0.4 mm throat and 5 mm reference dimensions](pictures/slice_coil.png)

The next picture illustrates the **one–two–two stacking pattern**: one turn
deep in the V, two beside it, and two farther out. It is a placement sketch,
**not a dimensioned drawing of the wire**; the calculation uses the specified
**1.80 mm insulated diameter**.

![Illustration of the five wire positions in the V groove](pictures/slice_with_5wires.png)

#### Estimation of the real diameter
For an ideal tight stack of round, uncompressed wires touching their
neighbors and the approximately 45° groove walls, let $d=1.80$ mm be the
insulated wire diameter and $w=0.40$ mm the throat width. The first
conductor center is $x_0=d/\sqrt{2}-w/2=1.07$ mm radially beyond the groove
bottom. Each next layer adds $d/\sqrt{2}=1.27$ mm of radius.

| Position in stack | Turns per coil | Conductor-center radial offset from groove bottom | Conductor-center diameter if groove bottom is 290 mm |
| --- | ---: | ---: | ---: |
| Deepest | 1 | 1.07 mm | 292.15 mm |
| Middle | 2 | 2.35 mm | 294.69 mm |
| Outermost | 2 | 3.62 mm | 297.24 mm |

The arithmetic mean conductor-center diameter of the five turns is
295.2mm. 
This value is an estimations, the wire stack is sligly wrong at the start and 
end of the coils adding some uncertainty. Considering that the uncertainty is estimated at **295.2+-1mm** or a radius of **147.6+-0.5mm**.


### 3) Resistance estimation

the coils are two 5 turns with an average diameter of 295.2mm and a spacing of 145mm x2 
for the back and forth and there is 50cm of leads for each ends adding 1m. The total
wire lenght is estimated at 10.55m+-0.05m. 2cm of uncertainty is added to consider error on the lenght of the leads and stristed wires.

With the resistance given by the wire manifacturer (0.01936 Ω/m), the resistance should be 0.204+-0.001 Ω.


### 5) Resistance survey

| Raw observation | Resistance |
| --- | ---: |
| Long feed cable, total loop | 0.246 +- 0.001 Ω |
| Coil measured through short leads, without 100 Ω ballast | 0.300 +- 0.005 Ω |
| Short measurement leads | 0.070 +-0.005 Ω |
| 100 Ω +-0.01% | 100.01 +-0.03 Ω |
| Complete coil, nominal 100 Ω ballast, and long feed cable | **100.35 +-0.01 Ω** |

Windings: 0.300 - 0.070 = **0.23+-0.01Ω**, this is 0.03 Ω above the estimation, this
is very acceptable considering some possible contact resistance.

**100.35 +-0.01 Ω** will be used for the current calculations.


### 6) Predicted field of the assembled coil

Each coil has five turns. Using the estimated mean conductor-center diameter
\(D=295.2\pm1.0\ \mathrm{mm}\), the mean radius is
\(R=147.6\pm0.5\ \mathrm{mm}\). The coil spacing is
\(s=145.0\pm0.5\ \mathrm{mm}\). Because the built spacing differs from the
mean radius, the midpoint field is calculated with the general coil-pair
equation:

$$
B(0)=\frac{\mu_0 N I R^2}
{\left[R^2+(s/2)^2\right]^{3/2}}.
$$

With \(N=5\) turns per coil and the nominal dimensions above,

$$
\frac{B(0)}{I}\approx 30.78\ \mathrm{\mu T/A}.
$$

Thus, the predicted midpoint field is
\(B(0)\approx(30.78\ \mathrm{\mu T/A})I\), with \(I\) in amperes.

The stated diameter and spacing tolerances give a worst-case range of
**30.68–30.89 µT/A**, or approximately **30.78 ± 0.11 µT/A** (±0.34%).


### 7) Wavegenerator 33500B and oscilloscope

The generator has a 50Ω series output impedance. We can input the load resistance 
value so the wavegenerator display accurate voltage when compared to the oscilloscope.
100Ω is set, it is noted that at low voltage the voltage (100mVpp) from the wavegen, the 
oscilooscope measure diverge. But at higher voltage they are coherent.

The wavegen has this accuracy:
at 1kHz +-1% of Vpp + 1mVpp

But the oscilloscope has been wired to the outputs of the wavegen se we can have 
the exact data of the signal and voltage sent to the coils. For each measurement the 
oscilloscpe data will be saved so it shows few periods.


### 8) Nominal sensor response predicts a measurable tone

The stated HMC sensitivity, 3.2 mV/V/gauss at 5.6 V bridge excitation,
predicts **17.92 mV/gauss = 179.2 µV/µT** at the bridge. INA851 gain 51
and AD7779 PGA gain 8 give nominal combined gain 408 and about **245,000
signed 24-bit codes/µT** using a 2.5 V reference. These are predictions,
not measured sensitivity. The balancing servo attenuates DC and slow
changes; other analog stages may change amplitude and phase versus
frequency. ADC counts per field measure the whole chain.


## The experiments

### Objectives

There are two main objectives, first objective is to determine the transfer 
function of the whole system. To do so a measurement of the of the system and 
environement noise must be done. 
Then a series of measurement can be done while injecting particular frequencies.
By comparing the injected magnetic field to the measured field and its background 
noise we will be able to determine the transfer function of the system.
This will be done for each axis (3x).

The second objective is to determine the lowest field we can sample and few frequencies.


### 1) Noise floor 
(2026-10-14)

All electronic devices of the wall with the windows of the lab PLT-4900 have been
unplugged to avoid as much noise as possible. 

The coils have been taped on a cartoon box with a slide, this is to accomodate one 
of the axis. The box have been taped on the table, so the whole system doest move much 
between different test. This methode is not great but the few milimeters of movements 
shouldnt cause much issues, since eath magnetic field is cancelled by the servo. This 
taping help only to keep the noise from electronis machinery around to stay in the same 
orientation.

The wavegen is connected and powered but no output are enabled. The oscilloscope is 
also connected but turned off. The oscilloscope is not connected to its PSU. 
The wavegen cable to the coils is a coax.

A 1h sampling have been started.


### 2) Sinusses in the coils

Since there are a lot of test to do for each axis, the recordings are 5m each. 
This gives enough times for the servo to settle and to avoid having to filter 
the SD card recording event (lots of noise every 300s).

Considering the theoretical field of the coils is given by this equation:
\(B(0)=(30.78\ \mathrm{\mu T/A})I\), with \(I\) in amperes.

and that the current is given by 
\(I_{pp}=\frac{V_{pp}}{100.35}\)

It gives 
\(B(0)= (0.3067 \ \mathrm{\mu T/V}) * V_{pp}\)

So a voltage of \(5.00 V_{pp}\) should give
\(B(0)=1.5336 \ \mathrm{\mu T_{pp}}\) peak to peak

The current is 49.83mA peak to peak, the wavegen should be able to supply this current.

#### The transfer function experiment

Those values of frequencies were tested one by one for 5 minutes or more. 
Only the seconds from 50 to 300 will be taken as usable since at second 
300, there is the recording on the SD card it is causing a spike in noise.

0.2, 0.5, 1, 2, 3, 4, 5, 8, 10, 20, 50, 100, 200, 300, 400 Hz

The 0.1Hz were tryed but the wavegenerator voltage output gets much lower 
at very low frequency making it very difficult to have precise control. 
No recordin of this frequency were taken.

Two recordings of 1Hz were taken by mistake.

An acquisition of each signal coming of the wavegenerator were took to assure 
that the voltage of the signal is precisely know, reducing the uncertainty of 
the wavegenerator. 

It is noted that the voltage is consistently lower on the oscilloscope than 
what is said by the wavegenerator, even when set to an output of 100 ohms.

This first series for the z_axis was completed on 2026-10-14.
