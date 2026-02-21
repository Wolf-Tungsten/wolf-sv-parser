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
- The original fix using only `getSemanticDiagnostics()` was insufficient because:
  1. `DiagnosticVisitor` (used internally by `getSemanticDiagnostics()`) has `VisitStatements=false`
     and `VisitExpressions=false`, meaning it does not traverse into expression trees.
  2. This leaves some expression types unresolved, causing lazy type resolution to
     occur during multithreaded access, leading to race conditions.
  3. Additionally, the compilation was not frozen, allowing potential modifications
     during parallel access.

## Fix details
- **Prebind pass:** trigger slang's internal `DiagnosticVisitor` through the public API
  `Compilation::getSemanticDiagnostics()` before parallel conversion.
  - Located in `runSlangPrebind()` in `src/convert.cpp`, invoked when parallel mode is enabled.
  - This traverses the entire AST and triggers all lazy bindings (types, initializers,
    assignments, delays, etc.), making subsequent multithreaded access safe.
  - Uses the official slang API instead of a custom visitor to ensure completeness and
    maintainability across slang updates.

- **Enhanced expression prebind (2026-02-03 update):**
  - Added `ExpressionPrebindVisitor` that traverses all statements and expressions
    (`ASTVisitor<ExpressionPrebindVisitor, true, true>`) to force resolution of
    expression types that `DiagnosticVisitor` misses.
  - This visitor accesses `expr.type` for all expressions and `declaredType->getType()`
    for all symbols to ensure all lazy type bindings are resolved before parallel access.

- **Compilation freeze (2026-02-03 update):**
  - Added `root.getCompilation().freeze()` after prebind to ensure all internal data
    structures are finalized and to prevent any further modifications.
  - This follows the pattern used in slang's own `threadtest` tool for safe multithreaded
    AST access.

## Files changed
- `src/convert.cpp`
  - Added `ExpressionPrebindVisitor` struct for comprehensive expression type resolution
  - Modified `runSlangPrebind()` to include expression prebind and compilation freeze

## Validation
- Multi-thread run no longer reports the `not_null` assertion.
- C910 smart_run conversion completes successfully (verified `sim_top_wolf.sv` generated).
- All 23 unit tests pass.

## Open questions / follow-ups
- The prebind pass adds some overhead to the conversion process. If performance becomes
  an issue, consider profiling and optimizing the visitor implementation.
- Consider adding a command-line option to disable parallel conversion for debugging
  purposes when thread-safety issues arise.
