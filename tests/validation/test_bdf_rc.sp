* RC Circuit for BDF Validation
.title RC Circuit BDF Test

* 5V step input
V1 in 0 DC 5

* RC network (τ = R*C = 1k * 10μ = 10ms)
R1 in out 1k
C1 out 0 10u

* Transient analysis: 0 to 50ms, 0.5ms steps
.tran 0.5m 50m

.end
