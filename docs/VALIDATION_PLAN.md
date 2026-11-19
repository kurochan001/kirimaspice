# Validation improvement plan

[日本語](VALIDATION_PLAN.ja.md) · [Development](DEVELOPMENT.md) · [Known issues](../KNOWN_ISSUES.md)

**Status: working draft, recorded 2026-11-15.** This document preserves the
priorities discussed after the repository review, with completion criteria
and a starting point for the next session. The English version is the main
version; the Japanese version follows it.

The proposed direction is to limit new features while improving validation
of the existing implementation. This draft does not approve changes to
model specifications, existing expected values, the known-failure list,
dependencies, CI policy or published text. The approval rules remain in
[CLAUDE.md](../CLAUDE.md) and [AI_POLICY](spec/AI_POLICY.md).

## Starting point

The latest local review build was based on commit `75f8cb0` with uncommitted
review fixes, on macOS 27.2 arm64, GCC 16.2.0 and CMake 4.4.4. Running
`python3 tools/local_ci.py --jobs 8 --history` registered 1,276 tests:
1,246 passed, 7 failed with all 7 already listed as known failures,
22 skipped and 1 was disabled. Build, repository-content, history and SPDX
checks passed. These are local measurements of that working tree; other
platforms have not been checked again for these fixes.

The fixes cover sampled Monte Carlo parameters, parameter-sweep includes
and subcircuits, DC/AC boundaries and failure reporting, transfer-function
and Fourier boundaries, CLI/WAV validation, Python/REPL result handling,
and gaps in the repository history scan. The new regression cases are in
[test_analysis_edge_cases.cpp](../tests/unit/test_analysis_edge_cases.cpp)
and [test_tool_edge_cases.py](../tests/scripts/test_tool_edge_cases.py).
Passing them does not establish the accuracy of every device model.

At the time of recording, the implementation fixes are uncommitted.
Corrections to 22 documentation files, obsolete observations in 2
specification files, and a CI result-classification fix have been prepared
but have not been approved or applied. Check the actual working tree and
approval record before resuming; this paragraph is a dated snapshot.

## Priorities and completion criteria

Use this summary to locate the next task; the checklists below hold the
individual completion records. The fifth item applies throughout the work.

| Priority | Work | Completion evidence |
| --- | --- | --- |
| 1 | Review and preserve the fixes | commits, expected-value grounds and a local-CI summary |
| 2 | Reliable CI and reference comparisons | correct failure classification, executed comparisons and reasons for remaining exceptions |
| 3 | Verified scope from basic circuits to device models | analytic/reference checks with conditions and tolerances recorded |
| 4 | Equivalent paths and failure states | regression tests for matching results, restored state and propagated failures |
| 5 | A repeatable review process | reproduction, grounds, shared implementation fix and regression evidence for each issue |

### 1. Review and preserve the current fixes

- [ ] Review changes by purpose: analyses, CLI/output, Python tools and
  repository-content checks. Inspect the grounds of the regression tests
  as well as the implementation.
- [ ] Commit reviewed changes in small units with their regression tests
  and a record of the broken rule, its implementation point and the other
  paths using it, as required by CLAUDE.md.
- [ ] Resolve the pending documentation, specification-observation and CI
  proposals through their required approval process.

Completion evidence: identifiable commits, the expected-value grounds and
a local-CI summary for the resulting revision. A push remains a separate
action requiring an explicit request.

### 2. Make CI outcomes and comparison coverage reliable

- [x] Fix CI classification so an unavailable test executable cannot be
  treated as a successful check. Done in `cef412f` (approved before
  committing): `tools/local_ci.py` fails on a test that CTest could not
  start, an unknown status, a missing or unreadable result file and a run
  without tests, removes an old result file first, and keeps skipped and
  disabled tests. `tests/scripts/test_tool_edge_cases.py` exercises the
  cases, except the removal of the old file. CI run 44613253295 after the
  push to `e8cc364` confirmed the forms: on Ubuntu 24.04 with CMake
  3.31.6, GCC 13.3.0 and clang 18.1.3, both jobs classified the file as
  expected. The totals were 1270 passed, 6 failed (all listed), 3 skipped,
  1 disabled, and no `did not run`. The third skip is
  `BSIM4ComprehensiveTest.NgspiceComparison`, which skips itself when
  ngspice is not installed; the CI workflow does not install ngspice,
  the Nightly does.
