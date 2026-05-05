#include <binaryninjaapi.h>
#include <binaryninjacore.h>
#include <bntest.h>

namespace binja = BinaryNinja;

static void RunAllTests(binja::BinaryView* view) {
  (void)view;
  bntest::RunTests();
}

extern "C" {

BINARYNINJAPLUGIN uint32_t CorePluginABIVersion() {
  return BN_CURRENT_CORE_ABI_VERSION;
}

BINARYNINJAPLUGIN void CorePluginDependencies() {}

BINARYNINJAPLUGIN bool CorePluginInit() {
  bntest::InitTests();
  binja::PluginCommand::Register("[bn-mcp] Run Tests", "Run bn-mcp unit tests",
                                 RunAllTests);
  return true;
}
}
