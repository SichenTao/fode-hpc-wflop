/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: GeoGA Anholt-structured declared scalar and complete-layout oracle test
Paper title: A Geometric Mutation-Based Genetic Algorithm for Irregular Large-Scale Offshore Wind Farm Layout Optimization
DOI: 10.1109/CBD69312.2025.00059
Public asset/source: no author implementation or numerical Anholt data found; evidence dossier docs/source-dossiers/L0726.json
Missing information: original Anholt numerical arrays and author oracle
Reconstruction: verifies the frozen geoga_anholt_structured_declared_proxy_v1 oracle only
Problem semantic ID: geoga_anholt_structured_declared_proxy_v1
Evidence tier: P3_DECLARED_PROXY
Controlling oracle: shared/contracts/geoga_anholt_structured_declared_proxy_oracle.json
Claim boundary: tests only frozen proxy numerics and never compares with the unavailable actual Anholt layout
Last evidence-audit date: 2026-07-30
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "geoga/problem.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

double maximum_absolute_error = 0.0;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void near(
    double actual,
    double expected,
    double tolerance,
    const std::string& label
) {
    const double absolute_error = std::abs(actual - expected);
    maximum_absolute_error = std::max(
        maximum_absolute_error, absolute_error
    );
    if (absolute_error > tolerance) {
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
        const geoga::Problem problem = geoga::load_problem(argv[1]);
        require(problem.turbine_count == 111, "turbine count");
        require(problem.candidates.size() == 180, "candidate count");
        require(problem.wind_bins.size() == 12, "wind-bin count");
        for (const geoga::Point& point : problem.candidates) {
            require(
                geoga::point_in_boundary(problem, point),
                "candidate outside polygon"
            );
        }
        near(
            geoga::minimum_candidate_spacing_m(problem),
            586.12214158944721,
            1.0e-10,
            "Poisson minimum spacing"
        );
        const double probability_sum = std::accumulate(
            problem.wind_bins.begin(),
            problem.wind_bins.end(),
            0.0,
            [](double sum, const geoga::WindBin& bin) {
                return sum + bin.probability;
            }
        );
        near(probability_sum, 1.0, 1.0e-15, "probability sum");
        near(geoga::turbine_power_kw(problem, 2.99), 0.0, 0.0,
             "below cut-in");
        near(
            geoga::turbine_power_kw(problem, 7.0),
            4200.0 * (343.0 - 27.0) / (1728.0 - 27.0),
            1.0e-12,
            "cubic power"
        );
        near(geoga::turbine_power_kw(problem, 12.0), 4200.0, 0.0,
             "rated power");
        near(geoga::turbine_power_kw(problem, 25.01), 0.0, 0.0,
             "above cut-out");
        near(
            geoga::single_wake_deficit_fraction(problem, 585.0, 0.0),
            0.24568284644446317,
            1.0e-15,
            "centerline deficit"
        );
        near(
            geoga::single_wake_deficit_fraction(problem, 585.0, 100.0),
            0.0,
            0.0,
            "outside wake"
        );

        std::vector<int> layout(111);
        std::iota(layout.begin(), layout.end(), 0);
        const geoga::LayoutEvaluation evaluation =
            geoga::evaluate_layout(problem, layout);
        near(
            evaluation.aep_kwh,
            1845129182.7933457,
            1.0e-6,
            "complete-layout AEP"
        );
        near(
            evaluation.no_wake_aep_kwh,
            2395029927.7066669,
            1.0e-6,
            "complete-layout no-wake AEP"
        );
        near(
            evaluation.capacity_factor,
            0.45180434416641341,
            1.0e-15,
            "complete-layout capacity factor"
        );
        require(
            geoga::problem_semantic_hash(problem) == "42a7899a17237389",
            "full problem semantic hash"
        );
        std::cout << "{\"status\":\"pass\","
                  << "\"maximum_absolute_error\":"
                  << std::setprecision(17) << maximum_absolute_error
                  << ",\"tolerance_policy\":\"per-field absolute\"}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
