#include <binaryninjaapi.h>
#include <binaryninjacore.h>
#include <gtest/gtest.h>
#include <lowlevelilinstruction.h>

#include <nlohmann/json.hpp>

#include "bn_mcp.h"

namespace binja = BinaryNinja;

namespace {

class LlilExprTest : public ::testing::Test {
 protected:
  void SetUp() override {
    arch_ = binja::Architecture::GetByName("x86_64");
    ASSERT_NE(arch_.GetPtr(), nullptr);
    llil_ = new binja::LowLevelILFunction{arch_};
  }

  binja::Ref<binja::Architecture> arch_ = nullptr;
  binja::Ref<binja::LowLevelILFunction> llil_ = nullptr;

  // Helper: build an expression and serialize it.
  nlohmann::json Serialize(size_t id) {
    return bnmcp::BnMcp::ExprToJson(llil_->GetExpr(id));
  }

  // Helper: build two dummy operands (REG(rax), CONST(1)).
  std::pair<size_t, size_t> MakeBinOpArgs() {
    return {llil_->Register(8, 0), llil_->Const(8, 1)};
  }
};

// --- Constants ---

TEST_F(LlilExprTest, Const) {
  auto j = Serialize(llil_->Const(8, 0x42));
  EXPECT_EQ(j["operation"], "CONST");
  EXPECT_EQ(j["size"], 8);
  EXPECT_EQ(j["operands"][0]["type"], "int");
  EXPECT_EQ(j["operands"][0]["value"], 0x42);
}

TEST_F(LlilExprTest, ConstPointer) {
  auto j = Serialize(llil_->ConstPointer(8, 0xDEAD));
  EXPECT_EQ(j["operation"], "CONST_PTR");
  EXPECT_EQ(j["operands"][0]["value"], 0xDEAD);
}

TEST_F(LlilExprTest, ExternPointer) {
  auto j = Serialize(llil_->ExternPointer(8, 0x1000, 0x10));
  EXPECT_EQ(j["operation"], "EXTERN_PTR");
}

TEST_F(LlilExprTest, FloatConst) {
  auto j = Serialize(llil_->FloatConstRaw(4, 0x3F800000));
  EXPECT_EQ(j["operation"], "FLOAT_CONST");
}

// --- Register / Flag ---

TEST_F(LlilExprTest, Reg) {
  auto j = Serialize(llil_->Register(8, 0));
  EXPECT_EQ(j["operation"], "REG");
  EXPECT_EQ(j["operands"][0]["type"], "register");
}

TEST_F(LlilExprTest, RegisterSplit) {
  auto j = Serialize(llil_->RegisterSplit(8, 2, 0));
  EXPECT_EQ(j["operation"], "REG_SPLIT");
}

TEST_F(LlilExprTest, Flag) {
  auto j = Serialize(llil_->Flag(0));
  EXPECT_EQ(j["operation"], "FLAG");
  EXPECT_EQ(j["operands"][0]["type"], "flag");
}

TEST_F(LlilExprTest, FlagBit) {
  auto j = Serialize(llil_->FlagBit(1, 0, 0));
  EXPECT_EQ(j["operation"], "FLAG_BIT");
}

// --- SET_REG / SET_FLAG ---

TEST_F(LlilExprTest, SetReg) {
  auto val = llil_->Const(8, 0);
  auto j = Serialize(llil_->SetRegister(8, 0, val));
  EXPECT_EQ(j["operation"], "SET_REG");
  EXPECT_EQ(j["operands"][0]["type"], "register");
  EXPECT_EQ(j["operands"][1]["operation"], "CONST");
}

TEST_F(LlilExprTest, SetRegSplit) {
  auto val = llil_->Const(8, 0);
  auto j = Serialize(llil_->SetRegisterSplit(8, 2, 0, val));
  EXPECT_EQ(j["operation"], "SET_REG_SPLIT");
}

TEST_F(LlilExprTest, SetFlag) {
  auto val = llil_->Const(0, 1);
  auto j = Serialize(llil_->SetFlag(0, val));
  EXPECT_EQ(j["operation"], "SET_FLAG");
  EXPECT_EQ(j["operands"][0]["type"], "flag");
  EXPECT_EQ(j["operands"][1]["operation"], "CONST");
}

// --- Memory ---

TEST_F(LlilExprTest, Load) {
  auto addr = llil_->ConstPointer(8, 0x1000);
  auto j = Serialize(llil_->Load(8, addr));
  EXPECT_EQ(j["operation"], "LOAD");
  EXPECT_EQ(j["operands"][0]["operation"], "CONST_PTR");
}

TEST_F(LlilExprTest, Store) {
  auto addr = llil_->ConstPointer(8, 0x2000);
  auto val = llil_->Register(8, 0);
  auto j = Serialize(llil_->Store(8, addr, val));
  EXPECT_EQ(j["operation"], "STORE");
  EXPECT_EQ(j["operands"][0]["operation"], "CONST_PTR");
  EXPECT_EQ(j["operands"][1]["operation"], "REG");
}

TEST_F(LlilExprTest, Push) {
  auto val = llil_->Register(8, 0);
  auto j = Serialize(llil_->Push(8, val));
  EXPECT_EQ(j["operation"], "PUSH");
  EXPECT_EQ(j["operands"][0]["operation"], "REG");
}

TEST_F(LlilExprTest, Pop) {
  auto j = Serialize(llil_->Pop(8));
  EXPECT_EQ(j["operation"], "POP");
}

// --- Arithmetic (binary ops) ---

TEST_F(LlilExprTest, Add) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->Add(8, a, b));
  EXPECT_EQ(j["operation"], "ADD");
  EXPECT_EQ(j["operands"].size(), 2);
}

