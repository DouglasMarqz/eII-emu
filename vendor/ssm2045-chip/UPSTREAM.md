Source: https://github.com/Tuth/ssm2045-chip
Pinned commit: 7ea4df873d9bb01fbc312882edb842eabc35aa5b
MIT license retained. Local change: src/chip.cpp includes its header by relative path.
Behavioural/SPICE-validated model; not hardware-measured. EII adapter is in src/nonlinear_voice.h outside this vendor directory.

EII0.55 local corrections/extensions: shared residual/Jacobian nonlinear evaluation and exact sparse4x4 elimination (generic pivoting fallback retained), state-preserving configure APIs and solver counters for the service panel. No tanh approximation or reduced Newton tolerance/iteration limit. Defaults retain upstream equations; bounded nonlinear and stable linear comparison in native validation max abs error5.65e-15.

0311 isolated experiment: solveSample-local reuse of accepted deriv output only at bit-identical final state. Equations, convergence and failure branches unchanged. Not retained yet.

0312isolatedtrial: VCAEXPOweight cached on validated construction/configure/setExpoVolts; original expression/order unchanged. Nativechecker/owneracceptance pending.
