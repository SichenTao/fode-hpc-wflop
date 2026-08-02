/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: Y16 pure-C++/HiGHS BMM, IMM, evaluator and bounded
Dinkelbach lifecycle
Paper/DOI: Huang et al.; 10.1109/TSTE.2026.3686029
First-party patent: CN121683298A/CN121683298B.
Public asset, missing information, conflicts, corrections, reconstruction,
semantic IDs, production backend and claim boundary:
include/core99/huang_y16.hpp
HPC analysis: evidence/development/Y16_H0_H4_mathematical_hpc_analysis_20260801.md
Controlling contract: shared/contracts/core99_y16_huang_2026.json.
The BMM and IMM use one corrected physical model. BMM materializes turbine-
wise power rows and duplicate point-conflict rows. IMM aggregates power by
regular-layout row and deduplicates the conflict graph. Both retain the same
piecewise-linear TI equations and are checked by the same evaluator.
Claim boundary: flexible academic reconstruction on declared site/wind/
terrain proxies, not author code, private arrays, Gurobi or numeric replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/huang_y16.hpp"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
#include "Highs.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include "fode/executor.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace core99::y16 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr int kWindScenarios = 16;
constexpr double kAmbientTi = 0.077;
constexpr double kDiscountRate = 0.053;
constexpr double kOperatingYears = 20.0;
constexpr double kElectricalCnyPerKw = 2200.0;
constexpr double kOtherCnyPerKw = 950.0;
constexpr double kSupportH = 0.0005;
constexpr double kSupportD = 0.065;
constexpr double kSupportE = 3.8;
constexpr double kSupportF = 0.025;
constexpr double kSupportG = 0.0;
constexpr double kSteelCnyPerTonne = 18930.0;
constexpr double kAdditionalRockInstallCny = 1.0e6;
constexpr double kRepairCoefficient = 0.5;
constexpr double kReferenceTi = 0.12;
constexpr double kDisturbanceCoefficient = 0.7;
constexpr double kFaultFatigue = 0.7;
constexpr double kPreventiveIntervalYears = 0.5;
constexpr double kHoursPerYear = 8760.0;
constexpr double kInfinity = 1.0e30;
constexpr double kPi = std::numbers::pi_v<double>;

struct WindState {
    double direction_rad = 0.0;
    double probability = 0.0;
    double speed_mps = 0.0;
};

struct Site {
    std::vector<Point> polygon;
    std::array<WindState, kWindScenarios> wind{};
};

struct CandidatePoint {
    Point point;
    int row = -1;
};

struct RowPattern {
    int local_row = 0;
    std::vector<int> points;
    bool intrinsically_feasible = true;
};

struct RowPair {
    int left = 0;
    int right = 0;
    int z_column = -1;
    bool spacing_conflict = false;
};

struct SubproblemGeometry {
    int angle_degrees = 0;
    int pattern = 0;
    std::vector<CandidatePoint> points;
    std::vector<RowPattern> rows;
    std::vector<RowPair> pairs;
};

struct Coefficients {
    std::vector<double> free_power_kw;
    std::vector<double> internal_power_loss_kw;
    std::vector<double> cross_power_loss_kw;
    std::vector<double> internal_ti_square;
    std::vector<double> cross_ti_square;
};

struct LinearModel {
    HighsModel model;
    int row_count = 0;
    std::vector<int> x_columns;
    std::vector<RowPair> pairs;
    std::vector<double> annual_cost_coefficients;
    std::vector<double> capital_annualized_coefficients;
    std::vector<double> aep_kwh_coefficients;
};

struct SolveReceipt {
    bool has_incumbent = false;
    std::string status;
    double seconds = 0.0;
    double objective = 0.0;
    double gap = 0.0;
    std::vector<int> selected_rows;
};

struct BoundReceipt {
    bool feasible = false;
    double lower = std::numeric_limits<double>::infinity();
    double upper = std::numeric_limits<double>::infinity();
    double mip_seconds = 0.0;
    int solves = 0;
};

struct SubproblemResult {
    bool feasible = false;
    std::string status;
    int angle_degrees = -1;
    int pattern = -1;
    int iterations = 0;
    int solves = 0;
    double coefficient_seconds = 0.0;
    double mip_seconds = 0.0;
    Evaluation evaluation;
    std::vector<Point> layout;
};

