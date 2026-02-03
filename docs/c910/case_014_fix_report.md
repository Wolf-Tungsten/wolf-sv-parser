# case_014 - C910 smart_run convert not_null Type assertion

## Problem summary
- Symptom: `wolf_emit` in C910 smart_run intermittently fails with
  `Assertion 'ptr' failed` in `slang::not_null<const slang::ast::Type*>::get()`.
- Latest reproduction log: `build/artifacts/c910_wolf_emit_warmup.log`.

```
[convert] [ERROR] - Convert worker failed: Assertion 'ptr' failed
  in file /workspace/wolf-sv-parser/external/slang/source/../include/slang/util/Util.h, line 175
  function: T slang::not_null<const slang::ast::Type *>::get() const [T = const slang::ast::Type *]
```

## Repro (observed)
1. `make -C tests/data/openc910/smart_run wolf_emit CODE_BASE_PATH=...`
2. `make run_c910_gprof TIMEOUT=300` (user report)

## Root cause analysis
- The failure is non-deterministic and only shows up in the parallel convert flow on
  the full C910 filelist.
- The assertion fires inside slang while resolving expression types, which strongly
  suggests a thread-safety issue (concurrent AST access / lazy type resolution) during
  parallel conversion.

## Fix details
- **Prebind pass:** trigger slang's internal `DiagnosticVisitor` through the public API
  `Compilation::getSemanticDiagnostics()` before parallel conversion.
  - Located in `runSlangPrebind()` in `src/convert.cpp`, invoked when parallel mode is enabled.
  - This traverses the entire AST and triggers all lazy bindings (types, initializers,
    assignments, delays, etc.), making subsequent multithreaded access safe.
  - Uses the official slang API instead of a custom visitor to ensure completeness and
    maintainability across slang updates.

## Validation
- User verification: multi-thread run no longer reports the `not_null` assertion.

## Files changed
- `src/convert.cpp`

## Open questions / follow-ups
- The underlying thread-safety issue in parallel conversion still needs isolation.
  If performance becomes a problem, consider a targeted lock around slang type
  resolution or a full pre-elaboration pass that guarantees thread-safe read-only
  traversal.