TEST_F(LlilExprTest, AddCarry) {
  auto a = llil_->Register(8, 0);
  auto b = llil_->Const(8, 1);
  auto c = llil_->Flag(0);
  auto j = Serialize(llil_->AddCarry(8, a, b, c));
  EXPECT_EQ(j["operation"], "ADC");
}

TEST_F(LlilExprTest, Sub) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->Sub(8, a, b));
  EXPECT_EQ(j["operation"], "SUB");
}

TEST_F(LlilExprTest, SubBorrow) {
  auto a = llil_->Register(8, 0);
  auto b = llil_->Const(8, 1);
  auto c = llil_->Flag(0);
  auto j = Serialize(llil_->SubBorrow(8, a, b, c));
  EXPECT_EQ(j["operation"], "SBB");
}

TEST_F(LlilExprTest, Mult) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->Mult(8, a, b));
  EXPECT_EQ(j["operation"], "MUL");
}

TEST_F(LlilExprTest, MultDoublePrecUnsigned) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->MultDoublePrecUnsigned(8, a, b));
  EXPECT_EQ(j["operation"], "MULU_DP");
}

TEST_F(LlilExprTest, MultDoublePrecSigned) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->MultDoublePrecSigned(8, a, b));
  EXPECT_EQ(j["operation"], "MULS_DP");
}

TEST_F(LlilExprTest, DivUnsigned) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->DivUnsigned(8, a, b));
  EXPECT_EQ(j["operation"], "DIVU");
}

TEST_F(LlilExprTest, DivDoublePrecUnsigned) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->DivDoublePrecUnsigned(8, a, b));
  EXPECT_EQ(j["operation"], "DIVU_DP");
}

TEST_F(LlilExprTest, DivSigned) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->DivSigned(8, a, b));
  EXPECT_EQ(j["operation"], "DIVS");
}

TEST_F(LlilExprTest, DivDoublePrecSigned) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->DivDoublePrecSigned(8, a, b));
  EXPECT_EQ(j["operation"], "DIVS_DP");
}

TEST_F(LlilExprTest, ModUnsigned) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->ModUnsigned(8, a, b));
  EXPECT_EQ(j["operation"], "MODU");
}

TEST_F(LlilExprTest, ModDoublePrecUnsigned) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->ModDoublePrecUnsigned(8, a, b));
  EXPECT_EQ(j["operation"], "MODU_DP");
}

