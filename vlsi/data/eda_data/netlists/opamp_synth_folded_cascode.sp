* Synthesized analog netlist (open, generated for testing)
* Folded-cascode style opamp core with explicit m/nf
.subckt opamp_synth_folded_cascode VINP VINN VDD VSS VOUT IBIAS
* Input pair (NMOS) with multiplicity
XM1 N1 VINP NSRC VSS sky130_fd_pr__nfet_01v8 L=0.5 W=1.2 m=4
XM2 N2 VINN NSRC VSS sky130_fd_pr__nfet_01v8 L=0.5 W=1.2 m=4
* Tail current source
XM3 NSRC IBIAS VSS VSS sky130_fd_pr__nfet_01v8 L=1.0 W=2.0 m=4
* Folded PMOS devices
XM4 N1 N1 VDD VDD sky130_fd_pr__pfet_01v8 L=0.6 W=2.4 nf=4
XM5 N2 N2 VDD VDD sky130_fd_pr__pfet_01v8 L=0.6 W=2.4 nf=4
* Cascode devices
XM6 VOUT N1 VDD VDD sky130_fd_pr__pfet_01v8 L=0.8 W=3.0 m=4
XM7 VOUT N2 VDD VDD sky130_fd_pr__pfet_01v8 L=0.8 W=3.0 m=4
* Bias mirrors
XM8 NBIAS IBIAS VSS VSS sky130_fd_pr__nfet_01v8 L=1.0 W=1.0 m=4
XM9 IBIAS IBIAS VSS VSS sky130_fd_pr__nfet_01v8 L=1.0 W=1.0 m=4
.ends opamp_synth_folded_cascode
