// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/compiler-unittests/instruction-selector-unittest.h"

namespace v8 {
namespace internal {
namespace compiler {

class InstructionSelectorPPCTest : public InstructionSelectorTest {};


TARGET_TEST_F(InstructionSelectorPPCTest, Int32AddP) {
  StreamBuilder m(this, kMachineWord32, kMachineWord32, kMachineWord32);
  m.Return(m.Int32Add(m.Parameter(0), m.Parameter(1)));
  Stream s = m.Build();
  ASSERT_EQ(1U, s.size());
  EXPECT_EQ(kPPC_Add32, s[0]->arch_opcode());
  EXPECT_EQ(2U, s[0]->InputCount());
  EXPECT_EQ(1U, s[0]->OutputCount());
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
