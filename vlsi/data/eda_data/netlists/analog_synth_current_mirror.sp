* Synthesized analog netlist (open, generated for testing)
* Basic current mirror with explicit m/nf
.subckt analog_synth_current_mirror VDD VSS IREF IOUT
* PMOS current mirror
XM1 IREF IREF VDD VDD sky130_fd_pr__pfet_01v8 L=0.6 W=2.0 nf=4
XM2 IOUT IREF VDD VDD sky130_fd_pr__pfet_01v8 L=0.6 W=2.0 nf=4
* NMOS bias sink
XM3 IREF IREF VSS VSS sky130_fd_pr__nfet_01v8 L=0.8 W=1.2 m=4
.ends analog_synth_current_mirror
