// Example bn-mcp plugin: hello_world
//
// Registers a simple "hello_world" tool that echoes a greeting.
// Demonstrates the minimal code needed to register a tool with bn-mcp.

#include <binaryninjaapi.h>
#include <binaryninjacore.h>
#include <bnmcp/api.h>

#include <format>
#include <string>

namespace binja = BinaryNinja;

extern "C" {
BN_DECLARE_CORE_ABI_VERSION

BINARYNINJAPLUGIN bool CorePluginInit() {
  bnmcp::client::RegisterTool(
      "hello_world", "Returns a greeting for the given name.",
      {{"type", "object"},
       {"properties",
        {{"name", {{"type", "string"}, {"description", "Name to greet"}}}}},
       {"required", {"name"}}},
      [](const nlohmann::json& args) -> nlohmann::json {
        auto name = args.at("name").get<std::string>();
        auto greeting = std::format("Hello, {}!", name);
        return {{"content", {{{"type", "text"}, {"text", greeting}}}}};
      });

  binja::LogInfo("hello_world: registered with bn-mcp");
  return true;
}
}
