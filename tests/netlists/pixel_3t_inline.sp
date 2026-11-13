* Single 3T CMOS Pixel Test - Inline (no subcircuit)
* Verification test without subcircuit

.title Single CMOS Pixel Test (Inline)

* Power supply
VDD vdd 0 DC 3.3

* MOSFET model (simple Level 1)
.model NMOS NMOS LEVEL=1 VTO=0.7 KP=120u LAMBDA=0.01

* Photodiode voltage (simulating captured light level)
Vpd pd 0 DC 2.0

* Row selection control
Vrowsel rowsel 0 DC 3.3

* Column load resistor
Rcol col 0 10k

* Source follower NMOS: drain=VDD, gate=pd, source=vout_sf, bulk=GND
Msf vdd pd vout_sf 0 NMOS W=2u L=0.5u

* Row select transistor: drain=vout_sf, gate=rowsel, source=col, bulk=GND
Msel vout_sf rowsel col 0 NMOS W=1u L=0.5u

* Source follower load current source
Isf vout_sf 0 DC 10u

* DC operating point analysis
.op

* Print results
.print dc v(vdd) v(pd) v(rowsel) v(col) v(vout_sf)

* Options
.option abstol=1e-12
.option reltol=1e-4
.option vntol=1e-6

.end
