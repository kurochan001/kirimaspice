* RC low-pass filter (R = 1 kOhm, C = 1 uF), AC analysis
* H(f) = 1 / (1 + j 2 pi f R C); at 1 kHz: -16.072 dB, -80.96 degrees.
V1 in 0 DC 0 AC 1
R1 in out 1k
C1 out 0 1u
.ac dec 10 10 100k
.end
