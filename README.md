# bn-mcp

A [Model Context Protocol](https://modelcontextprotocol.io/) (MCP) server for
[Binary Ninja](https://binary.ninja/), exposed as a core plugin. It allows AI
agents to interact with Binary Ninja programmatically over HTTP.

The server starts automatically when Binary Ninja launches and listens on
`http://127.0.0.1:3142/mcp` using the Streamable HTTP transport (JSON-RPC 2.0).

## Built-in Tools

| Tool | Description |
|---|---|
| `load_executable` | Load an executable into Binary Ninja and run analysis. Returns a view ID for subsequent operations. |
| `close_binary_view` | Close a previously loaded binary view and free its resources. |

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
        {{"name", {{"type", "string"}}}}},
       {"required", {"name"}}},
      [](const nlohmann::json& args) -> nlohmann::json {
        auto name = args.at("name").get<std::string>();
        return {{"content", {{{"type", "text"}, {"text", "Hello, " + name}}}}};
      });

  return true;
}
}
```

### CMake Integration

Other plugins consume the API via FetchContent. The `bnmcp::client` target
provides include paths, `nlohmann_json`, and the import lib for
`bnmcp-plugin.dll`.

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

### Example Plugin

See [`examples/list_functions/plugin.cpp`](examples/list_functions/plugin.cpp)
for a complete example that registers a `hello_world` tool.

## Architecture

```
bnmcp-plugin.dll
  ├── Owns the HTTP server, JSON-RPC dispatch, and view management
  ├── Exports C API: McpRegisterTool()
  └── Built-in tools: load_executable, close_binary_view, list_binary_views

include/bnmcp/api.h
  ├── C API declarations (dllimport for consumers, dllexport for the plugin)
  └── C++ wrapper: bnmcp::client::RegisterTool() using std::function

other-plugin.dll
  ├── #include <bnmcp/api.h>
  ├── Links against: bnmcp-plugin import lib (via bnmcp::client CMake target)
  └── Calls bnmcp::client::RegisterTool() during CorePluginInit
```

All strings cross the DLL boundary as `const char*` — each side copies what it
needs. No shared allocators, no ABI coupling beyond the C function signatures.

## License

TBD
