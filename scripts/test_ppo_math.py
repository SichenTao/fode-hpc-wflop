#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: Build-and-run mathematical fixture for the T42 seeded C++ PPO kernel
Paper title: Reinforcement Learning-Based Particle Swarm Optimization for Wind Farm Layout Problems
DOI: 10.1016/j.energy.2024.134050
Public author code URL: https://raw.githubusercontent.com/toyamaailab/toyamaailab.github.io/main/resource/RPSO_Wind_Code.zip
Public author code revision or archive hash: sha256:44e89c033e90f5aaaa9b84c826c95f29d3b8ad73dd363ff68de99418cdfa93a2
Public code provides: 2-256-64 actor/critic topology, categorical policy,
discounted returns, clipped PPO loss, Adam settings, and K=80
Known missing information: a frozen seeded author policy and reproducible author
training lifecycle
Known source conflicts: unseeded lifecycle, incorrect action-log-probability
calculation, and paper/source action-step disagreement
Reconstruction verified here: deterministic initialization and categorical
sampling, softmax/log-probability consistency, terminal-aware gamma=0.99
returns, clipping, 80-epoch Adam learning, and deterministic replay
Claim boundary: mathematical-kernel fixture only; passing it does not reproduce
the complete RLPSO method or the paper's numerical results
Last evidence audit date: 2026-07-29
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import tempfile
import textwrap


