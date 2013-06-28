// Copyright (c) 1994-2006 Sun Microsystems Inc.
// All Rights Reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
// - Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// - Redistribution in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the
// distribution.
//
// - Neither the name of Sun Microsystems or the names of contributors may
// be used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
// OF THE POSSIBILITY OF SUCH DAMAGE.

// The original source code covered by the above license above has been
// modified significantly by Google Inc.
// Copyright 2012 the V8 project authors. All rights reserved.

//
// Copyright IBM Corp. 2012, 2013. All rights reserved.
//

#include "v8.h"

#if defined(V8_TARGET_ARCH_PPC)

#include "ppc/assembler-ppc-inl.h"
#include "serialize.h"

namespace v8 {
namespace internal {
#define INCLUDE_ARM 1

#ifdef DEBUG
bool CpuFeatures::initialized_ = false;
#endif
unsigned CpuFeatures::supported_ = 0;
unsigned CpuFeatures::found_by_runtime_probing_ = 0;

#define EMIT_FAKE_ARM_INSTR(arm_opcode) fake_asm(arm_opcode);

#define NEW_IMM_CHECK_CODE

// Get the CPU features enabled by the build. For cross compilation the
// preprocessor symbols CAN_USE_ARMV7_INSTRUCTIONS and CAN_USE_VFP3_INSTRUCTIONS
// can be defined to enable ARMv7 and VFPv3 instructions when building the
// snapshot.
static unsigned CpuFeaturesImpliedByCompiler() {
  unsigned answer = 0;
#ifdef CAN_USE_ARMV7_INSTRUCTIONS
  answer |= 1u << ARMv7;
#endif  // CAN_USE_ARMV7_INSTRUCTIONS
#ifdef CAN_USE_VFP3_INSTRUCTIONS
  answer |= 1u << VFP3 | 1u << VFP2 | 1u << ARMv7;
#endif  // CAN_USE_VFP3_INSTRUCTIONS
#ifdef CAN_USE_VFP2_INSTRUCTIONS
  answer |= 1u << VFP2;
#endif  // CAN_USE_VFP2_INSTRUCTIONS

#ifdef __arm__
  // If the compiler is allowed to use VFP then we can use VFP too in our code
  // generation even when generating snapshots. ARMv7 and hardware floating
  // point support implies VFPv3, see ARM DDI 0406B, page A1-6.
#if defined(CAN_USE_ARMV7_INSTRUCTIONS) && defined(__VFP_FP__) \
    && !defined(__SOFTFP__)
  answer |= 1u << VFP3 | 1u << ARMv7 | 1u << VFP2;
#endif  // defined(CAN_USE_ARMV7_INSTRUCTIONS) && defined(__VFP_FP__)
        // && !defined(__SOFTFP__)
#endif  // _arm__

  return answer;
}


void CpuFeatures::Probe() {
  unsigned standard_features = static_cast<unsigned>(
      OS::CpuFeaturesImpliedByPlatform()) | CpuFeaturesImpliedByCompiler();
  ASSERT(supported_ == 0 || supported_ == standard_features);
#ifdef DEBUG
  initialized_ = true;
#endif

  // Get the features implied by the OS and the compiler settings. This is the
  // minimal set of features which is also alowed for generated code in the
  // snapshot.
  supported_ |= standard_features;

  if (Serializer::enabled()) {
    // No probing for features if we might serialize (generate snapshot).
    return;
  }

#ifndef __arm__
  // For the simulator=arm build, use VFP when FLAG_enable_vfp3 is
  // enabled. VFPv3 implies ARMv7, see ARM DDI 0406B, page A1-6.
  if (FLAG_enable_vfp3) {
    supported_ |= 1u << VFP3 | 1u << ARMv7 | 1u << VFP2;
  }
  // For the simulator=arm build, use ARMv7 when FLAG_enable_armv7 is enabled
  if (FLAG_enable_armv7) {
    supported_ |= 1u << ARMv7;
  }
#else  // __arm__
  // Probe for additional features not already known to be available.
  if (!IsSupported(VFP3) && OS::ArmCpuHasFeature(VFP3)) {
    // This implementation also sets the VFP flags if runtime
    // detection of VFP returns true. VFPv3 implies ARMv7 and VFP2, see ARM DDI
    // 0406B, page A1-6.
    found_by_runtime_probing_ |= 1u << VFP3 | 1u << ARMv7 | 1u << VFP2;
  } else if (!IsSupported(VFP2) && OS::ArmCpuHasFeature(VFP2)) {
    found_by_runtime_probing_ |= 1u << VFP2;
  }

  if (!IsSupported(ARMv7) && OS::ArmCpuHasFeature(ARMv7)) {
    found_by_runtime_probing_ |= 1u << ARMv7;
  }

  supported_ |= found_by_runtime_probing_;
#endif

  // Assert that VFP3 implies VFP2 and ARMv7.
  ASSERT(!IsSupported(VFP3) || (IsSupported(VFP2) && IsSupported(ARMv7)));
}

Register ToRegister(int num) {
  ASSERT(num >= 0 && num < kNumRegisters);
  const Register kRegisters[] = {
    r0,
    sp,
    r2, r3, r4, r5, r6, r7, r8, r9, r10,
    r11, ip, r13, r14, r15,
    r16, r17, r18, r19, r20, r21, r22, r23, r24,
    r25, r26, r27, r28, r29, r30, fp
  };
  return kRegisters[num];
}


// -----------------------------------------------------------------------------
// Implementation of RelocInfo

const int RelocInfo::kApplyMask = 0;


bool RelocInfo::IsCodedSpecially() {
  // The deserializer needs to know whether a pointer is specially coded.  Being
  // specially coded on ARM means that it is a movw/movt instruction.  We don't
  // generate those yet.
  return false;
}


void RelocInfo::PatchCode(byte* instructions, int instruction_count) {
  // Patch the code at the current address with the supplied instructions.
  Instr* pc = reinterpret_cast<Instr*>(pc_);
  Instr* instr = reinterpret_cast<Instr*>(instructions);
  for (int i = 0; i < instruction_count; i++) {
    *(pc + i) = *(instr + i);
  }

  // Indicate that code has changed.
  CPU::FlushICache(pc_, instruction_count * Assembler::kInstrSize);
}


// Patch the code at the current PC with a call to the target address.
// Additional guard instructions can be added if required.
void RelocInfo::PatchCodeWithCall(Address target, int guard_bytes) {
  // Patch the code at the current address with a call to the target.
  UNIMPLEMENTED();
}


// -----------------------------------------------------------------------------
// Implementation of Operand and MemOperand
// See assembler-arm-inl.h for inlined constructors

Operand::Operand(Handle<Object> handle) {
  rm_ = no_reg;
  // Verify all Objects referred by code are NOT in new space.
  Object* obj = *handle;
  ASSERT(!HEAP->InNewSpace(obj));
  if (obj->IsHeapObject()) {
    imm32_ = reinterpret_cast<intptr_t>(handle.location());
    rmode_ = RelocInfo::EMBEDDED_OBJECT;
  } else {
    // no relocation needed
    imm32_ =  reinterpret_cast<intptr_t>(obj);
    rmode_ = RelocInfo::NONE;
  }
}


Operand::Operand(Register rm, ShiftOp shift_op, int shift_imm) {
  ASSERT(is_uint5(shift_imm));
  ASSERT(shift_op != ROR || shift_imm != 0);  // use RRX if you mean it
  rm_ = rm;
  rs_ = no_reg;
  shift_op_ = shift_op;
  shift_imm_ = shift_imm & 31;
  if (shift_op == RRX) {
    // encoded as ROR with shift_imm == 0
    ASSERT(shift_imm == 0);
    shift_op_ = ROR;
    shift_imm_ = 0;
  }
  rmode_ = RelocInfo::NONE;  // ?? again why not needed on ARM
}


Operand::Operand(Register rm, ShiftOp shift_op, Register rs) {
  ASSERT(shift_op != RRX);
  rm_ = rm;
  rs_ = no_reg;
  shift_op_ = shift_op;
  rs_ = rs;
  rmode_ = RelocInfo::NONE;  // ?? again why not needed on ARM
}


MemOperand::MemOperand(Register rn, int32_t offset, AddrMode am) {
  ra_ = rn;
  rm_ = no_reg;
  offset_ = offset;
  am_ = am;
}

MemOperand::MemOperand(Register rn, Register rm, AddrMode am) {
  ra_ = rn;
  rm_ = rm;
  shift_op_ = LSL;
  shift_imm_ = 0;
  am_ = am;
}


MemOperand::MemOperand(Register rn, Register rm,
                       ShiftOp shift_op, int shift_imm, AddrMode am) {
  ASSERT(is_uint5(shift_imm));
  ra_ = rn;
  rm_ = rm;
  shift_op_ = shift_op;
  shift_imm_ = shift_imm & 31;
  am_ = am;
}


// -----------------------------------------------------------------------------
// Specific instructions, constants, and masks.

// add(sp, sp, 4) instruction (aka Pop())
const Instr kPopInstruction =
    al | PostIndex | 4 | LeaveCC | I | kRegister_sp_Code * B16 |
        kRegister_sp_Code * B12;
// str(r, MemOperand(sp, 4, NegPreIndex), al) instruction (aka push(r))
// register r is not encoded.
const Instr kPushRegPattern =
    al | B26 | 4 | NegPreIndex | kRegister_sp_Code * B16;
// ldr(r, MemOperand(sp, 4, PostIndex), al) instruction (aka pop(r))
// register r is not encoded.
const Instr kPopRegPattern =
    al | B26 | L | 4 | PostIndex | kRegister_sp_Code * B16;

// Spare buffer.
static const int kMinimalBufferSize = 4*KB;


Assembler::Assembler(Isolate* arg_isolate, void* buffer, int buffer_size)
    : AssemblerBase(arg_isolate),
      recorded_ast_id_(TypeFeedbackId::None()),
      positions_recorder_(this),
      emit_debug_code_(FLAG_debug_code),
      predictable_code_size_(false) {
  if (buffer == NULL) {
    // Do our own buffer management.
    if (buffer_size <= kMinimalBufferSize) {
      buffer_size = kMinimalBufferSize;

      if (isolate()->assembler_spare_buffer() != NULL) {
        buffer = isolate()->assembler_spare_buffer();
        isolate()->set_assembler_spare_buffer(NULL);
      }
    }
    if (buffer == NULL) {
      buffer_ = NewArray<byte>(buffer_size);
    } else {
      buffer_ = static_cast<byte*>(buffer);
    }
    buffer_size_ = buffer_size;
    own_buffer_ = true;

  } else {
    // Use externally provided buffer instead.
    ASSERT(buffer_size > 0);
    buffer_ = static_cast<byte*>(buffer);
    buffer_size_ = buffer_size;
    own_buffer_ = false;
  }

  // Set up buffer pointers.
  ASSERT(buffer_ != NULL);
  pc_ = buffer_;
  reloc_info_writer.Reposition(buffer_ + buffer_size, pc_);
  num_pending_reloc_info_ = 0;
  next_buffer_check_ = 0;
  const_pool_blocked_nesting_ = 0;
  no_const_pool_before_ = 0;
  first_const_pool_use_ = -1;
  last_bound_pos_ = 0;
  ClearRecordedAstId();
}


Assembler::~Assembler() {
  ASSERT(const_pool_blocked_nesting_ == 0);
  if (own_buffer_) {
    if (isolate()->assembler_spare_buffer() == NULL &&
        buffer_size_ == kMinimalBufferSize) {
      isolate()->set_assembler_spare_buffer(buffer_);
    } else {
      DeleteArray(buffer_);
    }
  }
}


void Assembler::GetCode(CodeDesc* desc) {
  // Emit constant pool if necessary.
  CheckConstPool(true, false);
  ASSERT(num_pending_reloc_info_ == 0);

  // Set up code descriptor.
  desc->buffer = buffer_;
  desc->buffer_size = buffer_size_;
  desc->instr_size = pc_offset();
  desc->reloc_size = (buffer_ + buffer_size_) - reloc_info_writer.pos();
}


void Assembler::Align(int m) {
  ASSERT(m >= 4 && IsPowerOf2(m));
  while ((pc_offset() & (m - 1)) != 0) {
    nop();
  }
}


void Assembler::CodeTargetAlign() {
  // Preferred alignment of jump targets on some ARM chips.
  Align(8);
}


Condition Assembler::GetCondition(Instr instr) {
  switch (instr & kCondMask) {
    case BT:
      return eq;
    case BF:
      return ne;
    default:
      UNIMPLEMENTED();
    }
  return al;
}

// PowerPC

bool Assembler::IsLis(Instr instr) {
  return (instr & ADDIS) == ADDIS;
}

bool Assembler::IsAddic(Instr instr) {
  return (instr & ADDIC) == ADDIC;
}



bool Assembler::IsBranch(Instr instr) {
  return ((instr & kOpcodeMask) == BCX);
}

// end PowerPC

Register Assembler::GetRA(Instr instr) {
  Register reg;
  reg.code_ = Instruction::RAValue(instr);
  return reg;
}

Register Assembler::GetRB(Instr instr) {
  Register reg;
  reg.code_ = Instruction::RBValue(instr);
  return reg;
}

bool Assembler::IsPush(Instr instr) {
  return ((instr & ~kRdMask) == kPushRegPattern);
}


bool Assembler::IsPop(Instr instr) {
  return ((instr & ~kRdMask) == kPopRegPattern);
}

bool Assembler::IsLdrPcImmediateOffset(Instr instr) {
  // Check the instruction is indeed a
  // ldr<cond> <Rd>, [pc +/- offset_12].
  return 1;  /// hack
}

bool Assembler::IsCmpRegister(Instr instr) {
  return (((instr & kOpcodeMask) == EXT2) &&
          ((instr & kExt2OpcodeMask) == CMP));
}

bool Assembler::IsRlwinm(Instr instr) {
  return ((instr & kOpcodeMask) == RLWINMX);
}

bool Assembler::IsCmpImmediate(Instr instr) {
  return ((instr & kOpcodeMask) == CMPI);
}

Register Assembler::GetCmpImmediateRegister(Instr instr) {
  ASSERT(IsCmpImmediate(instr));
  return GetRA(instr);
}

int Assembler::GetCmpImmediateRawImmediate(Instr instr) {
  ASSERT(IsCmpImmediate(instr));
  return instr & kOff16Mask;
}

// Labels refer to positions in the (to be) generated code.
// There are bound, linked, and unused labels.
//
// Bound labels refer to known positions in the already
// generated code. pos() is the position the label refers to.
//
// Linked labels refer to unknown positions in the code
// to be generated; pos() is the position of the last
// instruction using the label.


// The link chain is terminated by a negative code position (must be aligned)
const int kEndOfChain = -4;


int Assembler::target_at(int pos)  {
  Instr instr = instr_at(pos);
  // check which type of branch this is 16 or 26 bit offset
  int opcode = instr & kOpcodeMask;
  if (BX == opcode) {
    int imm26 = ((instr & kImm26Mask) << 6) >> 6;
    imm26 &= ~kLKMask;  // discard LK bit if present
    if (imm26 == 0)
        return kEndOfChain;
    return pos + imm26;
  }
  if (BCX == opcode) {
    int imm16 = SIGN_EXT_IMM16((instr & kImm16Mask));
    imm16 &= ~(kAAMask|kLKMask);  // discard AA|LK bits if present
    if (imm16 == 0)
        return kEndOfChain;
    return pos + imm16;
  }
  PPCPORT_UNIMPLEMENTED();
  ASSERT(false);
  return -1;

#if 0
  // else we fall into the ARM code.. which is busted
  if ((instr & ~kImm26Mask) == 0) {  // todo - handle AA and LK bits if present
    // Emitted label constant, not part of a branch.
    return instr - (Code::kHeaderSize - kHeapObjectTag);
  }
  // ignore for now with mixed branches
  ASSERT((instr & 7*B25) == 5*B25);  // b, bl, or blx imm24
  int imm26 = ((instr & kImm26Mask) << 6) >> 6;
  if ((Instruction::ConditionField(instr) == kSpecialCondition) &&
      ((instr & B24) != 0)) {
    // blx uses bit 24 to encode bit 2 of imm26
    imm26 += 2;
  }
  return pos + imm26;
#endif
}

void Assembler::target_at_put(int pos, int target_pos) {
  Instr instr = instr_at(pos);
  // check which type of branch this is 16 or 26 bit offset
  int opcode = instr & kOpcodeMask;
  if (BX == opcode) {
    int imm26 = target_pos - pos;
    int lk = instr & kLKMask;
    // ASSERT((imm26 & 3) == 0); .. LK may or may not be set
    instr &= ~kImm26Mask;
    ASSERT(is_int26(imm26));
    // todo add AA and LK bits
    instr_at_put(pos, instr | (imm26 & kImm26Mask) | lk);
    return;
  }
  if (BCX == opcode) {
    int imm16 = target_pos - pos;
    ASSERT((imm16 & 3) == 0);
    instr &= ~kImm16Mask;
    ASSERT(is_int16(imm16));
    // todo add AA and LK bits
    instr_at_put(pos, instr | (imm16 & kImm16Mask));
    return;
  }
  ASSERT(false);

#if 0
  // else we fall into the ARM code.. which is busted
  if ((instr & ~kImm26Mask) == 0) {  // todo - handle AA and LK bits
    ASSERT(target_pos == kEndOfChain || target_pos >= 0);
    // Emitted label constant, not part of a branch.
    // Make label relative to Code* of generated Code object.
    instr_at_put(pos, target_pos + (Code::kHeaderSize - kHeapObjectTag));
    return;
  }
  int imm26 = target_pos - pos;
  ASSERT((imm26 & 3) == 0);
  instr &= ~kImm26Mask;
  ASSERT(is_int26(imm26));
  // todo add AA and LK bits
  instr_at_put(pos, instr | (imm26 & kImm26Mask));

  ASSERT((instr & 7*B25) == 5*B25);  // b, bl, or blx imm24
  if (Instruction::ConditionField(instr) == kSpecialCondition) {
    // blx uses bit 24 to encode bit 2 of imm26
    ASSERT((imm26 & 1) == 0);
    instr = (instr & ~(B24 | kImm24Mask)) | ((imm26 & 2) >> 1)*B24;
  } else {
    ASSERT((imm26 & 3) == 0);
    instr &= ~kImm24Mask;
  }
  int imm24 = imm26 >> 2;
  ASSERT(is_int24(imm24));
  instr_at_put(pos, instr | (imm24 & kImm24Mask));
#endif
}

void Assembler::bind_to(Label* L, int pos) {
  ASSERT(0 <= pos && pos <= pc_offset());  // must have a valid binding position
  while (L->is_linked()) {
    int fixup_pos = L->pos();
    next(L);  // call next before overwriting link with target at fixup_pos
    target_at_put(fixup_pos, pos);
  }
  L->bind_to(pos);

  // Keep track of the last bound label so we don't eliminate any instructions
  // before a bound label.
  if (pos > last_bound_pos_)
    last_bound_pos_ = pos;
}


void Assembler::link_to(Label* L, Label* appendix) {
  if (appendix->is_linked()) {
    if (L->is_linked()) {
      // Append appendix to L's list.
      int fixup_pos;
      int link = L->pos();
      do {
        fixup_pos = link;
        link = target_at(fixup_pos);
      } while (link > 0);
      ASSERT(link == kEndOfChain);
      target_at_put(fixup_pos, appendix->pos());
    } else {
      // L is empty, simply use appendix.
      *L = *appendix;
    }
  }
  appendix->Unuse();  // appendix should not be used anymore
}


void Assembler::bind(Label* L) {
  ASSERT(!L->is_bound());  // label can only be bound once
  bind_to(L, pc_offset());
}


void Assembler::next(Label* L) {
  ASSERT(L->is_linked());
  int link = target_at(L->pos());
  if (link == kEndOfChain) {
    L->Unuse();
  } else {
    ASSERT(link >= 0);
    L->link_to(link);
  }
}

// We have to use the temporary register for things that can be relocated even
// if they can be encoded in the ARM's 12 bits of immediate-offset instruction
// space.  There is no guarantee that the relocated location can be similarly
// encoded.
bool Operand::must_use_constant_pool(const Assembler* assembler) const {
  if (rmode_ == RelocInfo::EXTERNAL_REFERENCE) {
#ifdef DEBUG
    if (!Serializer::enabled()) {
      Serializer::TooLateToEnableNow();
    }
#endif  // def DEBUG
    if (assembler != NULL && assembler->predictable_code_size()) return true;
    return Serializer::enabled();
  } else if (rmode_ == RelocInfo::NONE) {
    return false;
  }
  return true;
}

void Assembler::a_form(Instr instr,
                       DwVfpRegister frt,
                       DwVfpRegister fra,
                       DwVfpRegister frb,
                       RCBit r) {
  CheckBuffer();
  emit(instr | frt.code()*B21 | fra.code()*B16 | frb.code()*B11 | r);
}

void Assembler::d_form(Instr instr,
                        Register rt,
                        Register ra,
                        const int val,
                        bool signed_disp) {
  CheckBuffer();
#ifdef NEW_IMM_CHECK_CODE
  if (signed_disp) {
    if (!is_int16(val)) {
      PrintF("val = %d, 0x%x\n", val, val);
    }
    ASSERT(is_int16(val));
  } else {
    if (!is_uint16(val)) {
      PrintF("val = %d, 0x%x, is_unsigned_imm16(val)=%d, kImm16Mask=0x%x\n",
             val, val, is_uint16(val), kImm16Mask);
    }
    ASSERT(is_uint16(val));
  }
#endif
  emit(instr | rt.code()*B21 | ra.code()*B16 | (kImm16Mask & val));
}

void Assembler::x_form(Instr instr,
                         Register ra,
                         Register rs,
                         Register rb,
                         RCBit r) {
  CheckBuffer();
  emit(instr | rs.code()*B21 | ra.code()*B16 | rb.code()*B11 | r);
}

void Assembler::xo_form(Instr instr,
                         Register rt,
                         Register ra,
                         Register rb,
                         OEBit o,
                         RCBit r) {
  CheckBuffer();
  emit(instr | rt.code()*B21 | ra.code()*B16 | rb.code()*B11 | o | r);
}

int Assembler::branch_offset(Label* L, bool jump_elimination_allowed) {
  int target_pos;
  if (L->is_bound()) {
    target_pos = L->pos();
  } else {
    if (L->is_linked()) {
      target_pos = L->pos();  // L's link
    } else {
      // was: target_pos = kEndOfChain;
      // However, using branch to self to mark the first reference
      // should avoid most instances of branch offset overflow.  See
      // target_at() for where this is converted back to kEndOfChain.
      target_pos = pc_offset();
    }
    L->link_to(pc_offset());
  }

  // Block the emission of the constant pool, since the branch instruction must
  // be emitted at the pc offset recorded by the label.
  BlockConstPoolFor(1);
  return target_pos - pc_offset();
}


void Assembler::label_at_put(Label* L, int at_offset) {
  int target_pos;
  if (L->is_bound()) {
    target_pos = L->pos();
  } else {
    if (L->is_linked()) {
      target_pos = L->pos();  // L's link
    } else {
      target_pos = kEndOfChain;
    }
    L->link_to(at_offset);
    instr_at_put(at_offset, target_pos + (Code::kHeaderSize - kHeapObjectTag));
  }
}


// Branch instructions.

// PowerPC
void Assembler::bclr(BOfield bo, LKBit lk) {
  positions_recorder()->WriteRecordedPositions();
  emit(EXT1 | bo | BCLRX | lk);
}

void Assembler::bcctr(BOfield bo, LKBit lk) {
  positions_recorder()->WriteRecordedPositions();
  emit(EXT1 | bo | BCCTRX | lk);
}

// Pseudo op - branch to link register
void Assembler::blr() {
  bclr(BA, LeaveLK);
}

// Pseudo op - branch to count register -- used for "jump"
void Assembler::bcr() {
  bcctr(BA, LeaveLK);
}

void Assembler::bc(int branch_offset, BOfield bo, int condition_bit) {
  positions_recorder()->WriteRecordedPositions();
#ifndef NEW_IMM_CHECK_CODE
  // currently assumes AA and LK are always zero
  ASSERT(is_int16(branch_offset) || is_uint16(branch_offset));
#else
  ASSERT(is_int16(branch_offset));
#endif
  emit(BCX | bo | condition_bit*B16 | (kImm16Mask & branch_offset));
}

void Assembler::b(int branch_offset, LKBit lk) {
  positions_recorder()->WriteRecordedPositions();
  ASSERT((branch_offset & 3) == 0);
  int imm26 = branch_offset;
  ASSERT(is_int26(imm26));
  // todo add AA and LK bits
  emit(BX | (imm26 & kImm26Mask) | lk);
}

void Assembler::b(int branch_offset, Condition cond) {
  b(branch_offset, LeaveLK);
}


void Assembler::bl(int branch_offset, Condition cond) {
  b(branch_offset, SetLK);
}

// end PowerPC

// Data-processing instructions.

void Assembler::and_(Register dst, Register src1, const Operand& src2,
                     SBit s, Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fAND);
}


void Assembler::eor(Register dst, Register src1, const Operand& src2,
                    SBit s, Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fEOR);
}

