# Contributing to kirimaspice

[日本語](CONTRIBUTING.ja.md) · This English text is the main version; the Japanese version follows it, and where they differ, this text holds.

Thank you for your interest. kirimaspice is in early development; the
README gives the status and [KNOWN_ISSUES.md](KNOWN_ISSUES.md) the known
problems.

## What we accept for now

For now, kirimaspice accepts **issues and specification proposals only**.
Pull requests are not accepted; collaborators push to `main`. When the
project starts accepting more, this file and the README will say so.

We accept:

- **Bug reports**: a netlist (as small as you can make it), the command
  you ran, what you expected and why (the model's specification, an
  analytic solution, or the result of an open-source simulator with its
  version), and what you got.
- **Reports of wrong expected values** in tests, with the grounds.
- **Feature requests**: a device model, an analysis or a netlist feature to
  support.
- **Specification proposals**: changes or additions to the documents under
  `docs/spec/`.

In issues, do not paste text or figures from standards, papers, manuals or
code written by others, and do not include confidential information from
your employer, such as foundry model parameters. Cite section numbers,
describe things in your own words, and write your own minimal netlists.

Do not include the output, logs, waveforms or diagnostics of commercial
simulators, including anonymized excerpts or summaries. Do not run
commercial simulators for a report or ask others to do so.

## Use of AI tools

AI tools are welcome, and most of kirimaspice's code is AI-generated. If an
AI tool helped with a commit, say so with an `Assisted-by` trailer:

```text
Assisted-by: <tool>:<model and version>
```

Do not use `Co-authored-by` for AI tools. You remain responsible for every
line you submit: review it, test it, and be ready to explain it. See
[docs/spec/AI_POLICY.md](docs/spec/AI_POLICY.md).

## Changing expected results

Test expectations are the project's definition of correct behaviour, so
they are protected:

- A change that modifies, removes or loosens an existing expectation, that
  deletes or renames a test, or that adds a test to
  `tests/known_failures.txt` must be its own commit, separate from
  implementation changes under `src/`. Explain the reason and the grounds
  in the commit message.
- Adding new tests and their expectations in the same commit as the
  implementation is fine. Reviewers check where the expectation comes from.

## Building and testing

kirimaspice is built with CMake 3.25 or newer, a C++23 compiler, Ninja and
Git (CMake fetches the dependencies with it). SuiteSparse (KLU) is taken
from the system and Python 3 is needed for the tests. See the README for the commands. Before pushing, run:

```sh
python3 tools/local_ci.py
```

It configures, builds and tests in `build/`, compares the failures with
`tests/known_failures.txt`, and runs `tools/check_banned.py` and
`tools/check_spdx.py`. Attach its summary when you report a change.
[DEVELOPMENT](docs/DEVELOPMENT.md) describes the tests, CI and the steps
to add a device model, and [ARCHITECTURE](docs/ARCHITECTURE.md) the code.

## Code

- Each file of the project's own code starts with two comment lines, the
  copyright line and the SPDX line; `python3 tools/check_spdx.py --add`
  writes them.
- Format with the repository's `.clang-format`.
- Follow [CLAUDE.md](CLAUDE.md), which binds people as well as agents:
  device equations are written once, fixes state the rule they restore,
  and nothing in the simulator knows about the tests.

## Commits

- One commit per purpose. Messages in English, the first line at most 72
  characters.
- The author email is your GitHub noreply address.
- Do not add `Signed-off-by`: the project does not use the DCO.

The trailers a commit may carry ([AI_POLICY](docs/spec/AI_POLICY.md#record-of-human-contributions)):

| Trailer | When |
| --- | --- |
| `Assisted-by: <tool>:<model and version>` | an AI tool helped with the commit |
| `Approved-by: <name> <noreply address>` | the commit records a person's approval of a specification or of an expected value |
| `Reviewed-by: <name> <noreply address>` | a person read the commit, made with AI, before it was pushed |
| `Co-authored-by`, `Signed-off-by` | never: not for AI tools, and the project does not use the DCO |
