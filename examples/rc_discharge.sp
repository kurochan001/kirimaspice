* RC discharge from 1 V (R = 1 kOhm, C = 1 uF, RC = 1 ms)
* V(out) = exp(-t/RC); at t = 1 ms the exact value is 1/e = 0.367879.
* UIC starts the transient from .IC. Change METHOD to gear or euler to compare
* the integration methods.
R1 out 0 1k
C1 out 0 1u
.IC V(out)=1
.OPTIONS METHOD=trap
.tran 10u 1m UIC
.end
