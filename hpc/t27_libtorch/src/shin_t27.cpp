/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T27 native GCH evaluator and LibTorch paper-profile EDM
Paper DOI: 10.1145/3711896.3737181
Public source: https://github.com/dbsxodud-11/layopt at 19ff389;
FLORIS 4.1.1 at 2c3be8f.
Missing facts and reconstruction decisions: include/core99/shin_t27.hpp.
Semantic IDs: shin2025_conditional_edm_gat_paper_profile_v1 and
shin2025_floris411_gch_rectangular_v1.
Contract: shared/contracts/core99_t27_shin_diffusion_2025.json.
Official numeric reference: shared/data/core99_t27/floris411_gch_fixture.json.
Claim boundary: academic paper-first reconstruction, not author software or
bitwise FLORIS identity.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/shin_t27.hpp"

#include <ATen/Parallel.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <thread>

namespace core99::t27 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double pi = 3.141592653589793238462643383279502884;
constexpr double rotor_diameter_m = 125.88;
constexpr double hub_height_m = 90.0;

constexpr double wind_speed_table[] = {
    0.0,2.9,3.0,4.0,5.0,6.0,7.0,7.1,7.2,7.3,7.4,7.5,7.6,
    7.7,7.8,7.9,8.0,9.0,10.0,10.1,10.2,10.3,10.4,10.5,10.6,
    10.7,10.8,10.9,11.0,11.1,11.2,11.3,11.4,11.5,11.6,11.7,
    11.8,11.9,12.0,13.0,14.0,15.0,16.0,17.0,18.0,19.0,20.0,
    21.0,22.0,23.0,24.0,25.0,25.1,50.0
};
constexpr double power_kw_table[] = {
    0.0,0.0,40.518011517569214,177.67162506419703,
    403.900880943964,737.5889584824021,1187.1774030611875,
    1239.245945375778,1292.5184293723503,1347.3213147477102,
    1403.2573725578948,1460.7011898730707,1519.6419125979983,
    1580.174365096404,1642.1103166918167,1705.758292831,
    1771.1659528893977,2518.553107505315,3448.381605840943,
    3552.140809000129,3657.9545431794127,3765.121299313842,
    3873.928844315059,3984.4800226955504,4096.582833096852,
    4210.721306623712,4326.154305853405,4443.395565353604,
    4562.497934188341,4683.419890251577,4806.164748311019,
    4929.931918769215,5000.0,5000.0,5000.0,5000.0,5000.0,
    5000.0,5000.0,5000.0,5000.0,5000.0,5000.0,5000.0,5000.0,
    5000.0,5000.0,5000.0,5000.0,5000.0,5000.0,5000.0,0.0,0.0
};
constexpr double thrust_table[] = {
    0.0,0.0,1.132034888,0.999470963,0.917697381,0.860849503,
    0.815371198,0.811614904,0.807939328,0.80443352,0.800993851,
    0.79768116,0.794529244,0.791495834,0.788560434,0.787217182,
    0.787127977,0.785839257,0.783812219,0.783568108,0.783328285,
    0.781194418,0.777292539,0.773464375,0.769690236,0.766001924,
    0.762348072,0.758760824,0.755242872,0.751792927,0.748434131,
    0.745113997,0.717806682,0.672204789,0.63831272,0.610176496,
    0.585456847,0.563222111,0.542912273,0.399312061,0.310517829,
    0.248633226,0.203543725,0.169616419,0.143478955,0.122938861,
    0.106515296,0.093026095,0.081648606,0.072197368,0.064388275,
    0.057782745,0.0,0.0
};
constexpr std::size_t table_size =
    sizeof(wind_speed_table) / sizeof(wind_speed_table[0]);

double interpolate(
    const double* values,
    double speed,
    double out_of_bounds
) {
    if (speed < wind_speed_table[0]
        || speed > wind_speed_table[table_size - 1]) {
        return out_of_bounds;
    }
    const auto* upper = std::upper_bound(
        wind_speed_table,
        wind_speed_table + table_size,
        speed
    );
    if (upper == wind_speed_table) return values[0];
    if (upper == wind_speed_table + table_size) {
        return values[table_size - 1];
    }
    const std::size_t right =
        static_cast<std::size_t>(upper - wind_speed_table);
    const std::size_t left = right - 1;
    const double fraction =
        (speed - wind_speed_table[left])
        / (wind_speed_table[right] - wind_speed_table[left]);
    return values[left] + fraction * (values[right] - values[left]);
}

double turbine_power_w(double effective_speed) {
    return 1000.0 * interpolate(power_kw_table, effective_speed, 0.0);
}

double thrust_coefficient(double effective_speed) {
    return std::clamp(
        interpolate(thrust_table, effective_speed, 0.0001),
        0.0001,
        0.9999
    );
}

struct RotatedTurbine {
    std::size_t original = 0;
    double downwind = 0.0;
    double crosswind = 0.0;
};

