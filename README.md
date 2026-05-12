# bn-mcp

A [Model Context Protocol](https://modelcontextprotocol.io/) (MCP) server for
[Binary Ninja](https://binary.ninja/), exposed as a core plugin. It allows AI
agents to interact with Binary Ninja programmatically over HTTP.

The server starts automatically when Binary Ninja launches and listens on
`http://127.0.0.1:3142/mcp` using the Streamable HTTP transport (JSON-RPC 2.0).

## Built-in Tools

### Binary Management

| Tool | Description |
|---|---|
| `load_executable` | Load an executable into Binary Ninja and run analysis. Returns a view ID for subsequent operations. |
| `close_binary_view` | Close a previously loaded binary view and free its resources. |
| `list_binary_views` | List all open views with their IDs, file paths, architectures, and platforms. |

### Function & Data Discovery

| Tool | Description |
|---|---|
| `list_functions` | List all functions: address, size, name. |
| `get_function_info` | Get function details: name, calling convention, return type, parameters, basic block count. |
| `get_strings` | List strings found in a binary view, optionally filtered by address range. |
| `get_xrefs` | Get cross-references to an address: code refs (callers) and data refs. |

### Types

| Tool | Description |
|---|---|
| `list_types` | List all defined types: name, kind (struct/enum/union/typedef), size. |
| `get_type_info` | Get full type definition with member offsets, sizes, and types. |

### Low Level IL

| Tool | Description |
|---|---|
| `list_llil_instructions` | List non-SSA LLIL instructions: index, address, operation, text. |
| `list_llil_ssa_instructions` | List SSA LLIL instructions with versioned registers. |
| `get_llil_expr_tree` | Get recursive expression tree for a non-SSA LLIL instruction. |
| `get_llil_ssa_expr_tree` | Get recursive expression tree for an SSA LLIL instruction. |

## Building

Requires CMake 4.2+, a C++23 compiler, and a Binary Ninja installation.

```bash
cmake --preset default
cmake --build --preset relwithdebinfo
```

## Installing

```bash
cmake --install build --config RelWithDebInfo
```

This copies `bnmcp-plugin.dll` (and the example plugin) to your Binary Ninja
plugins directory (`%APPDATA%\Binary Ninja\plugins`).

## MCP Client Configuration

Copy or reference the included [`mcp.json`](mcp.json) in your editor. For
example, in VS Code's `.vscode/mcp.json`:

```json
{
  "servers": {
    "bn-mcp": {
      "url": "http://127.0.0.1:3142/mcp"
    }
  }
}
```

Start Binary Ninja before connecting your MCP client — the server is only
available while Binary Ninja is running.

## Plugin API — Registering Custom Tools

Other Binary Ninja plugins can register their own MCP tools with bn-mcp at
runtime. Consumer plugins link against the `bnmcp::client` CMake target, which
provides the import lib and include paths.

### API

The API is defined in [`include/bnmcp/api.h`](include/bnmcp/api.h) and
exported from `bnmcp-plugin.dll`. It provides both a C interface and a C++
convenience wrapper:

```c
// C API
typedef const char* (*McpToolHandler)(const char* args_json, void* userdata);

bool McpRegisterTool(const char* name, const char* description,
                     const char* input_schema_json,
                     McpToolHandler handler, void* userdata);

// Resolve a view ID to a BinaryView (caller must free with BNFreeBinaryView).
BNBinaryView* McpGetView(const char* view_id);
```

```cpp
// C++ wrapper
#include <bnmcp/api.h>

extern "C" {
BN_DECLARE_CORE_ABI_VERSION

BINARYNINJAPLUGIN bool CorePluginInit() {
  bnmcp::client::RegisterTool(
      "my_tool",
      "Description of what my tool does",
      {{"type", "object"},
       {"properties",
        {{"view_id", {{"type", "string"}}},
         {"name", {{"type", "string"}}}}},
       {"required", {"view_id", "name"}}},
      [](const nlohmann::json& args) -> nlohmann::json {
        auto bv = bnmcp::client::GetView(args["view_id"]);
        if (!bv) { /* error */ }
        // ... use bv ...
        return {{"content", {{{"type", "text"}, {"text", "result"}}}}};
      });

  return true;
}
}
```

### CMake Integration

Other plugins consume the API via FetchContent. The `bnmcp::client` target
provides include paths, `nlohmann_json`, and the import lib for
`bnmcp-plugin.dll`.

#### Via FetchContent

```cmake
include(FetchContent)

FetchContent_Declare(bn-mcp
  GIT_REPOSITORY https://github.com/user/bn-mcp.git
  GIT_TAG main
)
FetchContent_MakeAvailable(bn-mcp)

add_library(my-plugin SHARED src/plugin.cpp)
target_link_libraries(my-plugin PRIVATE binaryninjaapi bnmcp::client)
```

#### Via find_package

If bn-mcp has been installed or its build tree is on `CMAKE_PREFIX_PATH`:

```cmake
find_package(bnmcp REQUIRED)

add_library(my-plugin SHARED src/plugin.cpp)
target_link_libraries(my-plugin PRIVATE binaryninjaapi bnmcp::client)
```

### Example Plugin

See [`examples/list_functions/plugin.cpp`](examples/list_functions/plugin.cpp)
for a complete example that registers a `hello_world` tool.

## Architecture

```
bnmcp-plugin.dll
  ├── Owns the HTTP server, JSON-RPC dispatch, and view management
  ├── Exports C API: McpRegisterTool(), McpGetView()
  └── Built-in tools: load/close/list views, functions, types, strings, xrefs, LLIL

include/bnmcp/api.h
  ├── C API declarations (dllimport for consumers, dllexport for the plugin)
  └── C++ wrapper: bnmcp::client::RegisterTool(), bnmcp::client::GetView()

other-plugin.dll
  ├── #include <bnmcp/api.h>
  ├── Links against: bnmcp-plugin import lib (via bnmcp::client CMake target)
  └── Calls bnmcp::client::RegisterTool() during CorePluginInit
```

All strings cross the DLL boundary as `const char*` — each side copies what it
needs. No shared allocators, no ABI coupling beyond the C function signatures.

## Testing

Tests use the [bn-test](https://github.com/ek0/bn-test) framework (GTest
compiled into a plugin DLL). Install `bnmcp-tests.dll` to the BN plugins
folder and run via Plugin Commands → `[bn-mcp] Run Tests`.

## License

TBD
