#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T72 paper-equation and reconstructed-map H5 oracle
Paper/DOI: Constrained Multi-Objective Wind Farm Layout Optimization:
Novel Constraint Handling Approach Based on Constraint Programming;
10.1016/j.renene.2018.03.053
Public source: no author source or native maps were located; ISO 9613 formula
choices were independently checked against PyWake revision
5b07481ec9b3633a74844651648f266ba82a8b32
Missing/conflicts/completion:
hpc/core99_cpp/include/core99/sorkhabi_t72.hpp
Independence: this script re-derives the counter-keyed Voronoi maps, the
direction-conditioned wind distribution, Jensen/RSS AEP, ISO propagation,
spacing constraints, and all nine paper cases without importing production code
Method/problem semantic IDs: t72_chcp_nsga2_declared_reconstruction_v1;
t72_energy_noise_voronoi9_declared_reconstruction_v1
Controlling contract: shared/contracts/core99_t72_sorkhabi_2018.json
Claim boundary: equation and declared-reconstruction oracle, not an
optimization-front or unavailable native-map oracle
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess
from dataclasses import dataclass


MASK = (1 << 64) - 1
DOMAIN = 3000.0
MAP_AXIS = 15
CP_BINS = 150
CP_STEP = DOMAIN / (CP_BINS - 1)
HUB_HEIGHT = 80.0
RECEIVER_HEIGHT = 1.5
ROTOR_RADIUS = 38.5
MINIMUM_SPACING = 385.0
THRUST = 0.8
ROUGHNESS = 0.1
WAKE_EXPANSION = 0.5 / math.log(HUB_HEIGHT / ROUGHNESS)
WAKE_DEFICIT = 1.0 - math.sqrt(1.0 - THRUST)
FREQUENCIES = [63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0]
A_WEIGHTING = [-26.2, -16.1, -8.6, -3.2, 0.0, 1.2, 1.0, -1.1]
SCALES = [
    7.0, 5.0, 5.0, 5.0, 5.0, 4.0,
    5.0, 6.0, 7.0, 7.0, 8.0, 9.5,
    10.0, 8.5, 8.5, 6.5, 4.6, 2.6,
    8.0, 5.0, 6.4, 5.2, 4.5, 3.9,
]
DIRECTION_PROBABILITY = [
    0.0002, 0.0080, 0.0227, 0.0242, 0.0225, 0.0339,
    0.0423, 0.0290, 0.0617, 0.0813, 0.0994, 0.1394,
    0.1839, 0.1115, 0.0765, 0.0080, 0.0051, 0.0019,
    0.0012, 0.0010, 0.0017, 0.0031, 0.0097, 0.0317,
]


@dataclass(frozen=True)
class Point:
    x: float
    y: float


def splitmix64(value: int) -> int:
    value = (value + 0x9E3779B97F4A7C15) & MASK
    value = ((value ^ (value >> 30)) * 0xBF58476D1CE4E5B9) & MASK
    value = ((value ^ (value >> 27)) * 0x94D049BB133111EB) & MASK
    return (value ^ (value >> 31)) & MASK


def uniform(seed: int, generation: int, phase: int, individual: int,
            coordinate: int = 0, draw: int = 0) -> float:
    state = seed & MASK
    for value in [generation, phase, individual, coordinate, draw]:
        mixed_input = (
            value
            + 0x9E3779B97F4A7C15
            + ((state << 6) & MASK)
            + (state >> 2)
        ) & MASK
        state ^= splitmix64(mixed_input)
        state &= MASK
    return (splitmix64(state) >> 11) / 9007199254740992.0


def squared_distance(left: Point, right: Point) -> float:
    return (left.x - right.x) ** 2 + (left.y - right.y) ** 2


