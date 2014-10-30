// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/instruction-selector-impl.h"
#include "src/compiler/node-matchers.h"

namespace v8 {
namespace internal {
namespace compiler {

enum ImmediateMode {
  kInt16Imm,
  kUint16Imm,
  kMinusInt16Imm,
  kInt16Imm_4ByteAligned,
  kShift32Imm,
  kShift64Imm,
  kNoImmediate
};


// Adds PPC-specific methods for generating operands.
class PPCOperandGenerator V8_FINAL : public OperandGenerator {
 public:
  explicit PPCOperandGenerator(InstructionSelector* selector)
      : OperandGenerator(selector) {}

  InstructionOperand* UseOperand(Node* node, ImmediateMode mode) {
    if (CanBeImmediate(node, mode)) {
      return UseImmediate(node);
    }
    return UseRegister(node);
  }

  bool CanBeImmediate(Node* node, ImmediateMode mode) {
    int64_t value;
    if (node->opcode() == IrOpcode::kInt32Constant)
        value = ValueOf<int32_t>(node->op());
    else if (node->opcode() == IrOpcode::kInt64Constant)
        value = ValueOf<int64_t>(node->op());
    else
      return false;
    switch (mode) {
      case kInt16Imm:
        return is_int16(value);
      case kUint16Imm:
        return is_uint16(value);
      case kMinusInt16Imm:
        return is_int16(-value);
      case kInt16Imm_4ByteAligned:
        return is_int16(value) && !(value & 3);
      case kShift32Imm:
        return 0 <= value && value < 32;
      case kShift64Imm:
        return 0 <= value && value < 64;
      case kNoImmediate:
        return false;
    }
    return false;
  }
};


#if V8_TARGET_ARCH_PPC64
static void VisitRR(InstructionSelector* selector, Node* node,
                    ArchOpcode opcode) {
  PPCOperandGenerator g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(node->InputAt(0)));
}
#endif


static void VisitRRR(InstructionSelector* selector, Node* node,
                     ArchOpcode opcode) {
  PPCOperandGenerator g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(node->InputAt(0)),
                 g.UseRegister(node->InputAt(1)));
}


static void VisitRRRFloat64(InstructionSelector* selector, Node* node,
                            ArchOpcode opcode) {
  PPCOperandGenerator g(selector);
  selector->Emit(opcode, g.DefineAsDoubleRegister(node),
                 g.UseDoubleRegister(node->InputAt(0)),
                 g.UseDoubleRegister(node->InputAt(1)));
}


static void VisitRRO(InstructionSelector* selector, Node* node,
                     ArchOpcode opcode, ImmediateMode operand_mode) {
  PPCOperandGenerator g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(node->InputAt(0)),
                 g.UseOperand(node->InputAt(1), operand_mode));
}


// Shared routine for multiple binary operations.
static void VisitBinop(InstructionSelector* selector, Node* node,
                       InstructionCode opcode, ImmediateMode operand_mode,
                       FlagsContinuation* cont) {
  PPCOperandGenerator g(selector);
  Int32BinopMatcher m(node);
  InstructionOperand* inputs[4];
  size_t input_count = 0;
  InstructionOperand* outputs[2];
  size_t output_count = 0;

  inputs[input_count++] = g.UseRegister(m.left().node());
  inputs[input_count++] = g.UseOperand(m.right().node(), operand_mode);

  if (cont->IsBranch()) {
    inputs[input_count++] = g.Label(cont->true_block());
    inputs[input_count++] = g.Label(cont->false_block());
  }

  outputs[output_count++] = g.DefineAsRegister(node);
  if (cont->IsSet()) {
    outputs[output_count++] = g.DefineAsRegister(cont->result());
  }

  DCHECK_NE(0, input_count);
  DCHECK_NE(0, output_count);
  DCHECK_GE(ARRAY_SIZE(inputs), input_count);
  DCHECK_GE(ARRAY_SIZE(outputs), output_count);

  Instruction* instr = selector->Emit(cont->Encode(opcode), output_count,
                                      outputs, input_count, inputs);
  if (cont->IsBranch()) instr->MarkAsControl();
}


// Shared routine for multiple binary operations.
static void VisitBinop(InstructionSelector* selector, Node* node,
                       ArchOpcode opcode, ImmediateMode operand_mode) {
  FlagsContinuation cont;
  VisitBinop(selector, node, opcode, operand_mode, &cont);
}


