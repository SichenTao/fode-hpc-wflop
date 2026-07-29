/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: shared parameterized scalar/discrete WFLOP C++ evaluator
Paper title and DOI: thirteen scalar WFLOP packages; see
docs/scalar_problem_package_registry.tsv
Paper/source basis: Jensen/Park equations and per-case physical constants
Public asset: paper/source authority is recorded by each case contract
Missing/conflicts: legacy FODE defaults remain literal when fields are absent
Reconstruction: cache-aware deterministic equations and parallel batches
Method/problem semantic IDs: not_applicable_shared_infrastructure;
registry_defined
Controlling contract and claim boundary:
docs/scalar_problem_package_registry.tsv; expected power in kW, not raw AEP
Last evidence-audit date: 2026-07-30
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "fode/evaluator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fode {
namespace {

template <typename Task>
void execute_stage(
    PersistentExecutor& executor,
    int begin,
    int end,
    int minimum_parallel_items,
    EvaluationSchedule schedule,
    Task&& task
) {
    if (schedule == EvaluationSchedule::GranularityAware
        && end - begin < minimum_parallel_items) {
        for (int index = begin; index < end; ++index) {
            task(index);
        }
        return;
    }
    executor.parallel_for(begin, end, std::forward<Task>(task));
}

double interaction_area(double dx, double radius, double wake_radius) {
    if (dx >= radius + wake_radius) {
        return 0.0;
    }
    if (dx >= std::sqrt(wake_radius * wake_radius - radius * radius)) {
        const double alpha = std::acos(
            (wake_radius * wake_radius + dx * dx - radius * radius)
            / (2.0 * wake_radius * dx)
        );
        const double beta = std::acos(
            (radius * radius + dx * dx - wake_radius * wake_radius)
            / (2.0 * radius * dx)
        );
        return alpha * wake_radius * wake_radius
            + beta * radius * radius
            - wake_radius * dx * std::sin(alpha);
    }
    if (dx >= wake_radius - radius) {
        const double alpha = std::acos(
            (wake_radius * wake_radius + dx * dx - radius * radius)
            / (2.0 * wake_radius * dx)
        );
        const double beta = std::numbers::pi - std::acos(
            (radius * radius + dx * dx - wake_radius * wake_radius)
            / (2.0 * radius * dx)
        );
        return std::numbers::pi * radius * radius
            - (
                beta * radius * radius
                + wake_radius * dx * std::sin(alpha)
                - alpha * wake_radius * wake_radius
            );
    }
    return std::numbers::pi * radius * radius;
}

double deficiency(const CaseData& data, double dx, double dy) {
    if (dy == 0.0) {
        return 0.0;
    }
    const double rotor_radius = data.rotor_diameter / 2.0;
    const double entrainment =
        0.5 / std::log(data.hub_height / data.surface_roughness);
    const double wake_radius = rotor_radius + entrainment * dy;
    const double area = interaction_area(dx, rotor_radius, wake_radius);
    return data.wake_deficit_coefficient
        * (rotor_radius * rotor_radius)
        / (wake_radius * wake_radius)
        * area
        / (std::numbers::pi * rotor_radius * rotor_radius);
}

double turbine_power(const CaseData& data, double velocity) {
    if (velocity < data.power_curve_cutin_mps) {
        return 0.0;
    }
    if (velocity < data.power_curve_rated_mps) {
        if (data.power_curve_model == "cutin_shifted_cubic") {
            const double cutin3 =
                data.power_curve_cutin_mps
                * data.power_curve_cutin_mps
                * data.power_curve_cutin_mps;
            const double rated3 =
                data.power_curve_rated_mps
                * data.power_curve_rated_mps
                * data.power_curve_rated_mps;
            const double velocity3 = velocity * velocity * velocity;
            return data.power_curve_rated_kw
                * (velocity3 - cutin3) / (rated3 - cutin3);
        }
        return data.power_curve_cubic_coefficient
            * velocity * velocity * velocity;
    }
    if (velocity < data.power_curve_cutout_mps) {
        return data.power_curve_rated_kw;
    }
    return 0.0;
}

double gaussian_deficiency(
    const CaseData& data,
    double downstream,
    double crosswind,
    double vertical
) {
    if (!(downstream > 0.0)) {
        return 0.0;
    }
    const double rotor_radius = 0.5 * data.rotor_diameter;
    const double wake_radius =
        rotor_radius + data.gaussian_wake_expansion * downstream;
    const double sigma = wake_radius / 1.98;
    const double radial_square =
        crosswind * crosswind + vertical * vertical;
    return data.wake_deficit_coefficient
        * (rotor_radius * rotor_radius)
        / (wake_radius * wake_radius)
        * std::exp(-radial_square / (2.0 * sigma * sigma));
}

void evaluate_terrain_gaussian_states(
    const std::vector<double>& indices,
    int batch_size,
    const CaseData& data,
    PersistentExecutor& executor,
    EvaluationDetail detail,
    Evaluation& result
) {
    const int n = data.turbine_count;
    const int directions = static_cast<int>(data.theta.size());
    const int speeds = static_cast<int>(data.velocity.size());
    const int state_count = batch_size * directions;
    std::vector<double> x(static_cast<std::size_t>(batch_size * n));
    std::vector<double> y(static_cast<std::size_t>(batch_size * n));
    std::vector<double> z(static_cast<std::size_t>(batch_size * n));
    for (int row = 0; row < batch_size; ++row) {
        for (int turbine = 0; turbine < n; ++turbine) {
            const int cell_1based = static_cast<int>(std::llround(
                indices[static_cast<std::size_t>(row * n + turbine)]
            ));
            const int cell = cell_1based - 1;
            const int grid_row = cell / data.cols;
            const int grid_col = cell % data.cols;
            const std::size_t target =
                static_cast<std::size_t>(row * n + turbine);
            x[target] = (
                static_cast<double>(grid_col) + 0.5
            ) * data.cell_width;
            y[target] = (
                static_cast<double>(grid_row) + 0.5
            ) * data.cell_width;
            z[target] = data.hub_height + (
                data.terrain_elevation_m.empty()
                    ? 0.0
                    : data.terrain_elevation_m[
                        static_cast<std::size_t>(cell)
                    ]
            );
        }
    }

    std::vector<double> wake_fraction(
        static_cast<std::size_t>(state_count * n),
        0.0
    );
    auto evaluate_state = [&](int state) {
        const int row = state / directions;
        const int direction = state % directions;
        const double cosine = std::cos(data.theta[direction]);
        const double sine = std::sin(data.theta[direction]);
        const std::size_t row_offset = static_cast<std::size_t>(row * n);
        const std::size_t state_offset =
            static_cast<std::size_t>(state * n);
        std::vector<double> downwind(static_cast<std::size_t>(n));
        std::vector<double> crosswind(static_cast<std::size_t>(n));
        for (int turbine = 0; turbine < n; ++turbine) {
            const std::size_t source =
                row_offset + static_cast<std::size_t>(turbine);
            downwind[static_cast<std::size_t>(turbine)] =
                cosine * x[source] + sine * y[source];
            crosswind[static_cast<std::size_t>(turbine)] =
                -sine * x[source] + cosine * y[source];
        }
        for (int target = 0; target < n; ++target) {
            double squared_sum = 0.0;
            for (int source = 0; source < n; ++source) {
                if (source == target) {
                    continue;
                }
                const double fraction = gaussian_deficiency(
                    data,
                    downwind[static_cast<std::size_t>(target)]
                        - downwind[static_cast<std::size_t>(source)],
                    crosswind[static_cast<std::size_t>(target)]
                        - crosswind[static_cast<std::size_t>(source)],
                    z[row_offset + static_cast<std::size_t>(target)]
                        - z[row_offset + static_cast<std::size_t>(source)]
                );
                squared_sum += fraction * fraction;
            }
            wake_fraction[
                state_offset + static_cast<std::size_t>(target)
            ] = std::min(1.0, std::sqrt(squared_sum));
        }
    };
    if (state_count >= executor.thread_count()) {
        executor.parallel_for(0, state_count, evaluate_state);
    } else {
        for (int state = 0; state < state_count; ++state) {
            evaluate_state(state);
        }
    }

    for (int row = 0; row < batch_size; ++row) {
        std::vector<double> turbine_power_kw(
            static_cast<std::size_t>(n),
            0.0
        );
        for (int turbine = 0; turbine < n; ++turbine) {
            const std::size_t point =
                static_cast<std::size_t>(row * n + turbine);
            const double shear = std::pow(
                std::max(z[point] / data.hub_height, 1.0e-12),
                data.terrain_shear_exponent
            );
            for (int direction = 0;
                 direction < directions;
                 ++direction) {
                const double fraction = wake_fraction[
                    static_cast<std::size_t>(
                        (row * directions + direction) * n + turbine
                    )
                ];
                for (int speed = 0; speed < speeds; ++speed) {
                    const double ambient =
                        data.velocity[static_cast<std::size_t>(speed)]
                        * shear;
                    turbine_power_kw[static_cast<std::size_t>(turbine)] +=
                        data.probability[static_cast<std::size_t>(
                            direction * speeds + speed
                        )]
                        * turbine_power(data, ambient * (1.0 - fraction));
                }
            }
        }
        std::vector<double> ordered = turbine_power_kw;
        std::stable_sort(ordered.begin(), ordered.end());
        result.fitness[static_cast<std::size_t>(row)] =
            std::accumulate(ordered.begin(), ordered.end(), 0.0);
        if (detail == EvaluationDetail::TotalAndPerTurbine) {
            std::vector<int> order(static_cast<std::size_t>(n));
            std::iota(order.begin(), order.end(), 0);
            std::stable_sort(
                order.begin(),
                order.end(),
                [&](int left, int right) {
                    return turbine_power_kw[static_cast<std::size_t>(left)]
                        < turbine_power_kw[static_cast<std::size_t>(right)];
                }
            );
            for (int rank = 0; rank < n; ++rank) {
                const int turbine = order[static_cast<std::size_t>(rank)];
                const std::size_t target =
                    static_cast<std::size_t>(row * n + rank);
                result.accumulated_turbine_power_kw[target] =
                    turbine_power_kw[static_cast<std::size_t>(turbine)];
                result.turbine_position_order_1based[target] =
                    static_cast<int>(std::llround(
                        indices[static_cast<std::size_t>(
                            row * n + turbine
                        )]
                    ));
            }
        }
    }
}

void evaluate_fused_states(
    const std::vector<double>& indices,
    int batch_size,
    const CaseData& data,
    PersistentExecutor& executor,
    EvaluationDetail detail,
    Evaluation& result
) {
    const int n = data.turbine_count;
    const int directions = static_cast<int>(data.theta.size());
    const int speeds = static_cast<int>(data.velocity.size());
    const int state_count = batch_size * directions;
    std::vector<double> x(static_cast<std::size_t>(batch_size * n));
    std::vector<double> y(static_cast<std::size_t>(batch_size * n));
    std::vector<double> wake(
        static_cast<std::size_t>(state_count * n),
        0.0
    );

    // FODE reaches only three layouts per generation on the largest case.
    // Coordinate conversion is too short to justify waking a 20-thread team.
    for (int row = 0; row < batch_size; ++row) {
        for (int turbine = 0; turbine < n; ++turbine) {
            const int cell_1based = static_cast<int>(std::llround(
                indices[static_cast<std::size_t>(row * n + turbine)]
            ));
            const int cell = cell_1based - 1;
            const int grid_row = cell / data.cols;
            const int grid_col = cell - grid_row * data.cols;
            const std::size_t target =
                static_cast<std::size_t>(row * n + turbine);
            x[target] = static_cast<double>(grid_col) * data.cell_width
                + 0.5 * data.cell_width;
            y[target] = static_cast<double>(grid_row) * data.cell_width
                + 0.5 * data.cell_width;
        }
    }

    // A state is one complete layout-direction wake calculation.  Fusing
    // rotation, upstream ordering, and all downstream deficits gives each
    // dispatched task enough O(n^2) work to amortize synchronization.
    // Batch-one local-search evaluations remain serial; a population of three
    // layouts exposes 30 independent states for the 20-core Spark2 run.
    auto evaluate_state = [&](int state) {
        const int row = state / directions;
        const int direction = state - row * directions;
        const std::size_t row_offset = static_cast<std::size_t>(row * n);
        const std::size_t state_offset = static_cast<std::size_t>(state * n);
        const double cosine = std::cos(data.theta[direction]);
        const double sine = std::sin(data.theta[direction]);
        std::vector<double> transformed_x(static_cast<std::size_t>(n));
        std::vector<double> transformed_y(static_cast<std::size_t>(n));
        std::vector<int> upstream_order(static_cast<std::size_t>(n));
        for (int turbine = 0; turbine < n; ++turbine) {
            const std::size_t source =
                row_offset + static_cast<std::size_t>(turbine);
            transformed_x[static_cast<std::size_t>(turbine)] =
                cosine * x[source] - sine * y[source];
            transformed_y[static_cast<std::size_t>(turbine)] =
                sine * x[source] + cosine * y[source];
            upstream_order[static_cast<std::size_t>(turbine)] = turbine;
        }
        std::stable_sort(
            upstream_order.begin(),
            upstream_order.end(),
            [&transformed_y](int lhs, int rhs) {
                return transformed_y[static_cast<std::size_t>(lhs)]
                    > transformed_y[static_cast<std::size_t>(rhs)];
            }
        );
        for (int downstream_position = 1;
             downstream_position < n;
             ++downstream_position) {
            const int downstream = upstream_order[
                static_cast<std::size_t>(downstream_position)
            ];
            double squared_sum = 0.0;
            for (int upstream_position = 0;
                 upstream_position <= downstream_position;
                 ++upstream_position) {
                const int upstream = upstream_order[
                    static_cast<std::size_t>(upstream_position)
                ];
                const double dx = std::abs(
                    transformed_x[static_cast<std::size_t>(downstream)]
                    - transformed_x[static_cast<std::size_t>(upstream)]
                );
                const double dy = std::abs(
                    transformed_y[static_cast<std::size_t>(downstream)]
                    - transformed_y[static_cast<std::size_t>(upstream)]
                );
                const double value = deficiency(data, dx, dy);
                squared_sum += value * value;
            }
            wake[state_offset + static_cast<std::size_t>(downstream)] =
                std::sqrt(squared_sum);
        }
    };
    if (state_count >= executor.thread_count()) {
        executor.parallel_for(0, state_count, evaluate_state);
    } else {
        for (int state = 0; state < state_count; ++state) {
            evaluate_state(state);
        }
    }

    // The per-turbine reduction keeps the original direction-speed order.
    // Consequently the fused schedule changes task granularity only; it does
    // not change the wind model, probability weighting, or floating-point
    // reduction order that defines one layout's objective value.
    std::vector<double> accumulated(
        static_cast<std::size_t>(batch_size * n),
        0.0
    );
    for (int row = 0; row < batch_size; ++row) {
        for (int turbine = 0; turbine < n; ++turbine) {
            double power = 0.0;
            for (int direction = 0; direction < directions; ++direction) {
                const double loss = wake[
                    static_cast<std::size_t>(
                        (row * directions + direction) * n + turbine
                    )
                ];
                for (int speed = 0; speed < speeds; ++speed) {
                    const double actual_velocity =
                        (1.0 - loss) * data.velocity[speed];
                    power += turbine_power(data, actual_velocity)
                        * data.probability[
                            static_cast<std::size_t>(
                                direction * speeds + speed
                            )
                        ];
                }
            }
            accumulated[static_cast<std::size_t>(row * n + turbine)] =
                power;
        }

        const auto begin = accumulated.begin()
            + static_cast<std::ptrdiff_t>(row * n);
        std::vector<double> sorted_power(
            begin,
            begin + static_cast<std::ptrdiff_t>(n)
        );
        std::stable_sort(sorted_power.begin(), sorted_power.end());
        double fitness = 0.0;
        for (const double value : sorted_power) {
            fitness += value;
        }
        result.fitness[static_cast<std::size_t>(row)] = fitness;
        if (detail == EvaluationDetail::TotalAndPerTurbine) {
            std::vector<int> order(static_cast<std::size_t>(n));
            std::iota(order.begin(), order.end(), 0);
            std::stable_sort(
                order.begin(),
                order.end(),
                [&accumulated, row, n](int lhs, int rhs) {
                    return accumulated[
                        static_cast<std::size_t>(row * n + lhs)
                    ] < accumulated[
                        static_cast<std::size_t>(row * n + rhs)
                    ];
                }
            );
            for (int rank = 0; rank < n; ++rank) {
                const int turbine = order[static_cast<std::size_t>(rank)];
                const std::size_t target =
                    static_cast<std::size_t>(row * n + rank);
                result.accumulated_turbine_power_kw[target] = accumulated[
                    static_cast<std::size_t>(row * n + turbine)
                ];
                result.turbine_position_order_1based[target] =
                    static_cast<int>(std::llround(indices[
                        static_cast<std::size_t>(row * n + turbine)
                    ]));
            }
        }
    }
}

}  // namespace

