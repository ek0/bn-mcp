#include "mcp_server.h"

#include <httplib.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace bnmcp {

struct McpServer::Impl {
  httplib::Server server;
  std::thread thread;
  uint16_t bound_port = 0;
};

McpServer::McpServer(Options options)
    : options_(std::move(options)), impl_(std::make_unique<Impl>()) {}

McpServer::~McpServer() { Stop(); }

void McpServer::RegisterTool(Tool tool) {
  std::lock_guard lock(tools_mutex_);
  tools_.push_back(std::move(tool));
}

bool McpServer::Start() {
  impl_->server.Post(
      "/mcp", [this](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json request;
        try {
          request = nlohmann::json::parse(req.body);
        } catch (const nlohmann::json::parse_error&) {
          auto error = MakeError(nullptr, -32700, "Parse error");
          res.set_content(error.dump(), "application/json");
          return;
        }

        // JSON-RPC notifications have no "id" field — acknowledge and return.
        if (!request.contains("id")) {
          res.status = 202;
          return;
        }

        auto response = Dispatch(request);
        res.set_content(response.dump(), "application/json");
      });

  if (!impl_->server.bind_to_port(options_.host, options_.port)) {
    return false;
  }
  impl_->bound_port = options_.port;

  impl_->thread = std::thread([this]() { impl_->server.listen_after_bind(); });

  return true;
}

void McpServer::Stop() {
  if (impl_->server.is_running()) {
    impl_->server.stop();
  }
  if (impl_->thread.joinable()) {
    impl_->thread.join();
  }
}

uint16_t McpServer::GetPort() const { return impl_->bound_port; }

nlohmann::json McpServer::Dispatch(const nlohmann::json& request) {
  if (!request.contains("jsonrpc") || request["jsonrpc"] != "2.0") {
    return MakeError(request.value("id", nlohmann::json{}), -32600,
                     "Invalid Request: missing or invalid jsonrpc version");
  }

  if (!request.contains("method") || !request["method"].is_string()) {
    return MakeError(request.value("id", nlohmann::json{}), -32600,
                     "Invalid Request: missing method");
  }

  const auto& id = request["id"];
  auto method = request["method"].get<std::string>();
  auto params = request.value("params", nlohmann::json::object());

  if (method == "initialize") return HandleInitialize(id);
  if (method == "ping") return MakeResponse(id, nlohmann::json::object());
  if (method == "tools/list") return HandleToolsList(id);
  if (method == "tools/call") return HandleToolsCall(id, params);

  return MakeError(id, -32601, "Method not found: " + method);
}

nlohmann::json McpServer::HandleInitialize(const nlohmann::json& id) {
  initialized_ = true;
  return MakeResponse(
      id, {{"protocolVersion", "2025-03-26"},
           {"capabilities", {{"tools", nlohmann::json::object()}}},
           {"serverInfo",
            {{"name", options_.name}, {"version", options_.version}}}});
}

nlohmann::json McpServer::HandleToolsList(const nlohmann::json& id) {
  std::lock_guard lock(tools_mutex_);

  auto tools_json = nlohmann::json::array();
  for (const auto& tool : tools_) {
    tools_json.push_back({{"name", tool.name},
                          {"description", tool.description},
                          {"inputSchema", tool.input_schema}});
  }
  return MakeResponse(id, {{"tools", tools_json}});
}

nlohmann::json McpServer::HandleToolsCall(const nlohmann::json& id,
                                          const nlohmann::json& params) {
  if (!params.contains("name") || !params["name"].is_string()) {
    return MakeError(id, -32602, "Invalid params: missing tool name");
  }

  auto tool_name = params["name"].get<std::string>();
  auto arguments = params.value("arguments", nlohmann::json::object());

  std::lock_guard lock(tools_mutex_);
  for (const auto& tool : tools_) {
    if (tool.name == tool_name) {
      try {
        return MakeResponse(id, tool.handler(arguments));
      } catch (const std::exception& e) {
        return MakeResponse(
            id, {{"content", {{{"type", "text"}, {"text", e.what()}}}},
                 {"isError", true}});
      }
    }
  }

  return MakeError(id, -32602, "Unknown tool: " + tool_name);
}

nlohmann::json McpServer::MakeResponse(const nlohmann::json& id,
                                       const nlohmann::json& result) {
  return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

nlohmann::json McpServer::MakeError(const nlohmann::json& id, int code,
                                    const std::string& message) {
  return {{"jsonrpc", "2.0"},
          {"id", id},
          {"error", {{"code", code}, {"message", message}}}};
}

}  // namespace bnmcp