double elapsed_seconds(const Clock::time_point& start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

Turbine wt1() {
    return {"WT-1",155.0,95.0,3.0,10.1,25.0,5500.0,5.34,0.05,0.88,
            19.0e6,64.0,2.5e6,3.25e5,5.75e5,5.0e3,1.35e6};
}

Turbine wt2() {
    return {"WT-2",90.0,90.0,3.0,11.0,25.0,3000.0,2.59,0.05,0.88,
            11.0e6,43.0,1.3e6,2.25e5,3.45e5,3.0e3,0.8e6};
}

Turbine wt3() {
    return {"WT-3",263.0,155.0,3.0,10.6,21.0,13000.0,10.92,0.05,0.88,
            44.0e6,95.0,4.0e6,5.2e5,9.2e5,15.0e3,2.5e6};
}

std::array<double,kWindScenarios> normalized(
    std::array<double,kWindScenarios> values
) {
    const double sum = std::accumulate(values.begin(),values.end(),0.0);
    if (!(sum > 0.0)) throw std::runtime_error("Y16 empty wind distribution");
    for (double& value : values) value /= sum;
    return values;
}

Site site_for(const SiteKind kind) {
    Site result;
    if (kind == SiteKind::hainan) {
        result.polygon = {
            {0.0,7000.0},{3900.0,0.0},{9400.0,0.0},
            {13000.0,10000.0},{0.0,10000.0},
        };
        const auto probabilities = normalized({
            0.035,0.100,0.190,0.135,0.050,0.025,0.020,0.025,
            0.030,0.040,0.075,0.150,0.045,0.025,0.020,0.035,
        });
        for (int index=0; index<kWindScenarios; ++index) {
            result.wind[static_cast<std::size_t>(index)] = {
                index*22.5*kPi/180.0,
                probabilities[static_cast<std::size_t>(index)],
                8.67,
            };
        }
    } else {
        result.polygon = {
            {0.0,0.0},{7900.0,0.0},{11200.0,5400.0},
            {10200.0,7600.0},{0.0,2850.0},
        };
        const auto probabilities = normalized({
            0.045,0.050,0.060,0.075,0.090,0.085,0.065,0.050,
            0.040,0.040,0.045,0.055,0.070,0.080,0.080,0.070,
        });
        const std::array<double,kWindScenarios> speeds{
            8.0,8.3,8.7,9.0,9.2,9.0,8.6,8.2,
            7.8,7.5,7.4,7.6,7.9,8.2,8.4,8.2,
        };
        for (int index=0; index<kWindScenarios; ++index) {
            result.wind[static_cast<std::size_t>(index)] = {
                index*22.5*kPi/180.0,
                probabilities[static_cast<std::size_t>(index)],
                speeds[static_cast<std::size_t>(index)],
            };
        }
    }
    return result;
}

bool point_in_polygon(const Point& point, const std::vector<Point>& polygon) {
    bool inside = false;
    for (std::size_t current=0, previous=polygon.size()-1;
         current<polygon.size(); previous=current++) {
        const Point& a = polygon[current];
        const Point& b = polygon[previous];
        const bool crosses = ((a.y_m>point.y_m)!=(b.y_m>point.y_m))
            && point.x_m < (b.x_m-a.x_m)*(point.y_m-a.y_m)
                /(b.y_m-a.y_m) + a.x_m;
        if (crosses) inside = !inside;
    }
    return inside;
}

double square_distance(const Point& left, const Point& right) {
    const double dx=left.x_m-right.x_m;
    const double dy=left.y_m-right.y_m;
    return dx*dx+dy*dy;
}

std::pair<int,int> pattern_specification(
    const int pattern,
    const double grid_spacing_diameters
) {
    if (pattern<0 || pattern>=10) {
        throw std::out_of_range("Y16 pattern");
    }
    const int minimum_gap=std::max(
        1,static_cast<int>(std::ceil(5.0/grid_spacing_diameters-1.0e-12))
    );
    int visited=0;
    for (int gap=minimum_gap; ; ++gap) {
        for (int offset=0; offset<gap; ++offset) {
            if (visited++==pattern) return {offset,gap};
        }
    }
}

SubproblemGeometry build_geometry(
    const Scenario& scenario,
    const int angle_index,
    const int pattern_index
) {
    SubproblemGeometry result;
    result.angle_degrees = 10*angle_index;
    result.pattern = pattern_index;
    const Site site = site_for(scenario.site);
    const double angle=result.angle_degrees*kPi/180.0;
    const double cosine=std::cos(angle);
    const double sine=std::sin(angle);
    double min_x=kInfinity,max_x=-kInfinity,min_y=kInfinity,max_y=-kInfinity;
    for (const Point& point : site.polygon) {
        min_x=std::min(min_x,point.x_m); max_x=std::max(max_x,point.x_m);
        min_y=std::min(min_y,point.y_m); max_y=std::max(max_y,point.y_m);
    }
    const Point center{0.5*(min_x+max_x),0.5*(min_y+max_y)};
    double min_u=kInfinity,max_u=-kInfinity,min_v=kInfinity,max_v=-kInfinity;
    for (const Point& point : site.polygon) {
        const double dx=point.x_m-center.x_m;
        const double dy=point.y_m-center.y_m;
        const double u=cosine*dx+sine*dy;
        const double v=-sine*dx+cosine*dy;
        min_u=std::min(min_u,u); max_u=std::max(max_u,u);
        min_v=std::min(min_v,v); max_v=std::max(max_v,v);
    }
    const double pitch=scenario.grid_spacing_diameters*scenario.turbine.diameter_m;
    const int columns=std::max(1,static_cast<int>(std::floor((max_u-min_u)/pitch))+1);
    const int rows=std::max(1,static_cast<int>(std::floor((max_v-min_v)/pitch))+1);
    const double u0=0.5*(min_u+max_u)-0.5*(columns-1)*pitch;
    const double v0=0.5*(min_v+max_v)-0.5*(rows-1)*pitch;
    const auto [offset,gap]=pattern_specification(
        pattern_index,scenario.grid_spacing_diameters
    );
    for (int row=0; row<rows; ++row) {
        std::vector<std::pair<int,Point>> feasible;
        for (int column=0; column<columns; ++column) {
            const double u=u0+column*pitch;
            const double v=v0+row*pitch;
            const Point point{
                center.x_m+cosine*u-sine*v,
                center.y_m+sine*u+cosine*v,
            };
            if (point_in_polygon(point,site.polygon)) {
                feasible.emplace_back(column,point);
            }
        }
        if (feasible.empty()) continue;
        RowPattern row_pattern;
        row_pattern.local_row=row;
        const int first_column=feasible.front().first;
        for (const auto& [column,point] : feasible) {
            if ((column-first_column-offset)%gap!=0
                || column-first_column<offset) continue;
            const int index=static_cast<int>(result.points.size());
            result.points.push_back({point,static_cast<int>(result.rows.size())});
            row_pattern.points.push_back(index);
        }
        if (row_pattern.points.empty()
            || static_cast<int>(row_pattern.points.size())>scenario.turbine_count) {
            result.points.resize(
                result.points.size()-row_pattern.points.size()
            );
            continue;
        }
        const double minimum=5.0*scenario.turbine.diameter_m;
        for (std::size_t left=0; left<row_pattern.points.size(); ++left) {
            for (std::size_t right=left+1; right<row_pattern.points.size(); ++right) {
                if (square_distance(
                        result.points[static_cast<std::size_t>(row_pattern.points[left])].point,
                        result.points[static_cast<std::size_t>(row_pattern.points[right])].point
                    ) < minimum*minimum-1.0e-7) {
                    row_pattern.intrinsically_feasible=false;
                }
            }
        }
        result.rows.push_back(std::move(row_pattern));
    }
    for (int left=0; left<static_cast<int>(result.rows.size()); ++left) {
        for (int right=left+1; right<static_cast<int>(result.rows.size()); ++right) {
            bool conflict=false;
            const double minimum=5.0*scenario.turbine.diameter_m;
            for (const int lp : result.rows[static_cast<std::size_t>(left)].points) {
                for (const int rp : result.rows[static_cast<std::size_t>(right)].points) {
                    if (square_distance(
                            result.points[static_cast<std::size_t>(lp)].point,
                            result.points[static_cast<std::size_t>(rp)].point
                        ) < minimum*minimum-1.0e-7) {
                        conflict=true;
                    }
                }
            }
            result.pairs.push_back({left,right,-1,conflict});
        }
    }
    return result;
}

double depth_m(const SiteKind site, const Point& point) {
    if (site==SiteKind::zhuhai_type1) return 20.0;
    if (site==SiteKind::zhuhai_type2) {
        return std::clamp(12.0+16.0*point.x_m/11200.0,12.0,28.0);
    }
    if (site==SiteKind::zhuhai_type3) {
        return std::clamp(
            20.0+4.2*std::sin(point.x_m/720.0)
                +2.7*std::cos(point.y_m/610.0)
                +1.5*std::sin((point.x_m+point.y_m)/430.0),
            12.0,28.0
        );
    }
    return std::clamp(
        40.0+1.5*std::sin(point.x_m/1800.0)
            +1.2*std::cos(point.y_m/1500.0),
        37.0,43.0
    );
}

bool rocky(const SiteKind site, const Point& point) {
    if (site==SiteKind::zhuhai_type3) {
        const bool upper=point.x_m>7600.0 && point.x_m<10100.0
            && point.y_m>2800.0 && point.y_m<4300.0
            && point.y_m<0.65*point.x_m-2200.0;
        const bool lower=point.x_m>7300.0 && point.x_m<10000.0
            && point.y_m>700.0 && point.y_m<2300.0
            && point.y_m>0.35*point.x_m-2500.0;
        return upper || lower;
    }
    if (site==SiteKind::hainan) {
        return point.x_m<2600.0 && point.y_m>2600.0
            && point.y_m<7600.0 && point.x_m<0.32*(7600.0-point.y_m);
    }
    return false;
}

double support_cost(const double depth) {
    const double diameter=std::max(4.0,kSupportH*depth*depth+kSupportD*depth+kSupportE);
    const double thickness=kSupportF*depth+kSupportG;
    const double length=2.0*depth+20.0;
    return kPi*diameter*thickness*length*kSteelCnyPerTonne;
}

double installation_cost(
    const Scenario& scenario,
    const Point& point
) {
    const double depth=depth_m(scenario.site,point);
    if (rocky(scenario.site,point)) {
        return scenario.turbine.base_install_cny
            +scenario.turbine.rock_depth_cny_per_m*depth
            +kAdditionalRockInstallCny;
    }
    return scenario.turbine.base_install_cny
        +scenario.turbine.mud_depth_cny_per_m*depth;
}

double free_power_kw(const Turbine& turbine, const double speed) {
    if (speed<turbine.cut_in_mps || speed>=turbine.cut_out_mps) return 0.0;
    if (speed>=turbine.rated_mps) return turbine.rated_power_kw;
    return std::min(
        turbine.rated_power_kw,
        turbine.cubic_power_coefficient*speed*speed*speed
    );
}

struct Interaction {
    double power_loss_kw=0.0;
    double ti_square=0.0;
};

Interaction interaction(
    const Turbine& turbine,
    const Point& source,
    const Point& target,
    const WindState& wind
) {
    const double downwind_x=-std::sin(wind.direction_rad);
    const double downwind_y=-std::cos(wind.direction_rad);
    const double dx=target.x_m-source.x_m;
    const double dy=target.y_m-source.y_m;
    const double downstream=dx*downwind_x+dy*downwind_y;
    if (!(downstream>0.0)) return {};
    const double crosswind=std::abs(-dx*downwind_y+dy*downwind_x);
    const double ct=turbine.thrust_coefficient;
    const double alpha=0.5*(1.0-std::sqrt(1.0-ct));
    const double rotor_radius=0.5*turbine.diameter_m;
    const double initial_radius=rotor_radius*std::sqrt(
        (1.0-alpha)/(1.0-2.0*alpha)
    );
    const double wake_radius=initial_radius+turbine.wake_expansion*downstream;
    double deficit=0.0;
    if (crosswind<=wake_radius) {
        deficit=(1.0-std::sqrt(1.0-ct))*initial_radius*initial_radius
            /(wake_radius*wake_radius)
            *(1.0-std::cos(kPi*crosswind/wake_radius+kPi));
    }
    const double waked_speed=wind.speed_mps*std::max(0.0,1.0-deficit);
    const double power_loss=std::max(
        0.0,free_power_kw(turbine,wind.speed_mps)
            -free_power_kw(turbine,waked_speed)
    );
    const double normalized_distance=downstream/turbine.diameter_m;
    const double g=1.0/(
        2.3*std::pow(ct,-1.2)
        +std::pow(kAmbientTi,0.1)*normalized_distance
        +0.7*std::pow(ct,-3.2)*std::pow(kAmbientTi,-0.45)
            *std::pow(1.0+normalized_distance,-2.0)
    );
    const double sigma=turbine.diameter_m*(
        turbine.wake_expansion*normalized_distance
        +0.23*std::pow(ct,-0.25)*std::pow(kAmbientTi,0.17)
    );
    double k1=1.0,k2=1.0;
    if (crosswind<=rotor_radius) {
        k1=std::pow(std::cos(0.5*kPi*(crosswind/turbine.diameter_m-0.5)),2.0);
        k2=std::pow(std::cos(0.5*kPi*(crosswind/turbine.diameter_m+0.5)),2.0);
    }
    const double h=k1*std::exp(-std::pow(crosswind-rotor_radius,2.0)/(2.0*sigma*sigma))
        +k2*std::exp(-std::pow(crosswind+rotor_radius,2.0)/(2.0*sigma*sigma));
    const double added=g*h;
    return {power_loss,added*added};
}

Coefficients build_coefficients(
    const Scenario& scenario,
    const SubproblemGeometry& geometry
) {
    const int points=static_cast<int>(geometry.points.size());
    Coefficients result;
    result.free_power_kw.resize(static_cast<std::size_t>(points)*kWindScenarios);
    result.internal_power_loss_kw.assign(result.free_power_kw.size(),0.0);
    result.internal_ti_square.assign(result.free_power_kw.size(),0.0);
    result.cross_power_loss_kw.assign(
        static_cast<std::size_t>(geometry.pairs.size())*2U
            *static_cast<std::size_t>(points)*kWindScenarios,
        0.0
    );
    result.cross_ti_square.assign(result.cross_power_loss_kw.size(),0.0);
    const Site site=site_for(scenario.site);
    for (int point=0; point<points; ++point) {
        for (int wind=0; wind<kWindScenarios; ++wind) {
            result.free_power_kw[static_cast<std::size_t>(point)*kWindScenarios+wind]
                =free_power_kw(scenario.turbine,site.wind[static_cast<std::size_t>(wind)].speed_mps);
        }
    }
    auto cross_index=[&](const int pair,const int side,const int point,const int wind) {
        return (((static_cast<std::size_t>(pair)*2U+static_cast<std::size_t>(side))
                 *static_cast<std::size_t>(points)+static_cast<std::size_t>(point))
                *kWindScenarios+static_cast<std::size_t>(wind));
    };
    for (int target=0; target<points; ++target) {
        const int target_row=geometry.points[static_cast<std::size_t>(target)].row;
        for (int source=0; source<points; ++source) {
            if (source==target) continue;
            const int source_row=geometry.points[static_cast<std::size_t>(source)].row;
            int pair_index=-1;
            int side=0;
            if (source_row!=target_row) {
                const int left=std::min(source_row,target_row);
                const int right=std::max(source_row,target_row);
                const int row_count=static_cast<int>(geometry.rows.size());
                pair_index=left*(2*row_count-left-1)/2+(right-left-1);
                side=(target_row==left)?0:1;
            }
            for (int wind=0; wind<kWindScenarios; ++wind) {
                const Interaction value=interaction(
                    scenario.turbine,
                    geometry.points[static_cast<std::size_t>(source)].point,
                    geometry.points[static_cast<std::size_t>(target)].point,
                    site.wind[static_cast<std::size_t>(wind)]
                );
                if (source_row==target_row) {
                    const std::size_t index=static_cast<std::size_t>(target)*kWindScenarios+wind;
                    result.internal_power_loss_kw[index]+=value.power_loss_kw;
                    result.internal_ti_square[index]+=value.ti_square;
                } else {
                    const std::size_t index=cross_index(pair_index,side,target,wind);
                    result.cross_power_loss_kw[index]+=value.power_loss_kw;
                    result.cross_ti_square[index]+=value.ti_square;
                }
            }
        }
    }
    return result;
}

class Builder {
public:
    int variable(
        const double lower,
        const double upper,
        const HighsVarType type
    ) {
        const int column=static_cast<int>(model_.lp_.col_lower_.size());
        model_.lp_.col_lower_.push_back(lower);
        model_.lp_.col_upper_.push_back(upper);
        model_.lp_.col_cost_.push_back(0.0);
        model_.lp_.integrality_.push_back(type);
        return column;
    }

    void row(
        std::vector<std::pair<int,double>> entries,
        const double lower,
        const double upper
    ) {
        std::sort(entries.begin(),entries.end());
        for (const auto& [column,value] : entries) {
            if (std::abs(value)<=1.0e-18) continue;
            matrix_.index_.push_back(column);
            matrix_.value_.push_back(value);
        }
        matrix_.start_.push_back(static_cast<HighsInt>(matrix_.index_.size()));
        lower_.push_back(lower);
        upper_.push_back(upper);
    }

    HighsModel finish() {
        model_.lp_.num_col_=static_cast<HighsInt>(model_.lp_.col_lower_.size());
        model_.lp_.num_row_=static_cast<HighsInt>(lower_.size());
        model_.lp_.sense_=ObjSense::kMinimize;
        model_.lp_.row_lower_=std::move(lower_);
        model_.lp_.row_upper_=std::move(upper_);
        model_.lp_.a_matrix_=std::move(matrix_);
        return std::move(model_);
    }

    Builder() {
        matrix_.format_=MatrixFormat::kRowwise;
        matrix_.start_.clear();
        matrix_.start_.push_back(0);
    }

private:
    HighsModel model_;
    HighsSparseMatrix matrix_;
    std::vector<double> lower_;
    std::vector<double> upper_;
};

double capital_recovery_factor() {
    const double factor=std::pow(1.0+kDiscountRate,kOperatingYears);
    return kDiscountRate*factor/(factor-1.0);
}

LinearModel build_model(
    const Scenario& scenario,
    const SubproblemGeometry& geometry,
    const Coefficients& coefficients
) {
    Builder builder;
    LinearModel result;
    const int rows=static_cast<int>(geometry.rows.size());
    const int points=static_cast<int>(geometry.points.size());
    result.row_count=rows;
    result.x_columns.resize(static_cast<std::size_t>(rows));
    for (int row=0; row<rows; ++row) {
        result.x_columns[static_cast<std::size_t>(row)] = builder.variable(
            0.0,1.0,HighsVarType::kInteger
        );
    }
    result.pairs=geometry.pairs;
    for (auto& pair : result.pairs) {
        pair.z_column=builder.variable(0.0,1.0,HighsVarType::kInteger);
        const int xl=result.x_columns[static_cast<std::size_t>(pair.left)];
        const int xr=result.x_columns[static_cast<std::size_t>(pair.right)];
        builder.row({{pair.z_column,1.0},{xl,-1.0}},-kInfinity,0.0);
        builder.row({{pair.z_column,1.0},{xr,-1.0}},-kInfinity,0.0);
        builder.row({{xl,1.0},{xr,1.0},{pair.z_column,-1.0}},-kInfinity,1.0);
    }
    std::vector<std::pair<int,double>> cardinality;
    for (int row=0; row<rows; ++row) {
        cardinality.emplace_back(
            result.x_columns[static_cast<std::size_t>(row)],
            static_cast<double>(geometry.rows[static_cast<std::size_t>(row)].points.size())
        );
        if (!geometry.rows[static_cast<std::size_t>(row)].intrinsically_feasible) {
            builder.row(
                {{result.x_columns[static_cast<std::size_t>(row)],1.0}},0.0,0.0
            );
        }
    }
    builder.row(cardinality,scenario.turbine_count,scenario.turbine_count);
    if (scenario.model==ModelKind::imm) {
        for (const auto& pair : result.pairs) {
            if (pair.spacing_conflict) {
                builder.row({
                    {result.x_columns[static_cast<std::size_t>(pair.left)],1.0},
                    {result.x_columns[static_cast<std::size_t>(pair.right)],1.0},
                },-kInfinity,1.0);
            }
        }
    } else {
        const double minimum2=std::pow(5.0*scenario.turbine.diameter_m,2.0);
        for (int left=0; left<points; ++left) {
            for (int right=left+1; right<points; ++right) {
                if (square_distance(
                        geometry.points[static_cast<std::size_t>(left)].point,
                        geometry.points[static_cast<std::size_t>(right)].point
                    )>=minimum2-1.0e-7) continue;
                const int lr=geometry.points[static_cast<std::size_t>(left)].row;
                const int rr=geometry.points[static_cast<std::size_t>(right)].row;
                if (lr==rr) {
                    builder.row({{result.x_columns[static_cast<std::size_t>(lr)],1.0}},0.0,0.0);
                } else {
                    builder.row({
                        {result.x_columns[static_cast<std::size_t>(lr)],1.0},
                        {result.x_columns[static_cast<std::size_t>(rr)],1.0},
                    },-kInfinity,1.0);
                }
            }
        }
    }
    const auto pair_index=[&](const int left,const int right) {
        const int a=std::min(left,right);
        const int b=std::max(left,right);
        return a*(2*rows-a-1)/2+(b-a-1);
    };
    const auto cross_index=[&](const int pair,const int side,const int point,const int wind) {
        return (((static_cast<std::size_t>(pair)*2U+static_cast<std::size_t>(side))
                 *static_cast<std::size_t>(points)+static_cast<std::size_t>(point))
                *kWindScenarios+static_cast<std::size_t>(wind));
    };
    std::vector<int> power_columns;
    std::vector<double> power_probabilities;
    const Site site=site_for(scenario.site);
    if (scenario.model==ModelKind::bmm) {
        power_columns.reserve(static_cast<std::size_t>(points)*kWindScenarios);
        power_probabilities.reserve(power_columns.capacity());
        for (int point=0; point<points; ++point) {
            const int target_row=geometry.points[static_cast<std::size_t>(point)].row;
            for (int wind=0; wind<kWindScenarios; ++wind) {
                const int p=builder.variable(0.0,scenario.turbine.rated_power_kw,HighsVarType::kContinuous);
                std::vector<std::pair<int,double>> equation{{p,1.0}};
                const std::size_t base=static_cast<std::size_t>(point)*kWindScenarios+wind;
                equation.emplace_back(
                    result.x_columns[static_cast<std::size_t>(target_row)],
                    -coefficients.free_power_kw[base]+coefficients.internal_power_loss_kw[base]
                );
                for (int source_row=0; source_row<rows; ++source_row) {
                    if (source_row==target_row) continue;
                    const int pair=pair_index(source_row,target_row);
                    const int side=(target_row==result.pairs[static_cast<std::size_t>(pair)].left)?0:1;
                    const double loss=coefficients.cross_power_loss_kw[
                        cross_index(pair,side,point,wind)
                    ];
                    if (loss>0.0) equation.emplace_back(
                        result.pairs[static_cast<std::size_t>(pair)].z_column,loss
                    );
                }
                builder.row(std::move(equation),0.0,0.0);
                power_columns.push_back(p);
                power_probabilities.push_back(site.wind[static_cast<std::size_t>(wind)].probability);
            }
        }
    } else {
        power_columns.reserve(static_cast<std::size_t>(rows)*kWindScenarios);
        power_probabilities.reserve(power_columns.capacity());
        for (int target_row=0; target_row<rows; ++target_row) {
            for (int wind=0; wind<kWindScenarios; ++wind) {
                const double maximum=scenario.turbine.rated_power_kw
                    *geometry.rows[static_cast<std::size_t>(target_row)].points.size();
                const int p=builder.variable(0.0,maximum,HighsVarType::kContinuous);
                std::vector<std::pair<int,double>> equation{{p,1.0}};
                double base=0.0;
                for (const int point : geometry.rows[static_cast<std::size_t>(target_row)].points) {
                    const std::size_t index=static_cast<std::size_t>(point)*kWindScenarios+wind;
                    base+=coefficients.free_power_kw[index]-coefficients.internal_power_loss_kw[index];
                }
                equation.emplace_back(
                    result.x_columns[static_cast<std::size_t>(target_row)],-base
                );
                for (int source_row=0; source_row<rows; ++source_row) {
                    if (source_row==target_row) continue;
                    const int pair=pair_index(source_row,target_row);
                    const int side=(target_row==result.pairs[static_cast<std::size_t>(pair)].left)?0:1;
                    double loss=0.0;
                    for (const int point : geometry.rows[static_cast<std::size_t>(target_row)].points) {
                        loss+=coefficients.cross_power_loss_kw[
                            cross_index(pair,side,point,wind)
                        ];
                    }
                    if (loss>0.0) equation.emplace_back(
                        result.pairs[static_cast<std::size_t>(pair)].z_column,loss
                    );
                }
                builder.row(std::move(equation),0.0,0.0);
                power_columns.push_back(p);
                power_probabilities.push_back(site.wind[static_cast<std::size_t>(wind)].probability);
            }
        }
    }

    struct TiTerm { int column; double annual_cost_factor; };
    std::vector<TiTerm> ti_terms;
    const double lower_ti_square=kAmbientTi*kAmbientTi;
    const int intervals=scenario.ti_intervals;
    for (int point=0; point<points; ++point) {
        const int target_row=geometry.points[static_cast<std::size_t>(point)].row;
        for (int wind=0; wind<kWindScenarios; ++wind) {
            std::vector<std::pair<int,double>> eta_sum;
            std::vector<std::pair<int,double>> square_sum;
            for (int interval=0; interval<intervals; ++interval) {
                const double lo=lower_ti_square
                    +(1.0-lower_ti_square)*interval/intervals;
                const double hi=lower_ti_square
                    +(1.0-lower_ti_square)*(interval+1)/intervals;
                const double slope=(std::sqrt(hi)-std::sqrt(lo))/(hi-lo);
                const double intercept=std::sqrt(lo)-slope*lo;
                const int eta=builder.variable(0.0,1.0,HighsVarType::kInteger);
                const int shat=builder.variable(0.0,hi,HighsVarType::kContinuous);
                builder.row({{shat,1.0},{eta,-lo}},0.0,kInfinity);
                builder.row({{shat,1.0},{eta,-hi}},-kInfinity,0.0);
                eta_sum.emplace_back(eta,1.0);
                square_sum.emplace_back(shat,1.0);
                const double probability=site.wind[static_cast<std::size_t>(wind)].probability;
                const double factor=scenario.turbine.corrective_cny/kFaultFatigue
                    *kDisturbanceCoefficient*probability
                    /(kReferenceTi*(1.0+kRepairCoefficient));
                ti_terms.push_back({shat,factor*slope});
                ti_terms.push_back({eta,factor*intercept});
            }
            eta_sum.emplace_back(
                result.x_columns[static_cast<std::size_t>(target_row)],-1.0
            );
            builder.row(std::move(eta_sum),0.0,0.0);
            const std::size_t base=static_cast<std::size_t>(point)*kWindScenarios+wind;
            square_sum.emplace_back(
                result.x_columns[static_cast<std::size_t>(target_row)],
                -(lower_ti_square+coefficients.internal_ti_square[base])
            );
            for (int source_row=0; source_row<rows; ++source_row) {
                if (source_row==target_row) continue;
                const int pair=pair_index(source_row,target_row);
                const int side=(target_row==result.pairs[static_cast<std::size_t>(pair)].left)?0:1;
                const double value=coefficients.cross_ti_square[
                    cross_index(pair,side,point,wind)
                ];
                if (value>0.0) square_sum.emplace_back(
                    result.pairs[static_cast<std::size_t>(pair)].z_column,-value
                );
            }
            builder.row(std::move(square_sum),0.0,0.0);
        }
    }
    result.model=builder.finish();
    const std::size_t columns=static_cast<std::size_t>(result.model.lp_.num_col_);
    result.annual_cost_coefficients.assign(columns,0.0);
    result.capital_annualized_coefficients.assign(columns,0.0);
    result.aep_kwh_coefficients.assign(columns,0.0);
    const double crf=capital_recovery_factor();
    for (int row=0; row<rows; ++row) {
        double capital=0.0;
        for (const int point : geometry.rows[static_cast<std::size_t>(row)].points) {
            const Point& location=geometry.points[static_cast<std::size_t>(point)].point;
            capital+=scenario.turbine.turbine_cost_cny
                +kElectricalCnyPerKw*scenario.turbine.rated_power_kw
                +kOtherCnyPerKw*scenario.turbine.rated_power_kw
                +support_cost(depth_m(scenario.site,location))
                +installation_cost(scenario,location);
        }
        const double annualized=crf*capital;
        const double count=geometry.rows[static_cast<std::size_t>(row)].points.size();
        const int column=result.x_columns[static_cast<std::size_t>(row)];
        result.capital_annualized_coefficients[static_cast<std::size_t>(column)]=annualized;
        result.annual_cost_coefficients[static_cast<std::size_t>(column)]
            =annualized
            +scenario.turbine.opex_cny_per_kw_year
                *scenario.turbine.rated_power_kw*count
            +kOperatingYears/kPreventiveIntervalYears
                *scenario.turbine.preventive_cny*count;
    }
    for (std::size_t index=0; index<power_columns.size(); ++index) {
        const int column=power_columns[index];
        const double probability=power_probabilities[index];
        result.aep_kwh_coefficients[static_cast<std::size_t>(column)]
            =kHoursPerYear*probability;
        result.annual_cost_coefficients[static_cast<std::size_t>(column)]
            +=scenario.turbine.corrective_cny/kFaultFatigue
                *probability/(scenario.turbine.rated_power_kw
                    *(1.0+kRepairCoefficient));
    }
    for (const TiTerm& term : ti_terms) {
        result.annual_cost_coefficients[static_cast<std::size_t>(term.column)]
            +=term.annual_cost_factor;
    }
    return result;
}

SolveReceipt solve_model(
    LinearModel model,
    const std::vector<double>& objective,
    const double time_limit
) {
    if (objective.size()!=static_cast<std::size_t>(model.model.lp_.num_col_)) {
        throw std::runtime_error("Y16 objective dimension mismatch");
    }
    model.model.lp_.col_cost_=objective;
    Highs highs;
    highs.setOptionValue("output_flag",false);
    highs.setOptionValue("threads",1);
    highs.setOptionValue("parallel",kHighsOffString);
    highs.setOptionValue("mip_rel_gap",0.0);
    highs.setOptionValue("mip_abs_gap",0.0);
    highs.setOptionValue("mip_detect_symmetry",false);
    // HiGHS presolve can report a false infeasibility on this highly
    // disjunctive TI/MILFP reconstruction; the unchanged model solves and
    // verifies under presolve-off.  Keep the conservative solver path.
    highs.setOptionValue("presolve",kHighsOffString);
    highs.setOptionValue("time_limit",time_limit);
    highs.setOptionValue("random_seed",16);
    const auto start=Clock::now();
    if (highs.passModel(model.model)==HighsStatus::kError) {
        throw std::runtime_error("HiGHS rejected Y16 MILP");
    }
    const HighsStatus status=highs.run();
    SolveReceipt result;
    result.seconds=elapsed_seconds(start);
    result.status=highs.modelStatusToString(highs.getModelStatus());
    if (status==HighsStatus::kError) return result;
    const HighsSolution& solution=highs.getSolution();
    if (!solution.value_valid) return result;
    result.has_incumbent=true;
    const HighsInfo& info=highs.getInfo();
    result.objective=info.objective_function_value;
    result.gap=info.mip_gap;
    for (int row=0; row<model.row_count; ++row) {
        const int column=model.x_columns[static_cast<std::size_t>(row)];
        if (solution.col_value[static_cast<std::size_t>(column)]>=0.5) {
            result.selected_rows.push_back(row);
        }
    }
    return result;
}

std::vector<Point> layout_from_rows(
    const SubproblemGeometry& geometry,
    const std::vector<int>& rows
) {
    std::vector<Point> layout;
    for (const int row : rows) {
        for (const int point : geometry.rows[static_cast<std::size_t>(row)].points) {
            layout.push_back(geometry.points[static_cast<std::size_t>(point)].point);
        }
    }
    std::sort(layout.begin(),layout.end(),[](const Point& left,const Point& right) {
        if (left.x_m!=right.x_m) return left.x_m<right.x_m;
        return left.y_m<right.y_m;
    });
    return layout;
}

double piecewise_ti(const double square_value, const int intervals) {
    const double lower=kAmbientTi*kAmbientTi;
    const double bounded=std::clamp(square_value,lower,1.0);
    int interval=static_cast<int>(
        (bounded-lower)/(1.0-lower)*intervals
    );
    interval=std::clamp(interval,0,intervals-1);
    const double lo=lower+(1.0-lower)*interval/intervals;
    const double hi=lower+(1.0-lower)*(interval+1)/intervals;
    const double slope=(std::sqrt(hi)-std::sqrt(lo))/(hi-lo);
    return std::sqrt(lo)+slope*(bounded-lo);
}

Evaluation evaluate_layout(
    const Scenario& scenario,
    const std::vector<Point>& layout
) {
    Evaluation result;
    result.feasible=static_cast<int>(layout.size())==scenario.turbine_count;
    result.minimum_spacing_m=kInfinity;
    const double minimum=5.0*scenario.turbine.diameter_m;
    for (std::size_t left=0; left<layout.size(); ++left) {
        for (std::size_t right=left+1; right<layout.size(); ++right) {
            const double distance=std::sqrt(square_distance(layout[left],layout[right]));
            result.minimum_spacing_m=std::min(result.minimum_spacing_m,distance);
            if (distance<minimum-1.0e-5) result.feasible=false;
        }
    }
    const Site site=site_for(scenario.site);
    double free_expected_kw=0.0;
    double actual_expected_kw=0.0;
    double ti_expected_sum=0.0;
    for (const WindState& wind : site.wind) {
        const double free=free_power_kw(scenario.turbine,wind.speed_mps);
        free_expected_kw+=wind.probability*free*layout.size();
        for (std::size_t target=0; target<layout.size(); ++target) {
            double power=free;
            double ti_square=kAmbientTi*kAmbientTi;
            for (std::size_t source=0; source<layout.size(); ++source) {
                if (source==target) continue;
                const Interaction value=interaction(
                    scenario.turbine,layout[source],layout[target],wind
                );
                power-=value.power_loss_kw;
                ti_square+=value.ti_square;
            }
            actual_expected_kw+=wind.probability*std::max(0.0,power);
            ti_expected_sum+=wind.probability*piecewise_ti(
                ti_square,scenario.ti_intervals
            );
        }
    }
    result.annual_energy_mwh=kHoursPerYear*actual_expected_kw/1000.0;
    result.wake_loss_percent=free_expected_kw>0.0
        ?100.0*(free_expected_kw-actual_expected_kw)/free_expected_kw:0.0;
    for (const Point& point : layout) {
        const double depth=depth_m(scenario.site,point);
        result.support_cost_cny+=support_cost(depth);
        result.installation_cost_cny+=installation_cost(scenario,point);
        result.capital_cost_cny+=scenario.turbine.turbine_cost_cny
            +(kElectricalCnyPerKw+kOtherCnyPerKw)*scenario.turbine.rated_power_kw
            +support_cost(depth)+installation_cost(scenario,point);
    }
    result.work_fatigue=actual_expected_kw/
        (scenario.turbine.rated_power_kw*(1.0+kRepairCoefficient));
    result.disturbance_fatigue=ti_expected_sum/
        (kReferenceTi*(1.0+kRepairCoefficient));
    const double operation=scenario.turbine.opex_cny_per_kw_year
        *scenario.turbine.rated_power_kw*layout.size();
    const double preventive=kOperatingYears/kPreventiveIntervalYears
        *scenario.turbine.preventive_cny*layout.size();
    const double corrective=scenario.turbine.corrective_cny/kFaultFatigue
        *(result.work_fatigue+kDisturbanceCoefficient*result.disturbance_fatigue);
    result.operation_maintenance_cost_cny=operation+preventive+corrective;
    result.annual_cost_cny=capital_recovery_factor()*result.capital_cost_cny
        +result.operation_maintenance_cost_cny;
    if (result.annual_energy_mwh>0.0) {
        result.lcoe_cny_per_kwh=result.annual_cost_cny
            /(1000.0*result.annual_energy_mwh);
    } else {
        result.lcoe_cny_per_kwh=kInfinity;
        result.feasible=false;
    }
    return result;
}

double numerator(
    const Scenario& scenario,
    const Evaluation& evaluation
) {
    if (scenario.objective==ObjectiveKind::minimum_capital_lcoe) {
        return capital_recovery_factor()*evaluation.capital_cost_cny;
    }
    return evaluation.annual_cost_cny;
}

double score(const Scenario& scenario, const Evaluation& evaluation) {
    if (!evaluation.feasible) return kInfinity;
    if (scenario.objective==ObjectiveKind::maximum_aep) {
        return -evaluation.annual_energy_mwh;
    }
    if (scenario.objective==ObjectiveKind::minimum_annual_cost) {
        return evaluation.annual_cost_cny;
    }
    return numerator(scenario,evaluation)/(1000.0*evaluation.annual_energy_mwh);
}

std::vector<double> objective_coefficients(
    const Scenario& scenario,
    const LinearModel& model,
    const double q,
    const bool maximum_energy,
    const bool minimum_numerator
) {
    std::vector<double> values(model.annual_cost_coefficients.size(),0.0);
    const auto& cost=scenario.objective==ObjectiveKind::minimum_capital_lcoe
        ?model.capital_annualized_coefficients:model.annual_cost_coefficients;
    for (std::size_t index=0; index<values.size(); ++index) {
        if (maximum_energy) values[index]=-model.aep_kwh_coefficients[index];
        else if (minimum_numerator) values[index]=cost[index];
        else values[index]=cost[index]-q*model.aep_kwh_coefficients[index];
    }
    return values;
}

BoundReceipt initial_bounds(
    const Scenario& scenario,
    const SubproblemGeometry& geometry,
    const Coefficients& coefficients,
    const double time_limit
) {
    BoundReceipt result;
    LinearModel model=build_model(scenario,geometry,coefficients);
    SolveReceipt minimum=solve_model(
        model,objective_coefficients(scenario,model,0.0,false,true),time_limit
    );
    SolveReceipt maximum=solve_model(
        model,objective_coefficients(scenario,model,0.0,true,false),time_limit
    );
    result.mip_seconds=minimum.seconds+maximum.seconds;
    result.solves=2;
    if (!minimum.has_incumbent || !maximum.has_incumbent) return result;
    const Evaluation min_eval=evaluate_layout(
        scenario,layout_from_rows(geometry,minimum.selected_rows)
    );
    const Evaluation max_eval=evaluate_layout(
        scenario,layout_from_rows(geometry,maximum.selected_rows)
    );
    if (!min_eval.feasible || !max_eval.feasible) return result;
    const double max_energy_kwh=1000.0*max_eval.annual_energy_mwh;
    result.lower=numerator(scenario,min_eval)/max_energy_kwh;
    result.upper=numerator(scenario,max_eval)/max_energy_kwh;
    if (result.lower>result.upper) std::swap(result.lower,result.upper);
    result.feasible=true;
    return result;
}

SubproblemResult solve_subproblem(
    const Scenario& scenario,
    const int angle_index,
    const int pattern_index,
    const RunConfig& config,
    const std::optional<BoundReceipt>& supplied_bound
) {
    SubproblemResult result;
    result.angle_degrees=10*angle_index;
    result.pattern=pattern_index;
    const auto coefficient_start=Clock::now();
    const SubproblemGeometry geometry=build_geometry(scenario,angle_index,pattern_index);
    if (geometry.rows.empty() || geometry.points.empty()) {
        result.status="empty_geometry";
        return result;
    }
    const Coefficients coefficients=build_coefficients(scenario,geometry);
    LinearModel model=build_model(scenario,geometry,coefficients);
    result.coefficient_seconds=elapsed_seconds(coefficient_start);
    if (scenario.objective==ObjectiveKind::minimum_annual_cost
        || scenario.objective==ObjectiveKind::maximum_aep) {
        const bool maximize=scenario.objective==ObjectiveKind::maximum_aep;
        SolveReceipt solved=solve_model(
            model,
            objective_coefficients(scenario,model,0.0,maximize,!maximize),
            config.mip_time_limit_seconds
        );
        result.mip_seconds=solved.seconds;
        result.solves=1;
        result.status=solved.status;
        if (!solved.has_incumbent) return result;
        result.layout=layout_from_rows(geometry,solved.selected_rows);
        result.evaluation=evaluate_layout(scenario,result.layout);
        result.feasible=result.evaluation.feasible;
        return result;
    }
    const bool generated_bound_locally=!supplied_bound.has_value();
    BoundReceipt bound=supplied_bound.value_or(
        initial_bounds(scenario,geometry,coefficients,config.mip_time_limit_seconds)
    );
    // The shared bound stage is accounted once by run().  Only a bound that
    // this subproblem generated locally belongs in the subproblem receipt.
    if (generated_bound_locally) {
        result.mip_seconds+=bound.mip_seconds;
        result.solves+=bound.solves;
    }
    if (!bound.feasible) {
        result.status="infeasible_initial_bounds";
        return result;
    }
    double lower=bound.lower;
    double upper=bound.upper;
    Evaluation best;
    std::vector<Point> best_layout;
    double best_score=kInfinity;
    for (int iteration=1; iteration<=config.maximum_bda_iterations; ++iteration) {
        SolveReceipt lower_solved=solve_model(
            model,objective_coefficients(scenario,model,lower,false,false),
            config.mip_time_limit_seconds
        );
        SolveReceipt upper_solved=solve_model(
            model,objective_coefficients(scenario,model,upper,false,false),
            config.mip_time_limit_seconds
        );
        result.mip_seconds+=lower_solved.seconds+upper_solved.seconds;
        result.solves+=2;
        result.iterations=iteration;
        result.status=upper_solved.status;
        if (!lower_solved.has_incumbent || !upper_solved.has_incumbent) break;
        const std::vector<Point> lower_layout=
            layout_from_rows(geometry,lower_solved.selected_rows);
        const std::vector<Point> upper_layout=
            layout_from_rows(geometry,upper_solved.selected_rows);
        const Evaluation lower_evaluation=evaluate_layout(scenario,lower_layout);
        const Evaluation upper_evaluation=evaluate_layout(scenario,upper_layout);
        if (!lower_evaluation.feasible || !upper_evaluation.feasible) break;
        const auto ratio_of=[&](const Evaluation& evaluation) {
            return numerator(scenario,evaluation)
                /(1000.0*evaluation.annual_energy_mwh);
        };
        const double lower_ratio=ratio_of(lower_evaluation);
        const double upper_ratio=ratio_of(upper_evaluation);
        for (const auto& candidate : std::array{
                 std::pair{lower_ratio,&lower_evaluation},
                 std::pair{upper_ratio,&upper_evaluation}}) {
            if (candidate.first<best_score) {
                best_score=candidate.first;
                best=*candidate.second;
                best_layout=candidate.second==&lower_evaluation
                    ?lower_layout:upper_layout;
            }
        }
        const double f_lower=numerator(scenario,lower_evaluation)
            -lower*1000.0*lower_evaluation.annual_energy_mwh;
        const double f_upper=numerator(scenario,upper_evaluation)
            -upper*1000.0*upper_evaluation.annual_energy_mwh;
        const double next_upper=std::min(upper,upper_ratio);
        if (next_upper-lower<=config.bda_tolerance
            || std::abs(f_upper)<=config.bda_tolerance
                *std::max(1.0,numerator(scenario,upper_evaluation))) {
            upper=next_upper;
            break;
        }
        const double denominator=f_upper-f_lower;
        double next_lower=0.5*(lower+next_upper);
        if (std::abs(denominator)>1.0e-15) {
            next_lower=lower-f_lower*(upper-lower)/denominator;
        }
        lower=std::clamp(next_lower,lower,next_upper);
        upper=next_upper;
    }
    if (best_layout.empty()) return result;
    result.feasible=true;
    result.evaluation=best;
    result.layout=std::move(best_layout);
    return result;
}

std::uint64_t hash_result(const RunResult& result) {
    std::uint64_t hash=1469598103934665603ULL;
    auto mix=[&](const std::uint64_t value) {
        hash^=value;
        hash*=1099511628211ULL;
    };
    mix(static_cast<std::uint64_t>(result.selected_angle_degrees));
    mix(static_cast<std::uint64_t>(result.selected_pattern));
    mix(std::bit_cast<std::uint64_t>(result.evaluation.lcoe_cny_per_kwh));
    mix(std::bit_cast<std::uint64_t>(result.evaluation.annual_energy_mwh));
    for (const Point& point : result.layout) {
        mix(std::bit_cast<std::uint64_t>(point.x_m));
        mix(std::bit_cast<std::uint64_t>(point.y_m));
    }
    return hash;
}

}  // namespace

