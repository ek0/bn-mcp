#include "bn_mcp.h"

#include <binaryninjaapi.h>
#include <binaryninjacore.h>
#include <lowlevelilinstruction.h>

#include <algorithm>
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

  server_.RegisterTool(
      {.name = "list_llil_instructions",
       .description =
           "List all Low Level IL instructions for a function. Returns each "
           "instruction's index, address, operation name, and text "
           "representation. Useful for understanding the IL produced by the "
           "compiler and debugging analysis issues.",
       .input_schema =
           {{"type", "object"},
            {"properties",
             {{"view_id",
               {{"type", "string"},
                {"description", "The view ID returned by load_executable"}}},
              {"address",
               {{"type", "integer"},
                {"description", "Start address of the function to inspect"}}}}},
            {"required", {"view_id", "address"}}},
       .handler = [this](const nlohmann::json& args) {
         return ListLlilInstructions(args);
       }});

  server_.RegisterTool(
      {.name = "list_llil_ssa_instructions",
       .description =
           "List all Low Level IL instructions in SSA form for a function. "
           "SSA form assigns unique versions to each register definition, "
           "making data flow explicit. Returns each instruction's index, "
           "address, operation name, and text representation.",
       .input_schema =
           {{"type", "object"},
            {"properties",
             {{"view_id",
               {{"type", "string"},
                {"description", "The view ID returned by load_executable"}}},
              {"address",
               {{"type", "integer"},
                {"description", "Start address of the function to inspect"}}}}},
            {"required", {"view_id", "address"}}},
       .handler = [this](const nlohmann::json& args) {
         return ListLlilSsaInstructions(args);
       }});

  server_.RegisterTool(
      {.name = "get_llil_expr_tree",
       .description =
           "Get the full expression tree for a specific LLIL instruction, "
           "recursively expanding all sub-expressions with their operations "
           "and operands. Useful for understanding complex instruction "
           "patterns like nested loads or register assignments.",
       .input_schema =
           {{"type", "object"},
            {"properties",
             {{"view_id",
               {{"type", "string"},
                {"description", "The view ID returned by load_executable"}}},
              {"address",
               {{"type", "integer"},
                {"description",
                 "Start address of the function containing the "
                 "instruction"}}},
              {"instr_index",
               {{"type", "integer"},
                {"description",
                 "Instruction index (from list_llil_instructions)"}}}}},
            {"required", {"view_id", "address", "instr_index"}}},
       .handler = [this](const nlohmann::json& args) {
         return GetLlilExprTree(args);
       }});

  server_.RegisterTool(
      {.name = "get_llil_ssa_expr_tree",
       .description =
           "Get the full expression tree for a specific LLIL SSA instruction, "
           "recursively expanding all sub-expressions. SSA form includes "
           "register versioning, making data flow and def-use chains "
           "explicit.",
       .input_schema =
           {{"type", "object"},
            {"properties",
             {{"view_id",
               {{"type", "string"},
                {"description", "The view ID returned by load_executable"}}},
              {"address",
               {{"type", "integer"},
                {"description",
                 "Start address of the function containing the "
                 "instruction"}}},
              {"instr_index",
               {{"type", "integer"},
                {"description",
                 "Instruction index (from list_llil_ssa_instructions)"}}}}},
            {"required", {"view_id", "address", "instr_index"}}},
       .handler = [this](const nlohmann::json& args) {
         return GetLlilSsaExprTree(args);
       }});

  server_.RegisterTool(
      {.name = "list_functions",
       .description =
           "List all functions in a binary view. Returns each function's "
           "address, name, and size. Useful for discovering entry points and "
           "navigating the binary.",
       .input_schema = {{"type", "object"},
                        {"properties",
                         {{"view_id",
                           {{"type", "string"},
                            {"description",
                             "The view ID returned by load_executable"}}}}},
                        {"required", {"view_id"}}},
       .handler = [this](const nlohmann::json& args) {
         return ListFunctions(args);
       }});

  server_.RegisterTool(
      {.name = "get_function_info",
       .description =
           "Get detailed information about a function: name, calling "
           "convention, parameters, return type, and basic block count. "
           "The address must be the exact start address of the function "
           "as returned by list_functions.",
       .input_schema = {{"type", "object"},
                        {"properties",
                         {{"view_id",
                           {{"type", "string"},
                            {"description",
                             "The view ID returned by load_executable"}}},
                          {"address",
                           {{"type", "integer"},
                            {"description",
                             "Start address of the function (from "
                             "list_functions)"}}}}},
                        {"required", {"view_id", "address"}}},
       .handler = [this](const nlohmann::json& args) {
         return GetFunctionInfo(args);
       }});

  server_.RegisterTool(
      {.name = "get_strings",
       .description =
           "List strings found in a binary view. Optionally filter to a "
           "specific address range. Returns each string's address, length, "
           "and content.",
       .input_schema =
           {{"type", "object"},
            {"properties",
             {{"view_id",
               {{"type", "string"},
                {"description", "The view ID returned by load_executable"}}},
              {"start",
               {{"type", "integer"},
                {"description", "Start address for filtering (optional)"}}},
              {"length",
               {{"type", "integer"},
                {"description",
                 "Length of address range for filtering (optional)"}}}}},
            {"required", {"view_id"}}},
       .handler = [this](const nlohmann::json& args) {
         return GetStrings(args);
       }});

  server_.RegisterTool(
      {.name = "get_xrefs",
       .description =
           "Get cross-references to a given address. Returns code references "
           "(callers) and data references pointing to the address. Useful for "
           "understanding call graphs and data usage.",
       .input_schema =
           {{"type", "object"},
            {"properties",
             {{"view_id",
               {{"type", "string"},
                {"description", "The view ID returned by load_executable"}}},
              {"address",
               {{"type", "integer"},
                {"description", "Target address to find references to"}}}}},
            {"required", {"view_id", "address"}}},
       .handler = [this](const nlohmann::json& args) {
         return GetXrefs(args);
       }});

  server_.RegisterTool(
      {.name = "list_types",
       .description =
           "List all types defined in a binary view. Returns each type's "
           "name, kind (struct, enum, union, typedef, etc.), and size in "
           "bytes.",
       .input_schema = {{"type", "object"},
                        {"properties",
                         {{"view_id",
                           {{"type", "string"},
                            {"description",
                             "The view ID returned by load_executable"}}}}},
                        {"required", {"view_id"}}},
       .handler = [this](const nlohmann::json& args) {
         return ListTypes(args);
       }});

  server_.RegisterTool(
      {.name = "get_type_info",
       .description =
           "Get detailed information about a named type. For structs/unions: "
           "lists each member with name, type, offset, and size. For enums: "
           "lists each value with name and integer constant. For typedefs: "
           "shows the underlying type.",
       .input_schema = {{"type", "object"},
                        {"properties",
                         {{"view_id",
                           {{"type", "string"},
                            {"description",
                             "The view ID returned by load_executable"}}},
                          {"type_name",
                           {{"type", "string"},
                            {"description", "Name of the type to look up"}}}}},
                        {"required", {"view_id", "type_name"}}},
       .handler = [this](const nlohmann::json& args) {
         return GetTypeInfo(args);
       }});

  server_.RegisterTool(
      {.name = "get_disassembly",
       .description =
           "Get the disassembly text for a function. Returns assembly "
           "instructions grouped by basic block with address prefixes. "
           "The address must be the exact start address of the function "
           "as returned by list_functions.",
       .input_schema =
           {{"type", "object"},
            {"properties",
             {{"view_id",
               {{"type", "string"},
                {"description", "The view ID returned by load_executable"}}},
              {"address",
               {{"type", "integer"},
                {"description",
                 "Start address of the function (from list_functions)"}}},
              {"max_lines",
               {{"type", "integer"},
                {"description",
                 "Maximum number of lines to return (optional, "
                 "default unlimited)"}}}}},
            {"required", {"view_id", "address"}}},
       .handler = [this](const nlohmann::json& args) {
         return GetDisassembly(args);
       }});

  server_.RegisterTool(
      {.name = "list_imports",
       .description =
           "List imported and external symbols in a binary view. Includes "
           "imported functions, imported data, and external symbols for "
           "cross-platform coverage. Returns each symbol's address, kind, "
           "and name.",
       .input_schema = {{"type", "object"},
                        {"properties",
                         {{"view_id",
                           {{"type", "string"},
                            {"description",
                             "The view ID returned by load_executable"}}}}},
                        {"required", {"view_id"}}},
       .handler = [this](const nlohmann::json& args) {
         return ListImports(args);
       }});

  server_.RegisterTool(
      {.name = "list_exports",
       .description =
           "List exported/global symbols in a binary view. Returns globally "
           "bound function and data symbols visible to external consumers. "
           "Returns each symbol's address, kind, and name.",
       .input_schema = {{"type", "object"},
                        {"properties",
                         {{"view_id",
                           {{"type", "string"},
                            {"description",
                             "The view ID returned by load_executable"}}}}},
                        {"required", {"view_id"}}},
       .handler = [this](const nlohmann::json& args) {
         return ListExports(args);
       }});

  server_.RegisterTool(
      {.name = "list_segments",
       .description =
           "List memory segments in a binary view. Returns each segment's "
           "start address, length, and permission flags (read/write/execute).",
       .input_schema = {{"type", "object"},
                        {"properties",
                         {{"view_id",
                           {{"type", "string"},
                            {"description",
                             "The view ID returned by load_executable"}}}}},
                        {"required", {"view_id"}}},
       .handler = [this](const nlohmann::json& args) {
         return ListSegments(args);
       }});

  server_.RegisterTool(
      {.name = "list_sections",
       .description =
           "List named sections in a binary view. Returns each section's "
           "start address, length, name, and semantics (code/data/external).",
       .input_schema = {{"type", "object"},
                        {"properties",
                         {{"view_id",
                           {{"type", "string"},
                            {"description",
                             "The view ID returned by load_executable"}}}}},
                        {"required", {"view_id"}}},
       .handler = [this](const nlohmann::json& args) {
         return ListSections(args);
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

binja::Ref<binja::BinaryView> BnMcp::FindView(const std::string& view_id) {
  std::lock_guard lock(views_mutex_);
  auto it = views_.find(view_id);
  if (it == views_.end()) return nullptr;
  return it->second;
}

std::vector<binja::Ref<binja::Function>> BnMcp::FindFunctionsAt(
    binja::BinaryView* bv, uint64_t address) {
  return bv->GetAnalysisFunctionsForAddress(address);
}

nlohmann::json BnMcp::NoFunctionError(binja::BinaryView* bv,
                                      uint64_t address) {
  std::string msg =
      std::format("No function starts at address 0x{:x}.", address);

  // Check if the address falls inside a known function and hint about it.
  auto containing = bv->GetAnalysisFunctionsContainingAddress(address);
  if (!containing.empty()) {
    auto& f = containing[0];
    auto name =
        f->GetSymbol()
            ? f->GetSymbol()->GetFullName()
            : std::format("sub_{:x}", f->GetStart());
    msg += std::format(
        " However, this address is inside '{}' which starts at 0x{:x}."
        " Use that start address instead.",
        name, f->GetStart());
  }

  return {{"content", {{{"type", "text"}, {"text", msg}}}},
          {"isError", true}};
}

BNBinaryView* BnMcp::GetView(const char* view_id) {
  std::lock_guard lock(views_mutex_);
  auto it = views_.find(view_id);
  if (it == views_.end()) return nullptr;
  return BNNewViewReference(it->second->GetObject());
}

static const char* LlilOpName(BNLowLevelILOperation op) {
  // Must match the BNLowLevelILOperation enum order in binaryninjacore.h.
  static const char* const kNames[] = {
      "NOP",                          // LLIL_NOP
      "SET_REG",                      // LLIL_SET_REG
      "SET_REG_SPLIT",                // LLIL_SET_REG_SPLIT
      "SET_FLAG",                     // LLIL_SET_FLAG
      "SET_REG_STACK_REL",            // LLIL_SET_REG_STACK_REL
      "REG_STACK_PUSH",               // LLIL_REG_STACK_PUSH
      "ASSERT",                       // LLIL_ASSERT
      "FORCE_VER",                    // LLIL_FORCE_VER
      "LOAD",                         // LLIL_LOAD
      "STORE",                        // LLIL_STORE
      "PUSH",                         // LLIL_PUSH
      "POP",                          // LLIL_POP
      "REG",                          // LLIL_REG
      "REG_SPLIT",                    // LLIL_REG_SPLIT
      "REG_STACK_REL",                // LLIL_REG_STACK_REL
      "REG_STACK_POP",                // LLIL_REG_STACK_POP
      "REG_STACK_FREE_REG",           // LLIL_REG_STACK_FREE_REG
      "REG_STACK_FREE_REL",           // LLIL_REG_STACK_FREE_REL
      "CONST",                        // LLIL_CONST
      "CONST_PTR",                    // LLIL_CONST_PTR
      "EXTERN_PTR",                   // LLIL_EXTERN_PTR
      "FLOAT_CONST",                  // LLIL_FLOAT_CONST
      "FLAG",                         // LLIL_FLAG
      "FLAG_BIT",                     // LLIL_FLAG_BIT
      "ADD",                          // LLIL_ADD
      "ADC",                          // LLIL_ADC
      "SUB",                          // LLIL_SUB
      "SBB",                          // LLIL_SBB
      "AND",                          // LLIL_AND
      "OR",                           // LLIL_OR
      "XOR",                          // LLIL_XOR
      "LSL",                          // LLIL_LSL
      "LSR",                          // LLIL_LSR
      "ASR",                          // LLIL_ASR
      "ROL",                          // LLIL_ROL
      "RLC",                          // LLIL_RLC
      "ROR",                          // LLIL_ROR
      "RRC",                          // LLIL_RRC
      "MUL",                          // LLIL_MUL
      "MULU_DP",                      // LLIL_MULU_DP
      "MULS_DP",                      // LLIL_MULS_DP
      "DIVU",                         // LLIL_DIVU
      "DIVU_DP",                      // LLIL_DIVU_DP
      "DIVS",                         // LLIL_DIVS
      "DIVS_DP",                      // LLIL_DIVS_DP
      "MODU",                         // LLIL_MODU
      "MODU_DP",                      // LLIL_MODU_DP
      "MODS",                         // LLIL_MODS
      "MODS_DP",                      // LLIL_MODS_DP
      "NEG",                          // LLIL_NEG
      "NOT",                          // LLIL_NOT
      "SX",                           // LLIL_SX
      "ZX",                           // LLIL_ZX
      "LOW_PART",                     // LLIL_LOW_PART
      "JUMP",                         // LLIL_JUMP
      "JUMP_TO",                      // LLIL_JUMP_TO
      "CALL",                         // LLIL_CALL
      "CALL_STACK_ADJUST",            // LLIL_CALL_STACK_ADJUST
      "TAILCALL",                     // LLIL_TAILCALL
      "RET",                          // LLIL_RET
      "NORET",                        // LLIL_NORET
      "IF",                           // LLIL_IF
      "GOTO",                         // LLIL_GOTO
      "FLAG_COND",                    // LLIL_FLAG_COND
      "FLAG_GROUP",                   // LLIL_FLAG_GROUP
      "CMP_E",                        // LLIL_CMP_E
      "CMP_NE",                       // LLIL_CMP_NE
      "CMP_SLT",                      // LLIL_CMP_SLT
      "CMP_ULT",                      // LLIL_CMP_ULT
      "CMP_SLE",                      // LLIL_CMP_SLE
      "CMP_ULE",                      // LLIL_CMP_ULE
      "CMP_SGE",                      // LLIL_CMP_SGE
      "CMP_UGE",                      // LLIL_CMP_UGE
      "CMP_SGT",                      // LLIL_CMP_SGT
      "CMP_UGT",                      // LLIL_CMP_UGT
      "TEST_BIT",                     // LLIL_TEST_BIT
      "BOOL_TO_INT",                  // LLIL_BOOL_TO_INT
      "ADD_OVERFLOW",                 // LLIL_ADD_OVERFLOW
      "SYSCALL",                      // LLIL_SYSCALL
      "BP",                           // LLIL_BP
      "TRAP",                         // LLIL_TRAP
      "INTRINSIC",                    // LLIL_INTRINSIC
      "UNDEF",                        // LLIL_UNDEF
      "UNIMPL",                       // LLIL_UNIMPL
      "UNIMPL_MEM",                   // LLIL_UNIMPL_MEM
      "FADD",                         // LLIL_FADD
      "FSUB",                         // LLIL_FSUB
      "FMUL",                         // LLIL_FMUL
      "FDIV",                         // LLIL_FDIV
      "FSQRT",                        // LLIL_FSQRT
      "FNEG",                         // LLIL_FNEG
      "FABS",                         // LLIL_FABS
      "FLOAT_TO_INT",                 // LLIL_FLOAT_TO_INT
      "INT_TO_FLOAT",                 // LLIL_INT_TO_FLOAT
      "FLOAT_CONV",                   // LLIL_FLOAT_CONV
      "ROUND_TO_INT",                 // LLIL_ROUND_TO_INT
      "FLOOR",                        // LLIL_FLOOR
      "CEIL",                         // LLIL_CEIL
      "FTRUNC",                       // LLIL_FTRUNC
      "FCMP_E",                       // LLIL_FCMP_E
      "FCMP_NE",                      // LLIL_FCMP_NE
      "FCMP_LT",                      // LLIL_FCMP_LT
      "FCMP_LE",                      // LLIL_FCMP_LE
      "FCMP_GE",                      // LLIL_FCMP_GE
      "FCMP_GT",                      // LLIL_FCMP_GT
      "FCMP_O",                       // LLIL_FCMP_O
      "FCMP_UO",                      // LLIL_FCMP_UO
      "SET_REG_SSA",                  // LLIL_SET_REG_SSA
      "SET_REG_SSA_PARTIAL",          // LLIL_SET_REG_SSA_PARTIAL
      "SET_REG_SPLIT_SSA",            // LLIL_SET_REG_SPLIT_SSA
      "SET_REG_STACK_REL_SSA",        // LLIL_SET_REG_STACK_REL_SSA
      "SET_REG_STACK_ABS_SSA",        // LLIL_SET_REG_STACK_ABS_SSA
      "REG_SPLIT_DEST_SSA",           // LLIL_REG_SPLIT_DEST_SSA
      "REG_STACK_DEST_SSA",           // LLIL_REG_STACK_DEST_SSA
      "REG_SSA",                      // LLIL_REG_SSA
      "REG_SSA_PARTIAL",              // LLIL_REG_SSA_PARTIAL
      "REG_SPLIT_SSA",                // LLIL_REG_SPLIT_SSA
      "REG_STACK_REL_SSA",            // LLIL_REG_STACK_REL_SSA
      "REG_STACK_ABS_SSA",            // LLIL_REG_STACK_ABS_SSA
      "REG_STACK_FREE_REL_SSA",       // LLIL_REG_STACK_FREE_REL_SSA
      "REG_STACK_FREE_ABS_SSA",       // LLIL_REG_STACK_FREE_ABS_SSA
      "SET_FLAG_SSA",                 // LLIL_SET_FLAG_SSA
      "ASSERT_SSA",                   // LLIL_ASSERT_SSA
      "FORCE_VER_SSA",                // LLIL_FORCE_VER_SSA
      "FLAG_SSA",                     // LLIL_FLAG_SSA
      "FLAG_BIT_SSA",                 // LLIL_FLAG_BIT_SSA
      "CALL_SSA",                     // LLIL_CALL_SSA
      "SYSCALL_SSA",                  // LLIL_SYSCALL_SSA
      "TAILCALL_SSA",                 // LLIL_TAILCALL_SSA
      "CALL_PARAM",                   // LLIL_CALL_PARAM
      "CALL_STACK_SSA",               // LLIL_CALL_STACK_SSA
      "CALL_OUTPUT_SSA",              // LLIL_CALL_OUTPUT_SSA
      "SEPARATE_PARAM_LIST_SSA",      // LLIL_SEPARATE_PARAM_LIST_SSA
      "SHARED_PARAM_SLOT_SSA",        // LLIL_SHARED_PARAM_SLOT_SSA
      "MEMORY_INTRINSIC_OUTPUT_SSA",  // LLIL_MEMORY_INTRINSIC_OUTPUT_SSA
      "LOAD_SSA",                     // LLIL_LOAD_SSA
      "STORE_SSA",                    // LLIL_STORE_SSA
      "INTRINSIC_SSA",                // LLIL_INTRINSIC_SSA
      "MEMORY_INTRINSIC_SSA",         // LLIL_MEMORY_INTRINSIC_SSA
      "REG_PHI",                      // LLIL_REG_PHI
      "REG_STACK_PHI",                // LLIL_REG_STACK_PHI
      "FLAG_PHI",                     // LLIL_FLAG_PHI
      "MEM_PHI",                      // LLIL_MEM_PHI
  };
  auto idx = static_cast<size_t>(op);
  if (idx < std::size(kNames)) return kNames[idx];
  return "UNKNOWN";
}

static std::string TokensToString(
    const std::vector<binja::InstructionTextToken>& tokens) {
  std::string result;
  for (const auto& tok : tokens) result += tok.text;
  return result;
}

nlohmann::json BnMcp::ExprToJson(const binja::LowLevelILInstruction& expr) {
  auto op = expr.operation;
  nlohmann::json node = {
      {"expr_index", expr.exprIndex},
      {"operation", LlilOpName(op)},
      {"size", expr.size},
  };

  auto arch = expr.function->GetArchitecture();
  nlohmann::json children = nlohmann::json::array();

  for (const auto& operand : expr.GetOperands()) {
    switch (operand.GetType()) {
      case BinaryNinja::ExprLowLevelOperand:
        children.push_back(ExprToJson(operand.GetExpr()));
        break;
      case BinaryNinja::IntegerLowLevelOperand:
        children.push_back(
            {{"type", "int"},
             {"value", static_cast<int64_t>(operand.GetInteger())}});
        break;
      case BinaryNinja::RegisterLowLevelOperand: {
        auto reg_name = arch ? arch->GetRegisterName(operand.GetRegister())
                             : std::format("reg{}", operand.GetRegister());
        children.push_back({{"type", "register"}, {"name", reg_name}});
        break;
      }
      case BinaryNinja::FlagLowLevelOperand:
        children.push_back({{"type", "flag"}, {"index", operand.GetFlag()}});
        break;
      case BinaryNinja::IndexLowLevelOperand:
        children.push_back({{"type", "index"}, {"value", operand.GetIndex()}});
        break;
      case BinaryNinja::SSARegisterLowLevelOperand: {
        auto ssa_reg = operand.GetSSARegister();
        auto reg_name = arch ? arch->GetRegisterName(ssa_reg.reg)
                             : std::format("reg{}", ssa_reg.reg);
        children.push_back({{"type", "register_ssa"},
                            {"name", reg_name},
                            {"version", ssa_reg.version}});
        break;
      }
      case BinaryNinja::SSAFlagLowLevelOperand: {
        auto ssa_flag = operand.GetSSAFlag();
        children.push_back({{"type", "flag_ssa"},
                            {"index", ssa_flag.flag},
                            {"version", ssa_flag.version}});
        break;
      }
      case BinaryNinja::FlagConditionLowLevelOperand:
        children.push_back(
            {{"type", "flag_condition"},
             {"value", static_cast<int>(operand.GetFlagCondition())}});
        break;
      case BinaryNinja::IntrinsicLowLevelOperand:
        children.push_back(
            {{"type", "intrinsic"}, {"index", operand.GetIntrinsic()}});
        break;
      case BinaryNinja::ExprListLowLevelOperand: {
        auto list = nlohmann::json::array();
        for (const auto& sub : operand.GetExprList())
          list.push_back(ExprToJson(sub));
        children.push_back({{"type", "expr_list"}, {"exprs", list}});
        break;
      }
      case BinaryNinja::SSARegisterListLowLevelOperand: {
        auto list = nlohmann::json::array();
        for (const auto& ssa_reg : operand.GetSSARegisterList()) {
          auto rn = arch ? arch->GetRegisterName(ssa_reg.reg)
                         : std::format("reg{}", ssa_reg.reg);
          list.push_back({{"name", rn}, {"version", ssa_reg.version}});
        }
        children.push_back(
            {{"type", "ssa_register_list"}, {"registers", list}});
        break;
      }
      default:
        // Skip other complex operand types.
        break;
    }
  }

  if (!children.empty()) node["operands"] = children;
  return node;
}

nlohmann::json BnMcp::ListLlilInstructions(const nlohmann::json& args) {
  return ListLlilImpl(args, false);
}

nlohmann::json BnMcp::ListLlilSsaInstructions(const nlohmann::json& args) {
  return ListLlilImpl(args, true);
}

nlohmann::json BnMcp::GetLlilExprTree(const nlohmann::json& args) {
  return GetLlilExprTreeImpl(args, false);
}

nlohmann::json BnMcp::GetLlilSsaExprTree(const nlohmann::json& args) {
  return GetLlilExprTreeImpl(args, true);
}

nlohmann::json BnMcp::ListLlilImpl(const nlohmann::json& args, bool ssa) {
  auto view_id = args.at("view_id").get<std::string>();
  auto address = args.at("address").get<uint64_t>();

  auto bv = FindView(view_id);
  if (!bv) {
    return {{"content",
             {{{"type", "text"},
               {"text", std::format("No binary view with ID: {}", view_id)}}}},
            {"isError", true}};
  }

  auto funcs = FindFunctionsAt(bv.GetPtr(), address);
  if (funcs.empty()) return NoFunctionError(bv.GetPtr(), address);

  auto func = funcs[0];
  auto base_llil = func->GetLowLevelIL();
  if (!base_llil) {
    return {{"content",
             {{{"type", "text"}, {"text", "Failed to get LLIL for function"}}}},
            {"isError", true}};
  }
  auto llil = ssa ? base_llil->GetSSAForm() : base_llil;
  if (!llil) {
    return {{"content",
             {{{"type", "text"}, {"text", "Failed to get LLIL SSA form"}}}},
            {"isError", true}};
  }

  auto arch = func->GetArchitecture();
  size_t count = llil->GetInstructionCount();
  auto form = ssa ? "SSA" : "non-SSA";

  std::string result =
      std::format("LLIL ({}) for function at 0x{:x} ({} instructions):\n", form,
                  address, count);

  for (size_t i = 0; i < count; i++) {
    auto instr = llil->GetInstruction(i);
    std::vector<binja::InstructionTextToken> tokens;
    llil->GetInstructionText(func, arch, i, tokens);
    auto text = TokensToString(tokens);

    result +=
        std::format("  {:4d}  0x{:08x}  {:20s}  {}\n", static_cast<int>(i),
                    instr.address, LlilOpName(instr.operation), text);
  }

  return {{"content", {{{"type", "text"}, {"text", result}}}}};
}

nlohmann::json BnMcp::GetLlilExprTreeImpl(const nlohmann::json& args,
                                          bool ssa) {
  auto view_id = args.at("view_id").get<std::string>();
  auto address = args.at("address").get<uint64_t>();
  auto instr_index = args.at("instr_index").get<size_t>();

  auto bv = FindView(view_id);
  if (!bv) {
    return {{"content",
             {{{"type", "text"},
               {"text", std::format("No binary view with ID: {}", view_id)}}}},
            {"isError", true}};
  }

  auto funcs = FindFunctionsAt(bv.GetPtr(), address);
  if (funcs.empty()) return NoFunctionError(bv.GetPtr(), address);

  auto func = funcs[0];
  auto base_llil = func->GetLowLevelIL();
  if (!base_llil) {
    return {{"content",
             {{{"type", "text"}, {"text", "Failed to get LLIL for function"}}}},
            {"isError", true}};
  }
  auto llil = ssa ? base_llil->GetSSAForm() : base_llil;
  if (!llil) {
    return {{"content",
             {{{"type", "text"}, {"text", "Failed to get LLIL SSA form"}}}},
            {"isError", true}};
  }

  if (instr_index >= llil->GetInstructionCount()) {
    return {
        {"content",
         {{{"type", "text"},
           {"text",
            std::format("Instruction index {} out of range (max {})",
                        static_cast<int>(instr_index),
                        static_cast<int>(llil->GetInstructionCount() - 1))}}}},
        {"isError", true}};
  }

  auto instr = llil->GetInstruction(instr_index);
  auto tree = ExprToJson(instr);

  auto arch = func->GetArchitecture();
  std::vector<binja::InstructionTextToken> tokens;
  llil->GetInstructionText(func, arch, instr_index, tokens);
  tree["text"] = TokensToString(tokens);
  tree["address"] = std::format("0x{:x}", instr.address);
  tree["instr_index"] = instr_index;

  return {{"content", {{{"type", "text"}, {"text", tree.dump(2)}}}}};
}

nlohmann::json BnMcp::ListFunctions(const nlohmann::json& args) {
  auto view_id = args.at("view_id").get<std::string>();

  auto bv = FindView(view_id);
  if (!bv) {
    return {{"content",
             {{{"type", "text"},
               {"text", std::format("No binary view with ID: {}", view_id)}}}},
            {"isError", true}};
  }

  auto functions = bv->GetAnalysisFunctionList();
  std::string result = std::format("Functions ({}):\n", functions.size());

  for (const auto& func : functions) {
    auto name = func->GetSymbol() ? func->GetSymbol()->GetFullName()
                                  : std::format("sub_{:x}", func->GetStart());
    auto bbs = func->GetBasicBlocks();
    uint64_t size = 0;
    for (const auto& bb : bbs) size += bb->GetEnd() - bb->GetStart();
    result +=
        std::format("  0x{:x}  {:6d}  {}\n", func->GetStart(), size, name);
  }

  return {{"content", {{{"type", "text"}, {"text", result}}}}};
}

nlohmann::json BnMcp::GetFunctionInfo(const nlohmann::json& args) {
  auto view_id = args.at("view_id").get<std::string>();
  auto address = args.at("address").get<uint64_t>();

  auto bv = FindView(view_id);
  if (!bv) {
    return {{"content",
             {{{"type", "text"},
               {"text", std::format("No binary view with ID: {}", view_id)}}}},
            {"isError", true}};
  }

  auto funcs = FindFunctionsAt(bv.GetPtr(), address);
  if (funcs.empty()) return NoFunctionError(bv.GetPtr(), address);

  auto func = funcs[0];
  auto name = func->GetSymbol() ? func->GetSymbol()->GetFullName()
                                : std::format("sub_{:x}", func->GetStart());

  auto bbs = func->GetBasicBlocks();
  auto type = func->GetType();

  std::string cc_name = "unknown";
  if (type) {
    auto cc = type->GetCallingConvention();
    if (cc.GetValue()) cc_name = cc.GetValue()->GetName();
  }

  std::string ret_type = "void";
  if (type) {
    auto rt = type->GetChildType();
    if (rt.GetValue()) ret_type = rt.GetValue()->GetString();
  }

  std::string params;
  auto param_vars = func->GetParameterVariables().GetValue();
  for (size_t i = 0; i < param_vars.size(); i++) {
    auto& pv = param_vars[i];
    auto ptype = func->GetVariableType(pv);
    auto pname = func->GetVariableName(pv);
    if (pname.empty()) pname = std::format("arg{}", i);
    std::string tstr = ptype.GetValue() ? ptype.GetValue()->GetString() : "?";
    if (i > 0) params += ", ";
    params += tstr + " " + pname;
  }

  auto result = std::format(
      "Function: {}\n"
      "Address: 0x{:x}\n"
      "Calling convention: {}\n"
      "Return type: {}\n"
      "Parameters: ({})\n"
      "Basic blocks: {}\n",
      name, func->GetStart(), cc_name, ret_type, params, bbs.size());

  return {{"content", {{{"type", "text"}, {"text", result}}}}};
}

nlohmann::json BnMcp::GetStrings(const nlohmann::json& args) {
  auto view_id = args.at("view_id").get<std::string>();

  auto bv = FindView(view_id);
  if (!bv) {
    return {{"content",
             {{{"type", "text"},
               {"text", std::format("No binary view with ID: {}", view_id)}}}},
            {"isError", true}};
  }

  std::vector<BNStringReference> strings;
  if (args.contains("start") && args.contains("length")) {
    auto start = args["start"].get<uint64_t>();
    auto length = args["length"].get<uint64_t>();
    strings = bv->GetStrings(start, length);
  } else {
    strings = bv->GetStrings();
  }

  std::string result = std::format("Strings ({}):\n", strings.size());
  for (const auto& s : strings) {
    auto buf = bv->ReadBuffer(s.start, s.length);
    std::string content(static_cast<const char*>(buf.GetData()),
                        std::min(s.length, static_cast<size_t>(256)));
    // Escape newlines for readability.
    for (auto& c : content) {
      if (c == '\n') c = ' ';
      if (c == '\r') c = ' ';
    }
    result += std::format("  0x{:x}  len={:4d}  \"{}\"\n", s.start, s.length,
                          content);
  }

  return {{"content", {{{"type", "text"}, {"text", result}}}}};
}

nlohmann::json BnMcp::GetXrefs(const nlohmann::json& args) {
  auto view_id = args.at("view_id").get<std::string>();
  auto address = args.at("address").get<uint64_t>();

  auto bv = FindView(view_id);
  if (!bv) {
    return {{"content",
             {{{"type", "text"},
               {"text", std::format("No binary view with ID: {}", view_id)}}}},
            {"isError", true}};
  }

  auto code_refs = bv->GetCallers(address);
  auto data_refs = bv->GetDataReferences(address);

  std::string result = std::format("Cross-references to 0x{:x}:\n", address);

  result += std::format("\nCode references ({}):\n", code_refs.size());
  for (const auto& ref : code_refs) {
    std::string func_name = "unknown";
    if (ref.func) {
      auto sym = ref.func->GetSymbol();
      func_name = sym ? sym->GetFullName()
                      : std::format("sub_{:x}", ref.func->GetStart());
    }
    result += std::format("  0x{:x}  in {}\n", ref.addr, func_name);
  }

  result += std::format("\nData references ({}):\n", data_refs.size());
  for (auto addr : data_refs) {
    result += std::format("  0x{:x}\n", addr);
  }

  return {{"content", {{{"type", "text"}, {"text", result}}}}};
}

static const char* TypeClassName(BNTypeClass tc) {
  switch (tc) {
    case VoidTypeClass:
      return "void";
    case BoolTypeClass:
      return "bool";
    case IntegerTypeClass:
      return "integer";
    case FloatTypeClass:
      return "float";
    case StructureTypeClass:
      return "struct";
    case EnumerationTypeClass:
      return "enum";
    case PointerTypeClass:
      return "pointer";
    case ArrayTypeClass:
      return "array";
    case FunctionTypeClass:
      return "function";
    case VarArgsTypeClass:
      return "varargs";
    case ValueTypeClass:
      return "value";
    case NamedTypeReferenceClass:
      return "typedef";
    case WideCharTypeClass:
      return "widechar";
    default:
      return "unknown";
  }
}

nlohmann::json BnMcp::ListTypes(const nlohmann::json& args) {
  auto view_id = args.at("view_id").get<std::string>();

  auto bv = FindView(view_id);
  if (!bv) {
    return {{"content",
             {{{"type", "text"},
               {"text", std::format("No binary view with ID: {}", view_id)}}}},
            {"isError", true}};
  }

  auto types = bv->GetTypes();

  std::string result = std::format("Types ({}):\n", types.size());
  for (const auto& [name, type] : types) {
    auto tc = type->GetClass();
    auto kind = TypeClassName(tc);
    auto width = type->GetWidth();

    // For structs, show union vs struct.
    if (tc == StructureTypeClass) {
      auto structure = type->GetStructure();
      if (structure && structure->IsUnion()) kind = "union";
    }

    result += std::format("  {:40s}  {:10s}  {:d} bytes\n", name.GetString(),
                          kind, width);
  }

  return {{"content", {{{"type", "text"}, {"text", result}}}}};
}

nlohmann::json BnMcp::GetTypeInfo(const nlohmann::json& args) {
  auto view_id = args.at("view_id").get<std::string>();
  auto type_name = args.at("type_name").get<std::string>();

  auto bv = FindView(view_id);
  if (!bv) {
    return {{"content",
             {{{"type", "text"},
               {"text", std::format("No binary view with ID: {}", view_id)}}}},
            {"isError", true}};
  }

  auto type = bv->GetTypeByName(type_name);
  if (!type) {
    return {{"content",
             {{{"type", "text"},
               {"text", std::format("Type '{}' not found", type_name)}}}},
            {"isError", true}};
  }

  auto tc = type->GetClass();
  auto kind = TypeClassName(tc);
  auto width = type->GetWidth();

  std::string result;

  if (tc == StructureTypeClass) {
    auto structure = type->GetStructure();
    if (!structure) {
      return {{"content",
               {{{"type", "text"}, {"text", "Failed to get structure info"}}}},
              {"isError", true}};
    }
    auto variant = structure->IsUnion() ? "union" : "struct";
    result = std::format("Type: {} ({}, {} bytes)\nMembers:\n", type_name,
                         variant, width);
    for (const auto& member : structure->GetMembers()) {
      auto mtype_str =
          member.type.GetValue() ? member.type.GetValue()->GetString() : "?";
      auto mwidth =
          member.type.GetValue() ? member.type.GetValue()->GetWidth() : 0;
      result += std::format("  +0x{:04x}  {:30s}  {}  ({} bytes)\n",
                            member.offset, mtype_str, member.name, mwidth);
    }
  } else if (tc == EnumerationTypeClass) {
    auto enumeration = type->GetEnumeration();
    if (!enumeration) {
      return {
          {"content",
           {{{"type", "text"}, {"text", "Failed to get enumeration info"}}}},
          {"isError", true}};
    }
    result =
        std::format("Type: {} (enum, {} bytes)\nValues:\n", type_name, width);
    for (const auto& member : enumeration->GetMembers()) {
      result += std::format("  {} = {}\n", member.name, member.value);
    }
  } else if (tc == NamedTypeReferenceClass) {
    auto child = type->GetChildType();
    auto underlying =
        child.GetValue() ? child.GetValue()->GetString() : "unknown";
    result = std::format("Type: {} (typedef)\n  → {}\n", type_name, underlying);
  } else if (tc == PointerTypeClass) {
    auto child = type->GetChildType();
    auto pointee = child.GetValue() ? child.GetValue()->GetString() : "unknown";
    result = std::format("Type: {} (pointer, {} bytes)\n  → {}*\n", type_name,
                         width, pointee);
  } else if (tc == ArrayTypeClass) {
    auto child = type->GetChildType();
    auto elem = child.GetValue() ? child.GetValue()->GetString() : "unknown";
    auto elem_width = child.GetValue() ? child.GetValue()->GetWidth() : 0;
    auto count = elem_width > 0 ? width / elem_width : 0;
    result = std::format("Type: {} (array, {} bytes)\n  {}[{}]\n", type_name,
                         width, elem, count);
  } else if (tc == FunctionTypeClass) {
    result = std::format("Type: {} (function)\n  {}\n", type_name,
                         type->GetString());
  } else {
    result = std::format("Type: {} ({}, {} bytes)\n  {}\n", type_name, kind,
                         width, type->GetString());
  }

  return {{"content", {{{"type", "text"}, {"text", result}}}}};
}

nlohmann::json BnMcp::GetDisassembly(const nlohmann::json& args) {
  auto view_id = args.at("view_id").get<std::string>();
  auto address = args.at("address").get<uint64_t>();
  size_t max_lines = 0;  // 0 = unlimited
  if (args.contains("max_lines")) {
    if (!args["max_lines"].is_number_integer()) {
      return {{"content",
               {{{"type", "text"},
                 {"text", "max_lines must be a non-negative integer"}}}},
              {"isError", true}};
    }
    auto val = args["max_lines"].get<int64_t>();
    if (val < 0) {
      return {{"content",
               {{{"type", "text"},
                 {"text", "max_lines must be a non-negative integer"}}}},
              {"isError", true}};
    }
    max_lines = static_cast<size_t>(val);
  }

  auto bv = FindView(view_id);
  if (!bv) {
    return {{"content",
             {{{"type", "text"},
               {"text", std::format("No binary view with ID: {}", view_id)}}}},
            {"isError", true}};
  }

  auto funcs = FindFunctionsAt(bv.GetPtr(), address);
  if (funcs.empty()) return NoFunctionError(bv.GetPtr(), address);

  auto func = funcs[0];
  auto name = func->GetSymbol() ? func->GetSymbol()->GetFullName()
                                : std::format("sub_{:x}", func->GetStart());

  auto bbs = func->GetBasicBlocks();
  std::sort(bbs.begin(), bbs.end(),
            [](const binja::Ref<binja::BasicBlock>& a,
               const binja::Ref<binja::BasicBlock>& b) {
              return a->GetStart() < b->GetStart();
            });

  binja::Ref<binja::DisassemblySettings> settings =
      new binja::DisassemblySettings();

  std::string result =
      std::format("Disassembly for {} at 0x{:x}:\n", name, func->GetStart());

  size_t line_count = 0;
  bool truncated = false;
  for (size_t bi = 0; bi < bbs.size(); bi++) {
    auto& bb = bbs[bi];

    // Peek: only emit block header if at least one instruction line fits.
    if (max_lines > 0 && line_count + 1 >= max_lines) {
      truncated = true;
      break;
    }
    result += std::format("; block [0x{:x}, 0x{:x})\n", bb->GetStart(),
                          bb->GetEnd());
    line_count++;

    auto lines = bb->GetDisassemblyText(settings.GetPtr());
    for (const auto& line : lines) {
      result += std::format("  0x{:x}  {}\n", line.addr,
                            TokensToString(line.tokens));
      line_count++;
      if (max_lines > 0 && line_count >= max_lines) {
        truncated = true;
        break;
      }
    }
    if (truncated) break;
  }

  if (truncated) {
    result += std::format("... truncated after {} lines\n", max_lines);
  }

  return {{"content", {{{"type", "text"}, {"text", result}}}}};
}

static const char* SymbolTypeLabel(BNSymbolType type) {
  switch (type) {
    case ImportedFunctionSymbol:
      return "func";
    case ImportedDataSymbol:
      return "data";
    case ExternalSymbol:
      return "extern";
    case FunctionSymbol:
      return "func";
    case DataSymbol:
      return "data";
    default:
      return "other";
  }
}

nlohmann::json BnMcp::ListImports(const nlohmann::json& args) {
  auto view_id = args.at("view_id").get<std::string>();

  auto bv = FindView(view_id);
  if (!bv) {
    return {{"content",
             {{{"type", "text"},
               {"text", std::format("No binary view with ID: {}", view_id)}}}},
            {"isError", true}};
  }

  // Collect imported and external symbols for cross-platform coverage.
  // ImportedFunction/ImportedData cover PE imports and ELF PLT stubs.
  // ExternalSymbol covers ELF/Mach-O unresolved externals.
  struct SymEntry {
    uint64_t address;
    BNSymbolType type;
    std::string name;
  };
  std::vector<SymEntry> entries;

  auto collect = [&](BNSymbolType st) {
    for (const auto& sym : bv->GetSymbolsOfType(st)) {
      entries.push_back(
          {sym->GetAddress(), sym->GetType(), sym->GetFullName()});
    }
  };
  collect(ImportedFunctionSymbol);
  collect(ImportedDataSymbol);
  collect(ExternalSymbol);

  // Deduplicate: prefer ImportedFunction/ImportedData over ExternalSymbol
  // when the same (address, name) pair appears from multiple queries.
  auto import_priority = [](BNSymbolType t) -> int {
    switch (t) {
      case ImportedFunctionSymbol: return 0;
      case ImportedDataSymbol:     return 1;
      case ExternalSymbol:         return 2;
      default:                     return 3;
    }
  };
  std::sort(entries.begin(), entries.end(),
            [&](const auto& a, const auto& b) {
              if (a.address != b.address) return a.address < b.address;
              if (a.name != b.name) return a.name < b.name;
              return import_priority(a.type) < import_priority(b.type);
            });
  entries.erase(std::unique(entries.begin(), entries.end(),
                            [](const auto& a, const auto& b) {
                              return a.address == b.address &&
                                     a.name == b.name;
                            }),
                entries.end());

  std::string result =
      std::format("Imported/external symbols ({}):\n", entries.size());
  for (const auto& e : entries) {
    result += std::format("  0x{:x}  {:6s}  {}\n", e.address,
                          SymbolTypeLabel(e.type), e.name);
  }

  return {{"content", {{{"type", "text"}, {"text", result}}}}};
}

nlohmann::json BnMcp::ListExports(const nlohmann::json& args) {
  auto view_id = args.at("view_id").get<std::string>();

  auto bv = FindView(view_id);
  if (!bv) {
    return {{"content",
             {{{"type", "text"},
               {"text", std::format("No binary view with ID: {}", view_id)}}}},
            {"isError", true}};
  }

  struct SymEntry {
    uint64_t address;
    BNSymbolType type;
    std::string name;
  };
  std::vector<SymEntry> entries;

  auto collect = [&](BNSymbolType st) {
    for (const auto& sym : bv->GetSymbolsOfType(st)) {
      auto binding = sym->GetBinding();
      if (binding != GlobalBinding && binding != WeakBinding) continue;
      entries.push_back(
          {sym->GetAddress(), sym->GetType(), sym->GetFullName()});
    }
  };
  collect(FunctionSymbol);
  collect(DataSymbol);

  std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
    if (a.address != b.address) return a.address < b.address;
    return a.name < b.name;
  });

  std::string result =
      std::format("Exported/global symbols ({}):\n", entries.size());
  for (const auto& e : entries) {
    result += std::format("  0x{:x}  {:6s}  {}\n", e.address,
                          SymbolTypeLabel(e.type), e.name);
  }

  return {{"content", {{{"type", "text"}, {"text", result}}}}};
}

static const char* SegmentFlagsToRwx(uint32_t flags) {
  // Index by (readable:2, writable:1, executable:0) bits.
  static const char* const kTable[] = {
      "---", "--x", "-w-", "-wx", "r--", "r-x", "rw-", "rwx",
  };
  unsigned idx = 0;
  if (flags & SegmentReadable) idx |= 4;
  if (flags & SegmentWritable) idx |= 2;
  if (flags & SegmentExecutable) idx |= 1;
  return kTable[idx];
}

nlohmann::json BnMcp::ListSegments(const nlohmann::json& args) {
  auto view_id = args.at("view_id").get<std::string>();

  auto bv = FindView(view_id);
  if (!bv) {
    return {{"content",
             {{{"type", "text"},
               {"text", std::format("No binary view with ID: {}", view_id)}}}},
            {"isError", true}};
  }

  auto segments = bv->GetSegments();
  std::sort(segments.begin(), segments.end(),
            [](const binja::Ref<binja::Segment>& a,
               const binja::Ref<binja::Segment>& b) {
              return a->GetStart() < b->GetStart();
            });

  std::string result = std::format("Segments ({}):\n", segments.size());
  for (const auto& seg : segments) {
    result += std::format("  0x{:x}  0x{:x}  {}\n", seg->GetStart(),
                          seg->GetLength(), SegmentFlagsToRwx(seg->GetFlags()));
  }

  return {{"content", {{{"type", "text"}, {"text", result}}}}};
}

static const char* SectionSemanticsLabel(BNSectionSemantics sem) {
  switch (sem) {
    case ReadOnlyCodeSectionSemantics:
      return "code, readonly";
    case ReadOnlyDataSectionSemantics:
      return "data, readonly";
    case ReadWriteDataSectionSemantics:
      return "data, read-write";
    case ExternalSectionSemantics:
      return "external";
    default:
      return "";
  }
}

nlohmann::json BnMcp::ListSections(const nlohmann::json& args) {
  auto view_id = args.at("view_id").get<std::string>();

  auto bv = FindView(view_id);
  if (!bv) {
    return {{"content",
             {{{"type", "text"},
               {"text", std::format("No binary view with ID: {}", view_id)}}}},
            {"isError", true}};
  }

  auto sections = bv->GetSections();
  std::sort(sections.begin(), sections.end(),
            [](const binja::Ref<binja::Section>& a,
               const binja::Ref<binja::Section>& b) {
              return a->GetStart() < b->GetStart();
            });

  std::string result = std::format("Sections ({}):\n", sections.size());
  for (const auto& sec : sections) {
    auto sem = SectionSemanticsLabel(sec->GetSemantics());
    auto name = sec->GetName();
    if (name.empty()) name = "(unnamed)";
    result += std::format("  0x{:x}  0x{:x}  {}", sec->GetStart(),
                          sec->GetLength(), name);
    if (sem[0] != '\0') result += std::format("  [{}]", sem);
    result += '\n';
  }

  return {{"content", {{{"type", "text"}, {"text", result}}}}};
}

}  // namespace bnmcp
