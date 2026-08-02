/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: Y16 pure-C++/HiGHS CPU-HPC command line
Paper DOI: 10.1109/TSTE.2026.3686029
First-party patent: CN121683298A/CN121683298B
Public asset, missing information, conflicts, corrections, reconstruction,
semantic IDs, backend, controlling contract and claim boundary:
include/core99/huang_y16.hpp
Claim boundary: flexible academic reconstruction, not author code, private
site/wind/terrain arrays, Gurobi or numeric replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/huang_y16.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

struct Arguments {
    std::string action="optimize";
    std::string case_id="Y16_case1_type3_n40_g2p5_imm_lcoe_i3";
    core99::y16::RunConfig config;
    std::filesystem::path output;
};

Arguments parse(const int argc, char** argv) {
    Arguments result;
    for (int index=1; index<argc; ++index) {
        const std::string key=argv[index];
        auto value=[&]() {
            if (++index>=argc) throw std::invalid_argument("Y16 missing "+key);
            return std::string(argv[index]);
        };
        if (key=="--action") result.action=value();
        else if (key=="--case") result.case_id=value();
        else if (key=="--workers") result.config.workers=std::stoi(value());
        else if (key=="--angle-start") result.config.angle_start=std::stoi(value());
        else if (key=="--angle-count") result.config.angle_count=std::stoi(value());
        else if (key=="--pattern-start") result.config.pattern_start=std::stoi(value());
        else if (key=="--pattern-count") result.config.pattern_count=std::stoi(value());
        else if (key=="--maximum-bda-iterations") {
            result.config.maximum_bda_iterations=std::stoi(value());
        } else if (key=="--bda-tolerance") {
            result.config.bda_tolerance=std::stod(value());
        } else if (key=="--mip-time-limit-seconds") {
            result.config.mip_time_limit_seconds=std::stod(value());
        } else if (key=="--output") result.output=value();
        else throw std::invalid_argument("Y16 unknown option "+key);
    }
    return result;
}

const core99::y16::Scenario& scenario(const std::string& id) {
    static const auto scenarios=core99::y16::paper_scenarios();
    for (const auto& item : scenarios) if (item.case_id==id) return item;
    throw std::invalid_argument("unknown Y16 case "+id);
}

std::string site_name(const core99::y16::SiteKind value) {
    switch (value) {
        case core99::y16::SiteKind::zhuhai_type1: return "zhuhai_type1";
        case core99::y16::SiteKind::zhuhai_type2: return "zhuhai_type2";
        case core99::y16::SiteKind::zhuhai_type3: return "zhuhai_type3";
        case core99::y16::SiteKind::hainan: return "hainan";
    }
    throw std::logic_error("Y16 site enum");
}

std::string model_name(const core99::y16::ModelKind value) {
    return value==core99::y16::ModelKind::bmm?"bmm":"imm";
}

std::string objective_name(const core99::y16::ObjectiveKind value) {
    switch (value) {
        case core99::y16::ObjectiveKind::minimum_lcoe: return "minimum_lcoe";
        case core99::y16::ObjectiveKind::minimum_annual_cost: return "minimum_annual_cost";
        case core99::y16::ObjectiveKind::maximum_aep: return "maximum_aep";
        case core99::y16::ObjectiveKind::minimum_capital_lcoe: return "minimum_capital_lcoe";
    }
    throw std::logic_error("Y16 objective enum");
}

std::string metadata_json(const core99::y16::Scenario& item) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"case_id\":\"" << item.case_id << "\""
        << ",\"site\":\"" << site_name(item.site) << "\""
        << ",\"model\":\"" << model_name(item.model) << "\""
        << ",\"objective\":\"" << objective_name(item.objective) << "\""
        << ",\"turbine\":\"" << item.turbine.id << "\""
        << ",\"rotor_diameter_m\":" << item.turbine.diameter_m
        << ",\"turbine_count\":" << item.turbine_count
        << ",\"grid_spacing_diameters\":" << item.grid_spacing_diameters
        << ",\"ti_intervals\":" << item.ti_intervals
        << ",\"expected_paper_infeasible\":"
        << (item.expected_paper_infeasible?"true":"false")
        << ",\"paper_table_role\":\"" << item.paper_table_role << "\"}";
    return out.str();
}

