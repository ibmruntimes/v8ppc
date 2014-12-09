// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/code-generator.h"

#include "src/compiler/code-generator-impl.h"
#include "src/compiler/gap-resolver.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties-inl.h"
#include "src/ppc/macro-assembler-ppc.h"
#include "src/scopes.h"

namespace v8 {
namespace internal {
namespace compiler {

#define __ masm()->


#define kScratchReg r11


// Adds PPC-specific methods to convert InstructionOperands.
class PPCOperandConverter FINAL : public InstructionOperandConverter {
 public:
  PPCOperandConverter(CodeGenerator* gen, Instruction* instr)
      : InstructionOperandConverter(gen, instr) {}

  RCBit OutputRCBit() const {
    switch (instr_->flags_mode()) {
      case kFlags_branch:
      case kFlags_set:
        return SetRC;
      case kFlags_none:
        return LeaveRC;
    }
    UNREACHABLE();
    return LeaveRC;
  }

  bool CompareLogical() const {
    switch (instr_->flags_condition()) {
      case kUnsignedLessThan:
      case kUnsignedGreaterThanOrEqual:
      case kUnsignedLessThanOrEqual:
      case kUnsignedGreaterThan:
        return true;
      default:
        return false;
    }
    UNREACHABLE();
    return false;
  }

  Operand InputImmediate(int index) {
    Constant constant = ToConstant(instr_->InputAt(index));
    switch (constant.type()) {
      case Constant::kInt32:
        return Operand(constant.ToInt32());
      case Constant::kFloat32:
        return Operand(
            isolate()->factory()->NewNumber(constant.ToFloat32(), TENURED));
      case Constant::kFloat64:
        return Operand(
            isolate()->factory()->NewNumber(constant.ToFloat64(), TENURED));
      case Constant::kInt64:
#if V8_TARGET_ARCH_PPC64
        return Operand(constant.ToInt64());
#endif
      case Constant::kExternalReference:
      case Constant::kHeapObject:
      case Constant::kRpoNumber:
        break;
    }
    UNREACHABLE();
    return Operand::Zero();
  }

  MemOperand MemoryOperand(AddressingMode* mode, int* first_index) {
    const int index = *first_index;
    *mode = AddressingModeField::decode(instr_->opcode());
    switch (*mode) {
      case kMode_None:
        break;
      case kMode_MRI:
        *first_index += 2;
        return MemOperand(InputRegister(index + 0), InputInt32(index + 1));
      case kMode_MRR:
        *first_index += 2;
        return MemOperand(InputRegister(index + 0), InputRegister(index + 1));
    }
    UNREACHABLE();
    return MemOperand(r0);
  }

  MemOperand MemoryOperand(AddressingMode* mode, int first_index = 0) {
    return MemoryOperand(mode, &first_index);
  }

  MemOperand ToMemOperand(InstructionOperand* op) const {
    DCHECK(op != NULL);
    DCHECK(!op->IsRegister());
    DCHECK(!op->IsDoubleRegister());
    DCHECK(op->IsStackSlot() || op->IsDoubleStackSlot());
    // The linkage computes where all spill slots are located.
    FrameOffset offset = linkage()->GetFrameOffset(op->index(), frame(), 0);
    return MemOperand(offset.from_stack_pointer() ? sp : fp, offset.offset());
  }
};


static inline bool HasRegisterInput(Instruction* instr, int index) {
  return instr->InputAt(index)->IsRegister();
}


namespace {

class OutOfLineLoadFloat32 FINAL : public OutOfLineCode {
 public:
  OutOfLineLoadFloat32(CodeGenerator* gen, DoubleRegister result)
      : OutOfLineCode(gen), result_(result) {}

  void Generate() FINAL {
    __ LoadDoubleLiteral(result_, std::numeric_limits<float>::quiet_NaN(),
                         kScratchReg);
  }

 private:
  DoubleRegister const result_;
};


class OutOfLineLoadFloat64 FINAL : public OutOfLineCode {
 public:
  OutOfLineLoadFloat64(CodeGenerator* gen, DoubleRegister result)
      : OutOfLineCode(gen), result_(result) {}

  void Generate() FINAL {
    __ LoadDoubleLiteral(result_, std::numeric_limits<double>::quiet_NaN(),
                         kScratchReg);
  }

 private:
  DoubleRegister const result_;
};


class OutOfLineLoadInteger FINAL : public OutOfLineCode {
 public:
  OutOfLineLoadInteger(CodeGenerator* gen, Register result)
      : OutOfLineCode(gen), result_(result) {}

  void Generate() FINAL { __ li(result_, Operand::Zero()); }

