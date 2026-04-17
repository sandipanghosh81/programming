* --- CMOS Beta Multiplier Reference (Constant Gm) ---
* Self-biased current source

* PMOS Current Mirror (Top)
M3 n1 n1 vdd vdd pmos W=20u L=2u
M4 n2 n1 vdd vdd pmos W=20u L=2u

* NMOS Loop (Bottom)
* M2 is a "Weak" device (Multiplied L or smaller W)
* R1 is usually in series with M2 source, but for topology check:
* We simulate the resistor as a separate net 'vx'
M1 n1 n2 gnd gnd nmos W=10u L=1u
M2 n2 n2 vx gnd nmos W=40u L=1u 

* Start-up Circuit (Inverter-like)
M_start1 gate_start n1 vdd vdd pmos W=1u L=5u
M_start2 gate_start gate_start gnd gnd nmos W=1u L=5u
M_inject n1 gate_start gnd gnd nmos W=1u L=1u
