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

 private:
  void RegisterTools();

  nlohmann::json LoadExecutable(const nlohmann::json& args);
  nlohmann::json CloseBinaryView(const nlohmann::json& args);
  nlohmann::json ListBinaryViews(const nlohmann::json& args);

  McpServer server_;

  std::mutex views_mutex_;
  std::unordered_map<std::string, BinaryNinja::Ref<BinaryNinja::BinaryView>>
      views_;
  uint64_t next_id_ = 1;
};

}  // namespace bnmcp
