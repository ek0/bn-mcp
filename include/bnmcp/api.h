#pragma once

// bnmcp API — cross-plugin MCP tool registration for Binary Ninja.
//
// This header provides both the C interface exported by bnmcp-plugin.dll and
// a C++ convenience wrapper.  Link against bnmcp::client (provides the import
// lib and include paths).
//
// Usage:
//   #include <bnmcp/api.h>
//
//   BINARYNINJAPLUGIN bool CorePluginInit() {
//     bnmcp::client::RegisterTool("my_tool", "Does a thing",
//         {{"type", "object"}, ...},
//         [](const nlohmann::json& args) -> nlohmann::json {
//           // ...
//         });
//     return true;
//   }

#ifdef BNMCP_EXPORTS
#define BNMCP_API __declspec(dllexport)
#else
#define BNMCP_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Tool handler callback.
// Receives the arguments as a JSON string and a user-provided context pointer.
// Must return a JSON string containing the MCP tool result (with "content"
// array), or NULL on error.  The returned pointer must remain valid until the
// handler returns — the server copies it immediately.
typedef const char* (*McpToolHandler)(const char* args_json, void* userdata);

// Register a tool with the MCP server.
// name:              Tool name (e.g. "decompile_function").
// description:       Human-readable description of the tool.
// input_schema_json: JSON string describing the tool's input schema.
// handler:           Callback invoked when the tool is called.
// userdata:          Opaque pointer passed back to handler on every call.
// Returns true on success, false if the server is not available.
BNMCP_API bool McpRegisterTool(const char* name, const char* description,
                               const char* input_schema_json,
                               McpToolHandler handler, void* userdata);

// Look up a managed BinaryView by its view ID (returned by load_executable).
// Returns a new reference to the BNBinaryView, or NULL if not found.
// The caller owns the returned reference and must free it with
// BNFreeBinaryView when done.
BNMCP_API BNBinaryView* McpGetView(const char* view_id);

#ifdef __cplusplus
}
#endif

// --- C++ convenience wrapper ---

#ifdef __cplusplus

#include <binaryninjaapi.h>

#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace bnmcp::client {

using HandlerFn = std::function<nlohmann::json(const nlohmann::json&)>;

// Resolve a view ID to a BinaryView. Returns nullptr if not found.
inline BinaryNinja::Ref<BinaryNinja::BinaryView> GetView(
    std::string_view view_id) {
  auto* core_view = McpGetView(std::string(view_id).c_str());
  if (!core_view) return nullptr;
  return new BinaryNinja::BinaryView(core_view);
}

// Register a tool with the MCP server.
inline bool RegisterTool(std::string_view name, std::string_view description,
                         const nlohmann::json& input_schema,
                         HandlerFn handler) {
  auto* fn = new HandlerFn(std::move(handler));

  return McpRegisterTool(
      std::string(name).c_str(), std::string(description).c_str(),
      input_schema.dump().c_str(),
      [](const char* args_json, void* ud) -> const char* {
        thread_local std::string buf;
        try {
          auto* h = static_cast<HandlerFn*>(ud);
          buf = (*h)(nlohmann::json::parse(args_json)).dump();
          return buf.c_str();
        } catch (const std::exception& e) {
          buf = nlohmann::json{{"content",
                                {{{"type", "text"}, {"text", e.what()}}}},
                               {"isError", true}}
                    .dump();
          return buf.c_str();
        }
      },
      fn);
}

}  // namespace bnmcp::client

#endif  // __cplusplus
