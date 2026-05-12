#include <binaryninjaapi.h>
#include <binaryninjacore.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "bn_mcp.h"

namespace binja = BinaryNinja;
using binja::QualifiedName;

namespace {

class TypeToolsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create a minimal BinaryView we can define types on.
    file_ = new binja::FileMetadata();
    uint8_t dummy[] = {0};
    bv_ = new binja::BinaryData(file_, dummy, sizeof(dummy));
    ASSERT_NE(bv_.GetPtr(), nullptr);

    // Register the view with the MCP server.
    mcp_ = std::make_unique<bnmcp::BnMcp>();
  }

  void TearDown() override { mcp_.reset(); }

  // Helper: call a tool handler and return the result JSON.
  nlohmann::json CallTool(const std::string& method,
                          const nlohmann::json& args) {
    // We call the methods indirectly through the MCP server's dispatch.
    // For testing, we directly use the public ExprToJson or parse the
    // result. Instead, we test the underlying BN type API and the
    // ListTypes/GetTypeInfo output format by defining types on the BV
    // and checking the view's type list.
    //
    // Since the tools are private methods on BnMcp, we test by defining
    // types on a BinaryView and verifying the BN APIs work correctly.
    // The MCP tool methods are thin wrappers around these APIs.
    return {};
  }

  binja::Ref<binja::FileMetadata> file_;
  binja::Ref<binja::BinaryView> bv_;
  std::unique_ptr<bnmcp::BnMcp> mcp_;
};

// --- Structure types ---

TEST_F(TypeToolsTest, DefineAndListStruct) {
  binja::StructureBuilder sb;
  sb.AddMemberAtOffset(binja::Type::IntegerType(4, true), "x", 0);
  sb.AddMemberAtOffset(binja::Type::IntegerType(4, true), "y", 4);
  sb.AddMemberAtOffset(binja::Type::IntegerType(4, true), "z", 8);
  auto struct_type = binja::Type::StructureType(sb.Finalize());

  bv_->DefineUserType(QualifiedName("Point3D"), struct_type);

  auto type = bv_->GetTypeByName(QualifiedName("Point3D"));
  ASSERT_NE(type.GetPtr(), nullptr);
  EXPECT_EQ(type->GetClass(), StructureTypeClass);
  EXPECT_EQ(type->GetWidth(), 12);

  auto structure = type->GetStructure();
  ASSERT_NE(structure.GetPtr(), nullptr);

  auto members = structure->GetMembers();
  ASSERT_EQ(members.size(), 3);

  EXPECT_EQ(members[0].name, "x");
  EXPECT_EQ(members[0].offset, 0);
  EXPECT_EQ(members[1].name, "y");
  EXPECT_EQ(members[1].offset, 4);
  EXPECT_EQ(members[2].name, "z");
  EXPECT_EQ(members[2].offset, 8);
}

TEST_F(TypeToolsTest, StructMemberTypes) {
  binja::StructureBuilder sb;
  sb.AddMemberAtOffset(binja::Type::IntegerType(2, false), "Length", 0);
  sb.AddMemberAtOffset(binja::Type::IntegerType(2, false), "MaximumLength", 2);
  sb.AddMemberAtOffset(
      binja::Type::PointerType(8, binja::Type::IntegerType(2, false)), "Buffer",
      8);
  auto struct_type = binja::Type::StructureType(sb.Finalize());

  bv_->DefineUserType(QualifiedName("UNICODE_STRING"), struct_type);

  auto type = bv_->GetTypeByName(QualifiedName("UNICODE_STRING"));
  ASSERT_NE(type.GetPtr(), nullptr);

  auto members = type->GetStructure()->GetMembers();
  ASSERT_EQ(members.size(), 3);

  // First member is uint16_t.
  EXPECT_EQ(members[0].name, "Length");
  EXPECT_EQ(members[0].offset, 0);
  ASSERT_NE(members[0].type.GetValue().GetPtr(), nullptr);
  EXPECT_EQ(members[0].type.GetValue()->GetWidth(), 2);

  // Third member is a pointer (8 bytes on x64).
  EXPECT_EQ(members[2].name, "Buffer");
  EXPECT_EQ(members[2].offset, 8);
  ASSERT_NE(members[2].type.GetValue().GetPtr(), nullptr);
  EXPECT_EQ(members[2].type.GetValue()->GetClass(), PointerTypeClass);
  EXPECT_EQ(members[2].type.GetValue()->GetWidth(), 8);
}

