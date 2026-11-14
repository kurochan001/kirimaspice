* RC step response (R = 1 kOhm, C = 1 uF, RC = 1 ms)
* V(out) = 1 - exp(-t/RC); at t = 1 ms the exact value is 1 - 1/e = 0.632121.
V1 in 0 DC 0 PULSE(0 1 0 1n 1n 1 2)
R1 in out 1k
C1 out 0 1u
.tran 10u 5m
.end