double elapsed(const Clock::time_point& begin) {
    return std::chrono::duration<double>(Clock::now() - begin).count();
}

torch::Tensor activate(
    torch::Tensor value,
    ActivationProfile profile
) {
    if (profile == ActivationProfile::paper_leaky_relu) {
        return torch::leaky_relu(value, 0.2);
    }
    return torch::gelu(value);
}

torch::Tensor expand_like_batch(
    torch::Tensor value,
    const torch::Tensor& reference
) {
    while (value.dim() < reference.dim()) value = value.unsqueeze(-1);
    return value;
}

void update_ema(
    torch::nn::Module& ema,
    const torch::nn::Module& current,
    double decay
) {
    torch::NoGradGuard guard;
    auto ema_parameters = ema.named_parameters();
    for (const auto& item : current.named_parameters()) {
        ema_parameters[item.key()].mul_(decay).add_(
            item.value(),
            1.0 - decay
        );
    }
    auto ema_buffers = ema.named_buffers();
    for (const auto& item : current.named_buffers()) {
        if (ema_buffers.contains(item.key())) {
            ema_buffers[item.key()].copy_(item.value());
        }
    }
}

}  // namespace

Floris411Gch::Floris411Gch(int workers)
    : workers_(std::max(1, workers)) {}

int Floris411Gch::workers() const noexcept { return workers_; }

Evaluation Floris411Gch::evaluate(
    const Layout& layout,
    double side_length_m,
    WindScenario wind
) const {
    if (
        layout.x_unit.empty()
        || layout.x_unit.size() != layout.y_unit.size()
        || side_length_m <= 0.0
        || wind.speed_mps <= 0.0
    ) {
        throw std::invalid_argument("invalid T27 layout or wind scenario");
    }
    const std::size_t count = layout.x_unit.size();
    const double radians = wind.direction_degrees * pi / 180.0;
    std::vector<RotatedTurbine> turbines(count);
    for (std::size_t index = 0; index < count; ++index) {
        const double x = layout.x_unit[index] * side_length_m;
        const double y = layout.y_unit[index] * side_length_m;
        turbines[index] = {
            index,
            -std::sin(radians) * x - std::cos(radians) * y,
            std::cos(radians) * x - std::sin(radians) * y,
        };
    }
    std::stable_sort(
        turbines.begin(),
        turbines.end(),
        [](const auto& first, const auto& second) {
            return first.downwind < second.downwind;
        }
    );

    constexpr double rotor_offset = rotor_diameter_m / 4.0;
    constexpr double offsets[3] = {-rotor_offset, 0.0, rotor_offset};
    std::vector<std::array<double, 9>> initial(count);
    std::vector<std::array<double, 9>> deficit_square(count);
    std::vector<double> effective_ti(count, 0.06);
    for (std::size_t turbine = 0; turbine < count; ++turbine) {
        int point = 0;
        for (double lateral : offsets) {
            (void)lateral;
            for (double vertical : offsets) {
                const double height = hub_height_m + vertical;
                initial[turbine][point] =
                    wind.speed_mps * std::pow(height / hub_height_m, 0.12);
                deficit_square[turbine][point] = 0.0;
                ++point;
            }
        }
    }

    auto rotor_speed = [&](std::size_t sorted_index) {
        double cubic = 0.0;
        for (int point = 0; point < 9; ++point) {
            const double velocity = std::max(
                0.0,
                initial[sorted_index][point]
                    - std::sqrt(deficit_square[sorted_index][point])
            );
            cubic += velocity * velocity * velocity;
        }
        return std::cbrt(cubic / 9.0);
    };

    for (std::size_t upstream = 0; upstream < count; ++upstream) {
        const double speed = rotor_speed(upstream);
        const double ct = thrust_coefficient(speed);
        const double induction =
            0.5 * (1.0 - std::sqrt(std::max(0.0, 1.0 - ct)));
        const double u0 = wind.speed_mps * std::sqrt(1.0 - ct);
        const double u_r =
            wind.speed_mps * ct
            / (2.0 * (1.0 - std::sqrt(1.0 - ct)));
        const double sigma0 =
            0.5 * rotor_diameter_m
            * std::sqrt(u_r / (wind.speed_mps + u0));
        const double x0 =
            turbines[upstream].downwind
            + rotor_diameter_m * (1.0 + std::sqrt(1.0 - ct))
                / (
                    std::sqrt(2.0)
                    * (4.0 * 0.58 * effective_ti[upstream]
                       + 2.0 * 0.077 * (1.0 - std::sqrt(1.0 - ct)))
                );
        for (
            std::size_t downstream = upstream + 1;
            downstream < count;
            ++downstream
        ) {
            const double distance =
                turbines[downstream].downwind
                - turbines[upstream].downwind;
            if (distance <= 0.1) continue;
            const double start_distance =
                x0 - turbines[upstream].downwind;
            double sigma;
            if (distance < start_distance) {
                const double ramp = distance / start_distance;
                sigma =
                    (1.0 - ramp) * 0.501 * rotor_diameter_m
                        * std::sqrt(ct / 2.0)
                    + ramp * sigma0;
            } else {
                const double expansion =
                    0.38 * effective_ti[upstream] + 0.004;
                sigma =
                    expansion * (distance - start_distance) + sigma0;
            }
            double overlap_points = 0.0;
            int point = 0;
            for (double lateral : offsets) {
                const double cross_distance =
                    turbines[downstream].crosswind + lateral
                    - turbines[upstream].crosswind;
                for (double vertical : offsets) {
                    const double argument =
                        (cross_distance * cross_distance
                         + vertical * vertical)
                        / (2.0 * sigma * sigma);
                    const double core = std::clamp(
                        1.0 - ct * rotor_diameter_m * rotor_diameter_m
                            / (8.0 * sigma * sigma),
                        0.0,
                        1.0
                    );
                    const double fraction =
                        (1.0 - std::sqrt(core)) * std::exp(-argument);
                    const double absolute_deficit =
                        fraction * initial[downstream][point];
                    deficit_square[downstream][point] +=
                        absolute_deficit * absolute_deficit;
                    if (absolute_deficit > 0.05) overlap_points += 1.0;
                    ++point;
                }
            }
            if (distance <= 15.0 * rotor_diameter_m) {
                const double added =
                    (overlap_points / 9.0)
                    * 0.5 * std::pow(induction, 0.8)
                    * std::pow(0.06, 0.1)
                    * std::pow(distance / rotor_diameter_m, -0.32);
                effective_ti[downstream] = std::max(
                    effective_ti[downstream],
                    std::sqrt(0.06 * 0.06 + added * added)
                );
            }
        }
    }

    Evaluation result;
    result.turbine_power_w.assign(count, 0.0);
    result.minimum_spacing_m = std::numeric_limits<double>::infinity();
    for (std::size_t sorted = 0; sorted < count; ++sorted) {
        const double power = turbine_power_w(rotor_speed(sorted));
        result.turbine_power_w[turbines[sorted].original] = power;
        result.farm_power_w += power;
    }
    for (std::size_t first = 0; first < count; ++first) {
        for (std::size_t second = first + 1; second < count; ++second) {
            const double dx =
                (layout.x_unit[first] - layout.x_unit[second])
                * side_length_m;
            const double dy =
                (layout.y_unit[first] - layout.y_unit[second])
                * side_length_m;
            result.minimum_spacing_m = std::min(
                result.minimum_spacing_m,
                std::hypot(dx, dy)
            );
        }
    }
    if (count == 1) {
        result.minimum_spacing_m =
            std::numeric_limits<double>::infinity();
    }
    result.annual_energy_mwh = result.farm_power_w * 8760.0 / 1.0e6;
    return result;
}