TEST_F(LlilExprTest, ModSigned) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->ModSigned(8, a, b));
  EXPECT_EQ(j["operation"], "MODS");
}

TEST_F(LlilExprTest, ModDoublePrecSigned) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->ModDoublePrecSigned(8, a, b));
  EXPECT_EQ(j["operation"], "MODS_DP");
}

// --- Unary arithmetic ---

TEST_F(LlilExprTest, Neg) {
  auto a = llil_->Register(8, 0);
  auto j = Serialize(llil_->Neg(8, a));
  EXPECT_EQ(j["operation"], "NEG");
  EXPECT_EQ(j["operands"][0]["operation"], "REG");
}

TEST_F(LlilExprTest, Not) {
  auto a = llil_->Register(8, 0);
  auto j = Serialize(llil_->Not(8, a));
  EXPECT_EQ(j["operation"], "NOT");
}

// --- Bitwise ---

TEST_F(LlilExprTest, And) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->And(8, a, b));
  EXPECT_EQ(j["operation"], "AND");
}

TEST_F(LlilExprTest, Or) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->Or(8, a, b));
  EXPECT_EQ(j["operation"], "OR");
}

TEST_F(LlilExprTest, Xor) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->Xor(8, a, b));
  EXPECT_EQ(j["operation"], "XOR");
}

// --- Shifts ---

TEST_F(LlilExprTest, ShiftLeft) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->ShiftLeft(8, a, b));
  EXPECT_EQ(j["operation"], "LSL");
}

TEST_F(LlilExprTest, LogicalShiftRight) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->LogicalShiftRight(8, a, b));
  EXPECT_EQ(j["operation"], "LSR");
}

TEST_F(LlilExprTest, ArithShiftRight) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->ArithShiftRight(8, a, b));
  EXPECT_EQ(j["operation"], "ASR");
}

// --- Rotates ---

TEST_F(LlilExprTest, RotateLeft) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->RotateLeft(8, a, b));
  EXPECT_EQ(j["operation"], "ROL");
}

TEST_F(LlilExprTest, RotateLeftCarry) {
  auto a = llil_->Register(8, 0);
  auto b = llil_->Const(8, 1);
  auto c = llil_->Flag(0);
  auto j = Serialize(llil_->RotateLeftCarry(8, a, b, c));
  EXPECT_EQ(j["operation"], "RLC");
}

TEST_F(LlilExprTest, RotateRight) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->RotateRight(8, a, b));
  EXPECT_EQ(j["operation"], "ROR");
}

TEST_F(LlilExprTest, RotateRightCarry) {
  auto a = llil_->Register(8, 0);
  auto b = llil_->Const(8, 1);
  auto c = llil_->Flag(0);
  auto j = Serialize(llil_->RotateRightCarry(8, a, b, c));
  EXPECT_EQ(j["operation"], "RRC");
}

// --- Extension / Truncation ---

TEST_F(LlilExprTest, SignExtend) {
  auto a = llil_->Register(4, 0);
  auto j = Serialize(llil_->SignExtend(8, a));
  EXPECT_EQ(j["operation"], "SX");
  EXPECT_EQ(j["operands"][0]["operation"], "REG");
}

TEST_F(LlilExprTest, ZeroExtend) {
  auto a = llil_->Register(4, 0);
  auto j = Serialize(llil_->ZeroExtend(8, a));
  EXPECT_EQ(j["operation"], "ZX");
}

TEST_F(LlilExprTest, LowPart) {
  auto a = llil_->Register(8, 0);
  auto j = Serialize(llil_->LowPart(4, a));
  EXPECT_EQ(j["operation"], "LOW_PART");
}

// --- Comparisons ---

TEST_F(LlilExprTest, CompareEqual) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->CompareEqual(8, a, b));
  EXPECT_EQ(j["operation"], "CMP_E");
}