void Assembler::rsb(Register dst, Register src1, const Operand& src2,
                    SBit s, Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fRSB);
}

// PowerPC

void Assembler::xori(Register dst, Register src, const Operand& imm) {
  d_form(XORI, dst, src, imm.imm32_, false);
}

void Assembler::xoris(Register ra, Register rs, const Operand& imm) {
#ifndef NEW_IMM_CHECK_CODE
  int imm16 = imm.imm32_;
  ASSERT(is_int16(imm16) || is_uint16(imm16));
  imm16 &= kImm16Mask;
  d_form(XORIS, rs, ra, imm16, false);
#else
  d_form(XORIS, rs, ra, imm.imm32_, false);
#endif
}

void Assembler::xor_(Register dst, Register src1, Register src2, RCBit rc) {
  x_form(EXT2 | XORX, dst, src1, src2, rc);
}

void Assembler::cntlzw_(Register ra, Register rs, RCBit rc) {
  x_form(EXT2 | CNTLZWX, ra, rs, r0, rc);
}

void Assembler::and_(Register ra, Register rs, Register rb, RCBit rc) {
  x_form(EXT2 | ANDX, ra, rs, rb, rc);
}


void Assembler::rlwinm(Register ra, Register rs,
                       int sh, int mb, int me, RCBit rc) {
  sh &= 0x1f;
  mb &= 0x1f;
  me &= 0x1f;
  CheckBuffer();
  emit(RLWINMX | rs.code()*B21 | ra.code()*B16 | sh*B11 | mb*B6 | me << 1 | rc);
}

