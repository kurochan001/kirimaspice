* Single 3T CMOS Pixel Test - DC Analysis
* Simplified test to verify source follower operation

.title Single CMOS Pixel Test

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

* 3T Pixel subcircuit definition
.subckt pixel_3t_dc vdd rowsel vout pd_voltage
* Source follower (amplifier)
* NMOS source follower: drain=VDD, gate=pd_voltage, source=vout_sf, bulk=GND
Msf vdd pd_voltage vout_sf 0 NMOS W=2u L=0.5u

* Row select transistor: drain=vout_sf, gate=rowsel, source=vout, bulk=GND
Msel vout_sf rowsel vout 0 NMOS W=1u L=0.5u

* Source follower load current source
Isf vout_sf 0 DC 10u
.ends pixel_3t_dc

* Options (must be before analysis commands)
.option abstol=1e-12
.option reltol=1e-4
.option vntol=1e-6
.option itl1=500

* Pixel instantiation
Xp1 vdd rowsel col pd pixel_3t_dc

* DC operating point analysis
.op

* Print results
.print dc v(vdd) v(pd) v(rowsel) v(col)

.end
