* Parameters, a function and a subcircuit
* VH = HALF(VIN) = 2 V. The top divider gives V(out) = 2 * 2k / (1k + 2k) = 1.33333 V.
* X1 divides V(in) by (3k + 1k) / 1k, so V(xout) = 2 * 1k / 4k = 0.5 V.
.PARAM RLOAD=2k VIN=4
.FUNC HALF(x) {x/2}
.PARAM VH={HALF(VIN)}
.SUBCKT DIV a b RTOP=1k
R1 a b {RTOP}
R2 b 0 1k
.ENDS DIV
V1 in 0 DC {VH}
R1 in out 1k
R2 out 0 {RLOAD}
X1 in xout DIV RTOP=3k
.op
.end
