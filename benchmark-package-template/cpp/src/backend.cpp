/* BENCHMARK PACKAGE FACT DECLARATION
Paper DOI: REQUIRED_DOI
Public author source: REQUIRED_URL_OR_EXPLICITLY_NOT_FOUND
Known missing fields: REQUIRED_ACCELERATOR_KERNEL_AND_EQUIVALENCE_RECEIPTS
Reconstruction action: REJECT UNADMITTED HYBRID/GPU REQUESTS BEFORE OPTIMIZATION
Claim boundary: CPU SKELETON ONLY; NO HYBRID OR GPU IMPLEMENTATION CLAIM
*/
#include "benchmark_template/backend.hpp"

#include <stdexcept>

namespace benchmark_template {

Backend parse_backend(const std::string_view value) {
    if (value == "cpu") {
        return Backend::cpu;
    }
    if (value == "hybrid" || value == "cpu+gpu") {
        return Backend::hybrid;
    }
    if (value == "gpu") {
        return Backend::gpu;
    }
    if (value == "auto") {
        return Backend::automatic;
    }
    throw std::invalid_argument("unknown backend: " + std::string(value));
}

BackendCapability capability(const Backend backend) {
    switch (backend) {
    case Backend::cpu:
        return {backend, true, "pure_cpp_cpu", "admitted template backend"};
    case Backend::automatic:
        return {
            backend,
            true,
            "resolves_to_cpu",
            "accelerator backends remain unadmitted",
        };
    case Backend::hybrid:
        return {
            backend,
            false,
            "fails_closed",
            "hybrid semantic and scaling gates are absent",
        };
    case Backend::gpu:
        return {
            backend,
            false,
            "fails_closed",
            "GPU semantic and scaling gates are absent",
        };
    }
    throw std::logic_error("unreachable backend");
}

Backend resolve_backend_or_throw(const Backend requested) {
    const auto record = capability(requested);
    if (!record.supported) {
        throw std::runtime_error(record.reason);
    }
    return requested == Backend::automatic ? Backend::cpu : requested;
}

}  // namespace benchmark_template
