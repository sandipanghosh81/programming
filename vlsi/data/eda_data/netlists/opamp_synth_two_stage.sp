* Synthesized analog netlist (open, generated for testing)
* Two-stage opamp core with explicit m/nf
.subckt opamp_synth_two_stage VINP VINN VDD VSS VOUT IBIAS
* Input differential pair
XM1 N1 VINP NSRC VSS sky130_fd_pr__nfet_01v8 L=0.5 W=1.0 m=4
XM2 N2 VINN NSRC VSS sky130_fd_pr__nfet_01v8 L=0.5 W=1.0 m=4
XM3 NSRC IBIAS VSS VSS sky130_fd_pr__nfet_01v8 L=1.0 W=2.0 m=4
* PMOS load current mirror
XM4 N1 N2 VDD VDD sky130_fd_pr__pfet_01v8 L=0.6 W=2.0 nf=4
XM5 N2 N2 VDD VDD sky130_fd_pr__pfet_01v8 L=0.6 W=2.0 nf=4
* Second stage gain device
XM6 VOUT N2 VDD VDD sky130_fd_pr__pfet_01v8 L=1.0 W=6.0 m=4
XM7 VOUT N2 VSS VSS sky130_fd_pr__nfet_01v8 L=1.0 W=3.0 m=4
* Bias mirror for tail
XM8 IBIAS IBIAS VSS VSS sky130_fd_pr__nfet_01v8 L=1.0 W=1.0 m=4
.ends opamp_synth_two_stage
