# kirimaspice Documentation Guide

[日本語](README.ja.md) · [Project README](../README.md) · This English text is the main version; the Japanese version follows it, and where they differ, this text holds.

**Updated 2026-11-15** for commit `eaf0629`. Choose a document by what you
want to do. The guides and manuals say near their top the commit they were
checked against, and KNOWN_ISSUES and the examples say the commit of their
measurements; when a document and the program disagree, the program is
what runs, and the disagreement is a bug in one of them. Save the version line the program
logs first (`kirimaspice v0.0.0`) and the commit you built when you report
a result.

| Question | Start here | Then |
| --- | --- | --- |
| How do I build it and run a first netlist? | [User Manual](USER_MANUAL.md) | [Examples](../examples/README.md) |
| What does an option, element, directive or output file mean? | [Reference Manual](REFERENCE_MANUAL.md) | [Glossary](GLOSSARY.md) |
| I have a netlist for ngspice. What do I change? | [Moving from ngspice](PORTING.md) | [NETLIST_CONVENTIONS](spec/NETLIST_CONVENTIONS.md) |
| Is this result right? What is known to be wrong? | [KNOWN_ISSUES](../KNOWN_ISSUES.md) | [FAQ](FAQ.md#results-and-accuracy) |
| How does the solver assemble a circuit matrix and find the next voltage? | [How the circuit solver works](SOLVER_ALGORITHMS.md) | [Architecture](ARCHITECTURE.md) |
| How do I choose transient settings and check waveform accuracy? | [Transient analysis](TRANSIENT_ANALYSIS.md) (draft) | |
| How do I drive it from Python? | [User Manual: Python tools](USER_MANUAL.md#use-the-python-tools) | |
| How is the code organised? | [Architecture](ARCHITECTURE.md) | |
| How do I change the code, test it and push? | [Development](DEVELOPMENT.md) | [CONTRIBUTING](../CONTRIBUTING.md), [CLAUDE.md](../CLAUDE.md) |
| What has been agreed as a rule? | [docs/spec/](spec/) | |

## Use kirimaspice

- [User Manual](USER_MANUAL.md): building, writing a netlist, choosing the
  analyses, saving and reading the results, the Python tools,
  convergence, exit codes and troubleshooting.
- [Examples](../examples/README.md): small netlists with their analytic
  solutions; CTest checks each of them.
- [Reference Manual](REFERENCE_MANUAL.md): the command line, environment
  variables, exit codes, netlist syntax, elements, models, directives,
  options, output formats and build options.
- [Moving from ngspice](PORTING.md): what is the same, how the commands
  map, and a checklist for a netlist.
- [FAQ](FAQ.md) and [Glossary](GLOSSARY.md): common questions and the
  terms the documents use.
- [KNOWN_ISSUES](../KNOWN_ISSUES.md): what is known to be wrong or
  missing, with the commit at which each was measured.
- [To do](../TODO.md): the work still to do about it, in the order
  recommended.

## Change kirimaspice

- [Development](DEVELOPMENT.md): the build for development, the tests,
  the checks before a push, CI, adding a device model, fixing a bug and
  the rules for expected values.
- [Architecture](ARCHITECTURE.md): the parts of the library and the path
  of a netlist through them.
- [CONTRIBUTING](../CONTRIBUTING.md): what contributions are accepted.
- [CLAUDE.md](../CLAUDE.md) and [AGENTS.md](../AGENTS.md): the rules for
  coding agents, which bind people too.

## Rules and specifications

The documents under [spec/](spec/) are decisions; each gives its status.

- [NETLIST_CONVENTIONS](spec/NETLIST_CONVENTIONS.md): where SPICE dialects
  differ, the five rules in which kirimaspice follows ngspice (approved).
- [AI_POLICY](spec/AI_POLICY.md): how AI tools are used and who approves
  what (approved).

## Project

- [SECURITY](../SECURITY.md): reporting a vulnerability.
- [CODE_OF_CONDUCT](../CODE_OF_CONDUCT.md): Contributor Covenant 2.1.
- [THIRD_PARTY_NOTICES](../THIRD_PARTY_NOTICES.md): the dependencies and
  their licenses.
