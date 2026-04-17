* --- High Speed CML Latch ---
* Track Stage (Diff Pair)
M1 out out_b clk tail gnd nmos W=5u L=40n
M2 out_b out clk tail gnd nmos W=5u L=40n

* Latch Stage (Cross Coupled Pair)
* Activated when clk_b is high
M3 out out_b tail_latch gnd nmos W=5u L=40n
M4 out_b out tail_latch gnd nmos W=5u L=40n

* Tail Current Sources
M5 tail bias gnd gnd nmos W=10u L=100n
M6 tail_latch bias gnd gnd nmos W=10u L=100n

* Load Resistors (Active PMOS Linear Region loads)
M7 out gnd vdd vdd pmos W=2u L=40n
M8 out_b gnd vdd vdd pmos W=2u L=40n