std::string evaluation_json(const core99::y16::Evaluation& value) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"lcoe_cny_per_kwh\":" << value.lcoe_cny_per_kwh
        << ",\"annual_cost_cny\":" << value.annual_cost_cny
        << ",\"capital_cost_cny\":" << value.capital_cost_cny
        << ",\"annual_energy_mwh\":" << value.annual_energy_mwh
        << ",\"wake_loss_percent\":" << value.wake_loss_percent
        << ",\"support_cost_cny\":" << value.support_cost_cny
        << ",\"installation_cost_cny\":" << value.installation_cost_cny
        << ",\"operation_maintenance_cost_cny\":"
        << value.operation_maintenance_cost_cny
        << ",\"work_fatigue\":" << value.work_fatigue
        << ",\"disturbance_fatigue\":" << value.disturbance_fatigue
        << ",\"minimum_spacing_m\":" << value.minimum_spacing_m
        << ",\"feasible\":" << (value.feasible?"true":"false") << '}';
    return out.str();
}

std::string layout_json(const std::vector<core99::y16::Point>& layout) {
    std::ostringstream out;
    out << std::setprecision(17) << '[';
    for (std::size_t index=0; index<layout.size(); ++index) {
        if (index) out << ',';
        out << '[' << layout[index].x_m << ',' << layout[index].y_m << ']';
    }
    return out.str()+"]";
}

std::string result_json(
    const core99::y16::Scenario& scenario,
    const core99::y16::RunResult& result
) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"metadata\":" << metadata_json(scenario)
        << ",\"method_semantic_id\":\"" << result.method_semantic_id << "\""
        << ",\"problem_semantic_id\":\"" << result.problem_semantic_id << "\""
        << ",\"protocol_semantic_id\":\"" << result.protocol_semantic_id << "\""
        << ",\"status\":\"" << result.status << "\""
        << ",\"first_subproblem_status\":\""
        << result.first_subproblem_status << "\""
        << ",\"requested_workers\":" << result.requested_workers
        << ",\"observed_workers\":" << result.observed_workers
        << ",\"generated_subproblems\":" << result.generated_subproblems
        << ",\"bound_feasible_subproblems\":"
        << result.bound_feasible_subproblems
        << ",\"feasible_subproblems\":" << result.feasible_subproblems
        << ",\"solved_subproblems\":" << result.solved_subproblems
        << ",\"incumbent_subproblems\":" << result.incumbent_subproblems
        << ",\"evaluator_rejected_subproblems\":"
        << result.evaluator_rejected_subproblems
        << ",\"pruned_subproblems\":" << result.pruned_subproblems
        << ",\"bda_iterations\":" << result.bda_iterations
        << ",\"selected_angle_degrees\":" << result.selected_angle_degrees
        << ",\"selected_pattern\":" << result.selected_pattern
        << ",\"coefficient_seconds\":" << result.coefficient_seconds
        << ",\"mip_seconds\":" << result.mip_seconds
        << ",\"end_to_end_seconds\":" << result.end_to_end_seconds
        << ",\"scientific_hash\":" << result.scientific_hash
        << ",\"evaluation\":" << evaluation_json(result.evaluation)
        << ",\"layout\":" << layout_json(result.layout) << '}';
    return out.str();
}

void emit(const std::string& text, const std::filesystem::path& output) {
    if (output.empty()) {
        std::cout << text << '\n';
        return;
    }
    std::filesystem::create_directories(output.parent_path());
    const auto temporary=output.string()+".tmp";
    std::ofstream stream(temporary);
    if (!stream) throw std::runtime_error("cannot open Y16 output");
    stream << text << '\n';
    stream.close();
    std::filesystem::rename(temporary,output);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments=parse(argc,argv);
        if (arguments.action=="list-cases") {
            std::ostringstream out;
            out << '[';
            const auto cases=core99::y16::paper_scenarios();
            for (std::size_t index=0; index<cases.size(); ++index) {
                if (index) out << ',';
                out << metadata_json(cases[index]);
            }
            emit(out.str()+"]",arguments.output);
            return 0;
        }
        const auto& item=scenario(arguments.case_id);
        if (arguments.action=="metadata") {
            emit(metadata_json(item),arguments.output);
        } else if (arguments.action=="optimize") {
            emit(result_json(item,core99::y16::run(item,arguments.config)),arguments.output);
        } else {
            throw std::invalid_argument("unknown Y16 action "+arguments.action);
        }
    } catch (const std::exception& error) {
        std::cerr << "Y16 error: " << error.what() << '\n';
        return 2;
    }
    return 0;
}
