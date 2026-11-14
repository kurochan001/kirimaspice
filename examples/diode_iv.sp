* Diode I-V curve with a DC sweep
* With IS = 1e-14 A and N = 1 at 27 C, I = IS * (exp(V / Vt) - 1), Vt = kT/q = 25.865 mV.
* The current of V1 is positive into its + terminal, so I(V1) = -I(D1).
V1 a 0 DC 0
D1 a 0 DMOD
.MODEL DMOD D(IS=1e-14 N=1)
.DC V1 0 0.8 0.05
.end