std::vector<Evaluation> Floris411Gch::evaluate_batch(
    const std::vector<Layout>& layouts,
    double side_length_m,
    WindScenario wind
) const {
    std::vector<Evaluation> results(layouts.size());
    std::atomic<std::size_t> cursor{0};
    const int actual_workers = std::min<int>(
        workers_,
        std::max<std::size_t>(1, layouts.size())
    );
    std::vector<std::jthread> threads;
    threads.reserve(static_cast<std::size_t>(actual_workers));
    for (int worker = 0; worker < actual_workers; ++worker) {
        threads.emplace_back([&]() {
            while (true) {
                const std::size_t index = cursor.fetch_add(1);
                if (index >= layouts.size()) return;
                results[index] = evaluate(layouts[index], side_length_m, wind);
            }
        });
    }
    return results;
}

std::vector<Evaluation> Floris411Gch::evaluate_batch(
    const std::vector<Layout>& layouts,
    double side_length_m,
    const std::vector<WindScenario>& winds
) const {
    if (layouts.size() != winds.size()) {
        throw std::invalid_argument("T27 layout/wind batch mismatch");
    }
    std::vector<Evaluation> results(layouts.size());
    std::atomic<std::size_t> cursor{0};
    const int actual_workers = std::min<int>(
        workers_,
        std::max<std::size_t>(1, layouts.size())
    );
    std::vector<std::jthread> threads;
    threads.reserve(static_cast<std::size_t>(actual_workers));
    for (int worker = 0; worker < actual_workers; ++worker) {
        threads.emplace_back([&]() {
            while (true) {
                const std::size_t index = cursor.fetch_add(1);
                if (index >= layouts.size()) return;
                results[index] = evaluate(
                    layouts[index],
                    side_length_m,
                    winds[index]
                );
            }
        });
    }
    return results;
}

DenseGatLayerImpl::DenseGatLayerImpl(int input_width, int output_width)
    : projection(register_module(
          "projection",
          torch::nn::Linear(input_width, output_width)
      )),
      residual(register_module(
          "residual",
          torch::nn::Linear(input_width, output_width)
      )) {
    attention_source = register_parameter(
        "attention_source",
        torch::empty({output_width})
    );
    attention_target = register_parameter(
        "attention_target",
        torch::empty({output_width})
    );
    torch::nn::init::xavier_uniform_(attention_source.unsqueeze(0));
    torch::nn::init::xavier_uniform_(attention_target.unsqueeze(0));
}

