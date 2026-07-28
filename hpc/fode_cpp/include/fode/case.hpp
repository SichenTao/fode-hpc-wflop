#pragma once

#include <string>
#include <vector>

namespace fode {

struct CaseData {
    std::string case_id;
    int rows = 0;
    int cols = 0;
    int turbine_count = 0;
    double cell_width = 0.0;
    std::vector<double> theta;
    std::vector<double> velocity;
    std::vector<double> probability;
    std::vector<int> unavailable_cells_1based;
};

std::vector<CaseData> load_cases(const std::string& path);
CaseData load_case(const std::string& path, const std::string& case_id);

}  // namespace fode
