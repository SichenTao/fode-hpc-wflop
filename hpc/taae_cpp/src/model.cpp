/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: TAAE declared-reconstruction trainable Transformer kernel
Paper title: Transformer Autoencoder-Assisted Evolutionary Framework for Constrained Multiobjective 3D Wind Farm Layout Optimization
DOI: 10.1109/JAS.2026.126233
Public author method source/checkpoint: unavailable as recorded in docs/source-dossiers/Y36.json
Missing choices completed here: FFN width 256, post-norm, zero dropout, Xavier-uniform initialization, mean encoder pooling, separate encoder/decoder embeddings, deterministic metric-pair seed, per-parameter Adam age, and checkpoint format
Reconstruction status: engineering reconstruction with declared completion choices
Method evidence tier: M3_DECLARED_COMPLETION
Method semantic ID: taae_transformer_declared_reconstruction_v1
Controlling contract: shared/contracts/taae_transformer_declared_reconstruction_contract.json
Claim boundary: trainable mathematical kernel only; end-to-end M3 method is not admitted, original taae remains blocked, and no author-result, optimizer, performance, or GPU claim is made
END WFLOP IMPLEMENTATION FACT DECLARATION
*/

#include "taae/model.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace taae {
namespace {

struct Matrix {
    std::size_t rows = 0;
    std::size_t cols = 0;
    std::vector<double> values;

    Matrix() = default;
    Matrix(std::size_t row_count, std::size_t column_count, double value = 0.0)
        : rows(row_count),
          cols(column_count),
          values(row_count * column_count, value) {}

    double& operator()(std::size_t row, std::size_t column) {
        return values.at(row * cols + column);
    }
    const double& operator()(std::size_t row, std::size_t column) const {
        return values.at(row * cols + column);
    }
};

void add_in_place(Matrix& target, const Matrix& addition) {
    if (target.rows != addition.rows || target.cols != addition.cols) {
        throw std::invalid_argument("matrix addition shape mismatch");
    }
    for (std::size_t index = 0; index < target.values.size(); ++index) {
        target.values[index] += addition.values[index];
    }
}

struct DeterministicRng {
    std::uint64_t state;

    explicit DeterministicRng(std::uint64_t seed) : state(seed) {}

    std::uint64_t next_u64() {
        state += 0x9e3779b97f4a7c15ULL;
        std::uint64_t value = state;
        value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31U);
    }

    double uniform(double low, double high) {
        constexpr double kDenominator =
            1.0 / static_cast<double>(std::uint64_t{1} << 53U);
        const double unit =
            static_cast<double>(next_u64() >> 11U) * kDenominator;
        return low + (high - low) * unit;
    }
};

struct Parameter {
    std::string name;
    std::vector<double> value;
    std::vector<double> gradient;
    std::vector<double> first_moment;
    std::vector<double> second_moment;
    std::uint64_t update_step = 0;
    bool decoder = false;

    Parameter(
        std::string parameter_name,
        std::size_t size,
        bool belongs_to_decoder
    )
        : name(std::move(parameter_name)),
          value(size, 0.0),
          gradient(size, 0.0),
          first_moment(size, 0.0),
          second_moment(size, 0.0),
          decoder(belongs_to_decoder) {}
};

void xavier_uniform(
    Parameter& parameter,
    std::size_t fan_in,
    std::size_t fan_out,
    DeterministicRng& rng
) {
    const double limit =
        std::sqrt(6.0 / static_cast<double>(fan_in + fan_out));
    for (double& value : parameter.value) {
        value = rng.uniform(-limit, limit);
    }
}

class ParameterRegistry {
public:
    Parameter& add(
        const std::string& name,
        std::size_t size,
        bool decoder = false
    ) {
        if (find(name) != nullptr) {
            throw std::invalid_argument("duplicate parameter " + name);
        }
        parameters_.push_back(
            std::make_unique<Parameter>(name, size, decoder)
        );
        return *parameters_.back();
    }

    Parameter* find(const std::string& name) {
        for (const auto& parameter : parameters_) {
            if (parameter->name == name) {
                return parameter.get();
            }
        }
        return nullptr;
    }

    const Parameter* find(const std::string& name) const {
        for (const auto& parameter : parameters_) {
            if (parameter->name == name) {
                return parameter.get();
            }
        }
        return nullptr;
    }

    std::vector<Parameter*> all() {
        std::vector<Parameter*> result;
        result.reserve(parameters_.size());
        for (const auto& parameter : parameters_) {
            result.push_back(parameter.get());
        }
        return result;
    }

    std::vector<const Parameter*> all() const {
        std::vector<const Parameter*> result;
        result.reserve(parameters_.size());
        for (const auto& parameter : parameters_) {
            result.push_back(parameter.get());
        }
        return result;
    }

    void zero_gradients() {
        for (const auto& parameter : parameters_) {
            std::fill(
                parameter->gradient.begin(),
                parameter->gradient.end(),
                0.0
            );
        }
    }

    std::string stable_hash() const {
        std::uint64_t hash = 14695981039346656037ULL;
        auto consume = [&](const unsigned char* bytes, std::size_t count) {
            for (std::size_t index = 0; index < count; ++index) {
                hash ^= bytes[index];
                hash *= 1099511628211ULL;
            }
        };
        for (const auto& parameter : parameters_) {
            consume(
                reinterpret_cast<const unsigned char*>(
                    parameter->name.data()
                ),
                parameter->name.size()
            );
            const unsigned char separator = 0;
            consume(&separator, 1);
            for (double value : parameter->value) {
                const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
                unsigned char bytes[8];
                for (std::size_t index = 0; index < 8; ++index) {
                    bytes[index] = static_cast<unsigned char>(
                        (bits >> (index * 8U)) & 0xffU
                    );
                }
                consume(bytes, 8);
            }
        }
        std::ostringstream stream;
        stream << "fnv1a64:" << std::hex << std::setfill('0')
               << std::setw(16) << hash;
        return stream.str();
    }

private:
    std::vector<std::unique_ptr<Parameter>> parameters_;
};

struct LayerNormCache {
    Matrix normalized;
    std::vector<double> inverse_standard_deviation;
};

Matrix layer_norm_forward(
    const Matrix& input,
    const std::vector<double>& scale,
    const std::vector<double>& shift,
    double epsilon,
    LayerNormCache& cache
) {
    if (input.cols == 0 || scale.size() != input.cols ||
        shift.size() != input.cols) {
        throw std::invalid_argument("invalid LayerNorm shape");
    }
    cache.normalized = Matrix(input.rows, input.cols);
    cache.inverse_standard_deviation.assign(input.rows, 0.0);
    Matrix output(input.rows, input.cols);
    const double width = static_cast<double>(input.cols);
    for (std::size_t row = 0; row < input.rows; ++row) {
        double mean = 0.0;
        for (std::size_t column = 0; column < input.cols; ++column) {
            mean += input(row, column);
        }
        mean /= width;
        double variance = 0.0;
        for (std::size_t column = 0; column < input.cols; ++column) {
            const double centered = input(row, column) - mean;
            variance += centered * centered;
        }
        variance /= width;
        const double inverse_std = 1.0 / std::sqrt(variance + epsilon);
        cache.inverse_standard_deviation[row] = inverse_std;
        for (std::size_t column = 0; column < input.cols; ++column) {
            const double normalized =
                (input(row, column) - mean) * inverse_std;
            cache.normalized(row, column) = normalized;
            output(row, column) =
                normalized * scale[column] + shift[column];
        }
    }
    return output;
}

Matrix layer_norm_backward(
    const Matrix& output_gradient,
    const std::vector<double>& scale,
    const LayerNormCache& cache,
    std::vector<double>& scale_gradient,
    std::vector<double>& shift_gradient
) {
    if (output_gradient.rows != cache.normalized.rows ||
        output_gradient.cols != cache.normalized.cols ||
        scale.size() != output_gradient.cols) {
        throw std::invalid_argument("invalid LayerNorm backward shape");
    }
    scale_gradient.assign(scale.size(), 0.0);
    shift_gradient.assign(scale.size(), 0.0);
    Matrix input_gradient(output_gradient.rows, output_gradient.cols);
    const double width = static_cast<double>(output_gradient.cols);
    for (std::size_t row = 0; row < output_gradient.rows; ++row) {
        double sum_scaled_gradient = 0.0;
        double sum_scaled_normalized_gradient = 0.0;
        for (std::size_t column = 0; column < output_gradient.cols; ++column) {
            const double gradient = output_gradient(row, column);
            scale_gradient[column] +=
                gradient * cache.normalized(row, column);
            shift_gradient[column] += gradient;
            const double scaled_gradient = gradient * scale[column];
            sum_scaled_gradient += scaled_gradient;
            sum_scaled_normalized_gradient +=
                scaled_gradient * cache.normalized(row, column);
        }
        for (std::size_t column = 0; column < output_gradient.cols; ++column) {
            const double scaled_gradient =
                output_gradient(row, column) * scale[column];
            input_gradient(row, column) =
                cache.inverse_standard_deviation[row] *
                (scaled_gradient - sum_scaled_gradient / width -
                 cache.normalized(row, column) *
                     sum_scaled_normalized_gradient / width);
        }
    }
    return input_gradient;
}

struct LinearCache {
    Matrix input;
};

Matrix linear_forward(
    const Matrix& input,
    const Parameter& weight,
    std::size_t output_width,
    LinearCache& cache
) {
    if (input.cols * output_width != weight.value.size()) {
        throw std::invalid_argument("linear forward shape mismatch");
    }
    cache.input = input;
    Matrix output(input.rows, output_width);
    for (std::size_t row = 0; row < input.rows; ++row) {
        for (std::size_t inner = 0; inner < input.cols; ++inner) {
            for (std::size_t column = 0; column < output_width; ++column) {
                output(row, column) +=
                    input(row, inner) *
                    weight.value[inner * output_width + column];
            }
        }
    }
    return output;
}

Matrix linear_backward(
    const Matrix& output_gradient,
    Parameter& weight,
    const LinearCache& cache
) {
    if (cache.input.rows != output_gradient.rows ||
        weight.value.size() != cache.input.cols * output_gradient.cols) {
        throw std::invalid_argument("linear backward shape mismatch");
    }
    Matrix input_gradient(cache.input.rows, cache.input.cols);
    for (std::size_t row = 0; row < cache.input.rows; ++row) {
        for (std::size_t inner = 0; inner < cache.input.cols; ++inner) {
            for (std::size_t column = 0;
                 column < output_gradient.cols;
                 ++column) {
                const std::size_t weight_index =
                    inner * output_gradient.cols + column;
                weight.gradient[weight_index] +=
                    cache.input(row, inner) *
                    output_gradient(row, column);
                input_gradient(row, inner) +=
                    output_gradient(row, column) *
                    weight.value[weight_index];
            }
        }
    }
    return input_gradient;
}

struct AttentionCache {
    Matrix query_input;
    Matrix key_value_input;
    Matrix query;
    Matrix key;
    Matrix value;
    Matrix probabilities;
    Matrix concatenated;
    LinearCache query_linear;
    LinearCache key_linear;
    LinearCache value_linear;
    LinearCache output_linear;
    bool causal = false;
};

struct Attention {
    std::size_t dimension;
    std::size_t heads;
    Parameter* query_weight;
    Parameter* key_weight;
    Parameter* value_weight;
    Parameter* output_weight;

    Attention(
        ParameterRegistry& registry,
        const std::string& prefix,
        std::size_t model_dimension,
        std::size_t head_count,
        DeterministicRng& rng,
        bool decoder
    )
        : dimension(model_dimension),
          heads(head_count),
          query_weight(
              &registry.add(prefix + ".query_weight",
                            model_dimension * model_dimension,
                            decoder)),
          key_weight(
              &registry.add(prefix + ".key_weight",
                            model_dimension * model_dimension,
                            decoder)),
          value_weight(
              &registry.add(prefix + ".value_weight",
                            model_dimension * model_dimension,
                            decoder)),
          output_weight(
              &registry.add(prefix + ".output_weight",
                            model_dimension * model_dimension,
                            decoder)) {
        if (dimension == 0 || heads == 0 || dimension % heads != 0) {
            throw std::invalid_argument("attention dimensions invalid");
        }
        xavier_uniform(*query_weight, dimension, dimension, rng);
        xavier_uniform(*key_weight, dimension, dimension, rng);
        xavier_uniform(*value_weight, dimension, dimension, rng);
        xavier_uniform(*output_weight, dimension, dimension, rng);
    }