class Map:
    def __init__(self, availability: int, turbines: int):
        seed = 720000 + availability * 100 + turbines
        macro = DOMAIN / MAP_AXIS
        self.seeds = []
        for row in range(MAP_AXIS):
            for column in range(MAP_AXIS):
                index = row * MAP_AXIS + column
                x_fraction = 0.25 + 0.5 * uniform(seed, 0, 7201, index, 0)
                y_fraction = 0.25 + 0.5 * uniform(seed, 0, 7201, index, 1)
                self.seeds.append(
                    Point(
                        (column + x_fraction) * macro,
                        (row + y_fraction) * macro,
                    )
                )
        counts = [0] * (MAP_AXIS * MAP_AXIS)
        for y in range(CP_BINS):
            for x in range(CP_BINS):
                point = Point(x * CP_STEP, y * CP_STEP)
                counts[self.nearest(point)] += 1
        order = list(range(MAP_AXIS * MAP_AXIS))
        order.sort(key=lambda index: (uniform(seed, 0, 7202, index), index))
        target = round((100 - availability) * CP_BINS * CP_BINS / 100)
        unavailable = 0
        self.forbidden_cells = set()
        for cell in order:
            following = unavailable + counts[cell]
            if abs(following - target) <= abs(unavailable - target):
                self.forbidden_cells.add(cell)
                unavailable = following
            if unavailable >= target:
                break
        self.measured_availability = 1.0 - unavailable / (CP_BINS * CP_BINS)
        self.receptors = [
            point
            for index, point in enumerate(self.seeds)
            if index in self.forbidden_cells
        ]

    def nearest(self, point: Point) -> int:
        macro = DOMAIN / MAP_AXIS
        column = min(MAP_AXIS - 1, max(0, int(point.x / macro)))
        row = min(MAP_AXIS - 1, max(0, int(point.y / macro)))
        candidates = [
            candidate_row * MAP_AXIS + candidate_column
            for candidate_row in range(max(0, row - 2), min(MAP_AXIS, row + 3))
            for candidate_column in range(
                max(0, column - 2), min(MAP_AXIS, column + 3)
            )
        ]
        return min(
            candidates,
            key=lambda index: (squared_distance(point, self.seeds[index]), index),
        )

    def forbidden(self, point: Point) -> bool:
        if not 0.0 <= point.x <= DOMAIN or not 0.0 <= point.y <= DOMAIN:
            return True
        return self.nearest(point) in self.forbidden_cells


def weibull_cdf(speed: float, scale: float) -> float:
    if speed <= 0.0:
        return 0.0
    return 1.0 - math.exp(-(speed / scale) ** 2)


def wind_states():
    speeds = [4.0 + 0.5 * index for index in range(43)]
    direction_sum = sum(DIRECTION_PROBABILITY)
    result = []
    for direction in range(24):
        conditional = [
            weibull_cdf(speed + 0.25, SCALES[direction])
            - weibull_cdf(speed - 0.25, SCALES[direction])
            for speed in speeds
        ]
        conditional_sum = sum(conditional)
        probabilities = [
            DIRECTION_PROBABILITY[direction] / direction_sum
            * value / conditional_sum
            for value in conditional
        ]
        result.append(
            (
                math.fmod(97.5 + 15.0 * direction, 360.0),
                list(zip(speeds, probabilities)),
            )
        )
    return result


def power_kw(speed: float) -> float:
    if speed < 4.0 or speed > 25.0:
        return 0.0
    if speed < 15.0:
        return max(0.0, 140.86 * speed - 500.0)
    return 1500.0


def sound_power_db(speed: float) -> float:
    speeds = [3.0, 7.2, 7.9, 8.6, 9.3, 10.0, 11.5, 12.9, 25.0]
    levels = [97.1, 97.1, 99.7, 102.0, 103.4, 104.0, 104.0, 104.0, 104.0]
    if speed <= speeds[0]:
        return levels[0]
    if speed >= speeds[-1]:
        return levels[-1]
    for upper in range(1, len(speeds)):
        if speed <= speeds[upper]:
            fraction = (
                (speed - speeds[upper - 1])
                / (speeds[upper] - speeds[upper - 1])
            )
            return (
                levels[upper - 1]
                + fraction * (levels[upper] - levels[upper - 1])
            )
    raise AssertionError("unreachable")