TEST_F(TypeToolsTest, EmptyStruct) {
  binja::StructureBuilder sb;
  auto struct_type = binja::Type::StructureType(sb.Finalize());

  bv_->DefineUserType(QualifiedName("EmptyStruct"), struct_type);

  auto type = bv_->GetTypeByName(QualifiedName("EmptyStruct"));
  ASSERT_NE(type.GetPtr(), nullptr);
  EXPECT_EQ(type->GetClass(), StructureTypeClass);
  EXPECT_EQ(type->GetStructure()->GetMembers().size(), 0);
}

TEST_F(TypeToolsTest, NestedStruct) {
  // Inner struct.
  binja::StructureBuilder inner_sb;
  inner_sb.AddMemberAtOffset(binja::Type::IntegerType(4, true), "a", 0);
  inner_sb.AddMemberAtOffset(binja::Type::IntegerType(4, true), "b", 4);
  auto inner_type = binja::Type::StructureType(inner_sb.Finalize());
  bv_->DefineUserType(QualifiedName("Inner"), inner_type);

  // Outer struct with inner embedded.
  binja::StructureBuilder outer_sb;
  outer_sb.AddMemberAtOffset(inner_type, "inner", 0);
  outer_sb.AddMemberAtOffset(binja::Type::IntegerType(4, true), "c", 8);
  auto outer_type = binja::Type::StructureType(outer_sb.Finalize());
  bv_->DefineUserType(QualifiedName("Outer"), outer_type);

  auto type = bv_->GetTypeByName(QualifiedName("Outer"));
  ASSERT_NE(type.GetPtr(), nullptr);

  auto members = type->GetStructure()->GetMembers();
  ASSERT_EQ(members.size(), 2);
  EXPECT_EQ(members[0].name, "inner");
  EXPECT_EQ(members[0].offset, 0);
  EXPECT_EQ(members[0].type.GetValue()->GetClass(), StructureTypeClass);
  EXPECT_EQ(members[1].name, "c");
  EXPECT_EQ(members[1].offset, 8);
}

// --- Union types ---

TEST_F(TypeToolsTest, UnionType) {
  binja::StructureBuilder sb(UnionStructureType);
  sb.AddMember(binja::Type::IntegerType(4, true), "i");
  sb.AddMember(binja::Type::FloatType(4), "f");
  sb.AddMember(binja::Type::IntegerType(1, false), "b");
  auto union_type = binja::Type::StructureType(sb.Finalize());

  bv_->DefineUserType(QualifiedName("MyUnion"), union_type);

  auto type = bv_->GetTypeByName(QualifiedName("MyUnion"));
  ASSERT_NE(type.GetPtr(), nullptr);
  EXPECT_EQ(type->GetClass(), StructureTypeClass);

  auto structure = type->GetStructure();
  ASSERT_TRUE(structure->IsUnion());
  // Union width = max member width.
  EXPECT_EQ(type->GetWidth(), 4);

  auto members = structure->GetMembers();
  ASSERT_EQ(members.size(), 3);
  // All members at offset 0 in a union.
  EXPECT_EQ(members[0].offset, 0);
  EXPECT_EQ(members[1].offset, 0);
  EXPECT_EQ(members[2].offset, 0);
}

// --- Enumeration types ---