void Assembler::rlwimi(Register ra, Register rs,
                       int sh, int mb, int me, RCBit rc) {
  sh &= 0x1f;
  mb &= 0x1f;
  me &= 0x1f;
  CheckBuffer();
  emit(RLWIMIX | rs.code()*B21 | ra.code()*B16 | sh*B11 | mb*B6 | me << 1 | rc);
}

void Assembler::slwi(Register dst, Register src, const Operand& val) {
  ASSERT((32 > val.imm32_)&&(val.imm32_ >= 0));
  rlwinm(dst, src, val.imm32_, 0, 31-val.imm32_);
}
void Assembler::srwi(Register dst, Register src, const Operand& val) {
  ASSERT((32 > val.imm32_)&&(val.imm32_ >= 0));
  rlwinm(dst, src, 32-val.imm32_, val.imm32_, 31);
}
void Assembler::srawi(Register ra, Register rs, int sh, RCBit r) {
  CheckBuffer();
  emit(EXT2 | SRAWIX | rs.code()*B21 | ra.code()*B16 | sh*B11 | r);
}

void Assembler::srw(Register dst, Register src1, Register src2, RCBit r) {
  x_form(EXT2 | SRWX, dst, src1, src2, r);
}

void Assembler::slw(Register dst, Register src1, Register src2, RCBit r) {
  x_form(EXT2 | SLWX, dst, src1, src2, r);
}