TEST_F(LlilExprTest, CompareNotEqual) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->CompareNotEqual(8, a, b));
  EXPECT_EQ(j["operation"], "CMP_NE");
}

TEST_F(LlilExprTest, CompareSignedLessThan) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->CompareSignedLessThan(8, a, b));
  EXPECT_EQ(j["operation"], "CMP_SLT");
}

TEST_F(LlilExprTest, CompareUnsignedLessThan) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->CompareUnsignedLessThan(8, a, b));
  EXPECT_EQ(j["operation"], "CMP_ULT");
}

TEST_F(LlilExprTest, CompareSignedLessEqual) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->CompareSignedLessEqual(8, a, b));
  EXPECT_EQ(j["operation"], "CMP_SLE");
}

TEST_F(LlilExprTest, CompareUnsignedLessEqual) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->CompareUnsignedLessEqual(8, a, b));
  EXPECT_EQ(j["operation"], "CMP_ULE");
}

TEST_F(LlilExprTest, CompareSignedGreaterEqual) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->CompareSignedGreaterEqual(8, a, b));
  EXPECT_EQ(j["operation"], "CMP_SGE");
}

TEST_F(LlilExprTest, CompareUnsignedGreaterEqual) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->CompareUnsignedGreaterEqual(8, a, b));
  EXPECT_EQ(j["operation"], "CMP_UGE");
}

TEST_F(LlilExprTest, CompareSignedGreaterThan) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->CompareSignedGreaterThan(8, a, b));
  EXPECT_EQ(j["operation"], "CMP_SGT");
}

TEST_F(LlilExprTest, CompareUnsignedGreaterThan) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->CompareUnsignedGreaterThan(8, a, b));
  EXPECT_EQ(j["operation"], "CMP_UGT");
}

// --- Special ---

TEST_F(LlilExprTest, TestBit) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->TestBit(8, a, b));
  EXPECT_EQ(j["operation"], "TEST_BIT");
}

TEST_F(LlilExprTest, BoolToInt) {
  auto a = llil_->CompareEqual(8, llil_->Register(8, 0), llil_->Const(8, 0));
  auto j = Serialize(llil_->BoolToInt(8, a));
  EXPECT_EQ(j["operation"], "BOOL_TO_INT");
  EXPECT_EQ(j["operands"][0]["operation"], "CMP_E");
}

TEST_F(LlilExprTest, AddOverflow) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->AddOverflow(8, a, b));
  EXPECT_EQ(j["operation"], "ADD_OVERFLOW");
}

// --- Control Flow ---

TEST_F(LlilExprTest, Jump) {
  auto dest = llil_->ConstPointer(8, 0x1000);
  auto j = Serialize(llil_->Jump(dest));
  EXPECT_EQ(j["operation"], "JUMP");
  EXPECT_EQ(j["operands"][0]["operation"], "CONST_PTR");
}

TEST_F(LlilExprTest, Call) {
  auto dest = llil_->ConstPointer(8, 0x2000);
  auto j = Serialize(llil_->Call(dest));
  EXPECT_EQ(j["operation"], "CALL");
  EXPECT_EQ(j["operands"][0]["operation"], "CONST_PTR");
}

TEST_F(LlilExprTest, TailCall) {
  auto dest = llil_->ConstPointer(8, 0x3000);
  auto j = Serialize(llil_->TailCall(dest));
  EXPECT_EQ(j["operation"], "TAILCALL");
}

TEST_F(LlilExprTest, Return) {
  auto dest = llil_->Register(8, 0);
  auto j = Serialize(llil_->Return(dest));
  EXPECT_EQ(j["operation"], "RET");
}

TEST_F(LlilExprTest, NoReturn) {
  auto j = Serialize(llil_->NoReturn());
  EXPECT_EQ(j["operation"], "NORET");
}

TEST_F(LlilExprTest, SystemCall) {
  auto j = Serialize(llil_->SystemCall());
  EXPECT_EQ(j["operation"], "SYSCALL");
}

TEST_F(LlilExprTest, Nop) {
  auto j = Serialize(llil_->Nop());
  EXPECT_EQ(j["operation"], "NOP");
}

