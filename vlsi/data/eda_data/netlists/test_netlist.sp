* 1. Differential Pair
M1 d1 in1 tail gnd nmos W=1u L=100n
M2 d2 in2 tail gnd nmos W=1u L=100n

* 2. Current Mirror (P-Type)
M3 d1 d1 vdd vdd pmos W=2u L=100n
M4 d2 d1 vdd vdd pmos W=2u L=100n

* 3. Cascode (N-Type)
M5 tail bias1 casc_mid gnd nmos W=5u L=100n
M6 casc_mid bias2 gnd gnd nmos W=5u L=100n

* 4. CMOS Inverter
M7 out in vdd vdd pmos W=2u L=50n
M8 out in gnd gnd nmos W=1u L=50n

* 5. Transmission Gate
M9 nodeA nodeB clk gnd nmos W=0.5u L=50n
M10 nodeA nodeB clk_b vdd pmos W=1u L=50n

* 6. Cross-Coupled Pair (SRAM Core)
M11 q q_b gnd gnd nmos W=1u L=50n
M12 q_b q gnd gnd nmos W=1u L=50n