void Assembler::sraw(Register ra, Register rs, Register rb, RCBit r) {
  x_form(EXT2 | SRAW, ra, rs, rb, r);
}

// delete this later when removing ARM
void Assembler::sub(Register dst, Register src, const Operand& imm,
    SBit s, Condition cond) {
  addi(dst, src, Operand(-(imm.imm32_)));
}

void Assembler::addc(Register dst, Register src1, Register src2,
                    OEBit o, RCBit r) {
  xo_form(EXT2 | ADDCX, dst, src1, src2, o, r);
}

void Assembler::addze(Register dst, Register src1, OEBit o, RCBit r) {
  // a special xo_form
  CheckBuffer();
  emit(EXT2 | ADDZEX | dst.code()*B21 | src1.code()*B16 | o | r);
}

void Assembler::sub(Register dst, Register src1, Register src2,
                    OEBit o, RCBit r) {
  xo_form(EXT2 | SUBFX, dst, src2, src1, o, r);
}

void Assembler::subfc(Register dst, Register src1, Register src2,
                    OEBit o, RCBit r) {
  xo_form(EXT2 | SUBFCX, dst, src2, src1, o, r);
}

void Assembler::subfic(Register dst, Register src, const Operand& imm) {
  d_form(SUBFIC, dst, src, imm.imm32_, true);
}

void Assembler::add(Register dst, Register src1, Register src2,
                    OEBit o, RCBit r) {
  xo_form(EXT2 | ADDX, dst, src1, src2, o, r);
}

// Multiply low word
void Assembler::mullw(Register dst, Register src1, Register src2,
                    OEBit o, RCBit r) {
  xo_form(EXT2 | MULLW, dst, src1, src2, o, r);
}

// Multiply hi word
void Assembler::mulhw(Register dst, Register src1, Register src2,
                    OEBit o, RCBit r) {
  xo_form(EXT2 | MULHWX, dst, src1, src2, o, r);
}

void Assembler::addi(Register dst, Register src, const Operand& imm) {
  ASSERT(!src.is(r0));  // use li instead to show intent
  d_form(ADDI, dst, src, imm.imm32_, true);
}

void  Assembler::addis(Register dst, Register src, const Operand& imm) {
  ASSERT(!src.is(r0));  // use lis instead to show intent
  d_form(ADDIS, dst, src, imm.imm32_, true);
}

void Assembler::addic(Register dst, Register src, const Operand& imm) {
  d_form(ADDIC, dst, src, imm.imm32_, true);
}

void  Assembler::andi(Register ra, Register rs, const Operand& imm) {
#ifndef NEW_IMM_CHECK_CODE
  int imm16 = imm.imm32_;
  ASSERT(is_int16(imm16));
  imm16 &= kImm16Mask;
  d_form(ANDIx, rs, ra, imm16, false);
#else
  d_form(ANDIx, rs, ra, imm.imm32_, false);
#endif
}

void Assembler::andis(Register ra, Register rs, const Operand& imm) {
#ifndef NEW_IMM_CHECK_CODE
  int imm16 = imm.imm32_;
  ASSERT(is_int16(imm16) || is_uint16(imm16));
  imm16 &= kImm16Mask;
  d_form(ANDISx, rs, ra, imm16, false);
#else
  d_form(ANDISx, rs, ra, imm.imm32_, false);
#endif
}

void Assembler::nor(Register dst, Register src1, Register src2, RCBit r) {
  x_form(EXT2 | NORX, dst, src1, src2, r);
}

void Assembler::notx(Register dst, Register src, RCBit r) {
  x_form(EXT2 | NORX, dst, src, src, r);
}

void Assembler::ori(Register ra, Register rs, const Operand& imm) {
  d_form(ORI, ra, rs, imm.imm32_, false);
}

void Assembler::oris(Register dst, Register src, const Operand& imm) {
  d_form(ORIS, dst, src, imm.imm32_, false);
}

void Assembler::orx(Register dst, Register src1, Register src2, RCBit r) {
  x_form(EXT2 | ORX, dst, src1, src2, r);
}

void Assembler::cmpi(Register src1, const Operand& src2, CRegister cr) {
  int imm16 = src2.imm32_;
  ASSERT(is_int16(imm16));
  ASSERT(cr.code() >= 0 && cr.code() <= 7);
  imm16 &= kImm16Mask;
  emit(CMPI | cr.code()*B23 | src1.code()*B16 | imm16);
}

