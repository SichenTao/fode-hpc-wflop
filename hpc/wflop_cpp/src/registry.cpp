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
        },
        {
            "alga_guishan_planar_transfer",
            "ALGA declared planar Guishan-wind transfer",
            "alga_guishan_planar_wind_fode_evaluator_transfer_v1",
            "maximize expected complete-farm power on the paper-visible "
            "12 by 12 ideal grid using public WFLO-GGA Guishan annual "
            "wind probabilities and the audited FODE-E0 evaluator"
        },
        {
            "taae_zhangbei_structured_declared_proxy_v1",
            "TAAE structured 3D energy-noise declared proxy",
            "taae_zhangbei_structured_declared_proxy_v1",
            "minimize reciprocal expected power and A-weighted noise "
            "under a declared land-and-turbine cost constraint"
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
            "alga_attention_declared_reconstruction_v1",
            "ALGA attention engineering reconstruction",
            "10.1016/j.swevo.2025.102018",
            "paper_guided_citation_predecessor_derived_declared_completion",
            "alga_attention_declared_reconstruction_v1",
            {"alga_guishan_planar_transfer", "fode_e0_common"}
        },
        {
            "rlpso_compact_policy_declared_reconstruction_v1",
            "RPSO-derived compact policy engineering proxy",
            "10.1016/j.energy.2024.134050",
            "paper_guided_declared_compact_policy_engineering_proxy",
            "rlpso_compact_policy_declared_reconstruction_v1",
            {"rpso2024_source_problem_ws1_ws4"}
        },
        {
            "rlpso_paper_corrected_training_reconstruction_v1",
            "RPSO paper-corrected seeded PPO reconstruction",
            "10.1016/j.energy.2024.134050",
            "paper_corrected_seeded_full_ppo_declared_reconstruction",
            "rlpso_paper_corrected_training_reconstruction_v1",
            {"rpso2024_source_problem_ws1_ws4"}
        },
        {
            "fqfode_seeded_training_declared_reconstruction_v1",
            "FQFODE seeded offline-training reconstruction",
            "10.3390/math13182935",
            "paper_guided_seeded_offline_qtable_declared_reconstruction",
            "fqfode_seeded_training_declared_reconstruction_v1",
            {"fode_e0_common"}
        },
        {
            "taae_transformer_evolution_declared_reconstruction_v1",
            "TAAE Transformer-evolution reconstruction",
            "10.1109/jas.2026.126233",
            "paper_guided_training_from_scratch_declared_reconstruction",
            "taae_transformer_evolution_declared_reconstruction_v1",
            {}
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

const std::vector<TrainingDescriptor>& training_descriptors() {
    static const std::vector<TrainingDescriptor> descriptors{
        {
            "alga_attention_train_from_scratch_v1",
            "alga_attention_declared_reconstruction_v1",
            "generate deterministic generation corpus; train attention; "
            "freeze artifact; optimize",
            false,
        },
        {
            "rlpso_compact_policy_train_from_scratch_v1",
            "rlpso_compact_policy_declared_reconstruction_v1",
            "train online policy; validate; freeze run state; optimize",
            true,
        },
        {
            "rlpso_paper_corrected_train_from_scratch_v1",
            "rlpso_paper_corrected_training_reconstruction_v1",
            "train seeded PPO; validate; freeze run state; optimize",
            true,
        },
        {
            "fqfode_train_from_scratch_v1",
            "fqfode_seeded_training_declared_reconstruction_v1",
            "train four Q tables; validate; freeze artifact; optimize",
            true,
        },
        {
            "taae_train_from_scratch_v1",
            "taae_transformer_evolution_declared_reconstruction_v1",
            "generate corpus; pretrain Transformer; fine tune; freeze "
            "artifact; optimize latent representation",
            false,
        },
    };
    return descriptors;
}

const std::vector<BackendDescriptor>& backend_descriptors() {
    static const std::vector<BackendDescriptor> descriptors{
        {"cpu", "optimized pure C++ persistent-worker CPU path", true},
        {"auto", "resolves only to a measured executable backend", false},
        {"hybrid", "registered CPU+GPU capability; no admitted kernels", false},
        {"gpu", "registered GPU capability; no admitted kernels", false},
    };
    return descriptors;
}

const BackendDescriptor& backend_descriptor(const std::string& id) {
    const std::string normalized = id == "cpu+gpu" ? "hybrid" : id;
    const auto& descriptors = backend_descriptors();
    const auto found = std::find_if(
        descriptors.begin(),
        descriptors.end(),
        [&](const BackendDescriptor& descriptor) {
            return descriptor.id == normalized;
        }
    );
    if (found == descriptors.end()) {
        throw std::invalid_argument("unknown backend: " + id);
    }
    return *found;
}

CompatibilityDescriptor explain_compatibility(
    const std::string& algorithm_id,
    const std::string& problem_id
) {
    const auto& algorithm = algorithm_descriptor(algorithm_id);
    const auto& problem = problem_descriptor(problem_id);
    const bool admitted = std::find(
        algorithm.compatible_problem_ids.begin(),
        algorithm.compatible_problem_ids.end(),
        problem_id
    ) != algorithm.compatible_problem_ids.end();
    CompatibilityDescriptor result;
    result.algorithm_id = algorithm.id;
    result.problem_id = problem.id;
    result.compatible = admitted;
    if (admitted) {
        result.reason =
            "registered method and problem semantic identities preserve "
            "decision encoding, objective, constraints, evaluator detail, "
            "and physical-FES meaning";
    } else {
        result.reason =
            "method semantic identity does not admit this problem; no "
            "objective, decision, constraint, or physics coercion is allowed";
    }
    return result;
}

bool algorithm_supports_problem(
    const std::string& algorithm_id,
    const std::string& problem_id
) {
    return explain_compatibility(algorithm_id, problem_id).compatible;
}

}  // namespace wflop
