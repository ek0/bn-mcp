#include "bn_mcp.h"

#include <binaryninjaapi.h>

#include <format>
#include <mutex>
#include <string>
#include <utility>

namespace binja = BinaryNinja;

namespace bnmcp {

BnMcp::BnMcp() { RegisterTools(); }

BnMcp::~BnMcp() { Stop(); }

bool BnMcp::Start() {
  if (!server_.Start()) {
    return false;
  }
  binja::LogInfoF("bn-mcp: MCP server listening on http://127.0.0.1:{}/mcp",
                  server_.GetPort());
  return true;
}

void BnMcp::Stop() {
  server_.Stop();

  // Release all managed views.
  std::lock_guard lock(views_mutex_);
  for (auto& [id, bv] : views_) {
    auto* file = bv->GetFile();
    bv = nullptr;
    file->Close();
  }
  views_.clear();
}

bool BnMcp::RegisterExternalTool(const char* name, const char* description,
                                 const char* input_schema_json,
                                 McpToolHandler handler, void* userdata) {
  auto schema = nlohmann::json::parse(input_schema_json, nullptr, false);
  if (schema.is_discarded()) {
    binja::LogErrorF("bn-mcp: Invalid input schema JSON for tool '{}'", name);
    return false;
  }

  server_.RegisterTool(
      {.name = std::string(name),
       .description = description,
       .input_schema = std::move(schema),
       .handler = [handler, userdata](const nlohmann::json& args) {
         auto args_str = args.dump();
         const char* result = handler(args_str.c_str(), userdata);
         if (!result) {
           return nlohmann::json{
               {"content",
                {{{"type", "text"}, {"text", "Tool returned null"}}}},
               {"isError", true}};
         }
         auto parsed = nlohmann::json::parse(result, nullptr, false);
         if (parsed.is_discarded()) {
           return nlohmann::json{
               {"content",
                {{{"type", "text"}, {"text", "Tool returned invalid JSON"}}}},
               {"isError", true}};
         }
         return parsed;
       }});

  binja::LogInfoF("bn-mcp: Registered external tool '{}'", name);
  return true;
}

void BnMcp::RegisterTools() {
  server_.RegisterTool(
      {.name = "load_executable",
       .description =
           "Load an executable file into Binary Ninja for analysis. Returns a "
           "view ID that can be used in subsequent operations. Analysis runs "
           "to completion before returning, which may take a while for large "
           "binaries.",
       .input_schema =
           {{"type", "object"},
            {"properties",
             {{"path",
               {{"type", "string"},
                {"description",
                 "Absolute path to the executable file to load"}}}}},
            {"required", {"path"}}},
       .handler = [this](const nlohmann::json& args) {
         return LoadExecutable(args);
       }});

  server_.RegisterTool(
      {.name = "close_binary_view",
       .description =
           "Close a previously loaded binary view and free its resources.",
       .input_schema = {{"type", "object"},
                        {"properties",
                         {{"view_id",
                           {{"type", "string"},
                            {"description",
                             "The view ID returned by load_executable"}}}}},
                        {"required", {"view_id"}}},
       .handler = [this](const nlohmann::json& args) {
         return CloseBinaryView(args);
       }});

  server_.RegisterTool(
      {.name = "list_binary_views",
       .description = "List all currently open binary views with their IDs, "
                      "file paths, architectures, and platforms.",
       .input_schema = {{"type", "object"},
                        {"properties", nlohmann::json::object()}},
       .handler = [this](const nlohmann::json& args) {
         return ListBinaryViews(args);
       }});
}

nlohmann::json BnMcp::LoadExecutable(const nlohmann::json& args) {
  if (!args.contains("path") || !args["path"].is_string()) {
    return {
        {"content",
         {{{"type", "text"}, {"text", "Missing required parameter: path"}}}},
        {"isError", true}};
  }

  auto path = args["path"].get<std::string>();

  binja::LogInfoF("bn-mcp: Loading executable: {}", path);

  auto bv = binja::Load(path);
  if (!bv) {
    return {{"content",
             {{{"type", "text"},
               {"text", std::format("Failed to load executable: {}", path)}}}},
            {"isError", true}};
  }

  std::string view_id;
  {
    std::lock_guard lock(views_mutex_);
    view_id = std::to_string(next_id_++);
    views_[view_id] = bv;
  }

  auto arch_name = bv->GetDefaultArchitecture()
                       ? bv->GetDefaultArchitecture()->GetName()
                       : "unknown";
  auto plat_name = bv->GetDefaultPlatform()
                       ? bv->GetDefaultPlatform()->GetName()
                       : "unknown";

  auto result_text = std::format(
      "Loaded executable: {}\n"
      "View ID: {}\n"
      "Architecture: {}\n"
      "Platform: {}\n"
      "Entry point: 0x{:x}\n"
      "Functions: {}",
      path, view_id, arch_name, plat_name, bv->GetEntryPoint(),
      bv->GetAnalysisFunctionList().size());

  binja::LogInfoF("bn-mcp: {}", result_text);

  return {{"content", {{{"type", "text"}, {"text", result_text}}}}};
}

nlohmann::json BnMcp::ListBinaryViews(const nlohmann::json& /*args*/) {
  std::lock_guard lock(views_mutex_);

  if (views_.empty()) {
    return {{"content",
             {{{"type", "text"},
               {"text", "No binary views are currently open."}}}}};
  }

  std::string result = std::format("Open binary views ({}):\n", views_.size());
  for (const auto& [id, bv] : views_) {
    auto arch = bv->GetDefaultArchitecture()
                    ? bv->GetDefaultArchitecture()->GetName()
                    : "unknown";
    auto plat = bv->GetDefaultPlatform() ? bv->GetDefaultPlatform()->GetName()
                                         : "unknown";
    auto path = bv->GetFile()->GetOriginalFilename();
    result += std::format("  ID: {}  path: {}  arch: {}  platform: {}\n", id,
                          path, arch, plat);
  }

  return {{"content", {{{"type", "text"}, {"text", result}}}}};
}

nlohmann::json BnMcp::CloseBinaryView(const nlohmann::json& args) {
  if (!args.contains("view_id") || !args["view_id"].is_string()) {
    return {
        {"content",
         {{{"type", "text"}, {"text", "Missing required parameter: view_id"}}}},
        {"isError", true}};
  }

  auto view_id = args["view_id"].get<std::string>();

  std::lock_guard lock(views_mutex_);
  auto it = views_.find(view_id);
  if (it == views_.end()) {
    return {{"content",
             {{{"type", "text"},
               {"text",
                std::format("No binary view found with ID: {}", view_id)}}}},
            {"isError", true}};
  }

  // Pull the view out of the map and close it properly.
  auto bv = std::move(it->second);
  views_.erase(it);

  auto* file = bv->GetFile();
  bv = nullptr;
  file->Close();

  binja::LogInfoF("bn-mcp: Closed binary view {}", view_id);

  return {{"content",
           {{{"type", "text"},
             {"text", std::format("Closed binary view: {}", view_id)}}}}};
}

}  // namespace bnmcp
