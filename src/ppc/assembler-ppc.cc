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

#ifdef DEBUG
bool CpuFeatures::initialized_ = false;
#endif
unsigned CpuFeatures::supported_ = 0;
unsigned CpuFeatures::found_by_runtime_probing_ = 0;

// Get the CPU features enabled by the build.
static unsigned CpuFeaturesImpliedByCompiler() {
  unsigned answer = 0;
  return answer;
}

#if !defined(_AIX)
// This function uses types in elf.h
static bool is_processor(const char* p) {
  static bool read_tried = false;
  static char *auxv_cpu_type = NULL;

  if (!read_tried) {
    // Open the AUXV (auxilliary vector) psuedo-file
    int fd = open("/proc/self/auxv", O_RDONLY);

    read_tried = true;
    if (fd != -1) {
#if V8_TARGET_ARCH_PPC64
      static Elf64_auxv_t buffer[16];
      Elf64_auxv_t *auxv_element;
#else
      static Elf32_auxv_t buffer[16];
      Elf32_auxv_t *auxv_element;
#endif
      int bytes_read = 0;
      while (bytes_read >= 0) {
        // Read a chunk of the AUXV
        bytes_read = read(fd, buffer, sizeof(buffer));
        // Locate and read the platform field of AUXV if it is in the chunk
        for (auxv_element = buffer;
             auxv_element+sizeof(auxv_element) <= buffer+bytes_read &&
             auxv_element->a_type != AT_NULL;
             auxv_element++) {
          if (auxv_element->a_type == AT_PLATFORM) {
            /* Note: Both auxv_cpu_type and buffer are static */
            auxv_cpu_type = reinterpret_cast<char*>(auxv_element->a_un.a_val);
            goto done_reading;
          }
        }
      }
      done_reading:
      close(fd);
    }
  }

  if (auxv_cpu_type == NULL) {
    return false;
  }
  return (strcmp(auxv_cpu_type, p) == 0);
}
#endif

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

  // Detect whether frim instruction is supported (POWER5+)
  // For now we will just check for processors we know do not
  // support it
#if !defined(_AIX)
  if (!is_processor("ppc970") /* G5 */ && !is_processor("ppc7450") /* G4 */) {
    // Assume support
    supported_ |= (1u << FPU);
  }
#else
  // Fallback: assume frim is supported -- will implement processor
  // detection for other PPC platforms in is_processor() if required
  supported_ |= (1u << FPU);
#endif
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

const int RelocInfo::kApplyMask = 1 << RelocInfo::INTERNAL_REFERENCE;


bool RelocInfo::IsCodedSpecially() {
  // The deserializer needs to know whether a pointer is specially
  // coded.  Being specially coded on PPC means that it is a lis/ori
  // instruction sequence, and that is always the case inside code
  // objects.
  return true;
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
// See assembler-ppc-inl.h for inlined constructors

Operand::Operand(Handle<Object> handle) {
  rm_ = no_reg;
  // Verify all Objects referred by code are NOT in new space.
  Object* obj = *handle;
  ASSERT(!HEAP->InNewSpace(obj));
  if (obj->IsHeapObject()) {
    imm_ = reinterpret_cast<intptr_t>(handle.location());
    rmode_ = RelocInfo::EMBEDDED_OBJECT;
  } else {
    // no relocation needed
    imm_ =  reinterpret_cast<intptr_t>(obj);
    rmode_ = RelocInfo::NONE;
  }
}

MemOperand::MemOperand(Register rn, int32_t offset) {
  ra_ = rn;
  rb_ = no_reg;
  offset_ = offset;
}

MemOperand::MemOperand(Register ra, Register rb) {
  ra_ = ra;
  rb_ = rb;
  offset_ = 0;
}

// -----------------------------------------------------------------------------
// Specific instructions, constants, and masks.

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

  no_trampoline_pool_before_ = 0;
  trampoline_pool_blocked_nesting_ = 0;
  // We leave space (kMaxBlockTrampolineSectionSize)
  // for BlockTrampolinePoolScope buffer.
  next_buffer_check_ = kMaxCondBranchReach - kMaxBlockTrampolineSectionSize;
  internal_trampoline_exception_ = false;
  last_bound_pos_ = 0;

  trampoline_emitted_ = false;
  unbound_labels_count_ = 0;

  ClearRecordedAstId();
}


Assembler::~Assembler() {
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
  return (instr & kOpcodeMask) == ADDIS;
}

bool Assembler::IsAddic(Instr instr) {
  return (instr & kOpcodeMask) == ADDIC;
}

bool Assembler::IsOri(Instr instr) {
  return (instr & kOpcodeMask) == ORI;
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

#if V8_TARGET_ARCH_PPC64
// This code assumes a FIXED_SEQUENCE for 64bit loads (lis/ori)
bool Assembler::Is64BitLoadIntoR12(Instr instr1, Instr instr2,
                             Instr instr3, Instr instr4, Instr instr5) {
  // Check the instructions are indeed a five part load (into r12)
  // 3d800000       lis     r12, 0
  // 618c0000       ori     r12, r12, 0
  // 798c07c6       rldicr  r12, r12, 32, 31
  // 658c00c3       oris    r12, r12, 195
  // 618ccd40       ori     r12, r12, 52544
  return(((instr1 >> 16) == 0x3d80) && ((instr2 >> 16) == 0x618c) &&
         (instr3 == 0x798c07c6) &&
         ((instr4 >> 16) == 0x658c) && ((instr5 >> 16) == 0x618c));
}
#else
// This code assumes a FIXED_SEQUENCE for 32bit loads (lis/ori)
bool Assembler::Is32BitLoadIntoR12(Instr instr1, Instr instr2) {
  // Check the instruction is indeed a two part load (into r12)
  // 3d802553       lis     r12, 9555
  // 618c5000       ori   r12, r12, 20480
  return(((instr1 >> 16) == 0x3d80) && ((instr2 >> 16) == 0x618c));
}
#endif

bool Assembler::IsCmpRegister(Instr instr) {
  return (((instr & kOpcodeMask) == EXT2) &&
          ((instr & kExt2OpcodeMask) == CMP));
}

bool Assembler::IsRlwinm(Instr instr) {
  return ((instr & kOpcodeMask) == RLWINMX);
}

#if V8_TARGET_ARCH_PPC64
bool Assembler::IsRldicl(Instr instr) {
  return (((instr & kOpcodeMask) == EXT5) &&
          ((instr & kExt5OpcodeMask) == RLDICL));
}
#endif

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
    imm26 &= ~(kAAMask|kLKMask);  // discard AA|LK bits if present
    if (imm26 == 0)
        return kEndOfChain;
    return pos + imm26;
  } else if (BCX == opcode) {
    int imm16 = SIGN_EXT_IMM16((instr & kImm16Mask));
    imm16 &= ~(kAAMask|kLKMask);  // discard AA|LK bits if present
    if (imm16 == 0)
        return kEndOfChain;
    return pos + imm16;
  } else if ((instr & ~kImm16Mask) == 0) {
    // Emitted label constant, not part of a branch (regexp PushBacktrack).
     if (instr == 0) {
       return kEndOfChain;
     } else {
       int32_t imm16 = SIGN_EXT_IMM16(instr);
       return (imm16 + pos);
     }
  }

  PPCPORT_UNIMPLEMENTED();
  ASSERT(false);
  return -1;
}