    Matrix forward(
        const Matrix& query_input,
        const Matrix& key_value_input,
        bool causal,
        AttentionCache& cache
    ) const {
        cache.query_input = query_input;
        cache.key_value_input = key_value_input;
        cache.causal = causal;
        cache.query = linear_forward(
            query_input,
            *query_weight,
            dimension,
            cache.query_linear
        );
        cache.key = linear_forward(
            key_value_input,
            *key_weight,
            dimension,
            cache.key_linear
        );
        cache.value = linear_forward(
            key_value_input,
            *value_weight,
            dimension,
            cache.value_linear
        );
        const std::size_t head_dimension = dimension / heads;
        const std::size_t query_count = query_input.rows;
        const std::size_t key_count = key_value_input.rows;
        cache.probabilities =
            Matrix(heads * query_count, key_count);
        cache.concatenated = Matrix(query_count, dimension);
        const double scale =
            1.0 / std::sqrt(static_cast<double>(head_dimension));
        for (std::size_t head = 0; head < heads; ++head) {
            const std::size_t offset = head * head_dimension;
            for (std::size_t query_index = 0;
                 query_index < query_count;
                 ++query_index) {
                double maximum = -std::numeric_limits<double>::infinity();
                for (std::size_t key_index = 0;
                     key_index < key_count;
                     ++key_index) {
                    double score = 0.0;
                    if (!(causal && key_index > query_index)) {
                        for (std::size_t feature = 0;
                             feature < head_dimension;
                             ++feature) {
                            score += cache.query(
                                         query_index,
                                         offset + feature
                                     ) *
                                     cache.key(
                                         key_index,
                                         offset + feature
                                     );
                        }
                        score *= scale;
                    } else {
                        score = -std::numeric_limits<double>::infinity();
                    }
                    cache.probabilities(
                        head * query_count + query_index,
                        key_index
                    ) = score;
                    maximum = std::max(maximum, score);
                }
                double denominator = 0.0;
                for (std::size_t key_index = 0;
                     key_index < key_count;
                     ++key_index) {
                    double& probability = cache.probabilities(
                        head * query_count + query_index,
                        key_index
                    );
                    probability =
                        std::isfinite(probability)
                            ? std::exp(probability - maximum)
                            : 0.0;
                    denominator += probability;
                }
                for (std::size_t key_index = 0;
                     key_index < key_count;
                     ++key_index) {
                    double& probability = cache.probabilities(
                        head * query_count + query_index,
                        key_index
                    );
                    probability /= denominator;
                    for (std::size_t feature = 0;
                         feature < head_dimension;
                         ++feature) {
                        cache.concatenated(
                            query_index,
                            offset + feature
                        ) += probability *
                             cache.value(key_index, offset + feature);
                    }
                }
            }
        }
        return linear_forward(
            cache.concatenated,
            *output_weight,
            dimension,
            cache.output_linear
        );
    }

    std::pair<Matrix, Matrix> backward(
        const Matrix& output_gradient,
        AttentionCache& cache
    ) {
        Matrix concatenated_gradient = linear_backward(
            output_gradient,
            *output_weight,
            cache.output_linear
        );
        Matrix query_gradient(cache.query.rows, dimension);
        Matrix key_gradient(cache.key.rows, dimension);
        Matrix value_gradient(cache.value.rows, dimension);
        const std::size_t head_dimension = dimension / heads;
        const std::size_t query_count = cache.query.rows;
        const std::size_t key_count = cache.key.rows;
        const double scale =
            1.0 / std::sqrt(static_cast<double>(head_dimension));
        for (std::size_t head = 0; head < heads; ++head) {
            const std::size_t offset = head * head_dimension;
            for (std::size_t query_index = 0;
                 query_index < query_count;
                 ++query_index) {
                std::vector<double> probability_gradient(key_count, 0.0);
                for (std::size_t key_index = 0;
                     key_index < key_count;
                     ++key_index) {
                    for (std::size_t feature = 0;
                         feature < head_dimension;
                         ++feature) {
                        const double gradient = concatenated_gradient(
                            query_index,
                            offset + feature
                        );
                        probability_gradient[key_index] +=
                            gradient *
                            cache.value(key_index, offset + feature);
                        value_gradient(key_index, offset + feature) +=
                            cache.probabilities(
                                head * query_count + query_index,
                                key_index
                            ) *
                            gradient;
                    }
                }
                double softmax_projection = 0.0;
                for (std::size_t key_index = 0;
                     key_index < key_count;
                     ++key_index) {
                    softmax_projection +=
                        probability_gradient[key_index] *
                        cache.probabilities(
                            head * query_count + query_index,
                            key_index
                        );
                }
                for (std::size_t key_index = 0;
                     key_index < key_count;
                     ++key_index) {
                    const double score_gradient =
                        cache.probabilities(
                            head * query_count + query_index,
                            key_index
                        ) *
                        (probability_gradient[key_index] -
                         softmax_projection);
                    for (std::size_t feature = 0;
                         feature < head_dimension;
                         ++feature) {
                        query_gradient(query_index, offset + feature) +=
                            score_gradient *
                            cache.key(key_index, offset + feature) * scale;
                        key_gradient(key_index, offset + feature) +=
                            score_gradient *
                            cache.query(query_index, offset + feature) *
                            scale;
                    }
                }
            }
        }
        Matrix query_input_gradient = linear_backward(
            query_gradient,
            *query_weight,
            cache.query_linear
        );
        Matrix key_value_input_gradient = linear_backward(
            key_gradient,
            *key_weight,
            cache.key_linear
        );
        add_in_place(
            key_value_input_gradient,
            linear_backward(
                value_gradient,
                *value_weight,
                cache.value_linear
            )
        );
        return {query_input_gradient, key_value_input_gradient};
    }
};

struct FfnPostNormCache {
    Matrix input;
    Matrix hidden;
    Matrix activated;
    Matrix projected;
    LinearCache first_linear;
    LinearCache second_linear;
    LayerNormCache norm;
};

struct FfnPostNorm {
    std::size_t dimension;
    std::size_t hidden_width;
    Parameter* first_weight;
    Parameter* first_bias;
    Parameter* second_weight;
    Parameter* second_bias;
    Parameter* norm_scale;
    Parameter* norm_shift;

    FfnPostNorm(
        ParameterRegistry& registry,
        const std::string& prefix,
        std::size_t model_dimension,
        std::size_t ffn_width,
        DeterministicRng& rng,
        bool decoder
    )
        : dimension(model_dimension),
          hidden_width(ffn_width),
          first_weight(
              &registry.add(prefix + ".first_weight",
                            model_dimension * ffn_width,
                            decoder)),
          first_bias(
              &registry.add(prefix + ".first_bias", ffn_width, decoder)),
          second_weight(
              &registry.add(prefix + ".second_weight",
                            ffn_width * model_dimension,
                            decoder)),
          second_bias(
              &registry.add(prefix + ".second_bias",
                            model_dimension,
                            decoder)),
          norm_scale(
              &registry.add(prefix + ".norm_scale",
                            model_dimension,
                            decoder)),
          norm_shift(
              &registry.add(prefix + ".norm_shift",
                            model_dimension,
                            decoder)) {
        xavier_uniform(
            *first_weight,
            model_dimension,
            ffn_width,
            rng
        );
        xavier_uniform(
            *second_weight,
            ffn_width,
            model_dimension,
            rng
        );
        std::fill(norm_scale->value.begin(), norm_scale->value.end(), 1.0);
    }

    Matrix forward(const Matrix& input, FfnPostNormCache& cache) const {
        cache.input = input;
        cache.hidden = linear_forward(
            input,
            *first_weight,
            hidden_width,
            cache.first_linear
        );
        for (std::size_t row = 0; row < cache.hidden.rows; ++row) {
            for (std::size_t column = 0;
                 column < cache.hidden.cols;
                 ++column) {
                cache.hidden(row, column) += first_bias->value[column];
            }
        }
        cache.activated = cache.hidden;
        for (double& value : cache.activated.values) {
            value = std::max(0.0, value);
        }
        cache.projected = linear_forward(
            cache.activated,
            *second_weight,
            dimension,
            cache.second_linear
        );
        for (std::size_t row = 0; row < cache.projected.rows; ++row) {
            for (std::size_t column = 0;
                 column < cache.projected.cols;
                 ++column) {
                cache.projected(row, column) +=
                    second_bias->value[column] + input(row, column);
            }
        }
        return layer_norm_forward(
            cache.projected,
            norm_scale->value,
            norm_shift->value,
            1.0e-5,
            cache.norm
        );
    }

    Matrix backward(
        const Matrix& output_gradient,
        FfnPostNormCache& cache
    ) {
        std::vector<double> scale_gradient;
        std::vector<double> shift_gradient;
        Matrix projected_gradient = layer_norm_backward(
            output_gradient,
            norm_scale->value,
            cache.norm,
            scale_gradient,
            shift_gradient
        );
        for (std::size_t index = 0; index < dimension; ++index) {
            norm_scale->gradient[index] += scale_gradient[index];
            norm_shift->gradient[index] += shift_gradient[index];
        }
        Matrix input_gradient = projected_gradient;
        for (std::size_t row = 0; row < projected_gradient.rows; ++row) {
            for (std::size_t column = 0;
                 column < projected_gradient.cols;
                 ++column) {
                second_bias->gradient[column] +=
                    projected_gradient(row, column);
            }
        }
        Matrix activated_gradient = linear_backward(
            projected_gradient,
            *second_weight,
            cache.second_linear
        );
        for (std::size_t index = 0;
             index < activated_gradient.values.size();
             ++index) {
            if (cache.hidden.values[index] <= 0.0) {
                activated_gradient.values[index] = 0.0;
            }
        }
        for (std::size_t row = 0; row < activated_gradient.rows; ++row) {
            for (std::size_t column = 0;
                 column < activated_gradient.cols;
                 ++column) {
                first_bias->gradient[column] +=
                    activated_gradient(row, column);
            }
        }
        add_in_place(
            input_gradient,
            linear_backward(
                activated_gradient,
                *first_weight,
                cache.first_linear
            )
        );
        return input_gradient;
    }
};

struct AttentionPostNormCache {
    AttentionCache attention;
    LayerNormCache norm;
};

struct AttentionPostNorm {
    Attention attention;
    Parameter* norm_scale;
    Parameter* norm_shift;

    AttentionPostNorm(
        ParameterRegistry& registry,
        const std::string& prefix,
        std::size_t dimension,
        std::size_t heads,
        DeterministicRng& rng,
        bool decoder
    )
        : attention(
              registry,
              prefix + ".attention",
              dimension,
              heads,
              rng,
              decoder
          ),
          norm_scale(
              &registry.add(prefix + ".norm_scale", dimension, decoder)),
          norm_shift(
              &registry.add(prefix + ".norm_shift", dimension, decoder)) {
        std::fill(norm_scale->value.begin(), norm_scale->value.end(), 1.0);
    }

    Matrix forward(
        const Matrix& query,
        const Matrix& key_value,
        bool causal,
        AttentionPostNormCache& cache
    ) const {
        Matrix residual = attention.forward(
            query,
            key_value,
            causal,
            cache.attention
        );
        add_in_place(residual, query);
        return layer_norm_forward(
            residual,
            norm_scale->value,
            norm_shift->value,
            1.0e-5,
            cache.norm
        );
    }

    std::pair<Matrix, Matrix> backward(
        const Matrix& output_gradient,
        AttentionPostNormCache& cache
    ) {
        std::vector<double> scale_gradient;
        std::vector<double> shift_gradient;
        Matrix residual_gradient = layer_norm_backward(
            output_gradient,
            norm_scale->value,
            cache.norm,
            scale_gradient,
            shift_gradient
        );
        for (std::size_t index = 0; index < norm_scale->value.size(); ++index) {
            norm_scale->gradient[index] += scale_gradient[index];
            norm_shift->gradient[index] += shift_gradient[index];
        }
        auto [query_gradient, key_value_gradient] =
            attention.backward(residual_gradient, cache.attention);
        add_in_place(query_gradient, residual_gradient);
        return {query_gradient, key_value_gradient};
    }
};

struct EncoderLayerCache {
    AttentionPostNormCache self_attention;
    FfnPostNormCache ffn;
};

struct EncoderLayer {
    AttentionPostNorm self_attention;
    FfnPostNorm ffn;

