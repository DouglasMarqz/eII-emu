* EII channel-1 board around the SSM2045 (service sheet 7-45 / 7-52)
.model D1N914F D(IS=1.99n N=1.752 RS=0.568)
Vcc 18 0 15
Vee 10 0 -5
Vm15 m15 0 -15
* signal: deglitch output Y -> R138 68k -> SIG IN
Vy  y 0 dc 0 ac 1
R138 y 2 68k
* FC network: FCV -> R122 22.6k, R130 124k to -15, R112 1k, C136 3.3u
Vfcv fcv 0 dc 2.5
R122 fcv 5 22.6k
R130 5 m15 124k
R112 5 0 1k
C136 5 0 3.3u
* Q: QCV -> R146 22k -> pin 17
Vqcv qcv 0 dc 0
R146 qcv 17 22k
* caps
C153 3 0 20n
C144 4 0 3.9n
C128 6 0 20n
C120 7 0 1.8n
* 4P load: R66 4.7k into the S3528 input amplifier virtual ground
R66 8 vg 4.7k
Vvg vg 0 0
* -BIAS: +15 -> R114 22k -> D5 1N914; R105 10k / R104 4.7k inverter (IC96, ideal)
R114 18 d5 22k
D5 d5 0 D1N914F
R105 d5 inv 10k
R104 inv bias 4.7k
Eop bias 0 0 inv 1e6
* VCA2 control: ACV -> R91 4.7k -> C112 1u -> R100 4.7k -> pin 15; pin 14 = -BIAS
Vacv acv 0 dc 0
R91 acv c112 4.7k
C112 c112 0 1u
R100 c112 15 4.7k
Vb14 14 bias 0
* VCA1 control/input grounded; VCA2 input pin 12 driven by test source
Vlin1 16 0 0
Vin11 11 0 dc 0 ac 0
Vin12 12 0 dc 0 ac 0
* MIX OUT -> S3528 output buffer as I/V: R154 4.7k || C161 1n (ideal op-amp)
R154 13 cout 4.7k
C161 13 cout 1n
Eio cout 0 0 13 1e6
X 1 2 3 4 5 6 7 8 0 10 11 12 13 14 15 16 17 18 SSM2045M2
.options temp=25 tnom=25