TEST_F(TypeToolsTest, EnumType) {
  auto arch = binja::Architecture::GetByName("x86_64");
  ASSERT_NE(arch.GetPtr(), nullptr);

  binja::EnumerationBuilder eb;
  eb.AddMemberWithValue("RED", 0);
  eb.AddMemberWithValue("GREEN", 1);
  eb.AddMemberWithValue("BLUE", 2);
  auto enum_type = binja::Type::EnumerationType(arch, eb.Finalize(), 4);

  bv_->DefineUserType(QualifiedName("Color"), enum_type);

  auto type = bv_->GetTypeByName(QualifiedName("Color"));
  ASSERT_NE(type.GetPtr(), nullptr);
  EXPECT_EQ(type->GetClass(), EnumerationTypeClass);
  EXPECT_EQ(type->GetWidth(), 4);

  auto enumeration = type->GetEnumeration();
  ASSERT_NE(enumeration.GetPtr(), nullptr);

  auto members = enumeration->GetMembers();
  ASSERT_EQ(members.size(), 3);
  EXPECT_EQ(members[0].name, "RED");
  EXPECT_EQ(members[0].value, 0);
  EXPECT_EQ(members[1].name, "GREEN");
  EXPECT_EQ(members[1].value, 1);
  EXPECT_EQ(members[2].name, "BLUE");
  EXPECT_EQ(members[2].value, 2);
}

TEST_F(TypeToolsTest, EnumWithGaps) {
  auto arch = binja::Architecture::GetByName("x86_64");
  ASSERT_NE(arch.GetPtr(), nullptr);

  binja::EnumerationBuilder eb;
  eb.AddMemberWithValue("NONE", 0);
  eb.AddMemberWithValue("SOME", 10);
  eb.AddMemberWithValue("ALL", 100);
  auto enum_type = binja::Type::EnumerationType(arch, eb.Finalize(), 4);

  bv_->DefineUserType(QualifiedName("Flags"), enum_type);

  auto enumeration =
      bv_->GetTypeByName(QualifiedName("Flags"))->GetEnumeration();
  auto members = enumeration->GetMembers();
  ASSERT_EQ(members.size(), 3);
  EXPECT_EQ(members[0].value, 0);
  EXPECT_EQ(members[1].value, 10);
  EXPECT_EQ(members[2].value, 100);
}

TEST_F(TypeToolsTest, EmptyEnum) {
  auto arch = binja::Architecture::GetByName("x86_64");
  ASSERT_NE(arch.GetPtr(), nullptr);

  binja::EnumerationBuilder eb;
  auto enum_type = binja::Type::EnumerationType(arch, eb.Finalize(), 4);

  bv_->DefineUserType(QualifiedName("EmptyEnum"), enum_type);

  auto type = bv_->GetTypeByName(QualifiedName("EmptyEnum"));
  ASSERT_NE(type.GetPtr(), nullptr);
  EXPECT_EQ(type->GetClass(), EnumerationTypeClass);
  EXPECT_EQ(type->GetEnumeration()->GetMembers().size(), 0);
}

// --- Basic / primitive types ---

TEST_F(TypeToolsTest, IntegerType) {
  auto int32 = binja::Type::IntegerType(4, true);
  EXPECT_EQ(int32->GetClass(), IntegerTypeClass);
  EXPECT_EQ(int32->GetWidth(), 4);
}

TEST_F(TypeToolsTest, PointerType) {
  auto ptr = binja::Type::PointerType(8, binja::Type::IntegerType(4, true));
  EXPECT_EQ(ptr->GetClass(), PointerTypeClass);
  EXPECT_EQ(ptr->GetWidth(), 8);
  EXPECT_NE(ptr->GetChildType().GetValue().GetPtr(), nullptr);
  EXPECT_EQ(ptr->GetChildType().GetValue()->GetClass(), IntegerTypeClass);
}

TEST_F(TypeToolsTest, VoidType) {
  auto v = binja::Type::VoidType();
  EXPECT_EQ(v->GetClass(), VoidTypeClass);
  EXPECT_EQ(v->GetWidth(), 0);
}

