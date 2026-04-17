* Synthesized analog netlist (open, generated for testing)
* Differential pair with active load and explicit m/nf
.subckt analog_synth_diffpair_active_load VINP VINN VDD VSS VOUT IBIAS
* Input NMOS pair
XM1 N1 VINP NSRC VSS sky130_fd_pr__nfet_01v8 L=0.5 W=1.0 m=4
XM2 N2 VINN NSRC VSS sky130_fd_pr__nfet_01v8 L=0.5 W=1.0 m=4
* Tail current source
XM3 NSRC IBIAS VSS VSS sky130_fd_pr__nfet_01v8 L=1.0 W=2.0 m=4
* PMOS active load (current mirror)
XM4 VOUT N2 VDD VDD sky130_fd_pr__pfet_01v8 L=0.6 W=2.0 nf=4
XM5 N2 N2 VDD VDD sky130_fd_pr__pfet_01v8 L=0.6 W=2.0 nf=4
.ends analog_synth_diffpair_active_load