void Assembler::cmpli(Register src1, const Operand& src2, CRegister cr) {
  uint uimm16 = src2.imm32_;
  ASSERT(is_uint16(uimm16));
  ASSERT(cr.code() >= 0 && cr.code() <= 7);
  uimm16 &= kImm16Mask;
  emit(CMPLI | cr.code()*B23 | src1.code()*B16 | uimm16);
}

void Assembler::cmp(Register src1, Register src2, CRegister cr) {
  ASSERT(cr.code() >= 0 && cr.code() <= 7);
  emit(EXT2 | CMP | cr.code()*B23 | src1.code()*B16 | src2.code()*B11);
}

void Assembler::cmpl(Register src1, Register src2, CRegister cr) {
  ASSERT(cr.code() >= 0 && cr.code() <= 7);
  emit(EXT2 | CMPL | cr.code()*B23 | src1.code()*B16 | src2.code()*B11);
}

// Pseudo op - load immediate
void Assembler::li(Register dst, const Operand &imm) {
  // this should only be signed 16bit values, the uint16 is a hack for now
  // it may not work correctly on an actual PowerPC
#ifndef NEW_IMM_CHECK_CODE
  ASSERT(is_int16(imm.imm32_) || is_uint16(imm.imm32_));
#endif
  d_form(ADDI, dst, r0, imm.imm32_, true);
}

void  Assembler::lis(Register dst, const Operand& imm) {
#ifndef NEW_IMM_CHECK_CODE
  int imm16 = imm.imm32_;
  ASSERT(is_int16(imm16) || is_uint16(imm16));
  imm16 &= kImm16Mask;
  d_form(ADDIS, dst, r0, imm16, true);
#else
  d_form(ADDIS, dst, r0, imm.imm32_, true);
#endif
}

// Pseudo op - move register
void Assembler::mr(Register dst, Register src) {
  // actually or(dst, src, src)
  orx(dst, src, src);
}

void Assembler::lbz(Register dst, const MemOperand &src) {
  ASSERT(!src.ra_.is(r0));
  d_form(LBZ, dst, src.ra(), src.offset(), true);
}

void Assembler::lhz(Register dst, const MemOperand &src) {
  ASSERT(!src.ra_.is(r0));
  d_form(LHZ, dst, src.ra(), src.offset(), true);
}

void Assembler::lwz(Register dst, const MemOperand &src) {
  ASSERT(!src.ra_.is(r0));
  d_form(LWZ, dst, src.ra(), src.offset(), true);
}

void Assembler::lwzu(Register dst, const MemOperand &src) {
  ASSERT(!src.ra_.is(r0));
  d_form(LWZU, dst, src.ra(), src.offset(), true);
}

void Assembler::lwzx(Register rt, Register ra, Register rb) {
  ASSERT(!ra.is(r0));
  emit(EXT2 | LWZX | rt.code()*B21 | ra.code()*B16 | rb.code()*B11 | LeaveRC);
}

void Assembler::lwzux(Register rt, Register ra, Register rb) {
  ASSERT(!ra.is(r0));
  emit(EXT2 | LWZUX | rt.code()*B21 | ra.code()*B16 | rb.code()*B11 | LeaveRC);
}

void Assembler::stb(Register dst, const MemOperand &src) {
  ASSERT(!src.ra_.is(r0));
  d_form(STB, dst, src.ra(), src.offset(), true);
}

void Assembler::sth(Register dst, const MemOperand &src) {
  ASSERT(!src.ra_.is(r0));
  d_form(STH, dst, src.ra(), src.offset(), true);
}

void Assembler::stw(Register dst, const MemOperand &src) {
  ASSERT(!src.ra_.is(r0));
  d_form(STW, dst, src.ra(), src.offset(), true);
}

void Assembler::stwu(Register dst, const MemOperand &src) {
  ASSERT(!src.ra_.is(r0));
  d_form(STWU, dst, src.ra(), src.offset(), true);
}

void Assembler::stwx(Register rs, Register ra, Register rb) {
  ASSERT(!ra.is(r0));
  emit(EXT2 | STWX | rs.code()*B21 | ra.code()*B16 | rb.code()*B11 | LeaveRC);
}

void Assembler::stwux(Register rs, Register ra, Register rb) {
  ASSERT(!ra.is(r0));
  emit(EXT2 | STWUX | rs.code()*B21 | ra.code()*B16 | rb.code()*B11 | LeaveRC);
}

void Assembler::extsb(Register rs, Register ra, RCBit rc) {
  CheckBuffer();
  emit(EXT2 | EXTSB | rs.code()*B21 | ra.code()*B16 | rc);
}

void Assembler::extsh(Register rs, Register ra, RCBit rc) {
  CheckBuffer();
  emit(EXT2 | EXTSH | rs.code()*B21 | ra.code()*B16 | rc);
}

void Assembler::neg(Register rt, Register ra, RCBit rc) {
  CheckBuffer();
  emit(EXT2 | NEGX| rt.code()*B21 | ra.code()*B16 | rc);
}

void Assembler::andc(Register dst, Register src1, Register src2, RCBit rc) {
  x_form(EXT2 | ANDCX, dst, src1, src2, rc);
}

void Assembler::fake_asm(enum FAKE_OPCODE_T fopcode) {
  ASSERT(fopcode < fLastFaker);
  emit(FAKE_OPCODE | FAKER_SUBOPCODE | fopcode);
}

void Assembler::marker_asm(int mcode) {
  if (::v8::internal::FLAG_trace_sim_stubs) {
    ASSERT(mcode < F_NEXT_AVAILABLE_STUB_MARKER);
    emit(FAKE_OPCODE | MARKER_SUBOPCODE | mcode);
  }
}
// end PowerPC

void Assembler::add(Register dst, Register src1, const Operand& src2,
                    SBit s, Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fADD);
}

void Assembler::adc(Register dst, Register src1, const Operand& src2,
                    SBit s, Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fADC);
}


void Assembler::sbc(Register dst, Register src1, const Operand& src2,
                    SBit s, Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fSBC);
}


void Assembler::rsc(Register dst, Register src1, const Operand& src2,
                    SBit s, Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fRSC);
}


void Assembler::tst(Register src1, const Operand& src2, Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fTST);
}


void Assembler::teq(Register src1, const Operand& src2, Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fTEQ);
}


void Assembler::cmp(Register src1, const Operand& src2, Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fCMP);
}

void Assembler::cmn(Register src1, const Operand& src2, Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fCMN);
}


void Assembler::orr(Register dst, Register src1, const Operand& src2,
                    SBit s, Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fORR);
}

// This should really move to be in macro-assembler as it
// is really a pseudo instruction
// ARM stuff, remove later
void Assembler::mov(Register dst, const Operand& src, SBit s, Condition cond) {
  // Primarily used on PPC for loading constants

#if 0
  // hacky for ARM
  if (src.is_reg()) {
  // Don't allow nop instructions in the form mov rn, rn to be generated using
  // the mov instruction. They must be generated using nop(int/NopMarkerTypes)
  // or MarkCode(int/NopMarkerTypes) pseudo instructions.
  ASSERT(!(src.is_reg() && src.rm().is(dst) && s == LeaveCC && cond == al));
  addrmod1(cond | MOV | s, r0, dst, src);
  }
#endif

  if (src.rmode_ != RelocInfo::NONE) {
    // some form of relocation needed
    RecordRelocInfo(src.rmode_, src.imm32_);
  }

  int value = src.imm32_;
  int hi_word = static_cast<int>(value) >> 16;
  int lo_word = SIGN_EXT_IMM16(value);
  if (lo_word & 0x8000) {
    // lo word is signed, so increment hi word by one
    hi_word++;
  }
  // ASSERT(dst.code() != 0);  // r0 is invalid destination eee
  BlockConstPoolFor(2);  // don't split these
  lis(dst, Operand(SIGN_EXT_IMM16(hi_word)));
  addic(dst, dst, Operand(lo_word));
}

void Assembler::bic(Register dst, Register src1, const Operand& src2,
                    SBit s, Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fBIC);
}


void Assembler::mvn(Register dst, const Operand& src, SBit s, Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fMVN);
}


// Multiply instructions.
void Assembler::mla(Register dst, Register src1, Register src2, Register srcA,
                    SBit s, Condition cond) {
  emit(cond | A | s | dst.code()*B16 | srcA.code()*B12 |
       src2.code()*B8 | B7 | B4 | src1.code());
}