    EncoderLayer(
        ParameterRegistry& registry,
        const std::string& prefix,
        std::size_t dimension,
        std::size_t heads,
        std::size_t ffn_width,
        DeterministicRng& rng
    )
        : self_attention(
              registry,
              prefix + ".self",
              dimension,
              heads,
              rng,
              false
          ),
          ffn(
              registry,
              prefix + ".ffn",
              dimension,
              ffn_width,
              rng,
              false
          ) {}

    Matrix forward(const Matrix& input, EncoderLayerCache& cache) const {
        const Matrix attended = self_attention.forward(
            input,
            input,
            false,
            cache.self_attention
        );
        return ffn.forward(attended, cache.ffn);
    }

    Matrix backward(
        const Matrix& output_gradient,
        EncoderLayerCache& cache
    ) {
        const Matrix attended_gradient =
            ffn.backward(output_gradient, cache.ffn);
        auto [query_gradient, key_value_gradient] =
            self_attention.backward(
                attended_gradient,
                cache.self_attention
            );
        add_in_place(query_gradient, key_value_gradient);
        return query_gradient;
    }
};

struct DecoderLayerCache {
    AttentionPostNormCache self_attention;
    AttentionPostNormCache cross_attention;
    FfnPostNormCache ffn;
};

struct DecoderLayer {
    AttentionPostNorm self_attention;
    AttentionPostNorm cross_attention;
    FfnPostNorm ffn;

    DecoderLayer(
        ParameterRegistry& registry,
        const std::string& prefix,
        std::size_t dimension,
        std::size_t heads,
        std::size_t ffn_width,
        DeterministicRng& rng
    )
        : self_attention(
              registry,
              prefix + ".self",
              dimension,
              heads,
              rng,
              true
          ),
          cross_attention(
              registry,
              prefix + ".cross",
              dimension,
              heads,
              rng,
              true
          ),
          ffn(
              registry,
              prefix + ".ffn",
              dimension,
              ffn_width,
              rng,
              true
          ) {}

    Matrix forward(
        const Matrix& input,
        const Matrix& memory,
        DecoderLayerCache& cache
    ) const {
        const Matrix self_attended = self_attention.forward(
            input,
            input,
            true,
            cache.self_attention
        );
        const Matrix cross_attended = cross_attention.forward(
            self_attended,
            memory,
            false,
            cache.cross_attention
        );
        return ffn.forward(cross_attended, cache.ffn);
    }

    std::pair<Matrix, Matrix> backward(
        const Matrix& output_gradient,
        DecoderLayerCache& cache
    ) {
        const Matrix cross_gradient =
            ffn.backward(output_gradient, cache.ffn);
        auto [self_output_gradient, memory_gradient] =
            cross_attention.backward(
                cross_gradient,
                cache.cross_attention
            );
        auto [decoder_gradient, self_key_value_gradient] =
            self_attention.backward(
                self_output_gradient,
                cache.self_attention
            );
        add_in_place(decoder_gradient, self_key_value_gradient);
        return {decoder_gradient, memory_gradient};
    }
};

struct ModelSampleCache {
    std::vector<int> source_tokens;
    std::vector<int> decoder_tokens;
    Matrix encoder_embedding;
    std::vector<EncoderLayerCache> encoder_layers;
    Matrix encoder_output;
    Matrix pooled;
    LinearCache latent_linear;
    Matrix latent_raw;
    Matrix latent;
    double latent_inverse_norm = 0.0;
    LinearCache memory_linear;
    Matrix memory_seed;
    Matrix memory;
    Matrix decoder_embedding;
    std::vector<DecoderLayerCache> decoder_layers;
    Matrix decoder_output;
    LinearCache output_linear;
    Matrix logits;
    Matrix probabilities;
    Matrix regression_hidden;
    Matrix regression_activated;
};

Matrix softmax_rows(const Matrix& logits) {
    Matrix probabilities(logits.rows, logits.cols);
    for (std::size_t row = 0; row < logits.rows; ++row) {
        double maximum = -std::numeric_limits<double>::infinity();
        for (std::size_t column = 0; column < logits.cols; ++column) {
            maximum = std::max(maximum, logits(row, column));
        }
        double denominator = 0.0;
        for (std::size_t column = 0; column < logits.cols; ++column) {
            probabilities(row, column) =
                std::exp(logits(row, column) - maximum);
            denominator += probabilities(row, column);
        }
        for (std::size_t column = 0; column < logits.cols; ++column) {
            probabilities(row, column) /= denominator;
        }
    }
    return probabilities;
}

double cross_entropy(
    const Matrix& logits,
    const std::vector<int>& targets,
    Matrix* gradient
) {
    if (logits.rows != targets.size() || logits.cols == 0) {
        throw std::invalid_argument("cross entropy shape mismatch");
    }
    const Matrix probabilities = softmax_rows(logits);
    if (gradient != nullptr) {
        *gradient = probabilities;
    }
    double loss = 0.0;
    for (std::size_t row = 0; row < targets.size(); ++row) {
        const int target = targets[row];
        if (target < 0 ||
            static_cast<std::size_t>(target) >= logits.cols) {
            throw std::invalid_argument("cross entropy target out of range");
        }
        const std::size_t target_index = static_cast<std::size_t>(target);
        loss -= std::log(
            std::max(probabilities(row, target_index), 1.0e-300)
        );
        if (gradient != nullptr) {
            (*gradient)(row, target_index) -= 1.0;
        }
    }
    const double count = static_cast<double>(targets.size());
    if (gradient != nullptr) {
        for (double& value : gradient->values) {
            value /= count;
        }
    }
    return loss / count;
}

double regression_mse(
    const std::vector<double>& prediction,
    const std::vector<double>& target,
    std::vector<double>* gradient
) {
    if (prediction.size() != target.size() || prediction.empty()) {
        throw std::invalid_argument("regression MSE shape mismatch");
    }
    if (gradient != nullptr) {
        gradient->assign(prediction.size(), 0.0);
    }
    double loss = 0.0;
    const double count = static_cast<double>(prediction.size());
    for (std::size_t index = 0; index < prediction.size(); ++index) {
        const double difference = prediction[index] - target[index];
        loss += difference * difference;
        if (gradient != nullptr) {
            (*gradient)[index] = 2.0 * difference / count;
        }
    }
    return loss / count;
}

std::vector<double> l2_normalize(
    const std::vector<double>& input,
    double& inverse_norm
) {
    double squared_norm = 0.0;
    for (double value : input) {
        squared_norm += value * value;
    }
    inverse_norm = 1.0 / std::sqrt(squared_norm + 1.0e-12);
    std::vector<double> output = input;
    for (double& value : output) {
        value *= inverse_norm;
    }
    return output;
}

std::vector<double> l2_normalize_backward(
    const std::vector<double>& output_gradient,
    const std::vector<double>& normalized,
    double inverse_norm
) {
    if (output_gradient.size() != normalized.size()) {
        throw std::invalid_argument("L2 normalize backward shape mismatch");
    }
    double projection = 0.0;
    for (std::size_t index = 0; index < normalized.size(); ++index) {
        projection += output_gradient[index] * normalized[index];
    }
    std::vector<double> input_gradient(normalized.size(), 0.0);
    for (std::size_t index = 0; index < normalized.size(); ++index) {
        input_gradient[index] =
            inverse_norm *
            (output_gradient[index] - normalized[index] * projection);
    }
    return input_gradient;
}

double metric_alignment(
    const std::vector<std::vector<double>>& raw_latent,
    const std::vector<double>& target,
    std::uint64_t pair_seed,
    std::vector<std::vector<double>>* gradient
) {
    if (raw_latent.size() != target.size() || raw_latent.size() < 2) {
        if (gradient != nullptr) {
            gradient->assign(
                raw_latent.size(),
                raw_latent.empty()
                    ? std::vector<double>{}
                    : std::vector<double>(
                          raw_latent.front().size(),
                          0.0
                      )
            );
        }
        return 0.0;
    }
    const std::size_t dimension = raw_latent.front().size();
    std::vector<std::vector<double>> latent(raw_latent.size());
    std::vector<double> inverse_norm(raw_latent.size(), 0.0);
    for (std::size_t index = 0; index < raw_latent.size(); ++index) {
        const auto& value = raw_latent[index];
        if (value.size() != dimension) {
            throw std::invalid_argument("metric alignment shape mismatch");
        }
        latent[index] = l2_normalize(value, inverse_norm[index]);
    }
    std::vector<std::vector<double>> normalized_gradient(
        latent.size(),
        std::vector<double>(dimension, 0.0)
    );
    const std::size_t pair_count = latent.size();
    DeterministicRng pair_rng(
        pair_seed ^ (static_cast<std::uint64_t>(latent.size()) << 32U)
    );
    double loss = 0.0;
    for (std::size_t pair = 0; pair < pair_count; ++pair) {
        const std::size_t left = static_cast<std::size_t>(
            pair_rng.next_u64() %
            static_cast<std::uint64_t>(latent.size())
        );
        std::size_t right = static_cast<std::size_t>(
            pair_rng.next_u64() %
            static_cast<std::uint64_t>(latent.size() - 1)
        );
        if (right >= left) {
            ++right;
        }
        double squared_distance = 0.0;
        for (std::size_t feature = 0; feature < dimension; ++feature) {
            const double difference =
                latent[left][feature] - latent[right][feature];
            squared_distance += difference * difference;
        }
        const double distance =
            std::sqrt(squared_distance + 1.0e-12);
        const double target_distance =
            std::abs(target[left] - target[right]);
        const double error = distance - target_distance;
        loss += error * error;
        const double coefficient =
            2.0 * error /
            (static_cast<double>(pair_count) * distance);
        for (std::size_t feature = 0; feature < dimension; ++feature) {
            const double value = coefficient *
                (latent[left][feature] - latent[right][feature]);
            normalized_gradient[left][feature] += value;
            normalized_gradient[right][feature] -= value;
        }
    }
    if (gradient != nullptr) {
        gradient->resize(raw_latent.size());
        for (std::size_t index = 0; index < raw_latent.size(); ++index) {
            (*gradient)[index] = l2_normalize_backward(
                normalized_gradient[index],
                latent[index],
                inverse_norm[index]
            );
        }
    }
    return loss / static_cast<double>(pair_count);
}

double regression_head_forward(
    const std::vector<double>& normalized_latent,
    const Parameter& hidden_weight,
    const Parameter& hidden_bias,
    const Parameter& output_weight,
    const Parameter& output_bias,
    Matrix& hidden,
    Matrix& activated
) {
    const std::size_t hidden_width = hidden_bias.value.size();
    if (hidden_weight.value.size() !=
            normalized_latent.size() * hidden_width ||
        output_weight.value.size() != hidden_width ||
        output_bias.value.size() != 1) {
        throw std::invalid_argument("regression head shape mismatch");
    }
    hidden = Matrix(1, hidden_width);
    activated = Matrix(1, hidden_width);
    for (std::size_t unit = 0; unit < hidden_width; ++unit) {
        hidden(0, unit) = hidden_bias.value[unit];
        for (std::size_t feature = 0;
             feature < normalized_latent.size();
             ++feature) {
            hidden(0, unit) +=
                normalized_latent[feature] *
                hidden_weight.value[feature * hidden_width + unit];
        }
        activated(0, unit) = std::max(0.0, hidden(0, unit));
    }
    double prediction = output_bias.value[0];
    for (std::size_t unit = 0; unit < hidden_width; ++unit) {
        prediction +=
            activated(0, unit) * output_weight.value[unit];
    }
    return prediction;
}

std::vector<double> regression_head_backward(
    const std::vector<double>& normalized_latent,
    double prediction_gradient,
    const Matrix& hidden,
    const Matrix& activated,
    Parameter& hidden_weight,
    Parameter& hidden_bias,
    Parameter& output_weight,
    Parameter& output_bias
) {
    output_bias.gradient[0] += prediction_gradient;
    std::vector<double> latent_gradient(normalized_latent.size(), 0.0);
    for (std::size_t unit = 0; unit < activated.cols; ++unit) {
        output_weight.gradient[unit] +=
            prediction_gradient * activated(0, unit);
        double hidden_gradient =
            prediction_gradient * output_weight.value[unit];
        if (hidden(0, unit) <= 0.0) {
            hidden_gradient = 0.0;
        }
        hidden_bias.gradient[unit] += hidden_gradient;
        for (std::size_t feature = 0;
             feature < normalized_latent.size();
             ++feature) {
            hidden_weight.gradient[
                feature * activated.cols + unit
            ] += normalized_latent[feature] * hidden_gradient;
            latent_gradient[feature] +=
                hidden_weight.value[
                    feature * activated.cols + unit
                ] * hidden_gradient;
        }
    }
    return latent_gradient;
}

