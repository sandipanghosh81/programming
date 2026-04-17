* Synthesized analog netlist (open, generated for testing)
* Cascode current mirror with explicit m/nf
.subckt analog_synth_cascode_mirror VDD VSS IREF IOUT VBIAS
* PMOS mirror devices
XM1 N1 IREF VDD VDD sky130_fd_pr__pfet_01v8 L=0.6 W=2.2 nf=4
XM2 IOUT IREF VDD VDD sky130_fd_pr__pfet_01v8 L=0.6 W=2.2 nf=4
* Cascode devices
XM3 N1 VBIAS VDD VDD sky130_fd_pr__pfet_01v8 L=0.8 W=1.8 m=4
XM4 IOUT VBIAS VDD VDD sky130_fd_pr__pfet_01v8 L=0.8 W=1.8 m=4
* NMOS bias sink
XM5 IREF IREF VSS VSS sky130_fd_pr__nfet_01v8 L=0.8 W=1.2 m=4
.ends analog_synth_cascode_mirror
