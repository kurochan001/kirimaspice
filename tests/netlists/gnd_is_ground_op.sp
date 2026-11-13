* gnd in any case is ground (NETLIST_CONVENTIONS rule 5): a 1k/3k divider to GND puts mid at 0.75 V
V1 a 0 DC 1
R1 a mid 1k
R2 mid GND 3k
.op
.end