double dot(const Matrix& left, const Matrix& right) {
    if (left.rows != right.rows || left.cols != right.cols) {
        throw std::invalid_argument("dot shape mismatch");
    }
    double result = 0.0;
    for (std::size_t index = 0; index < left.values.size(); ++index) {
        result += left.values[index] * right.values[index];
    }
    return result;
}

bool close_gradient(
    double analytic,
    double numerical,
    double absolute_tolerance,
    double relative_tolerance
) {
    const double absolute_error = std::abs(analytic - numerical);
    const double scale =
        std::max({1.0, std::abs(analytic), std::abs(numerical)});
    return absolute_error <= absolute_tolerance ||
           absolute_error / scale <= relative_tolerance;
}

bool check_layer_norm(std::ostringstream& report) {
    constexpr double kStep = 1.0e-6;
    constexpr double kAbsoluteTolerance = 2.0e-7;
    constexpr double kRelativeTolerance = 2.0e-6;
    Matrix input(2, 4);
    input.values = {0.2, -0.7, 1.1, 0.4, -0.3, 0.8, 0.1, -1.2};
    std::vector<double> scale = {1.1, 0.7, -0.4, 1.3};
    std::vector<double> shift = {0.1, -0.2, 0.3, 0.05};
    Matrix upstream(2, 4);
    upstream.values = {0.3, -0.1, 0.8, -0.4, 0.2, 0.5, -0.7, 0.6};

    LayerNormCache cache;
    static_cast<void>(
        layer_norm_forward(input, scale, shift, 1.0e-5, cache)
    );
    std::vector<double> scale_gradient;
    std::vector<double> shift_gradient;
    const Matrix input_gradient = layer_norm_backward(
        upstream,
        scale,
        cache,
        scale_gradient,
        shift_gradient
    );
    auto objective = [&](const Matrix& candidate_input,
                         const std::vector<double>& candidate_scale,
                         const std::vector<double>& candidate_shift) {
        LayerNormCache candidate_cache;
        return dot(
            layer_norm_forward(
                candidate_input,
                candidate_scale,
                candidate_shift,
                1.0e-5,
                candidate_cache
            ),
            upstream
        );
    };
    double maximum_error = 0.0;
    for (std::size_t index = 0; index < input.values.size(); ++index) {
        Matrix plus = input;
        Matrix minus = input;
        plus.values[index] += kStep;
        minus.values[index] -= kStep;
        const double numerical =
            (objective(plus, scale, shift) -
             objective(minus, scale, shift)) /
            (2.0 * kStep);
        maximum_error =
            std::max(maximum_error, std::abs(input_gradient.values[index] -
                                             numerical));
        if (!close_gradient(
                input_gradient.values[index],
                numerical,
                kAbsoluteTolerance,
                kRelativeTolerance)) {
            report << "layer_norm_input_gradient_failed index=" << index;
            return false;
        }
    }
    for (std::size_t index = 0; index < scale.size(); ++index) {
        std::vector<double> plus = scale;
        std::vector<double> minus = scale;
        plus[index] += kStep;
        minus[index] -= kStep;
        const double numerical =
            (objective(input, plus, shift) -
             objective(input, minus, shift)) /
            (2.0 * kStep);
        maximum_error =
            std::max(maximum_error, std::abs(scale_gradient[index] -
                                             numerical));
        if (!close_gradient(
                scale_gradient[index],
                numerical,
                kAbsoluteTolerance,
                kRelativeTolerance)) {
            report << "layer_norm_scale_gradient_failed index=" << index;
            return false;
        }
    }
    for (std::size_t index = 0; index < shift.size(); ++index) {
        std::vector<double> plus = shift;
        std::vector<double> minus = shift;
        plus[index] += kStep;
        minus[index] -= kStep;
        const double numerical =
            (objective(input, scale, plus) -
             objective(input, scale, minus)) /
            (2.0 * kStep);
        maximum_error =
            std::max(maximum_error, std::abs(shift_gradient[index] -
                                             numerical));
        if (!close_gradient(
                shift_gradient[index],
                numerical,
                kAbsoluteTolerance,
                kRelativeTolerance)) {
            report << "layer_norm_shift_gradient_failed index=" << index;
            return false;
        }
    }
    report << std::scientific << std::setprecision(3)
           << "layer_norm_gradient_pass max_abs_error=" << maximum_error
           << " abs_tol=" << kAbsoluteTolerance
           << " rel_tol=" << kRelativeTolerance;
    return true;
}

bool check_parameter_foundation(std::ostringstream& report) {
    ParameterRegistry first;
    ParameterRegistry second;
    DeterministicRng first_rng(1234);
    DeterministicRng second_rng(1234);
    Parameter& first_weight = first.add("fixture.weight", 12);
    Parameter& second_weight = second.add("fixture.weight", 12);
    xavier_uniform(first_weight, 3, 4, first_rng);
    xavier_uniform(second_weight, 3, 4, second_rng);
    first.add("fixture.bias", 4);
    second.add("fixture.bias", 4);
    if (first_weight.value != second_weight.value ||
        first.stable_hash() != second.stable_hash()) {
        report << "parameter_foundation_failed deterministic replay";
        return false;
    }
    const std::string before = first.stable_hash();
    first_weight.value[0] += 1.0e-6;
    if (first.stable_hash() == before) {
        report << "parameter_foundation_failed hash sensitivity";
        return false;
    }
    report << "parameter_foundation_pass deterministic_xavier=pass "
           << "stable_hash=pass";
    return true;
}

bool check_attention(std::ostringstream& report) {
    constexpr double kStep = 1.0e-6;
    constexpr double kAbsoluteTolerance = 4.0e-7;
    constexpr double kRelativeTolerance = 4.0e-6;
    ParameterRegistry registry;
    DeterministicRng rng(91);
    Attention attention(registry, "attention", 8, 4, rng, false);
    Matrix input(3, 8);
    for (std::size_t index = 0; index < input.values.size(); ++index) {
        input.values[index] =
            std::sin(static_cast<double>(index + 1) * 0.37);
    }
    Matrix upstream(3, 8);
    for (std::size_t index = 0; index < upstream.values.size(); ++index) {
        upstream.values[index] =
            std::cos(static_cast<double>(index + 2) * 0.29);
    }
    registry.zero_gradients();
    AttentionCache cache;
    static_cast<void>(attention.forward(input, input, true, cache));
    auto [query_gradient, key_value_gradient] =
        attention.backward(upstream, cache);
    add_in_place(query_gradient, key_value_gradient);

    auto objective = [&](const Matrix& candidate, bool causal) {
        AttentionCache candidate_cache;
        return dot(
            attention.forward(
                candidate,
                candidate,
                causal,
                candidate_cache
            ),
            upstream
        );
    };
    double maximum_error = 0.0;
    for (std::size_t index = 0; index < input.values.size(); ++index) {
        Matrix plus = input;
        Matrix minus = input;
        plus.values[index] += kStep;
        minus.values[index] -= kStep;
        const double numerical =
            (objective(plus, true) - objective(minus, true)) /
            (2.0 * kStep);
        maximum_error = std::max(
            maximum_error,
            std::abs(query_gradient.values[index] - numerical)
        );
        if (!close_gradient(
                query_gradient.values[index],
                numerical,
                kAbsoluteTolerance,
                kRelativeTolerance)) {
            report << "attention_input_gradient_failed index=" << index;
            return false;
        }
    }
    const std::vector<Parameter*> representative = {
        attention.query_weight,
        attention.key_weight,
        attention.value_weight,
        attention.output_weight,
    };
    for (Parameter* parameter : representative) {
        const std::size_t index = parameter->value.size() / 3;
        const double analytic = parameter->gradient[index];
        parameter->value[index] += kStep;
        const double plus = objective(input, true);
        parameter->value[index] -= 2.0 * kStep;
        const double minus = objective(input, true);
        parameter->value[index] += kStep;
        const double numerical = (plus - minus) / (2.0 * kStep);
        maximum_error =
            std::max(maximum_error, std::abs(analytic - numerical));
        if (!close_gradient(
                analytic,
                numerical,
                kAbsoluteTolerance,
                kRelativeTolerance)) {
            report << "attention_weight_gradient_failed name="
                   << parameter->name;
            return false;
        }
    }

    AttentionCache causal_cache;
    const Matrix causal_before =
        attention.forward(input, input, true, causal_cache);
    Matrix future_changed = input;
    for (std::size_t column = 0; column < future_changed.cols; ++column) {
        future_changed(2, column) += 10.0 + static_cast<double>(column);
    }
    AttentionCache changed_cache;
    const Matrix causal_after =
        attention.forward(future_changed, future_changed, true, changed_cache);
    for (std::size_t column = 0; column < causal_before.cols; ++column) {
        if (std::abs(causal_before(0, column) - causal_after(0, column)) >
            1.0e-14) {
            report << "attention_causal_mask_failed";
            return false;
        }
    }
    AttentionCache noncausal_cache;
    const Matrix noncausal_before =
        attention.forward(input, input, false, noncausal_cache);
    AttentionCache noncausal_changed_cache;
    const Matrix noncausal_after = attention.forward(
        future_changed,
        future_changed,
        false,
        noncausal_changed_cache
    );
    bool noncausal_path_observed = false;
    for (std::size_t column = 0; column < noncausal_before.cols; ++column) {
        noncausal_path_observed =
            noncausal_path_observed ||
            std::abs(
                noncausal_before(0, column) -
                noncausal_after(0, column)
            ) > 1.0e-10;
    }
    if (!noncausal_path_observed) {
        report << "attention_noncausal_path_failed";
        return false;
    }
    report << std::scientific << std::setprecision(3)
           << "attention_gradient_pass heads=4 max_abs_error="
           << maximum_error << " abs_tol=" << kAbsoluteTolerance
           << " rel_tol=" << kRelativeTolerance
           << " causal_mask=pass noncausal=pass";
    return true;
}

bool check_ffn_post_norm(std::ostringstream& report) {
    constexpr double kStep = 1.0e-6;
    constexpr double kAbsoluteTolerance = 6.0e-7;
    constexpr double kRelativeTolerance = 6.0e-6;
    ParameterRegistry registry;
    DeterministicRng rng(707);
    FfnPostNorm block(registry, "ffn", 64, 256, rng, false);
    Matrix input(2, 64);
    Matrix upstream(2, 64);
    for (std::size_t index = 0; index < input.values.size(); ++index) {
        input.values[index] =
            0.2 + std::sin(static_cast<double>(index + 3) * 0.071);
        upstream.values[index] =
            std::cos(static_cast<double>(index + 5) * 0.053);
    }
    registry.zero_gradients();
    FfnPostNormCache cache;
    static_cast<void>(block.forward(input, cache));
    const Matrix input_gradient = block.backward(upstream, cache);
    auto objective = [&](const Matrix& candidate) {
        FfnPostNormCache candidate_cache;
        return dot(block.forward(candidate, candidate_cache), upstream);
    };
    double maximum_error = 0.0;
    for (const std::size_t index :
         std::vector<std::size_t>{0, 17, 63, 75, 127}) {
        Matrix plus = input;
        Matrix minus = input;
        plus.values[index] += kStep;
        minus.values[index] -= kStep;
        const double numerical =
            (objective(plus) - objective(minus)) / (2.0 * kStep);
        maximum_error = std::max(
            maximum_error,
            std::abs(input_gradient.values[index] - numerical)
        );
        if (!close_gradient(
                input_gradient.values[index],
                numerical,
                kAbsoluteTolerance,
                kRelativeTolerance)) {
            report << "ffn_post_norm_input_gradient_failed index=" << index;
            return false;
        }
    }
    const std::vector<Parameter*> representative = {
        block.first_weight,
        block.first_bias,
        block.second_weight,
        block.second_bias,
        block.norm_scale,
        block.norm_shift,
    };
    for (Parameter* parameter : representative) {
        const std::size_t index = parameter->value.size() / 2;
        const double analytic = parameter->gradient[index];
        parameter->value[index] += kStep;
        const double plus = objective(input);
        parameter->value[index] -= 2.0 * kStep;
        const double minus = objective(input);
        parameter->value[index] += kStep;
        const double numerical = (plus - minus) / (2.0 * kStep);
        maximum_error =
            std::max(maximum_error, std::abs(analytic - numerical));
        if (!close_gradient(
                analytic,
                numerical,
                kAbsoluteTolerance,
                kRelativeTolerance)) {
            report << "ffn_post_norm_weight_gradient_failed name="
                   << parameter->name;
            return false;
        }
    }
    report << std::scientific << std::setprecision(3)
           << "ffn_post_norm_gradient_pass shape=64x256x64 "
           << "max_abs_error=" << maximum_error
           << " abs_tol=" << kAbsoluteTolerance
           << " rel_tol=" << kRelativeTolerance;
    return true;
}