// PowerPC
void Assembler::mul(Register dst, Register src1, Register src2,
                    OEBit o, RCBit r) {
  xo_form(EXT2 | MULLW, dst, src1, src2, o, r);
}
// end PowerPC

void Assembler::smlal(Register dstL,
                      Register dstH,
                      Register src1,
                      Register src2,
                      SBit s,
                      Condition cond) {
  ASSERT(!dstL.is(dstH));
  emit(cond | B23 | B22 | A | s | dstH.code()*B16 | dstL.code()*B12 |
       src2.code()*B8 | B7 | B4 | src1.code());
}


void Assembler::smull(Register dstL,
                      Register dstH,
                      Register src1,
                      Register src2,
                      SBit s,
                      Condition cond) {
  ASSERT(!dstL.is(dstH));
  emit(cond | B23 | B22 | s | dstH.code()*B16 | dstL.code()*B12 |
       src2.code()*B8 | B7 | B4 | src1.code());
}


void Assembler::umlal(Register dstL,
                      Register dstH,
                      Register src1,
                      Register src2,
                      SBit s,
                      Condition cond) {
  ASSERT(!dstL.is(dstH));
  emit(cond | B23 | A | s | dstH.code()*B16 | dstL.code()*B12 |
       src2.code()*B8 | B7 | B4 | src1.code());
}


void Assembler::umull(Register dstL,
                      Register dstH,
                      Register src1,
                      Register src2,
                      SBit s,
                      Condition cond) {
  ASSERT(!dstL.is(dstH));
  emit(cond | B23 | s | dstH.code()*B16 | dstL.code()*B12 |
       src2.code()*B8 | B7 | B4 | src1.code());
}

// Saturating instructions.

// Unsigned saturate.
void Assembler::usat(Register dst,
                     int satpos,
                     const Operand& src,
                     Condition cond) {
  // v6 and above.
  ASSERT(CpuFeatures::IsSupported(ARMv7));
  ASSERT((satpos >= 0) && (satpos <= 31));
  ASSERT((src.shift_op_ == ASR) || (src.shift_op_ == LSL));
  ASSERT(src.rs_.is(no_reg));

  int sh = 0;
  if (src.shift_op_ == ASR) {
      sh = 1;
  }

  emit(cond | 0x6*B24 | 0xe*B20 | satpos*B16 | dst.code()*B12 |
       src.shift_imm_*B7 | sh*B6 | 0x1*B4 | src.rm_.code());
}


// Bitfield manipulation instructions.

// Unsigned bit field extract.
// Extracts #width adjacent bits from position #lsb in a register, and
// writes them to the low bits of a destination register.
//   ubfx dst, src, #lsb, #width
void Assembler::ubfx(Register dst,
                     Register src,
                     int lsb,
                     int width,
                     Condition cond) {
  // v7 and above.
  ASSERT(CpuFeatures::IsSupported(ARMv7));
  ASSERT((lsb >= 0) && (lsb <= 31));
  ASSERT((width >= 1) && (width <= (32 - lsb)));
  emit(cond | 0xf*B23 | B22 | B21 | (width - 1)*B16 | dst.code()*B12 |
       lsb*B7 | B6 | B4 | src.code());
}

// Special register instructions
// PowerPC
void Assembler::crxor(int bt, int ba, int bb) {
  emit(EXT1 | CRXOR | bt*B21 | ba*B16 | bb*B11);
}

void Assembler::mflr(Register dst) {
  emit(EXT2 | MFSPR | dst.code()*B21 | 256 << 11);   // Ignore RC bit
}

void Assembler::mtlr(Register src) {
  emit(EXT2 | MTSPR | src.code()*B21 | 256 << 11);   // Ignore RC bit
}

void Assembler::mtctr(Register src) {
  emit(EXT2 | MTSPR | src.code()*B21 | 288 << 11);   // Ignore RC bit
}

void Assembler::mcrfs(int bf, int bfa) {
  emit(EXT4 | MCRFS | bf*B23 | bfa*B18);
}
// end PowerPC

// Status register access instructions.
void Assembler::mrs(Register dst, SRegister s, Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fMRS);
}


void Assembler::msr(SRegisterFieldMask fields, const Operand& src,
                    Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fMSR);
}


// Load/Store instructions.
void Assembler::ldr(Register dst, const MemOperand& src, Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fLDR);
}


void Assembler::str(Register src, const MemOperand& dst, Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fSTR);
}


void Assembler::ldrb(Register dst, const MemOperand& src, Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fLDRB);
}


void Assembler::strb(Register src, const MemOperand& dst, Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fSTRB);
}


void Assembler::ldrh(Register dst, const MemOperand& src, Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fLDRH);
}


void Assembler::strh(Register src, const MemOperand& dst, Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fSTRH);
}


void Assembler::ldrsb(Register dst, const MemOperand& src, Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fLDRSB);
}


void Assembler::ldrsh(Register dst, const MemOperand& src, Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fLDRSH);
}


void Assembler::ldrd(Register dst1, Register dst2,
                     const MemOperand& src, Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fLDRD);
}


void Assembler::strd(Register src1, Register src2,
                     const MemOperand& dst, Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fSTRD);
}

// Load/Store multiple instructions.
void Assembler::ldm(BlockAddrMode am,
                    Register base,
                    RegList dst,
                    Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fLDM);
}


void Assembler::stm(BlockAddrMode am,
                    Register base,
                    RegList src,
                    Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fSTM);
}


// Exception-generating instructions and debugging support.
// Stops with a non-negative code less than kNumOfWatchedStops support
// enabling/disabling and a counter feature. See simulator-arm.h .
void Assembler::stop(const char* msg, Condition cond, int32_t code) {
  // PPCPORT_CHECK(false);
  // EMIT_FAKE_ARM_INSTR(fSTOP);
  if (cond != al) {
    Label skip;
    b(&skip, NegateCondition(cond));
    bkpt(0);
    bind(&skip);
  } else {
    bkpt(0);
  }
}


void Assembler::bkpt(uint32_t imm16) {
  emit(0x7d821008);
#if 0
  // PPCPORT_CHECK(false);
  ASSERT(is_uint16(imm16));
  // only supported in simulator to trigger debugging
  EMIT_FAKE_ARM_INSTR(fBKPT);
#endif
}


void Assembler::svc(uint32_t imm24, Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fSVC);
}

// Floating point support

void Assembler::lfd(const DwVfpRegister frt, const MemOperand &src) {
  int offset = src.offset();
  Register ra = src.ra();
  ASSERT(is_int16(offset));
  int imm16 = offset & kImm16Mask;
  // could be x_form instruction with some casting magic
  emit(LFD | frt.code()*B21 | ra.code()*B16 | imm16);
}

void Assembler::lfs(const DwVfpRegister frt, const MemOperand &src) {
  int offset = src.offset();
  Register ra = src.ra();
  ASSERT(is_int16(offset));
  ASSERT(!ra.is(r0));
  int imm16 = offset & kImm16Mask;
  // could be x_form instruction with some casting magic
  emit(LFS | frt.code()*B21 | ra.code()*B16 | imm16);
}

void Assembler::stfd(const DwVfpRegister frs, const MemOperand &src) {
  int offset = src.offset();
  Register ra = src.ra();
  ASSERT(is_int16(offset));
  ASSERT(!ra.is(r0));
  int imm16 = offset & kImm16Mask;
  // could be x_form instruction with some casting magic
  emit(STFD | frs.code()*B21 | ra.code()*B16 | imm16);
}

void Assembler::stfs(const DwVfpRegister frs, const MemOperand &src) {
  int offset = src.offset();
  Register ra = src.ra();
  ASSERT(is_int16(offset));
  ASSERT(!ra.is(r0));
  int imm16 = offset & kImm16Mask;
  // could be x_form instruction with some casting magic
  emit(STFS | frs.code()*B21 | ra.code()*B16 | imm16);
}

void Assembler::fsub(const DwVfpRegister frt,
                     const DwVfpRegister fra,
                     const DwVfpRegister frb,
                     RCBit rc) {
  a_form(EXT4 | FSUB, frt, fra, frb, rc);
}

