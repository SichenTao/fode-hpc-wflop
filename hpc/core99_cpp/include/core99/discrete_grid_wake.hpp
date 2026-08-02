/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: shared discrete-grid Jensen-overlap wake evaluator
Paper title: not_applicable_shared_infrastructure
Public asset: project-native clean-room implementation.
Missing/completion: paper-specific assumptions are supplied by versioned
paper contracts; this unit adds no paper defaults.
Reconstruction: not applicable shared infrastructure.
Semantic IDs: not_applicable_shared_infrastructure.
Contract: each consuming paper contract freezes all configuration fields.
Claim boundary: reusable mathematical kernel, not a paper reproduction by
itself.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <string>
#include <vector>

namespace core99::gridwake {

using Layout = std::vector<int>;

struct WindState {
    double from_degrees = 0.0;
    double speed_mps = 0.0;
    double probability = 0.0;
};

struct Turbine {
    std::string name;
    double rotor_diameter_m = 0.0;
    double hub_height_m = 0.0;
    double thrust_coefficient = 0.0;
    double wake_expansion = 0.0;
    double cut_in_mps = 0.0;
    double rated_mps = 0.0;
    double cut_out_mps = 0.0;
    double rated_power_kw = 0.0;
};

struct Configuration {
    int rows = 0;
    int columns = 0;
    int turbine_count = 0;
    double cell_width_m = 0.0;
    Turbine turbine;
    std::vector<WindState> wind_states;
};

struct Evaluation {
    double expected_power_kw = 0.0;
    double ideal_power_kw = 0.0;
    double conversion_efficiency_percent = 0.0;
    double cost_of_energy = 0.0;
    std::vector<double> turbine_power_kw;
    bool feasible = false;
};

class Problem {
public:
    explicit Problem(Configuration configuration);

    [[nodiscard]] const Configuration& configuration() const noexcept;
    [[nodiscard]] int candidate_count() const noexcept;
    [[nodiscard]] bool feasible(const Layout& layout) const noexcept;
    [[nodiscard]] Evaluation evaluate(const Layout& layout) const;

private:
    Configuration configuration_;
};

[[nodiscard]] double turbine_cost(int turbine_count);

}  // namespace core99::gridwake