bool check_losses(std::ostringstream& report) {
    constexpr double kStep = 1.0e-6;
    constexpr double kAbsoluteTolerance = 3.0e-7;
    constexpr double kRelativeTolerance = 3.0e-6;
    double maximum_error = 0.0;

    Matrix logits(2, 3);
    logits.values = {0.2, -0.4, 0.9, -0.3, 0.8, 0.1};
    const std::vector<int> targets = {2, 1};
    Matrix cross_entropy_gradient;
    static_cast<void>(
        cross_entropy(logits, targets, &cross_entropy_gradient)
    );
    for (std::size_t index = 0; index < logits.values.size(); ++index) {
        Matrix plus = logits;
        Matrix minus = logits;
        plus.values[index] += kStep;
        minus.values[index] -= kStep;
        const double numerical =
            (cross_entropy(plus, targets, nullptr) -
             cross_entropy(minus, targets, nullptr)) /
            (2.0 * kStep);
        maximum_error = std::max(
            maximum_error,
            std::abs(cross_entropy_gradient.values[index] - numerical)
        );
        if (!close_gradient(
                cross_entropy_gradient.values[index],
                numerical,
                kAbsoluteTolerance,
                kRelativeTolerance)) {
            report << "cross_entropy_gradient_failed index=" << index;
            return false;
        }
    }

    std::vector<double> predictions = {0.3, -0.8, 1.2};
    const std::vector<double> regression_targets = {0.1, -0.2, 0.9};
    std::vector<double> regression_gradient;
    static_cast<void>(regression_mse(
        predictions,
        regression_targets,
        &regression_gradient
    ));
    for (std::size_t index = 0; index < predictions.size(); ++index) {
        std::vector<double> plus = predictions;
        std::vector<double> minus = predictions;
        plus[index] += kStep;
        minus[index] -= kStep;
        const double numerical =
            (regression_mse(plus, regression_targets, nullptr) -
             regression_mse(minus, regression_targets, nullptr)) /
            (2.0 * kStep);
        maximum_error = std::max(
            maximum_error,
            std::abs(regression_gradient[index] - numerical)
        );
        if (!close_gradient(
                regression_gradient[index],
                numerical,
                kAbsoluteTolerance,
                kRelativeTolerance)) {
            report << "regression_gradient_failed index=" << index;
            return false;
        }
    }

    ParameterRegistry regression_registry;
    DeterministicRng regression_rng(2026);
    Parameter& hidden_weight =
        regression_registry.add("regression.hidden_weight", 12);
    Parameter& hidden_bias =
        regression_registry.add("regression.hidden_bias", 4);
    Parameter& output_weight =
        regression_registry.add("regression.output_weight", 4);
    Parameter& output_bias =
        regression_registry.add("regression.output_bias", 1);
    xavier_uniform(hidden_weight, 3, 4, regression_rng);
    xavier_uniform(output_weight, 4, 1, regression_rng);
    std::fill(hidden_bias.value.begin(), hidden_bias.value.end(), 0.7);
    const std::vector<double> regression_latent = {0.3, -0.4, 0.5};
    Matrix regression_hidden;
    Matrix regression_activated;
    const double head_prediction = regression_head_forward(
        regression_latent,
        hidden_weight,
        hidden_bias,
        output_weight,
        output_bias,
        regression_hidden,
        regression_activated
    );
    std::vector<double> head_prediction_gradient;
    static_cast<void>(regression_mse(
        {head_prediction},
        {0.25},
        &head_prediction_gradient
    ));
    regression_registry.zero_gradients();
    const std::vector<double> head_latent_gradient =
        regression_head_backward(
            regression_latent,
            head_prediction_gradient[0],
            regression_hidden,
            regression_activated,
            hidden_weight,
            hidden_bias,
            output_weight,
            output_bias
        );
    auto regression_objective = [&]() {
        Matrix candidate_hidden;
        Matrix candidate_activated;
        const double value = regression_head_forward(
            regression_latent,
            hidden_weight,
            hidden_bias,
            output_weight,
            output_bias,
            candidate_hidden,
            candidate_activated
        );
        return regression_mse({value}, {0.25}, nullptr);
    };
    for (Parameter* parameter : std::vector<Parameter*>{
             &hidden_weight,
             &hidden_bias,
             &output_weight,
             &output_bias,
         }) {
        const std::size_t index = parameter->value.size() / 2;
        const double analytic = parameter->gradient[index];
        parameter->value[index] += kStep;
        const double plus = regression_objective();
        parameter->value[index] -= 2.0 * kStep;
        const double minus = regression_objective();
        parameter->value[index] += kStep;
        const double numerical = (plus - minus) / (2.0 * kStep);
        maximum_error =
            std::max(maximum_error, std::abs(analytic - numerical));
        if (!close_gradient(
                analytic,
                numerical,
                kAbsoluteTolerance,
                kRelativeTolerance)) {
            report << "regression_head_gradient_failed name="
                   << parameter->name;
            return false;
        }
    }
    static_cast<void>(head_latent_gradient);

    std::vector<std::vector<double>> latent = {
        {0.2, -0.3, 0.7},
        {-0.5, 0.4, 0.1},
        {0.8, 0.2, -0.6},
    };
    const std::vector<double> metric_targets = {0.1, 0.8, -0.2};
    std::vector<std::vector<double>> metric_gradient;
    static_cast<void>(
        metric_alignment(
            latent,
            metric_targets,
            0x12345678ULL,
            &metric_gradient
        )
    );
    for (std::size_t row = 0; row < latent.size(); ++row) {
        for (std::size_t column = 0; column < latent[row].size(); ++column) {
            auto plus = latent;
            auto minus = latent;
            plus[row][column] += kStep;
            minus[row][column] -= kStep;
            const double numerical =
                (metric_alignment(
                     plus,
                     metric_targets,
                     0x12345678ULL,
                     nullptr
                 ) -
                 metric_alignment(
                     minus,
                     metric_targets,
                     0x12345678ULL,
                     nullptr
                 )) /
                (2.0 * kStep);
            maximum_error = std::max(
                maximum_error,
                std::abs(metric_gradient[row][column] - numerical)
            );
            if (!close_gradient(
                    metric_gradient[row][column],
                    numerical,
                    kAbsoluteTolerance,
                    kRelativeTolerance)) {
                report << "metric_alignment_gradient_failed row=" << row
                       << " column=" << column;
                return false;
            }
        }
    }
    report << std::scientific << std::setprecision(3)
           << "loss_gradient_pass cross_entropy=pass regression_mse=pass "
           << "regression_hidden_relu_output=pass "
           << "metric_alignment_l2_sampled_pairs=pass pair_count=batch "
           << "max_abs_error=" << maximum_error
           << " abs_tol=" << kAbsoluteTolerance
           << " rel_tol=" << kRelativeTolerance;
    return true;
}

}  // namespace

struct TransformerAutoencoder::Impl {
    ModelConfig config;
    ParameterRegistry registry;
    DeterministicRng rng;
    Parameter* token_embedding;
    Parameter* position_embedding;
    Parameter* decoder_token_embedding;
    Parameter* decoder_position_embedding;
    Parameter* bos_embedding;
    Parameter* latent_weight;
    Parameter* latent_bias;
    Parameter* memory_weight;
    Parameter* memory_bias;
    Parameter* regression_hidden_weight;
    Parameter* regression_hidden_bias;
    Parameter* regression_output_weight;
    Parameter* regression_output_bias;
    Parameter* output_weight;
    Parameter* output_bias;
    std::vector<std::unique_ptr<EncoderLayer>> encoder;
    std::vector<std::unique_ptr<DecoderLayer>> decoder;

    Impl(ModelConfig model_config, std::uint64_t seed)
        : config(std::move(model_config)),
          rng(seed),
          token_embedding(nullptr),
          position_embedding(nullptr),
          decoder_token_embedding(nullptr),
          decoder_position_embedding(nullptr),
          bos_embedding(nullptr),
          latent_weight(nullptr),
          latent_bias(nullptr),
          memory_weight(nullptr),
          memory_bias(nullptr),
          regression_hidden_weight(nullptr),
          regression_hidden_bias(nullptr),
          regression_output_weight(nullptr),
          regression_output_bias(nullptr),
          output_weight(nullptr),
          output_bias(nullptr) {
        if (config.vocabulary <= 1 || config.sequence_length <= 0 ||
            config.model_dimension <= 0 || config.latent_dimension <= 0 ||
            config.heads <= 0 || config.encoder_layers <= 0 ||
            config.decoder_layers <= 0 || config.ffn_width <= 0 ||
            config.regression_hidden_width <= 0 ||
            config.sequence_length > config.vocabulary ||
            config.model_dimension % config.heads != 0 ||
            config.dropout != 0.0) {
            throw std::invalid_argument("unsupported Transformer config");
        }
        const std::size_t vocabulary =
            static_cast<std::size_t>(config.vocabulary);
        const std::size_t sequence =
            static_cast<std::size_t>(config.sequence_length);
        const std::size_t dimension =
            static_cast<std::size_t>(config.model_dimension);
        const std::size_t latent =
            static_cast<std::size_t>(config.latent_dimension);
        token_embedding = &registry.add(
            "embedding.token",
            vocabulary * dimension
        );
        position_embedding = &registry.add(
            "embedding.position",
            sequence * dimension
        );
        decoder_token_embedding = &registry.add(
            "decoder.embedding.token",
            vocabulary * dimension,
            true
        );
        decoder_position_embedding = &registry.add(
            "decoder.embedding.position",
            sequence * dimension,
            true
        );
        bos_embedding = &registry.add("decoder.bos", dimension, true);
        latent_weight = &registry.add(
            "encoder.latent_weight",
            dimension * latent
        );
        latent_bias = &registry.add("encoder.latent_bias", latent);
        memory_weight = &registry.add(
            "decoder.memory_weight",
            latent * sequence * dimension,
            true
        );
        memory_bias = &registry.add(
            "decoder.memory_bias",
            sequence * dimension,
            true
        );
        const std::size_t regression_hidden =
            static_cast<std::size_t>(config.regression_hidden_width);
        regression_hidden_weight = &registry.add(
            "regression.hidden_weight",
            latent * regression_hidden
        );
        regression_hidden_bias = &registry.add(
            "regression.hidden_bias",
            regression_hidden
        );
        regression_output_weight = &registry.add(
            "regression.output_weight",
            regression_hidden
        );
        regression_output_bias = &registry.add(
            "regression.output_bias",
            1
        );
        xavier_uniform(
            *token_embedding,
            vocabulary,
            dimension,
            rng
        );
        xavier_uniform(
            *position_embedding,
            sequence,
            dimension,
            rng
        );
        xavier_uniform(
            *decoder_token_embedding,
            vocabulary,
            dimension,
            rng
        );
        xavier_uniform(
            *decoder_position_embedding,
            sequence,
            dimension,
            rng
        );
        xavier_uniform(*bos_embedding, 1, dimension, rng);
        xavier_uniform(*latent_weight, dimension, latent, rng);
        xavier_uniform(
            *memory_weight,
            latent,
            sequence * dimension,
            rng
        );
        xavier_uniform(
            *regression_hidden_weight,
            latent,
            regression_hidden,
            rng
        );
        xavier_uniform(
            *regression_output_weight,
            regression_hidden,
            1,
            rng
        );
        for (int layer = 0; layer < config.encoder_layers; ++layer) {
            encoder.push_back(std::make_unique<EncoderLayer>(
                registry,
                "encoder.layer." + std::to_string(layer),
                dimension,
                static_cast<std::size_t>(config.heads),
                static_cast<std::size_t>(config.ffn_width),
                rng
            ));
        }
        for (int layer = 0; layer < config.decoder_layers; ++layer) {
            decoder.push_back(std::make_unique<DecoderLayer>(
                registry,
                "decoder.layer." + std::to_string(layer),
                dimension,
                static_cast<std::size_t>(config.heads),
                static_cast<std::size_t>(config.ffn_width),
                rng
            ));
        }
        output_weight = &registry.add(
            "decoder.output_weight",
            dimension * vocabulary,
            true
        );
        output_bias = &registry.add(
            "decoder.output_bias",
            vocabulary,
            true
        );
        xavier_uniform(*output_weight, dimension, vocabulary, rng);
    }