torch::Tensor DenseGatLayerImpl::forward(const torch::Tensor& input) {
    const torch::Tensor hidden = projection(input);
    const torch::Tensor source =
        (hidden * attention_source).sum(-1);
    const torch::Tensor target =
        (hidden * attention_target).sum(-1);
    torch::Tensor logits = torch::leaky_relu(
        source.unsqueeze(2) + target.unsqueeze(1),
        0.2
    );
    const auto nodes = input.size(1);
    const torch::Tensor eye = torch::eye(
        nodes,
        torch::TensorOptions()
            .dtype(torch::kBool)
            .device(input.device())
    ).unsqueeze(0);
    logits = logits.masked_fill(eye, -1.0e9);
    const torch::Tensor attention = torch::softmax(logits, 1);
    return torch::bmm(attention.transpose(1, 2), hidden)
        + residual(input);
}

ConditionalGatDenoiserImpl::ConditionalGatDenoiserImpl(ModelConfig config)
    : config_(config),
      layout_projection(register_module(
          "layout_projection",
          torch::nn::Linear(2, config.time_width)
      )),
      score_projection(register_module(
          "score_projection",
          torch::nn::Linear(1, config.time_width)
      )),
      time_hidden(register_module(
          "time_hidden",
          torch::nn::Linear(config.fourier_width + 1, config.time_width)
      )),
      time_output(register_module(
          "time_output",
          torch::nn::Linear(config.time_width, config.time_width)
      )),
      wind_speed_hidden(register_module(
          "wind_speed_hidden",
          torch::nn::Linear(1, config.time_width)
      )),
      wind_speed_output(register_module(
          "wind_speed_output",
          torch::nn::Linear(config.time_width, config.time_width)
      )),
      wind_direction_hidden(register_module(
          "wind_direction_hidden",
          torch::nn::Linear(1, config.time_width)
      )),
      wind_direction_output(register_module(
          "wind_direction_output",
          torch::nn::Linear(config.time_width, config.time_width)
      )),
      gat_layers(register_module(
          "gat_layers",
          torch::nn::ModuleList()
      )),
      decoder_hidden(register_module(
          "decoder_hidden",
          torch::nn::Linear(config.hidden_width, config.hidden_width)
      )),
      decoder_output(register_module(
          "decoder_output",
          torch::nn::Linear(config.hidden_width, 2)
      )),
      mlp_layout_projection(register_module(
          "mlp_layout_projection",
          torch::nn::Linear(
              2 * config.turbine_count,
              config.time_width
          )
      )),
      mlp_input(register_module(
          "mlp_input",
          torch::nn::Linear(config.time_width, config.hidden_width)
      )),
      mlp_residual_layers(register_module(
          "mlp_residual_layers",
          torch::nn::ModuleList()
      )),
      mlp_output(register_module(
          "mlp_output",
          torch::nn::Linear(
              config.hidden_width,
              2 * config.turbine_count
          )
      )) {
    if (
        config.layers <= 0 || config.hidden_width <= 0
        || config.time_width <= 0 || config.fourier_width <= 0
        || (config.fourier_width % 2) != 0
        || config.turbine_count <= 1
    ) {
        throw std::invalid_argument("invalid T27 denoiser configuration");
    }
    fourier_weights = register_buffer(
        "fourier_weights",
        torch::randn({config.fourier_width / 2})
    );
    gat_layers->push_back(
        DenseGatLayer(config.time_width, config.hidden_width)
    );
    for (int layer = 1; layer < config.layers; ++layer) {
        gat_layers->push_back(
            DenseGatLayer(config.hidden_width, config.hidden_width)
        );
    }
    for (int layer = 0; layer < config.layers; ++layer) {
        mlp_residual_layers->push_back(
            torch::nn::Linear(config.hidden_width, config.hidden_width)
        );
    }
}

const ModelConfig& ConditionalGatDenoiserImpl::config() const noexcept {
    return config_;
}

