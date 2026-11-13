* BSIM4 QA DC deck (simple parameters)
.param VGS_VALUE=@VGS@
VDS drain 0 DC 0
VGS gate 0 DC {VGS_VALUE}
.model QA_NMOS nmos level=14 vth0=0.45 k1=0.53 k2=0.02 u0=0.045 vsat=8.5e4 pclm=1.3 toxe=1.25e-9
M1 drain gate 0 0 QA_NMOS W=10e-6 L=45e-9

.control
set filetype=ascii
set wr_singlescale
set wr_vecnames
dc VDS 0 1.0 0.25
print v(drain) i(vds)
write @RAWFILE@
.endc

.end