    void validate_tokens(const std::vector<int>& tokens) const {
        if (tokens.size() !=
            static_cast<std::size_t>(config.sequence_length)) {
            throw std::invalid_argument("layout token length mismatch");
        }
        for (int token : tokens) {
            if (token < 0 || token >= config.vocabulary) {
                throw std::invalid_argument("layout token out of range");
            }
        }
        if (!std::is_sorted(tokens.begin(), tokens.end()) ||
            std::adjacent_find(tokens.begin(), tokens.end()) !=
                tokens.end()) {
            throw std::invalid_argument(
                "layout tokens must be unique canonical sorted cells"
            );
        }
    }

    Matrix embed(
        const std::vector<int>& tokens,
        bool decoder_input
    ) const {
        const std::size_t sequence =
            static_cast<std::size_t>(config.sequence_length);
        const std::size_t dimension =
            static_cast<std::size_t>(config.model_dimension);
        Matrix result(sequence, dimension);
        for (std::size_t row = 0; row < sequence; ++row) {
            for (std::size_t column = 0; column < dimension; ++column) {
                const double token_value =
                    decoder_input && tokens[row] < 0
                        ? bos_embedding->value[column]
                        : (decoder_input
                               ? decoder_token_embedding->value[
                                     static_cast<std::size_t>(tokens[row]) *
                                         dimension +
                                     column
                                 ]
                               : token_embedding->value[
                              static_cast<std::size_t>(tokens[row]) *
                                  dimension +
                              column
                          ]);
                result(row, column) =
                    token_value +
                    (decoder_input
                         ? decoder_position_embedding->value[
                               row * dimension + column
                           ]
                         : position_embedding->value[
                               row * dimension + column
                           ]);
            }
        }
        return result;
    }

    Matrix encode_forward(
        const std::vector<int>& tokens,
        ModelSampleCache& cache
    ) const {
        validate_tokens(tokens);
        cache.source_tokens = tokens;
        cache.encoder_embedding = embed(tokens, false);
        Matrix hidden = cache.encoder_embedding;
        cache.encoder_layers.resize(encoder.size());
        for (std::size_t layer = 0; layer < encoder.size(); ++layer) {
            hidden = encoder[layer]->forward(
                hidden,
                cache.encoder_layers[layer]
            );
        }
        cache.encoder_output = hidden;
        const std::size_t dimension =
            static_cast<std::size_t>(config.model_dimension);
        cache.pooled = Matrix(1, dimension);
        for (std::size_t row = 0; row < hidden.rows; ++row) {
            for (std::size_t column = 0; column < dimension; ++column) {
                cache.pooled(0, column) +=
                    hidden(row, column) /
                    static_cast<double>(hidden.rows);
            }
        }
        cache.latent_raw = linear_forward(
            cache.pooled,
            *latent_weight,
            static_cast<std::size_t>(config.latent_dimension),
            cache.latent_linear
        );
        for (std::size_t index = 0;
             index < cache.latent_raw.cols;
             ++index) {
            cache.latent_raw(0, index) += latent_bias->value[index];
        }
        cache.latent = Matrix(1, cache.latent_raw.cols);
        cache.latent.values = l2_normalize(
            cache.latent_raw.values,
            cache.latent_inverse_norm
        );
        return cache.latent;
    }

    void decode_forward(
        const std::vector<int>& targets,
        ModelSampleCache& cache
    ) const {
        const std::size_t sequence =
            static_cast<std::size_t>(config.sequence_length);
        const std::size_t dimension =
            static_cast<std::size_t>(config.model_dimension);
        cache.memory_seed = linear_forward(
            cache.latent,
            *memory_weight,
            sequence * dimension,
            cache.memory_linear
        );
        for (std::size_t column = 0;
             column < sequence * dimension;
             ++column) {
            cache.memory_seed(0, column) += memory_bias->value[column];
        }
        cache.memory = Matrix(sequence, dimension);
        for (std::size_t row = 0; row < sequence; ++row) {
            for (std::size_t column = 0; column < dimension; ++column) {
                cache.memory(row, column) =
                    cache.memory_seed(0, row * dimension + column);
            }
        }
        cache.decoder_tokens.assign(sequence, -1);
        for (std::size_t row = 1; row < sequence; ++row) {
            cache.decoder_tokens[row] = targets[row - 1];
        }
        cache.decoder_embedding = embed(cache.decoder_tokens, true);
        Matrix hidden = cache.decoder_embedding;
        cache.decoder_layers.resize(decoder.size());
        for (std::size_t layer = 0; layer < decoder.size(); ++layer) {
            hidden = decoder[layer]->forward(
                hidden,
                cache.memory,
                cache.decoder_layers[layer]
            );
        }
        cache.decoder_output = hidden;
        cache.logits = linear_forward(
            hidden,
            *output_weight,
            static_cast<std::size_t>(config.vocabulary),
            cache.output_linear
        );
        for (std::size_t row = 0; row < cache.logits.rows; ++row) {
            for (std::size_t column = 0;
                 column < cache.logits.cols;
                 ++column) {
                cache.logits(row, column) += output_bias->value[column];
            }
        }
        cache.probabilities = softmax_rows(cache.logits);
    }

    void full_forward(
        const std::vector<int>& tokens,
        ModelSampleCache& cache
    ) const {
        static_cast<void>(encode_forward(tokens, cache));
        decode_forward(tokens, cache);
    }

    void accumulate_embedding_gradient(
        const Matrix& gradient,
        const std::vector<int>& tokens,
        bool decoder_input
    ) {
        const std::size_t dimension =
            static_cast<std::size_t>(config.model_dimension);
        for (std::size_t row = 0; row < gradient.rows; ++row) {
            for (std::size_t column = 0; column < gradient.cols; ++column) {
                if (decoder_input) {
                    decoder_position_embedding->gradient[
                        row * dimension + column
                    ] += gradient(row, column);
                    if (tokens[row] < 0) {
                        bos_embedding->gradient[column] +=
                            gradient(row, column);
                    } else {
                        decoder_token_embedding->gradient[
                            static_cast<std::size_t>(tokens[row]) *
                                dimension +
                            column
                        ] += gradient(row, column);
                    }
                } else {
                    position_embedding->gradient[
                        row * dimension + column
                    ] += gradient(row, column);
                    token_embedding->gradient[
                        static_cast<std::size_t>(tokens[row]) * dimension +
                        column
                    ] += gradient(row, column);
                }
            }
        }
    }

    void backward_sample(
        ModelSampleCache& cache,
        const Matrix& logits_gradient,
        const std::vector<double>& external_normalized_latent_gradient,
        const std::vector<double>& external_raw_latent_gradient
    ) {
        for (std::size_t row = 0; row < logits_gradient.rows; ++row) {
            for (std::size_t column = 0;
                 column < logits_gradient.cols;
                 ++column) {
                output_bias->gradient[column] +=
                    logits_gradient(row, column);
            }
        }
        Matrix decoder_gradient = linear_backward(
            logits_gradient,
            *output_weight,
            cache.output_linear
        );
        Matrix memory_gradient(
            cache.memory.rows,
            cache.memory.cols
        );
        for (std::size_t layer = decoder.size(); layer-- > 0;) {
            auto [layer_gradient, layer_memory_gradient] =
                decoder[layer]->backward(
                    decoder_gradient,
                    cache.decoder_layers[layer]
                );
            decoder_gradient = std::move(layer_gradient);
            add_in_place(memory_gradient, layer_memory_gradient);
        }
        accumulate_embedding_gradient(
            decoder_gradient,
            cache.decoder_tokens,
            true
        );
        Matrix memory_seed_gradient(
            1,
            memory_gradient.rows * memory_gradient.cols
        );
        for (std::size_t row = 0; row < memory_gradient.rows; ++row) {
            for (std::size_t column = 0;
                 column < memory_gradient.cols;
                 ++column) {
                memory_seed_gradient(
                    0,
                    row * memory_gradient.cols + column
                ) = memory_gradient(row, column);
            }
        }
        for (std::size_t column = 0;
             column < memory_seed_gradient.cols;
             ++column) {
            memory_bias->gradient[column] +=
                memory_seed_gradient(0, column);
        }
        Matrix normalized_latent_gradient = linear_backward(
            memory_seed_gradient,
            *memory_weight,
            cache.memory_linear
        );
        if (external_normalized_latent_gradient.size() !=
                normalized_latent_gradient.cols ||
            external_raw_latent_gradient.size() !=
                normalized_latent_gradient.cols) {
            throw std::invalid_argument("external latent gradient mismatch");
        }
        for (std::size_t column = 0;
             column < normalized_latent_gradient.cols;
             ++column) {
            normalized_latent_gradient(0, column) +=
                external_normalized_latent_gradient[column];
        }
        Matrix raw_latent_gradient(1, normalized_latent_gradient.cols);
        raw_latent_gradient.values = l2_normalize_backward(
            normalized_latent_gradient.values,
            cache.latent.values,
            cache.latent_inverse_norm
        );
        for (std::size_t column = 0;
             column < raw_latent_gradient.cols;
             ++column) {
            raw_latent_gradient(0, column) +=
                external_raw_latent_gradient[column];
            latent_bias->gradient[column] +=
                raw_latent_gradient(0, column);
        }
        Matrix pooled_gradient = linear_backward(
            raw_latent_gradient,
            *latent_weight,
            cache.latent_linear
        );
        Matrix encoder_gradient(
            cache.encoder_output.rows,
            cache.encoder_output.cols
        );
        for (std::size_t row = 0; row < encoder_gradient.rows; ++row) {
            for (std::size_t column = 0;
                 column < encoder_gradient.cols;
                 ++column) {
                encoder_gradient(row, column) =
                    pooled_gradient(0, column) /
                    static_cast<double>(encoder_gradient.rows);
            }
        }
        for (std::size_t layer = encoder.size(); layer-- > 0;) {
            encoder_gradient = encoder[layer]->backward(
                encoder_gradient,
                cache.encoder_layers[layer]
            );
        }
        accumulate_embedding_gradient(
            encoder_gradient,
            cache.source_tokens,
            false
        );
    }

    BatchLoss gradients(
        const std::vector<std::vector<int>>& layouts,
        const std::vector<double>& relative_fitness,
        const LossWeights& weights,
        bool freeze_decoder
    ) {
        if (layouts.empty() ||
            (!relative_fitness.empty() &&
             relative_fitness.size() != layouts.size())) {
            throw std::invalid_argument("training batch shape mismatch");
        }
        registry.zero_gradients();
        std::vector<ModelSampleCache> caches(layouts.size());
        std::vector<std::vector<double>> latent(layouts.size());
        std::vector<std::vector<double>> raw_latent(layouts.size());
        std::vector<double> prediction(layouts.size(), 0.0);
        BatchLoss losses;
        for (std::size_t sample = 0; sample < layouts.size(); ++sample) {
            full_forward(layouts[sample], caches[sample]);
            losses.reconstruction += cross_entropy(
                caches[sample].logits,
                layouts[sample],
                nullptr
            ) / static_cast<double>(layouts.size());
            latent[sample] = caches[sample].latent.values;
            raw_latent[sample] = caches[sample].latent_raw.values;
            prediction[sample] = regression_head_forward(
                latent[sample],
                *regression_hidden_weight,
                *regression_hidden_bias,
                *regression_output_weight,
                *regression_output_bias,
                caches[sample].regression_hidden,
                caches[sample].regression_activated
            );
        }
        const std::vector<double> targets =
            relative_fitness.empty()
                ? std::vector<double>(layouts.size(), 0.0)
                : relative_fitness;
        std::vector<double> regression_gradient;
        losses.regression = regression_mse(
            prediction,
            targets,
            &regression_gradient
        );
        std::vector<std::vector<double>> metric_gradient;
        losses.metric_smoothness = metric_alignment(
            raw_latent,
            targets,
            weights.metric_pair_seed,
            &metric_gradient
        );
        for (std::size_t sample = 0; sample < layouts.size(); ++sample) {
            Matrix logits_gradient;
            static_cast<void>(cross_entropy(
                caches[sample].logits,
                layouts[sample],
                &logits_gradient
            ));
            const double reconstruction_scale =
                weights.reconstruction /
                static_cast<double>(layouts.size());
            for (double& value : logits_gradient.values) {
                value *= reconstruction_scale;
            }
            const double regression_signal =
                weights.regression * regression_gradient[sample];
            std::vector<double> normalized_latent_gradient =
                regression_head_backward(
                    latent[sample],
                    regression_signal,
                    caches[sample].regression_hidden,
                    caches[sample].regression_activated,
                    *regression_hidden_weight,
                    *regression_hidden_bias,
                    *regression_output_weight,
                    *regression_output_bias
                );
            std::vector<double> raw_latent_gradient =
                metric_gradient[sample];
            for (std::size_t feature = 0;
                 feature < latent[sample].size();
                 ++feature) {
                raw_latent_gradient[feature] *=
                    weights.metric_smoothness;
            }
            backward_sample(
                caches[sample],
                logits_gradient,
                normalized_latent_gradient,
                raw_latent_gradient
            );
        }
        if (freeze_decoder) {
            for (Parameter* parameter : registry.all()) {
                if (parameter->decoder) {
                    std::fill(
                        parameter->gradient.begin(),
                        parameter->gradient.end(),
                        0.0
                    );
                }
            }
        }
        losses.total =
            weights.reconstruction * losses.reconstruction +
            weights.regression * losses.regression +
            weights.metric_smoothness * losses.metric_smoothness;
        return losses;
    }

