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

// The original source code covered by the above license above has been modified
// significantly by Google Inc.
// Copyright 2012 the V8 project authors. All rights reserved.

//
// Copyright IBM Corp. 2012, 2013. All rights reserved.
//

#ifndef V8_PPC_ASSEMBLER_PPC_INL_H_
#define V8_PPC_ASSEMBLER_PPC_INL_H_

#include "ppc/assembler-ppc.h"

#include "cpu.h"
#include "debug.h"


namespace v8 {
namespace internal {


int DwVfpRegister::ToAllocationIndex(DwVfpRegister reg) {
  ASSERT(!reg.is(kDoubleRegZero));
  ASSERT(!reg.is(kScratchDoubleReg));
  return reg.code();
}


void RelocInfo::apply(intptr_t delta) {
  if (RelocInfo::IsInternalReference(rmode_)) {
    // absolute code pointer inside code object moves with the code object.
    int32_t* p = reinterpret_cast<int32_t*>(pc_);
    *p += delta;  // relocate entry
  }
  // We do not use pc relative addressing on ARM, so there is
  // nothing else to do.
}


Address RelocInfo::target_address() {
  ASSERT(IsCodeTarget(rmode_) || rmode_ == RUNTIME_ENTRY);
  return Assembler::target_address_at(pc_);
}


Address RelocInfo::target_address_address() {
  ASSERT(IsCodeTarget(rmode_) || rmode_ == RUNTIME_ENTRY
                              || rmode_ == EMBEDDED_OBJECT
                              || rmode_ == EXTERNAL_REFERENCE);

  // Read the address of the word containing the target_address in an
  // instruction stream.
  // The only architecture-independent user of this function is the serializer.
  // The serializer uses it to find out how many raw bytes of instruction to
  // output before the next target.
  // For an instruction like LUI/ORI where the target bits are mixed into the
  // instruction bits, the size of the target will be zero, indicating that the
  // serializer should not step forward in memory after a target is resolved
  // and written. In this case the target_address_address function should
  // return the end of the instructions to be patched, allowing the
  // deserializer to deserialize the instructions as raw bytes and put them in
  // place, ready to be patched with the target. After jump optimization,
  // that is the address of the instruction that follows J/JAL/JR/JALR
  // instruction.

  return reinterpret_cast<Address>(
    pc_ + 3 * Assembler::kInstrSize);
}


int RelocInfo::target_address_size() {
  return kPointerSize;  // roohack needs to be kSpecialTargetSize??
}


void RelocInfo::set_target_address(Address target, WriteBarrierMode mode) {
  ASSERT(IsCodeTarget(rmode_) || rmode_ == RUNTIME_ENTRY);
  Assembler::set_target_address_at(pc_, target);
  if (mode == UPDATE_WRITE_BARRIER && host() != NULL && IsCodeTarget(rmode_)) {
    Object* target_code = Code::GetCodeFromTargetAddress(target);
    host()->GetHeap()->incremental_marking()->RecordWriteIntoCode(
        host(), this, HeapObject::cast(target_code));
  }
}


Object* RelocInfo::target_object() {
  ASSERT(IsCodeTarget(rmode_) || rmode_ == EMBEDDED_OBJECT);
  return reinterpret_cast<Object*>(Assembler::target_address_at(pc_));
}


Handle<Object> RelocInfo::target_object_handle(Assembler* origin) {
  ASSERT(IsCodeTarget(rmode_) || rmode_ == EMBEDDED_OBJECT);
  return Handle<Object>(reinterpret_cast<Object**>(
      Assembler::target_address_at(pc_)));
}


Object** RelocInfo::target_object_address() {
  ASSERT(IsCodeTarget(rmode_) || rmode_ == EMBEDDED_OBJECT);
  reconstructed_obj_ptr_ =
      reinterpret_cast<Object*>(Assembler::target_address_at(pc_));
  return &reconstructed_obj_ptr_;
}


void RelocInfo::set_target_object(Object* target, WriteBarrierMode mode) {
  ASSERT(IsCodeTarget(rmode_) || rmode_ == EMBEDDED_OBJECT);
  Assembler::set_target_address_at(pc_, reinterpret_cast<Address>(target));
  if (mode == UPDATE_WRITE_BARRIER &&
      host() != NULL &&
      target->IsHeapObject()) {
    host()->GetHeap()->incremental_marking()->RecordWrite(
        host(), &Memory::Object_at(pc_), HeapObject::cast(target));
  }
}


Address* RelocInfo::target_reference_address() {
  ASSERT(rmode_ == EXTERNAL_REFERENCE);
  reconstructed_adr_ptr_ = Assembler::target_address_at(pc_);
  return &reconstructed_adr_ptr_;
}


Handle<JSGlobalPropertyCell> RelocInfo::target_cell_handle() {
  ASSERT(rmode_ == RelocInfo::GLOBAL_PROPERTY_CELL);
  Address address = Memory::Address_at(pc_);
  return Handle<JSGlobalPropertyCell>(
      reinterpret_cast<JSGlobalPropertyCell**>(address));
}


JSGlobalPropertyCell* RelocInfo::target_cell() {
  ASSERT(rmode_ == RelocInfo::GLOBAL_PROPERTY_CELL);
  return JSGlobalPropertyCell::FromValueAddress(Memory::Address_at(pc_));
}


void RelocInfo::set_target_cell(JSGlobalPropertyCell* cell,
                                WriteBarrierMode mode) {
  ASSERT(rmode_ == RelocInfo::GLOBAL_PROPERTY_CELL);
  Address address = cell->address() + JSGlobalPropertyCell::kValueOffset;
  Memory::Address_at(pc_) = address;
  if (mode == UPDATE_WRITE_BARRIER && host() != NULL) {
    // TODO(1550) We are passing NULL as a slot because cell can never be on
    // evacuation candidate.
    host()->GetHeap()->incremental_marking()->RecordWrite(
        host(), NULL, cell);
  }
}


Address RelocInfo::call_address() {
  // The 4 instructions offset assumes patched debug break slot or return
  // sequence.
  ASSERT((IsJSReturn(rmode()) && IsPatchedReturnSequence()) ||
         (IsDebugBreakSlot(rmode()) && IsPatchedDebugBreakSlotSequence()));
  return Memory::Address_at(pc_ + 4 * Assembler::kInstrSize);
}


void RelocInfo::set_call_address(Address target) {
  ASSERT((IsJSReturn(rmode()) && IsPatchedReturnSequence()) ||
         (IsDebugBreakSlot(rmode()) && IsPatchedDebugBreakSlotSequence()));
  Memory::Address_at(pc_ + 4 * Assembler::kInstrSize) = target;
  if (host() != NULL) {
    Object* target_code = Code::GetCodeFromTargetAddress(target);
    host()->GetHeap()->incremental_marking()->RecordWriteIntoCode(
        host(), this, HeapObject::cast(target_code));
  }
}


Object* RelocInfo::call_object() {
  return *call_object_address();
}


void RelocInfo::set_call_object(Object* target) {
  *call_object_address() = target;
}


Object** RelocInfo::call_object_address() {
  ASSERT((IsJSReturn(rmode()) && IsPatchedReturnSequence()) ||
         (IsDebugBreakSlot(rmode()) && IsPatchedDebugBreakSlotSequence()));
  return reinterpret_cast<Object**>(pc_ + 4 * Assembler::kInstrSize);
}


bool RelocInfo::IsPatchedReturnSequence() {
#if 0
  Instr current_instr = Assembler::instr_at(pc_);
  Instr next_instr = Assembler::instr_at(pc_ + Assembler::kInstrSize);
  //
  // The patched return sequence is defined by
  // BreakLocationIterator::SetDebugBreakAtReturn()
  //
  // The patched return sequence is:
  //  mr  lr, pc        where lr = r14, and pc = r15
  //  lwz pc, -4(pc)
  return (current_instr == kMrLRPC)
          && ((next_instr & kLwzPCMask) == kLwzPCPattern);
#else
  Instr instr0 = Assembler::instr_at(pc_);
  Instr instr1 = Assembler::instr_at(pc_ + 1 * Assembler::kInstrSize);
//  Instr instr2 = Assembler::instr_at(pc_ + 2 * Assembler::kInstrSize);
  Instr instr5 = Assembler::instr_at(pc_ + 5 * Assembler::kInstrSize);
  bool patched_return = ((instr0 & kOpcodeMask) == ADDIS &&
                         (instr1 & kOpcodeMask) == ADDIC &&
                         (instr5 == 0x7d821008));

// printf("IsPatchedReturnSequence: %d\n", patched_return);
  return patched_return;
#endif
}


bool RelocInfo::IsPatchedDebugBreakSlotSequence() {
  Instr current_instr = Assembler::instr_at(pc_);
  return !Assembler::IsNop(current_instr, Assembler::DEBUG_BREAK_NOP);
}


void RelocInfo::Visit(ObjectVisitor* visitor) {
  RelocInfo::Mode mode = rmode();
  if (mode == RelocInfo::EMBEDDED_OBJECT) {
    visitor->VisitEmbeddedPointer(this);
  } else if (RelocInfo::IsCodeTarget(mode)) {
    visitor->VisitCodeTarget(this);
  } else if (mode == RelocInfo::GLOBAL_PROPERTY_CELL) {
    visitor->VisitGlobalPropertyCell(this);
  } else if (mode == RelocInfo::EXTERNAL_REFERENCE) {
    visitor->VisitExternalReference(this);
#ifdef ENABLE_DEBUGGER_SUPPORT
  // TODO(isolates): Get a cached isolate below.
  } else if (((RelocInfo::IsJSReturn(mode) &&
              IsPatchedReturnSequence()) ||
             (RelocInfo::IsDebugBreakSlot(mode) &&
              IsPatchedDebugBreakSlotSequence())) &&
             Isolate::Current()->debug()->has_break_points()) {
    visitor->VisitDebugTarget(this);
#endif
  } else if (mode == RelocInfo::RUNTIME_ENTRY) {
    visitor->VisitRuntimeEntry(this);
  }
}


template<typename StaticVisitor>
void RelocInfo::Visit(Heap* heap) {
  RelocInfo::Mode mode = rmode();
  if (mode == RelocInfo::EMBEDDED_OBJECT) {
    StaticVisitor::VisitEmbeddedPointer(heap, this);
  } else if (RelocInfo::IsCodeTarget(mode)) {
    StaticVisitor::VisitCodeTarget(heap, this);
  } else if (mode == RelocInfo::GLOBAL_PROPERTY_CELL) {
    StaticVisitor::VisitGlobalPropertyCell(heap, this);
  } else if (mode == RelocInfo::EXTERNAL_REFERENCE) {
    StaticVisitor::VisitExternalReference(this);
#ifdef ENABLE_DEBUGGER_SUPPORT
  } else if (heap->isolate()->debug()->has_break_points() &&
             ((RelocInfo::IsJSReturn(mode) &&
              IsPatchedReturnSequence()) ||
             (RelocInfo::IsDebugBreakSlot(mode) &&
              IsPatchedDebugBreakSlotSequence()))) {
    StaticVisitor::VisitDebugTarget(heap, this);
#endif
  } else if (mode == RelocInfo::RUNTIME_ENTRY) {
    StaticVisitor::VisitRuntimeEntry(this);
  }
}


Operand::Operand(int32_t immediate, RelocInfo::Mode rmode)  {
  rm_ = no_reg;
  imm32_ = immediate;
  rmode_ = rmode;
}


Operand::Operand(const ExternalReference& f)  {
  rm_ = no_reg;
  imm32_ = reinterpret_cast<int32_t>(f.address());
  rmode_ = RelocInfo::EXTERNAL_REFERENCE;
}


Operand::Operand(Smi* value) {
  rm_ = no_reg;
  imm32_ =  reinterpret_cast<intptr_t>(value);
  rmode_ = RelocInfo::NONE;
}


Operand::Operand(Register rm) {
  rm_ = rm;
  rs_ = no_reg;
  shift_op_ = LSL;
  shift_imm_ = 0;
  rmode_ = RelocInfo::NONE;  // PPC -why doesn't ARM do this?
}


bool Operand::is_reg() const {
  return rm_.is_valid() &&
         rs_.is(no_reg) &&
         shift_op_ == LSL &&
         shift_imm_ == 0;
}


void Assembler::CheckBuffer() {
  if (buffer_space() <= kGap) {
    GrowBuffer();
  }
  if (pc_offset() >= next_buffer_check_) {
    CheckConstPool(false, true);
  }
}


void Assembler::emit(Instr x) {
  CheckBuffer();
  *reinterpret_cast<Instr*>(pc_) = x;
  pc_ += kInstrSize;
}


#if 0
Address Assembler::target_address_address_at(Address pc) {
  Address target_pc = pc;
  Instr instr = Memory::int32_at(target_pc);
  // If we have a bx instruction, the instruction before the bx is
  // what we need to patch.
  static const int32_t kBxInstMask = 0x0ffffff0;
  static const int32_t kBxInstPattern = 0x012fff10;
  if ((instr & kBxInstMask) == kBxInstPattern) {
    target_pc -= kInstrSize;
    instr = Memory::int32_at(target_pc);
  }

  ASSERT(IsLdrPcImmediateOffset(instr));
  int offset = instr & 0xfff;  // offset_12 is unsigned
  if ((instr & (1 << 23)) == 0) offset = -offset;  // U bit defines offset sign
  // Verify that the constant pool comes after the instruction referencing it.
  ASSERT(offset >= -4);
  return target_pc + offset + 8;
}
#endif


// This is functional, but potentially wrong
// Needs to be reviewed (roohack)
Address Assembler::target_address_at(Address pc) {
  Instr instr1 = instr_at(pc);
  Instr instr2 = instr_at(pc + kInstrSize);
  // Interpret 2 instructions generated by lis/addic
  if (IsLis(instr1) && IsAddic(instr2)) {
    // Assemble the 32 bit value.
    return reinterpret_cast<Address>(
        ((instr1 & kImm16Mask) << 16) +
        (((instr2 & kImm16Mask) << 16) >> 16));
  }

#ifdef PENGUIN_CLEANUP
  // We should never get here, force a bad address if we do.
  // UNREACHABLE();  -- temporary removal for ARM code..
  // return (Address)0xdeadbeef;

// for now fall into ARM code.. for compatibility

  Address target_pc = pc;
  Instr instr = Memory::int32_at(target_pc);
  // If we have a bx instruction, the instruction before the bx is
  // what we need to patch.
  static const int32_t kBxInstMask = 0x0ffffff0;
  static const int32_t kBxInstPattern = 0x012fff10;
  if ((instr & kBxInstMask) == kBxInstPattern) {
    target_pc -= kInstrSize;
    instr = Memory::int32_at(target_pc);
  }

#ifdef USE_BLX
  // If we have a blx instruction, the instruction before it is
  // what needs to be patched.
  if ((instr & kBlxRegMask) == kBlxRegPattern) {
    target_pc -= kInstrSize;
    instr = Memory::int32_at(target_pc);
  }
#endif

  ASSERT(IsLdrPcImmediateOffset(instr));
  int offset = instr & 0xfff;  // offset_12 is unsigned
  if ((instr & (1 << 23)) == 0) offset = -offset;  // U bit defines offset sign
  // Verify that the constant pool comes after the instruction referencing it.
  ASSERT(offset >= -4);
  return target_pc + offset + 8;
#else
  PPCPORT_UNIMPLEMENTED();
return (Address)0;
//  return pc;  // fake a return (unimplemented path)
#endif
}


void Assembler::deserialization_set_special_target_at(
    Address constant_pool_entry, Address target) {
  Memory::Address_at(constant_pool_entry) = target;
}


void Assembler::set_external_target_at(Address constant_pool_entry,
                                       Address target) {
  Memory::Address_at(constant_pool_entry) = target;
}


void Assembler::set_target_address_at(Address pc, Address target) {
  Instr instr1 = instr_at(pc);
  Instr instr2 = instr_at(pc + kInstrSize);
  // Interpret 2 instructions generated by lis/addic
  if (IsLis(instr1) && IsAddic(instr2)) {
    uint32_t* p = reinterpret_cast<uint32_t*>(pc);
    uint32_t itarget = reinterpret_cast<uint32_t>(target);
    int lo_word = itarget & kImm16Mask;
    int hi_word = itarget >> 16;
    if (lo_word & 0x8000) {
      // lo word is signed, so increment hi word by one
      hi_word++;
    }
    instr1 &= ~kImm16Mask;
    instr1 |= hi_word;
    instr2 &= ~kImm16Mask;
    instr2 |= lo_word;

    *p = instr1;
    *(p+1) = instr2;
  } else {
    // do what ARM did
    Memory::Address_at(target_address_at(pc)) = target;
  }
  // TODO(roo): need to flush icache

#if 0
  Instr instr2 = instr_at(pc + kInstrSize);
  uint32_t rt_code = GetRtField(instr2);
  uint32_t* p = reinterpret_cast<uint32_t*>(pc);
  uint32_t itarget = reinterpret_cast<uint32_t>(target);

#ifdef DEBUG
  // Check we have the result from a li macro-instruction, using instr pair.
  Instr instr1 = instr_at(pc);
  CHECK((GetOpcodeField(instr1) == LUI && GetOpcodeField(instr2) == ORI));
#endif

  // Must use 2 instructions to insure patchable code => just use lui and ori.
  // lui rt, upper-16.
  // ori rt rt, lower-16.
  *p = LUI | rt_code | ((itarget & kHiMask) >> kLuiShift);
  *(p+1) = ORI | rt_code | (rt_code << 5) | (itarget & kImm16Mask);

  // The following code is an optimization for the common case of Call()
  // or Jump() which is load to register, and jump through register:
  //     li(t9, address); jalr(t9)    (or jr(t9)).
  // If the destination address is in the same 256 MB page as the call, it
  // is faster to do a direct jal, or j, rather than jump thru register, since
  // that lets the cpu pipeline prefetch the target address. However each
  // time the address above is patched, we have to patch the direct jal/j
  // instruction, as well as possibly revert to jalr/jr if we now cross a
  // 256 MB page. Note that with the jal/j instructions, we do not need to
  // load the register, but that code is left, since it makes it easy to
  // revert this process. A further optimization could try replacing the
  // li sequence with nops.
  // This optimization can only be applied if the rt-code from instr2 is the
  // register used for the jalr/jr. Finally, we have to skip 'jr ra', which is
  // mips return. Occasionally this lands after an li().

  Instr instr3 = instr_at(pc + 2 * kInstrSize);
  uint32_t ipc = reinterpret_cast<uint32_t>(pc + 3 * kInstrSize);
  bool in_range =
             ((uint32_t)(ipc ^ itarget) >> (kImm26Bits + kImmFieldShift)) == 0;
  uint32_t target_field = (uint32_t)(itarget & kJumpAddrMask) >> kImmFieldShift;
  bool patched_jump = false;

#ifndef ALLOW_JAL_IN_BOUNDARY_REGION
  // This is a workaround to the 24k core E156 bug (affect some 34k cores also).
  // Since the excluded space is only 64KB out of 256MB (0.02 %), we will just
  // apply this workaround for all cores so we don't have to identify the core.
  if (in_range) {
    // The 24k core E156 bug has some very specific requirements, we only check
    // the most simple one: if the address of the delay slot instruction is in
    // the first or last 32 KB of the 256 MB segment.
    uint32_t segment_mask = ((256 * MB) - 1) ^ ((32 * KB) - 1);
    uint32_t ipc_segment_addr = ipc & segment_mask;
    if (ipc_segment_addr == 0 || ipc_segment_addr == segment_mask)
      in_range = false;
  }
#endif

  if (IsJalr(instr3)) {
    // Try to convert JALR to JAL.
    if (in_range && GetRt(instr2) == GetRs(instr3)) {
      *(p+2) = JAL | target_field;
      patched_jump = true;
    }
  } else if (IsJr(instr3)) {
    // Try to convert JR to J, skip returns (jr ra).
    bool is_ret = static_cast<int>(GetRs(instr3)) == ra.code();
    if (in_range && !is_ret && GetRt(instr2) == GetRs(instr3)) {
      *(p+2) = J | target_field;
      patched_jump = true;
    }
  } else if (IsJal(instr3)) {
    if (in_range) {
      // We are patching an already converted JAL.
      *(p+2) = JAL | target_field;
    } else {
      // Patch JAL, but out of range, revert to JALR.
      // JALR rs reg is the rt reg specified in the ORI instruction.
      uint32_t rs_field = GetRt(instr2) << kRsShift;
      uint32_t rd_field = ra.code() << kRdShift;  // Return-address (ra) reg.
      *(p+2) = SPECIAL | rs_field | rd_field | JALR;
    }
    patched_jump = true;
  } else if (IsJ(instr3)) {
    if (in_range) {
      // We are patching an already converted J (jump).
      *(p+2) = J | target_field;
    } else {
      // Trying patch J, but out of range, just go back to JR.
      // JR 'rs' reg is the 'rt' reg specified in the ORI instruction (instr2).
      uint32_t rs_field = GetRt(instr2) << kRsShift;
      *(p+2) = SPECIAL | rs_field | JR;
    }
    patched_jump = true;
  }
  CPU::FlushICache(pc, (patched_jump ? 3 : 2) * sizeof(int32_t));
#endif
}

} }  // namespace v8::internal

#endif  // V8_PPC_ASSEMBLER_PPC_INL_H_
