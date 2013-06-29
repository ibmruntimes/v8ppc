// Copyright 2012 the V8 project authors. All rights reserved.
//
// Copyright IBM Corp. 2012, 2013. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <limits.h>  // For LONG_MIN, LONG_MAX.

#include "v8.h"

#if defined(V8_TARGET_ARCH_PPC)

#include "bootstrapper.h"
#include "codegen.h"
#include "debug.h"
#include "runtime.h"

namespace v8 {
namespace internal {

MacroAssembler::MacroAssembler(Isolate* arg_isolate, void* buffer, int size)
    : Assembler(arg_isolate, buffer, size),
      generating_stub_(false),
      allow_stub_calls_(true),
      has_frame_(false) {
  if (isolate() != NULL) {
    code_object_ = Handle<Object>(isolate()->heap()->undefined_value(),
                                  isolate());
  }
}



void MacroAssembler::Jump(Register target, Condition cond) {
  ASSERT(cond == al);
  mtctr(target);
  bcr();
}


void MacroAssembler::Jump(intptr_t target, RelocInfo::Mode rmode,
                          Condition cond) {
  ASSERT(rmode == RelocInfo::CODE_TARGET);
  ASSERT(cond == al);
  int addr = *(reinterpret_cast<int*>(target));
  addr = addr + Code::kHeaderSize - kHeapObjectTag;   // PPC - ugly
  mov(r0, Operand(addr));
  mtctr(r0);
  bcr();
  //  mov(pc, Operand(target, rmode), LeaveCC, cond);
}


void MacroAssembler::Jump(Address target, RelocInfo::Mode rmode,
                          Condition cond) {
  ASSERT(!RelocInfo::IsCodeTarget(rmode));
  Jump(reinterpret_cast<intptr_t>(target), rmode, cond);
}


void MacroAssembler::Jump(Handle<Code> code, RelocInfo::Mode rmode,
                          Condition cond) {
  ASSERT(RelocInfo::IsCodeTarget(rmode));
  // 'code' is always generated ARM code, never THUMB code
  Jump(reinterpret_cast<intptr_t>(code.location()), rmode, cond);
}


int MacroAssembler::CallSize(Register target, Condition cond) {
  return 2 * kInstrSize;
}


void MacroAssembler::Call(Register target, Condition cond) {
  // Block constant pool for the call instruction sequence.
  BlockConstPoolScope block_const_pool(this);
  Label start;
  bind(&start);
  ASSERT(cond == al);  // in prep of removal of condition

  // Statement positions are expected to be recorded when the target
  // address is loaded.
  positions_recorder()->WriteRecordedPositions();

  // branch via link register and set LK bit for return point
  mtlr(target);
  bclr(BA, SetLK);
  ASSERT_EQ(CallSize(target, cond), SizeOfCodeGeneratedSince(&start));
}


int MacroAssembler::CallSize(
    Address target, RelocInfo::Mode rmode, Condition cond) {
  int size;
  int movSize;

#if 0
  // Account for variable length Assembler::mov sequence.
  intptr_t value = reinterpret_cast<intptr_t>(target);
  if (is_int16(value) || (((value >> 16) << 16) == value)) {
    movSize = 1;
  } else {
    movSize = 2;
  }
#else
  movSize = 2;
#endif

  size = (2 + movSize) * kInstrSize;

#if 0
  Instr mov_instr = cond | MOV | LeaveCC;
  intptr_t immediate = reinterpret_cast<intptr_t>(target);
  if (!Operand(immediate, rmode).is_single_instruction(this, mov_instr)) {
    size += kInstrSize;
  }
#endif
  return size;
}


int MacroAssembler::CallSizeNotPredictableCodeSize(
    Address target, RelocInfo::Mode rmode, Condition cond) {
#ifndef PENGUIN_CLEANUP
  PPCPORT_UNIMPLEMENTED();  // Always fail
  return 0;
#else
  int size = 2 * kInstrSize;
  Instr mov_instr = cond | MOV | LeaveCC;
  intptr_t immediate = reinterpret_cast<intptr_t>(target);
  if (!Operand(immediate, rmode).is_single_instruction(NULL, mov_instr)) {
    size += kInstrSize;
  }
  return size;
#endif
}


void MacroAssembler::Call(Address target,
                          RelocInfo::Mode rmode,
                          Condition cond) {
  ASSERT(cond == al);
  // Block constant pool for the call instruction sequence.
  BlockConstPoolScope block_const_pool(this);
  Label start;
  bind(&start);

  // Statement positions are expected to be recorded when the target
  // address is loaded.
  positions_recorder()->WriteRecordedPositions();

  // This can likely be optimized to make use of bc() with 24bit relative
  //
  // RecordRelocInfo(x.rmode_, x.imm32_);
  // bc( BA, .... offset, LKset);
  //

  mov(ip, Operand(reinterpret_cast<int32_t>(target), rmode));
  mtlr(ip);
  bclr(BA, SetLK);

  ASSERT(kCallTargetAddressOffset == 4 * kInstrSize);
  ASSERT_EQ(CallSize(target, rmode, cond), SizeOfCodeGeneratedSince(&start));
}


int MacroAssembler::CallSize(Handle<Code> code,
                             RelocInfo::Mode rmode,
                             TypeFeedbackId ast_id,
                             Condition cond) {
  return CallSize(reinterpret_cast<Address>(code.location()), rmode, cond);
}


void MacroAssembler::Call(Handle<Code> code,
                          RelocInfo::Mode rmode,
                          TypeFeedbackId ast_id,
                          Condition cond) {
  Label start;
  bind(&start);
  ASSERT(RelocInfo::IsCodeTarget(rmode));
  if (rmode == RelocInfo::CODE_TARGET && !ast_id.IsNone()) {
    SetRecordedAstId(ast_id);
    rmode = RelocInfo::CODE_TARGET_WITH_ID;
  }
  // 'code' is always generated ARM code, never THUMB code
  Call(reinterpret_cast<Address>(code.location()), rmode, cond);
  ASSERT_EQ(CallSize(code, rmode, ast_id, cond),
            SizeOfCodeGeneratedSince(&start));
}


void MacroAssembler::Ret(Condition cond) {
  ASSERT(cond == al);
  blr();
}


void MacroAssembler::Drop(int count, Condition cond) {
  ASSERT(cond == al);
  if (count > 0) {
    Add(sp, sp, count * kPointerSize, r0);
  }
}


void MacroAssembler::Ret(int drop, Condition cond) {
  Drop(drop, cond);
  Ret(cond);
}


void MacroAssembler::Swap(Register reg1,
                          Register reg2,
                          Register scratch,
                          Condition cond) {
#ifdef PENGUIN_CLEANUP
  // Bogus instruction, inserted so we can trap here if we execute
  ldr(r0, MemOperand(r0));
  if (scratch.is(no_reg)) {
    eor(reg1, reg1, Operand(reg2), LeaveCC, cond);
    eor(reg2, reg2, Operand(reg1), LeaveCC, cond);
    eor(reg1, reg1, Operand(reg2), LeaveCC, cond);
  } else {
    mov(scratch, reg1, LeaveCC, cond);
    mov(reg1, reg2, LeaveCC, cond);
    mov(reg2, scratch, LeaveCC, cond);
  }
#else
  PPCPORT_UNIMPLEMENTED();
  fake_asm(fMASM16);
#endif
}


void MacroAssembler::Call(Label* target) {
  bl(target);
}


void MacroAssembler::Push(Handle<Object> handle) {
  mov(ip, Operand(handle));
  push(ip);
}


void MacroAssembler::Move(Register dst, Handle<Object> value) {
  mov(dst, Operand(value));
}


void MacroAssembler::Move(Register dst, Register src, Condition cond) {
  ASSERT(cond == al);
  if (!dst.is(src)) {
    mr(dst, src);
  }
}


void MacroAssembler::Move(DoubleRegister dst, DoubleRegister src) {
#ifdef PENGUIN_CLEANUP
  ASSERT(CpuFeatures::IsSupported(VFP2));
  CpuFeatures::Scope scope(VFP2);
  if (!dst.is(src)) {
    vmov(dst, src);
  }
#else
  PPCPORT_UNIMPLEMENTED();
  fake_asm(fMASM17);
#endif
}

void MacroAssembler::Ubfx(Register dst, Register src1, int lsb, int width,
                          Condition cond) {
  ASSERT(lsb < 32);
  if (!CpuFeatures::IsSupported(ARMv7) || predictable_code_size()) {
    int mask = (1 << (width + lsb)) - 1 - ((1 << lsb) - 1);
    and_(dst, src1, Operand(mask), LeaveCC, cond);
    if (lsb != 0) {
      mov(dst, Operand(dst, LSR, lsb), LeaveCC, cond);
    }
  } else {
    ubfx(dst, src1, lsb, width, cond);
  }
}


void MacroAssembler::Sbfx(Register dst, Register src1, int lsb, int width,
                          Condition cond) {
  ASSERT(cond == al);
  ASSERT(lsb < 32);
  int mask = (1 << (width + lsb)) - 1 - ((1 << lsb) - 1);
  mov(r0, Operand(mask));
  and_(dst, src1, r0);
  int shift_up = 32 - lsb - width;
  int shift_down = lsb + shift_up;
  if (shift_up != 0) {
    slwi(dst, dst, Operand(shift_up));
  }
  if (shift_down != 0) {
    srawi(dst, dst, shift_down);
  }
}

void MacroAssembler::Usat(Register dst, int satpos, const Operand& src,
                          Condition cond) {
#ifdef PENGUIN_CLEANUP
  if (!CpuFeatures::IsSupported(ARMv7) || predictable_code_size()) {
    ASSERT((satpos >= 0) && (satpos <= 31));

    // These asserts are required to ensure compatibility with the ARMv7
    // implementation.
    ASSERT((src.shift_op() == ASR) || (src.shift_op() == LSL));
    ASSERT(src.rs().is(no_reg));

    Label done;
    int satval = (1 << satpos) - 1;

    if (cond != al) {
      b(NegateCondition(cond), &done);  // Skip saturate if !condition.
    }
    if (!(src.is_reg() && dst.is(src.rm()))) {
      mov(dst, src);
    }
    mov(r0, Operand(~satval));
    and_(r0, dst, r0, SetRC);
    beq(&done, cr0);
    mov(dst, Operand(0, RelocInfo::NONE), LeaveCC, mi);  // 0 if negative.
    mov(dst, Operand(satval), LeaveCC, pl);  // satval if positive.
    bind(&done);
  } else {
    usat(dst, satpos, src, cond);
  }
#else
  PPCPORT_UNIMPLEMENTED();
  fake_asm(fMASM29);
#endif
}

void MacroAssembler::MultiPush(RegList regs) {
  int16_t num_to_push = NumberOfBitsSet(regs);
  int16_t stack_offset = num_to_push * kPointerSize;

  sub(sp, sp, Operand(stack_offset));
  for (int16_t i = kNumRegisters - 1; i >= 0; i--) {
    if ((regs & (1 << i)) != 0) {
      stack_offset -= kPointerSize;
      stw(ToRegister(i), MemOperand(sp, stack_offset));
    }
  }
}

void MacroAssembler::MultiPop(RegList regs) {
  int16_t stack_offset = 0;

  for (int16_t i = 0; i < kNumRegisters; i++) {
    if ((regs & (1 << i)) != 0) {
      lwz(ToRegister(i), MemOperand(sp, stack_offset));
      stack_offset += kPointerSize;
    }
  }
  addi(sp, sp, Operand(stack_offset));
}


void MacroAssembler::LoadRoot(Register destination,
                              Heap::RootListIndex index,
                              Condition cond) {
  lwz(destination, MemOperand(kRootRegister, index << kPointerSizeLog2));
}


void MacroAssembler::StoreRoot(Register source,
                               Heap::RootListIndex index,
                               Condition cond) {
  stw(source, MemOperand(kRootRegister, index << kPointerSizeLog2));
}


void MacroAssembler::LoadHeapObject(Register result,
                                    Handle<HeapObject> object) {
  if (isolate()->heap()->InNewSpace(*object)) {
    Handle<JSGlobalPropertyCell> cell =
        isolate()->factory()->NewJSGlobalPropertyCell(object);
    mov(result, Operand(cell));
    lwz(result, FieldMemOperand(result, JSGlobalPropertyCell::kValueOffset));
  } else {
    mov(result, Operand(object));
  }
}


void MacroAssembler::InNewSpace(Register object,
                                Register scratch,
                                Condition cond,
                                Label* branch) {
  ASSERT(cond == eq || cond == ne);
  mov(scratch, Operand(ExternalReference::new_space_mask(isolate())));
  and_(r0, scratch, object);
  mov(scratch, Operand(ExternalReference::new_space_start(isolate())));
  cmp(r0, scratch);
  b(cond, branch);
}


void MacroAssembler::RecordWriteField(
    Register object,
    int offset,
    Register value,
    Register dst,
    LinkRegisterStatus lr_status,
    SaveFPRegsMode save_fp,
    RememberedSetAction remembered_set_action,
    SmiCheck smi_check) {
  // First, check if a write barrier is even needed. The tests below
  // catch stores of Smis.
  Label done;

  // Skip barrier if writing a smi.
  if (smi_check == INLINE_SMI_CHECK) {
    JumpIfSmi(value, &done);
  }

  // Although the object register is tagged, the offset is relative to the start
  // of the object, so so offset must be a multiple of kPointerSize.
  ASSERT(IsAligned(offset, kPointerSize));

  addi(dst, object, Operand(offset - kHeapObjectTag));
  if (emit_debug_code()) {
    Label ok;
    andi(r0, dst, Operand((1 << kPointerSizeLog2) - 1));
    beq(&ok, cr0);
    stop("Unaligned cell in write barrier");
    bind(&ok);
  }

  RecordWrite(object,
              dst,
              value,
              lr_status,
              save_fp,
              remembered_set_action,
              OMIT_SMI_CHECK);

  bind(&done);

  // Clobber clobbered input registers when running with the debug-code flag
  // turned on to provoke errors.
  if (emit_debug_code()) {
    mov(value, Operand(BitCast<int32_t>(kZapValue + 4)));
    mov(dst, Operand(BitCast<int32_t>(kZapValue + 8)));
  }
}


// Will clobber 4 registers: object, address, scratch, ip.  The
// register 'object' contains a heap object pointer.  The heap object
// tag is shifted away.
void MacroAssembler::RecordWrite(Register object,
                                 Register address,
                                 Register value,
                                 LinkRegisterStatus lr_status,
                                 SaveFPRegsMode fp_mode,
                                 RememberedSetAction remembered_set_action,
                                 SmiCheck smi_check) {
  // The compiled code assumes that record write doesn't change the
  // context register, so we check that none of the clobbered
  // registers are cp.
  ASSERT(!address.is(cp) && !value.is(cp));

  if (emit_debug_code()) {
    lwz(ip, MemOperand(address));
    cmp(ip, value);
    Check(eq, "Wrong address or value passed to RecordWrite");
  }

  Label done;

  if (smi_check == INLINE_SMI_CHECK) {
    JumpIfSmi(value, &done);
  }

  CheckPageFlag(value,
                value,  // Used as scratch.
                MemoryChunk::kPointersToHereAreInterestingMask,
                eq,
                &done);
  CheckPageFlag(object,
                value,  // Used as scratch.
                MemoryChunk::kPointersFromHereAreInterestingMask,
                eq,
                &done);

  // Record the actual write.
  if (lr_status == kLRHasNotBeenSaved) {
    mflr(r0);
    push(r0);
  }
  RecordWriteStub stub(object, value, address, remembered_set_action, fp_mode);
  CallStub(&stub);
  if (lr_status == kLRHasNotBeenSaved) {
    pop(r0);
    mtlr(r0);
  }

  bind(&done);

  // Clobber clobbered registers when running with the debug-code flag
  // turned on to provoke errors.
  if (emit_debug_code()) {
    mov(address, Operand(BitCast<int32_t>(kZapValue + 12)));
    mov(value, Operand(BitCast<int32_t>(kZapValue + 16)));
  }
}


void MacroAssembler::RememberedSetHelper(Register object,  // For debug tests.
                                         Register address,
                                         Register scratch,
                                         SaveFPRegsMode fp_mode,
                                         RememberedSetFinalAction and_then) {
  Label done;
  if (emit_debug_code()) {
    Label ok;
    JumpIfNotInNewSpace(object, scratch, &ok);
    stop("Remembered set pointer is in new space");
    bind(&ok);
  }
  // Load store buffer top.
  ExternalReference store_buffer =
      ExternalReference::store_buffer_top(isolate());
  mov(ip, Operand(store_buffer));
  lwz(scratch, MemOperand(ip));
  // Store pointer to buffer and increment buffer top.
  stw(address, MemOperand(scratch));
  addi(scratch, scratch, Operand(kPointerSize));
  // Write back new top of buffer.
  stw(scratch, MemOperand(ip));
  // Call stub on end of buffer.
  // Check for end of buffer.
  mov(r0, Operand(StoreBuffer::kStoreBufferOverflowBit));
  and_(r0, scratch, r0, SetRC);

  if (and_then == kFallThroughAtEnd) {
    beq(&done, cr0);
  } else {
    ASSERT(and_then == kReturnAtEnd);
    beq(&done, cr0);
  }
  mflr(r0);
  push(r0);
  StoreBufferOverflowStub store_buffer_overflow =
      StoreBufferOverflowStub(fp_mode);
  CallStub(&store_buffer_overflow);
  pop(r0);
  mtlr(r0);
  bind(&done);
  if (and_then == kReturnAtEnd) {
    Ret();
  }
}


// Push and pop all registers that can hold pointers.
void MacroAssembler::PushSafepointRegisters() {
#ifdef PENGUIN_CLEANUP
  // Safepoints expect a block of contiguous register values starting with r0:
  ASSERT(((1 << kNumSafepointSavedRegisters) - 1) == kSafepointSavedRegisters);
  // Safepoints expect a block of kNumSafepointRegisters values on the
  // stack, so adjust the stack for unsaved registers.
  const int num_unsaved = kNumSafepointRegisters - kNumSafepointSavedRegisters;
  ASSERT(num_unsaved >= 0);
  sub(sp, sp, Operand(num_unsaved * kPointerSize));
  stm(db_w, sp, kSafepointSavedRegisters);
#else
  PPCPORT_UNIMPLEMENTED();
  fake_asm(fMASM20);
#endif
}


void MacroAssembler::PopSafepointRegisters() {
#ifdef PENGUIN_CLEANUP
  const int num_unsaved = kNumSafepointRegisters - kNumSafepointSavedRegisters;
  ldm(ia_w, sp, kSafepointSavedRegisters);
  addi(sp, sp, Operand(num_unsaved * kPointerSize));
#else
  PPCPORT_UNIMPLEMENTED();
  fake_asm(fMASM21);
#endif
}


void MacroAssembler::PushSafepointRegistersAndDoubles() {
#ifdef PENGUIN_CLEANUP
  PushSafepointRegisters();
  sub(sp, sp, Operand(DwVfpRegister::kNumAllocatableRegisters *
                      kDoubleSize));
  for (int i = 0; i < DwVfpRegister::kNumAllocatableRegisters; i++) {
    vstr(DwVfpRegister::FromAllocationIndex(i), sp, i * kDoubleSize);
  }
#else
  PPCPORT_UNIMPLEMENTED();
  fake_asm(fMASM22);
#endif
}


void MacroAssembler::PopSafepointRegistersAndDoubles() {
#ifdef PENGUIN_CLEANUP
  for (int i = 0; i < DwVfpRegister::kNumAllocatableRegisters; i++) {
    vldr(DwVfpRegister::FromAllocationIndex(i), sp, i * kDoubleSize);
  }
  addi(sp, sp, Operand(DwVfpRegister::kNumAllocatableRegisters *
                      kDoubleSize));
  PopSafepointRegisters();
#else
  PPCPORT_UNIMPLEMENTED();
  fake_asm(fMASM23);
#endif
}

void MacroAssembler::StoreToSafepointRegistersAndDoublesSlot(Register src,
                                                             Register dst) {
#ifdef PENGUIN_CLEANUP
  str(src, SafepointRegistersAndDoublesSlot(dst));
#else
  PPCPORT_UNIMPLEMENTED();
  fake_asm(fMASM24);
#endif
}


void MacroAssembler::StoreToSafepointRegisterSlot(Register src, Register dst) {
#ifdef PENGUIN_CLEANUP
  str(src, SafepointRegisterSlot(dst));
#else
  PPCPORT_UNIMPLEMENTED();
  fake_asm(fMASM25);
#endif
}


void MacroAssembler::LoadFromSafepointRegisterSlot(Register dst, Register src) {
#ifdef PENGUIN_CLEANUP
  ldr(dst, SafepointRegisterSlot(src));
#else
  PPCPORT_UNIMPLEMENTED();
  fake_asm(fMASM26);
#endif
}


int MacroAssembler::SafepointRegisterStackIndex(int reg_code) {
  // The registers are pushed starting with the highest encoding,
  // which means that lowest encodings are closest to the stack pointer.
  ASSERT(reg_code >= 0 && reg_code < kNumSafepointRegisters);
  return reg_code;
}


MemOperand MacroAssembler::SafepointRegisterSlot(Register reg) {
  return MemOperand(sp, SafepointRegisterStackIndex(reg.code()) * kPointerSize);
}


MemOperand MacroAssembler::SafepointRegistersAndDoublesSlot(Register reg) {
  // General purpose registers are pushed last on the stack.
  int doubles_size = DwVfpRegister::kNumAllocatableRegisters * kDoubleSize;
  int register_offset = SafepointRegisterStackIndex(reg.code()) * kPointerSize;
  return MemOperand(sp, doubles_size + register_offset);
}


void MacroAssembler::Ldrd(Register dst1, Register dst2,
                          const MemOperand& src, Condition cond) {
#ifdef PENGUIN_CLEANUP
  ldr(dst1, MemOperand(dst2), al);  // PPC - bogus instruction to cause error
#if 0
  ASSERT(src.rm().is(no_reg));
  ASSERT(!dst1.is(lr));  // r14.
  // ASSERT_EQ(0, dst1.code() % 2);
  ASSERT_EQ(dst1.code() + 1, dst2.code());

  // V8 does not use this addressing mode, so the fallback code
  // below doesn't support it yet.
  ASSERT((src.am() != PreIndex) && (src.am() != NegPreIndex));

  // Generate two lwz instructions if lwzd is not available.
  if (CpuFeatures::IsSupported(ARMv7) && !predictable_code_size()) {
    CpuFeatures::Scope scope(ARMv7);
    ldrd(dst1, dst2, src, cond);
  } else {
    if ((src.am() == Offset) || (src.am() == NegOffset)) {
      MemOperand src2(src);
      src2.set_offset(src2.offset() + 4);
      if (dst1.is(src.rn())) {
        ldr(dst2, src2, cond);
        ldr(dst1, src, cond);
      } else {
        ldr(dst1, src, cond);
        ldr(dst2, src2, cond);
      }
    } else {  // PostIndex or NegPostIndex.
      ASSERT((src.am() == PostIndex) || (src.am() == NegPostIndex));
      if (dst1.is(src.rn())) {
        ldr(dst2, MemOperand(src.rn(), 4, Offset), cond);
        ldr(dst1, src, cond);
      } else {
        MemOperand src2(src);
        src2.set_offset(src2.offset() - 4);
        ldr(dst1, MemOperand(src.rn(), 4, PostIndex), cond);
        ldr(dst2, src2, cond);
      }
    }
  }
#endif
#else
  PPCPORT_UNIMPLEMENTED();
  fake_asm(fMASM18);
#endif
}


void MacroAssembler::Strd(Register src1, Register src2,
                          const MemOperand& dst, Condition cond) {
#ifdef PENGUIN_CLEANUP
  ldr(src1, MemOperand(src2), al);  // PPC - bogus instruction to cause error
#if 0
  ASSERT(dst.rm().is(no_reg));
  ASSERT(!src1.is(lr));  // r14.
  ASSERT_EQ(0, src1.code() % 2);
  ASSERT_EQ(src1.code() + 1, src2.code());

  // V8 does not use this addressing mode, so the fallback code
  // below doesn't support it yet.
  ASSERT((dst.am() != PreIndex) && (dst.am() != NegPreIndex));

  // Generate two str instructions if strd is not available.
  if (CpuFeatures::IsSupported(ARMv7) && !predictable_code_size()) {
    CpuFeatures::Scope scope(ARMv7);
    strd(src1, src2, dst, cond);
  } else {
    MemOperand dst2(dst);
    if ((dst.am() == Offset) || (dst.am() == NegOffset)) {
      dst2.set_offset(dst2.offset() + 4);
      str(src1, dst, cond);
      str(src2, dst2, cond);
    } else {  // PostIndex or NegPostIndex.
      ASSERT((dst.am() == PostIndex) || (dst.am() == NegPostIndex));
      dst2.set_offset(dst2.offset() - 4);
      str(src1, MemOperand(dst.rn(), 4, PostIndex), cond);
      str(src2, dst2, cond);
    }
  }
#endif
#else
  PPCPORT_UNIMPLEMENTED();
  fake_asm(fMASM19);
#endif
}

#ifdef PENGUIN_CLEANUP
void MacroAssembler::ClearFPSCRBits(const uint32_t bits_to_clear,
                                    const Register scratch,
                                    const Condition cond) {
  vmrs(scratch, cond);
  bic(scratch, scratch, Operand(bits_to_clear), LeaveCC, cond);
  vmsr(scratch, cond);
}
#endif

void MacroAssembler::VFPCompareAndSetFlags(const DwVfpRegister src1,
                                           const DwVfpRegister src2,
                                           const Condition cond) {
  // Compare and move FPSCR flags to the normal condition flags.
  VFPCompareAndLoadFlags(src1, src2,
                         r15,  // was pc?
                         cond);
}

void MacroAssembler::VFPCompareAndSetFlags(const DwVfpRegister src1,
                                           const double src2,
                                           const Condition cond) {
  // Compare and move FPSCR flags to the normal condition flags.
    VFPCompareAndLoadFlags(src1, src2,
                           r15,  // was pc?
                           cond);
}


void MacroAssembler::VFPCompareAndLoadFlags(const DwVfpRegister src1,
                                            const DwVfpRegister src2,
                                            const Register fpscr_flags,
                                            const Condition cond) {
#ifdef PENGUIN_CLEANUP
  // Compare and load FPSCR.
  vcmp(src1, src2, cond);
  vmrs(fpscr_flags, cond);
#else
  PPCPORT_UNIMPLEMENTED();
  fake_asm(fMASM26);
#endif
}

void MacroAssembler::VFPCompareAndLoadFlags(const DwVfpRegister src1,
                                            const double src2,
                                            const Register fpscr_flags,
                                            const Condition cond) {
#ifdef PENGUIN_CLEANUP
  // Compare and load FPSCR.
  vcmp(src1, src2, cond);
  vmrs(fpscr_flags, cond);
#else
  PPCPORT_UNIMPLEMENTED();
  fake_asm(fMASM27);
#endif
}

void MacroAssembler::Vmov(const DwVfpRegister dst,
                          const double imm,
                          const Register scratch,
                          const Condition cond) {
#ifdef PENGUIN_CLEANUP
  ASSERT(CpuFeatures::IsEnabled(VFP2));
  static const DoubleRepresentation minus_zero(-0.0);
  static const DoubleRepresentation zero(0.0);
  DoubleRepresentation value(imm);
  // Handle special values first.
  if (value.bits == zero.bits) {
    vmov(dst, kDoubleRegZero, cond);
  } else if (value.bits == minus_zero.bits) {
    vneg(dst, kDoubleRegZero, cond);
  } else {
    vmov(dst, imm, scratch, cond);
  }
#else
  PPCPORT_UNIMPLEMENTED();
  fake_asm(fMASM28);
#endif
}


void MacroAssembler::EnterFrame(StackFrame::Type type) {
  mflr(r0);
  push(r0);
  push(fp);
  push(cp);
  mov(r0, Operand(Smi::FromInt(type)));
  push(r0);
  mov(r0, Operand(CodeObject()));
  push(r0);
  addi(fp, sp, Operand(3 * kPointerSize));  // Adjust FP to point to saved FP

#if 0
  // r0-r3: preserved
  stm(db_w, sp, cp.bit() | fp.bit() | lr.bit());
  mov(ip, Operand(Smi::FromInt(type)));
  push(ip);
  mov(ip, Operand(CodeObject()));
  push(ip);
  addi(fp, sp, Operand(3 * kPointerSize));  // Adjust FP to point to saved FP.
#endif
}


void MacroAssembler::LeaveFrame(StackFrame::Type type) {
  // clean things up but don't perform return
  mr(sp, fp);
  lwz(fp, MemOperand(sp));
  lwz(r0, MemOperand(sp, kPointerSize));
  mtlr(r0);
  addi(sp, sp, Operand(2*kPointerSize));

/*
  // this destroys r11
  addi(r11,fp,Operand(16));
  lwz(r0, MemOperand(r11, 4));
  mtlr(r0);
  lwz(fp, MemOperand(r11,-4));
  mr(sp, r11);
*/

#if 0
  // r0: preserved
  // r1: preserved
  // r2: preserved

  // Drop the execution stack down to the frame pointer and restore
  // the caller frame pointer and return address.
  mr(sp, fp);
  ldm(ia_w, sp, fp.bit() | lr.bit());
#endif
}

// ExitFrame layout (probably wrongish.. needs updating)
//
//  SP -> previousSP
//        LK reserved
//        code
//        sp_on_exit (for debug?)
// oldSP->prev SP
//        LK
//        <parameters on stack>

// Prior to calling EnterExitFrame, we've got a bunch of parameters
// on the stack that we need to wrap a real frame around.. so first
// we reserve a slot for LK and push the previous SP which is captured
// in the fp register (r31)
// Then - we buy a new frame

void MacroAssembler::EnterExitFrame(bool save_doubles, int stack_space) {
  // Set up the frame structure on the stack.
  ASSERT_EQ(2 * kPointerSize, ExitFrameConstants::kCallerSPDisplacement);
  ASSERT_EQ(1 * kPointerSize, ExitFrameConstants::kCallerPCOffset);
  ASSERT_EQ(0 * kPointerSize, ExitFrameConstants::kCallerFPOffset);

#if 0
  // This is an opportunity to build a frame to wrap
  // all of the pushes that have happened inside of V8
  // since we were called from C code
  stwu(fp, MemOperand(sp, -8));  // build frame for pushed parameters
  mflr(r0);
  stw(r0, MemOperand(fp, 4));
  mr(fp, sp);
#endif

  // urgh -- replicate ARM frame for now
  mflr(r0);
  Push(r0, fp);
  mr(fp, sp);
  // Reserve room for saved entry sp and code object.
  sub(sp, sp, Operand(2 * kPointerSize));

  if (emit_debug_code()) {
    li(r8, Operand(0));
    stw(r8, MemOperand(fp, ExitFrameConstants::kSPOffset));
  }
  mov(r8, Operand(CodeObject()));
  stw(r8, MemOperand(fp, ExitFrameConstants::kCodeOffset));

  // Save the frame pointer and the context in top.
  mov(r8, Operand(ExternalReference(Isolate::kCEntryFPAddress, isolate())));
  stw(fp, MemOperand(r8));
  mov(r8, Operand(ExternalReference(Isolate::kContextAddress, isolate())));
  stw(cp, MemOperand(r8));

#if 0  // no double support yet on power
  // Optionally save all double registers.
  if (save_doubles) {
    DwVfpRegister first = d0;
    DwVfpRegister last =
        DwVfpRegister::from_code(DwVfpRegister::kNumRegisters - 1);
    vstm(db_w, sp, first, last);
    // Note that d0 will be accessible at
    //   fp - 2 * kPointerSize - DwVfpRegister::kNumRegisters * kDoubleSize,
    // since the sp slot and code slot were pushed after the fp.
  }
#endif

  // Reserve place for the return address and stack space and align the frame
  // preparing for calling the runtime function.
  // Also - add 1 more for the LR storage
  const int frame_alignment = MacroAssembler::ActivationFrameAlignment();
  sub(sp, sp, Operand((stack_space + 1 + 1) * kPointerSize));
  if (frame_alignment > 0) {
    ASSERT(frame_alignment == 8);
    rlwinm(sp, sp, 0, 0, 28);  // equivalent to &= -8
  }

  // Set the exit frame sp value to point just before the return address
  // location.
  // this is wrong on PowerPC, but we'll leave it for now
  // on PowerPC we might want to just have a SP + LR slot
  addi(r8, sp, Operand(kPointerSize));
  stw(r8, MemOperand(fp, ExitFrameConstants::kSPOffset));
}


void MacroAssembler::InitializeNewString(Register string,
                                         Register length,
                                         Heap::RootListIndex map_index,
                                         Register scratch1,
                                         Register scratch2) {
  slwi(scratch1, length, Operand(kSmiTagSize));
  LoadRoot(scratch2, map_index);
  stw(scratch1, FieldMemOperand(string, String::kLengthOffset));
  li(scratch1, Operand(String::kEmptyHashField));
  stw(scratch2, FieldMemOperand(string, HeapObject::kMapOffset));
  stw(scratch1, FieldMemOperand(string, String::kHashFieldOffset));
}


int MacroAssembler::ActivationFrameAlignment() {
#if defined(V8_HOST_ARCH_PPC)
  // Running on the real platform. Use the alignment as mandated by the local
  // environment.
  // Note: This will break if we ever start generating snapshots on one PPC
  // platform for another PPC platform with a different alignment.
  return OS::ActivationFrameAlignment();
#else  // Simulated
  // If we are using the simulator then we should always align to the expected
  // alignment. As the simulator is used to generate snapshots we do not know
  // if the target platform will need alignment, so this is controlled from a
  // flag.
  return FLAG_sim_stack_alignment;
#endif
}


void MacroAssembler::LeaveExitFrame(bool save_doubles,
                                    Register argument_count) {
#if 0  // no double support on PPC yet
  // Optionally restore all double registers.
  if (save_doubles) {
    // Calculate the stack location of the saved doubles and restore them.
    const int offset = 2 * kPointerSize;
    sub(r3, fp, Operand(offset + DwVfpRegister::kNumRegisters * kDoubleSize));
    DwVfpRegister first = d0;
    DwVfpRegister last =
        DwVfpRegister::from_code(DwVfpRegister::kNumRegisters - 1);
    vldm(ia, r3, first, last);
  }
#endif

  // Clear top frame.
  li(r6, Operand(0, RelocInfo::NONE));
  mov(ip, Operand(ExternalReference(Isolate::kCEntryFPAddress, isolate())));
  stw(r6, MemOperand(ip));

  // Restore current context from top and clear it in debug mode.
  mov(ip, Operand(ExternalReference(Isolate::kContextAddress, isolate())));
  lwz(cp, MemOperand(ip));
#ifdef DEBUG
  stw(r6, MemOperand(ip));
#endif

  // Tear down the exit frame, pop the arguments, and return.
  mr(sp, fp);
  pop(fp);
  pop(r0);
  mtlr(r0);

  if (argument_count.is_valid()) {
    slwi(argument_count, argument_count, Operand(kPointerSizeLog2));
    add(sp, sp, argument_count);
  }
}

void MacroAssembler::GetCFunctionDoubleResult(const DoubleRegister dst) {
    fmr(dst, d0);
}


void MacroAssembler::SetCallKind(Register dst, CallKind call_kind) {
  // This macro takes the dst register to make the code more readable
  // at the call sites. However, the dst register has to be r8 to
  // follow the calling convention which requires the call type to be
  // in r8.
  ASSERT(dst.is(r8));
  if (call_kind == CALL_AS_FUNCTION) {
    li(dst, Operand(Smi::FromInt(1)));
  } else {
    li(dst, Operand(Smi::FromInt(0)));
  }
}


void MacroAssembler::InvokePrologue(const ParameterCount& expected,
                                    const ParameterCount& actual,
                                    Handle<Code> code_constant,
                                    Register code_reg,
                                    Label* done,
                                    bool* definitely_mismatches,
                                    InvokeFlag flag,
                                    const CallWrapper& call_wrapper,
                                    CallKind call_kind) {
  bool definitely_matches = false;
  *definitely_mismatches = false;
  Label regular_invoke;

  // Check whether the expected and actual arguments count match. If not,
  // setup registers according to contract with ArgumentsAdaptorTrampoline:
  //  r3: actual arguments count
  //  r4: function (passed through to callee)
  //  r5: expected arguments count
  //  r6: callee code entry

  // The code below is made a lot easier because the calling code already sets
  // up actual and expected registers according to the contract if values are
  // passed in registers.

  // roohack - remove these 3 checks temporarily
  //  ASSERT(actual.is_immediate() || actual.reg().is(r3));
  //  ASSERT(expected.is_immediate() || expected.reg().is(r5));
  //  ASSERT((!code_constant.is_null() && code_reg.is(no_reg))
  //          || code_reg.is(r6));

  if (expected.is_immediate()) {
    ASSERT(actual.is_immediate());
    if (expected.immediate() == actual.immediate()) {
      definitely_matches = true;
    } else {
      mov(r3, Operand(actual.immediate()));
      const int sentinel = SharedFunctionInfo::kDontAdaptArgumentsSentinel;
      if (expected.immediate() == sentinel) {
        // Don't worry about adapting arguments for builtins that
        // don't want that done. Skip adaption code by making it look
        // like we have a match between expected and actual number of
        // arguments.
        definitely_matches = true;
      } else {
        *definitely_mismatches = true;
        mov(r5, Operand(expected.immediate()));
      }
    }
  } else {
    if (actual.is_immediate()) {
      cmpi(expected.reg(), Operand(actual.immediate()));
      beq(&regular_invoke);
      mov(r3, Operand(actual.immediate()));
    } else {
      cmp(expected.reg(), actual.reg());
      beq(&regular_invoke);
    }
  }

  if (!definitely_matches) {
    if (!code_constant.is_null()) {
      mov(r6, Operand(code_constant));
      addi(r6, r6, Operand(Code::kHeaderSize - kHeapObjectTag));
    }

    Handle<Code> adaptor =
        isolate()->builtins()->ArgumentsAdaptorTrampoline();
    if (flag == CALL_FUNCTION) {
      call_wrapper.BeforeCall(CallSize(adaptor));
      SetCallKind(r8, call_kind);
      Call(adaptor);
      call_wrapper.AfterCall();
      if (!*definitely_mismatches) {
        b(done);
      }
    } else {
      SetCallKind(r8, call_kind);
      Jump(adaptor, RelocInfo::CODE_TARGET);
    }
    bind(&regular_invoke);
  }
}


void MacroAssembler::InvokeCode(Register code,
                                const ParameterCount& expected,
                                const ParameterCount& actual,
                                InvokeFlag flag,
                                const CallWrapper& call_wrapper,
                                CallKind call_kind) {
  // You can't call a function without a valid frame.
  ASSERT(flag == JUMP_FUNCTION || has_frame());

  Label done;
  bool definitely_mismatches = false;
  InvokePrologue(expected, actual, Handle<Code>::null(), code,
                 &done, &definitely_mismatches, flag,
                 call_wrapper, call_kind);
  if (!definitely_mismatches) {
    if (flag == CALL_FUNCTION) {
      call_wrapper.BeforeCall(CallSize(code));
      SetCallKind(r8, call_kind);
      Call(code);
      call_wrapper.AfterCall();
    } else {
      ASSERT(flag == JUMP_FUNCTION);
      SetCallKind(r8, call_kind);
      Jump(code);
    }

    // Continue here if InvokePrologue does handle the invocation due to
    // mismatched parameter counts.
    bind(&done);
  }
}


void MacroAssembler::InvokeCode(Handle<Code> code,
                                const ParameterCount& expected,
                                const ParameterCount& actual,
                                RelocInfo::Mode rmode,
                                InvokeFlag flag,
                                CallKind call_kind) {
  // You can't call a function without a valid frame.
  ASSERT(flag == JUMP_FUNCTION || has_frame());

  Label done;
  bool definitely_mismatches = false;
  InvokePrologue(expected, actual, code, no_reg,
                 &done, &definitely_mismatches, flag,
                 NullCallWrapper(), call_kind);
  if (!definitely_mismatches) {
    if (flag == CALL_FUNCTION) {
      SetCallKind(r8, call_kind);
      Call(code, rmode);
    } else {
      SetCallKind(r8, call_kind);
      Jump(code, rmode);
    }

    // Continue here if InvokePrologue does handle the invocation due to
    // mismatched parameter counts.
    bind(&done);
  }
}


void MacroAssembler::InvokeFunction(Register fun,
                                    const ParameterCount& actual,
                                    InvokeFlag flag,
                                    const CallWrapper& call_wrapper,
                                    CallKind call_kind) {
  // You can't call a function without a valid frame.
  ASSERT(flag == JUMP_FUNCTION || has_frame());

  // Contract with called JS functions requires that function is passed in r4.
  ASSERT(fun.is(r4));

  Register expected_reg = r5;
  Register code_reg = r6;

  lwz(code_reg, FieldMemOperand(r4, JSFunction::kSharedFunctionInfoOffset));
  lwz(cp, FieldMemOperand(r4, JSFunction::kContextOffset));
  lwz(expected_reg,
      FieldMemOperand(code_reg,
                      SharedFunctionInfo::kFormalParameterCountOffset));
  srawi(expected_reg, expected_reg, kSmiTagSize);
  lwz(code_reg,
      FieldMemOperand(r4, JSFunction::kCodeEntryOffset));

  ParameterCount expected(expected_reg);
  InvokeCode(code_reg, expected, actual, flag, call_wrapper, call_kind);
}


void MacroAssembler::InvokeFunction(Handle<JSFunction> function,
                                    const ParameterCount& actual,
                                    InvokeFlag flag,
                                    const CallWrapper& call_wrapper,
                                    CallKind call_kind) {
  // You can't call a function without a valid frame.
  ASSERT(flag == JUMP_FUNCTION || has_frame());

  // Get the function and setup the context.
  LoadHeapObject(r4, function);
  lwz(cp, FieldMemOperand(r4, JSFunction::kContextOffset));

  ParameterCount expected(function->shared()->formal_parameter_count());
  // We call indirectly through the code field in the function to
  // allow recompilation to take effect without changing any of the
  // call sites.
  lwz(r6, FieldMemOperand(r4, JSFunction::kCodeEntryOffset));
  InvokeCode(r6, expected, actual, flag, call_wrapper, call_kind);
}


void MacroAssembler::IsObjectJSObjectType(Register heap_object,
                                          Register map,
                                          Register scratch,
                                          Label* fail) {
  lwz(map, FieldMemOperand(heap_object, HeapObject::kMapOffset));
  IsInstanceJSObjectType(map, scratch, fail);
}


void MacroAssembler::IsInstanceJSObjectType(Register map,
                                            Register scratch,
                                            Label* fail) {
  lbz(scratch, FieldMemOperand(map, Map::kInstanceTypeOffset));
  cmpi(scratch, Operand(FIRST_NONCALLABLE_SPEC_OBJECT_TYPE));
  blt(fail);
  cmpi(scratch, Operand(LAST_NONCALLABLE_SPEC_OBJECT_TYPE));
  bgt(fail);
}


void MacroAssembler::IsObjectJSStringType(Register object,
                                          Register scratch,
                                          Label* fail) {
  ASSERT(kNotStringTag != 0);

  lwz(scratch, FieldMemOperand(object, HeapObject::kMapOffset));
  lbz(scratch, FieldMemOperand(scratch, Map::kInstanceTypeOffset));
  andi(r0, scratch, Operand(kIsNotStringMask));
  bne(fail, cr0);
}


#ifdef ENABLE_DEBUGGER_SUPPORT
void MacroAssembler::DebugBreak() {
  li(r3, Operand(0, RelocInfo::NONE));
  mov(r4, Operand(ExternalReference(Runtime::kDebugBreak, isolate())));
  CEntryStub ces(1);
  ASSERT(AllowThisStubCall(&ces));
  Call(ces.GetCode(), RelocInfo::DEBUG_BREAK);
}
#endif


void MacroAssembler::PushTryHandler(StackHandler::Kind kind,
                                    int handler_index) {
  // Adjust this code if not the case.
  STATIC_ASSERT(StackHandlerConstants::kSize == 5 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kNextOffset == 0 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kCodeOffset == 1 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kStateOffset == 2 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kContextOffset == 3 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kFPOffset == 4 * kPointerSize);

  // For the JSEntry handler, we must preserve r1-r7, r0,r8-r15 are available.
  // We want the stack to look like
  // sp -> NextOffset
  //       CodeObject
  //       state
  //       context
  //       frame pointer

  // Link the current handler as the next handler.
  mov(r8, Operand(ExternalReference(Isolate::kHandlerAddress, isolate())));
  lwz(r0, MemOperand(r8));
  stwu(r0, MemOperand(sp, -StackHandlerConstants::kSize));
  // Set this new handler as the current one.
  stw(sp, MemOperand(r8));

  if (kind == StackHandler::JS_ENTRY) {
    li(r8, Operand(0, RelocInfo::NONE));  // NULL frame pointer.
    stw(r8, MemOperand(sp, StackHandlerConstants::kFPOffset));
    li(r8, Operand(Smi::FromInt(0)));    // Indicates no context.
    stw(r8, MemOperand(sp, StackHandlerConstants::kContextOffset));
  } else {
    // still not sure if fp is right
    stw(fp, MemOperand(sp, StackHandlerConstants::kFPOffset));
    stw(cp, MemOperand(sp, StackHandlerConstants::kContextOffset));
  }
  unsigned state =
      StackHandler::IndexField::encode(handler_index) |
      StackHandler::KindField::encode(kind);
  mov(r8, Operand(state));
  stw(r8, MemOperand(sp, StackHandlerConstants::kStateOffset));
  mov(r8, Operand(CodeObject()));
  stw(r8, MemOperand(sp, StackHandlerConstants::kCodeOffset));
}


void MacroAssembler::PopTryHandler() {
  STATIC_ASSERT(StackHandlerConstants::kNextOffset == 0);
  pop(r4);
  mov(ip, Operand(ExternalReference(Isolate::kHandlerAddress, isolate())));
  addi(sp, sp, Operand(StackHandlerConstants::kSize - kPointerSize));
  stw(r4, MemOperand(ip));
}

// PPC - make use of ip as a temporary register
void MacroAssembler::JumpToHandlerEntry() {
  // Compute the handler entry address and jump to it.  The handler table is
  // a fixed array of (smi-tagged) code offsets.
  // r3 = exception, r4 = code object, r5 = state.
  lwz(r6, FieldMemOperand(r4, Code::kHandlerTableOffset));  // Handler table.
  addi(r6, r6, Operand(FixedArray::kHeaderSize - kHeapObjectTag));
  srwi(r5, r5, Operand(StackHandler::kKindWidth));  // Handler index.
  slwi(ip, r5, Operand(kPointerSizeLog2));
  add(ip, r6, ip);
  lwz(r5, MemOperand(ip));  // Smi-tagged offset.
  addi(r4, r4, Operand(Code::kHeaderSize - kHeapObjectTag));  // Code start.
  srawi(ip, r5, kSmiTagSize);
  add(r0, r4, ip);
  mtctr(r0);
  bcr();
}


void MacroAssembler::Throw(Register value) {
  // Adjust this code if not the case.
  STATIC_ASSERT(StackHandlerConstants::kSize == 5 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kNextOffset == 0);
  STATIC_ASSERT(StackHandlerConstants::kCodeOffset == 1 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kStateOffset == 2 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kContextOffset == 3 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kFPOffset == 4 * kPointerSize);
  Label skip;

  // The exception is expected in r3.
  if (!value.is(r3)) {
    mr(r3, value);
  }
  // Drop the stack pointer to the top of the top handler.
  mov(r6, Operand(ExternalReference(Isolate::kHandlerAddress, isolate())));
  lwz(sp, MemOperand(r6));
  // Restore the next handler.
  pop(r5);
  stw(r5, MemOperand(r6));

  // Get the code object (r1) and state (r2).  Restore the context and frame
  // pointer.
  pop(r4);
  pop(r5);
  pop(cp);
  pop(fp);

  // If the handler is a JS frame, restore the context to the frame.
  // (kind == ENTRY) == (fp == 0) == (cp == 0), so we could test either fp
  // or cp.
  cmpi(cp, Operand(0));
  beq(&skip);
  stw(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
  bind(&skip);

  JumpToHandlerEntry();
}


void MacroAssembler::ThrowUncatchable(Register value) {
  // Adjust this code if not the case.
  STATIC_ASSERT(StackHandlerConstants::kSize == 5 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kNextOffset == 0 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kCodeOffset == 1 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kStateOffset == 2 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kContextOffset == 3 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kFPOffset == 4 * kPointerSize);

  // The exception is expected in r3.
  if (!value.is(r3)) {
    mr(r3, value);
  }
  // Drop the stack pointer to the top of the top stack handler.
  mov(r6, Operand(ExternalReference(Isolate::kHandlerAddress, isolate())));
  lwz(sp, MemOperand(r6));

  // Unwind the handlers until the ENTRY handler is found.
  Label fetch_next, check_kind;
  jmp(&check_kind);
  bind(&fetch_next);
  lwz(sp, MemOperand(sp, StackHandlerConstants::kNextOffset));

  bind(&check_kind);
  STATIC_ASSERT(StackHandler::JS_ENTRY == 0);
  lwz(r5, MemOperand(sp, StackHandlerConstants::kStateOffset));
  mov(r0, Operand(StackHandler::KindField::kMask));
  cmp(r5, r0);
  bne(&fetch_next);

  // Set the top handler address to next handler past the top ENTRY handler.
  pop(r5);
  stw(r5, MemOperand(r6));
  // Get the code object (r4) and state (r5).  Clear the context and frame
  // pointer (0 was saved in the handler).
  pop(r4);
  pop(r5);
  pop(cp);
  pop(fp);

  JumpToHandlerEntry();
}


void MacroAssembler::CheckAccessGlobalProxy(Register holder_reg,
                                            Register scratch,
                                            Label* miss) {
  Label same_contexts;

  ASSERT(!holder_reg.is(scratch));
  ASSERT(!holder_reg.is(ip));
  ASSERT(!scratch.is(ip));

  // Load current lexical context from the stack frame.
  lwz(scratch, MemOperand(fp, StandardFrameConstants::kContextOffset));
  // In debug mode, make sure the lexical context is set.
#ifdef DEBUG
  cmpi(scratch, Operand(0, RelocInfo::NONE));
  Check(ne, "we should not have an empty lexical context");
#endif

  // Load the native context of the current context.
  int offset =
      Context::kHeaderSize + Context::GLOBAL_OBJECT_INDEX * kPointerSize;
  lwz(scratch, FieldMemOperand(scratch, offset));
  lwz(scratch, FieldMemOperand(scratch, GlobalObject::kNativeContextOffset));

  // Check the context is a native context.
  if (emit_debug_code()) {
    // TODO(119): avoid push(holder_reg)/pop(holder_reg)
    // Cannot use ip as a temporary in this verification code. Due to the fact
    // that ip is clobbered as part of cmp with an object Operand.
    push(holder_reg);  // Temporarily save holder on the stack.
    // Read the first word and compare to the native_context_map.
    lwz(holder_reg, FieldMemOperand(scratch, HeapObject::kMapOffset));
    LoadRoot(ip, Heap::kNativeContextMapRootIndex);
    cmp(holder_reg, ip);
    Check(eq, "JSGlobalObject::native_context should be a native context.");
    pop(holder_reg);  // Restore holder.
  }

  // Check if both contexts are the same.
  lwz(ip, FieldMemOperand(holder_reg, JSGlobalProxy::kNativeContextOffset));
  cmp(scratch, ip);
  beq(&same_contexts);

  // Check the context is a native context.
  if (emit_debug_code()) {
    // TODO(119): avoid push(holder_reg)/pop(holder_reg)
    // Cannot use ip as a temporary in this verification code. Due to the fact
    // that ip is clobbered as part of cmp with an object Operand.
    push(holder_reg);  // Temporarily save holder on the stack.
    mr(holder_reg, ip);  // Move ip to its holding place.
    LoadRoot(ip, Heap::kNullValueRootIndex);
    cmp(holder_reg, ip);
    Check(ne, "JSGlobalProxy::context() should not be null.");

    lwz(holder_reg, FieldMemOperand(holder_reg, HeapObject::kMapOffset));
    LoadRoot(ip, Heap::kNativeContextMapRootIndex);
    cmp(holder_reg, ip);
    Check(eq, "JSGlobalObject::native_context should be a native context.");
    // Restore ip is not needed. ip is reloaded below.
    pop(holder_reg);  // Restore holder.
    // Restore ip to holder's context.
    lwz(ip, FieldMemOperand(holder_reg, JSGlobalProxy::kNativeContextOffset));
  }

  // Check that the security token in the calling global object is
  // compatible with the security token in the receiving global
  // object.
  int token_offset = Context::kHeaderSize +
                     Context::SECURITY_TOKEN_INDEX * kPointerSize;

  lwz(scratch, FieldMemOperand(scratch, token_offset));
  lwz(ip, FieldMemOperand(ip, token_offset));
  cmp(scratch, ip);
  bne(miss);

  bind(&same_contexts);
}


void MacroAssembler::GetNumberHash(Register t0, Register scratch) {
  // First of all we assign the hash seed to scratch.
  LoadRoot(scratch, Heap::kHashSeedRootIndex);
  SmiUntag(scratch);

  // Xor original key with a seed.
  xor_(t0, t0, scratch);

  // Compute the hash code from the untagged key.  This must be kept in sync
  // with ComputeIntegerHash in utils.h.
  //
  // hash = ~hash + (hash << 15);
  notx(scratch, t0);
  slwi(t0, t0, Operand(15));
  add(t0, scratch, t0);
  // hash = hash ^ (hash >> 12);
  srwi(scratch, t0, Operand(12));
  xor_(t0, t0, scratch);
  // hash = hash + (hash << 2);
  slwi(scratch, t0, Operand(2));
  add(t0, t0, scratch);
  // hash = hash ^ (hash >> 4);
  srwi(scratch, t0, Operand(4));
  xor_(t0, t0, scratch);
  // hash = hash * 2057;
  mr(r0, t0);
  slwi(scratch, t0, Operand(3));
  add(t0, t0, scratch);
  slwi(scratch, r0, Operand(11));
  add(t0, t0, scratch);
  // hash = hash ^ (hash >> 16);
  srwi(scratch, t0, Operand(16));
  xor_(t0, t0, scratch);
}


void MacroAssembler::LoadFromNumberDictionary(Label* miss,
                                              Register elements,
                                              Register key,
                                              Register result,
                                              Register t0,
                                              Register t1,
                                              Register t2) {
  // Register use:
  //
  // elements - holds the slow-case elements of the receiver on entry.
  //            Unchanged unless 'result' is the same register.
  //
  // key      - holds the smi key on entry.
  //            Unchanged unless 'result' is the same register.
  //
  // result   - holds the result on exit if the load succeeded.
  //            Allowed to be the same as 'key' or 'result'.
  //            Unchanged on bailout so 'key' or 'result' can be used
  //            in further computation.
  //
  // Scratch registers:
  //
  // t0 - holds the untagged key on entry and holds the hash once computed.
  //
  // t1 - used to hold the capacity mask of the dictionary
  //
  // t2 - used for the index into the dictionary.
  Label done;

  GetNumberHash(t0, t1);

  // Compute the capacity mask.
  lwz(t1, FieldMemOperand(elements, SeededNumberDictionary::kCapacityOffset));
  srawi(t1, t1, kSmiTagSize);  // convert smi to int
  sub(t1, t1, Operand(1));

  // Generate an unrolled loop that performs a few probes before giving up.
  static const int kProbes = 4;
  for (int i = 0; i < kProbes; i++) {
    // Use t2 for index calculations and keep the hash intact in t0.
    mr(t2, t0);
    // Compute the masked index: (hash + i + i * i) & mask.
    if (i > 0) {
      addi(t2, t2, Operand(SeededNumberDictionary::GetProbeOffset(i)));
    }
    and_(t2, t2, t1);

    // Scale the index by multiplying by the element size.
    ASSERT(SeededNumberDictionary::kEntrySize == 3);
    slwi(ip, t2, Operand(1));
    add(t2, t2, ip);  // t2 = t2 * 3

    // Check if the key is identical to the name.
    slwi(t2, t2, Operand(kPointerSizeLog2));
    add(t2, elements, t2);
    lwz(ip, FieldMemOperand(t2, SeededNumberDictionary::kElementsStartOffset));
    cmp(key, ip);
    if (i != kProbes - 1) {
      beq(&done);
    } else {
      bne(miss);
    }
  }

  bind(&done);
  // Check that the value is a normal property.
  // t2: elements + (index * kPointerSize)
  const int kDetailsOffset =
      SeededNumberDictionary::kElementsStartOffset + 2 * kPointerSize;
  lwz(t1, FieldMemOperand(t2, kDetailsOffset));
  mov(ip, Operand(Smi::FromInt(PropertyDetails::TypeField::kMask)));
  and_(r0, t1, ip, SetRC);
  bne(miss, cr0);

  // Get the value at the masked, scaled index and return.
  const int kValueOffset =
      SeededNumberDictionary::kElementsStartOffset + kPointerSize;
  lwz(result, FieldMemOperand(t2, kValueOffset));
}


void MacroAssembler::AllocateInNewSpace(int object_size,
                                        Register result,
                                        Register scratch1,
                                        Register scratch2,
                                        Label* gc_required,
                                        AllocationFlags flags) {
  if (!FLAG_inline_new) {
    if (emit_debug_code()) {
      // Trash the registers to simulate an allocation failure.
      li(result, Operand(0x7091));
      li(scratch1, Operand(0x7191));
      li(scratch2, Operand(0x7291));
    }
    jmp(gc_required);
    return;
  }

  ASSERT(!result.is(scratch1));
  ASSERT(!result.is(scratch2));
  ASSERT(!scratch1.is(scratch2));
  ASSERT(!scratch1.is(ip));
  ASSERT(!scratch2.is(ip));

  // Make object size into bytes.
  if ((flags & SIZE_IN_WORDS) != 0) {
    object_size *= kPointerSize;
  }
  ASSERT_EQ(0, object_size & kObjectAlignmentMask);

  // Check relative positions of allocation top and limit addresses.
  // The values must be adjacent in memory to allow the use of LDM.
  // Also, assert that the registers are numbered such that the values
  // are loaded in the correct order.
  ExternalReference new_space_allocation_top =
      ExternalReference::new_space_allocation_top_address(isolate());
  ExternalReference new_space_allocation_limit =
      ExternalReference::new_space_allocation_limit_address(isolate());
  intptr_t top   =
      reinterpret_cast<intptr_t>(new_space_allocation_top.address());
  intptr_t limit =
      reinterpret_cast<intptr_t>(new_space_allocation_limit.address());
  ASSERT((limit - top) == kPointerSize);
  ASSERT(result.code() < ip.code());

  // Set up allocation top address and object size registers.
  Register topaddr = scratch1;
  Register obj_size_reg = scratch2;
  mov(topaddr, Operand(new_space_allocation_top));
  // this won't work for very large object on PowerPC
  li(obj_size_reg, Operand(object_size));

  // This code stores a temporary value in ip. This is OK, as the code below
  // does not need ip for implicit literal generation.
  if ((flags & RESULT_CONTAINS_TOP) == 0) {
    // Load allocation top into result and allocation limit into ip.
    lwz(result, MemOperand(topaddr));
    lwz(ip, MemOperand(topaddr, kPointerSize));
  } else {
    if (emit_debug_code()) {
      // Assert that result actually contains top on entry. ip is used
      // immediately below so this use of ip does not cause difference with
      // respect to register content between debug and release mode.
      lwz(ip, MemOperand(topaddr));
      cmp(result, ip);
      Check(eq, "Unexpected allocation top");
    }
    // Load allocation limit into ip. Result already contains allocation top.
    lwz(ip, MemOperand(topaddr, limit - top));
  }

  // Calculate new top and bail out if new space is exhausted. Use result
  // to calculate the new top.
  li(r0, Operand(-1));
  addc(scratch2, result, obj_size_reg);
  addze(r0, r0, LeaveOE, SetRC);
  beq(gc_required, cr0);
  cmpl(scratch2, ip);
  bgt(gc_required);
  stw(scratch2, MemOperand(topaddr));

  // Tag object if requested.
  if ((flags & TAG_OBJECT) != 0) {
    addi(result, result, Operand(kHeapObjectTag));
  }
}


void MacroAssembler::AllocateInNewSpace(Register object_size,
                                        Register result,
                                        Register scratch1,
                                        Register scratch2,
                                        Label* gc_required,
                                        AllocationFlags flags) {
  if (!FLAG_inline_new) {
    if (emit_debug_code()) {
      // Trash the registers to simulate an allocation failure.
      li(result, Operand(0x7091));
      li(scratch1, Operand(0x7191));
      li(scratch2, Operand(0x7291));
    }
    jmp(gc_required);
    return;
  }

  // Assert that the register arguments are different and that none of
  // them are ip. ip is used explicitly in the code generated below.
  ASSERT(!result.is(scratch1));
  ASSERT(!result.is(scratch2));
  ASSERT(!scratch1.is(scratch2));
  ASSERT(!object_size.is(ip));
  ASSERT(!result.is(ip));
  ASSERT(!scratch1.is(ip));
  ASSERT(!scratch2.is(ip));

  // Check relative positions of allocation top and limit addresses.
  // The values must be adjacent in memory to allow the use of LDM.
  // Also, assert that the registers are numbered such that the values
  // are loaded in the correct order.
  ExternalReference new_space_allocation_top =
      ExternalReference::new_space_allocation_top_address(isolate());
  ExternalReference new_space_allocation_limit =
      ExternalReference::new_space_allocation_limit_address(isolate());
  intptr_t top =
      reinterpret_cast<intptr_t>(new_space_allocation_top.address());
  intptr_t limit =
      reinterpret_cast<intptr_t>(new_space_allocation_limit.address());
  ASSERT((limit - top) == kPointerSize);
  ASSERT(result.code() < ip.code());

  // Set up allocation top address.
  Register topaddr = scratch1;
  mov(topaddr, Operand(new_space_allocation_top));

  // This code stores a temporary value in ip. This is OK, as the code below
  // does not need ip for implicit literal generation.
  if ((flags & RESULT_CONTAINS_TOP) == 0) {
    // Load allocation top into result and allocation limit into ip.
    lwz(result, MemOperand(topaddr));
    lwz(ip, MemOperand(topaddr, kPointerSize));
  } else {
    if (emit_debug_code()) {
      // Assert that result actually contains top on entry. ip is used
      // immediately below so this use of ip does not cause difference with
      // respect to register content between debug and release mode.
      lwz(ip, MemOperand(topaddr));
      cmp(result, ip);
      Check(eq, "Unexpected allocation top");
    }
    // Load allocation limit into ip. Result already contains allocation top.
    lwz(ip, MemOperand(topaddr, limit - top));
  }

  // Calculate new top and bail out if new space is exhausted. Use result
  // to calculate the new top. Object size may be in words so a shift is
  // required to get the number of bytes.
  li(r0, Operand(-1));
  if ((flags & SIZE_IN_WORDS) != 0) {
    slwi(scratch2, object_size, Operand(kPointerSizeLog2));
    addc(scratch2, result, scratch2);
  } else {
    addc(scratch2, result, object_size);
  }
  addze(r0, r0, LeaveOE, SetRC);
  beq(gc_required, cr0);
  cmp(scratch2, ip);
  bgt(gc_required);

  // Update allocation top. result temporarily holds the new top.
  if (emit_debug_code()) {
    andi(r0, scratch2, Operand(kObjectAlignmentMask));
    Check(eq, "Unaligned allocation in new space", cr0);
  }
  stw(scratch2, MemOperand(topaddr));

  // Tag object if requested.
  if ((flags & TAG_OBJECT) != 0) {
    addi(result, result, Operand(kHeapObjectTag));
  }
}


void MacroAssembler::UndoAllocationInNewSpace(Register object,
                                              Register scratch) {
  ExternalReference new_space_allocation_top =
      ExternalReference::new_space_allocation_top_address(isolate());

  // Make sure the object has no tag before resetting top.
  mov(r0, Operand(~kHeapObjectTagMask));
  and_(object, object, r0);
  // was.. and_(object, object, Operand(~kHeapObjectTagMask));
#ifdef DEBUG
  // Check that the object un-allocated is below the current top.
  mov(scratch, Operand(new_space_allocation_top));
  lwz(scratch, MemOperand(scratch));
  cmp(object, scratch);
  Check(lt, "Undo allocation of non allocated memory");
#endif
  // Write the address of the object to un-allocate as the current top.
  mov(scratch, Operand(new_space_allocation_top));
  stw(object, MemOperand(scratch));
}


void MacroAssembler::AllocateTwoByteString(Register result,
                                           Register length,
                                           Register scratch1,
                                           Register scratch2,
                                           Register scratch3,
                                           Label* gc_required) {
  // Calculate the number of bytes needed for the characters in the string while
  // observing object alignment.
  ASSERT((SeqTwoByteString::kHeaderSize & kObjectAlignmentMask) == 0);
  slwi(scratch1, length, Operand(1));  // Length in bytes, not chars.
  addi(scratch1, scratch1,
       Operand(kObjectAlignmentMask + SeqTwoByteString::kHeaderSize));
  mov(r0, Operand(~kObjectAlignmentMask));
  and_(scratch1, scratch1, r0);

  // Allocate two-byte string in new space.
  AllocateInNewSpace(scratch1,
                     result,
                     scratch2,
                     scratch3,
                     gc_required,
                     TAG_OBJECT);

  // Set the map, length and hash field.
  InitializeNewString(result,
                      length,
                      Heap::kStringMapRootIndex,
                      scratch1,
                      scratch2);
}


void MacroAssembler::AllocateAsciiString(Register result,
                                         Register length,
                                         Register scratch1,
                                         Register scratch2,
                                         Register scratch3,
                                         Label* gc_required) {
  // Calculate the number of bytes needed for the characters in the string while
  // observing object alignment.
  ASSERT((SeqAsciiString::kHeaderSize & kObjectAlignmentMask) == 0);
  ASSERT(kCharSize == 1);
  addi(scratch1, length,
       Operand(kObjectAlignmentMask + SeqAsciiString::kHeaderSize));
  li(r0, Operand(~kObjectAlignmentMask));
  and_(scratch1, scratch1, r0);

  // Allocate ASCII string in new space.
  AllocateInNewSpace(scratch1,
                     result,
                     scratch2,
                     scratch3,
                     gc_required,
                     TAG_OBJECT);

  // Set the map, length and hash field.
  InitializeNewString(result,
                      length,
                      Heap::kAsciiStringMapRootIndex,
                      scratch1,
                      scratch2);
}


void MacroAssembler::AllocateTwoByteConsString(Register result,
                                               Register length,
                                               Register scratch1,
                                               Register scratch2,
                                               Label* gc_required) {
  AllocateInNewSpace(ConsString::kSize,
                     result,
                     scratch1,
                     scratch2,
                     gc_required,
                     TAG_OBJECT);

  InitializeNewString(result,
                      length,
                      Heap::kConsStringMapRootIndex,
                      scratch1,
                      scratch2);
}


void MacroAssembler::AllocateAsciiConsString(Register result,
                                             Register length,
                                             Register scratch1,
                                             Register scratch2,
                                             Label* gc_required) {
  AllocateInNewSpace(ConsString::kSize,
                     result,
                     scratch1,
                     scratch2,
                     gc_required,
                     TAG_OBJECT);

  InitializeNewString(result,
                      length,
                      Heap::kConsAsciiStringMapRootIndex,
                      scratch1,
                      scratch2);
}


void MacroAssembler::AllocateTwoByteSlicedString(Register result,
                                                 Register length,
                                                 Register scratch1,
                                                 Register scratch2,
                                                 Label* gc_required) {
  AllocateInNewSpace(SlicedString::kSize,
                     result,
                     scratch1,
                     scratch2,
                     gc_required,
                     TAG_OBJECT);

  InitializeNewString(result,
                      length,
                      Heap::kSlicedStringMapRootIndex,
                      scratch1,
                      scratch2);
}


void MacroAssembler::AllocateAsciiSlicedString(Register result,
                                               Register length,
                                               Register scratch1,
                                               Register scratch2,
                                               Label* gc_required) {
  AllocateInNewSpace(SlicedString::kSize,
                     result,
                     scratch1,
                     scratch2,
                     gc_required,
                     TAG_OBJECT);

  InitializeNewString(result,
                      length,
                      Heap::kSlicedAsciiStringMapRootIndex,
                      scratch1,
                      scratch2);
}


void MacroAssembler::CompareObjectType(Register object,
                                       Register map,
                                       Register type_reg,
                                       InstanceType type) {
  lwz(map, FieldMemOperand(object, HeapObject::kMapOffset));
  CompareInstanceType(map, type_reg, type);
}


void MacroAssembler::CompareInstanceType(Register map,
                                         Register type_reg,
                                         InstanceType type) {
  lbz(type_reg, FieldMemOperand(map, Map::kInstanceTypeOffset));
  cmpi(type_reg, Operand(type));
}


void MacroAssembler::CompareRoot(Register obj,
                                 Heap::RootListIndex index) {
  ASSERT(!obj.is(ip));
  LoadRoot(ip, index);
  cmp(obj, ip);
}


void MacroAssembler::CheckFastElements(Register map,
                                       Register scratch,
                                       Label* fail) {
  STATIC_ASSERT(FAST_SMI_ELEMENTS == 0);
  STATIC_ASSERT(FAST_HOLEY_SMI_ELEMENTS == 1);
  STATIC_ASSERT(FAST_ELEMENTS == 2);
  STATIC_ASSERT(FAST_HOLEY_ELEMENTS == 3);
  lbz(scratch, FieldMemOperand(map, Map::kBitField2Offset));
  STATIC_ASSERT(Map::kMaximumBitField2FastHoleyElementValue < 0x8000);
  cmpi(scratch, Operand(Map::kMaximumBitField2FastHoleyElementValue));
  bgt(fail);
}


void MacroAssembler::CheckFastObjectElements(Register map,
                                             Register scratch,
                                             Label* fail) {
  STATIC_ASSERT(FAST_SMI_ELEMENTS == 0);
  STATIC_ASSERT(FAST_HOLEY_SMI_ELEMENTS == 1);
  STATIC_ASSERT(FAST_ELEMENTS == 2);
  STATIC_ASSERT(FAST_HOLEY_ELEMENTS == 3);
  lbz(scratch, FieldMemOperand(map, Map::kBitField2Offset));
  cmpi(scratch, Operand(Map::kMaximumBitField2FastHoleySmiElementValue));
  blt(fail);
  cmpi(scratch, Operand(Map::kMaximumBitField2FastHoleyElementValue));
  bgt(fail);
}


void MacroAssembler::CheckFastSmiElements(Register map,
                                          Register scratch,
                                          Label* fail) {
  STATIC_ASSERT(FAST_SMI_ELEMENTS == 0);
  STATIC_ASSERT(FAST_HOLEY_SMI_ELEMENTS == 1);
  lbz(scratch, FieldMemOperand(map, Map::kBitField2Offset));
  cmpli(scratch, Operand(Map::kMaximumBitField2FastHoleySmiElementValue));
  bgt(fail);
}


void MacroAssembler::StoreNumberToDoubleElements(Register value_reg,
                                                 Register key_reg,
                                                 Register receiver_reg,
                                                 Register elements_reg,
                                                 Register scratch1,
                                                 Register scratch2,
                                                 Register scratch3,
                                                 Register scratch4,
                                                 Label* fail) {
  Label smi_value, maybe_nan, have_double_value, is_nan, done;
  Register mantissa_reg = scratch2;
  Register exponent_reg = scratch3;

  // Handle smi values specially.
  JumpIfSmi(value_reg, &smi_value);

  // Ensure that the object is a heap number
  CheckMap(value_reg,
           scratch1,
           isolate()->factory()->heap_number_map(),
           fail,
           DONT_DO_SMI_CHECK);

  // Check for nan: all NaN values have a value greater (signed) than 0x7ff00000
  // in the exponent.
  mov(scratch1, Operand(kNaNOrInfinityLowerBoundUpper32));
  lwz(exponent_reg, FieldMemOperand(value_reg, HeapNumber::kExponentOffset));
  cmp(exponent_reg, scratch1);
  bge(&maybe_nan);

  lwz(mantissa_reg, FieldMemOperand(value_reg, HeapNumber::kMantissaOffset));

  bind(&have_double_value);
  slwi(scratch1, key_reg, Operand(kDoubleSizeLog2 - kSmiTagSize));
  add(scratch1, elements_reg, scratch1);
#if __BYTE_ORDER == __LITTLE_ENDIAN
  stw(mantissa_reg, FieldMemOperand(scratch1, FixedDoubleArray::kHeaderSize));
  uint32_t offset = FixedDoubleArray::kHeaderSize + sizeof(kHoleNanLower32);
  stw(exponent_reg, FieldMemOperand(scratch1, offset));
#elif __BYTE_ORDER == __BIG_ENDIAN
  stw(exponent_reg, FieldMemOperand(scratch1, FixedDoubleArray::kHeaderSize));
  uint32_t offset = FixedDoubleArray::kHeaderSize + sizeof(kHoleNanLower32);
  stw(mantissa_reg, FieldMemOperand(scratch1, offset));
#endif
  jmp(&done);

  bind(&maybe_nan);
  // Could be NaN or Infinity. If fraction is not zero, it's NaN, otherwise
  // it's an Infinity, and the non-NaN code path applies.
  b(gt, &is_nan);
  lwz(mantissa_reg, FieldMemOperand(value_reg, HeapNumber::kMantissaOffset));
  cmpi(mantissa_reg, Operand(0));
  beq(&have_double_value);
  bind(&is_nan);
  // Load canonical NaN for storing into the double array.
  uint64_t nan_int64 = BitCast<uint64_t>(
      FixedDoubleArray::canonical_not_the_hole_nan_as_double());
  mov(mantissa_reg, Operand(static_cast<uint32_t>(nan_int64)));
  mov(exponent_reg, Operand(static_cast<uint32_t>(nan_int64 >> 32)));
  jmp(&have_double_value);

  bind(&smi_value);
  addi(scratch1, elements_reg,
       Operand(FixedDoubleArray::kHeaderSize - kHeapObjectTag));
  slwi(scratch4, key_reg, Operand(kDoubleSizeLog2 - kSmiTagSize));
  add(scratch1, scratch1, scratch4);
  // scratch1 is now effective address of the double element

  Register untagged_value = elements_reg;
  SmiUntag(untagged_value, value_reg);
  FloatingPointHelper::ConvertIntToDouble(this,
                                          untagged_value,
                                          FloatingPointHelper::kFPRegisters,
                                          d0,
                                          mantissa_reg,
                                          exponent_reg,
                                          d2);
  stfd(d0, MemOperand(scratch1, 0));

  bind(&done);
}


void MacroAssembler::AddAndCheckForOverflow(Register dst,
                                            Register left,
                                            Register right,
                                            Register overflow_dst,
                                            Register scratch) {
  ASSERT(!dst.is(overflow_dst));
  ASSERT(!dst.is(scratch));
  ASSERT(!overflow_dst.is(scratch));
  ASSERT(!overflow_dst.is(left));
  ASSERT(!overflow_dst.is(right));

  // C = A+B; C overflows if A/B have same sign and C has diff sign than A
  if (dst.is(left)) {
    mr(scratch, left);            // Preserve left.
    add(dst, left, right);        // Left is overwritten.
    xor_(scratch, dst, scratch);  // Original left.
    xor_(overflow_dst, dst, right);
    and_(overflow_dst, overflow_dst, scratch, SetRC);
  } else if (dst.is(right)) {
    mr(scratch, right);           // Preserve right.
    add(dst, left, right);        // Right is overwritten.
    xor_(scratch, dst, scratch);  // Original right.
    xor_(overflow_dst, dst, left);
    and_(overflow_dst, overflow_dst, scratch, SetRC);
  } else {
    add(dst, left, right);
    xor_(overflow_dst, dst, left);
    xor_(scratch, dst, right);
    and_(overflow_dst, scratch, overflow_dst, SetRC);
  }
}


void MacroAssembler::CompareMap(Register obj,
                                Register scratch,
                                Handle<Map> map,
                                Label* early_success,
                                CompareMapMode mode) {
  lwz(scratch, FieldMemOperand(obj, HeapObject::kMapOffset));
  CompareMap(scratch, map, early_success, mode);
}


void MacroAssembler::CompareMap(Register obj_map,
                                Handle<Map> map,
                                Label* early_success,
                                CompareMapMode mode) {
  mov(r0, Operand(map));
  cmp(obj_map, r0);
  if (mode == ALLOW_ELEMENT_TRANSITION_MAPS) {
    ElementsKind kind = map->elements_kind();
    if (IsFastElementsKind(kind)) {
      bool packed = IsFastPackedElementsKind(kind);
      Map* current_map = *map;
      while (CanTransitionToMoreGeneralFastElementsKind(kind, packed)) {
        kind = GetNextMoreGeneralFastElementsKind(kind, packed);
        current_map = current_map->LookupElementsTransitionMap(kind);
        if (!current_map) break;
        beq(early_success);
        mov(r0, Operand(Handle<Map>(current_map)));
        cmp(obj_map, r0);
      }
    }
  }
}


void MacroAssembler::CheckMap(Register obj,
                              Register scratch,
                              Handle<Map> map,
                              Label* fail,
                              SmiCheckType smi_check_type,
                              CompareMapMode mode) {
  if (smi_check_type == DO_SMI_CHECK) {
    JumpIfSmi(obj, fail);
  }

  Label success;
  CompareMap(obj, scratch, map, &success, mode);
  bne(fail);
  bind(&success);
}


void MacroAssembler::CheckMap(Register obj,
                              Register scratch,
                              Heap::RootListIndex index,
                              Label* fail,
                              SmiCheckType smi_check_type) {
  if (smi_check_type == DO_SMI_CHECK) {
    JumpIfSmi(obj, fail);
  }
  lwz(scratch, FieldMemOperand(obj, HeapObject::kMapOffset));
  LoadRoot(ip, index);
  cmp(scratch, ip);
  bne(fail);
}


void MacroAssembler::DispatchMap(Register obj,
                                 Register scratch,
                                 Handle<Map> map,
                                 Handle<Code> success,
                                 SmiCheckType smi_check_type) {
  Label fail;
  if (smi_check_type == DO_SMI_CHECK) {
    JumpIfSmi(obj, &fail);
  }
  lwz(scratch, FieldMemOperand(obj, HeapObject::kMapOffset));
  mov(ip, Operand(map));
  cmp(scratch, ip);
  bne(&fail);
  Jump(success, RelocInfo::CODE_TARGET, al);
  bind(&fail);
}


void MacroAssembler::TryGetFunctionPrototype(Register function,
                                             Register result,
                                             Register scratch,
                                             Label* miss,
                                             bool miss_on_bound_function) {
  // Check that the receiver isn't a smi.
  JumpIfSmi(function, miss);

  // Check that the function really is a function.  Load map into result reg.
  CompareObjectType(function, result, scratch, JS_FUNCTION_TYPE);
  bne(miss);

  if (miss_on_bound_function) {
    lwz(scratch,
        FieldMemOperand(function, JSFunction::kSharedFunctionInfoOffset));
    lwz(scratch,
        FieldMemOperand(scratch, SharedFunctionInfo::kCompilerHintsOffset));
    andi(r0, scratch,
        Operand(Smi::FromInt(1 << SharedFunctionInfo::kBoundFunction)));
    bne(miss, cr0);
  }

  // Make sure that the function has an instance prototype.
  Label non_instance;
  lbz(scratch, FieldMemOperand(result, Map::kBitFieldOffset));
  andi(r0, scratch, Operand(1 << Map::kHasNonInstancePrototype));
  bne(&non_instance, cr0);

  // Get the prototype or initial map from the function.
  lwz(result,
      FieldMemOperand(function, JSFunction::kPrototypeOrInitialMapOffset));

  // If the prototype or initial map is the hole, don't return it and
  // simply miss the cache instead. This will allow us to allocate a
  // prototype object on-demand in the runtime system.
  LoadRoot(ip, Heap::kTheHoleValueRootIndex);
  cmp(result, ip);
  beq(miss);

  // If the function does not have an initial map, we're done.
  Label done;
  CompareObjectType(result, scratch, scratch, MAP_TYPE);
  bne(&done);

  // Get the prototype from the initial map.
  lwz(result, FieldMemOperand(result, Map::kPrototypeOffset));
  jmp(&done);

  // Non-instance prototype: Fetch prototype from constructor field
  // in initial map.
  bind(&non_instance);
  lwz(result, FieldMemOperand(result, Map::kConstructorOffset));

  // All done.
  bind(&done);
}


void MacroAssembler::CallStub(CodeStub* stub, Condition cond) {
  ASSERT(AllowThisStubCall(stub));  // Stub calls are not allowed in some stubs.
  Call(stub->GetCode(), RelocInfo::CODE_TARGET, TypeFeedbackId::None(), cond);
}


void MacroAssembler::TailCallStub(CodeStub* stub, Condition cond) {
  ASSERT(allow_stub_calls_ || stub->CompilingCallsToThisStubIsGCSafe());
  Jump(stub->GetCode(), RelocInfo::CODE_TARGET, cond);
}


static int AddressOffset(ExternalReference ref0, ExternalReference ref1) {
  return ref0.address() - ref1.address();
}


void MacroAssembler::CallApiFunctionAndReturn(ExternalReference function,
                                              int stack_space,
                                              FunctionCallType type) {
  ExternalReference next_address =
      ExternalReference::handle_scope_next_address();
  const int kNextOffset = 0;
  const int kLimitOffset = AddressOffset(
      ExternalReference::handle_scope_limit_address(),
      next_address);
  const int kLevelOffset = AddressOffset(
      ExternalReference::handle_scope_level_address(),
      next_address);

  // Allocate HandleScope in callee-save registers.
  // r26 - next_address
  // r27 - next_address->kNextOffset
  // r28 - next_address->kLimitOffset
  // r29 - next_address->kLevelOffset
  mov(r26, Operand(next_address));
  lwz(r27, MemOperand(r26, kNextOffset));
  lwz(r28, MemOperand(r26, kLimitOffset));
  lwz(r29, MemOperand(r26, kLevelOffset));
  addi(r29, r29, Operand(1));
  stw(r29, MemOperand(r26, kLevelOffset));

  // Native call returns to the DirectCEntry stub which redirects to the
  // return address pushed on stack (could have moved after GC).
  // DirectCEntry stub itself is generated early and never moves.
  DirectCEntryStub stub;
  stub.GenerateCall(this, function, type);

  Label promote_scheduled_exception;
  Label delete_allocated_handles;
  Label leave_exit_frame;
  Label skip1, skip2;

  // If result is non-zero, dereference to get the result value
  // otherwise set it to undefined.
  cmpi(r3, Operand(0));
  bne(&skip1);
  LoadRoot(r3, Heap::kUndefinedValueRootIndex);
  b(&skip2);
  bind(&skip1);
  lwz(r3, MemOperand(r3));
  bind(&skip2);

  // No more valid handles (the result handle was the last one). Restore
  // previous handle scope.
  stw(r27, MemOperand(r26, kNextOffset));
  if (emit_debug_code()) {
    lwz(r4, MemOperand(r26, kLevelOffset));
    cmp(r4, r29);
    Check(eq, "Unexpected level after return from api call");
  }
  sub(r29, r29, Operand(1));
  stw(r29, MemOperand(r26, kLevelOffset));
  lwz(ip, MemOperand(r26, kLimitOffset));
  cmp(r28, ip);
  bne(&delete_allocated_handles);

  // Check if the function scheduled an exception.
  bind(&leave_exit_frame);
  LoadRoot(r27, Heap::kTheHoleValueRootIndex);
  mov(ip, Operand(ExternalReference::scheduled_exception_address(isolate())));
  lwz(r28, MemOperand(ip));
  cmp(r27, r28);
  bne(&promote_scheduled_exception);

  // LeaveExitFrame expects unwind space to be in a register.
  mov(r27, Operand(stack_space));
  LeaveExitFrame(false, r27);
  blr();

  bind(&promote_scheduled_exception);
  TailCallExternalReference(
      ExternalReference(Runtime::kPromoteScheduledException, isolate()),
      0,
      1);

  // HandleScope limit has changed. Delete allocated extensions.
  bind(&delete_allocated_handles);
  stw(r28, MemOperand(r26, kLimitOffset));
  mr(r27, r3);
  PrepareCallCFunction(1, r28);
  mov(r3, Operand(ExternalReference::isolate_address()));
  CallCFunction(
      ExternalReference::delete_handle_scope_extensions(isolate()), 1);
  mr(r3, r27);
  jmp(&leave_exit_frame);
}


bool MacroAssembler::AllowThisStubCall(CodeStub* stub) {
  if (!has_frame_ && stub->SometimesSetsUpAFrame()) return false;
  return allow_stub_calls_ || stub->CompilingCallsToThisStubIsGCSafe();
}


void MacroAssembler::IllegalOperation(int num_arguments) {
  if (num_arguments > 0) {
    Add(sp, sp, num_arguments * kPointerSize, r0);
  }
  LoadRoot(r0, Heap::kUndefinedValueRootIndex);
}


void MacroAssembler::IndexFromHash(Register hash, Register index) {
  // If the hash field contains an array index pick it out. The assert checks
  // that the constants for the maximum number of digits for an array index
  // cached in the hash field and the number of bits reserved for it does not
  // conflict.
  ASSERT(TenToThe(String::kMaxCachedArrayIndexLength) <
         (1 << String::kArrayIndexValueBits));
  // We want the smi-tagged index in key.  kArrayIndexValueMask has zeros in
  // the low kHashShift bits.
  STATIC_ASSERT(kSmiTag == 0);
  STATIC_ASSERT(String::kHashShift == 2);
  STATIC_ASSERT(String::kArrayIndexValueBits == 24);
  // This function is performing the logic: index = (hash & 0x03FFFFFC) >> 1;
  rlwinm(index, hash, 31, 7, 30);
}

void MacroAssembler::SmiToDoubleFPRegister(Register smi,
                                            DwVfpRegister value,
                                            Register scratch1,
                                            DwVfpRegister scratch2) {
  srawi(scratch1, smi, kSmiTagSize);
  sub(sp, sp, Operand(16));   // reserve two temporary doubles on the stack
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
  lis(r0, Operand(0x4330));
  stw(r0, MemOperand(sp, 4));
  stw(r0, MemOperand(sp, 12));
  lis(r0, Operand(SIGN_EXT_IMM16(0x8000)));
  stw(r0, MemOperand(sp, 0));
  xor_(r0, scratch1, r0);
  stw(r0, MemOperand(sp, 8));
#else
  lis(r0, Operand(0x4330));
  stw(r0, MemOperand(sp, 0));
  stw(r0, MemOperand(sp, 8));
  lis(r0, Operand(SIGN_EXT_IMM16(0x8000)));
  stw(r0, MemOperand(sp, 4));
  xor_(r0, scratch1, r0);
  stw(r0, MemOperand(sp, 12));
#endif
  lfd(value, MemOperand(sp, 0));
  lfd(scratch2, MemOperand(sp, 8));
  addi(sp, sp, Operand(16));  // restore stack
  fsub(value, scratch2, value);
}


// Tries to get a signed int32 out of a double precision floating point heap
// number. Rounds towards 0. Branch to 'not_int32' if the double is out of the
// 32bits signed integer range.
void MacroAssembler::ConvertToInt32(Register source,
                                    Register dest,
                                    Register scratch,
                                    Register scratch2,
                                    DwVfpRegister double_scratch,
                                    Label *not_int32) {
  addi(sp, sp, Operand(-2 * kPointerSize));

  // Retrieve double from heap
  lfd(double_scratch, FieldMemOperand(source, HeapNumber::kValueOffset));

  // Convert
  fctiwz(double_scratch, double_scratch);
  stfd(double_scratch, MemOperand(sp, 0));
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
  lwz(dest, MemOperand(sp, 0));
#else
  lwz(dest, MemOperand(sp, kPointerSize));
#endif

  addi(sp, sp, Operand(2 * kPointerSize));

  // fctiwz instruction will saturate to the minimum (0x80000000) or
  // maximum (0x7fffffff) signed 32bits integer when the double is out of
  // range. When substracting one, the minimum signed integer becomes the
  // maximun signed integer.
  addi(scratch, dest, Operand(-1));
  mov(scratch2, Operand(LONG_MAX - 1));
  cmp(scratch, scratch2);
  // If equal then dest was LONG_MAX, if greater dest was LONG_MIN.
  bge(not_int32);
}


// Temporary compatibility function
void MacroAssembler::EmitVFPTruncate(VFPRoundingMode rounding_mode,
                                     SwVfpRegister result,
                                     DwVfpRegister double_input,
                                     Register scratch1,
                                     Register scratch2,
                                     CheckForInexactConversion check_inexact) {
  PPCPORT_UNIMPLEMENTED();
  fake_asm(fMASM5);
  // Fail (penguin: assert triggered in mjsunit/sparse-array-reverse)
  // ASSERT(false);
}



void MacroAssembler::EmitVFPTruncate(VFPRoundingMode rounding_mode,
                                     DwVfpRegister result,
                                     DwVfpRegister double_input,
                                     Register scratch1,
                                     Register scratch2,
                                     CheckForInexactConversion check_inexact) {
  // Not sure if we need inexact conversion test on PowerPC
#if 0
  int32_t check_inexact_conversion =
    (check_inexact == kCheckForInexactConversion) ? kVFPInexactExceptionBit : 0;
#endif

  ASSERT((rounding_mode == kRoundToZero)
          || (rounding_mode == kRoundToMinusInf));
  // Actually, Power defaults to round to nearest.. so we will need to
  // fix this eventually
  frsp(result, double_input, SetRC);

  // Special branches must follow if the condition is required
  // CR1 is set as follows: FX,FEX,VX,OX
  // bit offset 4 - FX Floating-Point Exception
  // bit offset 5 - FEX Floating-Point Enabled Exception
  // bit offset 6 - VX Floating-Point Invalid Operation Exception
  // bit offset 7 - OX Floating-Point Overflow Exception
}


void MacroAssembler::EmitOutOfInt32RangeTruncate(Register result,
                                                 Register input_high,
                                                 Register input_low,
                                                 Register scratch) {
  Label done, high_shift_needed, pos_shift, neg_shift, shift_done;

  li(result, Operand(0));

  // check for NaN or +/-Infinity
  // by extracting exponent (mask: 0x7ff00000)
  STATIC_ASSERT(HeapNumber::kExponentMask == 0x7ff00000u);
  rlwinm(scratch, input_high, 12, 21, 31, LeaveRC);
  cmpli(scratch, Operand(0x7ff));
  beq(&done);

  // Express exponent as delta to (number of mantissa bits + 31).
  addi(scratch,
       scratch,
       Operand(-(HeapNumber::kExponentBias + HeapNumber::kMantissaBits + 31)));

  // If the delta is strictly positive, all bits would be shifted away,
  // which means that we can return 0.
  cmpi(scratch, Operand(0));
  bgt(&done);

  const int kShiftBase = HeapNumber::kNonMantissaBitsInTopWord - 1;
  // Calculate shift.
  addi(scratch, scratch, Operand(kShiftBase + HeapNumber::kMantissaBits));

  // Save the sign.
  STATIC_ASSERT(HeapNumber::kSignMask == 0x80000000u);
  Register sign = result;
  result = no_reg;
  rlwinm(sign, input_high, 0, 0, 0, LeaveRC);

  // Shifts >= 32 bits should result in zero.
  // slw extracts only the 6 most significant bits of the shift value.
  cmpi(scratch, Operand(32));
  blt(&high_shift_needed);
  li(input_high, Operand(0));
  subfic(scratch, scratch, Operand(32));
  b(&neg_shift);

  // Set the implicit 1 before the mantissa part in input_high.
  bind(&high_shift_needed);
  STATIC_ASSERT(HeapNumber::kMantissaBitsInTopWord >= 16);
  oris(input_high,
       input_high,
       Operand(1 << ((HeapNumber::kMantissaBitsInTopWord) - 16)));
  // Shift the mantissa bits to the correct position.
  // We don't need to clear non-mantissa bits as they will be shifted away.
  // If they weren't, it would mean that the answer is in the 32bit range.
  slw(input_high, input_high, scratch);
  subfic(scratch, scratch, Operand(32));
  b(&pos_shift);

  // Replace the shifted bits with bits from the lower mantissa word.

  bind(&neg_shift);
  neg(scratch, scratch);
  slw(input_low, input_low, scratch);
  b(&shift_done);

  bind(&pos_shift);
  srw(input_low, input_low, scratch);

  bind(&shift_done);
  orx(input_high, input_high, input_low);

  // Restore sign if necessary.
  cmpi(sign, Operand(0));
  result = sign;
  sign = no_reg;
  mr(result, input_high);
  beq(&done);
  neg(result, result);

  bind(&done);
}


void MacroAssembler::EmitECMATruncate(Register result,
                                      DwVfpRegister double_input,
                                      SwVfpRegister single_scratch,
                                      Register scratch,
                                      Register input_high,
                                      Register input_low) {
#ifdef PENGUIN_CLEANUP
  CpuFeatures::Scope scope(VFP2);
  ASSERT(!input_high.is(result));
  ASSERT(!input_low.is(result));
  ASSERT(!input_low.is(input_high));
  ASSERT(!scratch.is(result) &&
         !scratch.is(input_high) &&
         !scratch.is(input_low));
  ASSERT(!single_scratch.is(double_input.low()) &&
         !single_scratch.is(double_input.high()));

  Label done;

  // Clear cumulative exception flags.
  ClearFPSCRBits(kVFPExceptionMask, scratch);
  // Try a conversion to a signed integer.
  vcvt_s32_f64(single_scratch, double_input);
  vmov(result, single_scratch);
  // Retrieve he FPSCR.
  vmrs(scratch);
  // Check for overflow and NaNs.
  tst(scratch, Operand(kVFPOverflowExceptionBit |
                       kVFPUnderflowExceptionBit |
                       kVFPInvalidOpExceptionBit));
  // If we had no exceptions we are done.
  beq(&done);

  // Load the double value and perform a manual truncation.
  vmov(input_low, input_high, double_input);
  EmitOutOfInt32RangeTruncate(result,
                              input_high,
                              input_low,
                              scratch);
  bind(&done);
#else
  PPCPORT_UNIMPLEMENTED();
  fake_asm(fMASM6);
#endif
}


void MacroAssembler::GetLeastBitsFromSmi(Register dst,
                                         Register src,
                                         int num_least_bits) {
  rlwinm(dst, src, 31, 32 - num_least_bits, 31);
}


void MacroAssembler::GetLeastBitsFromInt32(Register dst,
                                           Register src,
                                           int num_least_bits) {
  rlwinm(dst, src, 0, 32 - num_least_bits, 31);
}


void MacroAssembler::CallRuntime(const Runtime::Function* f,
                                 int num_arguments) {
  // All parameters are on the stack.  r3 has the return value after call.

  // If the expected number of arguments of the runtime function is
  // constant, we check that the actual number of arguments match the
  // expectation.
  if (f->nargs >= 0 && f->nargs != num_arguments) {
    IllegalOperation(num_arguments);
    return;
  }

  // TODO(1236192): Most runtime routines don't need the number of
  // arguments passed in because it is constant. At some point we
  // should remove this need and make the runtime routine entry code
  // smarter.
  mov(r3, Operand(num_arguments));
  mov(r4, Operand(ExternalReference(f, isolate())));
  CEntryStub stub(1);
  CallStub(&stub);
}


void MacroAssembler::CallRuntime(Runtime::FunctionId fid, int num_arguments) {
  CallRuntime(Runtime::FunctionForId(fid), num_arguments);
}


void MacroAssembler::CallRuntimeSaveDoubles(Runtime::FunctionId id) {
  const Runtime::Function* function = Runtime::FunctionForId(id);
  li(r3, Operand(function->nargs));
  mov(r4, Operand(ExternalReference(function, isolate())));
  CEntryStub stub(1, kSaveFPRegs);
  CallStub(&stub);
}


void MacroAssembler::CallExternalReference(const ExternalReference& ext,
                                           int num_arguments) {
  mov(r3, Operand(num_arguments));
  mov(r4, Operand(ext));

  CEntryStub stub(1);
  CallStub(&stub);
}


void MacroAssembler::TailCallExternalReference(const ExternalReference& ext,
                                               int num_arguments,
                                               int result_size) {
  // TODO(1236192): Most runtime routines don't need the number of
  // arguments passed in because it is constant. At some point we
  // should remove this need and make the runtime routine entry code
  // smarter.
  mov(r3, Operand(num_arguments));
  JumpToExternalReference(ext);
}


void MacroAssembler::TailCallRuntime(Runtime::FunctionId fid,
                                     int num_arguments,
                                     int result_size) {
  TailCallExternalReference(ExternalReference(fid, isolate()),
                            num_arguments,
                            result_size);
}


void MacroAssembler::JumpToExternalReference(const ExternalReference& builtin) {
  mov(r4, Operand(builtin));
  CEntryStub stub(1);
  Jump(stub.GetCode(), RelocInfo::CODE_TARGET);
}


void MacroAssembler::InvokeBuiltin(Builtins::JavaScript id,
                                   InvokeFlag flag,
                                   const CallWrapper& call_wrapper) {
  // You can't call a builtin without a valid frame.
  ASSERT(flag == JUMP_FUNCTION || has_frame());

  GetBuiltinEntry(r5, id);
  if (flag == CALL_FUNCTION) {
    call_wrapper.BeforeCall(CallSize(r2));
    SetCallKind(r8, CALL_AS_METHOD);
    Call(r5);
    call_wrapper.AfterCall();
  } else {
    ASSERT(flag == JUMP_FUNCTION);
    SetCallKind(r8, CALL_AS_METHOD);
    Jump(r5);
  }
}


void MacroAssembler::GetBuiltinFunction(Register target,
                                        Builtins::JavaScript id) {
  // Load the builtins object into target register.
  lwz(target,
      MemOperand(cp, Context::SlotOffset(Context::GLOBAL_OBJECT_INDEX)));
  lwz(target, FieldMemOperand(target, GlobalObject::kBuiltinsOffset));
  // Load the JavaScript builtin function from the builtins object.
  lwz(target, FieldMemOperand(target,
                          JSBuiltinsObject::OffsetOfFunctionWithId(id)));
}


void MacroAssembler::GetBuiltinEntry(Register target, Builtins::JavaScript id) {
  ASSERT(!target.is(r4));
  GetBuiltinFunction(r4, id);
  // Load the code entry point from the builtins object.
  lwz(target, FieldMemOperand(r4, JSFunction::kCodeEntryOffset));
}


void MacroAssembler::SetCounter(StatsCounter* counter, int value,
                                Register scratch1, Register scratch2) {
  if (FLAG_native_code_counters && counter->Enabled()) {
    mov(scratch1, Operand(value));
    mov(scratch2, Operand(ExternalReference(counter)));
    stw(scratch1, MemOperand(scratch2));
  }
}


void MacroAssembler::IncrementCounter(StatsCounter* counter, int value,
                                      Register scratch1, Register scratch2) {
  ASSERT(value > 0);
  if (FLAG_native_code_counters && counter->Enabled()) {
    mov(scratch2, Operand(ExternalReference(counter)));
    lwz(scratch1, MemOperand(scratch2));
    addi(scratch1, scratch1, Operand(value));
    stw(scratch1, MemOperand(scratch2));
  }
}


void MacroAssembler::DecrementCounter(StatsCounter* counter, int value,
                                      Register scratch1, Register scratch2) {
  ASSERT(value > 0);
  if (FLAG_native_code_counters && counter->Enabled()) {
    mov(scratch2, Operand(ExternalReference(counter)));
    lwz(scratch1, MemOperand(scratch2));
    sub(scratch1, scratch1, Operand(value));
    stw(scratch1, MemOperand(scratch2));
  }
}


void MacroAssembler::Assert(Condition cond, const char* msg, CRegister cr) {
  if (emit_debug_code())
    Check(cond, msg, cr);
}


void MacroAssembler::AssertRegisterIsRoot(Register reg,
                                          Heap::RootListIndex index) {
  if (emit_debug_code()) {
    LoadRoot(ip, index);
    cmp(reg, ip);
    Check(eq, "Register did not match expected root");
  }
}


void MacroAssembler::AssertFastElements(Register elements) {
  if (emit_debug_code()) {
    ASSERT(!elements.is(ip));
    Label ok;
    push(elements);
    lwz(elements, FieldMemOperand(elements, HeapObject::kMapOffset));
    LoadRoot(ip, Heap::kFixedArrayMapRootIndex);
    cmp(elements, ip);
    beq(&ok);
    LoadRoot(ip, Heap::kFixedDoubleArrayMapRootIndex);
    cmp(elements, ip);
    beq(&ok);
    LoadRoot(ip, Heap::kFixedCOWArrayMapRootIndex);
    cmp(elements, ip);
    beq(&ok);
    Abort("JSObject with fast elements map has slow elements");
    bind(&ok);
    pop(elements);
  }
}


void MacroAssembler::Check(Condition cond, const char* msg, CRegister cr) {
  Label L;
  b(cond, &L, cr);
  Abort(msg);
  // will not return here
  bind(&L);
}


void MacroAssembler::Abort(const char* msg) {
  Label abort_start;
  bind(&abort_start);
  // We want to pass the msg string like a smi to avoid GC
  // problems, however msg is not guaranteed to be aligned
  // properly. Instead, we pass an aligned pointer that is
  // a proper v8 smi, but also pass the alignment difference
  // from the real pointer as a smi.
  intptr_t p1 = reinterpret_cast<intptr_t>(msg);
  intptr_t p0 = (p1 & ~kSmiTagMask) + kSmiTag;
  ASSERT(reinterpret_cast<Object*>(p0)->IsSmi());
#ifdef DEBUG
  if (msg != NULL) {
    RecordComment("Abort message: ");
    RecordComment(msg);
  }
#endif

  mov(r0, Operand(p0));
  push(r0);
  mov(r0, Operand(Smi::FromInt(p1 - p0)));
  push(r0);
  // Disable stub call restrictions to always allow calls to abort.
  if (!has_frame_) {
    // We don't actually want to generate a pile of code for this, so just
    // claim there is a stack frame, without generating one.
    FrameScope scope(this, StackFrame::NONE);
    CallRuntime(Runtime::kAbort, 2);
  } else {
    CallRuntime(Runtime::kAbort, 2);
  }
  // will not return here
  if (is_const_pool_blocked()) {
    // If the calling code cares about the exact number of
    // instructions generated, we insert padding here to keep the size
    // of the Abort macro constant.
    static const int kExpectedAbortInstructions = 10;
    int abort_instructions = InstructionsGeneratedSince(&abort_start);
    ASSERT(abort_instructions <= kExpectedAbortInstructions);
    while (abort_instructions++ < kExpectedAbortInstructions) {
      nop();
    }
  }
}


void MacroAssembler::LoadContext(Register dst, int context_chain_length) {
  if (context_chain_length > 0) {
    // Move up the chain of contexts to the context containing the slot.
    lwz(dst, MemOperand(cp, Context::SlotOffset(Context::PREVIOUS_INDEX)));
    for (int i = 1; i < context_chain_length; i++) {
      lwz(dst, MemOperand(dst, Context::SlotOffset(Context::PREVIOUS_INDEX)));
    }
  } else {
    // Slot is in the current function context.  Move it into the
    // destination register in case we store into it (the write barrier
    // cannot be allowed to destroy the context in esi).
    mr(dst, cp);
  }
}


void MacroAssembler::LoadTransitionedArrayMapConditional(
    ElementsKind expected_kind,
    ElementsKind transitioned_kind,
    Register map_in_out,
    Register scratch,
    Label* no_map_match) {
  // Load the global or builtins object from the current context.
  lwz(scratch,
      MemOperand(cp, Context::SlotOffset(Context::GLOBAL_OBJECT_INDEX)));
  lwz(scratch, FieldMemOperand(scratch, GlobalObject::kNativeContextOffset));

  // Check that the function's map is the same as the expected cached map.
  lwz(scratch,
      MemOperand(scratch,
                 Context::SlotOffset(Context::JS_ARRAY_MAPS_INDEX)));
  size_t offset = expected_kind * kPointerSize +
      FixedArrayBase::kHeaderSize;
  lwz(ip, FieldMemOperand(scratch, offset));
  cmp(map_in_out, ip);
  bne(no_map_match);

  // Use the transitioned cached map.
  offset = transitioned_kind * kPointerSize +
      FixedArrayBase::kHeaderSize;
  lwz(map_in_out, FieldMemOperand(scratch, offset));
}


void MacroAssembler::LoadInitialArrayMap(
    Register function_in, Register scratch,
    Register map_out, bool can_have_holes) {
  ASSERT(!function_in.is(map_out));
  Label done;
  lwz(map_out, FieldMemOperand(function_in,
                               JSFunction::kPrototypeOrInitialMapOffset));
  if (!FLAG_smi_only_arrays) {
    ElementsKind kind = can_have_holes ? FAST_HOLEY_ELEMENTS : FAST_ELEMENTS;
    LoadTransitionedArrayMapConditional(FAST_SMI_ELEMENTS,
                                        kind,
                                        map_out,
                                        scratch,
                                        &done);
  } else if (can_have_holes) {
    LoadTransitionedArrayMapConditional(FAST_SMI_ELEMENTS,
                                        FAST_HOLEY_SMI_ELEMENTS,
                                        map_out,
                                        scratch,
                                        &done);
  }
  bind(&done);
}


void MacroAssembler::LoadGlobalFunction(int index, Register function) {
  // Load the global or builtins object from the current context.
  lwz(function,
      MemOperand(cp, Context::SlotOffset(Context::GLOBAL_OBJECT_INDEX)));
  // Load the native context from the global or builtins object.
  lwz(function, FieldMemOperand(function,
                                GlobalObject::kNativeContextOffset));
  // Load the function from the native context.
  lwz(function, MemOperand(function, Context::SlotOffset(index)));
}


void MacroAssembler::LoadGlobalFunctionInitialMap(Register function,
                                                  Register map,
                                                  Register scratch) {
  // Load the initial map. The global functions all have initial maps.
  lwz(map, FieldMemOperand(function, JSFunction::kPrototypeOrInitialMapOffset));
  if (emit_debug_code()) {
    Label ok, fail;
    CheckMap(map, scratch, Heap::kMetaMapRootIndex, &fail, DO_SMI_CHECK);
    b(&ok);
    bind(&fail);
    Abort("Global functions must have initial map");
    bind(&ok);
  }
}


void MacroAssembler::JumpIfNotPowerOfTwoOrZero(
    Register reg,
    Register scratch,
    Label* not_power_of_two_or_zero) {
  cmpi(reg, Operand(0));
  blt(not_power_of_two_or_zero);
  sub(scratch, reg, Operand(1));
  and_(r0, scratch, reg, SetRC);
  bne(not_power_of_two_or_zero, cr0);
}


void MacroAssembler::JumpIfNotPowerOfTwoOrZeroAndNeg(
    Register reg,
    Register scratch,
    Label* zero_and_neg,
    Label* not_power_of_two) {
  sub(scratch, reg, Operand(1));
  cmpi(reg, Operand(0));
  blt(zero_and_neg);
  and_(r0, scratch, reg, SetRC);
  bne(not_power_of_two, cr0);
}


void MacroAssembler::JumpIfNotBothSmi(Register reg1,
                                      Register reg2,
                                      Label* on_not_both_smi) {
  STATIC_ASSERT(kSmiTag == 0);
  ASSERT_EQ(1, kSmiTagMask);
  orx(r0, reg1, reg2, LeaveRC);
  JumpIfNotSmi(r0, on_not_both_smi);
}


void MacroAssembler::UntagAndJumpIfSmi(
    Register dst, Register src, Label* smi_case) {
  STATIC_ASSERT(kSmiTag == 0);
  STATIC_ASSERT(kSmiTagSize == 1);
  rlwinm(r0, src, 0, 31, 31, SetRC);
  srawi(dst, src, kSmiTagSize);
  beq(smi_case, cr0);
}


void MacroAssembler::UntagAndJumpIfNotSmi(
    Register dst, Register src, Label* non_smi_case) {
  STATIC_ASSERT(kSmiTag == 0);
  STATIC_ASSERT(kSmiTagSize == 1);
  rlwinm(r0, src, 0, 31, 31, SetRC);
  srawi(dst, src, kSmiTagSize);
  bne(non_smi_case, cr0);
}


void MacroAssembler::JumpIfEitherSmi(Register reg1,
                                     Register reg2,
                                     Label* on_either_smi) {
  STATIC_ASSERT(kSmiTag == 0);
  JumpIfSmi(reg1, on_either_smi);
  JumpIfSmi(reg2, on_either_smi);
}


void MacroAssembler::AbortIfSmi(Register object) {
  STATIC_ASSERT(kSmiTag == 0);
  andi(r0, object, Operand(kSmiTagMask));
  Assert(ne, "Operand is a smi", cr0);
}


void MacroAssembler::AbortIfNotSmi(Register object) {
  STATIC_ASSERT(kSmiTag == 0);
  andi(r0, object, Operand(kSmiTagMask));
  Assert(eq, "Operand is not smi", cr0);
}


void MacroAssembler::AbortIfNotString(Register object) {
  STATIC_ASSERT(kSmiTag == 0);
  andi(r0, object, Operand(kSmiTagMask));
  Assert(ne, "Operand is not a string", cr0);
  push(object);
  lwz(object, FieldMemOperand(object, HeapObject::kMapOffset));
  CompareInstanceType(object, object, FIRST_NONSTRING_TYPE);
  pop(object);
  Assert(lt, "Operand is not a string");
}



void MacroAssembler::AbortIfNotRootValue(Register src,
                                         Heap::RootListIndex root_value_index,
                                         const char* message) {
  CompareRoot(src, root_value_index);
  Assert(eq, message);
}


void MacroAssembler::JumpIfNotHeapNumber(Register object,
                                         Register heap_number_map,
                                         Register scratch,
                                         Label* on_not_heap_number) {
  lwz(scratch, FieldMemOperand(object, HeapObject::kMapOffset));
  AssertRegisterIsRoot(heap_number_map, Heap::kHeapNumberMapRootIndex);
  cmp(scratch, heap_number_map);
  bne(on_not_heap_number);
}


void MacroAssembler::JumpIfNonSmisNotBothSequentialAsciiStrings(
    Register first,
    Register second,
    Register scratch1,
    Register scratch2,
    Label* failure) {
  // Test that both first and second are sequential ASCII strings.
  // Assume that they are non-smis.
  lwz(scratch1, FieldMemOperand(first, HeapObject::kMapOffset));
  lwz(scratch2, FieldMemOperand(second, HeapObject::kMapOffset));
  lbz(scratch1, FieldMemOperand(scratch1, Map::kInstanceTypeOffset));
  lbz(scratch2, FieldMemOperand(scratch2, Map::kInstanceTypeOffset));

  JumpIfBothInstanceTypesAreNotSequentialAscii(scratch1,
                                               scratch2,
                                               scratch1,
                                               scratch2,
                                               failure);
}

void MacroAssembler::JumpIfNotBothSequentialAsciiStrings(Register first,
                                                         Register second,
                                                         Register scratch1,
                                                         Register scratch2,
                                                         Label* failure) {
  // Check that neither is a smi.
  STATIC_ASSERT(kSmiTag == 0);
  and_(scratch1, first, second);
  JumpIfSmi(scratch1, failure);
  JumpIfNonSmisNotBothSequentialAsciiStrings(first,
                                             second,
                                             scratch1,
                                             scratch2,
                                             failure);
}


// Allocates a heap number or jumps to the need_gc label if the young space
// is full and a scavenge is needed.
void MacroAssembler::AllocateHeapNumber(Register result,
                                        Register scratch1,
                                        Register scratch2,
                                        Register heap_number_map,
                                        Label* gc_required) {
  // Allocate an object in the heap for the heap number and tag it as a heap
  // object.
  AllocateInNewSpace(HeapNumber::kSize,
                     result,
                     scratch1,
                     scratch2,
                     gc_required,
                     TAG_OBJECT);

  // Store heap number map in the allocated object.
  AssertRegisterIsRoot(heap_number_map, Heap::kHeapNumberMapRootIndex);
  stw(heap_number_map, FieldMemOperand(result, HeapObject::kMapOffset));
}


void MacroAssembler::AllocateHeapNumberWithValue(Register result,
                                                 DwVfpRegister value,
                                                 Register scratch1,
                                                 Register scratch2,
                                                 Register heap_number_map,
                                                 Label* gc_required) {
  AllocateHeapNumber(result, scratch1, scratch2, heap_number_map, gc_required);
  stfd(value, FieldMemOperand(result, HeapNumber::kValueOffset));
}


// Copies a fixed number of fields of heap objects from src to dst.
void MacroAssembler::CopyFields(Register dst,
                                Register src,
                                RegList temps,
                                int field_count) {
  // At least one bit set in the first 15 registers.
  ASSERT((temps & ((1 << 15) - 1)) != 0);
  ASSERT((temps & dst.bit()) == 0);
  ASSERT((temps & src.bit()) == 0);
  // Primitive implementation using only one temporary register.

  Register tmp = no_reg;
  // Find a temp register in temps list.
  for (int i = 0; i < 15; i++) {
    if ((temps & (1 << i)) != 0) {
      tmp.set_code(i);
      break;
    }
  }
  ASSERT(!tmp.is(no_reg));

  for (int i = 0; i < field_count; i++) {
    lwz(tmp, FieldMemOperand(src, i * kPointerSize));
    stw(tmp, FieldMemOperand(dst, i * kPointerSize));
  }
}


void MacroAssembler::CopyBytes(Register src,
                               Register dst,
                               Register length,
                               Register scratch) {
  Label align_loop, align_loop_1, word_loop, byte_loop, byte_loop_1, done;

  // Align src before copying in word size chunks.
  bind(&align_loop);
  cmpi(length, Operand(0));
  beq(&done);
  bind(&align_loop_1);
  andi(r0, src, Operand(kPointerSize - 1));
  beq(&word_loop, cr0);
  lbz(scratch, MemOperand(src));
  addi(src, src, Operand(1));
  stb(scratch, MemOperand(dst));
  addi(dst, dst, Operand(1));
  sub(length, length, Operand(1));
  cmpi(r0, Operand(0));
  bne(&byte_loop_1);

  // Copy bytes in word size chunks.
  bind(&word_loop);
  if (emit_debug_code()) {
    andi(r0, src, Operand(kPointerSize - 1));
    Assert(eq, "Expecting alignment for CopyBytes", cr0);
  }
  cmpi(length, Operand(kPointerSize));
  blt(&byte_loop);
  lwz(scratch, MemOperand(src));
  addi(src, src, Operand(kPointerSize));
#if CAN_USE_UNALIGNED_ACCESSES
  // currently false for PPC - but possible future opt
  stw(scratch, MemOperand(dst));
  addi(dst, dst, Operand(kPointerSize));
#else
#if __BYTE_ORDER == __LITTLE_ENDIAN
  stb(scratch, MemOperand(dst, 0));
  srwi(scratch, scratch, Operand(8));
  stb(scratch, MemOperand(dst, 1));
  srwi(scratch, scratch, Operand(8));
  stb(scratch, MemOperand(dst, 2));
  srwi(scratch, scratch, Operand(8));
  stb(scratch, MemOperand(dst, 3));
#else
  stb(scratch, MemOperand(dst, 3));
  srwi(scratch, scratch, Operand(8));
  stb(scratch, MemOperand(dst, 2));
  srwi(scratch, scratch, Operand(8));
  stb(scratch, MemOperand(dst, 1));
  srwi(scratch, scratch, Operand(8));
  stb(scratch, MemOperand(dst, 0));
#endif
  addi(dst, dst, Operand(4));
#endif
  sub(length, length, Operand(kPointerSize));
  b(&word_loop);

  // Copy the last bytes if any left.
  bind(&byte_loop);
  cmpi(length, Operand(0));
  beq(&done);
  bind(&byte_loop_1);
  lbz(scratch, MemOperand(src));
  addi(src, src, Operand(1));
  stb(scratch, MemOperand(dst));
  addi(dst, dst, Operand(1));
  sub(length, length, Operand(1));
  cmpi(length, Operand(0));
  bne(&byte_loop_1);
  bind(&done);
}


void MacroAssembler::InitializeFieldsWithFiller(Register start_offset,
                                                Register end_offset,
                                                Register filler) {
  Label loop, entry;
  b(&entry);
  bind(&loop);
  stw(filler, MemOperand(start_offset));
  addi(start_offset, start_offset, Operand(kPointerSize));
  bind(&entry);
  cmp(start_offset, end_offset);
  blt(&loop);
}


void MacroAssembler::JumpIfBothInstanceTypesAreNotSequentialAscii(
    Register first,
    Register second,
    Register scratch1,
    Register scratch2,
    Label* failure) {
  int kFlatAsciiStringMask =
      kIsNotStringMask | kStringEncodingMask | kStringRepresentationMask;
  int kFlatAsciiStringTag = ASCII_STRING_TYPE;
  andi(scratch1, first, Operand(kFlatAsciiStringMask));
  andi(scratch2, second, Operand(kFlatAsciiStringMask));
  cmpi(scratch1, Operand(kFlatAsciiStringTag));
  bne(failure);
  cmpi(scratch2, Operand(kFlatAsciiStringTag));
  bne(failure);
}


void MacroAssembler::JumpIfInstanceTypeIsNotSequentialAscii(Register type,
                                                            Register scratch,
                                                            Label* failure) {
  int kFlatAsciiStringMask =
      kIsNotStringMask | kStringEncodingMask | kStringRepresentationMask;
  int kFlatAsciiStringTag = ASCII_STRING_TYPE;
  andi(scratch, type, Operand(kFlatAsciiStringMask));
  cmpi(scratch, Operand(kFlatAsciiStringTag));
  bne(failure);
}

static const int kRegisterPassedArguments = 4;


int MacroAssembler::CalculateStackPassedWords(int num_reg_arguments,
                                              int num_double_arguments) {
  int stack_passed_words = 0;
  if (use_eabi_hardfloat()) {
    // In the hard floating point calling convention, we can use
    // all double registers to pass doubles.
    if (num_double_arguments > DoubleRegister::kNumRegisters) {
      stack_passed_words +=
          2 * (num_double_arguments - DoubleRegister::kNumRegisters);
    }
  } else {
    // In the soft floating point calling convention, every double
    // argument is passed using two registers.
    num_reg_arguments += 2 * num_double_arguments;
  }
  // Up to four simple arguments are passed in registers r0..r3.
  if (num_reg_arguments > kRegisterPassedArguments) {
    stack_passed_words += num_reg_arguments - kRegisterPassedArguments;
  }
  return stack_passed_words;
}


void MacroAssembler::PrepareCallCFunction(int num_reg_arguments,
                                          int num_double_arguments,
                                          Register scratch) {
  int frame_alignment = ActivationFrameAlignment();
  int stack_passed_arguments = CalculateStackPassedWords(
      num_reg_arguments, num_double_arguments);
  if (frame_alignment > kPointerSize) {
    // Make stack end at alignment and make room for num_arguments - 4 words
    // and the original value of sp (on native +2 empty slots to make ABI work)
    mr(scratch, sp);
#if defined(V8_HOST_ARCH_PPC)
    sub(sp, sp, Operand((stack_passed_arguments + 1 + 2) * kPointerSize));
#else
    sub(sp, sp, Operand((stack_passed_arguments + 1) * kPointerSize));
#endif
    ASSERT(IsPowerOf2(frame_alignment));
    li(r0, Operand(-frame_alignment));
    and_(sp, sp, r0);
#if defined(V8_HOST_ARCH_PPC)
    // On the simulator we pass args on the stack
    stw(scratch, MemOperand(sp));
#else
    // On the simulator we pass args on the stack
    stw(scratch, MemOperand(sp, stack_passed_arguments * kPointerSize));
#endif
  } else {
    // this case appears to never beused on PPC (even simulated)
    sub(sp, sp, Operand(stack_passed_arguments * kPointerSize));
  }
}


void MacroAssembler::PrepareCallCFunction(int num_reg_arguments,
                                          Register scratch) {
  PrepareCallCFunction(num_reg_arguments, 0, scratch);
}


void MacroAssembler::SetCallCDoubleArguments(DoubleRegister dreg) {
  ASSERT(CpuFeatures::IsSupported(VFP2));
  if (use_eabi_hardfloat()) {
    Move(d0, dreg);
  } else {
    vmov(r0, r1, dreg);
  }
}


void MacroAssembler::SetCallCDoubleArguments(DoubleRegister dreg1,
                                             DoubleRegister dreg2) {
  ASSERT(CpuFeatures::IsSupported(VFP2));
  if (use_eabi_hardfloat()) {
    if (dreg2.is(d0)) {
      ASSERT(!dreg1.is(d1));
      Move(d1, dreg2);
      Move(d0, dreg1);
    } else {
      Move(d0, dreg1);
      Move(d1, dreg2);
    }
  } else {
    vmov(r0, r1, dreg1);
    vmov(r2, r3, dreg2);
  }
}


void MacroAssembler::SetCallCDoubleArguments(DoubleRegister dreg,
                                             Register reg) {
  ASSERT(CpuFeatures::IsSupported(VFP2));
  if (use_eabi_hardfloat()) {
    Move(d0, dreg);
    Move(r0, reg);
  } else {
    Move(r2, reg);
    vmov(r0, r1, dreg);
  }
}


void MacroAssembler::CallCFunction(ExternalReference function,
                                   int num_reg_arguments,
                                   int num_double_arguments) {
  mov(ip, Operand(function));
  CallCFunctionHelper(ip, num_reg_arguments, num_double_arguments);
}


void MacroAssembler::CallCFunction(Register function,
                                   int num_reg_arguments,
                                   int num_double_arguments) {
  CallCFunctionHelper(function, num_reg_arguments, num_double_arguments);
}


void MacroAssembler::CallCFunction(ExternalReference function,
                                   int num_arguments) {
  CallCFunction(function, num_arguments, 0);
}


void MacroAssembler::CallCFunction(Register function,
                                   int num_arguments) {
  CallCFunction(function, num_arguments, 0);
}


void MacroAssembler::CallCFunctionHelper(Register function,
                                         int num_reg_arguments,
                                         int num_double_arguments) {
  ASSERT(has_frame());
  // Make sure that the stack is aligned before calling a C function unless
  // running in the simulator. The simulator has its own alignment check which
  // provides more information.
#if defined(V8_HOST_ARCH_ARM)  // never used on PPC
  if (emit_debug_code()) {
    int frame_alignment = OS::ActivationFrameAlignment();
    int frame_alignment_mask = frame_alignment - 1;
    if (frame_alignment > kPointerSize) {
      ASSERT(IsPowerOf2(frame_alignment));
      Label alignment_as_expected;
      tst(sp, Operand(frame_alignment_mask));
      beq(&alignment_as_expected);
      // Don't use Check here, as it will call Runtime_Abort possibly
      // re-entering here.
      stop("Unexpected alignment");
      bind(&alignment_as_expected);
    }
  }
#endif

  // Just call directly. The function called cannot cause a GC, or
  // allow preemption, so the return address in the link register
  // stays correct.
  Call(function);
  int stack_passed_arguments = CalculateStackPassedWords(
      num_reg_arguments, num_double_arguments);
  if (ActivationFrameAlignment() > kPointerSize) {
#if defined(V8_HOST_ARCH_PPC)
    // On real hardware we follow the ABI
    lwz(sp, MemOperand(sp));
#else
    // On the simulator we pass args on the stack
    lwz(sp, MemOperand(sp, stack_passed_arguments * kPointerSize));
#endif
  } else {
    // this case appears to never beused on PPC (even simulated)
    Add(sp, sp, stack_passed_arguments * sizeof(kPointerSize), r0);
  }
}


void MacroAssembler::GetRelocatedValueLocation(Register lwz_location,
                               Register result) {
#ifdef PENGUIN_CLEANUP
  const uint32_t kLdrOffsetMask = (1 << 12) - 1;
  const int32_t kPCRegOffset = 2 * kPointerSize;
  lwz(result, MemOperand(lwz_location));
  if (emit_debug_code()) {
    // Check that the instruction is a lwz reg, [pc + offset] .
    ASSERT((uint32_t)kLwzPCMask == 0xffff0000);
    srwi(result, result, Operand(16));
    cmpli(result, Operand((uint32_t)kLwzPCPattern >> 16));
    Check(eq, "The instruction to patch should be a load from pc.");
    // Result was clobbered. Restore it.
    lwz(result, MemOperand(lwz_location));
  }
  // Get the address of the constant.
  andi(result, result, Operand(kLdrOffsetMask));
  addi(result, lwz_location, Operand(result));
  addi(result, result, Operand(kPCRegOffset));
#else
  PPCPORT_UNIMPLEMENTED();
  fake_asm(fMASM8);
#endif
}


void MacroAssembler::CheckPageFlag(
    Register object,
    Register scratch,  // scratch may be same register as object
    int mask,
    Condition cc,
    Label* condition_met) {
  ASSERT(cc == ne || cc == eq);
  ASSERT((~Page::kPageAlignmentMask & 0xffff) == 0);
  lis(r0, Operand((~Page::kPageAlignmentMask >> 16)));
  and_(scratch, object, r0);
  lwz(scratch, MemOperand(scratch, MemoryChunk::kFlagsOffset));
  li(r0, Operand(mask));
  and_(r0, r0, scratch, SetRC);
  if (cc == ne) {
    bne(condition_met, cr0);
  }
  if (cc == eq) {
    beq(condition_met, cr0);
  }
}


void MacroAssembler::JumpIfBlack(Register object,
                                 Register scratch0,
                                 Register scratch1,
                                 Label* on_black) {
  HasColor(object, scratch0, scratch1, on_black, 1, 0);  // kBlackBitPattern.
  ASSERT(strcmp(Marking::kBlackBitPattern, "10") == 0);
}


void MacroAssembler::HasColor(Register object,
                              Register bitmap_scratch,
                              Register mask_scratch,
                              Label* has_color,
                              int first_bit,
                              int second_bit) {
  ASSERT(!AreAliased(object, bitmap_scratch, mask_scratch, no_reg));

  GetMarkBits(object, bitmap_scratch, mask_scratch);

  Label other_color, word_boundary;
  lwz(ip, MemOperand(bitmap_scratch, MemoryChunk::kHeaderSize));
  and_(r0, ip, mask_scratch, SetRC);
  b(first_bit == 1 ? eq : ne, &other_color, cr0);
  // Shift left 1
  rlwinm(mask_scratch, mask_scratch, 1, 0, 30, SetRC);
  beq(&word_boundary, cr0);
  and_(r0, ip, mask_scratch, SetRC);
  b(second_bit == 1 ? ne : eq, has_color, cr0);
  jmp(&other_color);

  bind(&word_boundary);
  lwz(ip, MemOperand(bitmap_scratch, MemoryChunk::kHeaderSize + kPointerSize));
  andi(r0, ip, Operand(1));
  b(second_bit == 1 ? ne : eq, has_color, cr0);
  bind(&other_color);
}


// Detect some, but not all, common pointer-free objects.  This is used by the
// incremental write barrier which doesn't care about oddballs (they are always
// marked black immediately so this code is not hit).
void MacroAssembler::JumpIfDataObject(Register value,
                                      Register scratch,
                                      Label* not_data_object) {
  Label is_data_object;
  lwz(scratch, FieldMemOperand(value, HeapObject::kMapOffset));
  CompareRoot(scratch, Heap::kHeapNumberMapRootIndex);
  beq(&is_data_object);
  ASSERT(kIsIndirectStringTag == 1 && kIsIndirectStringMask == 1);
  ASSERT(kNotStringTag == 0x80 && kIsNotStringMask == 0x80);
  // If it's a string and it's not a cons string then it's an object containing
  // no GC pointers.
  lbz(scratch, FieldMemOperand(scratch, Map::kInstanceTypeOffset));
  STATIC_ASSERT((kIsIndirectStringMask | kIsNotStringMask) == 0x81);
  andi(scratch, scratch, Operand(kIsIndirectStringMask | kIsNotStringMask));
  bne(not_data_object, cr0);
  bind(&is_data_object);
}


void MacroAssembler::GetMarkBits(Register addr_reg,
                                 Register bitmap_reg,
                                 Register mask_reg) {
  ASSERT(!AreAliased(addr_reg, bitmap_reg, mask_reg, no_reg));
  ASSERT((~Page::kPageAlignmentMask & 0xffff) == 0);
  lis(r0, Operand((~Page::kPageAlignmentMask >> 16)));
  and_(bitmap_reg, addr_reg, r0);
  rlwinm(mask_reg, addr_reg, 32-kPointerSizeLog2,
         32-Bitmap::kBitsPerCellLog2, 31);
  const int kLowBits = kPointerSizeLog2 + Bitmap::kBitsPerCellLog2;
  rlwinm(ip, addr_reg, 32-kLowBits, 32-(kPageSizeBits - kLowBits), 31);
  slwi(ip, ip, Operand(kPointerSizeLog2));
  add(bitmap_reg, bitmap_reg, ip);
  li(ip, Operand(1));
  slw(mask_reg, ip, mask_reg);
}


void MacroAssembler::EnsureNotWhite(
    Register value,
    Register bitmap_scratch,
    Register mask_scratch,
    Register load_scratch,
    Label* value_is_white_and_not_data) {
  ASSERT(!AreAliased(value, bitmap_scratch, mask_scratch, ip));
  GetMarkBits(value, bitmap_scratch, mask_scratch);

  // If the value is black or grey we don't need to do anything.
  ASSERT(strcmp(Marking::kWhiteBitPattern, "00") == 0);
  ASSERT(strcmp(Marking::kBlackBitPattern, "10") == 0);
  ASSERT(strcmp(Marking::kGreyBitPattern, "11") == 0);
  ASSERT(strcmp(Marking::kImpossibleBitPattern, "01") == 0);

  Label done;

  // Since both black and grey have a 1 in the first position and white does
  // not have a 1 there we only need to check one bit.
  lwz(load_scratch, MemOperand(bitmap_scratch, MemoryChunk::kHeaderSize));
  and_(r0, mask_scratch, load_scratch, SetRC);
  bne(&done, cr0);

  if (emit_debug_code()) {
    // Check for impossible bit pattern.
    Label ok;
    // LSL may overflow, making the check conservative.
    slwi(r0, mask_scratch, Operand(1));
    and_(r0, load_scratch, r0, SetRC);
    beq(&ok, cr0);
    stop("Impossible marking bit pattern");
    bind(&ok);
  }

  // Value is white.  We check whether it is data that doesn't need scanning.
  // Currently only checks for HeapNumber and non-cons strings.
  Register map = load_scratch;  // Holds map while checking type.
  Register length = load_scratch;  // Holds length of object after testing type.
  Label is_data_object, maybe_string_object, is_string_object, is_encoded;

  // Check for heap-number
  lwz(map, FieldMemOperand(value, HeapObject::kMapOffset));
  CompareRoot(map, Heap::kHeapNumberMapRootIndex);
  bne(&maybe_string_object);
  li(length, Operand(HeapNumber::kSize));
  b(&is_data_object);
  bind(&maybe_string_object);

  // Check for strings.
  ASSERT(kIsIndirectStringTag == 1 && kIsIndirectStringMask == 1);
  ASSERT(kNotStringTag == 0x80 && kIsNotStringMask == 0x80);
  // If it's a string and it's not a cons string then it's an object containing
  // no GC pointers.
  Register instance_type = load_scratch;
  lbz(instance_type, FieldMemOperand(map, Map::kInstanceTypeOffset));
  andi(r0, instance_type, Operand(kIsIndirectStringMask | kIsNotStringMask));
  bne(value_is_white_and_not_data, cr0);
  // It's a non-indirect (non-cons and non-slice) string.
  // If it's external, the length is just ExternalString::kSize.
  // Otherwise it's String::kHeaderSize + string->length() * (1 or 2).
  // External strings are the only ones with the kExternalStringTag bit
  // set.
  ASSERT_EQ(0, kSeqStringTag & kExternalStringTag);
  ASSERT_EQ(0, kConsStringTag & kExternalStringTag);
  andi(r0, instance_type, Operand(kExternalStringTag));
  beq(&is_string_object, cr0);
  li(length, Operand(ExternalString::kSize));
  b(&is_data_object);
  bind(&is_string_object);

  // Sequential string, either ASCII or UC16.
  // For ASCII (char-size of 1) we shift the smi tag away to get the length.
  // For UC16 (char-size of 2) we just leave the smi tag in place, thereby
  // getting the length multiplied by 2.
  ASSERT(kAsciiStringTag == 4 && kStringEncodingMask == 4);
  ASSERT(kSmiTag == 0 && kSmiTagSize == 1);
  lwz(ip, FieldMemOperand(value, String::kLengthOffset));
  andi(r0, instance_type, Operand(kStringEncodingMask));
  beq(&is_encoded, cr0);
  slwi(ip, ip, Operand(1));
  bind(&is_encoded);
  addi(length, ip, Operand(SeqString::kHeaderSize + kObjectAlignmentMask));
  li(r0, Operand(~kObjectAlignmentMask));
  and_(length, length, r0);

  bind(&is_data_object);
  // Value is a data object, and it is white.  Mark it black.  Since we know
  // that the object is white we can make it black by flipping one bit.
  lwz(ip, MemOperand(bitmap_scratch, MemoryChunk::kHeaderSize));
  orx(ip, ip, mask_scratch);
  stw(ip, MemOperand(bitmap_scratch, MemoryChunk::kHeaderSize));

  mov(ip, Operand(~Page::kPageAlignmentMask));
  and_(bitmap_scratch, bitmap_scratch, ip);
  lwz(ip, MemOperand(bitmap_scratch, MemoryChunk::kLiveBytesOffset));
  add(ip, ip, length);
  stw(ip, MemOperand(bitmap_scratch, MemoryChunk::kLiveBytesOffset));

  bind(&done);
}


void MacroAssembler::ClampUint8(Register output_reg, Register input_reg) {
  Usat(output_reg, 8, Operand(input_reg));
}

void MacroAssembler::ClampDoubleToUint8(Register result_reg,
                                        DoubleRegister input_reg,

                                        DoubleRegister temp_double_reg) {
#ifdef PENGUIN_CLEANUP
  Label above_zero;
  Label done;
  Label in_bounds;

  Vmov(temp_double_reg, 0.0);
  VFPCompareAndSetFlags(input_reg, temp_double_reg);
  b(gt, &above_zero);

  // Double value is less than zero, NaN or Inf, return 0.
  mov(result_reg, Operand(0));
  b(al, &done);

  // Double value is >= 255, return 255.
  bind(&above_zero);
  Vmov(temp_double_reg, 255.0, result_reg);
  VFPCompareAndSetFlags(input_reg, temp_double_reg);
  b(le, &in_bounds);
  mov(result_reg, Operand(255));
  b(al, &done);

  // In 0-255 range, round and truncate.
  bind(&in_bounds);
  // Save FPSCR.
  vmrs(ip);
  // Set rounding mode to round to the nearest integer by clearing bits[23:22].
  bic(result_reg, ip, Operand(kVFPRoundingModeMask));
  vmsr(result_reg);
  vcvt_s32_f64(input_reg.low(), input_reg, kFPSCRRounding);
  vmov(result_reg, input_reg.low());
  // Restore FPSCR.
  vmsr(ip);
  bind(&done);
#else
  PPCPORT_UNIMPLEMENTED();
  fake_asm(fMASM7);
  ASSERT(false);
#endif
}

void MacroAssembler::LoadInstanceDescriptors(Register map,
                                             Register descriptors,
                                             Register scratch) {
  Register temp = descriptors;
  lwz(temp, FieldMemOperand(map, Map::kTransitionsOrBackPointerOffset));

  Label ok, fail, load_from_back_pointer;
  CheckMap(temp,
           scratch,
           isolate()->factory()->fixed_array_map(),
           &fail,
           DONT_DO_SMI_CHECK);
  lwz(temp, FieldMemOperand(temp, TransitionArray::kDescriptorsPointerOffset));
  lwz(descriptors, FieldMemOperand(temp, JSGlobalPropertyCell::kValueOffset));
  jmp(&ok);

  bind(&fail);
  CompareRoot(temp, Heap::kUndefinedValueRootIndex);
  bne(&load_from_back_pointer);
  mov(descriptors, Operand(FACTORY->empty_descriptor_array()));
  jmp(&ok);

  bind(&load_from_back_pointer);
  lwz(temp, FieldMemOperand(temp, Map::kTransitionsOrBackPointerOffset));
  lwz(temp, FieldMemOperand(temp, TransitionArray::kDescriptorsPointerOffset));
  lwz(descriptors, FieldMemOperand(temp, JSGlobalPropertyCell::kValueOffset));

  bind(&ok);
}


void MacroAssembler::NumberOfOwnDescriptors(Register dst, Register map) {
  lwz(dst, FieldMemOperand(map, Map::kBitField3Offset));
  DecodeField<Map::NumberOfOwnDescriptorsBits>(dst);
}


void MacroAssembler::EnumLength(Register dst, Register map) {
  STATIC_ASSERT(Map::EnumLengthBits::kShift == 0);
  lwz(dst, FieldMemOperand(map, Map::kBitField3Offset));
  mov(r0, Operand(Smi::FromInt(Map::EnumLengthBits::kMask)));
  and_(dst, dst, r0);
}


void MacroAssembler::CheckEnumCache(Register null_value, Label* call_runtime) {
  Register  empty_fixed_array_value = r9;
  LoadRoot(empty_fixed_array_value, Heap::kEmptyFixedArrayRootIndex);
  Label next, start;
  mr(r5, r3);

  // Check if the enum length field is properly initialized, indicating that
  // there is an enum cache.
  lwz(r4, FieldMemOperand(r5, HeapObject::kMapOffset));

  EnumLength(r6, r4);
  cmpi(r6, Operand(Smi::FromInt(Map::kInvalidEnumCache)));
  beq(call_runtime);

  jmp(&start);

  bind(&next);
  lwz(r4, FieldMemOperand(r5, HeapObject::kMapOffset));

  // For all objects but the receiver, check that the cache is empty.
  EnumLength(r6, r4);
  cmpi(r6, Operand(Smi::FromInt(0)));
  bne(call_runtime);

  bind(&start);

  // Check that there are no elements. Register r5 contains the current JS
  // object we've reached through the prototype chain.
  lwz(r5, FieldMemOperand(r5, JSObject::kElementsOffset));
  cmp(r5, empty_fixed_array_value);
  bne(call_runtime);

  lwz(r5, FieldMemOperand(r4, Map::kPrototypeOffset));
  cmp(r5, null_value);
  bne(&next);
}


////////////////////////////////////////////////////////////////////////////////
//
// New MacroAssembler Interfaces added for PPC
//
////////////////////////////////////////////////////////////////////////////////
void MacroAssembler::LoadSignedImmediate(Register dst, int value) {
  if (is_int16(value)) {
    li(dst, Operand(value));
  } else {
    int hi_word = static_cast<int>(value) >> 16;
    if ((hi_word << 16) == value) {
      lis(dst, Operand(hi_word));
    } else {
      mov(dst, Operand(value));
    }
  }
}

void MacroAssembler::Add(Register dst, Register src,
                         uint32_t value, Register scratch) {
  if (is_int16(value)) {
    addi(dst, dst, Operand(value));
  } else {
    mov(scratch, Operand(value));
    add(dst, dst, scratch);
  }
}

void MacroAssembler::Cmpi(Register src1, const Operand& src2, Register scratch,
                          CRegister cr) {
  int value = src2.immediate();
  if (is_int16(value)) {
    cmpi(src1, src2, cr);
  } else {
    mov(scratch, src2);
    cmp(src1, scratch, cr);
  }
}

void MacroAssembler::Cmpli(Register src1, const Operand& src2, Register scratch,
                           CRegister cr) {
  int value = src2.immediate();
  if (is_uint16(value)) {
    cmpli(src1, src2, cr);
  } else {
    mov(scratch, src2);
    cmpl(src1, scratch, cr);
  }
}

// Variable length depending on whether offset fits into immediate field
// MemOperand currently only supports d-form
void MacroAssembler::LoadWord(Register dst, const MemOperand& mem,
                              Register scratch, bool updateForm) {
  Register base = mem.ra();
  int offset = mem.offset();

  bool use_dform = true;
  if (!is_int16(offset)) {
    use_dform = false;
    LoadSignedImmediate(scratch, offset);
  }

  if (!updateForm) {
    if (use_dform) {
      lwz(dst, mem);
    } else {
      lwzx(dst, base, scratch);
    }
  } else {
    if (use_dform) {
      lwzu(dst, mem);
    } else {
      lwzux(dst, base, scratch);
    }
  }
}

// Variable length depending on whether offset fits into immediate field
// MemOperand current only supports d-form
void MacroAssembler::StoreWord(Register src, const MemOperand& mem,
                               Register scratch, bool updateForm) {
  Register base = mem.ra();
  int offset = mem.offset();

  bool use_dform = true;
  if (!is_int16(offset)) {
    use_dform = false;
    LoadSignedImmediate(scratch, offset);
  }

  if (!updateForm) {
    if (use_dform) {
      stw(src, mem);
    } else {
      stwx(src, base, scratch);
    }
  } else {
    if (use_dform) {
      stwu(src, mem);
    } else {
      stwux(src, base, scratch);
    }
  }
}

#ifdef DEBUG
bool AreAliased(Register reg1,
                Register reg2,
                Register reg3,
                Register reg4,
                Register reg5,
                Register reg6) {
  int n_of_valid_regs = reg1.is_valid() + reg2.is_valid() +
    reg3.is_valid() + reg4.is_valid() + reg5.is_valid() + reg6.is_valid();

  RegList regs = 0;
  if (reg1.is_valid()) regs |= reg1.bit();
  if (reg2.is_valid()) regs |= reg2.bit();
  if (reg3.is_valid()) regs |= reg3.bit();
  if (reg4.is_valid()) regs |= reg4.bit();
  if (reg5.is_valid()) regs |= reg5.bit();
  if (reg6.is_valid()) regs |= reg6.bit();
  int n_of_non_aliasing_regs = NumRegs(regs);

  return n_of_valid_regs != n_of_non_aliasing_regs;
}
#endif


CodePatcher::CodePatcher(byte* address, int instructions)
    : address_(address),
      instructions_(instructions),
      size_(instructions * Assembler::kInstrSize),
      masm_(NULL, address, size_ + Assembler::kGap) {
  // Create a new macro assembler pointing to the address of the code to patch.
  // The size is adjusted with kGap on order for the assembler to generate size
  // bytes of instructions without failing with buffer size constraints.
  ASSERT(masm_.reloc_info_writer.pos() == address_ + size_ + Assembler::kGap);
}


CodePatcher::~CodePatcher() {
  // Indicate that code has changed.
  CPU::FlushICache(address_, size_);

  // Check that the code was patched as expected.
  ASSERT(masm_.pc_ == address_ + size_);
  ASSERT(masm_.reloc_info_writer.pos() == address_ + size_ + Assembler::kGap);
}


void CodePatcher::Emit(Instr instr) {
  masm()->emit(instr);
}


void CodePatcher::Emit(Address addr) {
  masm()->emit(reinterpret_cast<Instr>(addr));
}


void CodePatcher::EmitCondition(Condition cond) {
  Instr instr = Assembler::instr_at(masm_.pc_);
  switch (cond) {
    case eq:
      instr = (instr & ~kCondMask) | BT;
      break;
    case ne:
      instr = (instr & ~kCondMask) | BF;
      break;
    default:
      UNIMPLEMENTED();
  }
  masm_.emit(instr);
}


} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_PPC
