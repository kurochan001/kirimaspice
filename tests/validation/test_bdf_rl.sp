* RL Circuit for BDF Validation
.title RL Circuit BDF Test

* 10V step input
V1 in 0 DC 10

* RL network (τ = L/R = 10m / 100 = 100μs)
R1 in out 100
L1 out 0 10m

* Transient analysis: 0 to 500μs, 10μs steps
.tran 10u 500u

.end
