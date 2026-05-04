#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace bnmcp {

// A tool that can be registered with the MCP server.
struct Tool {
  std::string name;
  std::string description;
  nlohmann::json input_schema;
  std::function<nlohmann::json(const nlohmann::json&)> handler;
};

class McpServer {
 public:
  struct Options {
    std::string host = "127.0.0.1";
    uint16_t port = 3142;
    std::string name = "bn-mcp";
    std::string version = "0.1.0";
  };

  explicit McpServer(Options options = {});
  ~McpServer();

  McpServer(const McpServer&) = delete;
  McpServer& operator=(const McpServer&) = delete;

  void RegisterTool(Tool tool);
  bool Start();
  void Stop();
  uint16_t GetPort() const;

 private:
  struct Impl;

  nlohmann::json Dispatch(const nlohmann::json& request);
  nlohmann::json HandleInitialize(const nlohmann::json& id);
  nlohmann::json HandleToolsList(const nlohmann::json& id);
  nlohmann::json HandleToolsCall(const nlohmann::json& id,
                                 const nlohmann::json& params);

  static nlohmann::json MakeResponse(const nlohmann::json& id,
                                     const nlohmann::json& result);
  static nlohmann::json MakeError(const nlohmann::json& id, int code,
                                  const std::string& message);

  Options options_;
  std::vector<Tool> tools_;
  std::mutex tools_mutex_;
  std::atomic<bool> initialized_{false};
  std::unique_ptr<Impl> impl_;
};

}  // namespace bnmcp