- [x] Restore the 20 reference comparisons. Done in `ebde192`, `810c728`
  and `5097fa0`: the tests read `validation/`, all 20 pass on Linux
  (x86-64) with clang 20.1.8 and with GCC 15.2.0, and CI run 44613253295
  passed them with GCC 13.3.0 and clang 18.1.3; other platforms are not
  confirmed. The phase
  mismatch of `MOSFET2StageAmplifierValidation.MatchesNgspice` was in the
  reference: its file held 0 V at every frequency, from a netlist whose
  transistors were cut off, and 11.7 degrees is the phase of the correct
  output. The simulator agrees with ngspice-45.2 and with the small-signal
  solution worked by hand within 3e-7 dB. The helper that reported 0 dB
  against a zero reference was corrected.
- [ ] Resolve the six known failures caused by missing model files, with
  the origin and redistribution terms checked before adding any files.
- [x] Investigate the remaining CMOS near-zero assertion against the
  analytic solution and floating-point error. Done in `6088f8a`: the exact
  value is 0 V and -4.1e-25 V is roundoff; the lower bound now allows
  -1e-12 V, and the test leaves `tests/known_failures.txt`.
- [ ] Account for the disabled BSIM4 transient test described in
  KNOWN_ISSUES. The BSIM4 temperature test now runs against the committed
  ngspice-45.2 reference (commit `7f3bd69`); on Linux (x86-64) the model
  matches it within 0.65% (Vth), 2.41% (Ids) and 2.44% (Vdsat). The VDSAT
  branch test runs against values verified with ngspice-45.2 (commit
  `df3b036`).

Completion evidence: genuine failure/skip classification, comparisons that
execute, a reason and next action for each remaining exception, and
reproducible evidence for any discrepancy. Do not make a comparison pass
by silently loosening its tolerance or adding a known-failure exemption.

### 3. Establish a verified scope, starting with basic circuits

- [ ] Check resistors, RLC networks and independent sources in operating
  point, DC sweep, AC and transient analyses against analytic solutions.
  Check branch-current signs, units and saved results as well as voltages.
- [ ] Extend the verified scope to diodes and BJTs, then to MOS models,
  with each model's supported effects and limits stated explicitly.
- [ ] Combine analytic checks with reference runs of ngspice, recording
  its version, model parameters, analysis settings and relevant options.
  Existing reference data without recorded provenance needs investigation.
- [ ] Record the verified device, analysis, operating conditions and error
  tolerance in the public documentation after review and approval.

Completion evidence: repeatable tests with independently grounded expected
values and stated limits for each claimed range. Overall test counts are
kept separate from those accuracy claims.

### 4. Check equivalent paths and failure states

- [x] Compare normal and fast Monte Carlo execution using identical
  sampled device values, and compare sequential and parallel execution
  where both modes exist. `test_monte_carlo_paths` compares the two
  paths run by run for resistor and Level1 KP variations (commit
  `97a4ba7`). Sequential and parallel now give the same samples for the
  same seed (commit `9b9c19f`); before the fix 19 of 20 runs differed
  between the two builds, and a test now checks the per-run seed.
- [ ] Compare equivalent top-level/subcircuit netlists and library/CLI
  paths where the CLI implements that analysis. Include relative includes
  in parameter sweeps.
- [ ] Cover zero/one-point boundaries, invalid and non-finite inputs,
  unknown nodes/sources, convergence failure and repeated solver use.
  `DCBoundaryConditions.NonFiniteDeviceValueDoesNotReportSuccess` covers a
  NaN device value (commit `4be4e38`).
- [ ] Verify that failures propagate to the caller, temporary source and
  parameter changes are restored as specified, and old or partial results
  cannot be mistaken for a successful new run.

Completion evidence: grounded regression cases that exercise the relevant
paths and verify both numerical results and failure-state handling.

### 5. Make every future fix reviewable

Use the same sequence for each issue:

1. Write a minimal reproduction and record the observed failure.
2. Establish the expected result from a specification, analytic solution
   or reference run with its version recorded.
3. Fix the shared implementation of the rule, including its other callers.
4. Add a regression test, run the relevant checks and report remaining
   limitations.

Human review should focus on model meaning and the grounds of expected
values, as well as the code. Preserve the approval boundaries and commit
rules already defined in CLAUDE.md and AI_POLICY.

## Resume here

1. Read this plan, CLAUDE.md, AI_POLICY and KNOWN_ISSUES; inspect the current
   branch, working tree and approval record.
2. Check which review fixes and proposals have actually been applied. Start
   with the first incomplete priority; the next implementation task after
   preserving the review fixes is the approved CI-classification change,
   followed by restoring the 20 reference comparisons.
3. After meaningful changes, update completion markers with the commit,
   verification command and result, and record unresolved questions.
4. Before a requested push, run `python3 tools/local_ci.py` and attach its
   summary. Use `--history` when checking the full repository history.
