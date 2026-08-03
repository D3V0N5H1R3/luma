<!--
Thanks for contributing to Luma! Please fill out the sections below and delete
this comment. Keep the PR title in Conventional Commit form, for example:
  feat: add String.center
  fix: correct DateTime.add_months overflow at year boundaries
See CONTRIBUTING.md for the full workflow.
-->

## Summary

<!-- What does this PR change, and why? -->

## Related issues

<!-- e.g. "Closes #123" or "Refs #456". Delete this section if there are none. -->

## Type of change

<!-- Mark all that apply with an [x]. This should match your title's prefix. -->

- [ ] `feat` — new feature
- [ ] `fix` — bug fix
- [ ] `refactor` — code restructuring, no behaviour change
- [ ] `test` — adding or updating tests
- [ ] `docs` — documentation only
- [ ] `chore` — tooling, config, dependencies, or CI

## How was this tested?

<!--
Commands you ran (e.g. `ctest --preset default`), the platforms you tried,
any new tests added, and examples exercised (`python scripts/run_luma_examples.py`).
-->

## Checklist

- [ ] My commits follow the [Conventional Commits](https://www.conventionalcommits.org/) style (`feat:`, `fix:`, `refactor:`, `test:`, `docs:`, `chore:`), use the imperative mood, and keep the first line under 72 characters.
- [ ] I built the project and the C++ test suite passes locally (`ctest --preset default`).
- [ ] The Luma feature tests pass locally (`python scripts/run_luma_tests.py`).
- [ ] I formatted my C++ changes with clang-format 18 and clang-tidy reports no new findings.
- [ ] I ran the relevant linters/formatters for any other languages I touched (see [CONTRIBUTING.md](../blob/main/CONTRIBUTING.md#linters-and-formatters)).
- [ ] I added or updated tests where it makes sense.
- [ ] I updated the documentation (README, `documents/`, or `instructions/`) if behaviour changed.
- [ ] My change introduces no new compiler warnings.