void Assembler::target_at_put(int pos, int target_pos) {
  Instr instr = instr_at(pos);
  int opcode = instr & kOpcodeMask;

  // check which type of branch this is 16 or 26 bit offset
  if (BX == opcode) {
    int imm26 = target_pos - pos;
    ASSERT((imm26 & (kAAMask|kLKMask)) == 0);
    instr &= ((~kImm26Mask)|kAAMask|kLKMask);
    ASSERT(is_int26(imm26));
    instr_at_put(pos, instr | (imm26 & kImm26Mask));
    return;
  } else if (BCX == opcode) {
    int imm16 = target_pos - pos;
    ASSERT((imm16 & (kAAMask|kLKMask)) == 0);
    instr &= ((~kImm16Mask)|kAAMask|kLKMask);
    ASSERT(is_int16(imm16));
    instr_at_put(pos, instr | (imm16 & kImm16Mask));
    return;
  } else if ((instr & ~kImm16Mask) == 0) {
    ASSERT(target_pos == kEndOfChain || target_pos >= 0);
    // Emitted label constant, not part of a branch.
    // Make label relative to Code* of generated Code object.
    instr_at_put(pos, target_pos + (Code::kHeaderSize - kHeapObjectTag));
    return;
  }

  ASSERT(false);
}

int Assembler::max_reach_from(int pos) {
  Instr instr = instr_at(pos);
  int opcode = instr & kOpcodeMask;

  // check which type of branch this is 16 or 26 bit offset
  if (BX == opcode) {
    return 26;
  } else if (BCX == opcode) {
    return 16;
  } else if ((instr & ~kImm16Mask) == 0) {
    // Emitted label constant, not part of a branch (regexp PushBacktrack).
    return 16;
  }

  ASSERT(false);
  return 0;
}

void Assembler::bind_to(Label* L, int pos) {
  ASSERT(0 <= pos && pos <= pc_offset());  // must have a valid binding position
  int32_t trampoline_pos = kInvalidSlotPos;
  if (L->is_linked() && !trampoline_emitted_) {
    unbound_labels_count_--;
    next_buffer_check_ += kTrampolineSlotsSize;
  }

  while (L->is_linked()) {
    int fixup_pos = L->pos();
    int32_t offset = pos - fixup_pos;
    int maxReach = max_reach_from(fixup_pos);
    next(L);  // call next before overwriting link with target at fixup_pos
    if (is_intn(offset, maxReach) == false) {
      if (trampoline_pos == kInvalidSlotPos) {
        trampoline_pos = get_trampoline_entry();
        CHECK(trampoline_pos != kInvalidSlotPos);
        target_at_put(trampoline_pos, pos);
      }
      target_at_put(fixup_pos, trampoline_pos);
    } else {
      target_at_put(fixup_pos, pos);
    }
  }
  L->bind_to(pos);

  // Keep track of the last bound label so we don't eliminate any instructions
  // before a bound label.
  if (pos > last_bound_pos_)
    last_bound_pos_ = pos;
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

bool Assembler::is_near(Label* L, Condition cond) {
  ASSERT(L->is_bound());
  if (L->is_bound() == false)
    return false;

  int maxReach = ((cond == al) ? 26 : 16);
  int offset = L->pos() - pc_offset();

  return is_intn(offset, maxReach);
}

void Assembler::a_form(Instr instr,
                       DwVfpRegister frt,
                       DwVfpRegister fra,
                       DwVfpRegister frb,
                       RCBit r) {
  emit(instr | frt.code()*B21 | fra.code()*B16 | frb.code()*B11 | r);
}

void Assembler::d_form(Instr instr,
                        Register rt,
                        Register ra,
                        const intptr_t val,
                        bool signed_disp) {
  if (signed_disp) {
    if (!is_int16(val)) {
      PrintF("val = %" V8PRIdPTR ", 0x%" V8PRIxPTR "\n", val, val);
    }
    ASSERT(is_int16(val));
  } else {
    if (!is_uint16(val)) {
      PrintF("val = %" V8PRIdPTR ", 0x%" V8PRIxPTR
             ", is_unsigned_imm16(val)=%d, kImm16Mask=0x%x\n",
             val, val, is_uint16(val), kImm16Mask);
    }
    ASSERT(is_uint16(val));
  }
  emit(instr | rt.code()*B21 | ra.code()*B16 | (kImm16Mask & val));
}

void Assembler::x_form(Instr instr,
                         Register ra,
                         Register rs,
                         Register rb,
                         RCBit r) {
  emit(instr | rs.code()*B21 | ra.code()*B16 | rb.code()*B11 | r);
}

void Assembler::xo_form(Instr instr,
                         Register rt,
                         Register ra,
                         Register rb,
                         OEBit o,
                         RCBit r) {
  emit(instr | rt.code()*B21 | ra.code()*B16 | rb.code()*B11 | o | r);
}

void Assembler::md_form(Instr instr,
                        Register ra,
                        Register rs,
                        int shift,
                        int maskbit,
                        RCBit r) {
  int sh0_4 = shift & 0x1f;
  int sh5   = (shift >> 5) & 0x1;
  int m0_4  = maskbit & 0x1f;
  int m5    = (maskbit >> 5) & 0x1;

  emit(instr | rs.code()*B21 | ra.code()*B16 |
       sh0_4*B11 | m0_4*B6 | m5*B5 | sh5*B1 | r);
}

// Returns the next free trampoline entry.
int32_t Assembler::get_trampoline_entry() {
  int32_t trampoline_entry = kInvalidSlotPos;

  if (!internal_trampoline_exception_) {
    trampoline_entry = trampoline_.take_slot();

    if (kInvalidSlotPos == trampoline_entry) {
      internal_trampoline_exception_ = true;
    }
  }
  return trampoline_entry;
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
      if (!trampoline_emitted_) {
        unbound_labels_count_++;
        next_buffer_check_ -= kTrampolineSlotsSize;
      }
    }
    L->link_to(pc_offset());
  }

  return target_pos - pc_offset();
}