    void adam(
        double learning_rate,
        double beta1,
        double beta2,
        double epsilon,
        bool freeze_decoder
    ) {
        for (Parameter* parameter : registry.all()) {
            if (freeze_decoder && parameter->decoder) {
                continue;
            }
            ++parameter->update_step;
            const double first_correction =
                1.0 -
                std::pow(
                    beta1,
                    static_cast<double>(parameter->update_step)
                );
            const double second_correction =
                1.0 -
                std::pow(
                    beta2,
                    static_cast<double>(parameter->update_step)
                );
            for (std::size_t index = 0;
                 index < parameter->value.size();
                 ++index) {
                const double gradient = parameter->gradient[index];
                parameter->first_moment[index] =
                    beta1 * parameter->first_moment[index] +
                    (1.0 - beta1) * gradient;
                parameter->second_moment[index] =
                    beta2 * parameter->second_moment[index] +
                    (1.0 - beta2) * gradient * gradient;
                const double first_hat =
                    parameter->first_moment[index] / first_correction;
                const double second_hat =
                    parameter->second_moment[index] / second_correction;
                parameter->value[index] -=
                    learning_rate * first_hat /
                    (std::sqrt(second_hat) + epsilon);
            }
        }
    }
};

namespace {

template <typename Value>
void write_binary(std::ostream& stream, const Value& value) {
    stream.write(
        reinterpret_cast<const char*>(&value),
        static_cast<std::streamsize>(sizeof(Value))
    );
    if (!stream) {
        throw std::runtime_error("checkpoint write failed");
    }
}

template <typename Value>
Value read_binary(std::istream& stream) {
    Value value{};
    stream.read(
        reinterpret_cast<char*>(&value),
        static_cast<std::streamsize>(sizeof(Value))
    );
    if (!stream) {
        throw std::runtime_error("checkpoint read failed");
    }
    return value;
}

void write_string(std::ostream& stream, const std::string& value) {
    write_binary(stream, static_cast<std::uint64_t>(value.size()));
    stream.write(value.data(), static_cast<std::streamsize>(value.size()));
    if (!stream) {
        throw std::runtime_error("checkpoint string write failed");
    }
}

std::string read_string(std::istream& stream) {
    const std::uint64_t size = read_binary<std::uint64_t>(stream);
    if (size > 1024U * 1024U) {
        throw std::runtime_error("checkpoint string size invalid");
    }
    std::string value(static_cast<std::size_t>(size), '\0');
    stream.read(value.data(), static_cast<std::streamsize>(value.size()));
    if (!stream) {
        throw std::runtime_error("checkpoint string read failed");
    }
    return value;
}

std::uint32_t rotate_right(std::uint32_t value, unsigned shift) {
    return (value >> shift) | (value << (32U - shift));
}

std::string sha256_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open checkpoint for SHA-256");
    }
    std::vector<unsigned char> bytes(
        (std::istreambuf_iterator<char>(stream)),
        std::istreambuf_iterator<char>()
    );
    const std::uint64_t bit_length =
        static_cast<std::uint64_t>(bytes.size()) * 8U;
    bytes.push_back(0x80U);
    while (bytes.size() % 64U != 56U) {
        bytes.push_back(0U);
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<unsigned char>(
            (bit_length >> static_cast<unsigned>(shift)) & 0xffU
        ));
    }
    std::array<std::uint32_t, 8> hash = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    };
    constexpr std::array<std::uint32_t, 64> constants = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
        0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
        0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
        0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
    };
    for (std::size_t offset = 0; offset < bytes.size(); offset += 64U) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16; ++index) {
            const std::size_t start = offset + index * 4U;
            words[index] =
                (static_cast<std::uint32_t>(bytes[start]) << 24U) |
                (static_cast<std::uint32_t>(bytes[start + 1]) << 16U) |
                (static_cast<std::uint32_t>(bytes[start + 2]) << 8U) |
                static_cast<std::uint32_t>(bytes[start + 3]);
        }
        for (std::size_t index = 16; index < 64; ++index) {
            const std::uint32_t s0 =
                rotate_right(words[index - 15], 7) ^
                rotate_right(words[index - 15], 18) ^
                (words[index - 15] >> 3U);
            const std::uint32_t s1 =
                rotate_right(words[index - 2], 17) ^
                rotate_right(words[index - 2], 19) ^
                (words[index - 2] >> 10U);
            words[index] =
                words[index - 16] + s0 + words[index - 7] + s1;
        }
        std::uint32_t a = hash[0];
        std::uint32_t b = hash[1];
        std::uint32_t c = hash[2];
        std::uint32_t d = hash[3];
        std::uint32_t e = hash[4];
        std::uint32_t f = hash[5];
        std::uint32_t g = hash[6];
        std::uint32_t h = hash[7];
        for (std::size_t index = 0; index < 64; ++index) {
            const std::uint32_t sigma1 =
                rotate_right(e, 6) ^ rotate_right(e, 11) ^
                rotate_right(e, 25);
            const std::uint32_t choice = (e & f) ^ ((~e) & g);
            const std::uint32_t temporary1 =
                h + sigma1 + choice + constants[index] + words[index];
            const std::uint32_t sigma0 =
                rotate_right(a, 2) ^ rotate_right(a, 13) ^
                rotate_right(a, 22);
            const std::uint32_t majority =
                (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temporary2 = sigma0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temporary1;
            d = c;
            c = b;
            b = a;
            a = temporary1 + temporary2;
        }
        hash[0] += a;
        hash[1] += b;
        hash[2] += c;
        hash[3] += d;
        hash[4] += e;
        hash[5] += f;
        hash[6] += g;
        hash[7] += h;
    }
    std::ostringstream result;
    result << "sha256:" << std::hex << std::setfill('0');
    for (std::uint32_t value : hash) {
        result << std::setw(8) << value;
    }
    return result.str();
}

}  // namespace

TransformerAutoencoder::TransformerAutoencoder(
    ModelConfig config,
    std::uint64_t seed
) : impl_(new Impl(std::move(config), seed)) {}

TransformerAutoencoder::TransformerAutoencoder(
    TransformerAutoencoder&& other
) noexcept : impl_(std::exchange(other.impl_, nullptr)) {}

TransformerAutoencoder& TransformerAutoencoder::operator=(
    TransformerAutoencoder&& other
) noexcept {
    if (this != &other) {
        delete impl_;
        impl_ = std::exchange(other.impl_, nullptr);
    }
    return *this;
}

TransformerAutoencoder::~TransformerAutoencoder() {
    delete impl_;
}

const ModelConfig& TransformerAutoencoder::config() const noexcept {
    return impl_->config;
}

std::vector<double> TransformerAutoencoder::encode(
    const std::vector<int>& tokens
) const {
    ModelSampleCache cache;
    return impl_->encode_forward(tokens, cache).values;
}

std::vector<int> TransformerAutoencoder::decode_argmax(
    const std::vector<double>& latent
) const {
    if (latent.size() !=
        static_cast<std::size_t>(impl_->config.latent_dimension)) {
        throw std::invalid_argument("decode latent shape mismatch");
    }
    ModelSampleCache cache;
    cache.latent = Matrix(1, latent.size());
    cache.latent.values = latent;
    std::vector<int> generated(
        static_cast<std::size_t>(impl_->config.sequence_length),
        0
    );
    for (std::size_t position = 0; position < generated.size(); ++position) {
        impl_->decode_forward(generated, cache);
        const Matrix probabilities = softmax_rows(cache.logits);
        std::size_t best = 0;
        for (std::size_t token = 1;
             token < probabilities.cols;
             ++token) {
            if (probabilities(position, token) >
                probabilities(position, best)) {
                best = token;
            }
        }
        generated[position] = static_cast<int>(best);
    }
    return generated;
}

double TransformerAutoencoder::reconstruction_loss(
    const std::vector<std::vector<int>>& layouts,
    fode_compat::PersistentExecutor*
) const {
    if (layouts.empty()) {
        throw std::invalid_argument("reconstruction corpus is empty");
    }
    double loss = 0.0;
    for (const auto& layout : layouts) {
        ModelSampleCache cache;
        impl_->full_forward(layout, cache);
        loss += cross_entropy(cache.logits, layout, nullptr);
    }
    return loss / static_cast<double>(layouts.size());
}

BatchLoss TransformerAutoencoder::gradient_only(
    const std::vector<std::vector<int>>& layouts,
    const std::vector<double>& relative_fitness,
    const LossWeights& weights,
    bool freeze_decoder
) {
    return impl_->gradients(
        layouts,
        relative_fitness,
        weights,
        freeze_decoder
    );
}

BatchLoss TransformerAutoencoder::train_batch(
    const std::vector<std::vector<int>>& layouts,
    const std::vector<double>& relative_fitness,
    const LossWeights& weights,
    double learning_rate,
    double beta1,
    double beta2,
    double epsilon,
    bool freeze_decoder,
    fode_compat::PersistentExecutor*
) {
    BatchLoss loss = gradient_only(
        layouts,
        relative_fitness,
        weights,
        freeze_decoder
    );
    impl_->adam(
        learning_rate,
        beta1,
        beta2,
        epsilon,
        freeze_decoder
    );
    return loss;
}

std::string TransformerAutoencoder::parameter_hash() const {
    return impl_->registry.stable_hash();
}

CheckpointMetadata TransformerAutoencoder::save_checkpoint(
    const std::string& path,
    const std::string& training_profile_id,
    std::uint64_t initialization_seed,
    const TrainingWork& work
) const {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("cannot create checkpoint");
    }
    write_string(stream, "TAAE_KERNEL_CHECKPOINT_V2");
    write_binary(stream, impl_->config.vocabulary);
    write_binary(stream, impl_->config.sequence_length);
    write_binary(stream, impl_->config.model_dimension);
    write_binary(stream, impl_->config.latent_dimension);
    write_binary(stream, impl_->config.heads);
    write_binary(stream, impl_->config.encoder_layers);
    write_binary(stream, impl_->config.decoder_layers);
    write_binary(stream, impl_->config.ffn_width);
    write_binary(stream, impl_->config.regression_hidden_width);
    write_binary(stream, impl_->config.dropout);
    write_string(stream, "taae_transformer_declared_reconstruction_v1");
    write_string(stream, "taae_zhangbei_structured_declared_proxy_v1");
    write_string(stream, training_profile_id);
    write_binary(stream, initialization_seed);
    write_binary(stream, work.corpus_samples);
    write_binary(stream, work.token_operations);
    write_binary(stream, work.optimizer_steps);
    write_binary(stream, work.pretraining_epochs);
    write_binary(stream, work.fine_tuning_epochs);
    write_binary(stream, work.training_physical_fes);
    write_binary(stream, work.wall_seconds);
    const std::string hash = parameter_hash();
    write_string(stream, hash);
    const auto parameters = impl_->registry.all();
    write_binary(
        stream,
        static_cast<std::uint64_t>(parameters.size())
    );
    for (const Parameter* parameter : parameters) {
        write_string(stream, parameter->name);
        write_binary(
            stream,
            static_cast<std::uint64_t>(parameter->value.size())
        );
        write_binary(stream, parameter->update_step);
        for (double value : parameter->value) {
            write_binary(stream, value);
        }
        for (double value : parameter->first_moment) {
            write_binary(stream, value);
        }
        for (double value : parameter->second_moment) {
            write_binary(stream, value);
        }
    }
    stream.close();
    if (!stream) {
        throw std::runtime_error("checkpoint close failed");
    }
    CheckpointMetadata metadata;
    metadata.method_semantic_id =
        "taae_transformer_declared_reconstruction_v1";
    metadata.problem_semantic_id =
        "taae_zhangbei_structured_declared_proxy_v1";
    metadata.training_profile_id = training_profile_id;
    metadata.initialization_seed = initialization_seed;
    metadata.work = work;
    metadata.parameter_fnv1a64 = hash;
    metadata.file_sha256 = sha256_file(path);
    return metadata;
}