torch::Tensor ConditionalGatDenoiserImpl::forward(
    const torch::Tensor& layouts,
    const torch::Tensor& noise_time,
    const torch::Tensor& score_condition,
    const torch::Tensor& wind_condition
) {
    const auto batch = layouts.size(0);
    torch::Tensor frequency =
        noise_time.reshape({batch, 1}) * fourier_weights.reshape({1, -1})
        * (2.0 * pi);
    torch::Tensor time_features = torch::cat(
        {
            noise_time.reshape({batch, 1}),
            torch::sin(frequency),
            torch::cos(frequency),
        },
        1
    );
    torch::Tensor time_embedding = time_output(
        torch::silu(time_hidden(time_features))
    );
    torch::Tensor wind_embedding =
        wind_speed_output(torch::silu(
            wind_speed_hidden(wind_condition.slice(1, 0, 1))
        ))
        + wind_direction_output(torch::silu(
            wind_direction_hidden(wind_condition.slice(1, 1, 2))
        ));
    if (config_.architecture == ArchitectureProfile::mlp) {
        if (layouts.size(1) != config_.turbine_count) {
            throw std::invalid_argument(
                "T27 MLP turbine count differs from trained shape"
            );
        }
        torch::Tensor hidden = activate(
            mlp_input(
                mlp_layout_projection(layouts.flatten(1))
                + score_projection(score_condition)
                + time_embedding + wind_embedding
            ),
            config_.activation
        );
        for (const auto& module : *mlp_residual_layers) {
            hidden = hidden
                + module->as<torch::nn::Linear>()->forward(
                    activate(hidden, config_.activation)
                );
        }
        return mlp_output(activate(hidden, config_.activation))
            .reshape({batch, config_.turbine_count, 2});
    }
    torch::Tensor hidden =
        layout_projection(layouts)
        + score_projection(score_condition).unsqueeze(1)
        + (time_embedding + wind_embedding).unsqueeze(1);
    for (const auto& module : *gat_layers) {
        hidden = activate(
            module->as<DenseGatLayer>()->forward(hidden),
            config_.activation
        );
    }
    return decoder_output(
        activate(decoder_hidden(hidden), config_.activation)
    );
}

ConditionalEdm::ConditionalEdm(
    ConditionalGatDenoiser model,
    EdmConfig config
) : model_(std::move(model)), config_(config) {}

ConditionalGatDenoiser& ConditionalEdm::model() noexcept {
    return model_;
}

torch::Tensor ConditionalEdm::denoise(
    const torch::Tensor& value,
    const torch::Tensor& sigma,
    const torch::Tensor& score_condition,
    const torch::Tensor& wind_condition
) {
    const torch::Tensor padded = expand_like_batch(sigma, value);
    const double data2 = config_.sigma_data * config_.sigma_data;
    const torch::Tensor denominator = padded * padded + data2;
    const torch::Tensor c_skip = data2 / denominator;
    const torch::Tensor c_out =
        padded * config_.sigma_data / torch::sqrt(denominator);
    const torch::Tensor c_in = 1.0 / torch::sqrt(denominator);
    const torch::Tensor c_noise = 0.25 * torch::log(sigma);
    return c_skip * value
        + c_out * model_->forward(
            c_in * value,
            c_noise,
            score_condition,
            wind_condition
        );
}

torch::Tensor ConditionalEdm::training_loss(
    const torch::Tensor& layouts,
    const torch::Tensor& score_condition,
    const torch::Tensor& wind_condition,
    double condition_drop_probability
) {
    const auto batch = layouts.size(0);
    const torch::Tensor sigma = torch::exp(
        config_.p_mean + config_.p_std * torch::randn(
            {batch},
            layouts.options()
        )
    );
    const torch::Tensor padded = expand_like_batch(sigma, layouts);
    torch::Tensor condition = score_condition;
    if (condition_drop_probability > 0.0) {
        const torch::Tensor keep = (
            torch::rand_like(condition) >= condition_drop_probability
        ).to(condition.scalar_type());
        condition = condition * keep;
    }
    const torch::Tensor noised =
        layouts + padded * torch::randn_like(layouts);
    const torch::Tensor predicted =
        denoise(noised, sigma, condition, wind_condition);
    const torch::Tensor weight =
        (sigma * sigma + config_.sigma_data * config_.sigma_data)
        / (
            sigma * sigma
            * config_.sigma_data * config_.sigma_data
        );
    return (
        torch::mse_loss(predicted, layouts, torch::Reduction::None)
            .mean({1, 2})
        * weight
    ).mean();
}