void Assembler::fadd(const DwVfpRegister frt,
                     const DwVfpRegister fra,
                     const DwVfpRegister frb,
                     RCBit rc) {
  a_form(EXT4 | FADD, frt, fra, frb, rc);
}
void Assembler::fmul(const DwVfpRegister frt,
                     const DwVfpRegister fra,
                     const DwVfpRegister frc,
                     RCBit rc) {
  CheckBuffer();
  emit(EXT4 | FMUL | frt.code()*B21 | fra.code()*B16 | frc.code()*B6 | rc);
}
void Assembler::fdiv(const DwVfpRegister frt,
                     const DwVfpRegister fra,
                     const DwVfpRegister frb,
                     RCBit rc) {
  a_form(EXT4 | FDIV, frt, fra, frb, rc);
}

void Assembler::fcmpu(const DwVfpRegister fra,
                      const DwVfpRegister frb,
                      CRegister cr) {
  CheckBuffer();
  ASSERT(cr.code() >= 0 && cr.code() <= 7);
  emit(EXT4 | FCMPU | cr.code()*B23 | fra.code()*B16 | frb.code()*B11);
}

void Assembler::fmr(const DwVfpRegister frt,
                     const DwVfpRegister frb) {
  CheckBuffer();
  emit(EXT4 | FMR | frt.code()*B21 | frb.code()*B11);
}

void Assembler::fctiwz(const DwVfpRegister frt,
                     const DwVfpRegister frb) {
  CheckBuffer();
  emit(EXT4 | FCTIWZ | frt.code()*B21 | frb.code()*B11);
}

void Assembler::frim(const DwVfpRegister frt,
                     const DwVfpRegister frb) {
  CheckBuffer();
  emit(EXT4 | FRIM | frt.code()*B21 | frb.code()*B11);
}

void Assembler::frsp(const DwVfpRegister frt,
                     const DwVfpRegister frb,
                     RCBit rc) {
  CheckBuffer();
  emit(EXT4 | FRSP | frt.code()*B21 | frb.code()*B11 | rc);
}

void Assembler::fcfid(const DwVfpRegister frt,
                      const DwVfpRegister frb,
                      RCBit rc) {
  CheckBuffer();
  emit(EXT4 | FCFID | frt.code()*B21 | frb.code()*B11 | rc);
}

// Support for VFP.

void Assembler::vldr(const DwVfpRegister dst,
                     const Register base,
                     int offset,
                     const Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fVLDR);
}


void Assembler::vldr(const DwVfpRegister dst,
                     const MemOperand& operand,
                     const Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fVLDR);
}


void Assembler::vldr(const SwVfpRegister dst,
                     const Register base,
                     int offset,
                     const Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fVLDR);
}


void Assembler::vldr(const SwVfpRegister dst,
                     const MemOperand& operand,
                     const Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fVLDR);
}


void Assembler::vstr(const DwVfpRegister src,
                     const Register base,
                     int offset,
                     const Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fVSTR);
}


void Assembler::vstr(const DwVfpRegister src,
                     const MemOperand& operand,
                     const Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fVSTR);
}


void Assembler::vstr(const SwVfpRegister src,
                     const Register base,
                     int offset,
                     const Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fVSTR);
}


void Assembler::vstr(const SwVfpRegister src,
                     const MemOperand& operand,
                     const Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fVSTR);
}

void Assembler::vmov(const DwVfpRegister dst,
                     double imm,
                     const Register scratch,
                     const Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fVMOV);
}


void Assembler::vmov(const SwVfpRegister dst,
                     const SwVfpRegister src,
                     const Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fVMOV);
}


void Assembler::vmov(const DwVfpRegister dst,
                     const DwVfpRegister src,
                     const Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fVMOV);
}


void Assembler::vmov(const DwVfpRegister dst,
                     const Register src1,
                     const Register src2,
                     const Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fVMOV);
}


void Assembler::vmov(const Register dst1,
                     const Register dst2,
                     const DwVfpRegister src,
                     const Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fVMOV);
}


void Assembler::vmov(const SwVfpRegister dst,
                     const Register src,
                     const Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fVMOV);
}


void Assembler::vmov(const Register dst,
                     const SwVfpRegister src,
                     const Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fVMOV);
}


// Type of data to read from or write to VFP register.
// Used as specifier in generic vcvt instruction.
enum VFPType { S32, U32, F32, F64 };

void Assembler::vneg(const DwVfpRegister dst,
                     const DwVfpRegister src,
                     const Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fVNEG);
}


void Assembler::vabs(const DwVfpRegister dst,
                     const DwVfpRegister src,
                     const Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fVABS);
}


void Assembler::vadd(const DwVfpRegister dst,
                     const DwVfpRegister src1,
                     const DwVfpRegister src2,
                     const Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fVADD);
}


void Assembler::vsub(const DwVfpRegister dst,
                     const DwVfpRegister src1,
                     const DwVfpRegister src2,
                     const Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fVSUB);
}


void Assembler::vmul(const DwVfpRegister dst,
                     const DwVfpRegister src1,
                     const DwVfpRegister src2,
                     const Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fVMUL);
}


void Assembler::vdiv(const DwVfpRegister dst,
                     const DwVfpRegister src1,
                     const DwVfpRegister src2,
                     const Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fVDIV);
}


void Assembler::vcmp(const DwVfpRegister src1,
                     const DwVfpRegister src2,
                     const Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fVCMP);
}


void Assembler::vcmp(const DwVfpRegister src1,
                     const double src2,
                     const Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fVCMP);
}


void Assembler::vmsr(Register dst, Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fVMSR);
}


void Assembler::vmrs(Register dst, Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fVMRS);
}


void Assembler::vsqrt(const DwVfpRegister dst,
                      const DwVfpRegister src,
                      const Condition cond) {
  PPCPORT_CHECK(false);
  EMIT_FAKE_ARM_INSTR(fVSQRT);
}


// Pseudo instructions.
void Assembler::nop(int type) {
  switch (type) {
    case 0:
      ori(r0, r0, Operand(0));
      break;
    case DEBUG_BREAK_NOP:
      ori(r3, r3, Operand(0));
      break;
    default:
      UNIMPLEMENTED();
  }
}


bool Assembler::IsNop(Instr instr, int type) {
  ASSERT((0 == type) || (DEBUG_BREAK_NOP == type));
  int reg = 0;
  if (DEBUG_BREAK_NOP == type) {
    reg = 3;
  }
  return instr == (ORI | reg*B21 | reg*B16);
}

// Debugging.
void Assembler::RecordJSReturn() {
  positions_recorder()->WriteRecordedPositions();
  CheckBuffer();
  RecordRelocInfo(RelocInfo::JS_RETURN);
}


void Assembler::RecordDebugBreakSlot() {
  positions_recorder()->WriteRecordedPositions();
  CheckBuffer();
  RecordRelocInfo(RelocInfo::DEBUG_BREAK_SLOT);
}


void Assembler::RecordComment(const char* msg) {
  if (FLAG_code_comments) {
    CheckBuffer();
    RecordRelocInfo(RelocInfo::COMMENT, reinterpret_cast<intptr_t>(msg));
  }
}


void Assembler::RecordConstPool(int size) {
  // We only need this for debugger support, to correctly compute offsets in the
  // code.
#ifdef ENABLE_DEBUGGER_SUPPORT
  RecordRelocInfo(RelocInfo::CONST_POOL, static_cast<intptr_t>(size));
#endif
}

