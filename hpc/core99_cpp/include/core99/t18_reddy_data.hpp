/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T18 source-backed Cal/V90/AWEC data and declared
Figure-10(c) wind quadrature
Paper/DOI: Reddy 2020; 10.1016/j.apenergy.2020.115090.
Public source: https://github.com/sohailreddy/WindFLO revision
97dd43784bffb1c0c8a4388d8e7929b337d496a5, Apache-2.0.
Literal public assets: V90 Cp table SHA-256
11e2093e1ebb091e008ceafe9155e1315d282aede5519b99b9e9634efc7af477;
AWEC terrain SHA-256
fcf5f65b4697050ed6bccaa69340e3796db1526d8051c7ae243e614c10e86eed;
v1.0.0 Cal validation inputs are pinned by repository revision and blob IDs in
the controlling contract. The numeric 2019 cli-MATE wind table is absent; the
direction totals and seven conditional speed-bin weights below are a declared
digitization of Figure 10(c), normalized at construction and not result-fitted.
Tables 2--3 use the public 1000-point two-dimensional Sobol uniform-radius
sequence; optimization uses the separately declared 64-point equal-area rule.
Public/source conflicts, completions and claim boundary:
include/core99/reddy_t18.hpp.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <array>

namespace core99::t18::data {

struct TerrainSample {
    double x_m;
    double y_m;
    double elevation_m;
};

inline constexpr std::array<double, 16> v90_speed_mps{{
    3.5, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0,
    11.0, 12.0, 13.0, 14.0, 15.0, 16.0, 17.0, 18.0,
}};

inline constexpr std::array<double, 16> v90_cp{{
    0.22745656, 0.30876620, 0.39008800, 0.41941114,
    0.43471139, 0.44410204, 0.44779433, 0.43525609,
    0.40606852, 0.36965805, 0.32672428, 0.27599684,
    0.22758936, 0.18790378, 0.15670884, 0.13201484,
}};

inline constexpr std::array<double, 23> cal_speed_mps{{
    3.0, 3.2, 3.4, 3.6, 3.8, 4.0, 4.2, 4.4, 4.6, 4.8, 5.0,
    5.2, 5.4, 5.6, 5.8, 6.0, 6.2, 6.4, 6.6, 6.8, 7.0, 7.2, 7.4,
}};

inline constexpr std::array<double, 23> cal_cp{{
    0.1364, 0.1587, 0.1921, 0.2213, 0.2457, 0.2665, 0.2855,
    0.2983, 0.3066, 0.3117, 0.3138, 0.3105, 0.3043, 0.2938,
    0.2793, 0.2608, 0.2382, 0.2119, 0.1816, 0.1471, 0.1087,
    0.0664, 0.0204,
}};

inline constexpr std::array<double, 22> cal_ct_speed_mps{{
    3.0, 3.2, 3.4, 3.6, 3.8, 4.0, 4.2, 4.4, 4.6, 4.8, 5.0,
    5.2, 5.4, 5.6, 5.8, 6.0, 6.2, 6.4, 6.6, 6.8, 7.0, 7.2,
}};

inline constexpr std::array<double, 22> cal_ct{{
    0.11490, 0.16127, 0.20174, 0.23666, 0.26633, 0.29104,
    0.31099, 0.32639, 0.33738, 0.34406, 0.34649, 0.34470,
    0.33832, 0.33421, 0.33009, 0.32596, 0.32181, 0.31766,
    0.31349, 0.30931, 0.30511, 0.30090,
}};

inline constexpr std::array<double, 16> direction_degrees{{
    0.0, 22.5, 45.0, 67.5, 90.0, 112.5, 135.0, 157.5,
    180.0, 202.5, 225.0, 247.5, 270.0, 292.5, 315.0, 337.5,
}};

inline constexpr std::array<double, 16> digitized_direction_totals{{
    2.0, 2.8, 3.5, 3.0, 2.2, 1.5, 1.3, 1.8,
    3.0, 4.2, 6.8, 11.0, 15.0, 13.8, 5.6, 2.5,
}};

inline constexpr std::array<double, 7> speed_bin_midpoints_mps{{
    1.55, 2.70, 4.725, 7.20, 9.90, 12.825, 16.20,
}};

inline constexpr std::array<double, 7> digitized_speed_bin_weights{{
    0.012, 0.058, 0.185, 0.285, 0.255, 0.145, 0.060,
}};

inline constexpr std::array<TerrainSample, 489> awec_terrain{{
#include "core99/t18_reddy_terrain.inc"
}};

}  // namespace core99::t18::data
