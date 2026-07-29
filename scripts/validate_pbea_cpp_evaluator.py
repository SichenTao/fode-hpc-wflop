#!/usr/bin/env python3
"""Independent scalar oracle for the repaired Zhang et al. TWFLO evaluator."""

import argparse
import json
import math
import subprocess


VELOCITY = [4.0, 6.333333333333334, 8.666666666666668,
            11.0, 13.333333333333334, 15.666666666666666]
THETA = {
    "ws1": [4.88692190558412, 6.10865238198015],
    "ws2": [0.0, 0.17453292519943295, 0.6981317007977318,
            0.8726646259971648, 1.3962634015954636, 2.96705972839036,
            math.pi, 4.014257279586958, 4.363323129985823,
            4.71238898038469, 4.886921905584122, 5.585053606381854],
}
PROB = {
    "ws1": [0.06122,0.10330,0.09673,0.08689,0.07906,0.07231,
            0.06258,0.10148,0.09601,0.09014,0.07819,0.07209],
    "ws2": [
        0.017,0.00941,0.00471,0.00285,0.00761,0.01755,
        0.02767,0.01616,0.00866,0.00558,0.01327,0.03025,
        0.02475,0.01496,0.00749,0.00533,0.01189,0.02841,
        0.023,0.01384,0.00673,0.00452,0.012,0.02588,
        0.02068,0.01248,0.00607,0.00415,0.01053,0.02321,
        0.0191,0.01125,0.00557,0.00381,0.00953,0.02071,
        0.00156,0.00718,0.01485,0.01162,0.01355,0.01632,
        0.00276,0.01125,0.02442,0.01913,0.02206,0.02699,
        0.00232,0.00994,0.02332,0.01807,0.0195,0.02433,
        0.00213,0.00937,0.02101,0.01666,0.01838,0.02275,
        0.00213,0.00829,0.01803,0.01491,0.01629,0.02163,
        0.0019,0.00709,0.017,0.01288,0.01494,0.01889],
}


def power_curve(speed):
    if speed < 2:
        return 0.0
    if speed < 12.8:
        return 0.3 * speed ** 3
    if speed < 18:
        return 629.1
    return 0.0


def overlap(distance, rotor, wake):
    distance = abs(distance)
    if distance >= rotor + wake:
        return 0.0
    if distance <= abs(wake - rotor):
        return math.pi * min(rotor, wake) ** 2
    a = math.acos(max(-1, min(1, (wake*wake + distance*distance - rotor*rotor)
                               / (2*wake*distance))))
    b = math.acos(max(-1, min(1, (rotor*rotor + distance*distance - wake*wake)
                               / (2*rotor*distance))))
    triangle = 0.5 * math.sqrt(max(
        0.0, (-distance+wake+rotor)*(distance+wake-rotor)
        *(distance-wake+rotor)*(distance+wake+rotor)))
    return wake*wake*a + rotor*rotor*b - triangle


def expected_power(layout, scenario):
    points = [(((cell-1) % 20)*231 + 115.5,
               ((cell-1) // 20)*231 + 115.5) for cell in layout]
    rotor = 38.5
    entrainment = 0.5 / math.log(80 / 0.00025)
    total = 0.0
    for direction, theta in enumerate(THETA[scenario]):
        c, s = math.cos(theta), math.sin(theta)
        rotated = [(c*x-s*y, s*x+c*y) for x, y in points]
        order = sorted(range(len(layout)), key=lambda i: -rotated[i][1])
        deficits = [0.0] * len(layout)
        for rank in range(1, len(layout)):
            downstream = order[rank]
            squared = 0.0
            for prior in range(rank):
                upstream = order[prior]
                along = rotated[upstream][1] - rotated[downstream][1]
                if along <= 0:
                    continue
                lateral = abs(rotated[downstream][0] - rotated[upstream][0])
                wake = rotor + entrainment * along
                deficit = (2/3) * rotor**2 / wake**2
                deficit *= overlap(lateral, rotor, wake) / (math.pi*rotor**2)
                squared += deficit**2
            deficits[downstream] = math.sqrt(squared)
        for speed_index, speed in enumerate(VELOCITY):
            total += sum(power_curve((1-d)*speed) for d in deficits) \
                * PROB[scenario][direction*6 + speed_index]
    return total


def convex_hull(points):
    points = sorted(set(points))
    def cross(o, a, b):
        return (a[0]-o[0])*(b[1]-o[1]) - (a[1]-o[1])*(b[0]-o[0])
    lower = []
    for point in points:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], point) <= 0:
            lower.pop()
        lower.append(point)
    upper = []
    for point in reversed(points):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], point) <= 0:
            upper.pop()
        upper.append(point)
    return lower[:-1] + upper[:-1]


def oracle(layout, scenario):
    points = [((cell-1)//20+1, (cell-1)%20+1) for cell in layout]
    hull = convex_hull(points)
    hull_area = abs(sum(
        hull[i][0]*hull[(i+1)%len(hull)][1]
        - hull[(i+1)%len(hull)][0]*hull[i][1]
        for i in range(len(hull)))) / 2
    width = max(x for x, _ in points) - min(x for x, _ in points) + 1
    height = max(y for _, y in points) - min(y for _, y in points) + 1
    area = 0.5 * (hull_area + width*height)
    construction = len(layout) * (
        2/3 + (1/3)*math.exp(-0.00174*len(layout)**2))
    return {
        "inverse_power": 1 / expected_power(layout, scenario),
        "land_area_grid_units": area,
        "total_cost": construction + 0.1*area,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    layouts = [
        list(range(1, 16)),
        [1, 20, 21, 40, 81, 100, 121, 140, 181, 200, 241, 260, 321, 380, 400],
        [2, 18, 39, 64, 87, 111, 139, 165, 203, 228, 257, 291, 326, 364, 399],
    ]
    maximum = 0.0
    maximum_absolute = 0.0
    cases = 0
    for scenario in ("ws1", "ws2"):
        for layout in layouts:
            raw = subprocess.check_output([
                args.binary, "--scenario", scenario,
                "--evaluate-layout", ",".join(map(str, layout))
            ], text=True)
            actual = json.loads(raw)
            expected = oracle(layout, scenario)
            for key, value in expected.items():
                error = abs(actual[key] - value)
                maximum_absolute = max(maximum_absolute, error)
                maximum = max(maximum, error / max(1.0, abs(value)))
                if error > 2e-12 * max(1.0, abs(value)):
                    raise SystemExit(
                        f"{scenario} {key}: actual={actual[key]} expected={value}")
            cases += 1
    print(json.dumps({
        "status": "pass",
        "cases": cases,
        "maximum_absolute_error": maximum_absolute,
        "maximum_scaled_absolute_error": maximum,
        "scaled_tolerance": 2.0e-12,
    }, sort_keys=True))


if __name__ == "__main__":
    main()