std::vector<Scenario> paper_scenarios() {
    std::vector<Scenario> result;
    const Turbine first=wt1();
    const Turbine second=wt2();
    const Turbine third=wt3();
    result.push_back({"Y16_case1_type1_n40_g2p5_imm_lcoe_i3",SiteKind::zhuhai_type1,
        ModelKind::imm,ObjectiveKind::minimum_lcoe,first,40,2.5,3,false,"Table IV Type1"});
    result.push_back({"Y16_case1_type2_n40_g2p5_imm_lcoe_i3",SiteKind::zhuhai_type2,
        ModelKind::imm,ObjectiveKind::minimum_lcoe,first,40,2.5,3,false,"Table IV Type2"});
    result.push_back({"Y16_case1_type3_n40_g2p5_imm_lcoe_i3",SiteKind::zhuhai_type3,
        ModelKind::imm,ObjectiveKind::minimum_lcoe,first,40,2.5,3,false,"Table IV Type3"});
    result.push_back({"Y16_case1_type3_n40_g2p5_imm_ac_i3",SiteKind::zhuhai_type3,
        ModelKind::imm,ObjectiveKind::minimum_annual_cost,first,40,2.5,3,false,"Table V min AC"});
    result.push_back({"Y16_case1_type3_n40_g2p5_imm_aep_i3",SiteKind::zhuhai_type3,
        ModelKind::imm,ObjectiveKind::maximum_aep,first,40,2.5,3,false,"Table V max AEP"});
    for (int intervals=4; intervals<=6; ++intervals) {
        result.push_back({
            "Y16_case1_type3_n40_g2p5_imm_lcoe_i"+std::to_string(intervals),
            SiteKind::zhuhai_type3,ModelKind::imm,ObjectiveKind::minimum_lcoe,
            first,40,2.5,intervals,false,"Table VII TI sensitivity"
        });
    }
    for (const ModelKind model : {ModelKind::bmm,ModelKind::imm}) {
        for (const int count : {30,40,50}) {
            for (const double grid : {3.0,2.5,2.0}) {
                const bool expected=(model==ModelKind::bmm && grid==2.0)
                    || (count==50 && grid==3.0);
                std::string grid_id=grid==3.0?"g3":(grid==2.5?"g2p5":"g2");
                const std::string model_id=model==ModelKind::bmm?"bmm":"imm";
                result.push_back({
                    "Y16_scaling_"+model_id+"_n"+std::to_string(count)+"_"+grid_id,
                    SiteKind::zhuhai_type3,model,ObjectiveKind::minimum_lcoe,
                    first,count,grid,3,expected,"Table VIII BMM/IMM"
                });
            }
        }
    }
    for (const double grid : {5.0,4.0,3.5}) {
        std::string grid_id=grid==5.0?"g5":(grid==4.0?"g4":"g3p5");
        result.push_back({
            "Y16_large_imm_n100_"+grid_id,
            SiteKind::zhuhai_type3,ModelKind::imm,ObjectiveKind::minimum_lcoe,
            second,100,grid,3,false,"Table IX 100 WT-2"
        });
    }
    result.push_back({"Y16_hainan_n47_imm_capital_lcoe_i3",SiteKind::hainan,
        ModelKind::imm,ObjectiveKind::minimum_capital_lcoe,third,47,2.5,3,false,
        "Table X TI-agnostic capital LCOE"});
    result.push_back({"Y16_hainan_n47_imm_full_lcoe_i3",SiteKind::hainan,
        ModelKind::imm,ObjectiveKind::minimum_lcoe,third,47,2.5,3,false,
        "Table X full LCOE"});
    return result;
}