FIXTURE = r"""
#include "wflop/ppo.hpp"

#include "fode/rng.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <numeric>
#include <vector>

namespace {

bool close(double lhs, double rhs, double tolerance = 1.0e-11) {
    return std::abs(lhs - rhs) <= tolerance;
}

void require_probabilities(
    const std::array<double, wflop::ppo::Hyperparameters::action_dimension>& p
) {
    const double sum = std::accumulate(p.begin(), p.end(), 0.0);
    assert(close(sum, 1.0, 1.0e-13));
    for (const double value : p) {
        assert(value > 0.0);
        assert(std::isfinite(value));
    }
}

double central_difference(
    const std::function<double(double)>& objective,
    double value
) {
    constexpr double step = 1.0e-6;
    return (
        objective(value + step) - objective(value - step)
    ) / (2.0 * step);
}

}  // namespace

int main() {
    using wflop::ppo::Hyperparameters;
    using wflop::ppo::RngKey;
    using wflop::ppo::SeededPpo;
    using wflop::ppo::Transition;

    const Hyperparameters defaults;
    static_assert(Hyperparameters::state_dimension == 2);
    static_assert(Hyperparameters::first_hidden_width == 256);
    static_assert(Hyperparameters::second_hidden_width == 64);
    static_assert(Hyperparameters::action_dimension == 4);
    assert(close(defaults.gamma, 0.99));
    assert(close(defaults.clip_epsilon, 0.2));
    assert(close(defaults.learning_rate, 0.001));
    assert(close(defaults.adam_beta1, 0.9));
    assert(close(defaults.adam_beta2, 0.999));
    assert(defaults.update_epochs == 80);

    const fode::CounterRng rng(202607290042ULL);
    SeededPpo first(rng, 17);
    SeededPpo replay(rng, 17);
    SeededPpo other_stream(rng, 18);
    assert(close(
        first.parameter_checksum(),
        replay.parameter_checksum(),
        0.0
    ));
    assert(!close(
        first.parameter_checksum(),
        other_stream.parameter_checksum(),
        1.0e-9
    ));

    const std::array<double, 2> state{0.37, 0.61};
    const RngKey key{5, 11, 3, 7, 13};
    const auto first_sample = first.sample_action(state, rng, key);
    const auto replay_sample = replay.sample_action(state, rng, key);
    require_probabilities(first_sample.evaluation.probabilities);
    assert(first_sample.action == replay_sample.action);
    assert(close(
        first_sample.log_probability,
        replay_sample.log_probability,
        0.0
    ));
    assert(close(
        first_sample.log_probability,
        std::log(first_sample.evaluation.probabilities[
            static_cast<std::size_t>(first_sample.action)
        ]),
        1.0e-14
    ));

    std::vector<Transition> return_fixture(3);
    return_fixture[0].reward = 1.0;
    return_fixture[1].reward = 2.0;
    return_fixture[1].terminal = true;
    return_fixture[2].reward = 3.0;
    const auto returns =
        wflop::ppo::discounted_returns(return_fixture, 0.99);
    assert(close(returns[0], 2.98));
    assert(close(returns[1], 2.0));
    assert(close(returns[2], 3.0));
    assert(close(wflop::ppo::clipped_surrogate(1.5, 2.0, 0.2), 2.4));
    assert(close(wflop::ppo::clipped_surrogate(0.5, -2.0, 0.2), -1.6));

    std::array<double, 4> logits{0.2, -0.1, 0.4, 0.0};
    const auto actor_objective = wflop::ppo::clipped_actor_objective(
        logits,
        2,
        -1.1,
        0.7,
        0.2,
        0.01
    );
    for (std::size_t coordinate = 0;
         coordinate < logits.size();
         ++coordinate) {
        const double numerical = central_difference(
            [&](double value) {
                auto perturbed = logits;
                perturbed[coordinate] = value;
                return wflop::ppo::clipped_actor_objective(
                    perturbed,
                    2,
                    -1.1,
                    0.7,
                    0.2,
                    0.01
                ).loss;
            },
            logits[coordinate]
        );
        assert(close(
            numerical,
            actor_objective.logit_gradient[coordinate],
            2.0e-7
        ));
    }
    const auto clipped_actor_objective =
        wflop::ppo::clipped_actor_objective(
            logits,
            2,
            -2.0,
            0.7,
            0.2,
            0.01
        );
    for (std::size_t coordinate = 0;
         coordinate < logits.size();
         ++coordinate) {
        const double numerical = central_difference(
            [&](double value) {
                auto perturbed = logits;
                perturbed[coordinate] = value;
                return wflop::ppo::clipped_actor_objective(
                    perturbed,
                    2,
                    -2.0,
                    0.7,
                    0.2,
                    0.01
                ).loss;
            },
            logits[coordinate]
        );
        assert(close(
            numerical,
            clipped_actor_objective.logit_gradient[coordinate],
            2.0e-7
        ));
    }
    const auto critic_objective =
        wflop::ppo::critic_squared_error_objective(0.3, -0.2, 0.5);
    const double numerical_critic = central_difference(
        [](double value) {
            return wflop::ppo::critic_squared_error_objective(
                value, -0.2, 0.5
            ).loss;
        },
        0.3
    );
    assert(close(
        numerical_critic,
        critic_objective.value_gradient,
        1.0e-10
    ));

    std::vector<Transition> trajectory;
    trajectory.reserve(12);
    for (std::uint64_t index = 0; index < 12; ++index) {
        const std::array<double, 2> observation{
            0.1 + 0.03 * static_cast<double>(index),
            0.9 - 0.02 * static_cast<double>(index)
        };
        const auto sample = first.sample_action(
            observation,
            rng,
            RngKey{23, 29, index, 0, 0}
        );
        const auto replay_action = replay.sample_action(
            observation,
            rng,
            RngKey{23, 29, index, 0, 0}
        );
        assert(sample.action == replay_action.action);
        trajectory.push_back(Transition{
            observation,
            sample.action,
            sample.log_probability,
            index % 3 == 0 ? 1.0 : -0.2,
            index == 5 || index == 11
        });
    }

    const double before = first.parameter_checksum();
    const auto report = first.update(trajectory);
    const auto replay_report = replay.update(trajectory);
    const double after = first.parameter_checksum();
    assert(report.epochs == 80);
    assert(report.transitions == trajectory.size());
    assert(report.adam_step == 80);
    assert(first.adam_step() == 80);
    assert(!close(before, after, 1.0e-10));
    assert(close(after, replay.parameter_checksum(), 1.0e-12));
    assert(close(report.actor_loss, replay_report.actor_loss, 1.0e-13));
    assert(close(report.critic_loss, replay_report.critic_loss, 1.0e-13));
    assert(std::isfinite(report.mean_return));
    assert(std::isfinite(report.actor_loss));
    assert(std::isfinite(report.critic_loss));
    assert(std::isfinite(report.entropy));
    require_probabilities(first.evaluate(state).probabilities);

    std::cout
        << "ppo_math_fixture_pass"
        << " topology=2-256-64-4/2-256-64-1"
        << " epochs=" << report.epochs
        << " adam_step=" << report.adam_step
        << '\n';
    return 0;
}
"""


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compile and execute the standalone seeded C++ PPO fixture."
    )
    parser.add_argument(
        "--cxx",
        default="g++",
        help="C++20 compiler executable (default: g++)",
    )
    parser.add_argument(
        "--project-root",
        type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parents[1],
    )
    args = parser.parse_args()
    root = args.project_root.resolve()
    ppo_source = root / "hpc/wflop_cpp/src/algorithms/ppo.cpp"
    with tempfile.TemporaryDirectory(prefix="wflop-ppo-fixture-") as directory:
        temporary = pathlib.Path(directory)
        fixture_source = temporary / "ppo_math_fixture.cpp"
        executable = temporary / "ppo_math_fixture"
        fixture_source.write_text(textwrap.dedent(FIXTURE), encoding="utf-8")
        subprocess.run(
            [
                args.cxx,
                "-std=c++20",
                "-O2",
                "-ffp-contract=off",
                "-Wall",
                "-Wextra",
                "-Wpedantic",
                "-Wconversion",
                "-Wshadow",
                "-I",
                str(root / "hpc/wflop_cpp/include"),
                "-I",
                str(root / "hpc/fode_cpp/include"),
                str(ppo_source),
                str(fixture_source),
                "-o",
                str(executable),
            ],
            check=True,
        )
        completed = subprocess.run(
            [str(executable)],
            check=True,
            text=True,
            capture_output=True,
        )
    print(completed.stdout.strip())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