def atmospheric_absorption(frequency: float) -> float:
    reference_temperature = 293.15
    triple_point = 273.16
    temperature = 293.15
    pressure_atm = 1.0
    saturation = 10.0 ** (
        -6.8346 * (triple_point / temperature) ** 1.261 + 4.6151
    )
    humidity = 80.0 / pressure_atm * saturation
    normalized = frequency / pressure_atm
    squared = normalized * normalized
    oxygen = 24.0 + 4.04e4 * humidity * (0.02 + humidity) / (0.391 + humidity)
    nitrogen = math.sqrt(reference_temperature / temperature) * (
        9.0
        + 2.8e2 * humidity
        * math.exp(-4.17 * ((reference_temperature / temperature) ** (1 / 3) - 1))
    )
    alpha = squared * (
        1.84e-11 * math.sqrt(temperature / reference_temperature)
        + (temperature / reference_temperature) ** -2.5
        * (
            1.275e-2 * math.exp(-2239.1 / temperature)
            / (oxygen + squared / oxygen)
            + 0.1068 * math.exp(-3352.0 / temperature)
            / (nitrogen + squared / nitrogen)
        )
    )
    return alpha * pressure_atm * 20.0 / math.log(10.0)


def ground_effect(frequency: float, horizontal: float) -> float:
    q_value = (
        0.0
        if horizontal <= 30.0 * (HUB_HEIGHT + RECEIVER_HEIGHT)
        else 1.0 - 30.0 * (HUB_HEIGHT + RECEIVER_HEIGHT) / horizontal
    )

    def a_value(height):
        return (
            1.5
            + 3.0 * math.exp(-0.12 * (height - 5.0) ** 2)
            * (1.0 - math.exp(-horizontal / 50.0))
            + 5.7 * math.exp(-0.09 * height**2)
            * (1.0 - math.exp(-2.8e-6 * horizontal**2))
        )

    def b_value(height):
        return (
            1.5
            + 8.6 * math.exp(-0.09 * height**2)
            * (1.0 - math.exp(-horizontal / 50.0))
        )

    def c_value(height):
        return (
            1.5
            + 14.0 * math.exp(-0.46 * height**2)
            * (1.0 - math.exp(-horizontal / 50.0))
        )

    def d_value(height):
        return (
            1.5
            + 5.0 * math.exp(-0.9 * height**2)
            * (1.0 - math.exp(-horizontal / 50.0))
        )

    def endpoint(value, height):
        if value == 63.0:
            return -1.5
        if value == 125.0:
            return -1.5 + a_value(height)
        if value == 250.0:
            return -1.5 + b_value(height)
        if value == 500.0:
            return -1.5 + c_value(height)
        if value == 1000.0:
            return -1.5 + d_value(height)
        return 0.0

    middle = -3.0 * q_value if frequency == 63.0 else 0.0
    if frequency != 4000.0:
        return endpoint(frequency, HUB_HEIGHT) + endpoint(
            frequency, RECEIVER_HEIGHT
        ) + middle
    low = endpoint(2000.0, HUB_HEIGHT) + endpoint(2000.0, RECEIVER_HEIGHT)
    high = endpoint(8000.0, HUB_HEIGHT) + endpoint(8000.0, RECEIVER_HEIGHT)
    return low + (4000.0 - 2000.0) / (8000.0 - 2000.0) * (high - low)


def transmission(source: Point, receiver: Point, frequency: float) -> float:
    horizontal = max(1.0, math.hypot(source.x - receiver.x, source.y - receiver.y))
    distance = math.sqrt(horizontal**2 + (HUB_HEIGHT - RECEIVER_HEIGHT) ** 2)
    return (
        -(20.0 * math.log10(distance) + 11.0)
        - ground_effect(frequency, horizontal)
        - atmospheric_absorption(frequency) * distance
    )


def feasible_layout(problem_map: Map, turbines: int) -> list[Point]:
    result = []
    for y in range(31):
        for x in range(31):
            candidate = Point(100.0 * x, 100.0 * y)
            if problem_map.forbidden(candidate):
                continue
            if all(
                squared_distance(candidate, current) >= MINIMUM_SPACING**2
                for current in result
            ):
                result.append(candidate)
                if len(result) == turbines:
                    return result
    raise SystemExit("T72 oracle could not construct a feasible layout")


