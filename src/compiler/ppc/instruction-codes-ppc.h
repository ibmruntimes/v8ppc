// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_PPC_INSTRUCTION_CODES_PPC_H_
#define V8_COMPILER_PPC_INSTRUCTION_CODES_PPC_H_

namespace v8 {
namespace internal {
namespace compiler {

// PPC-specific opcodes that specify which assembly sequence to emit.
// Most opcodes specify a single instruction.
#define TARGET_ARCH_OPCODE_LIST(V) \
  V(PPC_And32)                     \
  V(PPC_And64)                     \
  V(PPC_Or32)                      \
  V(PPC_Or64)                      \
  V(PPC_Xor32)                     \
  V(PPC_Xor64)                     \
  V(PPC_Shl32)                     \
  V(PPC_Shl64)                     \
  V(PPC_Shr32)                     \
  V(PPC_Shr64)                     \
  V(PPC_Sar32)                     \
  V(PPC_Sar64)                     \
  V(PPC_Not32)                     \
  V(PPC_Not64)                     \
  V(PPC_Add32)                     \
  V(PPC_AddWithOverflow32)         \
  V(PPC_Add64)                     \
  V(PPC_AddFloat64)                \
  V(PPC_Sub32)                     \
  V(PPC_SubWithOverflow32)         \
  V(PPC_Sub64)                     \
  V(PPC_SubFloat64)                \
  V(PPC_Mul32)                     \
  V(PPC_Mul64)                     \
  V(PPC_MulFloat64)                \
  V(PPC_Div32)                     \
  V(PPC_Div64)                     \
  V(PPC_DivU32)                    \
  V(PPC_DivU64)                    \
  V(PPC_DivFloat64)                \
  V(PPC_Mod32)                     \
  V(PPC_Mod64)                     \
  V(PPC_ModU32)                    \
  V(PPC_ModU64)                    \
  V(PPC_ModFloat64)                \
  V(PPC_Neg32)                     \
  V(PPC_Neg64)                     \
  V(PPC_NegFloat64)                \
  V(PPC_Cmp32)                     \
  V(PPC_Cmp64)                     \
  V(PPC_CmpFloat64)                \
  V(PPC_Tst32)                     \
  V(PPC_Tst64)                     \
  V(PPC_CallCodeObject)            \
  V(PPC_CallJSFunction)            \
  V(PPC_CallAddress)               \
  V(PPC_Push)                      \
  V(PPC_Drop)                      \
  V(PPC_Int32ToInt64)              \
  V(PPC_Int64ToInt32)              \
  V(PPC_Int32ToFloat64)            \
  V(PPC_Uint32ToFloat64)           \
  V(PPC_Float64ToInt32)            \
  V(PPC_Float64ToUint32)           \
  V(PPC_LoadWord8)                 \
  V(PPC_LoadWord16)                \
  V(PPC_LoadWord32)                \
  V(PPC_LoadWord64)                \
  V(PPC_LoadFloat64)               \
  V(PPC_StoreWord8)                \
  V(PPC_StoreWord16)               \
  V(PPC_StoreWord32)               \
  V(PPC_StoreWord64)               \
  V(PPC_StoreFloat64)              \
  V(PPC_StoreWriteBarrier)


// Addressing modes represent the "shape" of inputs to an instruction.
// Many instructions support multiple addressing modes. Addressing modes
// are encoded into the InstructionCode of the instruction and tell the
// code generator after register allocation which assembler method to call.
//
// We use the following local notation for addressing modes:
//
// R = register
// O = register or stack slot
// D = double register
// I = immediate (handle, external, int32)
// MRI = [register + immediate]
// MRR = [register + register]
#define TARGET_ADDRESSING_MODE_LIST(V) \
  V(MRI) /* [%r0 + K] */               \
  V(MRR) /* [%r0 + %r1] */

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_PPC_INSTRUCTION_CODES_PPC_H_
