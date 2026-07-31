# T25 H0-H4 mathematical and HPC analysis

## H0: authority, source facts and reproduction boundary

- Target: Rodrigues et al., DOI `10.5194/wes-9-321-2024`; the 21-page publisher PDF and extracted text are hash-frozen in the controlling contract.
- The paper-linked CC-BY-4.0 Zenodo dataset, DOI `10.5281/zenodo.10402450`, is pinned by archive MD5 and embedded Git revision. It supplies processed result tensors, `Hornsrev1_xl.py`, partial helpers and a plotting notebook, but not a complete executable experiment environment.
- PyWake v2.5.0, revision `cd5ff8363ae2615a92860d409e748b4a0431f33d`, is the executable model authority for coordinates, wind resources, turbine curves, distance convention, wake propagation and source-effective-speed thrust coefficients. Direct PyWake source-oracle runs freeze IEA-37 16/36/64 and Horns Rev 100 AEP anchors in the contract.
- TOPFARM/SciPy revisions, author SLSQP trajectories, complete launch scripts, optimized coordinates, random bitstreams and Horns timing arrays are absent. The helper defaults to 1000 SciPy iterations while both public result tensors store `max_iter=5000`. NLopt Kraft SLSQP and counter-keyed random streams are declared replacements, not author-code replay; the production value 5000 is therefore an explicitly separate objective-callback cap, and every callback plus physical-layout count is retained.
- Every missing field, conflict and completion is stated in `shared/contracts/core99_t25_rodrigues_2024.json` and at the beginning of every implementation unit.

## H1: mathematical work decomposition

For `N` turbines and `S=Nwd*Nws` flow states, a complete AEP evaluation has `O(S*N^2)` ordered wake interactions. Horns Rev uses `S=360*23=8280`, while the IEA-37 case uses 360 directions and one speed. Central finite differences require `2*(2*N)+1` complete evaluations for one coordinate gradient. The exact derivative graph contains the path coordinate geometry to wake deficit, target effective speed, source V80 thrust coefficient and every downstream wake.

SMAST evaluates candidate cells under the turbines already placed. A naive implementation repeats complete partial-farm simulations and has quadratic placed-turbine work. The implemented cache retains candidate-by-flow-state squared wake sums and adds only the new source contribution, reducing the update work to `O(Ncandidate*S*N)` while preserving Algorithm 1's insertion semantics. One physical function evaluation remains one complete layout AEP evaluation; candidate flow updates are reported separately.

## H2: exact derivative and legal parallelism

- Each flow state has an independent wake-order forward traversal. The forward pass propagates effective-speed-dependent V80 CT; the reverse pass accumulates all coordinate derivatives through the CT dependency in `O(N^2)` work.
- Flow states write fixed output slots and are reduced in fixed index order. One-worker and all-worker AEP and gradients are therefore bitwise identical.
- Independent multi-start optimizations are scheduled at the outer level with one inner worker each. A single optimization uses one persistent all-core executor for flow states; nested oversubscription is forbidden.
- Central finite differences are validation-only. Production SLSQP uses the fixed reverse derivative.

## H3: pure-C++ realization and source-oracle correction

The implementation is C++20 with `-O3 -march=native -ffp-contract=off` and strict warnings. It embeds the public IEA-37 YAML decimal coordinates, Horns Rev geometry, V80 tables and both wind resources. PyWake's meteorological wind-direction transform is used exactly. The Horns evaluator follows `PropagateDownwind`: wake amplitude uses ambient reference speed, while each source CT is evaluated at its propagated effective speed. This distinction changed the preliminary Horns result and was corrected before admission.

Pinned-source validation gives C++ AEP values within `1e-5 GWh` of PyWake v2.5.0 for IEA-37 16/36/64 and Horns Rev 100. The Horns exact reverse derivative is also checked against a central finite-difference gradient on a nonsymmetric perturbed layout.

## H4: admission and formal protocol

H5 requires strict compilation, C++ unit tests, AddressSanitizer plus UndefinedBehaviorSanitizer, direct source-oracle AEP checks, central-finite-difference gradient checks and one/all-worker numerical identity. H6 measures fixed Horns Rev 100-turbine evaluator and exact-gradient work plus IEA-37 64-turbine SMAST with one and all visible Waffle cores.

After immutable Waffle H6 admission, the deterministic Horns matrix covers 100/200/300/400/500 turbines and the selected source-oracle, exact-gradient and finite-difference roles. The IEA campaign runs 30 paper-native stochastic roles per seed for 25 platform seeds, scheduling all visible cores across independent starts. Its NLopt stopping budget is 5000 objective callbacks per run, not a claim of identity with 5000 SciPy iterations. Of 65 mapped paper roles, the five complex-step comparators and five pair-spacing-cost diagnostics remain explicit observation-only records; they are not the proposed production method and are not fabricated as C++ reruns. Public 55,000-plus author result records remain external evidence and are never relabelled as platform reruns. Every platform result retains source commit, binary hash, physical work, worker receipt, timings, constraints, AEP and scientific hash.