def evaluate(layout: list[Point], problem_map: Map):
    states = wind_states()
    expected_power = 0.0
    for direction, speed_states in states:
        angle = math.radians(270.0 - direction)
        cosine = math.cos(angle)
        sine = math.sin(angle)
        factors = []
        for downstream, target in enumerate(layout):
            target_along = cosine * target.x + sine * target.y
            target_across = -sine * target.x + cosine * target.y
            squared_deficit = 0.0
            for upstream, source in enumerate(layout):
                if upstream == downstream:
                    continue
                source_along = cosine * source.x + sine * source.y
                distance = target_along - source_along
                if distance <= 0.0:
                    continue
                source_across = -sine * source.x + cosine * source.y
                radius = ROTOR_RADIUS + WAKE_EXPANSION * distance
                if abs(target_across - source_across) > radius:
                    continue
                deficit = WAKE_DEFICIT * ROTOR_RADIUS**2 / radius**2
                squared_deficit += deficit**2
            factors.append(max(0.0, 1.0 - math.sqrt(squared_deficit)))
        for speed, probability in speed_states:
            expected_power += probability * sum(
                power_kw(speed * factor) for factor in factors
            )
    aep = 8760.0 * expected_power / 1.0e6

    band_source_energy = [0.0] * len(FREQUENCIES)
    for _, speed_states in states:
        for speed, probability in speed_states:
            level = sound_power_db(speed)
            for band, weighting in enumerate(A_WEIGHTING):
                band_source_energy[band] += probability * 10.0 ** (
                    0.1 * (level + weighting)
                )
    maximum_spl = -math.inf
    for receptor in problem_map.receptors:
        acoustic_energy = 0.0
        for source in layout:
            for band, frequency in enumerate(FREQUENCIES):
                acoustic_energy += band_source_energy[band] * 10.0 ** (
                    0.1 * transmission(source, receptor, frequency)
                )
        maximum_spl = max(maximum_spl, 10.0 * math.log10(acoustic_energy))
    proximity = sum(
        max(0.0, MINIMUM_SPACING - math.sqrt(squared_distance(left, right)))
        for index, left in enumerate(layout)
        for right in layout[index + 1:]
    )
    return {
        "aep_gwh": aep,
        "maximum_spl_dba": maximum_spl,
        "proximity_violation_m": proximity,
        "regulatory_violation_m": 0.0,
        "feasible": proximity <= 1.0e-9,
    }


def close(left: float, right: float, tolerance: float = 5.0e-11) -> bool:
    return abs(left - right) <= tolerance * max(1.0, abs(left), abs(right))


def validate_case(binary: str, availability: int, turbines: int):
    problem_map = Map(availability, turbines)
    layout = feasible_layout(problem_map, turbines)
    expected = evaluate(layout, problem_map)
    csv = ",".join(
        str(value)
        for point in layout
        for value in (point.x, point.y)
    )
    completed = subprocess.run(
        [
            binary,
            "--land-availability-percent", str(availability),
            "--turbines", str(turbines),
            "--layout-csv", csv,
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    observed = json.loads(completed.stdout)
    if not close(
        observed["measured_land_availability"],
        problem_map.measured_availability,
    ):
        raise SystemExit("T72 reconstructed land-availability mismatch")
    if observed["receptors"] != len(problem_map.receptors):
        raise SystemExit("T72 reconstructed receptor-count mismatch")
    maximum_error = 0.0
    for field in [
        "aep_gwh",
        "maximum_spl_dba",
        "proximity_violation_m",
        "regulatory_violation_m",
    ]:
        left = float(observed["before"][field])
        right = float(expected[field])
        maximum_error = max(maximum_error, abs(left - right))
        if not close(left, right):
            raise SystemExit(
                f"T72 phi{availability} n{turbines} {field} mismatch: "
                f"{left} != {right}"
            )
    if observed["before"]["feasible"] is not expected["feasible"]:
        raise SystemExit("T72 feasibility mismatch")
    return {
        "problem_id": observed["problem_id"],
        "measured_land_availability": observed["measured_land_availability"],
        "restricted_areas_and_receptors": observed["receptors"],
        "aep_gwh": observed["before"]["aep_gwh"],
        "maximum_spl_dba": observed["before"]["maximum_spl_dba"],
        "maximum_absolute_error": maximum_error,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    arguments = parser.parse_args()
    cases = [
        validate_case(arguments.binary, availability, turbines)
        for availability in [70, 80, 90]
        for turbines in [5, 10, 15]
    ]
    print(json.dumps({"status": "pass", "cases": cases}, indent=2))


if __name__ == "__main__":
    main()
