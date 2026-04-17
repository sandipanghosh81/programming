* --- Two-Stage Miller OpAmp ---
* VDD: vdd, GND: gnd
* Bias Current Reference
M5 bias_n bias_n gnd gnd nmos W=10u L=1u
M6 bias_p bias_p vdd vdd pmos W=20u L=1u

* Input Differential Pair (NMOS)
M1 d1 inp comm_src gnd nmos W=50u L=1u
M2 d2 inm comm_src gnd nmos W=50u L=1u

* Active Load (Current Mirror PMOS)
M3 d1 d1 vdd vdd pmos W=20u L=1u
M4 d2 d1 vdd vdd pmos W=20u L=1u

* Tail Current Source (Mirrored from M5)
M7 comm_src bias_n gnd gnd nmos W=20u L=1u

* Second Stage (Common Source Amp)
M8 out d2 vdd vdd pmos W=100u L=1u
M9 out bias_n gnd gnd nmos W=50u L=1u
