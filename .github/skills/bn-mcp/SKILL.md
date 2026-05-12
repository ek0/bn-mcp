---
name: bn-mcp
description: 'Binary Ninja MCP server plugin — analyze binaries, inspect LLIL, query functions/strings/xrefs/types via MCP tools. Use when: analyzing a binary through Binary Ninja MCP; inspecting LLIL or SSA instruction trees; querying functions, strings, or cross-references; looking up struct/enum/typedef definitions; adding new MCP tools; writing unit tests; debugging build or ABI issues; registering external tools from other plugins.'
---

# bn-mcp

bn-mcp is a Binary Ninja plugin that exposes an MCP server over HTTP. Agents use it to load executables, inspect IL, list functions, query strings and cross-references through JSON-RPC 2.0 on `http://127.0.0.1:3142/mcp`.

## When to Use

- Analyzing a binary with Binary Ninja through MCP tools
- Inspecting LLIL or LLIL SSA instruction trees for a function
- Querying functions, strings, or cross-references in a loaded binary
- Adding new MCP tools to the bn-mcp plugin
- Writing unit tests for LLIL serialization
- Debugging build or ABI issues
- Registering external tools from another BN plugin

## Available Tools

All tools use `view_id` (returned by `load_executable`) to identify a binary.

### Binary Management

| Tool | Parameters | Description |
|---|---|---|
| `load_executable` | `path` (string) | Load an executable and run analysis. Returns view ID, arch, platform, entry point, function count. |
| `close_binary_view` | `view_id` (string) | Close a view and free resources. |
| `list_binary_views` | *(none)* | List all open views with ID, path, arch, platform. |

### Function & Data Discovery

| Tool | Parameters | Description |
|---|---|---|
| `list_functions` | `view_id` | List all functions: address, size, name. |
| `get_function_info` | `view_id`, `address` (int) | Function details: name, calling convention, return type, parameters, basic block count. |
| `get_strings` | `view_id`, optional `start`+`length` (int) | List strings: address, length, content. Optionally filter by address range. |
| `get_xrefs` | `view_id`, `address` (int) | Cross-references to an address: code refs (callers with function names) and data refs. |

### Types

| Tool | Parameters | Description |
|---|---|---|
| `list_types` | `view_id` | List all defined types: name, kind (struct/enum/union/typedef), size. |
| `get_type_info` | `view_id`, `type_name` (string) | Full type definition. Structs: members with name, type, offset, size. Enums: values with name and constant. Typedefs: underlying type. |

### Low Level IL

| Tool | Parameters | Description |
|---|---|---|
| `list_llil_instructions` | `view_id`, `address` (int) | List non-SSA LLIL: index, address, operation, text. |
| `list_llil_ssa_instructions` | `view_id`, `address` (int) | List SSA LLIL: index, address, operation, text with versioned registers. |
| `get_llil_expr_tree` | `view_id`, `address` (int), `instr_index` (int) | Recursive expression tree for a non-SSA instruction (JSON). |
| `get_llil_ssa_expr_tree` | `view_id`, `address` (int), `instr_index` (int) | Recursive expression tree for an SSA instruction (JSON). |

### Expression Tree Output Format

`get_llil_expr_tree` returns a JSON object like:

```json
{
  "instr_index": 2,
  "address": "0x40100a",
  "text": "[rbp - 8].q = rdi",
  "operation": "STORE",
  "size": 8,
  "operands": [
    {
      "operation": "SUB",
      "size": 8,
      "operands": [
        {"operation": "REG", "operands": [{"type": "register", "name": "rbp"}]},
        {"operation": "CONST", "operands": [{"type": "int", "value": 8}]}
      ]
    },
    {"operation": "REG", "operands": [{"type": "register", "name": "rdi"}]}
  ]
}
```

Leaf operands have a `type` field (`register`, `int`, `flag`, `register_ssa`, `flag_ssa`, `index`). Non-leaf operands have an `operation` field and nested `operands`.

## Typical Workflow

1. `load_executable` with the binary path → note the `view_id`
2. `list_functions` to discover functions of interest
3. `get_function_info` for details on a specific function
4. `list_types` to see available struct/enum definitions
5. `get_type_info` to inspect a struct's member layout (offsets, types)
6. `list_llil_instructions` to see the IL for a function
7. `get_llil_expr_tree` to inspect a specific instruction's operand tree
8. `get_xrefs` to trace callers/data references
9. `close_binary_view` when done

## Gotchas

- Binary Ninja must be running before the MCP server is available
- `load_executable` runs full analysis — it blocks and may take a while for large binaries
- `view_id` is an opaque handle managed by bn-mcp — do NOT pass it to `BinaryNinja::Load()` or treat it as a file path
- External plugins must call `bnmcp::client::GetView(view_id)` to resolve a view ID to a `BinaryView` pointer
- `address` parameters must be JSON integers, not strings — `{"address": 4198400}` not `{"address": "0x401000"}`
- `instr_index` is the LLIL instruction index (0-based), not the expression index
- The LLIL operation name table must match `BNLowLevelILOperation` enum order in `binaryninjacore.h` exactly — if BN updates and adds new ops, the table needs updating
- If builds fail with LNK2019 after a BN update, delete `build/` and reconfigure

## Development

See the [development reference](./references/development.md) for:
- Project architecture and file layout
- How to add new built-in tools
- How to register external tools from other plugins
- How to resolve view IDs to BinaryView pointers (`McpGetView` / `bnmcp::client::GetView`)
- Testing setup and conventions
- Code templates and patterns
