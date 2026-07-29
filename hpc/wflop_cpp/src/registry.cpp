#include "wflop/algorithms.hpp"

#include <algorithm>
#include <stdexcept>

namespace wflop {

const std::vector<ProblemDescriptor>& problem_descriptors() {
    static const std::vector<ProblemDescriptor> descriptors{
        {
            "fode_e0_common",
            "FODE-E0-L common 50-case benchmark",
            "fode_wflop_e0_legacy_v1",
            "maximize expected complete-farm power under the frozen "
            "Jensen/Park discrete-grid model"
        },
        {
            "bde2025_source_replay_ws1_ws4",
            "BDE official-source WS1-WS4 replay problem",
            "bde2025_source_replay_ws1_ws4_v1",
            "maximize expected complete-farm power on the official-source "
            "standard and Daegwallyeong discrete grids"
        },
        {
            "rpso2024_source_problem_ws1_ws4",
            "RPSO official-source WS1-WS4 problem",
            "rpso2024_source_problem_ws1_ws4_v1",
            "maximize expected complete-farm power on the official-source "
            "21 by 21 standard grid"
        }
    };
    return descriptors;
}

const ProblemDescriptor& problem_descriptor(const std::string& id) {
    const auto& descriptors = problem_descriptors();
    const auto found = std::find_if(
        descriptors.begin(),
        descriptors.end(),
        [&](const ProblemDescriptor& descriptor) {
            return descriptor.id == id;
        }
    );
    if (found == descriptors.end()) {
        throw std::invalid_argument("unknown problem: " + id);
    }
    return *found;
}

const std::vector<AlgorithmDescriptor>& algorithm_descriptors() {
    static const std::vector<AlgorithmDescriptor> descriptors{
        {
            "fode",
            "FODE",
            "10.3390/math13020282",
            "archived_matlab_source",
            "fode_e0_physical_fes",
            {"fode_e0_common"}
        },
        {
            "aga",
            "AGA",
            "10.1016/j.apenergy.2019.04.084",
            "paper_first_archived_matlab_completed",
            "aga_paper_first_e0_physical_fes_v1",
            {"fode_e0_common"}
        },
        {
            "sugga",
            "SUGGA",
            "10.1016/j.enconman.2019.06.082",
            "archived_matlab_source_and_frozen_surrogate",
            "sugga_frozen_surrogate_e0_physical_fes_v1",
            {"fode_e0_common"}
        },
        {
            "ise",
            "ISE",
            "10.1016/j.engappai.2023.106198",
            "paper_derived",
            "ise_paper_derived_e0_physical_fes_v1",
            {"fode_e0_common"}
        },
        {
            "agpso",
            "AGPSO",
            "10.1016/j.enconman.2022.116174",
            "paper_first_source_completed",
            "agpso_paper_staged_parallel_e0_physical_fes_v1",
            {"fode_e0_common"}
        },
        {
            "cgpso",
            "CGPSO",
            "10.1109/jas.2023.123387",
            "paper_first_source_completed",
            "cgpso_paper_staged_parallel_e0_physical_fes_v1",
            {"fode_e0_common", "rpso2024_source_problem_ws1_ws4"}
        },
        {
            "lshade",
            "LSHADE",
            "10.1109/CEC.2014.6900380",
            "paper_first_archived_matlab_completed",
            "lshade_paper_first_e0_physical_fes_v1",
            {"fode_e0_common"}
        },
        {
            "clshade",
            "CLSHADE",
            "10.1016/j.asoc.2023.110306",
            "paper_derived",
            "clshade_paper_derived_e0_physical_fes_v1",
            {"fode_e0_common"}
        },
        {
            "cede",
            "CEDE",
            "10.3390/math12233762",
            "paper_first_local_source_discrepancy_registered",
            "cede_paper_equations_physical_fes_v1",
            {"fode_e0_common"}
        },
        {
            "msshade",
            "MS-SHADE",
            "10.3390/electronics13163196",
            "paper_first_local_source_discrepancy_registered",
            "msshade_paper_equations_physical_fes_v1",
            {"fode_e0_common"}
        },
        {
            "bde",
            "BDE",
            "10.1016/j.energy.2025.137885",
            "paper_first_official_source_discrepancy_registered",
            "bde_paper_equations_physical_fes_v1",
            {"fode_e0_common", "bde2025_source_replay_ws1_ws4"}
        },
        {
            "hgpso",
            "HGPSO",
            "10.26599/tst.2026.9010059",
            "paper_first_local_source_discrepancy_registered",
            "hgpso_paper_equations_physical_fes_v1",
            {"fode_e0_common"}
        },
        {
            "aiga",
            "AIGA",
            "10.1007/s42235-024-00498-3",
            "paper_derived_explicit_reconstruction",
            "aiga_paper_derived_e0_physical_fes_v1",
            {"fode_e0_common"}
        },
        {
            "ciga",
            "CIGA",
            "10.1145/3766671.3766786",
            "paper_derived_explicit_reconstruction",
            "ciga_paper_derived_e0_physical_fes_v1",
            {"fode_e0_common"}
        },
        {
            "lsde",
            "LSDE",
            "10.1049/cit2.70150",
            "paper_derived_explicit_reconstruction",
            "lsde_paper_derived_e0_physical_fes_v1",
            {"fode_e0_common"}
        },
        {
            "wfadde",
            "WFADDE",
            "10.2139/ssrn.6135326",
            "preprint_derived_explicit_reconstruction",
            "wfadde_preprint_derived_e0_physical_fes_v1",
            {"fode_e0_common"}
        },
        {
            "alshade",
            "A-LSHADE",
            "10.1109/pic62406.2024.10892732",
            "paper_derived_explicit_reconstruction",
            "alshade_paper_derived_e0_physical_fes_v1",
            {"fode_e0_common"}
        },
        {
            "ppga",
            "PPGA",
            "10.1109/jas.2025.125351",
            "paper_derived_declared_problem_transfer",
            "ppga_declared_reconstruction_fode_e0_v1",
            {"fode_e0_common"}
        },
        {
            "rlpso_compact_policy_declared_reconstruction_v1",
            "RPSO-derived compact policy engineering proxy",
            "10.1016/j.energy.2024.134050",
            "paper_guided_declared_compact_policy_engineering_proxy",
            "rlpso_compact_policy_declared_reconstruction_v1",
            {"rpso2024_source_problem_ws1_ws4"}
        }
    };
    return descriptors;
}

const AlgorithmDescriptor& algorithm_descriptor(const std::string& id) {
    const auto& descriptors = algorithm_descriptors();
    const auto found = std::find_if(
        descriptors.begin(),
        descriptors.end(),
        [&](const AlgorithmDescriptor& descriptor) {
            return descriptor.id == id;
        }
    );
    if (found == descriptors.end()) {
        throw std::invalid_argument("unknown algorithm: " + id);
    }
    return *found;
}

const std::vector<std::string>& algorithm_ids() {
    static const std::vector<std::string> ids = [] {
        std::vector<std::string> result;
        result.reserve(algorithm_descriptors().size());
        for (const auto& descriptor : algorithm_descriptors()) {
            result.push_back(descriptor.id);
        }
        return result;
    }();
    return ids;
}

bool algorithm_supports_problem(
    const std::string& algorithm_id,
    const std::string& problem_id
) {
    const auto& descriptor = algorithm_descriptor(algorithm_id);
    return std::find(
        descriptor.compatible_problem_ids.begin(),
        descriptor.compatible_problem_ids.end(),
        problem_id
    ) != descriptor.compatible_problem_ids.end();
}

}  // namespace wflop
