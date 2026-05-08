# bn-mcp Development Reference

## Architecture

```
bnmcp-plugin.dll        — The plugin Binary Ninja loads. Owns the HTTP server,
                          JSON-RPC dispatch, view management, and built-in tools.
                          Exports McpRegisterTool() for external plugins.

include/bnmcp/api.h     — Public C API + C++ wrapper. Consumer plugins include
                          this and link against bnmcp::client (import lib).

src/mcp_server.h/.cpp   — Generic MCP/JSON-RPC server (httplib + nlohmann/json).
src/bn_mcp.h/.cpp       — Binary Ninja tools: load/close views, LLIL, functions,
                          strings, xrefs. Also handles external tool registration.
src/plugin.cpp          — CorePluginInit entry point, exports C API.

test/                   — GTest tests via bn-test framework (plugin DLL).
examples/               — Example consumer plugin (hello_world).
```

## Build & Install

```bash
cmake --preset default
cmake --build --preset relwithdebinfo
cmake --install build --config RelWithDebInfo
# Installs to %APPDATA%\Binary Ninja\plugins
# Also installs lib package to --prefix if specified
```

If Binary Ninja updates and the build fails with LNK2019 (unresolved externals), the API revision is stale. Delete `build/` and reconfigure — it re-reads `api_REVISION.txt` from the BN install.

## Adding a New Built-in Tool

1. Add the method declaration to `src/bn_mcp.h` (private section).
2. Register the tool in `BnMcp::RegisterTools()` in `src/bn_mcp.cpp` with name, description, input schema, and handler lambda.
3. Implement the handler method. Use `FindView(view_id)` to resolve a view ID.
4. Build and install.

### Tool result format

```cpp
// Success
return {{"content", {{{"type", "text"}, {"text", result_string}}}}};

// Error
return {{"content", {{{"type", "text"}, {"text", error_message}}}},
        {"isError", true}};
```

### Input schema format

```cpp
.input_schema = {{"type", "object"},
                 {"properties",
                  {{"view_id", {{"type", "string"},
                                {"description", "The view ID"}}},
                   {"address", {{"type", "integer"},
                                {"description", "Function address"}}}}},
                 {"required", {"view_id", "address"}}}
```

## Adding an External Tool (from another plugin)

Consumer plugins include `<bnmcp/api.h>` and link `bnmcp::client`:

```cmake
target_link_libraries(my-plugin PRIVATE binaryninjaapi bnmcp::client)
```

```cpp
#include <bnmcp/api.h>

bnmcp::client::RegisterTool("my_tool", "Description",
    {{"type", "object"}, ...},
    [](const nlohmann::json& args) -> nlohmann::json {
      auto bv = bnmcp::client::GetView(args["view_id"]);
      if (!bv) { /* error */ }
      // ... use bv ...
      return {{"content", {{{"type", "text"}, {"text", "result"}}}}};
    });
```

Use `bnmcp::client::GetView(view_id)` to resolve a view ID (received from the agent) to a `Ref<BinaryView>`. Do NOT pass the view ID to `BinaryNinja::Load()` — it is an opaque handle, not a file path.

The C API equivalent is `McpGetView(view_id)` which returns a `BNBinaryView*` (new reference, caller must free with `BNFreeBinaryView`).

## Testing

Tests use the [bn-test](https://github.com/ek0/bn-test) framework — GTest compiled into a plugin DLL that runs inside Binary Ninja.

- Test files: `test/llil_tools_test.cpp`, `test/test_plugin.cpp`
- Build target: `bnmcp-tests`
- Run: Open BN → Plugin commands → `[bn-mcp] Run Tests`
- Tests construct `LowLevelILFunction` programmatically with `x86_64` architecture and verify `ExprToJson` output

To add tests: add a `.cpp` file under `test/`, list it in `CMakeLists.txt` under `bnmcp-tests`, use `TEST()` or `TEST_F()` with the `LlilExprTest` fixture.

## Conventions

- C++23, Google style (`.clang-format` in root)
- Binary Ninja types use `binja::` namespace alias
- `Ref<T>` for all BN objects (reference-counted)
- LLIL operation name table in `LlilOpName()` must match `BNLowLevelILOperation` enum order in `binaryninjacore.h` exactly
- All view lookups go through `FindView()` which locks `views_mutex_`
- MCP server listens on `127.0.0.1:3142/mcp`
- Format with `clang-format -i` before committing

## Known Pitfalls

- `GetSSAForm()` is on `LowLevelILFunction`, not on `Function` — get non-SSA first, then call `GetSSAForm()` on it
- `Confidence<T>` doesn't convert to `bool` — use `.GetValue()` to unwrap
- `ExprId` is `size_t` inside the BN namespace — use `size_t` in test code outside the namespace
- `std::format` with `size_t` on MSVC requires a cast to `int` or `uint64_t` to avoid template errors
- `BNStringReference.length` is `size_t` — truncate string content to avoid huge output
- `GetVariableType()` returns `Confidence<Ref<Type>>` — check `.GetValue()` before dereferencing

## Key Dependencies

| Dependency | Purpose | Fetch |
|---|---|---|
| binaryninja-api | BN plugin SDK | FetchContent (revision from installed BN) |
| nlohmann/json | JSON-RPC serialization | FetchContent v3.11.3 |
| cpp-httplib | HTTP server | FetchContent v0.18.3 |
| bn-test + GTest | Unit testing | FetchContent |