// --- Trap / Breakpoint ---

TEST_F(LlilExprTest, Breakpoint) {
  auto j = Serialize(llil_->Breakpoint());
  EXPECT_EQ(j["operation"], "BP");
}

TEST_F(LlilExprTest, Trap) {
  auto j = Serialize(llil_->Trap(3));
  EXPECT_EQ(j["operation"], "TRAP");
}

// --- Undefined / Unimplemented ---

TEST_F(LlilExprTest, Undefined) {
  auto j = Serialize(llil_->Undefined());
  EXPECT_EQ(j["operation"], "UNDEF");
}

TEST_F(LlilExprTest, Unimplemented) {
  auto j = Serialize(llil_->Unimplemented());
  EXPECT_EQ(j["operation"], "UNIMPL");
}

TEST_F(LlilExprTest, UnimplementedMemoryRef) {
  auto addr = llil_->ConstPointer(8, 0x5000);
  auto j = Serialize(llil_->UnimplementedMemoryRef(8, addr));
  EXPECT_EQ(j["operation"], "UNIMPL_MEM");
  EXPECT_EQ(j["operands"][0]["operation"], "CONST_PTR");
}

// --- Floating point arithmetic ---

TEST_F(LlilExprTest, FloatAdd) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->FloatAdd(8, a, b));
  EXPECT_EQ(j["operation"], "FADD");
}

TEST_F(LlilExprTest, FloatSub) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->FloatSub(8, a, b));
  EXPECT_EQ(j["operation"], "FSUB");
}

TEST_F(LlilExprTest, FloatMult) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->FloatMult(8, a, b));
  EXPECT_EQ(j["operation"], "FMUL");
}

TEST_F(LlilExprTest, FloatDiv) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->FloatDiv(8, a, b));
  EXPECT_EQ(j["operation"], "FDIV");
}

TEST_F(LlilExprTest, FloatSqrt) {
  auto a = llil_->Register(8, 0);
  auto j = Serialize(llil_->FloatSqrt(8, a));
  EXPECT_EQ(j["operation"], "FSQRT");
}

TEST_F(LlilExprTest, FloatNeg) {
  auto a = llil_->Register(8, 0);
  auto j = Serialize(llil_->FloatNeg(8, a));
  EXPECT_EQ(j["operation"], "FNEG");
}

TEST_F(LlilExprTest, FloatAbs) {
  auto a = llil_->Register(8, 0);
  auto j = Serialize(llil_->FloatAbs(8, a));
  EXPECT_EQ(j["operation"], "FABS");
}

// --- Floating point conversions ---

TEST_F(LlilExprTest, FloatToInt) {
  auto a = llil_->Register(8, 0);
  auto j = Serialize(llil_->FloatToInt(8, a));
  EXPECT_EQ(j["operation"], "FLOAT_TO_INT");
}

TEST_F(LlilExprTest, IntToFloat) {
  auto a = llil_->Register(8, 0);
  auto j = Serialize(llil_->IntToFloat(8, a));
  EXPECT_EQ(j["operation"], "INT_TO_FLOAT");
}

TEST_F(LlilExprTest, FloatConvert) {
  auto a = llil_->Register(4, 0);
  auto j = Serialize(llil_->FloatConvert(8, a));
  EXPECT_EQ(j["operation"], "FLOAT_CONV");
}

// --- Floating point rounding ---

TEST_F(LlilExprTest, RoundToInt) {
  auto a = llil_->Register(8, 0);
  auto j = Serialize(llil_->RoundToInt(8, a));
  EXPECT_EQ(j["operation"], "ROUND_TO_INT");
}

TEST_F(LlilExprTest, Floor) {
  auto a = llil_->Register(8, 0);
  auto j = Serialize(llil_->Floor(8, a));
  EXPECT_EQ(j["operation"], "FLOOR");
}

TEST_F(LlilExprTest, Ceil) {
  auto a = llil_->Register(8, 0);
  auto j = Serialize(llil_->Ceil(8, a));
  EXPECT_EQ(j["operation"], "CEIL");
}

