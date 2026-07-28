# Scientific uncertainty decisions

Only choices that can change the mathematical problem, an algorithm identity,
the random-event sequence, or the stopping rule are recorded here. Engineering
names and directory layout do not require owner decisions.

## U-ISE-001 - ISE reduced-dimensional search

Status: `owner_confirmed_option_1`

Decision: `OWNER-20260727-002`

Evidence:

- Yang et al. (2023), Table 3 states \(D_c=D/3\).
- Equation (17) computes the sphere radius and angular components on the
  reduced dimension set denoted by `*`.
- Algorithm 1 does not define integerization when \(D\) is not divisible by
  three, how the reduced coordinates are selected, or whether the target
  individual may also be one of \(r_1,r_2,r_3\).
- The paper experiments include \(D=20\) and \(D=25\), so an unreported
  integerization rule necessarily existed.
- The inherited OpenWFLOP implementation uses `round(D/3)`, samples a random
  coordinate subset, excludes the target from three distinct parents, but
  calculates the radius over all \(D\) dimensions. The final point conflicts
  with Eq. (17).

Options:

1. Recommended: retain `round(D/3)`, select that many coordinates uniformly
   without replacement, choose three distinct parents excluding the target,
   and compute the Euclidean radius on the same selected coordinates.
2. Use `floor(D/3)` with the same subspace and parent rules.
3. Preserve the inherited all-\(D\) radius as a separately disclosed
   reconstruction, despite its weaker agreement with Eq. (17).

Impact:

- Options 1 or 2 change the inherited ISE trajectory and invalidate inherited
  ISE optimizer evidence, while leaving the benchmark and the other seven
  algorithms unchanged.
- Option 3 preserves existing output identity but cannot be described as a
  direct implementation of Eq. (17).

Selected resolution:

- Use Option 1. The owner instruction to complete the approved plan was
  received after the recommended choices were presented.
- ISE uses `round(D/3)`, uniformly samples that many coordinates without
  replacement, excludes the target while choosing three distinct parents,
  and computes the radius on the same selected coordinate set.

## U-CLSHADE-001 - CLSHADE adaptive shrink-factor update

Status: `owner_confirmed_option_1`

Decision: `OWNER-20260727-003`

Evidence:

- Yu et al. (2023), Eq. (21) defines the shrink factor from the improvement of
  the generation best \(x_{\mathrm{best},G}\) over
  \(x_{\mathrm{best},G-1}\).
- Algorithm 1 executes DE selection, chaotic local search, then updates the
  search radius.
- The inherited implementation updates the shrink factor only when the
  chaotic-local-search candidate improves the post-DE best. A best solution
  improved by DE alone does not shrink the radius.
- Initial \(r_0=0.01\) and \(\gamma_0=0.988\) are already approved disclosed
  reconstructions.

Options:

1. Recommended: compute Eq. (21) from the complete generation-best
   improvement after DE and chaotic local search relative to the previous
   generation best.
2. Preserve the inherited CLS-only improvement update and disclose it as a
   reconstruction.

Impact:

- Option 1 changes the inherited CLSHADE trajectory and invalidates inherited
  CLSHADE optimizer evidence, while leaving the benchmark and the other seven
  algorithms unchanged.
- Option 2 preserves existing output identity but weakens the claim of direct
  agreement with Eq. (21).

Selected resolution:

- Use Option 1. The owner instruction to complete the approved plan was
  received after the recommended choices were presented.
- The shrink factor compares the complete generation best after DE and CLS
  with the previous generation best, exactly following Eq. (21).