void Assembler::label_at_put(Label* L, int at_offset) {
  int target_pos;
  if (L->is_bound()) {
    target_pos = L->pos();
    instr_at_put(at_offset, target_pos + (Code::kHeaderSize - kHeapObjectTag));
  } else {
    if (L->is_linked()) {
      target_pos = L->pos();  // L's link
    } else {
      // was: target_pos = kEndOfChain;
      // However, using branch to self to mark the first reference
      // should avoid most instances of branch offset overflow.  See
      // target_at() for where this is converted back to kEndOfChain.
      target_pos = at_offset;
      if (!trampoline_emitted_) {
        unbound_labels_count_++;
        next_buffer_check_ -= kTrampolineSlotsSize;
      }
    }
    L->link_to(at_offset);

    Instr constant = target_pos - at_offset;
    ASSERT(is_int16(constant));
    instr_at_put(at_offset, constant);
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

void Assembler::bc(int branch_offset, BOfield bo, int condition_bit, LKBit lk) {
  positions_recorder()->WriteRecordedPositions();
  ASSERT(is_int16(branch_offset));
  emit(BCX | bo | condition_bit*B16 | (kImm16Mask & branch_offset) | lk);
}

void Assembler::b(int branch_offset, LKBit lk) {
  positions_recorder()->WriteRecordedPositions();
  ASSERT((branch_offset & 3) == 0);
  int imm26 = branch_offset;
  ASSERT(is_int26(imm26));
  // todo add AA and LK bits
  emit(BX | (imm26 & kImm26Mask) | lk);
}

void Assembler::xori(Register dst, Register src, const Operand& imm) {
  d_form(XORI, src, dst, imm.imm_, false);
}

void Assembler::xoris(Register ra, Register rs, const Operand& imm) {
  d_form(XORIS, rs, ra, imm.imm_, false);
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
  emit(RLWINMX | rs.code()*B21 | ra.code()*B16 | sh*B11 | mb*B6 | me << 1 | rc);
}

void Assembler::rlwimi(Register ra, Register rs,
                       int sh, int mb, int me, RCBit rc) {
  sh &= 0x1f;
  mb &= 0x1f;
  me &= 0x1f;
  emit(RLWIMIX | rs.code()*B21 | ra.code()*B16 | sh*B11 | mb*B6 | me << 1 | rc);
}

void Assembler::slwi(Register dst, Register src, const Operand& val,
                     RCBit rc) {
  ASSERT((32 > val.imm_)&&(val.imm_ >= 0));
  rlwinm(dst, src, val.imm_, 0, 31-val.imm_, rc);
}
void Assembler::srwi(Register dst, Register src, const Operand& val,
                     RCBit rc) {
  ASSERT((32 > val.imm_)&&(val.imm_ >= 0));
  rlwinm(dst, src, 32-val.imm_, val.imm_, 31, rc);
}
void Assembler::clrrwi(Register dst, Register src, const Operand& val,
                       RCBit rc) {
  ASSERT((32 > val.imm_)&&(val.imm_ >= 0));
  rlwinm(dst, src, 0, 0, 31-val.imm_, rc);
}
void Assembler::clrlwi(Register dst, Register src, const Operand& val,
                       RCBit rc) {
  ASSERT((32 > val.imm_)&&(val.imm_ >= 0));
  rlwinm(dst, src, 0, val.imm_, 31, rc);
}


void Assembler::srawi(Register ra, Register rs, int sh, RCBit r) {
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

void Assembler::subi(Register dst, Register src, const Operand& imm) {
  addi(dst, src, Operand(-(imm.imm_)));
}

void Assembler::addc(Register dst, Register src1, Register src2,
                    OEBit o, RCBit r) {
  xo_form(EXT2 | ADDCX, dst, src1, src2, o, r);
}

void Assembler::addze(Register dst, Register src1, OEBit o, RCBit r) {
  // a special xo_form
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
  d_form(SUBFIC, dst, src, imm.imm_, true);
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

// Divide word
void Assembler::divw(Register dst, Register src1, Register src2,
                     OEBit o, RCBit r) {
  xo_form(EXT2 | DIVW, dst, src1, src2, o, r);
}

void Assembler::addi(Register dst, Register src, const Operand& imm) {
  ASSERT(!src.is(r0));  // use li instead to show intent
  d_form(ADDI, dst, src, imm.imm_, true);
}

void  Assembler::addis(Register dst, Register src, const Operand& imm) {
  ASSERT(!src.is(r0));  // use lis instead to show intent
  d_form(ADDIS, dst, src, imm.imm_, true);
}

void Assembler::addic(Register dst, Register src, const Operand& imm) {
  d_form(ADDIC, dst, src, imm.imm_, true);
}

void  Assembler::andi(Register ra, Register rs, const Operand& imm) {
  d_form(ANDIx, rs, ra, imm.imm_, false);
}

void Assembler::andis(Register ra, Register rs, const Operand& imm) {
  d_form(ANDISx, rs, ra, imm.imm_, false);
}

void Assembler::nor(Register dst, Register src1, Register src2, RCBit r) {
  x_form(EXT2 | NORX, dst, src1, src2, r);
}

void Assembler::notx(Register dst, Register src, RCBit r) {
  x_form(EXT2 | NORX, dst, src, src, r);
}

void Assembler::ori(Register ra, Register rs, const Operand& imm) {
  d_form(ORI, rs, ra, imm.imm_, false);
}

void Assembler::oris(Register dst, Register src, const Operand& imm) {
  d_form(ORIS, src, dst, imm.imm_, false);
}

void Assembler::orx(Register dst, Register src1, Register src2, RCBit rc) {
  x_form(EXT2 | ORX, dst, src1, src2, rc);
}

void Assembler::cmpi(Register src1, const Operand& src2, CRegister cr) {
  intptr_t imm16 = src2.imm_;
#if V8_TARGET_ARCH_PPC64
  int L = 1;
#else
  int L = 0;
#endif
  ASSERT(is_int16(imm16));
  ASSERT(cr.code() >= 0 && cr.code() <= 7);
  imm16 &= kImm16Mask;
  emit(CMPI | cr.code()*B23 | L*B21 | src1.code()*B16 | imm16);
}

void Assembler::cmpli(Register src1, const Operand& src2, CRegister cr) {
  uintptr_t uimm16 = src2.imm_;
#if V8_TARGET_ARCH_PPC64
  int L = 1;
#else
  int L = 0;
#endif
  ASSERT(is_uint16(uimm16));
  ASSERT(cr.code() >= 0 && cr.code() <= 7);
  uimm16 &= kImm16Mask;
  emit(CMPLI | cr.code()*B23 | L*B21 | src1.code()*B16 | uimm16);
}

void Assembler::cmp(Register src1, Register src2, CRegister cr) {
#if V8_TARGET_ARCH_PPC64
  int L = 1;
#else
  int L = 0;
#endif
  ASSERT(cr.code() >= 0 && cr.code() <= 7);
  emit(EXT2 | CMP | cr.code()*B23 | L*B21 | src1.code()*B16 |
       src2.code()*B11);
}

void Assembler::cmpl(Register src1, Register src2, CRegister cr) {
#if V8_TARGET_ARCH_PPC64
  int L = 1;
#else
  int L = 0;
#endif
  ASSERT(cr.code() >= 0 && cr.code() <= 7);
  emit(EXT2 | CMPL | cr.code()*B23 | L*B21 | src1.code()*B16 |
       src2.code()*B11);
}

// Pseudo op - load immediate
void Assembler::li(Register dst, const Operand &imm) {
  d_form(ADDI, dst, r0, imm.imm_, true);
}

void  Assembler::lis(Register dst, const Operand& imm) {
  d_form(ADDIS, dst, r0, imm.imm_, true);
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

void Assembler::lbzx(Register rt, const MemOperand &src) {
  Register ra = src.ra();
  Register rb = src.rb();
  ASSERT(!ra.is(r0));
  emit(EXT2 | LBZX | rt.code()*B21 | ra.code()*B16 | rb.code()*B11 | LeaveRC);
}

void Assembler::lbzux(Register rt, const MemOperand & src) {
  Register ra = src.ra();
  Register rb = src.rb();
  ASSERT(!ra.is(r0));
  emit(EXT2 | LBZUX | rt.code()*B21 | ra.code()*B16 | rb.code()*B11 | LeaveRC);
}

void Assembler::lhz(Register dst, const MemOperand &src) {
  ASSERT(!src.ra_.is(r0));
  d_form(LHZ, dst, src.ra(), src.offset(), true);
}

void Assembler::lhzx(Register rt, const MemOperand &src) {
  Register ra = src.ra();
  Register rb = src.rb();
  ASSERT(!ra.is(r0));
  emit(EXT2 | LHZX | rt.code()*B21 | ra.code()*B16 | rb.code()*B11 | LeaveRC);
}

void Assembler::lhzux(Register rt, const MemOperand & src) {
  Register ra = src.ra();
  Register rb = src.rb();
  ASSERT(!ra.is(r0));
  emit(EXT2 | LHZUX | rt.code()*B21 | ra.code()*B16 | rb.code()*B11 | LeaveRC);
}

void Assembler::lwz(Register dst, const MemOperand &src) {
  ASSERT(!src.ra_.is(r0));
  d_form(LWZ, dst, src.ra(), src.offset(), true);
}

void Assembler::lwzu(Register dst, const MemOperand &src) {
  ASSERT(!src.ra_.is(r0));
  d_form(LWZU, dst, src.ra(), src.offset(), true);
}

void Assembler::lwzx(Register rt, const MemOperand &src) {
  Register ra = src.ra();
  Register rb = src.rb();
  ASSERT(!ra.is(r0));
  emit(EXT2 | LWZX | rt.code()*B21 | ra.code()*B16 | rb.code()*B11 | LeaveRC);
}

void Assembler::lwzux(Register rt, const MemOperand & src) {
  Register ra = src.ra();
  Register rb = src.rb();
  ASSERT(!ra.is(r0));
  emit(EXT2 | LWZUX | rt.code()*B21 | ra.code()*B16 | rb.code()*B11 | LeaveRC);
}

void Assembler::lwa(Register dst, const MemOperand &src) {
#if V8_TARGET_ARCH_PPC64
  int offset = src.offset();
  ASSERT(!src.ra_.is(r0));
  ASSERT(!(offset & 3) && is_int16(offset));
  offset = kImm16Mask & offset;
  emit(LD | dst.code()*B21 | src.ra().code()*B16 | offset | 2);
#else
  lwz(dst, src);
#endif
}

void Assembler::stb(Register dst, const MemOperand &src) {
  ASSERT(!src.ra_.is(r0));
  d_form(STB, dst, src.ra(), src.offset(), true);
}

void Assembler::stbx(Register rs, const MemOperand &src) {
  Register ra = src.ra();
  Register rb = src.rb();
  ASSERT(!ra.is(r0));
  emit(EXT2 | STBX | rs.code()*B21 | ra.code()*B16 | rb.code()*B11 | LeaveRC);
}

void Assembler::stbux(Register rs, const MemOperand &src) {
  Register ra = src.ra();
  Register rb = src.rb();
  ASSERT(!ra.is(r0));
  emit(EXT2 | STBUX | rs.code()*B21 | ra.code()*B16 | rb.code()*B11 | LeaveRC);
}

void Assembler::sth(Register dst, const MemOperand &src) {
  ASSERT(!src.ra_.is(r0));
  d_form(STH, dst, src.ra(), src.offset(), true);
}

void Assembler::sthx(Register rs, const MemOperand &src) {
  Register ra = src.ra();
  Register rb = src.rb();
  ASSERT(!ra.is(r0));
  emit(EXT2 | STHX | rs.code()*B21 | ra.code()*B16 | rb.code()*B11 | LeaveRC);
}

void Assembler::sthux(Register rs, const MemOperand &src) {
  Register ra = src.ra();
  Register rb = src.rb();
  ASSERT(!ra.is(r0));
  emit(EXT2 | STHUX | rs.code()*B21 | ra.code()*B16 | rb.code()*B11 | LeaveRC);
}

void Assembler::stw(Register dst, const MemOperand &src) {
  ASSERT(!src.ra_.is(r0));
  d_form(STW, dst, src.ra(), src.offset(), true);
}

void Assembler::stwu(Register dst, const MemOperand &src) {
  ASSERT(!src.ra_.is(r0));
  d_form(STWU, dst, src.ra(), src.offset(), true);
}

void Assembler::stwx(Register rs, const MemOperand &src) {
  Register ra = src.ra();
  Register rb = src.rb();
  ASSERT(!ra.is(r0));
  emit(EXT2 | STWX | rs.code()*B21 | ra.code()*B16 | rb.code()*B11 | LeaveRC);
}

void Assembler::stwux(Register rs, const MemOperand &src) {
  Register ra = src.ra();
  Register rb = src.rb();
  ASSERT(!ra.is(r0));
  emit(EXT2 | STWUX | rs.code()*B21 | ra.code()*B16 | rb.code()*B11 | LeaveRC);
}

void Assembler::extsb(Register rs, Register ra, RCBit rc) {
  emit(EXT2 | EXTSB | ra.code()*B21 | rs.code()*B16 | rc);
}

void Assembler::extsh(Register rs, Register ra, RCBit rc) {
  emit(EXT2 | EXTSH | ra.code()*B21 | rs.code()*B16 | rc);
}

void Assembler::neg(Register rt, Register ra, OEBit o, RCBit r) {
  emit(EXT2 | NEGX | rt.code()*B21 | ra.code()*B16 | o | r);
}

void Assembler::andc(Register dst, Register src1, Register src2, RCBit rc) {
  x_form(EXT2 | ANDCX, dst, src1, src2, rc);
}

#if V8_TARGET_ARCH_PPC64
// 64bit specific instructions
void Assembler::ld(Register rd, const MemOperand &src) {
  int offset = src.offset();
  ASSERT(!src.ra_.is(r0));
  ASSERT(!(offset & 3) && is_int16(offset));
  offset = kImm16Mask & offset;
  emit(LD | rd.code()*B21 | src.ra().code()*B16 | offset);
}

void Assembler::ldx(Register rd, const MemOperand &src) {
  Register ra = src.ra();
  Register rb = src.rb();
  ASSERT(!ra.is(r0));
  emit(EXT2 | LDX | rd.code()*B21 | ra.code()*B16 | rb.code()*B11);
}

void Assembler::ldu(Register rd, const MemOperand &src) {
  int offset = src.offset();
  ASSERT(!src.ra_.is(r0));
  ASSERT(!(offset & 3) && is_int16(offset));
  offset = kImm16Mask & offset;
  emit(LD | rd.code()*B21 | src.ra().code()*B16 | offset | 1);
}

void Assembler::ldux(Register rd, const MemOperand &src) {
  Register ra = src.ra();
  Register rb = src.rb();
  ASSERT(!ra.is(r0));
  emit(EXT2 | LDUX | rd.code()*B21 | ra.code()*B16 | rb.code()*B11);
}

void Assembler::std(Register rs, const MemOperand &src) {
  int offset = src.offset();
  ASSERT(!src.ra_.is(r0));
  ASSERT(!(offset & 3) && is_int16(offset));
  offset = kImm16Mask & offset;
  emit(STD | rs.code()*B21 | src.ra().code()*B16 | offset);
}

void Assembler::stdx(Register rs, const MemOperand &src) {
  Register ra = src.ra();
  Register rb = src.rb();
  ASSERT(!ra.is(r0));
  emit(EXT2 | STDX | rs.code()*B21 | ra.code()*B16 | rb.code()*B11);
}

void Assembler::stdu(Register rs, const MemOperand &src) {
  int offset = src.offset();
  ASSERT(!src.ra_.is(r0));
  ASSERT(!(offset & 3) && is_int16(offset));
  offset = kImm16Mask & offset;
  emit(STD | rs.code()*B21 | src.ra().code()*B16 | offset | 1);
}

void Assembler::stdux(Register rs, const MemOperand &src) {
  Register ra = src.ra();
  Register rb = src.rb();
  ASSERT(!ra.is(r0));
  emit(EXT2 | STDUX | rs.code()*B21 | ra.code()*B16 | rb.code()*B11);
}

void Assembler::rldic(Register ra, Register rs, int sh, int mb, RCBit r) {
  md_form(EXT5 | RLDIC, ra, rs, sh, mb, r);
}

void Assembler::rldicl(Register ra, Register rs, int sh, int mb, RCBit r) {
  md_form(EXT5 | RLDICL, ra, rs, sh, mb, r);
}

void Assembler::rldicr(Register ra, Register rs, int sh, int me, RCBit r) {
  md_form(EXT5 | RLDICR, ra, rs, sh, me, r);
}

void Assembler::sldi(Register dst, Register src, const Operand& val,
                     RCBit rc) {
  ASSERT((64 > val.imm_)&&(val.imm_ >= 0));
  rldicr(dst, src, val.imm_, 63-val.imm_, rc);
}
void Assembler::srdi(Register dst, Register src, const Operand& val,
                     RCBit rc) {
  ASSERT((64 > val.imm_)&&(val.imm_ >= 0));
  rldicl(dst, src, 64-val.imm_, val.imm_, rc);
}
void Assembler::clrrdi(Register dst, Register src, const Operand& val,
                       RCBit rc) {
  ASSERT((64 > val.imm_)&&(val.imm_ >= 0));
  rldicr(dst, src, 0, 63-val.imm_, rc);
}
void Assembler::clrldi(Register dst, Register src, const Operand& val,
                       RCBit rc) {
  ASSERT((64 > val.imm_)&&(val.imm_ >= 0));
  rldicl(dst, src, 0, val.imm_, rc);
}


void Assembler::rldimi(Register ra, Register rs, int sh, int mb, RCBit r) {
  md_form(EXT5 | RLDIMI, ra, rs, sh, mb, r);
}


void Assembler::sradi(Register ra, Register rs, int sh, RCBit r) {
  int sh0_4 = sh & 0x1f;
  int sh5   = (sh >> 5) & 0x1;

  emit(EXT2 | SRADIX | rs.code()*B21 | ra.code()*B16 | sh0_4*B11 | sh5*B1 | r);
}

void Assembler::srd(Register dst, Register src1, Register src2, RCBit r) {
  x_form(EXT2 | SRDX, dst, src1, src2, r);
}

void Assembler::sld(Register dst, Register src1, Register src2, RCBit r) {
  x_form(EXT2 | SLDX, dst, src1, src2, r);
}

void Assembler::srad(Register ra, Register rs, Register rb, RCBit r) {
  x_form(EXT2 | SRAD, ra, rs, rb, r);
}

void Assembler::cntlzd_(Register ra, Register rs, RCBit rc) {
  x_form(EXT2 | CNTLZDX, ra, rs, r0, rc);
}

void Assembler::extsw(Register rs, Register ra, RCBit rc) {
  emit(EXT2 | EXTSW | ra.code()*B21 | rs.code()*B16 | rc);
}

void Assembler::mulld(Register dst, Register src1, Register src2,
                      OEBit o, RCBit r) {
  xo_form(EXT2 | MULLD, dst, src1, src2, o, r);
}

void Assembler::divd(Register dst, Register src1, Register src2,
                     OEBit o, RCBit r) {
  xo_form(EXT2 | DIVD, dst, src1, src2, o, r);
}
#endif


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

// Function descriptor for AIX.
// Code address skips the function descriptor "header".
// TOC and static chain are ignored and set to 0.
void Assembler::function_descriptor() {
  RecordRelocInfo(RelocInfo::INTERNAL_REFERENCE);
#if V8_TARGET_ARCH_PPC64
  uint64_t value = reinterpret_cast<uint64_t>(pc_) + 3 * kPointerSize;
#if __BYTE_ORDER == __LITTLE_ENDIAN
  emit(static_cast<uint32_t>(value & 0xFFFFFFFF));
  emit(static_cast<uint32_t>(value >> 32));
#else
  emit(static_cast<uint32_t>(value >> 32));
  emit(static_cast<uint32_t>(value & 0xFFFFFFFF));
#endif
  emit(static_cast<Instr>(0));
  emit(static_cast<Instr>(0));
  emit(static_cast<Instr>(0));
  emit(static_cast<Instr>(0));
#else
  emit(reinterpret_cast<Instr>(pc_) + 3 * kPointerSize);
  emit(static_cast<Instr>(0));
  emit(static_cast<Instr>(0));
#endif
}
// end PowerPC

// Primarily used for loading constants
// This should really move to be in macro-assembler as it
// is really a pseudo instruction
// Some usages of this intend for a FIXED_SEQUENCE to be used
// Todo - break this dependency so we can optimize mov() in general
// and only use the generic version when we require a fixed sequence
void Assembler::mov(Register dst, const Operand& src) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  if (src.rmode_ != RelocInfo::NONE) {
    // some form of relocation needed
    RecordRelocInfo(src.rmode_, src.imm_);
  }

#if V8_TARGET_ARCH_PPC64
  int64_t value = src.immediate();
  int32_t hi_32 = static_cast<int64_t>(value) >> 32;
  int32_t lo_32 = static_cast<int32_t>(value);
  int hi_word = static_cast<int>(hi_32) >> 16;
  int lo_word = static_cast<int>(hi_32) & 0xFFFF;
  lis(dst, Operand(SIGN_EXT_IMM16(hi_word)));
  ori(dst, dst, Operand(lo_word));
  sldi(dst, dst, Operand(32));
  hi_word = (static_cast<int>(lo_32) >> 16) & 0xFFFF;
  lo_word = static_cast<int>(lo_32) & 0xFFFF;
  oris(dst, dst, Operand(hi_word));
  ori(dst, dst, Operand(lo_word));
#else
  int value = src.immediate();
  if (!is_trampoline_pool_blocked()) {
    if (is_int16(value)) {
      li(dst, Operand(value));
      return;
    }
  }
  int hi_word = static_cast<int>(value) >> 16;
  int lo_word = static_cast<int>(value) & 0XFFFF;

  lis(dst, Operand(SIGN_EXT_IMM16(hi_word)));
  if ((!is_trampoline_pool_blocked()) && (lo_word == 0)) {
    return;
  }
  ori(dst, dst, Operand(lo_word));
#endif
}

// Special register instructions
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

void Assembler::mtxer(Register src) {
  emit(EXT2 | MTSPR | src.code()*B21 | 32 << 11);
}

void Assembler::mcrfs(int bf, int bfa) {
  emit(EXT4 | MCRFS | bf*B23 | bfa*B18);
}

void Assembler::mfcr(Register dst) {
  emit(EXT2 | MFCR | dst.code()*B21);
}

// end PowerPC

// Exception-generating instructions and debugging support.
// Stops with a non-negative code less than kNumOfWatchedStops support
// enabling/disabling and a counter feature. See simulator-ppc.h .
void Assembler::stop(const char* msg, Condition cond, int32_t code,
                     CRegister cr) {
  if (cond != al) {
    Label skip;
    b(NegateCondition(cond), &skip, cr);
    bkpt(0);
    bind(&skip);
  } else {
    bkpt(0);
  }
}

void Assembler::bkpt(uint32_t imm16) {
  emit(0x7d821008);
}


void Assembler::info(const char* msg, Condition cond, int32_t code,
                     CRegister cr) {
  if (::v8::internal::FLAG_trace_sim_stubs) {
    emit(0x7d9ff808);
#if V8_TARGET_ARCH_PPC64
    uint64_t value = reinterpret_cast<uint64_t>(msg);
    emit(static_cast<uint32_t>(value >> 32));
    emit(static_cast<uint32_t>(value & 0xFFFFFFFF));
#else
    emit(reinterpret_cast<Instr>(msg));
#endif
  }
}

void Assembler::dcbf(Register ra, Register rb) {
    emit(EXT2 | DCBF | ra.code()*B16 | rb.code()*B11);
}

void Assembler::sync() {
    emit(EXT2 | SYNC);
}

void Assembler::icbi(Register ra, Register rb) {
    emit(EXT2 | ICBI | ra.code()*B16 | rb.code()*B11);
}

void Assembler::isync() {
    emit(EXT1 | ISYNC);
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

void Assembler::lfdu(const DwVfpRegister frt, const MemOperand &src) {
  int offset = src.offset();
  Register ra = src.ra();
  ASSERT(is_int16(offset));
  int imm16 = offset & kImm16Mask;
  // could be x_form instruction with some casting magic
  emit(LFDU | frt.code()*B21 | ra.code()*B16 | imm16);
}

void Assembler::lfdx(const DwVfpRegister frt, const MemOperand &src) {
  Register ra = src.ra();
  Register rb = src.rb();
  ASSERT(!ra.is(r0));
  emit(EXT2 | LFDX | frt.code()*B21 | ra.code()*B16 | rb.code()*B11 | LeaveRC);
}

void Assembler::lfdux(const DwVfpRegister frt, const MemOperand & src) {
  Register ra = src.ra();
  Register rb = src.rb();
  ASSERT(!ra.is(r0));
  emit(EXT2 | LFDUX | frt.code()*B21 | ra.code()*B16 | rb.code()*B11 | LeaveRC);
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

void Assembler::lfsu(const DwVfpRegister frt, const MemOperand &src) {
  int offset = src.offset();
  Register ra = src.ra();
  ASSERT(is_int16(offset));
  ASSERT(!ra.is(r0));
  int imm16 = offset & kImm16Mask;
  // could be x_form instruction with some casting magic
  emit(LFSU | frt.code()*B21 | ra.code()*B16 | imm16);
}

void Assembler::lfsx(const DwVfpRegister frt, const MemOperand &src) {
  Register ra = src.ra();
  Register rb = src.rb();
  ASSERT(!ra.is(r0));
  emit(EXT2 | LFSX | frt.code()*B21 | ra.code()*B16 | rb.code()*B11 | LeaveRC);
}

void Assembler::lfsux(const DwVfpRegister frt, const MemOperand & src) {
  Register ra = src.ra();
  Register rb = src.rb();
  ASSERT(!ra.is(r0));
  emit(EXT2 | LFSUX | frt.code()*B21 | ra.code()*B16 | rb.code()*B11 | LeaveRC);
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

void Assembler::stfdu(const DwVfpRegister frs, const MemOperand &src) {
  int offset = src.offset();
  Register ra = src.ra();
  ASSERT(is_int16(offset));
  ASSERT(!ra.is(r0));
  int imm16 = offset & kImm16Mask;
  // could be x_form instruction with some casting magic
  emit(STFDU | frs.code()*B21 | ra.code()*B16 | imm16);
}

void Assembler::stfdx(const DwVfpRegister frs, const MemOperand &src) {
  Register ra = src.ra();
  Register rb = src.rb();
  ASSERT(!ra.is(r0));
  emit(EXT2 | STFDX | frs.code()*B21 | ra.code()*B16 | rb.code()*B11 | LeaveRC);
}

void Assembler::stfdux(const DwVfpRegister frs, const MemOperand &src) {
  Register ra = src.ra();
  Register rb = src.rb();
  ASSERT(!ra.is(r0));
  emit(EXT2 | STFDUX | frs.code()*B21 | ra.code()*B16 | rb.code()*B11 |LeaveRC);
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

void Assembler::stfsu(const DwVfpRegister frs, const MemOperand &src) {
  int offset = src.offset();
  Register ra = src.ra();
  ASSERT(is_int16(offset));
  ASSERT(!ra.is(r0));
  int imm16 = offset & kImm16Mask;
  // could be x_form instruction with some casting magic
  emit(STFSU | frs.code()*B21 | ra.code()*B16 | imm16);
}

void Assembler::stfsx(const DwVfpRegister frs, const MemOperand &src) {
  Register ra = src.ra();
  Register rb = src.rb();
  ASSERT(!ra.is(r0));
  emit(EXT2 | STFSX | frs.code()*B21 | ra.code()*B16 | rb.code()*B11 |LeaveRC);
}

void Assembler::stfsux(const DwVfpRegister frs, const MemOperand &src) {
  Register ra = src.ra();
  Register rb = src.rb();
  ASSERT(!ra.is(r0));
  emit(EXT2 | STFSUX | frs.code()*B21 | ra.code()*B16 | rb.code()*B11 |LeaveRC);
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
  ASSERT(cr.code() >= 0 && cr.code() <= 7);
  emit(EXT4 | FCMPU | cr.code()*B23 | fra.code()*B16 | frb.code()*B11);
}

void Assembler::fmr(const DwVfpRegister frt,
                    const DwVfpRegister frb,
                    RCBit rc) {
  emit(EXT4 | FMR | frt.code()*B21 | frb.code()*B11 | rc);
}

void Assembler::fctiwz(const DwVfpRegister frt,
                     const DwVfpRegister frb) {
  emit(EXT4 | FCTIWZ | frt.code()*B21 | frb.code()*B11);
}

void Assembler::fctiw(const DwVfpRegister frt,
                     const DwVfpRegister frb) {
  emit(EXT4 | FCTIW | frt.code()*B21 | frb.code()*B11);
}

void Assembler::frim(const DwVfpRegister frt,
                     const DwVfpRegister frb) {
  emit(EXT4 | FRIM | frt.code()*B21 | frb.code()*B11);
}

void Assembler::frsp(const DwVfpRegister frt,
                     const DwVfpRegister frb,
                     RCBit rc) {
  emit(EXT4 | FRSP | frt.code()*B21 | frb.code()*B11 | rc);
}

void Assembler::fcfid(const DwVfpRegister frt,
                      const DwVfpRegister frb,
                      RCBit rc) {
  emit(EXT4 | FCFID | frt.code()*B21 | frb.code()*B11 | rc);
}

void Assembler::fctid(const DwVfpRegister frt,
                      const DwVfpRegister frb,
                      RCBit rc) {
  emit(EXT4 | FCTID | frt.code()*B21 | frb.code()*B11 | rc);
}

void Assembler::fctidz(const DwVfpRegister frt,
                      const DwVfpRegister frb,
                      RCBit rc) {
  emit(EXT4 | FCTIDZ | frt.code()*B21 | frb.code()*B11 | rc);
}

void Assembler::fsel(const DwVfpRegister frt, const DwVfpRegister fra,
                     const DwVfpRegister frc, const DwVfpRegister frb,
                     RCBit rc) {
  emit(EXT4 | FSEL | frt.code()*B21 | fra.code()*B16 | frb.code()*B11 |
       frc.code()*B6 | rc);
}

void Assembler::fneg(const DwVfpRegister frt,
                     const DwVfpRegister frb,
                     RCBit rc) {
  emit(EXT4 | FNEG | frt.code()*B21 | frb.code()*B11 | rc);
}

void Assembler::mtfsfi(int bf, int immediate, RCBit rc) {
  emit(EXT4 | MTFSFI | bf*B23 | immediate*B12 | rc);
}

void Assembler::mffs(const DwVfpRegister frt, RCBit rc) {
  emit(EXT4 | MFFS | frt.code()*B21 | rc);
}

void Assembler::mtfsf(const DwVfpRegister frb, bool L,
                      int FLM, bool W, RCBit rc) {
  emit(EXT4 | MTFSF | frb.code()*B11 | W*B16 | FLM*B17 | L*B25 | rc);
}

void Assembler::fsqrt(const DwVfpRegister frt,
                      const DwVfpRegister frb,
                      RCBit rc) {
  emit(EXT4 | FSQRT | frt.code()*B21 | frb.code()*B11 | rc);
}

void Assembler::fabs(const DwVfpRegister frt,
                     const DwVfpRegister frb,
                     RCBit rc) {
  emit(EXT4 | FABS | frt.code()*B21 | frb.code()*B11 | rc);
}

// Pseudo instructions.
void Assembler::nop(int type) {
  switch (type) {
    case 0:
      ori(r0, r0, Operand::Zero());
      break;
    case DEBUG_BREAK_NOP:
      ori(r3, r3, Operand::Zero());
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
  intptr_t pc_delta = desc.buffer - buffer_;
  intptr_t rc_delta = (desc.buffer + desc.buffer_size) -
    (buffer_ + buffer_size_);
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

#if ABI_USES_FUNCTION_DESCRIPTORS
  // Relocate runtime entries.
  for (RelocIterator it(desc); !it.done(); it.next()) {
    RelocInfo::Mode rmode = it.rinfo()->rmode();
    if (rmode == RelocInfo::INTERNAL_REFERENCE) {
      intptr_t* p = reinterpret_cast<intptr_t*>(it.rinfo()->pc());
      if (*p != 0) {  // 0 means uninitialized.
        *p += pc_delta;
      }
    }
  }
#endif
}


void Assembler::db(uint8_t data) {
  CheckBuffer();
  *reinterpret_cast<uint8_t*>(pc_) = data;
  pc_ += sizeof(uint8_t);
}


void Assembler::dd(uint32_t data) {
  CheckBuffer();
  *reinterpret_cast<uint32_t*>(pc_) = data;
  pc_ += sizeof(uint32_t);
}


void Assembler::RecordRelocInfo(RelocInfo::Mode rmode, intptr_t data) {
  RelocInfo rinfo(pc_, rmode, data, NULL);
  if (rmode >= RelocInfo::JS_RETURN && rmode <= RelocInfo::DEBUG_BREAK_SLOT) {
    // Adjust code for new modes.
    ASSERT(RelocInfo::IsDebugBreakSlot(rmode)
           || RelocInfo::IsJSReturn(rmode)
           || RelocInfo::IsComment(rmode)
           || RelocInfo::IsPosition(rmode));
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


void Assembler::BlockTrampolinePoolFor(int instructions) {
  BlockTrampolinePoolBefore(pc_offset() + instructions * kInstrSize);
}


void Assembler::CheckTrampolinePool() {
  // Some small sequences of instructions must not be broken up by the
  // insertion of a trampoline pool; such sequences are protected by setting
  // either trampoline_pool_blocked_nesting_ or no_trampoline_pool_before_,
  // which are both checked here. Also, recursive calls to CheckTrampolinePool
  // are blocked by trampoline_pool_blocked_nesting_.
  if ((trampoline_pool_blocked_nesting_ > 0) ||
      (pc_offset() < no_trampoline_pool_before_)) {
    // Emission is currently blocked; make sure we try again as soon as
    // possible.
    if (trampoline_pool_blocked_nesting_ > 0) {
      next_buffer_check_ = pc_offset() + kInstrSize;
    } else {
      next_buffer_check_ = no_trampoline_pool_before_;
    }
    return;
  }

  ASSERT(!trampoline_emitted_);
  ASSERT(unbound_labels_count_ >= 0);
  if (unbound_labels_count_ > 0) {
    // First we emit jump, then we emit trampoline pool.
    { BlockTrampolinePoolScope block_trampoline_pool(this);
      Label after_pool;
      b(&after_pool);

      int pool_start = pc_offset();
      for (int i = 0; i < unbound_labels_count_; i++) {
        b(&after_pool);
      }
      bind(&after_pool);
      trampoline_ = Trampoline(pool_start, unbound_labels_count_);

      trampoline_emitted_ = true;
      // As we are only going to emit trampoline once, we need to prevent any
      // further emission.
      next_buffer_check_ = kMaxInt;
    }
  } else {
    // Number of branches to unbound label at this point is zero, so we can
    // move next buffer check to maximum.
    next_buffer_check_ = pc_offset() +
      kMaxCondBranchReach - kMaxBlockTrampolineSectionSize;
  }
  return;
}

} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_PPC