TEST_F(LlilExprTest, FloatTrunc) {
  auto a = llil_->Register(8, 0);
  auto j = Serialize(llil_->FloatTrunc(8, a));
  EXPECT_EQ(j["operation"], "FTRUNC");
}

// --- Floating point comparisons ---

TEST_F(LlilExprTest, FloatCompareEqual) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->FloatCompareEqual(8, a, b));
  EXPECT_EQ(j["operation"], "FCMP_E");
}

TEST_F(LlilExprTest, FloatCompareNotEqual) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->FloatCompareNotEqual(8, a, b));
  EXPECT_EQ(j["operation"], "FCMP_NE");
}

TEST_F(LlilExprTest, FloatCompareLessThan) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->FloatCompareLessThan(8, a, b));
  EXPECT_EQ(j["operation"], "FCMP_LT");
}

TEST_F(LlilExprTest, FloatCompareLessEqual) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->FloatCompareLessEqual(8, a, b));
  EXPECT_EQ(j["operation"], "FCMP_LE");
}

TEST_F(LlilExprTest, FloatCompareGreaterEqual) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->FloatCompareGreaterEqual(8, a, b));
  EXPECT_EQ(j["operation"], "FCMP_GE");
}

TEST_F(LlilExprTest, FloatCompareGreaterThan) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->FloatCompareGreaterThan(8, a, b));
  EXPECT_EQ(j["operation"], "FCMP_GT");
}

TEST_F(LlilExprTest, FloatCompareOrdered) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->FloatCompareOrdered(8, a, b));
  EXPECT_EQ(j["operation"], "FCMP_O");
}

TEST_F(LlilExprTest, FloatCompareUnordered) {
  auto [a, b] = MakeBinOpArgs();
  auto j = Serialize(llil_->FloatCompareUnordered(8, a, b));
  EXPECT_EQ(j["operation"], "FCMP_UO");
}

// --- Nested expression trees (regression tests) ---

TEST_F(LlilExprTest, SetRegLoad) {
  // SET_REG(rax, LOAD(CONST_PTR(0x1000)))
  auto addr = llil_->ConstPointer(8, 0x1000);
  auto load = llil_->Load(8, addr);
  auto j = Serialize(llil_->SetRegister(8, 0, load));
  EXPECT_EQ(j["operation"], "SET_REG");
  EXPECT_EQ(j["operands"][1]["operation"], "LOAD");
  EXPECT_EQ(j["operands"][1]["operands"][0]["operation"], "CONST_PTR");
}

TEST_F(LlilExprTest, DoubleIndirection) {
  // SET_REG(rax, LOAD(LOAD(REG(rbp))))
  auto rbp = llil_->Register(8, 5);
  auto inner = llil_->Load(8, rbp);
  auto outer = llil_->Load(8, inner);
  auto j = Serialize(llil_->SetRegister(8, 0, outer));
  EXPECT_EQ(j["operands"][1]["operation"], "LOAD");
  EXPECT_EQ(j["operands"][1]["operands"][0]["operation"], "LOAD");
  EXPECT_EQ(j["operands"][1]["operands"][0]["operands"][0]["operation"], "REG");
}

TEST_F(LlilExprTest, StoreAddRegConst) {
  // STORE([rbp - 8], rdi)
  auto rbp = llil_->Register(8, 5);
  auto offset = llil_->Const(8, 8);
  auto addr = llil_->Sub(8, rbp, offset);
  auto val = llil_->Register(8, 7);
  auto j = Serialize(llil_->Store(8, addr, val));
  EXPECT_EQ(j["operation"], "STORE");
  EXPECT_EQ(j["operands"][0]["operation"], "SUB");
  EXPECT_EQ(j["operands"][0]["operands"][0]["operation"], "REG");
  EXPECT_EQ(j["operands"][0]["operands"][1]["operation"], "CONST");
  EXPECT_EQ(j["operands"][1]["operation"], "REG");
}

}  // namespace