torch::Tensor ConditionalEdm::sample(
    int batch_size,
    int turbine_count,
    const torch::Tensor& score_condition,
    const torch::Tensor& wind_condition,
    int sample_steps
) {
    torch::NoGradGuard guard;
    const int steps = sample_steps > 0 ? sample_steps : config_.sample_steps;
    const auto options = score_condition.options();
    const torch::Tensor indices = torch::arange(steps, options);
    const double inverse_rho = 1.0 / config_.rho;
    torch::Tensor sigmas = torch::pow(
        std::pow(config_.sigma_max, inverse_rho)
            + indices / static_cast<double>(steps - 1)
                * (
                    std::pow(config_.sigma_min, inverse_rho)
                    - std::pow(config_.sigma_max, inverse_rho)
                ),
        config_.rho
    );
    sigmas = torch::cat({sigmas, torch::zeros({1}, options)}, 0);
    torch::Tensor value =
        sigmas[0] * torch::randn(
            {batch_size, turbine_count, 2},
            options
        );
    const torch::Tensor unconditional =
        torch::zeros_like(score_condition);
    for (int step = 0; step < steps; ++step) {
        const double sigma = sigmas[step].item<double>();
        const double next_sigma = sigmas[step + 1].item<double>();
        const double gamma =
            sigma >= config_.churn_min && sigma <= config_.churn_max
                ? std::min(
                    config_.churn / static_cast<double>(steps),
                    std::sqrt(2.0) - 1.0
                )
                : 0.0;
        const double sigma_hat = sigma * (1.0 + gamma);
        torch::Tensor perturbed = value;
        if (gamma > 0.0) {
            perturbed = perturbed
                + std::sqrt(sigma_hat * sigma_hat - sigma * sigma)
                    * config_.sampling_noise * torch::randn_like(value);
        }
        const torch::Tensor sigma_tensor = torch::full(
            {batch_size},
            sigma_hat,
            options
        );
        const torch::Tensor unconditioned = denoise(
            perturbed,
            sigma_tensor,
            unconditional,
            wind_condition
        );
        const torch::Tensor conditioned = denoise(
            perturbed,
            sigma_tensor,
            score_condition,
            wind_condition
        );
        const torch::Tensor guided =
            unconditioned
            + config_.guidance * (conditioned - unconditioned);
        const torch::Tensor derivative =
            (perturbed - guided) / sigma_hat;
        torch::Tensor next =
            perturbed + (next_sigma - sigma_hat) * derivative;
        if (next_sigma > 0.0) {
            const torch::Tensor corrected = denoise(
                next,
                torch::full({batch_size}, next_sigma, options),
                score_condition,
                wind_condition
            );
            const torch::Tensor next_derivative =
                (next - corrected) / next_sigma;
            next = perturbed
                + 0.5 * (next_sigma - sigma_hat)
                    * (derivative + next_derivative);
        }
        value = next;
    }
    return torch::clamp(value, -1.0, 1.0);
}

torch::Tensor repair_spacing(
    torch::Tensor layouts,
    double side_length_m,
    double minimum_spacing_m,
    int steps,
    double learning_rate
) {
    if (steps <= 0) return torch::clamp(layouts, 0.0, 1.0);
    layouts = layouts.detach().clone().set_requires_grad(true);
    torch::optim::Adam optimizer(
        std::vector<torch::Tensor>{layouts},
        torch::optim::AdamOptions(learning_rate)
    );
    const auto nodes = layouts.size(1);
    const torch::Tensor diagonal = torch::eye(
        nodes,
        torch::TensorOptions()
            .dtype(torch::kBool)
            .device(layouts.device())
    ).unsqueeze(0);
    for (int step = 0; step < steps; ++step) {
        optimizer.zero_grad();
        const torch::Tensor distances =
            torch::cdist(layouts * side_length_m, layouts * side_length_m);
        torch::Tensor violations = torch::relu(
            minimum_spacing_m - distances
        ).masked_fill(diagonal, 0.0);
        const torch::Tensor spacing =
            torch::triu(violations, 1).sum()
            / static_cast<double>(layouts.size(0));
        const torch::Tensor boundary =
            (
                torch::relu(-layouts)
                + torch::relu(layouts - 1.0)
            ).sum() / static_cast<double>(layouts.size(0));
        const torch::Tensor loss = spacing + boundary;
        if (loss.item<double>() <= 1.0e-8) break;
        loss.backward();
        optimizer.step();
    }
    return torch::clamp(layouts.detach(), 0.0, 1.0);
}