void InstructionSelector::VisitLoad(Node* node) {
  MachineType rep = OpParameter<MachineType>(node);
  PPCOperandGenerator g(this);
  Node* base = node->InputAt(0);
  Node* index = node->InputAt(1);

  InstructionOperand* result = rep == kMachineFloat64
                                   ? g.DefineAsDoubleRegister(node)
                                   : g.DefineAsRegister(node);

  ArchOpcode opcode;
  ImmediateMode mode = kInt16Imm;
  switch (rep) {
    case kMachineFloat64:
      opcode = kPPC_LoadFloat64;
      break;
    case kMachineWord8:
      opcode = kPPC_LoadWord8;
      break;
    case kMachineWord16:
      opcode = kPPC_LoadWord16;
      break;
#if !V8_TARGET_ARCH_PPC64
    case kMachineTagged:  // Fall through.
#endif
    case kMachineWord32:
      opcode = kPPC_LoadWord32;
#if V8_TARGET_ARCH_PPC64
      // TODO(mbrandy): this applies to signed loads only (lwa)
      mode = kInt16Imm_4ByteAligned;
#endif
      break;
#if V8_TARGET_ARCH_PPC64
    case kMachineTagged:  // Fall through.
    case kMachineWord64:
      opcode = kPPC_LoadWord64;
      mode = kInt16Imm_4ByteAligned;
      break;
#endif
    default:
      UNREACHABLE();
      return;
  }
  if (g.CanBeImmediate(index, mode)) {
    Emit(opcode | AddressingModeField::encode(kMode_MRI), result,
         g.UseRegister(base), g.UseImmediate(index));
  } else if (g.CanBeImmediate(base, mode)) {
    Emit(opcode | AddressingModeField::encode(kMode_MRI), result,
         g.UseRegister(index), g.UseImmediate(base));
  } else {
    Emit(opcode | AddressingModeField::encode(kMode_MRR), result,
         g.UseRegister(base), g.UseRegister(index));
  }
}


void InstructionSelector::VisitStore(Node* node) {
  PPCOperandGenerator g(this);
  Node* base = node->InputAt(0);
  Node* index = node->InputAt(1);
  Node* value = node->InputAt(2);

  StoreRepresentation store_rep = OpParameter<StoreRepresentation>(node);
  MachineType rep = store_rep.rep;
  if (store_rep.write_barrier_kind == kFullWriteBarrier) {
    DCHECK(rep == kMachineTagged);
    // TODO(dcarney): refactor RecordWrite function to take temp registers
    //                and pass them here instead of using fixed regs
    // TODO(dcarney): handle immediate indices.
    InstructionOperand* temps[] = {g.TempRegister(r8), g.TempRegister(r9)};
    Emit(kPPC_StoreWriteBarrier, NULL, g.UseFixed(base, r7),
         g.UseFixed(index, r8), g.UseFixed(value, r9), ARRAY_SIZE(temps),
         temps);
    return;
  }
  DCHECK_EQ(kNoWriteBarrier, store_rep.write_barrier_kind);
  InstructionOperand* val;
  if (rep == kMachineFloat64) {
    val = g.UseDoubleRegister(value);
  } else {
    val = g.UseRegister(value);
  }
  ArchOpcode opcode;
  ImmediateMode mode = kInt16Imm;
  switch (rep) {
    case kMachineFloat64:
      opcode = kPPC_StoreFloat64;
      break;
    case kMachineWord8:
      opcode = kPPC_StoreWord8;
      break;
    case kMachineWord16:
      opcode = kPPC_StoreWord16;
      break;
#if !V8_TARGET_ARCH_PPC64
    case kMachineTagged:  // Fall through.
#endif
    case kMachineWord32:
      opcode = kPPC_StoreWord32;
      break;
#if V8_TARGET_ARCH_PPC64
    case kMachineTagged:  // Fall through.
    case kMachineWord64:
      opcode = kPPC_StoreWord64;
      mode = kInt16Imm_4ByteAligned;
      break;
#endif
    default:
      UNREACHABLE();
      return;
  }
  if (g.CanBeImmediate(index, mode)) {
    Emit(opcode | AddressingModeField::encode(kMode_MRI), NULL,
         g.UseRegister(base), g.UseImmediate(index), val);
  } else if (g.CanBeImmediate(base, mode)) {
    Emit(opcode | AddressingModeField::encode(kMode_MRI), NULL,
         g.UseRegister(index), g.UseImmediate(base), val);
  } else {
    Emit(opcode | AddressingModeField::encode(kMode_MRR), NULL,
         g.UseRegister(base), g.UseRegister(index), val);
  }
}


