# L0499 proxy-data notice

`core99_l0499_proxy.bin` is a deterministic declared reconstruction for
Wen, Song and Wang, DOI `10.1016/j.enconman.2022.115347`.

The paper declares 41 NDAWN stations, twenty hourly years per station and
twelve 30-degree wind-direction sectors, but it does not publish the numeric
station records or a machine-readable archive. The fixture therefore stores
41 × 20 normalized direction-frequency vectors generated around the
multi-modal distributions visible in Fig. 10, with deterministic
station/year variation and a modest held-out-decade drift.

The fixture also stores power and thrust-coefficient knots digitized from
Fig. 7. At the paper's only optimized wind speed, 8 m/s, the stored turbine
power is 604 kW, consistent with the approximately 30.2 MW no-wake
50-turbine scale reported in Table 4.

This file is not author data and must not be described as an exact numeric
reproduction. Its purpose is to make the paper's complete
Dirichlet–Multinomial, normal-approximation and CVaR workflow executable,
testable and replaceable when primary arrays become available.