RunResult run(
    const ProtocolConfig& protocol,
    torch::Device device,
    ModelConfig model_config,
    EdmConfig edm_config
) {
    if (
        protocol.turbine_count <= 1
        || protocol.initial_layouts <= 0
        || protocol.rounds < 0
        || protocol.training_steps_per_round < 0
        || protocol.batch_size <= 0
    ) {
        throw std::invalid_argument("invalid T27 protocol");
    }
    at::set_num_threads(std::max(1, protocol.workers));
    torch::manual_seed(static_cast<std::int64_t>(protocol.seed));
    if (device.is_cuda()) torch::cuda::manual_seed_all(protocol.seed);
    const auto begin = Clock::now();
    RunResult result;
    result.backend = device.is_cuda() ? "cuda" : "cpu";
    result.observed_cpu_threads = at::get_num_threads();

    const auto data_begin = Clock::now();
    torch::Tensor layouts = torch::rand(
        {
            protocol.initial_layouts,
            protocol.turbine_count,
            2,
        },
        torch::TensorOptions().dtype(torch::kFloat32).device(device)
    );
    layouts = repair_spacing(
        layouts,
        protocol.side_length_m,
        2.0 * 126.0,
        protocol.repair_steps,
        protocol.repair_learning_rate
    );
    result.data_generation_seconds = elapsed(data_begin);

    auto tensor_to_layouts = [&](const torch::Tensor& values) {
        const torch::Tensor cpu =
            values.detach().to(torch::kCPU).contiguous();
        const auto accessor = cpu.accessor<float, 3>();
        std::vector<Layout> converted(
            static_cast<std::size_t>(cpu.size(0))
        );
        for (std::int64_t batch = 0; batch < cpu.size(0); ++batch) {
            auto& layout = converted[static_cast<std::size_t>(batch)];
            layout.x_unit.resize(
                static_cast<std::size_t>(cpu.size(1))
            );
            layout.y_unit.resize(
                static_cast<std::size_t>(cpu.size(1))
            );
            for (std::int64_t node = 0; node < cpu.size(1); ++node) {
                layout.x_unit[static_cast<std::size_t>(node)] =
                    accessor[batch][node][0];
                layout.y_unit[static_cast<std::size_t>(node)] =
                    accessor[batch][node][1];
            }
        }
        return converted;
    };
    Floris411Gch evaluator(protocol.workers);
    const auto initial_evaluation_begin = Clock::now();
    std::vector<Layout> layout_values = tensor_to_layouts(layouts);
    std::vector<WindScenario> initial_winds(
        layout_values.size(),
        protocol.wind
    );
    if (protocol.diverse_wind_training) {
        std::mt19937_64 wind_rng(protocol.seed ^ 0x27d1ffULL);
        std::uniform_real_distribution<double> speed(
            protocol.training_wind_speed_min,
            protocol.training_wind_speed_max
        );
        std::uniform_real_distribution<double> direction(
            protocol.training_wind_direction_min,
            protocol.training_wind_direction_max
        );
        for (auto& wind : initial_winds) {
            wind = {speed(wind_rng), direction(wind_rng)};
        }
    }
    std::vector<Evaluation> evaluations = evaluator.evaluate_batch(
        layout_values,
        protocol.side_length_m,
        initial_winds
    );
    result.evaluation_seconds += elapsed(initial_evaluation_begin);
    result.physical_layout_evaluations += evaluations.size();
    std::vector<float> scores(evaluations.size());
    std::size_t best_index = 0;
    for (std::size_t index = 0; index < evaluations.size(); ++index) {
        scores[index] = static_cast<float>(
            evaluations[index].annual_energy_mwh
        );
        if (scores[index] > scores[best_index]) best_index = index;
    }
    result.best_aep_mwh = scores[best_index];
    result.initial_best_aep_mwh = result.best_aep_mwh;
    result.best_layout = layout_values[best_index];
    torch::Tensor score_tensor = torch::from_blob(
        scores.data(),
        {static_cast<std::int64_t>(scores.size()), 1},
        torch::TensorOptions().dtype(torch::kFloat32)
    ).clone().to(device);
    std::vector<float> wind_values(initial_winds.size() * 2);
    for (std::size_t index = 0; index < initial_winds.size(); ++index) {
        wind_values[2 * index] = static_cast<float>(
            initial_winds[index].speed_mps / 10.0
        );
        wind_values[2 * index + 1] = static_cast<float>(
            initial_winds[index].direction_degrees / 180.0
        );
    }
    torch::Tensor wind_tensor = torch::from_blob(
        wind_values.data(),
        {static_cast<std::int64_t>(initial_winds.size()), 2},
        torch::TensorOptions().dtype(torch::kFloat32)
    ).clone().to(device);

    auto model = ConditionalGatDenoiser(model_config);
    model->to(device);
    auto ema = ConditionalGatDenoiser(model_config);
    ema->to(device);
    update_ema(*ema, *model, 0.0);
    ConditionalEdm edm(model, edm_config);
    ConditionalEdm ema_edm(ema, edm_config);
    torch::optim::AdamW optimizer(
        model->parameters(),
        torch::optim::AdamWOptions(protocol.learning_rate)
            .betas(std::make_tuple(0.9, 0.99))
    );

    for (int round = 0; round < protocol.rounds; ++round) {
        const auto training_begin = Clock::now();
        const torch::Tensor minimum = score_tensor.min();
        const torch::Tensor maximum = score_tensor.max();
        const torch::Tensor normalized_scores =
            (score_tensor - minimum) / (maximum - minimum + 1.0e-7);
        for (
            int step = 0;
            step < protocol.training_steps_per_round;
            ++step
        ) {
            const torch::Tensor indices = torch::randint(
                layouts.size(0),
                {protocol.batch_size},
                torch::TensorOptions().dtype(torch::kInt64).device(device)
            );
            optimizer.zero_grad();
            const torch::Tensor loss = edm.training_loss(
                layouts.index_select(0, indices) * 2.0 - 1.0,
                normalized_scores.index_select(0, indices),
                wind_tensor.index_select(0, indices),
                protocol.condition_drop_probability
            );
            loss.backward();
            torch::nn::utils::clip_grad_norm_(model->parameters(), 1.0);
            optimizer.step();
            ++result.optimizer_steps;
            if (
                (step + 1) % protocol.ema_update_every == 0
            ) {
                update_ema(*ema, *model, protocol.ema_decay);
            }
        }
        result.training_seconds += elapsed(training_begin);

        const auto sample_begin = Clock::now();
        const int generation_batch = protocol.generated_per_round;
        const torch::Tensor generated_condition = torch::ones(
            {generation_batch, 1},
            layouts.options()
        );
        const torch::Tensor generated_wind = torch::tensor(
            {
                static_cast<float>(protocol.wind.speed_mps / 10.0),
                static_cast<float>(
                    protocol.wind.direction_degrees / 180.0
                ),
            },
            layouts.options()
        ).repeat({generation_batch, 1});
        torch::Tensor generated = (
            ema_edm.sample(
                generation_batch,
                protocol.turbine_count,
                generated_condition,
                generated_wind
            ) + 1.0
        ) * 0.5;
        generated = repair_spacing(
            generated,
            protocol.side_length_m,
            2.0 * 126.0,
            protocol.repair_steps,
            protocol.repair_learning_rate
        );
        result.sampling_repair_seconds += elapsed(sample_begin);

        const auto evaluation_begin = Clock::now();
        std::vector<Layout> generated_values =
            tensor_to_layouts(generated);
        std::vector<Evaluation> generated_evaluations =
            evaluator.evaluate_batch(
                generated_values,
                protocol.side_length_m,
                protocol.wind
            );
        result.evaluation_seconds += elapsed(evaluation_begin);
        result.physical_layout_evaluations +=
            generated_evaluations.size();
        std::vector<float> generated_scores(
            generated_evaluations.size()
        );
        for (
            std::size_t index = 0;
            index < generated_evaluations.size();
            ++index
        ) {
            generated_scores[index] = static_cast<float>(
                generated_evaluations[index].annual_energy_mwh
            );
            if (generated_scores[index] > result.best_aep_mwh) {
                result.best_aep_mwh = generated_scores[index];
                result.best_layout = generated_values[index];
            }
        }
        const torch::Tensor appended_scores = torch::from_blob(
            generated_scores.data(),
            {
                static_cast<std::int64_t>(generated_scores.size()),
                1,
            },
            torch::TensorOptions().dtype(torch::kFloat32)
        ).clone().to(device);
        layouts = torch::cat({layouts, generated}, 0);
        score_tensor = torch::cat({score_tensor, appended_scores}, 0);
        wind_tensor = torch::cat({wind_tensor, generated_wind}, 0);
        ++result.completed_rounds;
    }
    if (!protocol.transfer_turbine_counts.empty()) {
        if (model_config.architecture != ArchitectureProfile::gnn) {
            throw std::invalid_argument(
                "T27 zero-shot transfer requires size-invariant GNN"
            );
        }
        for (const int turbine_count : protocol.transfer_turbine_counts) {
            if (turbine_count <= 1) {
                throw std::invalid_argument(
                    "invalid T27 transfer turbine count"
                );
            }
            const int generated_count =
                std::max(1, protocol.generated_per_round);
            const torch::Tensor condition = torch::ones(
                {generated_count,1},
                layouts.options()
            );
            const torch::Tensor wind = torch::tensor(
                {
                    static_cast<float>(protocol.wind.speed_mps / 10.0),
                    static_cast<float>(
                        protocol.wind.direction_degrees / 180.0
                    ),
                },
                layouts.options()
            ).repeat({generated_count,1});
            const auto sample_begin = Clock::now();
            torch::Tensor generated = (
                ema_edm.sample(
                    generated_count,
                    turbine_count,
                    condition,
                    wind
                ) + 1.0
            ) * 0.5;
            const double transfer_side_length =
                protocol.side_length_m
                * static_cast<double>(turbine_count)
                / static_cast<double>(protocol.turbine_count);
            generated = repair_spacing(
                generated,
                transfer_side_length,
                2.0 * 126.0,
                protocol.repair_steps,
                protocol.repair_learning_rate
            );
            result.sampling_repair_seconds += elapsed(sample_begin);
            const auto evaluation_begin = Clock::now();
            const std::vector<Layout> transfer_layouts =
                tensor_to_layouts(generated);
            const auto transfer_evaluations = evaluator.evaluate_batch(
                transfer_layouts,
                transfer_side_length,
                protocol.wind
            );
            result.evaluation_seconds += elapsed(evaluation_begin);
            double best = 0.0;
            for (const auto& item : transfer_evaluations) {
                best = std::max(best, item.annual_energy_mwh);
            }
            result.physical_layout_evaluations +=
                transfer_evaluations.size();
            result.transfer.push_back({
                turbine_count,
                best,
                static_cast<std::uint64_t>(
                    transfer_evaluations.size()
                ),
            });
        }
    }
    result.end_to_end_seconds = elapsed(begin);
    return result;
}

std::string activation_profile_name(ActivationProfile profile) {
    return profile == ActivationProfile::paper_leaky_relu
        ? "paper_leaky_relu"
        : "source_gelu";
}

std::string architecture_profile_name(ArchitectureProfile profile) {
    return profile == ArchitectureProfile::gnn ? "gnn" : "mlp";
}

}  // namespace core99::t27