Evaluation evaluate_population_hpc(
    const std::vector<double>& indices,
    int batch_size,
    const CaseData& data,
    int workers,
    EvaluationDetail detail,
    EvaluationSchedule schedule
) {
    PersistentExecutor executor(workers);
    return evaluate_population_hpc(
        indices,
        batch_size,
        data,
        executor,
        detail,
        schedule
    );
}

Evaluation evaluate_population_hpc(
    const std::vector<double>& indices,
    int batch_size,
    const CaseData& data,
    PersistentExecutor& executor,
    EvaluationDetail detail,
    EvaluationSchedule schedule
) {
    if (batch_size <= 0) {
        throw std::invalid_argument("batch size must be positive");
    }
    const int n = data.turbine_count;
    const int directions = static_cast<int>(data.theta.size());
    const int speeds = static_cast<int>(data.velocity.size());
    const int dimension = data.rows * data.cols;
    if (static_cast<int>(indices.size()) != batch_size * n) {
        throw std::invalid_argument("index matrix has an invalid shape");
    }
    for (int row = 0; row < batch_size; ++row) {
        std::vector<int> cells;
        cells.reserve(static_cast<std::size_t>(n));
        for (int turbine = 0; turbine < n; ++turbine) {
            const int cell = static_cast<int>(std::llround(
                indices[static_cast<std::size_t>(row * n + turbine)]
            ));
            if (cell < 1 || cell > dimension) {
                throw std::invalid_argument(
                    "layout contains an out-of-range grid cell"
                );
            }
            cells.push_back(cell);
        }
        std::sort(cells.begin(), cells.end());
        if (std::adjacent_find(cells.begin(), cells.end()) != cells.end()) {
            throw std::invalid_argument("layout contains duplicate grid cells");
        }
    }

    Evaluation result;
    result.fitness.assign(static_cast<std::size_t>(batch_size), 0.0);
    if (detail == EvaluationDetail::TotalAndPerTurbine) {
        result.accumulated_turbine_power_kw.assign(
            static_cast<std::size_t>(batch_size * n),
            0.0
        );
        result.turbine_position_order_1based.assign(
            static_cast<std::size_t>(batch_size * n),
            0
        );
    }
    result.requested_workers = executor.thread_count();
    result.observed_workers = executor.thread_count();

    const auto started = std::chrono::steady_clock::now();
    if (data.wake_model == "terrain_gaussian_rss") {
        evaluate_terrain_gaussian_states(
            indices,
            batch_size,
            data,
            executor,
            detail,
            result
        );
        result.elapsed_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started
        ).count();
        return result;
    }
    if (schedule == EvaluationSchedule::GranularityAware) {
        evaluate_fused_states(
            indices,
            batch_size,
            data,
            executor,
            detail,
            result
        );
        result.elapsed_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started
        ).count();
        return result;
    }

    const int state_count = batch_size * directions;
    const int downstream_tasks = state_count * std::max(0, n - 1);
    std::vector<double> x(static_cast<std::size_t>(batch_size * n));
    std::vector<double> y(static_cast<std::size_t>(batch_size * n));
    std::vector<double> transformed_x(
        static_cast<std::size_t>(state_count * n)
    );
    std::vector<double> transformed_y(
        static_cast<std::size_t>(state_count * n)
    );
    std::vector<int> upstream_order(
        static_cast<std::size_t>(state_count * n)
    );
    std::vector<double> wake(
        static_cast<std::size_t>(state_count * n),
        0.0
    );
    std::vector<double> accumulated(
        static_cast<std::size_t>(batch_size * n),
        0.0
    );

    execute_stage(executor, 0, batch_size, 32, schedule, [&](int row) {
        for (int turbine = 0; turbine < n; ++turbine) {
            const int cell_1based = static_cast<int>(std::llround(
                indices[static_cast<std::size_t>(row * n + turbine)]
            ));
            const int cell = cell_1based - 1;
            const int grid_row = cell / data.cols;
            const int grid_col = cell - grid_row * data.cols;
            const std::size_t target =
                static_cast<std::size_t>(row * n + turbine);
            x[target] = static_cast<double>(grid_col) * data.cell_width
                + 0.5 * data.cell_width;
            y[target] = static_cast<double>(grid_row) * data.cell_width
                + 0.5 * data.cell_width;
        }
    });

    execute_stage(executor, 0, state_count, 64, schedule, [&](int state) {
        const int row = state / directions;
        const int direction = state - row * directions;
        const double cosine = std::cos(data.theta[direction]);
        const double sine = std::sin(data.theta[direction]);
        const std::size_t state_offset = static_cast<std::size_t>(state * n);
        const std::size_t row_offset = static_cast<std::size_t>(row * n);
        for (int turbine = 0; turbine < n; ++turbine) {
            const std::size_t source =
                row_offset + static_cast<std::size_t>(turbine);
            const std::size_t target =
                state_offset + static_cast<std::size_t>(turbine);
            transformed_x[target] = cosine * x[source] - sine * y[source];
            transformed_y[target] = sine * x[source] + cosine * y[source];
            upstream_order[target] = turbine;
        }
        std::stable_sort(
            upstream_order.begin()
                + static_cast<std::ptrdiff_t>(state_offset),
            upstream_order.begin()
                + static_cast<std::ptrdiff_t>(state_offset + n),
            [&transformed_y, state_offset](int lhs, int rhs) {
                return transformed_y[
                    state_offset + static_cast<std::size_t>(lhs)
                ] > transformed_y[
                    state_offset + static_cast<std::size_t>(rhs)
                ];
            }
        );
    });

    execute_stage(
        executor,
        0,
        downstream_tasks,
        256,
        schedule,
        [&](int task) {
        const int downstream_position = task % (n - 1) + 1;
        const int state = task / (n - 1);
        const std::size_t state_offset = static_cast<std::size_t>(state * n);
        const int downstream = upstream_order[
            state_offset + static_cast<std::size_t>(downstream_position)
        ];
        double squared_sum = 0.0;
        for (
            int upstream_position = 0;
            upstream_position <= downstream_position;
            ++upstream_position
        ) {
            const int upstream = upstream_order[
                state_offset + static_cast<std::size_t>(upstream_position)
            ];
            const double dx = std::abs(
                transformed_x[
                    state_offset + static_cast<std::size_t>(downstream)
                ] - transformed_x[
                    state_offset + static_cast<std::size_t>(upstream)
                ]
            );
            const double dy = std::abs(
                transformed_y[
                    state_offset + static_cast<std::size_t>(downstream)
                ] - transformed_y[
                    state_offset + static_cast<std::size_t>(upstream)
                ]
            );
            const double value = deficiency(data, dx, dy);
            squared_sum += value * value;
        }
        wake[state_offset + static_cast<std::size_t>(downstream)] =
            std::sqrt(squared_sum);
        }
    );

    execute_stage(
        executor,
        0,
        batch_size * n,
        512,
        schedule,
        [&](int task) {
        const int row = task / n;
        const int turbine = task - row * n;
        double power = 0.0;
        for (int direction = 0; direction < directions; ++direction) {
            const double loss = wake[
                static_cast<std::size_t>(
                    (row * directions + direction) * n + turbine
                )
            ];
            for (int speed = 0; speed < speeds; ++speed) {
                const double actual_velocity =
                    (1.0 - loss) * data.velocity[speed];
                power += turbine_power(data, actual_velocity)
                    * data.probability[
                        static_cast<std::size_t>(
                            direction * speeds + speed
                        )
                    ];
            }
        }
        accumulated[static_cast<std::size_t>(task)] = power;
        }
    );

    execute_stage(executor, 0, batch_size, 32, schedule, [&](int row) {
        const auto begin = accumulated.begin()
            + static_cast<std::ptrdiff_t>(row * n);
        std::vector<double> sorted_power(
            begin,
            begin + static_cast<std::ptrdiff_t>(n)
        );
        std::stable_sort(sorted_power.begin(), sorted_power.end());
        double fitness = 0.0;
        for (const double value : sorted_power) {
            fitness += value;
        }
        result.fitness[static_cast<std::size_t>(row)] = fitness;
        if (detail == EvaluationDetail::TotalAndPerTurbine) {
            std::vector<int> order(static_cast<std::size_t>(n));
            std::iota(order.begin(), order.end(), 0);
            std::stable_sort(
                order.begin(),
                order.end(),
                [&accumulated, row, n](int lhs, int rhs) {
                    return accumulated[
                        static_cast<std::size_t>(row * n + lhs)
                    ] < accumulated[
                        static_cast<std::size_t>(row * n + rhs)
                    ];
                }
            );
            for (int rank = 0; rank < n; ++rank) {
                const int turbine = order[static_cast<std::size_t>(rank)];
                const std::size_t target =
                    static_cast<std::size_t>(row * n + rank);
                result.accumulated_turbine_power_kw[target] = accumulated[
                    static_cast<std::size_t>(row * n + turbine)
                ];
                result.turbine_position_order_1based[target] =
                    static_cast<int>(std::llround(indices[
                        static_cast<std::size_t>(row * n + turbine)
                    ]));
            }
        }
    });

    result.elapsed_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started
    ).count();
    return result;
}

}  // namespace fode