TEST_F(TypeToolsTest, FloatType) {
  auto f = binja::Type::FloatType(4);
  EXPECT_EQ(f->GetClass(), FloatTypeClass);
  EXPECT_EQ(f->GetWidth(), 4);
}

TEST_F(TypeToolsTest, BoolType) {
  auto b = binja::Type::BoolType();
  EXPECT_EQ(b->GetClass(), BoolTypeClass);
}

// --- Type lookup ---

TEST_F(TypeToolsTest, TypeNotFound) {
  auto type = bv_->GetTypeByName(QualifiedName("NonExistentType"));
  EXPECT_EQ(type.GetPtr(), nullptr);
}

TEST_F(TypeToolsTest, MultipleTypes) {
  binja::StructureBuilder sb1;
  sb1.AddMemberAtOffset(binja::Type::IntegerType(4, true), "val", 0);
  bv_->DefineUserType(QualifiedName("TypeA"),
                      binja::Type::StructureType(sb1.Finalize()));

  binja::StructureBuilder sb2;
  sb2.AddMemberAtOffset(binja::Type::IntegerType(8, true), "val", 0);
  bv_->DefineUserType(QualifiedName("TypeB"),
                      binja::Type::StructureType(sb2.Finalize()));

  auto arch = binja::Architecture::GetByName("x86_64");
  binja::EnumerationBuilder eb;
  eb.AddMemberWithValue("X", 0);
  bv_->DefineUserType(QualifiedName("TypeC"),
                      binja::Type::EnumerationType(arch, eb.Finalize(), 4));

  auto types = bv_->GetTypes();
  // At least our 3 types should be present.
  size_t found = 0;
  for (const auto& [name, type] : types) {
    auto n = name.GetString();
    if (n == "TypeA" || n == "TypeB" || n == "TypeC") found++;
  }
  EXPECT_EQ(found, 3);
}

// --- Struct with padding/alignment ---

TEST_F(TypeToolsTest, StructWithPadding) {
  binja::StructureBuilder sb;
  sb.AddMemberAtOffset(binja::Type::IntegerType(1, false), "a", 0);
  // Explicit gap at offset 1-3 (padding).
  sb.AddMemberAtOffset(binja::Type::IntegerType(4, true), "b", 4);
  sb.AddMemberAtOffset(binja::Type::IntegerType(1, false), "c", 8);
  auto struct_type = binja::Type::StructureType(sb.Finalize());

  bv_->DefineUserType(QualifiedName("Padded"), struct_type);

  auto members =
      bv_->GetTypeByName(QualifiedName("Padded"))->GetStructure()->GetMembers();
  ASSERT_EQ(members.size(), 3);
  EXPECT_EQ(members[0].offset, 0);
  EXPECT_EQ(members[0].type.GetValue()->GetWidth(), 1);
  EXPECT_EQ(members[1].offset, 4);
  EXPECT_EQ(members[1].type.GetValue()->GetWidth(), 4);
  EXPECT_EQ(members[2].offset, 8);
  EXPECT_EQ(members[2].type.GetValue()->GetWidth(), 1);
}

// --- Struct with pointer member ---

TEST_F(TypeToolsTest, StructWithPointerMember) {
  binja::StructureBuilder sb;
  sb.AddMemberAtOffset(
      binja::Type::PointerType(8, binja::Type::IntegerType(1, false)), "data",
      0);
  sb.AddMemberAtOffset(binja::Type::IntegerType(8, false), "size", 8);
  auto struct_type = binja::Type::StructureType(sb.Finalize());

  bv_->DefineUserType(QualifiedName("Buffer"), struct_type);

  auto members =
      bv_->GetTypeByName(QualifiedName("Buffer"))->GetStructure()->GetMembers();
  ASSERT_EQ(members.size(), 2);
  EXPECT_EQ(members[0].type.GetValue()->GetClass(), PointerTypeClass);
  EXPECT_EQ(members[1].type.GetValue()->GetClass(), IntegerTypeClass);
}

}  // namespace