void Assembler::GrowBuffer() {
  if (!own_buffer_) FATAL("external code buffer is too small");

  // Compute new buffer size.
  CodeDesc desc;  // the new buffer
  if (buffer_size_ < 4*KB) {
    desc.buffer_size = 4*KB;
  } else if (buffer_size_ < 1*MB) {
    desc.buffer_size = 2*buffer_size_;
  } else {
    desc.buffer_size = buffer_size_ + 1*MB;
  }
  CHECK_GT(desc.buffer_size, 0);  // no overflow

  // Set up new buffer.
  desc.buffer = NewArray<byte>(desc.buffer_size);

  desc.instr_size = pc_offset();
  desc.reloc_size = (buffer_ + buffer_size_) - reloc_info_writer.pos();

  // Copy the data.
  int pc_delta = desc.buffer - buffer_;
  int rc_delta = (desc.buffer + desc.buffer_size) - (buffer_ + buffer_size_);
  memmove(desc.buffer, buffer_, desc.instr_size);
  memmove(reloc_info_writer.pos() + rc_delta,
          reloc_info_writer.pos(), desc.reloc_size);

  // Switch buffers.
  DeleteArray(buffer_);
  buffer_ = desc.buffer;
  buffer_size_ = desc.buffer_size;
  pc_ += pc_delta;
  reloc_info_writer.Reposition(reloc_info_writer.pos() + rc_delta,
                               reloc_info_writer.last_pc() + pc_delta);

  // None of our relocation types are pc relative pointing outside the code
  // buffer nor pc absolute pointing inside the code buffer, so there is no need
  // to relocate any emitted relocation entries.

  // Relocate pending relocation entries.
  for (int i = 0; i < num_pending_reloc_info_; i++) {
    RelocInfo& rinfo = pending_reloc_info_[i];
    ASSERT(rinfo.rmode() != RelocInfo::COMMENT &&
           rinfo.rmode() != RelocInfo::POSITION);
    if (rinfo.rmode() != RelocInfo::JS_RETURN) {
      rinfo.set_pc(rinfo.pc() + pc_delta);
    }
  }
}


void Assembler::db(uint8_t data) {
  // No relocation info should be pending while using db. db is used
  // to write pure data with no pointers and the constant pool should
  // be emitted before using db.
  ASSERT(num_pending_reloc_info_ == 0);
  CheckBuffer();
  *reinterpret_cast<uint8_t*>(pc_) = data;
  pc_ += sizeof(uint8_t);
}


void Assembler::dd(uint32_t data) {
  // No relocation info should be pending while using dd. dd is used
  // to write pure data with no pointers and the constant pool should
  // be emitted before using dd.
  ASSERT(num_pending_reloc_info_ == 0);
  CheckBuffer();
  *reinterpret_cast<uint32_t*>(pc_) = data;
  pc_ += sizeof(uint32_t);
}


void Assembler::RecordRelocInfo(RelocInfo::Mode rmode, intptr_t data) {
  // We do not try to reuse pool constants.
  RelocInfo rinfo(pc_, rmode, data, NULL);
  if (((rmode >= RelocInfo::JS_RETURN) &&
       (rmode <= RelocInfo::DEBUG_BREAK_SLOT)) ||
      (rmode == RelocInfo::CONST_POOL)) {
    // Adjust code for new modes.
    ASSERT(RelocInfo::IsDebugBreakSlot(rmode)
           || RelocInfo::IsJSReturn(rmode)
           || RelocInfo::IsComment(rmode)
           || RelocInfo::IsPosition(rmode)
           || RelocInfo::IsConstPool(rmode));
    // These modes do not need an entry in the constant pool.
  } else {
    ASSERT(num_pending_reloc_info_ < kMaxNumPendingRelocInfo);
    if (num_pending_reloc_info_ == 0) {
      first_const_pool_use_ = pc_offset();
    }
    pending_reloc_info_[num_pending_reloc_info_++] = rinfo;
    // Make sure the constant pool is not emitted in place of the next
    // instruction for which we just recorded relocation info.
    BlockConstPoolFor(1);
  }
  if (rinfo.rmode() != RelocInfo::NONE) {
    // Don't record external references unless the heap will be serialized.
    if (rmode == RelocInfo::EXTERNAL_REFERENCE) {
#ifdef DEBUG
      if (!Serializer::enabled()) {
        Serializer::TooLateToEnableNow();
      }
#endif
      if (!Serializer::enabled() && !emit_debug_code()) {
        return;
      }
    }
    ASSERT(buffer_space() >= kMaxRelocSize);  // too late to grow buffer here
    if (rmode == RelocInfo::CODE_TARGET_WITH_ID) {
      RelocInfo reloc_info_with_ast_id(pc_,
                                       rmode,
                                       RecordedAstId().ToInt(),
                                       NULL);
      ClearRecordedAstId();
      reloc_info_writer.Write(&reloc_info_with_ast_id);
    } else {
      reloc_info_writer.Write(&rinfo);
    }
  }
}


void Assembler::BlockConstPoolFor(int instructions) {
  int pc_limit = pc_offset() + instructions * kInstrSize;
  if (no_const_pool_before_ < pc_limit) {
    // If there are some pending entries, the constant pool cannot be blocked
    // further than first_const_pool_use_ + kMaxDistToPool
    ASSERT((num_pending_reloc_info_ == 0) ||
           (pc_limit < (first_const_pool_use_ + kMaxDistToPool)));
    no_const_pool_before_ = pc_limit;
  }

  if (next_buffer_check_ < no_const_pool_before_) {
    next_buffer_check_ = no_const_pool_before_;
  }
}


// Power shouldn't need constant pools at all, remove this
void Assembler::CheckConstPool(bool force_emit, bool require_jump) {
  // Some short sequence of instruction mustn't be broken up by constant pool
  // emission, such sequences are protected by calls to BlockConstPoolFor and
  // BlockConstPoolScope.
  if (is_const_pool_blocked()) {
    // Something is wrong if emission is forced and blocked at the same time.
    ASSERT(!force_emit);
    return;
  }

  // There is nothing to do if there are no pending constant pool entries.
  if (num_pending_reloc_info_ == 0)  {
    // Calculate the offset of the next check.
    next_buffer_check_ = pc_offset() + kCheckPoolInterval;
    return;
  }

  // We emit a constant pool when:
  //  * requested to do so by parameter force_emit (e.g. after each function).
  //  * the distance to the first instruction accessing the constant pool is
  //    kAvgDistToPool or more.
  //  * no jump is required and the distance to the first instruction accessing
  //    the constant pool is at least kMaxDistToPool / 2.
  ASSERT(first_const_pool_use_ >= 0);
  int dist = pc_offset() - first_const_pool_use_;
  if (!force_emit && dist < kAvgDistToPool &&
      (require_jump || (dist < (kMaxDistToPool / 2)))) {
    return;
  }

  // Check that the code buffer is large enough before emitting the constant
  // pool (include the jump over the pool and the constant pool marker and
  // the gap to the relocation information).
  int jump_instr = require_jump ? kInstrSize : 0;
  int size = jump_instr + kInstrSize + num_pending_reloc_info_ * kPointerSize;
  int needed_space = size + kGap;
  while (buffer_space() <= needed_space) GrowBuffer();

  {
    // Block recursive calls to CheckConstPool.
    BlockConstPoolScope block_const_pool(this);
    RecordComment("[ Constant Pool");
    RecordConstPool(size);

    // Emit jump over constant pool if necessary.
    Label after_pool;
    if (require_jump) {
      b(&after_pool);
    }

    // Put down constant pool marker "Undefined instruction" as specified by
    // A5.6 (ARMv7) Instruction set encoding.
    emit(kConstantPoolMarker | num_pending_reloc_info_);

    // Emit constant pool entries.
    for (int i = 0; i < num_pending_reloc_info_; i++) {
      RelocInfo& rinfo = pending_reloc_info_[i];
      ASSERT(rinfo.rmode() != RelocInfo::COMMENT &&
             rinfo.rmode() != RelocInfo::POSITION &&
             rinfo.rmode() != RelocInfo::STATEMENT_POSITION &&
             rinfo.rmode() != RelocInfo::CONST_POOL);

#if 0  // don't actually modify the code - we have real relocations
      Instr instr = instr_at(rinfo.pc());
      // Instruction to patch must be 'ldr rd, [pc, #offset]' with offset == 0.
      ASSERT(IsLdrPcImmediateOffset(instr) &&
             GetLdrRegisterImmediateOffset(instr) == 0);

      int delta = pc_ - rinfo.pc() - kPcLoadDelta;
      // 0 is the smallest delta:
      //   ldr rd, [pc, #0]
      //   constant pool marker
      //   data
      ASSERT(is_uint12(delta));

      instr_at_put(rinfo.pc(), SetLdrRegisterImmediateOffset(instr, delta));
#endif

      emit(rinfo.data());
    }

    num_pending_reloc_info_ = 0;
    first_const_pool_use_ = -1;

    RecordComment("]");

    if (after_pool.is_linked()) {
      bind(&after_pool);
    }
  }

  // Since a constant pool was just emitted, move the check offset forward by
  // the standard interval.
  next_buffer_check_ = pc_offset() + kCheckPoolInterval;
}

#undef INCLUDE_ARM

} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_PPC
