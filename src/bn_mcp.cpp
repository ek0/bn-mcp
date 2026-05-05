#include "bn_mcp.h"

#include <binaryninjaapi.h>
#include <lowlevelilinstruction.h>

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

static const char* LlilOpName(BNLowLevelILOperation op) {
  // Must match the BNLowLevelILOperation enum order in binaryninjacore.h.
  static const char* const kNames[] = {
      "NOP",                        // LLIL_NOP
      "SET_REG",                    // LLIL_SET_REG
      "SET_REG_SPLIT",              // LLIL_SET_REG_SPLIT
      "SET_FLAG",                   // LLIL_SET_FLAG
      "SET_REG_STACK_REL",          // LLIL_SET_REG_STACK_REL
      "REG_STACK_PUSH",             // LLIL_REG_STACK_PUSH
      "ASSERT",                     // LLIL_ASSERT
      "FORCE_VER",                  // LLIL_FORCE_VER
      "LOAD",                       // LLIL_LOAD
      "STORE",                      // LLIL_STORE
      "PUSH",                       // LLIL_PUSH
      "POP",                        // LLIL_POP
      "REG",                        // LLIL_REG
      "REG_SPLIT",                  // LLIL_REG_SPLIT
      "REG_STACK_REL",              // LLIL_REG_STACK_REL
      "REG_STACK_POP",              // LLIL_REG_STACK_POP
      "REG_STACK_FREE_REG",         // LLIL_REG_STACK_FREE_REG
      "REG_STACK_FREE_REL",         // LLIL_REG_STACK_FREE_REL
      "CONST",                      // LLIL_CONST
      "CONST_PTR",                  // LLIL_CONST_PTR
      "EXTERN_PTR",                 // LLIL_EXTERN_PTR
      "FLOAT_CONST",                // LLIL_FLOAT_CONST
      "FLAG",                       // LLIL_FLAG
      "FLAG_BIT",                   // LLIL_FLAG_BIT
      "ADD",                        // LLIL_ADD
      "ADC",                        // LLIL_ADC
      "SUB",                        // LLIL_SUB
      "SBB",                        // LLIL_SBB
      "AND",                        // LLIL_AND
      "OR",                         // LLIL_OR
      "XOR",                        // LLIL_XOR
      "LSL",                        // LLIL_LSL
      "LSR",                        // LLIL_LSR
      "ASR",                        // LLIL_ASR
      "ROL",                        // LLIL_ROL
      "RLC",                        // LLIL_RLC
      "ROR",                        // LLIL_ROR
      "RRC",                        // LLIL_RRC
      "MUL",                        // LLIL_MUL
      "MULU_DP",                    // LLIL_MULU_DP
      "MULS_DP",                    // LLIL_MULS_DP
      "DIVU",                       // LLIL_DIVU
      "DIVU_DP",                    // LLIL_DIVU_DP
      "DIVS",                       // LLIL_DIVS
      "DIVS_DP",                    // LLIL_DIVS_DP
      "MODU",                       // LLIL_MODU
      "MODU_DP",                    // LLIL_MODU_DP
      "MODS",                       // LLIL_MODS
      "MODS_DP",                    // LLIL_MODS_DP
      "NEG",                        // LLIL_NEG
      "NOT",                        // LLIL_NOT
      "SX",                         // LLIL_SX
      "ZX",                         // LLIL_ZX
      "LOW_PART",                   // LLIL_LOW_PART
      "JUMP",                       // LLIL_JUMP
      "JUMP_TO",                    // LLIL_JUMP_TO
      "CALL",                       // LLIL_CALL
      "CALL_STACK_ADJUST",          // LLIL_CALL_STACK_ADJUST
      "TAILCALL",                   // LLIL_TAILCALL
      "RET",                        // LLIL_RET
      "NORET",                      // LLIL_NORET
      "IF",                         // LLIL_IF
      "GOTO",                       // LLIL_GOTO
      "FLAG_COND",                  // LLIL_FLAG_COND
      "FLAG_GROUP",                 // LLIL_FLAG_GROUP
      "CMP_E",                      // LLIL_CMP_E
      "CMP_NE",                     // LLIL_CMP_NE
      "CMP_SLT",                    // LLIL_CMP_SLT
      "CMP_ULT",                    // LLIL_CMP_ULT
      "CMP_SLE",                    // LLIL_CMP_SLE
      "CMP_ULE",                    // LLIL_CMP_ULE
      "CMP_SGE",                    // LLIL_CMP_SGE
      "CMP_UGE",                    // LLIL_CMP_UGE
      "CMP_SGT",                    // LLIL_CMP_SGT
      "CMP_UGT",                    // LLIL_CMP_UGT
      "TEST_BIT",                   // LLIL_TEST_BIT
      "BOOL_TO_INT",                // LLIL_BOOL_TO_INT
      "ADD_OVERFLOW",               // LLIL_ADD_OVERFLOW
      "SYSCALL",                    // LLIL_SYSCALL
      "BP",                         // LLIL_BP
      "TRAP",                       // LLIL_TRAP
      "INTRINSIC",                  // LLIL_INTRINSIC
      "UNDEF",                      // LLIL_UNDEF
      "UNIMPL",                     // LLIL_UNIMPL
      "UNIMPL_MEM",                 // LLIL_UNIMPL_MEM
      "FADD",                       // LLIL_FADD
      "FSUB",                       // LLIL_FSUB
      "FMUL",                       // LLIL_FMUL
      "FDIV",                       // LLIL_FDIV
      "FSQRT",                      // LLIL_FSQRT
      "FNEG",                       // LLIL_FNEG
      "FABS",                       // LLIL_FABS
      "FLOAT_TO_INT",               // LLIL_FLOAT_TO_INT
      "INT_TO_FLOAT",               // LLIL_INT_TO_FLOAT
      "FLOAT_CONV",                 // LLIL_FLOAT_CONV
      "ROUND_TO_INT",               // LLIL_ROUND_TO_INT
      "FLOOR",                      // LLIL_FLOOR
      "CEIL",                       // LLIL_CEIL
      "FTRUNC",                     // LLIL_FTRUNC
      "FCMP_E",                     // LLIL_FCMP_E
      "FCMP_NE",                    // LLIL_FCMP_NE
      "FCMP_LT",                    // LLIL_FCMP_LT
      "FCMP_LE",                    // LLIL_FCMP_LE
      "FCMP_GE",                    // LLIL_FCMP_GE
      "FCMP_GT",                    // LLIL_FCMP_GT
      "FCMP_O",                     // LLIL_FCMP_O
      "FCMP_UO",                    // LLIL_FCMP_UO
      "SET_REG_SSA",                // LLIL_SET_REG_SSA
      "SET_REG_SSA_PARTIAL",        // LLIL_SET_REG_SSA_PARTIAL
      "SET_REG_SPLIT_SSA",          // LLIL_SET_REG_SPLIT_SSA
      "SET_REG_STACK_REL_SSA",      // LLIL_SET_REG_STACK_REL_SSA
      "SET_REG_STACK_ABS_SSA",      // LLIL_SET_REG_STACK_ABS_SSA
      "REG_SPLIT_DEST_SSA",         // LLIL_REG_SPLIT_DEST_SSA
      "REG_STACK_DEST_SSA",         // LLIL_REG_STACK_DEST_SSA
      "REG_SSA",                    // LLIL_REG_SSA
      "REG_SSA_PARTIAL",            // LLIL_REG_SSA_PARTIAL
      "REG_SPLIT_SSA",              // LLIL_REG_SPLIT_SSA
      "REG_STACK_REL_SSA",          // LLIL_REG_STACK_REL_SSA
      "REG_STACK_ABS_SSA",          // LLIL_REG_STACK_ABS_SSA
      "REG_STACK_FREE_REL_SSA",     // LLIL_REG_STACK_FREE_REL_SSA
      "REG_STACK_FREE_ABS_SSA",     // LLIL_REG_STACK_FREE_ABS_SSA
      "SET_FLAG_SSA",               // LLIL_SET_FLAG_SSA
      "ASSERT_SSA",                 // LLIL_ASSERT_SSA
      "FORCE_VER_SSA",              // LLIL_FORCE_VER_SSA
      "FLAG_SSA",                   // LLIL_FLAG_SSA
      "FLAG_BIT_SSA",               // LLIL_FLAG_BIT_SSA
      "CALL_SSA",                   // LLIL_CALL_SSA
      "SYSCALL_SSA",                // LLIL_SYSCALL_SSA
      "TAILCALL_SSA",               // LLIL_TAILCALL_SSA
      "CALL_PARAM",                 // LLIL_CALL_PARAM
      "CALL_STACK_SSA",             // LLIL_CALL_STACK_SSA
      "CALL_OUTPUT_SSA",            // LLIL_CALL_OUTPUT_SSA
      "SEPARATE_PARAM_LIST_SSA",    // LLIL_SEPARATE_PARAM_LIST_SSA
      "SHARED_PARAM_SLOT_SSA",      // LLIL_SHARED_PARAM_SLOT_SSA
      "MEMORY_INTRINSIC_OUTPUT_SSA",  // LLIL_MEMORY_INTRINSIC_OUTPUT_SSA
      "LOAD_SSA",                   // LLIL_LOAD_SSA
      "STORE_SSA",                  // LLIL_STORE_SSA
      "INTRINSIC_SSA",              // LLIL_INTRINSIC_SSA
      "MEMORY_INTRINSIC_SSA",       // LLIL_MEMORY_INTRINSIC_SSA
      "REG_PHI",                    // LLIL_REG_PHI
      "REG_STACK_PHI",              // LLIL_REG_STACK_PHI
      "FLAG_PHI",                   // LLIL_FLAG_PHI
      "MEM_PHI",                    // LLIL_MEM_PHI
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

  auto funcs = bv->GetAnalysisFunctionsForAddress(address);
  if (funcs.empty()) {
    return {
        {"content",
         {{{"type", "text"},
           {"text", std::format("No function at address 0x{:x}", address)}}}},
        {"isError", true}};
  }

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

  auto funcs = bv->GetAnalysisFunctionsForAddress(address);
  if (funcs.empty()) {
    return {
        {"content",
         {{{"type", "text"},
           {"text", std::format("No function at address 0x{:x}", address)}}}},
        {"isError", true}};
  }

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

}  // namespace bnmcp