RunResult run(const Scenario& scenario, const RunConfig& config) {
    if (config.workers<1 || config.angle_start<0 || config.angle_count<1
        || config.angle_start+config.angle_count>18
        || config.pattern_start<0 || config.pattern_count<1
        || config.pattern_start+config.pattern_count>10
        || config.maximum_bda_iterations<1 || !(config.bda_tolerance>0.0)
        || !(config.mip_time_limit_seconds>0.0)
        || scenario.ti_intervals<1 || scenario.ti_intervals>12) {
        throw std::invalid_argument("invalid Y16 configuration");
    }
    const auto started=Clock::now();
    const int tasks=config.angle_count*config.pattern_count;
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    std::vector<BoundReceipt> bounds(static_cast<std::size_t>(tasks));
    std::vector<double> bound_coeff_seconds(static_cast<std::size_t>(tasks),0.0);
    std::vector<std::string> errors(static_cast<std::size_t>(tasks));
    const bool ratio=scenario.objective==ObjectiveKind::minimum_lcoe
        || scenario.objective==ObjectiveKind::minimum_capital_lcoe;
    if (ratio) {
        executor.parallel_for(0,tasks,[&](const int task) {
            try {
                const int angle=config.angle_start+task/config.pattern_count;
                const int pattern=config.pattern_start+task%config.pattern_count;
                const auto begin=Clock::now();
                const SubproblemGeometry geometry=build_geometry(scenario,angle,pattern);
                if (geometry.rows.empty() || geometry.points.empty()) return;
                const Coefficients coefficients=build_coefficients(scenario,geometry);
                bound_coeff_seconds[static_cast<std::size_t>(task)]=elapsed_seconds(begin);
                bounds[static_cast<std::size_t>(task)]=initial_bounds(
                    scenario,geometry,coefficients,config.mip_time_limit_seconds
                );
            } catch (const std::exception& error) {
                errors[static_cast<std::size_t>(task)]=error.what();
            }
        });
    }
    for (const std::string& error : errors) {
        if (!error.empty()) throw std::runtime_error("Y16 bound stage: "+error);
    }
    double global_upper=kInfinity;
    if (ratio) {
        for (const BoundReceipt& bound : bounds) {
            if (bound.feasible) global_upper=std::min(global_upper,bound.upper);
        }
    }
    std::vector<SubproblemResult> subresults(static_cast<std::size_t>(tasks));
    std::fill(errors.begin(),errors.end(),std::string{});
    executor.parallel_for(0,tasks,[&](const int task) {
        try {
            const int angle=config.angle_start+task/config.pattern_count;
            const int pattern=config.pattern_start+task%config.pattern_count;
            if (ratio && (!bounds[static_cast<std::size_t>(task)].feasible
                || bounds[static_cast<std::size_t>(task)].lower>global_upper)) {
                subresults[static_cast<std::size_t>(task)].status="pruned_by_bda_bounds";
                return;
            }
            subresults[static_cast<std::size_t>(task)]=solve_subproblem(
                scenario,angle,pattern,config,
                ratio?std::optional<BoundReceipt>(bounds[static_cast<std::size_t>(task)])
                     :std::nullopt
            );
        } catch (const std::exception& error) {
            errors[static_cast<std::size_t>(task)]=error.what();
        }
    });
    Highs::resetGlobalScheduler(true);
    for (const std::string& error : errors) {
        if (!error.empty()) throw std::runtime_error("Y16 solve stage: "+error);
    }
    RunResult result;
    result.case_id=scenario.case_id;
    result.requested_workers=config.workers;
    result.generated_subproblems=tasks;
    const auto receipt=executor.work_receipt();
    result.observed_workers=receipt.distinct_participants;
    double best=kInfinity;
    for (int task=0; task<tasks; ++task) {
        const BoundReceipt& bound=bounds[static_cast<std::size_t>(task)];
        if (bound.feasible) ++result.bound_feasible_subproblems;
        result.coefficient_seconds+=bound_coeff_seconds[static_cast<std::size_t>(task)];
        result.mip_seconds+=bound.mip_seconds;
        if (subresults[static_cast<std::size_t>(task)].status=="pruned_by_bda_bounds") {
            ++result.pruned_subproblems;
            continue;
        }
        const SubproblemResult& sub=subresults[static_cast<std::size_t>(task)];
        if (result.first_subproblem_status.empty() && !sub.status.empty()) {
            result.first_subproblem_status=sub.status;
        }
        result.coefficient_seconds+=sub.coefficient_seconds;
        result.mip_seconds+=sub.mip_seconds;
        result.bda_iterations+=sub.iterations;
        if (!sub.status.empty()) ++result.solved_subproblems;
        if (!sub.layout.empty()) ++result.incumbent_subproblems;
        if (!sub.layout.empty() && !sub.feasible) {
            ++result.evaluator_rejected_subproblems;
        }
        if (!sub.feasible) continue;
        ++result.feasible_subproblems;
        const double value=score(scenario,sub.evaluation);
        if (value<best) {
            best=value;
            result.status=sub.status;
            result.selected_angle_degrees=sub.angle_degrees;
            result.selected_pattern=sub.pattern;
            result.evaluation=sub.evaluation;
            result.layout=sub.layout;
        }
    }
    if (result.layout.empty()) {
        result.status=scenario.expected_paper_infeasible
            ?"paper_expected_infeasible":"no_feasible_incumbent";
    } else {
        result.scientific_hash=hash_result(result);
    }
    result.end_to_end_seconds=elapsed_seconds(started);
    return result;
}

}  // namespace core99::y16
