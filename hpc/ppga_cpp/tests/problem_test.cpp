/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: PPGA Nantong-structured declared 3D scalar and complete-layout oracle test
Problem semantic ID: ppga_nantong_structured_3d_declared_proxy_v1
Evidence tier: P3_DECLARED_PROXY
Controlling oracle: shared/contracts/ppga_nantong_structured_3d_declared_proxy_oracle.json
Claim boundary: tests the frozen proxy numerics only; no original Nantong result claim
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "ppga/problem.hpp"

#include <cmath>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void near(double actual, double expected, double tolerance,
          const std::string& label) {
    if (std::abs(actual - expected) > tolerance) {
        std::ostringstream details;
        details << std::setprecision(17) << label << " actual=" << actual
                << " expected=" << expected;
        throw std::runtime_error(details.str());
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("usage: problem_test CASE_MANIFEST");
        }
        const ppga::Problem problem = ppga::load_problem(
            argv[1], "PPGA_NantongStructured_WS1_tn20"
        );
        require(problem.rows == 16 && problem.cols == 27, "grid shape");
        require(problem.turbine_count == 20, "turbine count");
        require(problem.wind_directions_rad.size() == 16, "directions");
        require(problem.wind_speeds_mps.size() == 7, "speeds");
        near(ppga::foundation_elevation_m(problem, 1), 6.0, 1.0e-13,
             "terrain maximum");
        near(ppga::foundation_elevation_m(problem, 419), 0.0, 1.0e-13,
             "terrain minimum");
        near(ppga::foundation_elevation_m(problem, 432), 3.0, 1.0e-13,
             "terrain corner");
        near(ppga::turbine_power_kw(1.0), 0.0, 0.0, "below cut-in");
        near(ppga::turbine_power_kw(3.0), 0.0, 0.0, "cut-in");
        near(ppga::turbine_power_kw(7.0),
             6200.0 * (343.0 - 27.0) / (1331.0 - 27.0),
             1.0e-12, "cubic power");
        near(ppga::turbine_power_kw(11.0), 6200.0, 0.0, "rated");
        near(ppga::turbine_power_kw(26.0), 0.0, 0.0, "above cut-out");
        near(ppga::single_wake_deficit_fraction(0.0, 0.0, 0.0),
             0.0, 0.0, "no upstream wake");
        near(ppga::single_wake_deficit_fraction(300.0, 0.0, 0.0),
             0.48251280908888394, 1.0e-15, "centerline wake");

        const std::vector<int> layout = {
            1, 14, 27, 55, 82, 109, 122, 135, 163, 217,
            230, 243, 298, 311, 324, 352, 379, 406, 419, 432
        };
        const ppga::LayoutEvaluation evaluation =
            ppga::evaluate_layout(problem, layout);
        near(evaluation.expected_power_kw, 37839.685912350418, 1.0e-9,
             "oracle expected power");
        near(evaluation.ideal_expected_power_kw, 42154.187661866963, 1.0e-9,
             "oracle ideal power");
        near(evaluation.conversion_efficiency, 0.89764951031378837, 1.0e-14,
             "oracle efficiency");
        near(evaluation.cost_per_expected_power, 0.00044020372839576722,
             1.0e-17, "oracle cost per power");
        const std::string semantic_hash = ppga::problem_semantic_hash(problem);
        require(
            semantic_hash == "ee06013d8778fd7e",
            "problem semantic hash actual=" + semantic_hash
        );
        const auto multiplicative = ppga::evaluate_layout(
            problem,
            layout,
            ppga::WakeCombination::multiplicative
        );
        const std::string multiplicative_hash =
            ppga::problem_semantic_hash(
                problem,
                ppga::WakeCombination::multiplicative
            );
        require(
            multiplicative.expected_power_kw
                != evaluation.expected_power_kw,
            "wake sensitivity score did not change"
        );
        require(
            multiplicative_hash != semantic_hash,
            "wake sensitivity semantic hash did not change"
        );
        std::cout << "PPGA problem oracle passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
