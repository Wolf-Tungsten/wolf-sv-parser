---
name: convert-develop
description: Standardize development for Convert steps in wolf-sv-parser. Use when asked to implement, continue, or review a Convert STEP XXXX task (e.g., "convert step 17"). Cover reading docs/convert/convert-progress.md, implementing code/tests, updating docs/convert/{convert-architecture,convert-workflow,convert-progress}, and asking for review.
---

# Convert Develop

## Intake

- Extract the step number from the request and normalize to 4 digits (e.g., 17 -> 0017).
- Open `docs/convert/convert-progress.md` and locate the matching STEP section to capture target/plan/implementation.
- Read `docs/convert/convert-architecture.md` and `docs/convert/convert-workflow.md` to align with the current design and flow.
- Identify dependencies or prior steps that are being revised; plan to mark superseded content with strikethroughs.
- Ask for clarification when the STEP definition is missing or ambiguous.

## Implementation

- Follow `AGENTS.md` and existing patterns in `include/convert.hpp`, `src/convert.cpp`, and related files.
- Keep includes minimal; use C++20, 4-space indentation, and same-line braces.
- Keep comments terse and avoid non-ASCII identifiers.

## Tests

- Add or extend tests under `tests/convert/` with fixtures in `tests/data/convert/`.
- Register new test targets in `CMakeLists.txt` following the existing `convert-*` pattern and compile definitions.
- Run relevant tests when possible; otherwise report what was not run and why.

## Documentation Updates

- Update `docs/convert/convert-architecture.md` as a static, top-down description of components and relationships.
- Update `docs/convert/convert-workflow.md` as a runtime, front-to-back description of execution flow.
- Keep documentation organized from general to specific; after stating principles, add a concrete example.
- Append a new STEP section to `docs/convert/convert-progress.md` after the separator; preserve target/plan/implementation/finish status fields.

## Review Handoff

- Summarize code/test/doc changes and list tests executed.
- Ask the user to review or confirm next actions.
