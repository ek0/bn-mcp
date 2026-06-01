#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include "binaryninjaapi.h"
#include "mcp_server.h"

// C API callback type (must match bnmcp/api.h).
typedef const char* (*McpToolHandler)(const char* args_json, void* userdata);

namespace bnmcp {

class BnMcp {
 public:
  BnMcp();
  ~BnMcp();

  BnMcp(const BnMcp&) = delete;
  BnMcp& operator=(const BnMcp&) = delete;

  bool Start();
  void Stop();

  // Register a tool from an external plugin via the C API.
  bool RegisterExternalTool(const char* name, const char* description,
                            const char* input_schema_json,
                            McpToolHandler handler, void* userdata);

  static nlohmann::json ExprToJson(
      const BinaryNinja::LowLevelILInstruction& expr);

  // Look up a managed BinaryView by ID. Returns a new reference (caller
  // must release). Returns nullptr if not found.
  BNBinaryView* GetView(const char* view_id);

 private:
  void RegisterTools();

  nlohmann::json LoadExecutable(const nlohmann::json& args);
  nlohmann::json CloseBinaryView(const nlohmann::json& args);
  nlohmann::json ListBinaryViews(const nlohmann::json& args);
  nlohmann::json ListLlilInstructions(const nlohmann::json& args);
  nlohmann::json ListLlilSsaInstructions(const nlohmann::json& args);
  nlohmann::json GetLlilExprTree(const nlohmann::json& args);
  nlohmann::json GetLlilSsaExprTree(const nlohmann::json& args);
  nlohmann::json ListFunctions(const nlohmann::json& args);
  nlohmann::json GetFunctionInfo(const nlohmann::json& args);
  nlohmann::json GetStrings(const nlohmann::json& args);
  nlohmann::json GetXrefs(const nlohmann::json& args);
  nlohmann::json ListTypes(const nlohmann::json& args);
  nlohmann::json GetTypeInfo(const nlohmann::json& args);
  nlohmann::json GetDisassembly(const nlohmann::json& args);
  nlohmann::json ListImports(const nlohmann::json& args);
  nlohmann::json ListExports(const nlohmann::json& args);
  nlohmann::json ListSegments(const nlohmann::json& args);
  nlohmann::json ListSections(const nlohmann::json& args);

  // Helpers
  BinaryNinja::Ref<BinaryNinja::BinaryView> FindView(
      const std::string& view_id);
  // Find functions starting at the given address.
  std::vector<BinaryNinja::Ref<BinaryNinja::Function>> FindFunctionsAt(
      BinaryNinja::BinaryView* bv, uint64_t address);
  // Build an error response when no function starts at the given address.
  // If the address is inside a known function, the message hints at its start.
  nlohmann::json NoFunctionError(BinaryNinja::BinaryView* bv,
                                 uint64_t address);
  nlohmann::json ListLlilImpl(const nlohmann::json& args, bool ssa);
  nlohmann::json GetLlilExprTreeImpl(const nlohmann::json& args, bool ssa);

  McpServer server_;

  std::mutex views_mutex_;
  std::unordered_map<std::string, BinaryNinja::Ref<BinaryNinja::BinaryView>>
      views_;
  uint64_t next_id_ = 1;
};

}  // namespace bnmcp
