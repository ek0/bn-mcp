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

 private:
  void RegisterTools();

  nlohmann::json LoadExecutable(const nlohmann::json& args);
  nlohmann::json CloseBinaryView(const nlohmann::json& args);
  nlohmann::json ListBinaryViews(const nlohmann::json& args);
  nlohmann::json ListLlilInstructions(const nlohmann::json& args);
  nlohmann::json ListLlilSsaInstructions(const nlohmann::json& args);
  nlohmann::json GetLlilExprTree(const nlohmann::json& args);
  nlohmann::json GetLlilSsaExprTree(const nlohmann::json& args);

  // Helpers
  BinaryNinja::Ref<BinaryNinja::BinaryView> FindView(
      const std::string& view_id);
  nlohmann::json ListLlilImpl(const nlohmann::json& args, bool ssa);
  nlohmann::json GetLlilExprTreeImpl(const nlohmann::json& args, bool ssa);

  McpServer server_;

  std::mutex views_mutex_;
  std::unordered_map<std::string, BinaryNinja::Ref<BinaryNinja::BinaryView>>
      views_;
  uint64_t next_id_ = 1;
};

}  // namespace bnmcp
