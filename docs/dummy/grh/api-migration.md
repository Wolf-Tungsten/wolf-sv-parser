# grh API Migration Guide (GraphView/GraphBuilder, id-centric)

This document captures the current grh API shape so follow-on migrations can update callers consistently.

## Goals

- Make `Graph` storage use `ir::GraphView` for read-only fast paths.
- Switch mutations to `ir::GraphBuilder` with explicit `freeze()` back to `GraphView`.
- Make the public `Graph` API id-centric (`ValueId`/`OperationId`).
- Keep `Value`/`Operation` read-only snapshots created by `Graph`.
- Preserve netlist alias behavior while centralizing graph id allocation in `NetlistSymbolTable`.

## Core Concepts

### GraphView vs GraphBuilder

- `GraphView` is a compact, read-only structure for fast iteration.
- `GraphBuilder` is mutable and supports edits; it can be created from a `GraphView`.
- `Graph::freeze()` compacts the current builder state into a new `GraphView`.
- `freeze()` may remap ids; id stability is only guaranteed within a builder session.

### Id-centric access

- `Graph::createValue` and `Graph::createOperation` return `ValueId`/`OperationId`.
- All mutations are done via `Graph` methods using ids.
- Lookups return ids; use `ValueId::invalid()` / `OperationId::invalid()` on miss.

### Read-only snapshots

- `Graph::getValue(ValueId)` returns a `Value` snapshot.
- `Graph::getOperation(OperationId)` returns an `Operation` snapshot.
- `Value`/`Operation` instances are read-only; all edits go through `Graph`.

## Symbol Handling

- Graph-local names use `ir::GraphSymbolTable`.
- `Graph::internSymbol()` returns the existing id only when the symbol text is unbound; if the symbol is already bound to a value/op it returns invalid.
- `Graph::symbolText(SymbolId)` returns the string for a symbol.
- `Value`/`Operation` snapshots expose `symbolText()` for convenience.

### Symbol constraints

- Operation symbols are intended to be unique within a graph; avoid creating duplicates at call sites.
- Attribute keys are per-operation and may repeat across different operations.

## NetlistSymbolTable

`ir::NetlistSymbolTable` now only manages graph-level symbols and ids:

- `allocateGraphId(SymbolId)` assigns a new `GraphId` for a graph symbol.
- `lookupGraphId(SymbolId)` returns the `GraphId` for a symbol, if any.
- `symbolForGraph(GraphId)` returns the symbol for a graph id.

Netlist alias resolution behavior is unchanged.

## API Changes (Before -> After)

### Create values and operations

Before:

```cpp
Value& v = graph.createValue("a", width, isSigned);
Operation& op = graph.createOperation(OperationKind::kAdd, "add0");
op.addOperand(v);
```

After:

```cpp
ir::SymbolId symA = graph.internSymbol("a");
ir::ValueId v = graph.createValue(symA, width, isSigned);
ir::OperationId op = graph.createOperation(OperationKind::kAdd, graph.internSymbol("add0"));
graph.addOperand(op, v);
```

### Find and read

Before:

```cpp
Value* v = graph.findValue("a");
const Operation& op = graph.getOperation("add0");
```

After:

```cpp
ir::ValueId vId = graph.findValue("a");
if (vId.valid()) {
    Value v = graph.getValue(vId);
    std::string_view name = v.symbolText();
}
```

### Attributes

Before:

```cpp
op.setAttribute("constValue", value);
```

After:

```cpp
ir::SymbolId key = graph.internSymbol("constValue");
graph.setAttr(opId, key, value);
```

### Ports

Before:

```cpp
graph.bindInputPort("in", value);
```

After:

```cpp
graph.bindInputPort("in", valueId);
```

## Read/Write Behavior

- Read-only access uses the `GraphView` fast path when the graph is frozen.
- Any mutation triggers load into a `GraphBuilder` (one-time cost).
- Multiple mutations do not re-load; call `freeze()` when ready to compact.

## Migration Checklist

1. Replace `Value*`/`Operation*` storage with `ValueId`/`OperationId`.
2. Replace direct `Value`/`Operation` mutations with `Graph` methods.
3. Use `Graph::internSymbol()` for names and keep `SymbolId` around.
4. Fetch data via `Graph::getValue()` / `Graph::getOperation()`.
5. Use `symbolText()` on snapshots for string names.
6. Call `Graph::freeze()` when you need the compact view for fast iteration or serialization.