 private:
  Register const result_;
};

}  // namespace


#define ASSEMBLE_LOAD_FLOAT(asm_instr, asm_instrx)              \
  do {                                                          \
    DoubleRegister result = i.OutputDoubleRegister();           \
    AddressingMode mode = kMode_None;                           \
    MemOperand operand = i.MemoryOperand(&mode);                \
    if (mode == kMode_MRI) {                                    \
      __ asm_instr(result, operand);                            \
    } else {                                                    \
      DCHECK_EQ(kMode_MRR, mode);                               \
      __ asm_instrx(result, operand);                           \
    }                                                           \
    DCHECK_EQ(LeaveRC, i.OutputRCBit());                        \
  } while (0)


#define ASSEMBLE_LOAD_INTEGER(asm_instr, asm_instrx)            \
  do {                                                          \
    Register result = i.OutputRegister();                       \
    AddressingMode mode = kMode_None;                           \
    MemOperand operand = i.MemoryOperand(&mode);                \
    if (mode == kMode_MRI) {                                    \
      __ asm_instr(result, operand);                            \
    } else {                                                    \
      DCHECK_EQ(kMode_MRR, mode);                               \
      __ asm_instrx(result, operand);                           \
    }                                                           \
    DCHECK_EQ(LeaveRC, i.OutputRCBit());                        \
  } while (0)


#define ASSEMBLE_STORE_FLOAT(asm_instr, asm_instrx)             \
  do {                                                          \
    int index = 0;                                              \
    AddressingMode mode = kMode_None;                           \
    MemOperand operand = i.MemoryOperand(&mode, &index);        \
    DoubleRegister value = i.InputDoubleRegister(index);        \
    if (mode == kMode_MRI) {                                    \
      __ asm_instr(value, operand);                             \
    } else {                                                    \
      DCHECK_EQ(kMode_MRR, mode);                               \
      __ asm_instrx(value, operand);                            \
    }                                                           \
    DCHECK_EQ(LeaveRC, i.OutputRCBit());                        \
  } while (0)


#define ASSEMBLE_STORE_INTEGER(asm_instr, asm_instrx)           \
  do {                                                          \
    int index = 0;                                              \
    AddressingMode mode = kMode_None;                           \
    MemOperand operand = i.MemoryOperand(&mode, &index);        \
    Register value = i.InputRegister(index);                    \
    if (mode == kMode_MRI) {                                    \
      __ asm_instr(value, operand);                             \
    } else {                                                    \
      DCHECK_EQ(kMode_MRR, mode);                               \
      __ asm_instrx(value, operand);                            \
    }                                                           \
    DCHECK_EQ(LeaveRC, i.OutputRCBit());                        \
  } while (0)


#define ASSEMBLE_CHECKED_LOAD_FLOAT(asm_instr, asm_instrx, width)    \
  do {                                                               \
    auto result = i.OutputDoubleRegister();                          \
    auto offset = i.InputRegister(0);                                \
    if (HasRegisterInput(instr, 1)) {                                \
      __ cmpl(offset, i.InputRegister(1));                           \
    } else {                                                         \
      __ cmpli(offset, i.InputImmediate(1));                         \
    }                                                                \
    AddressingMode mode = kMode_None;                                \
    MemOperand operand = i.MemoryOperand(&mode, 2);                  \
    auto ool = new (zone()) OutOfLineLoadFloat##width(this, result); \
    __ bge(ool->entry());                                            \
    if (mode == kMode_MRI) {                                         \
      __ asm_instr(result, operand);                                 \
    } else {                                                         \
      DCHECK_EQ(kMode_MRR, mode);                                    \
      __ asm_instrx(result, operand);                                \
    }                                                                \
    __ bind(ool->exit());                                            \
    DCHECK_EQ(LeaveRC, i.OutputRCBit());                             \
  } while (0)


#define ASSEMBLE_CHECKED_LOAD_INTEGER(asm_instr, asm_instrx)    \
  do {                                                          \
    auto result = i.OutputRegister();                           \
    auto offset = i.InputRegister(0);                           \
    if (HasRegisterInput(instr, 1)) {                           \
      __ cmpl(offset, i.InputRegister(1));                      \
    } else {                                                    \
      __ cmpli(offset, i.InputImmediate(1));                    \
    }                                                           \
    AddressingMode mode = kMode_None;                           \
    MemOperand operand = i.MemoryOperand(&mode, 2);             \
    auto ool = new (zone()) OutOfLineLoadInteger(this, result); \
    __ bge(ool->entry());                                       \
    if (mode == kMode_MRI) {                                    \
      __ asm_instr(result, operand);                            \
    } else {                                                    \
      DCHECK_EQ(kMode_MRR, mode);                               \
      __ asm_instrx(result, operand);                           \
    }                                                           \
    __ bind(ool->exit());                                       \
    DCHECK_EQ(LeaveRC, i.OutputRCBit());                        \
  } while (0)


#define ASSEMBLE_CHECKED_STORE_FLOAT(asm_instr, asm_instrx)             \
  do {                                                                  \
    Label done;                                                         \
    auto offset = i.InputRegister(0);                                   \
    if (HasRegisterInput(instr, 1)) {                                   \
      __ cmpl(offset, i.InputRegister(1));                              \
    } else {                                                            \
      __ cmpli(offset, i.InputImmediate(1));                            \
    }                                                                   \
    __ bge(&done);                                                      \
    auto value = i.InputDoubleRegister(2);                              \
    AddressingMode mode = kMode_None;                                   \
    MemOperand operand = i.MemoryOperand(&mode, 3);                     \
    if (mode == kMode_MRI) {                                            \
      __ asm_instr(value, operand);                                     \
    } else {                                                            \
      DCHECK_EQ(kMode_MRR, mode);                                       \
      __ asm_instrx(value, operand);                                    \
    }                                                                   \
    __ bind(&done);                                                     \
    DCHECK_EQ(LeaveRC, i.OutputRCBit());                                \
  } while (0)


#define ASSEMBLE_CHECKED_STORE_INTEGER(asm_instr, asm_instrx)      \
  do {                                                             \
    Label done;                                                    \
    auto offset = i.InputRegister(0);                              \
    if (HasRegisterInput(instr, 1)) {                              \
      __ cmpl(offset, i.InputRegister(1));                         \
    } else {                                                       \
      __ cmpli(offset, i.InputImmediate(1));                       \
    }                                                              \
    __ bge(&done);                                                 \
    auto value = i.InputRegister(2);                               \
    AddressingMode mode = kMode_None;                              \
    MemOperand operand = i.MemoryOperand(&mode, 3);                \
    if (mode == kMode_MRI) {                                       \
      __ asm_instr(value, operand);                                \
    } else {                                                       \
      DCHECK_EQ(kMode_MRR, mode);                                  \
      __ asm_instrx(value, operand);                               \
    }                                                              \
    __ bind(&done);                                                \
    DCHECK_EQ(LeaveRC, i.OutputRCBit());                           \
  } while (0)


// Assembles an instruction after register allocation, producing machine code.
void CodeGenerator::AssembleArchInstruction(Instruction* instr) {
  PPCOperandConverter i(this, instr);
  ArchOpcode opcode = ArchOpcodeField::decode(instr->opcode());

  switch (opcode) {
    case kArchCallCodeObject: {
      EnsureSpaceForLazyDeopt();
      if (HasRegisterInput(instr, 0)) {
        __ addi(ip, i.InputRegister(0),
                Operand(Code::kHeaderSize - kHeapObjectTag));
        __ Call(ip);
      } else {
        __ Call(Handle<Code>::cast(i.InputHeapObject(0)),
                RelocInfo::CODE_TARGET);
      }
      AddSafepointAndDeopt(instr);
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    }
    case kArchCallJSFunction: {
      EnsureSpaceForLazyDeopt();
      Register func = i.InputRegister(0);
      if (FLAG_debug_code) {
        // Check the function's context matches the context argument.
        __ LoadP(kScratchReg, FieldMemOperand(func,
                                              JSFunction::kContextOffset));
        __ cmp(cp, kScratchReg);
        __ Assert(eq, kWrongFunctionContext);
      }
      __ LoadP(ip, FieldMemOperand(func, JSFunction::kCodeEntryOffset));
      __ Call(ip);
      AddSafepointAndDeopt(instr);
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    }
    case kArchJmp:
      AssembleArchJump(i.InputRpo(0));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kArchNop:
      // don't emit code for nops.
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kArchRet:
      AssembleReturn();
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kArchStackPointer:
      __ mr(i.OutputRegister(), sp);
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kArchTruncateDoubleToI:
      __ TruncateDoubleToI(i.OutputRegister(), i.InputDoubleRegister(0));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_And32:
    case kPPC_And64:
      if (HasRegisterInput(instr, 1)) {
        __ and_(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
                i.OutputRCBit());
      } else {
        __ andi(i.OutputRegister(), i.InputRegister(0), i.InputImmediate(1));
      }
      break;
    case kPPC_AndCompliment32:
    case kPPC_AndCompliment64:
      __ andc(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
              i.OutputRCBit());
      break;
    case kPPC_Or32:
    case kPPC_Or64:
      if (HasRegisterInput(instr, 1)) {
        __ orx(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
               i.OutputRCBit());
      } else {
        __ ori(i.OutputRegister(), i.InputRegister(0), i.InputImmediate(1));
        DCHECK_EQ(LeaveRC, i.OutputRCBit());
      }
      break;
    case kPPC_OrCompliment32:
    case kPPC_OrCompliment64:
      __ orc(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
             i.OutputRCBit());
      break;
    case kPPC_Xor32:
    case kPPC_Xor64:
      if (HasRegisterInput(instr, 1)) {
        __ xor_(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
                i.OutputRCBit());
      } else {
        __ xori(i.OutputRegister(), i.InputRegister(0), i.InputImmediate(1));
        DCHECK_EQ(LeaveRC, i.OutputRCBit());
      }
      break;
    case kPPC_ShiftLeft32:
      if (HasRegisterInput(instr, 1)) {
        __ slw(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
               i.OutputRCBit());
      } else {
        __ slwi(i.OutputRegister(), i.InputRegister(0), i.InputImmediate(1),
                i.OutputRCBit());
      }
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_ShiftLeft64:
      if (HasRegisterInput(instr, 1)) {
        __ sld(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
               i.OutputRCBit());
      } else {
        __ sldi(i.OutputRegister(), i.InputRegister(0), i.InputImmediate(1),
                i.OutputRCBit());
      }
      break;
#endif
    case kPPC_ShiftRight32:
      if (HasRegisterInput(instr, 1)) {
        __ srw(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
               i.OutputRCBit());
      } else {
        __ srwi(i.OutputRegister(), i.InputRegister(0), i.InputImmediate(1),
                i.OutputRCBit());
      }
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_ShiftRight64:
      if (HasRegisterInput(instr, 1)) {
        __ srd(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
               i.OutputRCBit());
      } else {
        __ srdi(i.OutputRegister(), i.InputRegister(0), i.InputImmediate(1),
                i.OutputRCBit());
      }
      break;
#endif
    case kPPC_ShiftRightAlg32:
      if (HasRegisterInput(instr, 1)) {
        __ sraw(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
               i.OutputRCBit());
      } else {
        int sh = i.InputInt32(1);
        __ srawi(i.OutputRegister(), i.InputRegister(0), sh, i.OutputRCBit());
      }
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_ShiftRightAlg64:
      if (HasRegisterInput(instr, 1)) {
        __ srad(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
               i.OutputRCBit());
      } else {
        int sh = i.InputInt32(1);
        __ sradi(i.OutputRegister(), i.InputRegister(0), sh, i.OutputRCBit());
      }
      break;
#endif
    case kPPC_RotRight32:
      if (HasRegisterInput(instr, 1)) {
        __ subfic(kScratchReg, i.InputRegister(1), Operand(32));
        __ rotlw(i.OutputRegister(), i.InputRegister(0), kScratchReg,
                 i.OutputRCBit());
      } else {
        int sh = i.InputInt32(1);
        __ rotrwi(i.OutputRegister(), i.InputRegister(0), sh, i.OutputRCBit());
      }
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_RotRight64:
      if (HasRegisterInput(instr, 1)) {
        __ subfic(kScratchReg, i.InputRegister(1), Operand(64));
        __ rotld(i.OutputRegister(), i.InputRegister(0), kScratchReg,
                 i.OutputRCBit());
      } else {
        int sh = i.InputInt32(1);
        __ rotrdi(i.OutputRegister(), i.InputRegister(0), sh, i.OutputRCBit());
      }
      break;
#endif
    case kPPC_Not32:
    case kPPC_Not64:
      __ notx(i.OutputRegister(), i.InputRegister(0), i.OutputRCBit());
      break;
    case kPPC_RotLeftAndMask32:
      __ rlwinm(i.OutputRegister(), i.InputRegister(0), i.InputInt32(1),
                31 - i.InputInt32(2), 31 - i.InputInt32(3),
                i.OutputRCBit());
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_RotLeftAndClear64:
      __ rldic(i.OutputRegister(), i.InputRegister(0), i.InputInt32(1),
               63 - i.InputInt32(2), i.OutputRCBit());
      break;
    case kPPC_RotLeftAndClearLeft64:
      __ rldicl(i.OutputRegister(), i.InputRegister(0), i.InputInt32(1),
                63 - i.InputInt32(2), i.OutputRCBit());
      break;
    case kPPC_RotLeftAndClearRight64:
      __ rldicr(i.OutputRegister(), i.InputRegister(0), i.InputInt32(1),
                63 - i.InputInt32(2), i.OutputRCBit());
      break;
#endif
    case kPPC_Add32:
    case kPPC_Add64:
      if (HasRegisterInput(instr, 1)) {
        __ add(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
               LeaveOE, i.OutputRCBit());
      } else {
        __ addi(i.OutputRegister(), i.InputRegister(0), i.InputImmediate(1));
        DCHECK_EQ(LeaveRC, i.OutputRCBit());
      }
      break;
    case kPPC_AddWithOverflow32: {
      Register output = i.OutputRegister();
      Register left = i.InputRegister(0);
      if (HasRegisterInput(instr, 1)) {
        Register right = i.InputRegister(1);
#if V8_TARGET_ARCH_PPC64
        __ add(output, left, right);
#else
        __ AddAndCheckForOverflow(output, left, right, kScratchReg, r0);
#endif
      } else {
#if V8_TARGET_ARCH_PPC64
        Operand right = i.InputImmediate(1);
        __ addi(output, left, right);
#else
        intptr_t right = i.InputInt32(1);
        __ AddAndCheckForOverflow(output, left, right, kScratchReg, r0);
#endif
      }
#if V8_TARGET_ARCH_PPC64
      __ TestIfInt32(output, r0, cr0);
#endif
      break;
    }
    case kPPC_AddFloat64:
      __ fadd(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
              i.InputDoubleRegister(1));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_Sub32:
    case kPPC_Sub64:
      if (HasRegisterInput(instr, 1)) {
        __ sub(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
               LeaveOE, i.OutputRCBit());
      } else {
        __ subi(i.OutputRegister(), i.InputRegister(0), i.InputImmediate(1));
        DCHECK_EQ(LeaveRC, i.OutputRCBit());
      }
      break;
    case kPPC_SubWithOverflow32: {
      Register output = i.OutputRegister();
      Register left = i.InputRegister(0);
      if (HasRegisterInput(instr, 1)) {
        Register right = i.InputRegister(1);
#if V8_TARGET_ARCH_PPC64
        __ sub(output, left, right);
#else
        __ SubAndCheckForOverflow(output, left, right, kScratchReg, r0);
#endif
      } else {
#if V8_TARGET_ARCH_PPC64
        Operand right = i.InputImmediate(1);
        __ subi(output, left, right);
#else
        intptr_t right = i.InputInt32(1);
        __ AddAndCheckForOverflow(output, left, -right, kScratchReg, r0);
#endif
      }
#if V8_TARGET_ARCH_PPC64
      __ TestIfInt32(output, r0, cr0);
#endif
      break;
    }
    case kPPC_SubFloat64:
      __ fsub(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
              i.InputDoubleRegister(1));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_Mul32:
      __ mullw(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
               LeaveOE, i.OutputRCBit());
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_Mul64:
      __ mulld(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
               LeaveOE, i.OutputRCBit());
      break;
#endif
    case kPPC_MulHigh32:
      __ mulhw(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
               i.OutputRCBit());
      break;
    case kPPC_MulHighU32:
      __ mulhwu(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
                i.OutputRCBit());
      break;
    case kPPC_MulFloat64:
      __ fmul(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
              i.InputDoubleRegister(1));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_Div32:
      __ divw(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_Div64:
      __ divd(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
#endif
    case kPPC_DivU32:
      __ divwu(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_DivU64:
      __ divdu(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
#endif
    case kPPC_DivFloat64:
      __ fdiv(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
              i.InputDoubleRegister(1));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_Mod32:
      __ divw(kScratchReg, i.InputRegister(0), i.InputRegister(1));
      __ mullw(kScratchReg, kScratchReg, i.InputRegister(1));
      __ sub(i.OutputRegister(), i.InputRegister(0), kScratchReg, LeaveOE,
             i.OutputRCBit());
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_Mod64:
      __ divd(kScratchReg, i.InputRegister(0), i.InputRegister(1));
      __ mulld(kScratchReg, kScratchReg, i.InputRegister(1));
      __ sub(i.OutputRegister(), i.InputRegister(0), kScratchReg, LeaveOE,
             i.OutputRCBit());
      break;
#endif
    case kPPC_ModU32:
      __ divwu(kScratchReg, i.InputRegister(0), i.InputRegister(1));
      __ mullw(kScratchReg, kScratchReg, i.InputRegister(1));
      __ sub(i.OutputRegister(), i.InputRegister(0), kScratchReg, LeaveOE,
             i.OutputRCBit());
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_ModU64:
      __ divd(kScratchReg, i.InputRegister(0), i.InputRegister(1));
      __ mulld(kScratchReg, kScratchReg, i.InputRegister(1));
      __ sub(i.OutputRegister(), i.InputRegister(0), kScratchReg, LeaveOE,
             i.OutputRCBit());
      break;
#endif
    case kPPC_ModFloat64: {
      // TODO(bmeurer): We should really get rid of this special instruction,
      // and generate a CallAddress instruction instead.
      FrameScope scope(masm(), StackFrame::MANUAL);
      __ PrepareCallCFunction(0, 2, kScratchReg);
      __ MovToFloatParameters(i.InputDoubleRegister(0),
                              i.InputDoubleRegister(1));
      __ CallCFunction(ExternalReference::mod_two_doubles_operation(isolate()),
                       0, 2);
      // Move the result in the double result register.
      __ MovFromFloatResult(i.OutputDoubleRegister());
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    }
    case kPPC_Neg32:
    case kPPC_Neg64:
      __ neg(i.OutputRegister(), i.InputRegister(0), LeaveOE, i.OutputRCBit());
      break;
    case kPPC_SqrtFloat64:
      __ fsqrt(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_FloorFloat64:
      __ frim(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_CeilFloat64:
      __ frip(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_TruncateFloat64:
      __ friz(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_RoundFloat64:
      __ frin(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_NegFloat64:
      __ fneg(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_Cmp32: {
      CRegister cr = cr0;
      if (HasRegisterInput(instr, 1)) {
        if (i.CompareLogical()) {
          __ cmplw(i.InputRegister(0), i.InputRegister(1), cr);
        } else {
          __ cmpw(i.InputRegister(0), i.InputRegister(1), cr);
        }
      } else {
        if (i.CompareLogical()) {
          __ cmplwi(i.InputRegister(0), i.InputImmediate(1), cr);
        } else {
          __ cmpwi(i.InputRegister(0), i.InputImmediate(1), cr);
        }
      }
      DCHECK_EQ(SetRC, i.OutputRCBit());
      break;
    }
#if V8_TARGET_ARCH_PPC64
    case kPPC_Cmp64: {
      CRegister cr = cr0;
      if (HasRegisterInput(instr, 1)) {
        if (i.CompareLogical()) {
          __ cmpl(i.InputRegister(0), i.InputRegister(1), cr);
        } else {
          __ cmp(i.InputRegister(0), i.InputRegister(1), cr);
        }
      } else {
        if (i.CompareLogical()) {
          __ cmpli(i.InputRegister(0), i.InputImmediate(1), cr);
        } else {
          __ cmpi(i.InputRegister(0), i.InputImmediate(1), cr);
        }
      }
      DCHECK_EQ(SetRC, i.OutputRCBit());
      break;
    }
#endif
    case kPPC_CmpFloat64: {
      CRegister cr = cr0;
      __ fcmpu(i.InputDoubleRegister(0), i.InputDoubleRegister(1), cr);
      DCHECK_EQ(SetRC, i.OutputRCBit());
      break;
    }
    case kPPC_Tst32:
      if (HasRegisterInput(instr, 1)) {
        __ and_(r0, i.InputRegister(0), i.InputRegister(1), i.OutputRCBit());
      } else {
        __ andi(r0, i.InputRegister(0), i.InputImmediate(1));
      }
#if V8_TARGET_ARCH_PPC64
      __ extsw(r0, r0, i.OutputRCBit());
#endif
      DCHECK_EQ(SetRC, i.OutputRCBit());
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_Tst64:
      if (HasRegisterInput(instr, 1)) {
        __ and_(r0, i.InputRegister(0), i.InputRegister(1), i.OutputRCBit());
      } else {
        __ andi(r0, i.InputRegister(0), i.InputImmediate(1));
      }
      DCHECK_EQ(SetRC, i.OutputRCBit());
      break;
#endif
    case kPPC_Push:
      __ Push(i.InputRegister(0));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_ExtendSignWord8:
      __ extsb(i.OutputRegister(), i.InputRegister(0));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_ExtendSignWord16:
      __ extsh(i.OutputRegister(), i.InputRegister(0));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_ExtendSignWord32:
      __ extsw(i.OutputRegister(), i.InputRegister(0));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_Uint32ToUint64:
      // TODO(mbrandy): zero extend?
      __ Move(i.OutputRegister(), i.InputRegister(0));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_Int64ToInt32:
      // TODO(mbrandy): sign extend?
      __ Move(i.OutputRegister(), i.InputRegister(0));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
#endif
    case kPPC_Int32ToFloat64:
      __ ConvertIntToDouble(i.InputRegister(0),
                            i.OutputDoubleRegister());
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_Uint32ToFloat64:
      __ ConvertUnsignedIntToDouble(i.InputRegister(0),
                                    i.OutputDoubleRegister());
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_Float64ToInt32:
    case kPPC_Float64ToUint32:
      __ ConvertDoubleToInt64(i.InputDoubleRegister(0),
#if !V8_TARGET_ARCH_PPC64
                              kScratchReg,
#endif
                              i.OutputRegister(), kScratchDoubleReg);
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_Float64ToFloat32:
      __ frsp(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
              i.OutputRCBit());
      break;
    case kPPC_Float32ToFloat64:
      // Nothing to do.
      __ Move(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_LoadWordU8:
      ASSEMBLE_LOAD_INTEGER(lbz, lbzx);
      break;
    case kPPC_LoadWordS8:
      ASSEMBLE_LOAD_INTEGER(lbz, lbzx);
      __ extsb(i.OutputRegister(), i.OutputRegister());
      break;
    case kPPC_LoadWordU16:
      ASSEMBLE_LOAD_INTEGER(lhz, lhzx);
      break;
    case kPPC_LoadWordS16:
      ASSEMBLE_LOAD_INTEGER(lha, lhax);
      break;
    case kPPC_LoadWordS32:
      ASSEMBLE_LOAD_INTEGER(lwa, lwax);
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_LoadWord64:
      ASSEMBLE_LOAD_INTEGER(ld, ldx);
      break;
#endif
    case kPPC_LoadFloat32:
      ASSEMBLE_LOAD_FLOAT(lfs, lfsx);
      break;
    case kPPC_LoadFloat64:
      ASSEMBLE_LOAD_FLOAT(lfd, lfdx);
      break;
    case kPPC_StoreWord8:
      ASSEMBLE_STORE_INTEGER(stb, stbx);
      break;
    case kPPC_StoreWord16:
      ASSEMBLE_STORE_INTEGER(sth, sthx);
      break;
    case kPPC_StoreWord32:
      ASSEMBLE_STORE_INTEGER(stw, stwx);
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_StoreWord64:
      ASSEMBLE_STORE_INTEGER(std, stdx);
      break;
#endif
    case kPPC_StoreFloat32:
      ASSEMBLE_STORE_FLOAT(stfs, stfsx);
      break;
    case kPPC_StoreFloat64:
      ASSEMBLE_STORE_FLOAT(stfd, stfdx);
      break;
    case kPPC_StoreWriteBarrier: {
      Register object = i.InputRegister(0);
      Register index = i.InputRegister(1);
      Register value = i.InputRegister(2);
      __ add(index, object, index);
      __ StoreP(value, MemOperand(index));
      SaveFPRegsMode mode =
          frame()->DidAllocateDoubleRegisters() ? kSaveFPRegs : kDontSaveFPRegs;
      LinkRegisterStatus lr_status = kLRHasNotBeenSaved;
      __ RecordWrite(object, index, value, lr_status, mode);
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    }
    case kCheckedLoadInt8:
      ASSEMBLE_CHECKED_LOAD_INTEGER(lbz, lbzx);
      __ extsb(i.OutputRegister(), i.OutputRegister());
      break;
    case kCheckedLoadUint8:
      ASSEMBLE_CHECKED_LOAD_INTEGER(lbz, lbzx);
      break;
    case kCheckedLoadInt16:
      ASSEMBLE_CHECKED_LOAD_INTEGER(lha, lhax);
      break;
    case kCheckedLoadUint16:
      ASSEMBLE_CHECKED_LOAD_INTEGER(lhz, lhzx);
      break;
    case kCheckedLoadWord32:
      ASSEMBLE_CHECKED_LOAD_INTEGER(lwa, lwax);
      break;
    case kCheckedLoadFloat32:
      ASSEMBLE_CHECKED_LOAD_FLOAT(lfs, lfsx, 32);
      break;
    case kCheckedLoadFloat64:
      ASSEMBLE_CHECKED_LOAD_FLOAT(lfd, lfdx, 64);
      break;
    case kCheckedStoreWord8:
      ASSEMBLE_CHECKED_STORE_INTEGER(stb, stbx);
      break;
    case kCheckedStoreWord16:
      ASSEMBLE_CHECKED_STORE_INTEGER(sth, sthx);
      break;
    case kCheckedStoreWord32:
      ASSEMBLE_CHECKED_STORE_INTEGER(stw, stwx);
      break;
    case kCheckedStoreFloat32:
      ASSEMBLE_CHECKED_STORE_FLOAT(stfs, stfsx);
      break;
    case kCheckedStoreFloat64:
      ASSEMBLE_CHECKED_STORE_FLOAT(stfd, stfdx);
      break;
    default:
      UNREACHABLE();
      break;
  }
}


// Assembles branches after an instruction.
void CodeGenerator::AssembleArchBranch(Instruction* instr, BranchInfo* branch) {
  PPCOperandConverter i(this, instr);
  Label* tlabel = branch->true_label;
  Label* flabel = branch->false_label;
  FlagsCondition condition = branch->condition;
  Condition cond = kNoCondition;
  CRegister cr = cr0;
#if DEBUG
  ArchOpcode op = ArchOpcodeField::decode(instr->opcode());
#endif
  switch (condition) {
    case kUnorderedEqual:
    case kUnorderedLessThan:
    case kUnorderedLessThanOrEqual:
      __ bunordered(flabel, cr);
      break;
    case kUnorderedNotEqual:
    case kUnorderedGreaterThanOrEqual:
    case kUnorderedGreaterThan:
      __ bunordered(tlabel, cr);
      break;
    case kOverflow:
      // Currently used only for add/sub overflow
      DCHECK(op == kPPC_AddWithOverflow32 || op == kPPC_SubWithOverflow32);
#if V8_TARGET_ARCH_PPC64
      condition = kNotEqual;
#else
      condition = kSignedLessThan;
#endif
      break;
    case kNotOverflow:
      // Currently used only for add/sub overflow
      DCHECK(op == kPPC_AddWithOverflow32 || op == kPPC_SubWithOverflow32);
#if V8_TARGET_ARCH_PPC64
      condition = kEqual;
#else
      condition = kSignedGreaterThanOrEqual;
#endif
      break;
    default:
      break;
  }

  switch (condition) {
    case kUnorderedEqual:
    case kEqual:
      cond = eq;
      break;
    case kUnorderedNotEqual:
    case kNotEqual:
      cond = ne;
      break;
    case kUnorderedLessThan:
    case kSignedLessThan:
    case kUnsignedLessThan:
      cond = lt;
      break;
    case kUnorderedGreaterThanOrEqual:
    case kSignedGreaterThanOrEqual:
    case kUnsignedGreaterThanOrEqual:
      cond = ge;
      break;
    case kUnorderedLessThanOrEqual:
    case kSignedLessThanOrEqual:
    case kUnsignedLessThanOrEqual:
      cond = le;
      break;
    case kUnorderedGreaterThan:
    case kSignedGreaterThan:
    case kUnsignedGreaterThan:
      cond = gt;
      break;
    case kOverflow:
    case kNotOverflow:
      UNREACHABLE();
      break;
  }
  __ b(cond, tlabel, cr);
  if (!branch->fallthru) __ b(flabel);  // no fallthru to flabel.
}


void CodeGenerator::AssembleArchJump(BasicBlock::RpoNumber target) {
  if (!IsNextInAssemblyOrder(target)) __ b(GetLabel(target));
}


// Assembles boolean materializations after an instruction.
void CodeGenerator::AssembleArchBoolean(Instruction* instr,
                                        FlagsCondition condition) {
  PPCOperandConverter i(this, instr);
  Label done;
  CRegister cr = cr0;
#if DEBUG
  ArchOpcode op = ArchOpcodeField::decode(instr->opcode());
#endif

  // Materialize a full 32-bit 1 or 0 value. The result register is always the
  // last output of the instruction.
  DCHECK_NE(0, instr->OutputCount());
  Register reg = i.OutputRegister(instr->OutputCount() - 1);

  switch (condition) {
    case kOverflow:
      // Currently used only for add/sub overflow
      DCHECK(op == kPPC_AddWithOverflow32 || op == kPPC_SubWithOverflow32);
#if V8_TARGET_ARCH_PPC64
      condition = kNotEqual;
#else
      condition = kSignedLessThan;
#endif
      break;
    case kNotOverflow:
      // Currently used only for add/sub overflow
      DCHECK(op == kPPC_AddWithOverflow32 || op == kPPC_SubWithOverflow32);
#if V8_TARGET_ARCH_PPC64
      condition = kEqual;
#else
      condition = kSignedGreaterThanOrEqual;
#endif
      break;
    default:
      break;
  }

  switch (condition) {
    case kUnorderedEqual:
    case kEqual:
      __ li(reg, Operand::Zero());
      __ li(kScratchReg, Operand(1));
      if (condition == kUnorderedEqual) __ bunordered(&done, cr);
      __ isel(eq, reg, kScratchReg, reg, cr);
      break;
    case kUnorderedNotEqual:
    case kNotEqual:
      __ li(reg, Operand(1));
      if (condition == kUnorderedNotEqual) __ bunordered(&done, cr);
      __ isel(eq, reg, r0, reg, cr);
      break;
    case kUnorderedLessThan:
    case kSignedLessThan:
    case kUnsignedLessThan:
      __ li(reg, Operand::Zero());
      __ li(kScratchReg, Operand(1));
      if (condition == kUnorderedLessThan) __ bunordered(&done, cr);
      __ isel(lt, reg, kScratchReg, reg, cr);
      break;
    case kUnorderedGreaterThanOrEqual:
    case kSignedGreaterThanOrEqual:
    case kUnsignedGreaterThanOrEqual:
      __ li(reg, Operand(1));
      if (condition == kUnorderedGreaterThanOrEqual) __ bunordered(&done, cr);
      __ isel(lt, reg, r0, reg, cr);
      break;
    case kUnorderedLessThanOrEqual:
      __ li(reg, Operand::Zero());
      __ li(kScratchReg, Operand(1));
      __ bunordered(&done, cr);
      __ isel(gt, reg, r0, kScratchReg, cr);
      break;
    case kSignedLessThanOrEqual:
    case kUnsignedLessThanOrEqual:
      __ li(reg, Operand(1));
      __ isel(gt, reg, r0, reg, cr);
      break;
    case kUnorderedGreaterThan:
      __ li(reg, Operand(1));
      __ li(kScratchReg, Operand::Zero());
      __ bunordered(&done, cr);
      __ isel(gt, reg, reg, kScratchReg, cr);
      break;
    case kSignedGreaterThan:
    case kUnsignedGreaterThan:
      __ li(reg, Operand::Zero());
      __ li(kScratchReg, Operand(1));
      __ isel(gt, reg, kScratchReg, reg, cr);
      break;
    case kOverflow:
    case kNotOverflow:
      UNREACHABLE();
      break;
  }
  __ bind(&done);
}


void CodeGenerator::AssembleDeoptimizerCall(int deoptimization_id) {
  Address deopt_entry = Deoptimizer::GetDeoptimizationEntry(
      isolate(), deoptimization_id, Deoptimizer::LAZY);
  __ Call(deopt_entry, RelocInfo::RUNTIME_ENTRY);
}


void CodeGenerator::AssemblePrologue() {
  CallDescriptor* descriptor = linkage()->GetIncomingDescriptor();
  if (descriptor->kind() == CallDescriptor::kCallAddress) {
#if ABI_USES_FUNCTION_DESCRIPTORS
    __ function_descriptor();
#endif
    int register_save_area_size = 0;
    RegList frame_saves = fp.bit();
    if (FLAG_enable_ool_constant_pool) {
      __ mflr(r0);
      __ Push(r0, fp, kConstantPoolRegister);
      // Adjust FP to point to saved FP.
      __ subi(fp, sp, Operand(StandardFrameConstants::kConstantPoolOffset));
      register_save_area_size += kPointerSize;
      frame_saves |= kConstantPoolRegister.bit();
    } else {
      __ mflr(r0);
      __ Push(r0, fp);
      __ mr(fp, sp);
    }
    // Save callee-saved registers.
    const RegList saves = descriptor->CalleeSavedRegisters() & ~frame_saves;
    for (int i = Register::kNumRegisters - 1; i >= 0; i--) {
      if (!((1 << i) & saves)) continue;
      register_save_area_size += kPointerSize;
    }
    frame()->SetRegisterSaveAreaSize(register_save_area_size);
    __ MultiPush(saves);
  } else if (descriptor->IsJSFunctionCall()) {
    CompilationInfo* info = this->info();
    __ Prologue(info->IsCodePreAgingActive());
    frame()->SetRegisterSaveAreaSize(
        StandardFrameConstants::kFixedFrameSizeFromFp);
  } else {
    __ StubPrologue();
    frame()->SetRegisterSaveAreaSize(
        StandardFrameConstants::kFixedFrameSizeFromFp);
  }
  int stack_slots = frame()->GetSpillSlotCount();
  if (stack_slots > 0) {
    __ Add(sp, sp, -stack_slots * kPointerSize, r0);
  }
}


void CodeGenerator::AssembleReturn() {
  CallDescriptor* descriptor = linkage()->GetIncomingDescriptor();
  if (descriptor->kind() == CallDescriptor::kCallAddress) {
    if (frame()->GetRegisterSaveAreaSize() > 0) {
      // Remove this frame's spill slots first.
      int stack_slots = frame()->GetSpillSlotCount();
      if (stack_slots > 0) {
        __ Add(sp, sp, stack_slots * kPointerSize, r0);
      }
      // Restore registers.
      RegList frame_saves = fp.bit();
      if (FLAG_enable_ool_constant_pool) {
        frame_saves |= kConstantPoolRegister.bit();
      }
      const RegList saves = descriptor->CalleeSavedRegisters() & ~frame_saves;
      if (saves != 0) {
        __ MultiPop(saves);
      }
    }
    __ LeaveFrame(StackFrame::MANUAL);
    __ Ret();
  } else {
    int pop_count = descriptor->IsJSFunctionCall()
                        ? static_cast<int>(descriptor->JSParameterCount())
                        : 0;
    __ LeaveFrame(StackFrame::MANUAL, pop_count * kPointerSize);
    __ Ret();
  }
}


void CodeGenerator::AssembleMove(InstructionOperand* source,
                                 InstructionOperand* destination) {
  PPCOperandConverter g(this, NULL);
  // Dispatch on the source and destination operand kinds.  Not all
  // combinations are possible.
  if (source->IsRegister()) {
    DCHECK(destination->IsRegister() || destination->IsStackSlot());
    Register src = g.ToRegister(source);
    if (destination->IsRegister()) {
      __ Move(g.ToRegister(destination), src);
    } else {
      __ StoreP(src, g.ToMemOperand(destination), r0);
    }
  } else if (source->IsStackSlot()) {
    DCHECK(destination->IsRegister() || destination->IsStackSlot());
    MemOperand src = g.ToMemOperand(source);
    if (destination->IsRegister()) {
      __ LoadP(g.ToRegister(destination), src, r0);
    } else {
      Register temp = kScratchReg;
      __ LoadP(temp, src, r0);
      __ StoreP(temp, g.ToMemOperand(destination), r0);
    }
  } else if (source->IsConstant()) {
    Constant src = g.ToConstant(source);
    if (destination->IsRegister() || destination->IsStackSlot()) {
      Register dst =
          destination->IsRegister() ? g.ToRegister(destination) : kScratchReg;
      switch (src.type()) {
        case Constant::kInt32:
          __ mov(dst, Operand(src.ToInt32()));
          break;
        case Constant::kInt64:
          __ mov(dst, Operand(src.ToInt64()));
          break;
        case Constant::kFloat32:
          __ Move(dst,
                  isolate()->factory()->NewNumber(src.ToFloat32(), TENURED));
          break;
        case Constant::kFloat64:
          __ Move(dst,
                  isolate()->factory()->NewNumber(src.ToFloat64(), TENURED));
          break;
        case Constant::kExternalReference:
          __ mov(dst, Operand(src.ToExternalReference()));
          break;
        case Constant::kHeapObject:
          __ Move(dst, src.ToHeapObject());
          break;
        case Constant::kRpoNumber:
          UNREACHABLE();  // TODO(dcarney): loading RPO constants on PPC.
          break;
      }
      if (destination->IsStackSlot()) {
        __ StoreP(dst, g.ToMemOperand(destination), r0);
      }
    } else {
      DoubleRegister dst = destination->IsDoubleRegister()
                               ? g.ToDoubleRegister(destination)
                               : kScratchDoubleReg;
      double value = (src.type() == Constant::kFloat32)
                         ? src.ToFloat32()
                         : src.ToFloat64();
      __ LoadDoubleLiteral(dst, value, kScratchReg);
      if (destination->IsDoubleStackSlot()) {
        __ StoreDouble(dst, g.ToMemOperand(destination), r0);
      }
    }
  } else if (source->IsDoubleRegister()) {
    DoubleRegister src = g.ToDoubleRegister(source);
    if (destination->IsDoubleRegister()) {
      DoubleRegister dst = g.ToDoubleRegister(destination);
      __ Move(dst, src);
    } else {
      DCHECK(destination->IsDoubleStackSlot());
      __ StoreDouble(src, g.ToMemOperand(destination), r0);
    }
  } else if (source->IsDoubleStackSlot()) {
    DCHECK(destination->IsDoubleRegister() || destination->IsDoubleStackSlot());
    MemOperand src = g.ToMemOperand(source);
    if (destination->IsDoubleRegister()) {
      __ LoadDouble(g.ToDoubleRegister(destination), src, r0);
    } else {
      DoubleRegister temp = kScratchDoubleReg;
      __ LoadDouble(temp, src, r0);
      __ StoreDouble(temp, g.ToMemOperand(destination), r0);
    }
  } else {
    UNREACHABLE();
  }
}


void CodeGenerator::AssembleSwap(InstructionOperand* source,
                                 InstructionOperand* destination) {
  PPCOperandConverter g(this, NULL);
  // Dispatch on the source and destination operand kinds.  Not all
  // combinations are possible.
  if (source->IsRegister()) {
    // Register-register.
    Register temp = kScratchReg;
    Register src = g.ToRegister(source);
    if (destination->IsRegister()) {
      Register dst = g.ToRegister(destination);
      __ mr(temp, src);
      __ mr(src, dst);
      __ mr(dst, temp);
    } else {
      DCHECK(destination->IsStackSlot());
      MemOperand dst = g.ToMemOperand(destination);
      __ mr(temp, src);
      __ LoadP(src, dst);
      __ StoreP(temp, dst);
    }
#if V8_TARGET_ARCH_PPC64
  } else if (source->IsStackSlot() || source->IsDoubleStackSlot()) {
#else
  } else if (source->IsStackSlot()) {
#endif
    DCHECK(destination->IsStackSlot());
    Register temp_0 = kScratchReg;
    Register temp_1 = r0;
    MemOperand src = g.ToMemOperand(source);
    MemOperand dst = g.ToMemOperand(destination);
    __ LoadP(temp_0, src);
    __ LoadP(temp_1, dst);
    __ StoreP(temp_0, dst);
    __ StoreP(temp_1, src);
  } else if (source->IsDoubleRegister()) {
    DoubleRegister temp = kScratchDoubleReg;
    DoubleRegister src = g.ToDoubleRegister(source);
    if (destination->IsDoubleRegister()) {
      DoubleRegister dst = g.ToDoubleRegister(destination);
      __ fmr(temp, src);
      __ fmr(src, dst);
      __ fmr(dst, temp);
    } else {
      DCHECK(destination->IsDoubleStackSlot());
      MemOperand dst = g.ToMemOperand(destination);
      __ fmr(temp, src);
      __ lfd(src, dst);
      __ stfd(temp, dst);
    }
#if !V8_TARGET_ARCH_PPC64
  } else if (source->IsDoubleStackSlot()) {
    DCHECK(destination->IsDoubleStackSlot());
    DoubleRegister temp_0 = kScratchDoubleReg;
    DoubleRegister temp_1 = d0;
    MemOperand src = g.ToMemOperand(source);
    MemOperand dst = g.ToMemOperand(destination);
    __ lfd(temp_0, src);
    __ lfd(temp_1, dst);
    __ stfd(temp_0, dst);
    __ stfd(temp_1, src);
#endif
  } else {
    // No other combinations are possible.
    UNREACHABLE();
  }
}


void CodeGenerator::AddNopForSmiCodeInlining() {
  // We do not insert nops for inlined Smi code.
}


void CodeGenerator::EnsureSpaceForLazyDeopt() {
  int space_needed = Deoptimizer::patch_size();
  if (!info()->IsStub()) {
    // Ensure that we have enough space after the previous lazy-bailout
    // instruction for patching the code here.
    int current_pc = masm()->pc_offset();
    if (current_pc < last_lazy_deopt_pc_ + space_needed) {
      int padding_size = last_lazy_deopt_pc_ + space_needed - current_pc;
      DCHECK_EQ(0, padding_size % v8::internal::Assembler::kInstrSize);
      while (padding_size > 0) {
        __ nop();
        padding_size -= v8::internal::Assembler::kInstrSize;
      }
    }
  }
  MarkLazyDeoptSite();
}

#undef __

}  // namespace compiler
}  // namespace internal
}  // namespace v8
