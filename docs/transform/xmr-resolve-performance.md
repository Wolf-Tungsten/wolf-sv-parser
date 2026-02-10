# XMR Resolve Performance Notes

## Current Method (Module-Indexed Pending Ports)
When `xmr-resolve` finishes resolving reads/writes, it collects newly created
ports into `pendingOutputPorts` / `pendingInputPorts`. It then connects these
ports using a module index:

- Build an index once:
  - `module -> vector<InstanceRef>` where `InstanceRef` holds
    `{graph*, opId}` for each instance/blackbox.
- Group pending ports by `moduleName`:
  - `module -> vector<PendingPort*>`.
- For each module in pending ports:
  - Get the instance list from the index.
  - For each instance, apply all ports for that module:
    - Output ports: loop pending list and call `ensureInstanceOutput`.
    - Input ports: loop pending list and call `ensureInstanceInput`.

This reduces work to roughly:
`O(num_instance_ops + num_pending_ports + (sum over modules: instances * ports_per_module))`
and removes the expensive full-netlist scan per port.

## Previous Bottleneck (Historical)
The earlier approach scanned the entire netlist for every pending port:

- For each pending output port:
  - For every graph in the netlist:
    - For every operation in the graph:
      - If `op.kind` is instance/blackbox and `moduleName` matches, call
        `ensureInstanceOutput`.

- For each pending input port:
  - For every graph in the netlist:
    - For every operation in the graph:
      - If `op.kind` is instance/blackbox and `moduleName` matches, call
        `ensureInstanceInput` (after padding if needed).

This was effectively `O(num_pending_ports * num_instance_ops)` and became
dominant when `pendingOutputPorts` was large.

## Further Optimization Ideas
- Precompute a `std::unordered_set<std::string>` of existing input/output port
  names per instance to avoid repeated linear scans inside
  `ensureInstanceInput/Output`.
- If many ports share the same module, process them in batches to improve cache
  locality.
