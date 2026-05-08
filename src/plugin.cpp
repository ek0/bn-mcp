#include <binaryninjaapi.h>
#include <binaryninjacore.h>
#include <bnmcp/api.h>

#include <memory>

#include "bn_mcp.h"

namespace binja = BinaryNinja;

namespace {
std::unique_ptr<bnmcp::BnMcp> g_mcp;
}

extern "C" {
BN_DECLARE_CORE_ABI_VERSION

BINARYNINJAPLUGIN bool CorePluginInit() {
  g_mcp = std::make_unique<bnmcp::BnMcp>();
  if (!g_mcp->Start()) {
    binja::LogError("bn-mcp: Failed to start MCP server (port may be in use)");
    g_mcp.reset();
    return true;  // Don't fail the plugin entirely.
  }
  return true;
}

BNMCP_API bool McpRegisterTool(const char* name, const char* description,
                               const char* input_schema_json,
                               McpToolHandler handler, void* userdata) {
  if (!g_mcp) return false;
  return g_mcp->RegisterExternalTool(name, description, input_schema_json,
                                     handler, userdata);
}

BNMCP_API BNBinaryView* McpGetView(const char* view_id) {
  if (!g_mcp) return nullptr;
  return g_mcp->GetView(view_id);
}
}