TransformerAutoencoder TransformerAutoencoder::load_checkpoint(
    const std::string& path,
    CheckpointMetadata& metadata
) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream || read_string(stream) != "TAAE_KERNEL_CHECKPOINT_V2") {
        throw std::runtime_error("checkpoint magic mismatch");
    }
    ModelConfig config;
    config.vocabulary = read_binary<int>(stream);
    config.sequence_length = read_binary<int>(stream);
    config.model_dimension = read_binary<int>(stream);
    config.latent_dimension = read_binary<int>(stream);
    config.heads = read_binary<int>(stream);
    config.encoder_layers = read_binary<int>(stream);
    config.decoder_layers = read_binary<int>(stream);
    config.ffn_width = read_binary<int>(stream);
    config.regression_hidden_width = read_binary<int>(stream);
    config.dropout = read_binary<double>(stream);
    metadata.method_semantic_id = read_string(stream);
    metadata.problem_semantic_id = read_string(stream);
    metadata.training_profile_id = read_string(stream);
    metadata.initialization_seed = read_binary<std::uint64_t>(stream);
    metadata.work.corpus_samples = read_binary<std::uint64_t>(stream);
    metadata.work.token_operations = read_binary<std::uint64_t>(stream);
    metadata.work.optimizer_steps = read_binary<std::uint64_t>(stream);
    metadata.work.pretraining_epochs = read_binary<std::uint64_t>(stream);
    metadata.work.fine_tuning_epochs = read_binary<std::uint64_t>(stream);
    metadata.work.training_physical_fes =
        read_binary<std::uint64_t>(stream);
    metadata.work.wall_seconds = read_binary<double>(stream);
    metadata.parameter_fnv1a64 = read_string(stream);
    TransformerAutoencoder model(config, metadata.initialization_seed);
    const std::uint64_t count = read_binary<std::uint64_t>(stream);
    const auto parameters = model.impl_->registry.all();
    if (count != parameters.size()) {
        throw std::runtime_error("checkpoint parameter count mismatch");
    }
    for (Parameter* parameter : parameters) {
        if (read_string(stream) != parameter->name) {
            throw std::runtime_error("checkpoint parameter name mismatch");
        }
        const std::uint64_t size = read_binary<std::uint64_t>(stream);
        if (size != parameter->value.size()) {
            throw std::runtime_error("checkpoint parameter size mismatch");
        }
        parameter->update_step = read_binary<std::uint64_t>(stream);
        for (double& value : parameter->value) {
            value = read_binary<double>(stream);
        }
        for (double& value : parameter->first_moment) {
            value = read_binary<double>(stream);
        }
        for (double& value : parameter->second_moment) {
            value = read_binary<double>(stream);
        }
    }
    if (model.parameter_hash() != metadata.parameter_fnv1a64) {
        throw std::runtime_error("checkpoint parameter hash mismatch");
    }
    metadata.file_sha256 = sha256_file(path);
    return model;
}

std::vector<std::string> TransformerAutoencoder::parameter_names() const {
    std::vector<std::string> result;
    for (const Parameter* parameter : impl_->registry.all()) {
        result.push_back(parameter->name);
    }
    return result;
}

double TransformerAutoencoder::parameter_value(
    const std::string& name,
    std::size_t index
) const {
    const Parameter* parameter = impl_->registry.find(name);
    if (parameter == nullptr || index >= parameter->value.size()) {
        throw std::out_of_range("unknown parameter or index");
    }
    return parameter->value[index];
}

std::size_t TransformerAutoencoder::parameter_size(
    const std::string& name
) const {
    const Parameter* parameter = impl_->registry.find(name);
    if (parameter == nullptr) {
        throw std::out_of_range("unknown parameter");
    }
    return parameter->value.size();
}

void TransformerAutoencoder::set_parameter_value(
    const std::string& name,
    std::size_t index,
    double value
) {
    Parameter* parameter = impl_->registry.find(name);
    if (parameter == nullptr || index >= parameter->value.size()) {
        throw std::out_of_range("unknown parameter or index");
    }
    parameter->value[index] = value;
}

double TransformerAutoencoder::parameter_gradient(
    const std::string& name,
    std::size_t index
) const {
    const Parameter* parameter = impl_->registry.find(name);
    if (parameter == nullptr || index >= parameter->gradient.size()) {
        throw std::out_of_range("unknown parameter or index");
    }
    return parameter->gradient[index];
}

std::uint64_t TransformerAutoencoder::parameter_update_step(
    const std::string& name
) const {
    const Parameter* parameter = impl_->registry.find(name);
    if (parameter == nullptr) {
        throw std::out_of_range("unknown parameter");
    }
    return parameter->update_step;
}

bool TransformerAutoencoder::causal_mask_fixture() const {
    ParameterRegistry registry;
    DeterministicRng rng(5);
    Attention attention(
        registry,
        "causal_fixture",
        4,
        2,
        rng,
        false
    );
    Matrix input(3, 4);
    for (std::size_t index = 0; index < input.values.size(); ++index) {
        input.values[index] =
            static_cast<double>(index + 1) * 0.1;
    }
    AttentionCache before_cache;
    const Matrix before =
        attention.forward(input, input, true, before_cache);
    for (std::size_t column = 0; column < input.cols; ++column) {
        input(2, column) += 100.0;
    }
    AttentionCache after_cache;
    const Matrix after =
        attention.forward(input, input, true, after_cache);
    for (std::size_t column = 0; column < input.cols; ++column) {
        if (before(0, column) != after(0, column)) {
            return false;
        }
    }
    return true;
}

bool TransformerAutoencoder::shape_fixture() const {
    const auto& value = impl_->config;
    if (value.encoder_layers != 6 || value.decoder_layers != 6 ||
        value.heads != 4 || value.model_dimension != 64 ||
        value.ffn_width != 256 || value.latent_dimension != 64 ||
        value.dropout != 0.0) {
        return false;
    }
    std::vector<int> tokens(
        static_cast<std::size_t>(value.sequence_length),
        0
    );
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        tokens[index] = static_cast<int>(index);
    }
    ModelSampleCache cache;
    impl_->full_forward(tokens, cache);
    return cache.logits.rows == tokens.size() &&
           cache.logits.cols ==
               static_cast<std::size_t>(value.vocabulary) &&
           cache.encoder_layers.size() == 6 &&
           cache.decoder_layers.size() == 6;
}

TrainingProfile paper_scale_training_profile() {
    TrainingProfile profile;
    profile.id = "paper_scale_declared_reconstruction_v1";
    profile.corpus_seed = 3601;
    profile.split_seed = 3602;
    profile.batch_seed = 3603;
    profile.fine_tune_seed = 3604;
    profile.corpus_layouts = 100000;
    profile.train_layouts = 100000;
    profile.test_layouts = 0;
    profile.pretraining_epochs = 500;
    profile.pretraining_batch_size = 64;
    profile.fine_tuning_batch_size = 64;
    profile.learning_rate = 1.0e-3;
    profile.beta1 = 0.9;
    profile.beta2 = 0.999;
    profile.epsilon = 1.0e-8;
    profile.fine_tuning_epochs_per_generation = 10;
    return profile;
}

TrainingProfile bounded_smoke_training_profile() {
    TrainingProfile profile;
    profile.id = "bounded_smoke_v1";
    profile.corpus_seed = 42;
    profile.split_seed = 43;
    profile.batch_seed = 44;
    profile.fine_tune_seed = 45;
    profile.corpus_layouts = 4;
    profile.train_layouts = 4;
    profile.test_layouts = 0;
    profile.pretraining_epochs = 8;
    profile.pretraining_batch_size = 4;
    profile.fine_tuning_batch_size = 4;
    return profile;
}

std::vector<std::vector<int>> deterministic_layout_corpus(
    std::uint64_t count,
    const ModelConfig& config,
    std::uint64_t seed
) {
    if (config.sequence_length <= 0 ||
        config.vocabulary < config.sequence_length) {
        throw std::invalid_argument(
            "layout corpus requires vocabulary >= sequence length"
        );
    }
    DeterministicRng rng(seed);
    std::vector<std::vector<int>> corpus(
        static_cast<std::size_t>(count),
        std::vector<int>(
            static_cast<std::size_t>(config.sequence_length),
            0
        )
    );
    for (auto& layout : corpus) {
        std::vector<int> available(
            static_cast<std::size_t>(config.vocabulary),
            0
        );
        for (std::size_t index = 0; index < available.size(); ++index) {
            available[index] = static_cast<int>(index);
        }
        for (std::size_t position = 0;
             position < layout.size();
             ++position) {
            const std::size_t selected =
                position +
                static_cast<std::size_t>(
                    rng.next_u64() %
                    static_cast<std::uint64_t>(
                        available.size() - position
                    )
                );
            std::swap(available[position], available[selected]);
            layout[position] = available[position];
        }
        std::sort(layout.begin(), layout.end());
    }
    return corpus;
}

TrainingWork pretrain(
    TransformerAutoencoder& model,
    const TrainingProfile& profile,
    fode_compat::PersistentExecutor&
) {
    if (profile.train_layouts == 0 ||
        profile.pretraining_epochs < 0 ||
        profile.pretraining_batch_size <= 0) {
        throw std::invalid_argument("invalid pretraining profile");
    }
    const auto corpus = deterministic_layout_corpus(
        profile.train_layouts,
        model.config(),
        profile.corpus_seed
    );
    LossWeights weights;
    weights.reconstruction = 1.0;
    weights.regression = 0.0;
    weights.metric_smoothness = 0.0;
    TrainingWork work;
    work.corpus_samples = corpus.size();
    DeterministicRng batch_rng(profile.batch_seed);
    std::vector<std::size_t> order(corpus.size(), 0);
    for (std::size_t index = 0; index < order.size(); ++index) {
        order[index] = index;
    }
    for (int epoch = 0; epoch < profile.pretraining_epochs; ++epoch) {
        for (std::size_t index = order.size(); index > 1; --index) {
            const std::size_t selected = static_cast<std::size_t>(
                batch_rng.next_u64() %
                static_cast<std::uint64_t>(index)
            );
            std::swap(order[index - 1], order[selected]);
        }
        const std::size_t batch_size = static_cast<std::size_t>(
            profile.pretraining_batch_size
        );
        for (std::size_t begin = 0;
             begin < order.size();
             begin += batch_size) {
            const std::size_t end =
                std::min(begin + batch_size, order.size());
            std::vector<std::vector<int>> batch;
            batch.reserve(end - begin);
            for (std::size_t position = begin;
                 position < end;
                 ++position) {
                batch.push_back(corpus[order[position]]);
            }
            const std::vector<double> targets(batch.size(), 0.0);
            model.train_batch(
                batch,
                targets,
                weights,
                profile.learning_rate,
                profile.beta1,
                profile.beta2,
                profile.epsilon,
                false
            );
            ++work.optimizer_steps;
            work.token_operations +=
                batch.size() *
                static_cast<std::uint64_t>(
                    model.config().sequence_length
                );
        }
        ++work.pretraining_epochs;
    }
    work.training_physical_fes = 0;
    return work;
}

bool run_model_gradient_checks(std::string& report) {
    std::ostringstream stream;
    bool result = check_parameter_foundation(stream);
    stream << '\n';
    result = check_layer_norm(stream) && result;
    stream << '\n';
    result = check_attention(stream) && result;
    stream << '\n';
    result = check_ffn_post_norm(stream) && result;
    stream << '\n';
    result = check_losses(stream) && result;
    report = stream.str();
    return result;
}

}  // namespace taae
