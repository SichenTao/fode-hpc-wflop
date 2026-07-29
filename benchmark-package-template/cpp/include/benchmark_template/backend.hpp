/* BENCHMARK PACKAGE FACT DECLARATION
Paper DOI: REQUIRED_DOI
Public author source: REQUIRED_URL_OR_EXPLICITLY_NOT_FOUND
Known missing fields: REQUIRED_BACKEND_EVIDENCE
Reconstruction action: CPU ADMITTED; HYBRID AND GPU FAIL CLOSED UNTIL TESTED
Claim boundary: INTERFACE CAPABILITY ONLY; NO ACCELERATOR PERFORMANCE CLAIM
*/
#pragma once

#include <string>
#include <string_view>

namespace benchmark_template {

enum class Backend {
    cpu,
    hybrid,
    gpu,
    automatic,
};

struct BackendCapability {
    Backend backend;
    bool supported;
    std::string status;
    std::string reason;
};

[[nodiscard]] Backend parse_backend(std::string_view value);
[[nodiscard]] BackendCapability capability(Backend backend);
[[nodiscard]] Backend resolve_backend_or_throw(Backend requested);

}  // namespace benchmark_template
