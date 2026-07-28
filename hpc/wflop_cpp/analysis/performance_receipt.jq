def median:
  sort as $values
  | ($values | length) as $n
  | if $n % 2 == 1
    then $values[($n / 2 | floor)]
    else (
      $values[$n / 2 - 1] + $values[$n / 2]
    ) / 2
    end;

group_by([.algorithm_id, .case_id]) as $groups
| {
    schema_version: 1,
    gate: "paired_native_cpp_1_worker_vs_20_workers",
    result_count: length,
    binary_sha256: $binary_sha256,
    source_freeze_sha256: $source_freeze_sha256,
    groups: [
      $groups[] |
      {
        algorithm_id: .[0].algorithm_id,
        case_id: .[0].case_id,
        cpp1_end_to_end_median_seconds:
          ([.[] | select(.performance_endpoint == "cpp1")
            | .timing_seconds.end_to_end] | median),
        cpp20_end_to_end_median_seconds:
          ([.[] | select(.performance_endpoint == "cpp20")
            | .timing_seconds.end_to_end] | median),
        end_to_end_speedup_cpp1_over_cpp20:
          (
            ([.[] | select(.performance_endpoint == "cpp1")
              | .timing_seconds.end_to_end] | median)
            /
            ([.[] | select(.performance_endpoint == "cpp20")
              | .timing_seconds.end_to_end] | median)
          ),
        semantic_pair_mismatches: (
          group_by(.repeat)
          | map(
              select(
                length != 2
                or .[0].best_expected_power_kw
                  != .[1].best_expected_power_kw
                or .[0].best_layout_1based
                  != .[1].best_layout_1based
              )
            )
          | length
        )
      }
    ]
  }
| .status = (
    .result_count == 1008
    and (.groups | length) == 24
    and all(.groups[]; .semantic_pair_mismatches == 0)
    and all(
      .groups[];
      if .case_id == "WS10tn80"
      then .end_to_end_speedup_cpp1_over_cpp20 > 1.0
      else true
      end
    )
  )