void InstructionSelector::VisitWord32And(Node* node) {
  // TODO(mbrandy): detect bitfield extract (left is shr)
  // TODO(mbrandy): detect clear left
  // TODO(mbrandy): detect clear right
  VisitBinop(this, node, kPPC_And32, kUint16Imm);
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitWord64And(Node* node) {
  VisitBinop(this, node, kPPC_And64, kUint16Imm);
}
#endif


void InstructionSelector::VisitWord32Or(Node* node) {
  VisitBinop(this, node, kPPC_Or32, kUint16Imm);
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitWord64Or(Node* node) {
  VisitBinop(this, node, kPPC_Or64, kUint16Imm);
}
#endif


template <typename T>
static void VisitXor(InstructionSelector* selector, Node* node,
                     ArchOpcode xor_opcode, ArchOpcode not_opcode) {
  PPCOperandGenerator g(selector);
  BinopMatcher<IntMatcher<T>, IntMatcher<T> > m(node);
  if (m.right().Is(-1)) {
    selector->Emit(not_opcode, g.DefineAsRegister(node),
                   g.UseRegister(m.left().node()));
  } else {
    VisitBinop(selector, node, xor_opcode, kUint16Imm);
  }
}


void InstructionSelector::VisitWord32Xor(Node* node) {
  VisitXor<int32_t>(this, node, kPPC_Xor32, kPPC_Not32);
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitWord64Xor(Node* node) {
  VisitXor<int64_t>(this, node, kPPC_Xor64, kPPC_Not64);
}
#endif


void InstructionSelector::VisitWord32Shl(Node* node) {
  VisitRRO(this, node, kPPC_Shl32, kShift32Imm);
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitWord64Shl(Node* node) {
  VisitRRO(this, node, kPPC_Shl64, kShift64Imm);
}
#endif


void InstructionSelector::VisitWord32Shr(Node* node) {
  VisitRRO(this, node, kPPC_Shr32, kShift32Imm);
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitWord64Shr(Node* node) {
  VisitRRO(this, node, kPPC_Shr64, kShift64Imm);
}
#endif


void InstructionSelector::VisitWord32Sar(Node* node) {
  VisitRRO(this, node, kPPC_Sar32, kShift32Imm);
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitWord64Sar(Node* node) {
  VisitRRO(this, node, kPPC_Sar64, kShift64Imm);
}
#endif


void InstructionSelector::VisitInt32Add(Node* node) {
  VisitBinop(this, node, kPPC_Add32, kInt16Imm);
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitInt64Add(Node* node) {
  VisitBinop(this, node, kPPC_Add64, kInt16Imm);
}
#endif


template <typename T>
static void VisitSub(InstructionSelector* selector, Node* node,
                     ArchOpcode sub_opcode, ArchOpcode neg_opcode) {
  PPCOperandGenerator g(selector);
  BinopMatcher<IntMatcher<T>, IntMatcher<T> > m(node);
  if (m.left().Is(0)) {
    selector->Emit(neg_opcode, g.DefineAsRegister(node),
                   g.UseRegister(m.right().node()));
  } else {
    VisitBinop(selector, node, sub_opcode, kMinusInt16Imm);
  }
}


void InstructionSelector::VisitInt32Sub(Node* node) {
  VisitSub<int32_t>(this, node, kPPC_Sub32, kPPC_Neg32);
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitInt64Sub(Node* node) {
  VisitSub<int64_t>(this, node, kPPC_Sub64, kPPC_Neg64);
}
#endif


void InstructionSelector::VisitInt32Mul(Node* node) {
  VisitRRR(this, node, kPPC_Mul32);
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitInt64Mul(Node* node) {
  VisitRRR(this, node, kPPC_Mul64);
}
#endif


void InstructionSelector::VisitInt32Div(Node* node) {
  VisitRRR(this, node, kPPC_Div32);
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitInt64Div(Node* node) {
  VisitRRR(this, node, kPPC_Div64);
}
#endif


void InstructionSelector::VisitInt32UDiv(Node* node) {
  VisitRRR(this, node, kPPC_DivU32);
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitInt64UDiv(Node* node) {
  VisitRRR(this, node, kPPC_DivU64);
}
#endif


void InstructionSelector::VisitInt32Mod(Node* node) {
  VisitRRR(this, node, kPPC_Mod32);
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitInt64Mod(Node* node) {
  VisitRRR(this, node, kPPC_Mod64);
}
#endif


void InstructionSelector::VisitInt32UMod(Node* node) {
  VisitRRR(this, node, kPPC_ModU32);
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitInt64UMod(Node* node) {
  VisitRRR(this, node, kPPC_ModU64);
}
#endif


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitConvertInt32ToInt64(Node* node) {
  VisitRR(this, node, kPPC_Int32ToInt64);
}


void InstructionSelector::VisitConvertInt64ToInt32(Node* node) {
  VisitRR(this, node, kPPC_Int64ToInt32);
}
#endif


void InstructionSelector::VisitChangeInt32ToFloat64(Node* node) {
  PPCOperandGenerator g(this);
  Emit(kPPC_Int32ToFloat64, g.DefineAsDoubleRegister(node),
       g.UseRegister(node->InputAt(0)));
}


void InstructionSelector::VisitChangeUint32ToFloat64(Node* node) {
  PPCOperandGenerator g(this);
  Emit(kPPC_Uint32ToFloat64, g.DefineAsDoubleRegister(node),
       g.UseRegister(node->InputAt(0)));
}


void InstructionSelector::VisitChangeFloat64ToInt32(Node* node) {
  PPCOperandGenerator g(this);
  Emit(kPPC_Float64ToInt32, g.DefineAsRegister(node),
       g.UseDoubleRegister(node->InputAt(0)));
}


void InstructionSelector::VisitChangeFloat64ToUint32(Node* node) {
  PPCOperandGenerator g(this);
  Emit(kPPC_Float64ToUint32, g.DefineAsRegister(node),
       g.UseDoubleRegister(node->InputAt(0)));
}


void InstructionSelector::VisitFloat64Add(Node* node) {
  // TODO(mbrandy): detect multiply-add
  VisitRRRFloat64(this, node, kPPC_AddFloat64);
}


void InstructionSelector::VisitFloat64Sub(Node* node) {
  // TODO(mbrandy): detect multiply-subtract
  VisitRRRFloat64(this, node, kPPC_SubFloat64);
}


void InstructionSelector::VisitFloat64Mul(Node* node) {
  // TODO(mbrandy): detect negate
  VisitRRRFloat64(this, node, kPPC_MulFloat64);
}


void InstructionSelector::VisitFloat64Div(Node* node) {
  VisitRRRFloat64(this, node, kPPC_DivFloat64);
}


void InstructionSelector::VisitFloat64Mod(Node* node) {
  PPCOperandGenerator g(this);
  Emit(kPPC_ModFloat64, g.DefineAsFixedDouble(node, d1),
       g.UseFixedDouble(node->InputAt(0), d1),
       g.UseFixedDouble(node->InputAt(1), d2))->MarkAsCall();
}


void InstructionSelector::VisitInt32AddWithOverflow(Node* node,
                                                    FlagsContinuation* cont) {
  VisitBinop(this, node, kPPC_AddWithOverflow32, kInt16Imm, cont);
}


void InstructionSelector::VisitInt32SubWithOverflow(Node* node,
                                                    FlagsContinuation* cont) {
  VisitBinop(this, node, kPPC_SubWithOverflow32, kMinusInt16Imm, cont);
}


static bool CompareLogical(FlagsContinuation* cont) {
  switch (cont->condition()) {
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


// Shared routine for multiple compare operations.
static void VisitCompare(InstructionSelector* selector, InstructionCode opcode,
                         InstructionOperand* left, InstructionOperand* right,
                         FlagsContinuation* cont) {
  PPCOperandGenerator g(selector);
  opcode = cont->Encode(opcode);
  if (cont->IsBranch()) {
    selector->Emit(opcode, NULL, left, right, g.Label(cont->true_block()),
                   g.Label(cont->false_block()))->MarkAsControl();
  } else {
    DCHECK(cont->IsSet());
    selector->Emit(opcode, g.DefineAsRegister(cont->result()), left, right);
  }
}


// Shared routine for multiple word compare operations.
static void VisitWordCompare(InstructionSelector* selector, Node* node,
                             InstructionCode opcode, ImmediateMode operand_mode,
                             FlagsContinuation* cont, bool commutative) {
  PPCOperandGenerator g(selector);
  Node* left = node->InputAt(0);
  Node* right = node->InputAt(1);

  // Match immediates on left or right side of comparison.
  if (g.CanBeImmediate(right, operand_mode)) {
    VisitCompare(selector, opcode, g.UseRegister(left), g.UseImmediate(right),
                 cont);
  } else if (g.CanBeImmediate(left, operand_mode)) {
    if (!commutative) cont->Commute();
    VisitCompare(selector, opcode, g.UseRegister(right), g.UseImmediate(left),
                 cont);
  } else {
    VisitCompare(selector, opcode, g.UseRegister(left), g.UseRegister(right),
                 cont);
  }
}


void InstructionSelector::VisitWord32Test(Node* node, FlagsContinuation* cont) {
  switch (node->opcode()) {
    case IrOpcode::kInt32Sub: {
      ImmediateMode mode = (CompareLogical(cont) ? kUint16Imm : kInt16Imm);
      return VisitWordCompare(this, node, kPPC_Cmp32, mode, cont, false);
    }
    case IrOpcode::kWord32And:
      return VisitWordCompare(this, node, kPPC_Tst32, kUint16Imm, cont, true);
    default:
      break;
  }

  PPCOperandGenerator g(this);
  VisitCompare(this, kPPC_Tst32, g.UseRegister(node), g.UseRegister(node),
               cont);
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitWord64Test(Node* node, FlagsContinuation* cont) {
  switch (node->opcode()) {
    case IrOpcode::kInt64Sub: {
      ImmediateMode mode = (CompareLogical(cont) ? kUint16Imm : kInt16Imm);
      return VisitWordCompare(this, node, kPPC_Cmp64, mode, cont, false);
    }
    case IrOpcode::kWord64And:
      return VisitWordCompare(this, node, kPPC_Tst64, kUint16Imm, cont, true);
    default:
      break;
  }

  PPCOperandGenerator g(this);
  VisitCompare(this, kPPC_Tst64, g.UseRegister(node), g.UseRegister(node),
               cont);
}
#endif


void InstructionSelector::VisitWord32Compare(Node* node,
                                             FlagsContinuation* cont) {
  ImmediateMode mode = (CompareLogical(cont) ? kUint16Imm : kInt16Imm);
  VisitWordCompare(this, node, kPPC_Cmp32, mode, cont, false);
}


#if V8_TARGET_ARCH_PPC64
void InstructionSelector::VisitWord64Compare(Node* node,
                                             FlagsContinuation* cont) {
  ImmediateMode mode = (CompareLogical(cont) ? kUint16Imm : kInt16Imm);
  VisitWordCompare(this, node, kPPC_Cmp64, mode, cont, false);
}
#endif


void InstructionSelector::VisitFloat64Compare(Node* node,
                                              FlagsContinuation* cont) {
  PPCOperandGenerator g(this);
  Node* left = node->InputAt(0);
  Node* right = node->InputAt(1);
  VisitCompare(this, kPPC_CmpFloat64, g.UseDoubleRegister(left),
               g.UseDoubleRegister(right), cont);
}


void InstructionSelector::VisitCall(Node* call, BasicBlock* continuation,
                                    BasicBlock* deoptimization) {
  PPCOperandGenerator g(this);
  CallDescriptor* descriptor = OpParameter<CallDescriptor*>(call);
  CallBuffer buffer(zone(), descriptor);  // TODO(turbofan): temp zone here?

  // Compute InstructionOperands for inputs and outputs.
  // TODO(turbofan): on PPC it's probably better to use the code object in a
  // register if there are multiple uses of it. Improve constant pool and the
  // heuristics in the register allocator for where to emit constants.
  InitializeCallBuffer(call, &buffer, true, false, continuation,
                       deoptimization);

  // Push any stack arguments.
  // TODO(mbrandy): reverse order and use push only for first
  for (int i = buffer.pushed_count - 1; i >= 0; --i) {
    Node* input = buffer.pushed_nodes[i];
    Emit(kPPC_Push, NULL, g.UseRegister(input));
  }

  // Select the appropriate opcode based on the call type.
  InstructionCode opcode;
  switch (descriptor->kind()) {
    case CallDescriptor::kCallCodeObject: {
      bool lazy_deopt = descriptor->CanLazilyDeoptimize();
      opcode = kPPC_CallCodeObject | MiscField::encode(lazy_deopt ? 1 : 0);
      break;
    }
    case CallDescriptor::kCallAddress:
      opcode = kPPC_CallAddress;
      break;
    case CallDescriptor::kCallJSFunction:
      opcode = kPPC_CallJSFunction;
      break;
    default:
      UNREACHABLE();
      return;
  }

  // Emit the call instruction.
  Instruction* call_instr =
      Emit(opcode, buffer.output_count, buffer.outputs,
           buffer.fixed_and_control_count(), buffer.fixed_and_control_args);

  call_instr->MarkAsCall();
  if (deoptimization != NULL) {
    DCHECK(continuation != NULL);
    call_instr->MarkAsControl();
  }

  // Caller clean up of stack for C-style calls.
  if (descriptor->kind() == CallDescriptor::kCallAddress &&
      buffer.pushed_count > 0) {
    DCHECK(deoptimization == NULL && continuation == NULL);
    Emit(kPPC_Drop | MiscField::encode(buffer.pushed_count), NULL);
  }
}


}  // namespace compiler
}  // namespace internal
}  // namespace v8
