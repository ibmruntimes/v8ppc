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
#include <assert.h>  // For assert

#include "v8.h"

#if V8_TARGET_ARCH_PPC

#include "bootstrapper.h"
#include "codegen.h"
#include "cpu-profiler.h"
#include "debug.h"
#include "isolate-inl.h"
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
  bctr();
}


void MacroAssembler::Jump(intptr_t target, RelocInfo::Mode rmode,
                          Condition cond, CRegister cr) {
  Label skip;

  if (cond != al) b(NegateCondition(cond), &skip, cr);

  ASSERT(rmode == RelocInfo::CODE_TARGET ||
         rmode == RelocInfo::RUNTIME_ENTRY);

  mov(r0, Operand(target, rmode));
  mtctr(r0);
  bctr();

  bind(&skip);
  //  mov(pc, Operand(target, rmode), LeaveCC, cond);
}


void MacroAssembler::Jump(Address target, RelocInfo::Mode rmode,
                          Condition cond, CRegister cr) {
  ASSERT(!RelocInfo::IsCodeTarget(rmode));
  Jump(reinterpret_cast<intptr_t>(target), rmode, cond, cr);
}


void MacroAssembler::Jump(Handle<Code> code, RelocInfo::Mode rmode,
                          Condition cond) {
  ASSERT(RelocInfo::IsCodeTarget(rmode));
  // 'code' is always generated ppc code, never THUMB code
  AllowDeferredHandleDereference embedding_raw_address;
  Jump(reinterpret_cast<intptr_t>(code.location()), rmode, cond);
}


int MacroAssembler::CallSize(Register target, Condition cond) {
  return 2 * kInstrSize;
}


void MacroAssembler::Call(Register target, Condition cond) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
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

#if 0  // Account for variable length Assembler::mov sequence.
  intptr_t value = reinterpret_cast<intptr_t>(target);
  if (is_int16(value) || (((value >> 16) << 16) == value)) {
    movSize = 1;
  } else {
    movSize = 2;
  }
#else

#if V8_TARGET_ARCH_PPC64
  movSize = 5;
#else
  movSize = 2;
#endif

#endif

  size = (2 + movSize) * kInstrSize;

  return size;
}


int MacroAssembler::CallSizeNotPredictableCodeSize(
    Address target, RelocInfo::Mode rmode, Condition cond) {
  int size;
  int movSize;

#if 0  // Account for variable length Assembler::mov sequence.
  intptr_t value = reinterpret_cast<intptr_t>(target);
  if (is_int16(value) || (((value >> 16) << 16) == value)) {
    movSize = 1;
  } else {
    movSize = 2;
  }
#else

#if V8_TARGET_ARCH_PPC64
  movSize = 5;
#else
  movSize = 2;
#endif

#endif

  size = (2 + movSize) * kInstrSize;

  return size;
}


void MacroAssembler::Call(Address target,
                          RelocInfo::Mode rmode,
                          Condition cond) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  ASSERT(cond == al);
  Label start;
  bind(&start);

  // Statement positions are expected to be recorded when the target
  // address is loaded.
  positions_recorder()->WriteRecordedPositions();

  // This can likely be optimized to make use of bc() with 24bit relative
  //
  // RecordRelocInfo(x.rmode_, x.imm_);
  // bc( BA, .... offset, LKset);
  //

  mov(ip, Operand(reinterpret_cast<intptr_t>(target), rmode));
  mtlr(ip);
  bclr(BA, SetLK);

#if V8_TARGET_ARCH_PPC64
  ASSERT(kCallTargetAddressOffset == 7 * kInstrSize);
#else
  ASSERT(kCallTargetAddressOffset == 4 * kInstrSize);
#endif
  ASSERT_EQ(CallSize(target, rmode, cond), SizeOfCodeGeneratedSince(&start));
}


int MacroAssembler::CallSize(Handle<Code> code,
                             RelocInfo::Mode rmode,
                             TypeFeedbackId ast_id,
                             Condition cond) {
  AllowDeferredHandleDereference using_raw_address;
  return CallSize(reinterpret_cast<Address>(code.location()), rmode, cond);
}


void MacroAssembler::Call(Handle<Code> code,
                          RelocInfo::Mode rmode,
                          TypeFeedbackId ast_id,
                          Condition cond) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Label start;
  bind(&start);
  ASSERT(RelocInfo::IsCodeTarget(rmode));
  if (rmode == RelocInfo::CODE_TARGET && !ast_id.IsNone()) {
    SetRecordedAstId(ast_id);
    rmode = RelocInfo::CODE_TARGET_WITH_ID;
  }
  AllowDeferredHandleDereference using_raw_address;
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


void MacroAssembler::Call(Label* target) {
  b(target, SetLK);
}


void MacroAssembler::Push(Handle<Object> handle) {
  mov(ip, Operand(handle));
  push(ip);
}


void MacroAssembler::Move(Register dst, Handle<Object> value) {
  AllowDeferredHandleDereference smi_check;
  if (value->IsSmi()) {
    LoadSmiLiteral(dst, reinterpret_cast<Smi *>(*value));
  } else {
    ASSERT(value->IsHeapObject());
    if (isolate()->heap()->InNewSpace(*value)) {
      Handle<Cell> cell = isolate()->factory()->NewCell(value);
      mov(dst, Operand(cell));
      LoadP(dst, FieldMemOperand(dst, Cell::kValueOffset));
    } else {
      mov(dst, Operand(value));
    }
  }
}


void MacroAssembler::Move(Register dst, Register src, Condition cond) {
  ASSERT(cond == al);
  if (!dst.is(src)) {
    mr(dst, src);
  }
}


void MacroAssembler::Move(DoubleRegister dst, DoubleRegister src) {
  if (!dst.is(src)) {
    fmr(dst, src);
  }
}


void MacroAssembler::MultiPush(RegList regs) {
  int16_t num_to_push = NumberOfBitsSet(regs);
  int16_t stack_offset = num_to_push * kPointerSize;

  subi(sp, sp, Operand(stack_offset));
  for (int16_t i = kNumRegisters - 1; i >= 0; i--) {
    if ((regs & (1 << i)) != 0) {
      stack_offset -= kPointerSize;
      StoreP(ToRegister(i), MemOperand(sp, stack_offset));
    }
  }
}


void MacroAssembler::MultiPop(RegList regs) {
  int16_t stack_offset = 0;

  for (int16_t i = 0; i < kNumRegisters; i++) {
    if ((regs & (1 << i)) != 0) {
      LoadP(ToRegister(i), MemOperand(sp, stack_offset));
      stack_offset += kPointerSize;
    }
  }
  addi(sp, sp, Operand(stack_offset));
}


void MacroAssembler::LoadRoot(Register destination,
                              Heap::RootListIndex index,
                              Condition cond) {
  ASSERT(cond == al);
  LoadP(destination, MemOperand(kRootRegister,
                                index << kPointerSizeLog2), r0);
}


void MacroAssembler::StoreRoot(Register source,
                               Heap::RootListIndex index,
                               Condition cond) {
  ASSERT(cond == al);
  StoreP(source, MemOperand(kRootRegister, index << kPointerSizeLog2), r0);
}


void MacroAssembler::InNewSpace(Register object,
                                Register scratch,
                                Condition cond,
                                Label* branch) {
  // N.B. scratch may be same register as object
  ASSERT(cond == eq || cond == ne);
  mov(r0, Operand(ExternalReference::new_space_mask(isolate())));
  and_(scratch, object, r0);
  mov(r0, Operand(ExternalReference::new_space_start(isolate())));
  cmp(scratch, r0);
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

  Add(dst, object, offset - kHeapObjectTag, r0);
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
    mov(value, Operand(BitCast<intptr_t>(kZapValue + 4)));
    mov(dst, Operand(BitCast<intptr_t>(kZapValue + 8)));
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
  if (emit_debug_code()) {
    LoadP(ip, MemOperand(address));
    cmp(ip, value);
    Check(eq, kWrongAddressOrValuePassedToRecordWrite);
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
    mov(address, Operand(BitCast<intptr_t>(kZapValue + 12)));
    mov(value, Operand(BitCast<intptr_t>(kZapValue + 16)));
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
  LoadP(scratch, MemOperand(ip));
  // Store pointer to buffer and increment buffer top.
  StoreP(address, MemOperand(scratch));
  addi(scratch, scratch, Operand(kPointerSize));
  // Write back new top of buffer.
  StoreP(scratch, MemOperand(ip));
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
  // Safepoints expect a block of kNumSafepointRegisters values on the
  // stack, so adjust the stack for unsaved registers.
  const int num_unsaved = kNumSafepointRegisters - kNumSafepointSavedRegisters;
  ASSERT(num_unsaved >= 0);
  if (num_unsaved > 0) {
    subi(sp, sp, Operand(num_unsaved * kPointerSize));
  }
  MultiPush(kSafepointSavedRegisters);
}


void MacroAssembler::PopSafepointRegisters() {
  const int num_unsaved = kNumSafepointRegisters - kNumSafepointSavedRegisters;
  MultiPop(kSafepointSavedRegisters);
  if (num_unsaved > 0) {
    addi(sp, sp, Operand(num_unsaved * kPointerSize));
  }
}


void MacroAssembler::PushSafepointRegistersAndDoubles() {
  // Number of d-regs not known at snapshot time.
  ASSERT(!Serializer::enabled());
  PushSafepointRegisters();
  subi(sp, sp, Operand(DoubleRegister::NumAllocatableRegisters() *
                       kDoubleSize));
  for (int i = 0; i < DoubleRegister::NumAllocatableRegisters(); i++) {
    stfd(DoubleRegister::FromAllocationIndex(i),
         MemOperand(sp, i * kDoubleSize));
  }
}


void MacroAssembler::PopSafepointRegistersAndDoubles() {
  // Number of d-regs not known at snapshot time.
  ASSERT(!Serializer::enabled());
  for (int i = 0; i < DoubleRegister::NumAllocatableRegisters(); i++) {
    lfd(DoubleRegister::FromAllocationIndex(i),
        MemOperand(sp, i * kDoubleSize));
  }
  addi(sp, sp, Operand(DoubleRegister::NumAllocatableRegisters() *
                       kDoubleSize));
  PopSafepointRegisters();
}


void MacroAssembler::StoreToSafepointRegisterSlot(Register src, Register dst) {
  StoreP(src, SafepointRegisterSlot(dst));
}


void MacroAssembler::LoadFromSafepointRegisterSlot(Register dst, Register src) {
  LoadP(dst, SafepointRegisterSlot(src));
}


int MacroAssembler::SafepointRegisterStackIndex(int reg_code) {
  // The registers are pushed starting with the highest encoding,
  // which means that lowest encodings are closest to the stack pointer.
  RegList regs = kSafepointSavedRegisters;
  int index = 0;

  ASSERT(reg_code >= 0 && reg_code < kNumRegisters);

  for (int16_t i = 0; i < reg_code; i++) {
    if ((regs & (1 << i)) != 0) {
      index++;
    }
  }

  return index;
}


MemOperand MacroAssembler::SafepointRegisterSlot(Register reg) {
  return MemOperand(sp, SafepointRegisterStackIndex(reg.code()) * kPointerSize);
}


MemOperand MacroAssembler::SafepointRegistersAndDoublesSlot(Register reg) {
  // General purpose registers are pushed last on the stack.
  int doubles_size = DoubleRegister::NumAllocatableRegisters() * kDoubleSize;
  int register_offset = SafepointRegisterStackIndex(reg.code()) * kPointerSize;
  return MemOperand(sp, doubles_size + register_offset);
}


void MacroAssembler::CanonicalizeNaN(const DoubleRegister dst,
                                     const DoubleRegister src) {
  Label done;

  // Test for NaN
  fcmpu(src, src);

  if (dst.is(src)) {
    bordered(&done);
  } else {
    Label is_nan;
    bunordered(&is_nan);
    fmr(dst, src);
    b(&done);
    bind(&is_nan);
  }

  // Replace with canonical NaN.
  uint64_t nan_int64 = BitCast<uint64_t>(
    FixedDoubleArray::canonical_not_the_hole_nan_as_double());
#if V8_TARGET_ARCH_PPC64
  mov(r0, Operand(nan_int64));
  stdu(r0, MemOperand(sp, -kDoubleSize));
#else
  subi(sp, sp, Operand(kDoubleSize));
  mov(r0, Operand(static_cast<intptr_t>(nan_int64)));
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
  stw(r0, MemOperand(sp, 0));
#else
  stw(r0, MemOperand(sp, 4));
#endif
  mov(r0, Operand(static_cast<intptr_t>(nan_int64 >> 32)));
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
  stw(r0, MemOperand(sp, 4));
#else
  stw(r0, MemOperand(sp, 0));
#endif
#endif
  lfd(dst, MemOperand(sp));
  addi(sp, sp, Operand(kDoubleSize));

  bind(&done);
}


void MacroAssembler::LoadNumber(Register object,
                                DoubleRegister dst,
                                Register heap_number_map,
                                Register scratch,
                                Label* not_number) {
  Label is_smi, done;

  // Smi-check
  UntagAndJumpIfSmi(scratch, object, &is_smi);
  // Heap number check
  JumpIfNotHeapNumber(object, heap_number_map, scratch, not_number);

  // Handle loading a double from a heap number
  // Load the double from tagged HeapNumber to double register.
  lfd(dst, FieldMemOperand(object, HeapNumber::kValueOffset));
  b(&done);

  // Handle loading a double from a smi.
  bind(&is_smi);
  ConvertIntToDouble(scratch, dst);

  bind(&done);
}


void MacroAssembler::LoadNumberAsInt32Double(Register object,
                                             DoubleRegister double_dst,
                                             DoubleRegister double_scratch,
                                             Register heap_number_map,
                                             Register scratch1,
                                             Register scratch2,
                                             Label* not_int32) {
  ASSERT(!scratch1.is(object) && !scratch2.is(object));
  ASSERT(!scratch1.is(scratch2));
  ASSERT(!heap_number_map.is(object) &&
         !heap_number_map.is(scratch1) &&
         !heap_number_map.is(scratch2));

  Label done, obj_is_not_smi;

  UntagAndJumpIfNotSmi(scratch1, object, &obj_is_not_smi);
  ConvertIntToDouble(scratch1, double_dst);
  b(&done);

  bind(&obj_is_not_smi);
  JumpIfNotHeapNumber(object, heap_number_map, scratch1, not_int32);

  // Load the double value.
  lfd(double_dst, FieldMemOperand(object, HeapNumber::kValueOffset));

  TestDoubleIsInt32(double_dst, scratch1, scratch2, double_scratch);
  // Jump to not_int32 if the operation did not succeed.
  bne(not_int32);

  bind(&done);
}


void MacroAssembler::LoadNumberAsInt32(Register object,
                                       Register dst,
                                       Register heap_number_map,
                                       Register scratch,
                                       DoubleRegister double_scratch0,
                                       DoubleRegister double_scratch1,
                                       Label* not_int32) {
  ASSERT(!dst.is(object));
  ASSERT(!scratch.is(object));

  Label done, maybe_undefined;

  UntagAndJumpIfSmi(dst, object, &done);

  JumpIfNotHeapNumber(object, heap_number_map, scratch, &maybe_undefined);

  // Object is a heap number.
  // Convert the floating point value to a 32-bit integer.
  // Load the double value.
  lfd(double_scratch0, FieldMemOperand(object, HeapNumber::kValueOffset));

  TryDoubleToInt32Exact(dst, double_scratch0, scratch, double_scratch1);
  // Jump to not_int32 if the operation did not succeed.
  bne(not_int32);
  b(&done);

  bind(&maybe_undefined);
  CompareRoot(object, Heap::kUndefinedValueRootIndex);
  bne(not_int32);
  // |undefined| is truncated to 0.
  LoadSmiLiteral(dst, Smi::FromInt(0));
  // Fall through.

  bind(&done);
}


void MacroAssembler::ConvertIntToDouble(Register src,
                                        DoubleRegister double_dst) {
  ASSERT(!src.is(r0));

  subi(sp, sp, Operand(8));  // reserve one temporary double on the stack

  // sign-extend src to 64-bit and store it to temp double on the stack
#if V8_TARGET_ARCH_PPC64
  extsw(r0, src);
  std(r0, MemOperand(sp, 0));
#else
  srawi(r0, src, 31);
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
  stw(r0, MemOperand(sp, 4));
  stw(src, MemOperand(sp, 0));
#else
  stw(r0, MemOperand(sp, 0));
  stw(src, MemOperand(sp, 4));
#endif
#endif

  // load into FPR
  lfd(double_dst, MemOperand(sp, 0));

  addi(sp, sp, Operand(8));  // restore stack

  // convert to double
  fcfid(double_dst, double_dst);
}


void MacroAssembler::ConvertUnsignedIntToDouble(Register src,
                                                DoubleRegister double_dst) {
  ASSERT(!src.is(r0));

  subi(sp, sp, Operand(8));  // reserve one temporary double on the stack

  // zero-extend src to 64-bit and store it to temp double on the stack
#if V8_TARGET_ARCH_PPC64
  clrldi(r0, src, Operand(32));
  std(r0, MemOperand(sp, 0));
#else
  li(r0, Operand::Zero());
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
  stw(r0, MemOperand(sp, 4));
  stw(src, MemOperand(sp, 0));
#else
  stw(r0, MemOperand(sp, 0));
  stw(src, MemOperand(sp, 4));
#endif
#endif

  // load into FPR
  lfd(double_dst, MemOperand(sp, 0));

  addi(sp, sp, Operand(8));  // restore stack

  // convert to double
  fcfid(double_dst, double_dst);
}


void MacroAssembler::ConvertIntToFloat(const DoubleRegister dst,
                                       const Register src,
                                       const Register int_scratch) {
  subi(sp, sp, Operand(8));  // reserve one temporary double on the stack

  // sign-extend src to 64-bit and store it to temp double on the stack
#if V8_TARGET_ARCH_PPC64
  extsw(int_scratch, src);
  std(int_scratch, MemOperand(sp, 0));
#else
  srawi(int_scratch, src, 31);
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
  stw(int_scratch, MemOperand(sp, 4));
  stw(src, MemOperand(sp, 0));
#else
  stw(int_scratch, MemOperand(sp, 0));
  stw(src, MemOperand(sp, 4));
#endif
#endif

  // load sign-extended src into FPR
  lfd(dst, MemOperand(sp, 0));

  addi(sp, sp, Operand(8));  // restore stack

  fcfid(dst, dst);
  frsp(dst, dst);
}


void MacroAssembler::ConvertDoubleToInt64(const DoubleRegister double_input,
                                          const Register dst,
#if !V8_TARGET_ARCH_PPC64
                                          const Register dst_hi,
#endif
                                          const DoubleRegister double_dst,
                                          FPRoundingMode rounding_mode) {
  if (rounding_mode == kRoundToZero) {
    fctidz(double_dst, double_input);
  } else {
    SetRoundingMode(rounding_mode);
    fctid(double_dst, double_input);
    ResetRoundingMode();
  }

  stfdu(double_dst, MemOperand(sp, -kDoubleSize));
#if V8_TARGET_ARCH_PPC64
  ld(dst, MemOperand(sp, 0));
#else
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
  lwz(dst_hi, MemOperand(sp, 4));
  lwz(dst, MemOperand(sp, 0));
#else
  lwz(dst_hi, MemOperand(sp, 0));
  lwz(dst, MemOperand(sp, 4));
#endif
#endif
  addi(sp, sp, Operand(kDoubleSize));
}


void MacroAssembler::Prologue(PrologueFrameMode frame_mode) {
  if (frame_mode == BUILD_STUB_FRAME) {
    mflr(r0);
    Push(r0, fp, cp);
    Push(Smi::FromInt(StackFrame::STUB));
    // Adjust FP to point to saved FP.
    addi(fp, sp, Operand(2 * kPointerSize));
  } else {
    PredictableCodeSizeScope predictible_code_size_scope(
      this, kNoCodeAgeSequenceLength * Assembler::kInstrSize);
    Assembler::BlockTrampolinePoolScope block_trampoline_pool(this);
    // The following instructions must remain together and unmodified
    // for code aging to work properly.
    if (isolate()->IsCodePreAgingActive()) {
      // Pre-age the code.
      // This matches the code found in PatchPlatformCodeAge()
      Code* stub = Code::GetPreAgedCodeAgeStub(isolate());
      intptr_t target = reinterpret_cast<intptr_t>(stub->instruction_start());
      mflr(ip);
      mov(r3, Operand(target));
      Call(r3);
#if !V8_TARGET_ARCH_PPC64
      nop();
#endif
    } else {
      // This matches the code found in GetNoCodeAgeSequence()
      mflr(r0);
      Push(r0, fp, cp, r4);
      // Adjust fp to point to saved fp.
      addi(fp, sp, Operand(2 * kPointerSize));

#if V8_TARGET_ARCH_PPC64
      // With 64bit we need a couple of nop() instructions to pad
      // out to 8 instructions total to ensure we have enough
      // space to patch it later in Code::PatchPlatformCodeAge
      nop();
      nop();
#endif
    }
  }
}


void MacroAssembler::EnterFrame(StackFrame::Type type) {
  mflr(r0);
  push(r0);
  push(fp);
  push(cp);
  LoadSmiLiteral(r0, Smi::FromInt(type));
  push(r0);
  mov(r0, Operand(CodeObject()));
  push(r0);
  addi(fp, sp, Operand(3 * kPointerSize));  // Adjust FP to point to saved FP
}


void MacroAssembler::LeaveFrame(StackFrame::Type type) {
  // Drop the execution stack down to the frame pointer and restore
  // the caller frame pointer and return address.
  mr(sp, fp);
  LoadP(fp, MemOperand(sp));
  LoadP(r0, MemOperand(sp, kPointerSize));
  mtlr(r0);
  addi(sp, sp, Operand(2*kPointerSize));
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
  ASSERT(stack_space > 0);

  // This is an opportunity to build a frame to wrap
  // all of the pushes that have happened inside of V8
  // since we were called from C code

  // replicate ARM frame - TODO make this more closely follow PPC ABI
  mflr(r0);
  Push(r0, fp);
  mr(fp, sp);
  // Reserve room for saved entry sp and code object.
  subi(sp, sp, Operand(2 * kPointerSize));

  if (emit_debug_code()) {
    li(r8, Operand::Zero());
    StoreP(r8, MemOperand(fp, ExitFrameConstants::kSPOffset));
  }
  mov(r8, Operand(CodeObject()));
  StoreP(r8, MemOperand(fp, ExitFrameConstants::kCodeOffset));

  // Save the frame pointer and the context in top.
  mov(r8, Operand(ExternalReference(Isolate::kCEntryFPAddress, isolate())));
  StoreP(fp, MemOperand(r8));
  mov(r8, Operand(ExternalReference(Isolate::kContextAddress, isolate())));
  StoreP(cp, MemOperand(r8));

  // Optionally save all volatile double registers.
  if (save_doubles) {
    const int kNumRegs = DoubleRegister::kNumVolatileRegisters;
    subi(sp, sp, Operand(kNumRegs * kDoubleSize));
    for (int i = 0; i < kNumRegs; i++) {
      DoubleRegister reg = DoubleRegister::from_code(i);
      stfd(reg, MemOperand(sp, i * kDoubleSize));
    }
    // Note that d0 will be accessible at
    //   fp - 2 * kPointerSize - kNumVolatileRegisters * kDoubleSize,
    // since the sp slot and code slot were pushed after the fp.
  }

  addi(sp, sp, Operand(-stack_space * kPointerSize));

  // Allocate and align the frame preparing for calling the runtime
  // function.
  const int frame_alignment = ActivationFrameAlignment();
  if (frame_alignment > kPointerSize) {
    ASSERT(IsPowerOf2(frame_alignment));
    ClearRightImm(sp, sp, Operand(WhichPowerOf2(frame_alignment)));
  }
  li(r0, Operand::Zero());
  StorePU(r0, MemOperand(sp, -kNumRequiredStackFrameSlots * kPointerSize));

  // Set the exit frame sp value to point just before the return address
  // location.
  addi(r8, sp, Operand((kStackFrameExtraParamSlot + 1) * kPointerSize));
  StoreP(r8, MemOperand(fp, ExitFrameConstants::kSPOffset));
}


void MacroAssembler::InitializeNewString(Register string,
                                         Register length,
                                         Heap::RootListIndex map_index,
                                         Register scratch1,
                                         Register scratch2) {
  SmiTag(scratch1, length);
  LoadRoot(scratch2, map_index);
  StoreP(scratch1, FieldMemOperand(string, String::kLengthOffset), r0);
  li(scratch1, Operand(String::kEmptyHashField));
  StoreP(scratch2, FieldMemOperand(string, HeapObject::kMapOffset), r0);
  StoreP(scratch1, FieldMemOperand(string, String::kHashFieldSlot), r0);
}


int MacroAssembler::ActivationFrameAlignment() {
#if !defined(USE_SIMULATOR)
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
                                    Register argument_count,
                                    bool restore_context) {
  // Optionally restore all double registers.
  if (save_doubles) {
    // Calculate the stack location of the saved doubles and restore them.
    const int kNumRegs = DoubleRegister::kNumVolatileRegisters;
    const int offset = (2 * kPointerSize + kNumRegs * kDoubleSize);
    addi(r6, fp, Operand(-offset));
    for (int i = 0; i < kNumRegs; i++) {
      DoubleRegister reg = DoubleRegister::from_code(i);
      lfd(reg, MemOperand(r6, i * kDoubleSize));
    }
  }

  // Clear top frame.
  li(r6, Operand::Zero());
  mov(ip, Operand(ExternalReference(Isolate::kCEntryFPAddress, isolate())));
  StoreP(r6, MemOperand(ip));

  // Restore current context from top and clear it in debug mode.
  if (restore_context) {
    mov(ip, Operand(ExternalReference(Isolate::kContextAddress, isolate())));
    LoadP(cp, MemOperand(ip));
  }
#ifdef DEBUG
  mov(ip, Operand(ExternalReference(Isolate::kContextAddress, isolate())));
  StoreP(r6, MemOperand(ip));
#endif

  // Tear down the exit frame, pop the arguments, and return.
  mr(sp, fp);
  pop(fp);
  pop(r0);
  mtlr(r0);

  if (argument_count.is_valid()) {
    ShiftLeftImm(argument_count, argument_count, Operand(kPointerSizeLog2));
    add(sp, sp, argument_count);
  }
}


void MacroAssembler::GetCFunctionDoubleResult(const DoubleRegister dst) {
  fmr(dst, d1);
}


void MacroAssembler::SetCallKind(Register dst, CallKind call_kind) {
  // This macro takes the dst register to make the code more readable
  // at the call sites. However, the dst register has to be r8 to
  // follow the calling convention which requires the call type to be
  // in r8.
  ASSERT(dst.is(r8));
  if (call_kind == CALL_AS_FUNCTION) {
    LoadSmiLiteral(dst, Smi::FromInt(1));
  } else {
    LoadSmiLiteral(dst, Smi::FromInt(0));
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

  LoadP(code_reg, FieldMemOperand(r4, JSFunction::kSharedFunctionInfoOffset));
  LoadP(cp, FieldMemOperand(r4, JSFunction::kContextOffset));
  LoadWordArith(expected_reg,
      FieldMemOperand(code_reg,
                      SharedFunctionInfo::kFormalParameterCountOffset));
#if !defined(V8_TARGET_ARCH_PPC64)
  SmiUntag(expected_reg);
#endif
  LoadP(code_reg,
        FieldMemOperand(r4, JSFunction::kCodeEntryOffset));

  ParameterCount expected(expected_reg);
  InvokeCode(code_reg, expected, actual, flag, call_wrapper, call_kind);
}


void MacroAssembler::InvokeFunction(Handle<JSFunction> function,
                                    const ParameterCount& expected,
                                    const ParameterCount& actual,
                                    InvokeFlag flag,
                                    const CallWrapper& call_wrapper,
                                    CallKind call_kind) {
  // You can't call a function without a valid frame.
  ASSERT(flag == JUMP_FUNCTION || has_frame());

  // Get the function and setup the context.
  Move(r4, function);
  LoadP(cp, FieldMemOperand(r4, JSFunction::kContextOffset));

  // We call indirectly through the code field in the function to
  // allow recompilation to take effect without changing any of the
  // call sites.
  LoadP(r6, FieldMemOperand(r4, JSFunction::kCodeEntryOffset));
  InvokeCode(r6, expected, actual, flag, call_wrapper, call_kind);
}


void MacroAssembler::IsObjectJSObjectType(Register heap_object,
                                          Register map,
                                          Register scratch,
                                          Label* fail) {
  LoadP(map, FieldMemOperand(heap_object, HeapObject::kMapOffset));
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

  LoadP(scratch, FieldMemOperand(object, HeapObject::kMapOffset));
  lbz(scratch, FieldMemOperand(scratch, Map::kInstanceTypeOffset));
  andi(r0, scratch, Operand(kIsNotStringMask));
  bne(fail, cr0);
}


void MacroAssembler::IsObjectNameType(Register object,
                                      Register scratch,
                                      Label* fail) {
  LoadP(scratch, FieldMemOperand(object, HeapObject::kMapOffset));
  lbz(scratch, FieldMemOperand(scratch, Map::kInstanceTypeOffset));
  cmpi(scratch, Operand(LAST_NAME_TYPE));
  bgt(fail);
}


#ifdef ENABLE_DEBUGGER_SUPPORT
void MacroAssembler::DebugBreak() {
  li(r3, Operand::Zero());
  mov(r4, Operand(ExternalReference(Runtime::kDebugBreak, isolate())));
  CEntryStub ces(1);
  ASSERT(AllowThisStubCall(&ces));
  Call(ces.GetCode(isolate()), RelocInfo::DEBUG_BREAK);
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
  LoadP(r0, MemOperand(r8));
  StorePU(r0, MemOperand(sp, -StackHandlerConstants::kSize));
  // Set this new handler as the current one.
  StoreP(sp, MemOperand(r8));

  if (kind == StackHandler::JS_ENTRY) {
    li(r8, Operand::Zero());  // NULL frame pointer.
    StoreP(r8, MemOperand(sp, StackHandlerConstants::kFPOffset));
    LoadSmiLiteral(r8, Smi::FromInt(0));    // Indicates no context.
    StoreP(r8, MemOperand(sp, StackHandlerConstants::kContextOffset));
  } else {
    // still not sure if fp is right
    StoreP(fp, MemOperand(sp, StackHandlerConstants::kFPOffset));
    StoreP(cp, MemOperand(sp, StackHandlerConstants::kContextOffset));
  }
  unsigned state =
      StackHandler::IndexField::encode(handler_index) |
      StackHandler::KindField::encode(kind);
  LoadIntLiteral(r8, state);
  StoreP(r8, MemOperand(sp, StackHandlerConstants::kStateOffset));
  mov(r8, Operand(CodeObject()));
  StoreP(r8, MemOperand(sp, StackHandlerConstants::kCodeOffset));
}


void MacroAssembler::PopTryHandler() {
  STATIC_ASSERT(StackHandlerConstants::kNextOffset == 0);
  pop(r4);
  mov(ip, Operand(ExternalReference(Isolate::kHandlerAddress, isolate())));
  addi(sp, sp, Operand(StackHandlerConstants::kSize - kPointerSize));
  StoreP(r4, MemOperand(ip));
}


// PPC - make use of ip as a temporary register
void MacroAssembler::JumpToHandlerEntry() {
  // Compute the handler entry address and jump to it.  The handler table is
  // a fixed array of (smi-tagged) code offsets.
  // r3 = exception, r4 = code object, r5 = state.
  LoadP(r6, FieldMemOperand(r4, Code::kHandlerTableOffset));  // Handler table.
  addi(r6, r6, Operand(FixedArray::kHeaderSize - kHeapObjectTag));
  srwi(r5, r5, Operand(StackHandler::kKindWidth));  // Handler index.
  slwi(ip, r5, Operand(kPointerSizeLog2));
  add(ip, r6, ip);
  LoadP(r5, MemOperand(ip));  // Smi-tagged offset.
  addi(r4, r4, Operand(Code::kHeaderSize - kHeapObjectTag));  // Code start.
  SmiUntag(ip, r5);
  add(r0, r4, ip);
  mtctr(r0);
  bctr();
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
  LoadP(sp, MemOperand(r6));
  // Restore the next handler.
  pop(r5);
  StoreP(r5, MemOperand(r6));

  // Get the code object (r4) and state (r5).  Restore the context and frame
  // pointer.
  pop(r4);
  pop(r5);
  pop(cp);
  pop(fp);

  // If the handler is a JS frame, restore the context to the frame.
  // (kind == ENTRY) == (fp == 0) == (cp == 0), so we could test either fp
  // or cp.
  cmpi(cp, Operand::Zero());
  beq(&skip);
  StoreP(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
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
  LoadP(sp, MemOperand(r6));

  // Unwind the handlers until the ENTRY handler is found.
  Label fetch_next, check_kind;
  b(&check_kind);
  bind(&fetch_next);
  LoadP(sp, MemOperand(sp, StackHandlerConstants::kNextOffset));

  bind(&check_kind);
  STATIC_ASSERT(StackHandler::JS_ENTRY == 0);
  LoadP(r5, MemOperand(sp, StackHandlerConstants::kStateOffset));
  andi(r0, r5, Operand(StackHandler::KindField::kMask));
  bne(&fetch_next, cr0);

  // Set the top handler address to next handler past the top ENTRY handler.
  pop(r5);
  StoreP(r5, MemOperand(r6));
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
  LoadP(scratch, MemOperand(fp, StandardFrameConstants::kContextOffset));
  // In debug mode, make sure the lexical context is set.
#ifdef DEBUG
  cmpi(scratch, Operand::Zero());
  Check(ne, kWeShouldNotHaveAnEmptyLexicalContext);
#endif

  // Load the native context of the current context.
  int offset =
      Context::kHeaderSize + Context::GLOBAL_OBJECT_INDEX * kPointerSize;
  LoadP(scratch, FieldMemOperand(scratch, offset));
  LoadP(scratch, FieldMemOperand(scratch, GlobalObject::kNativeContextOffset));

  // Check the context is a native context.
  if (emit_debug_code()) {
    // Cannot use ip as a temporary in this verification code. Due to the fact
    // that ip is clobbered as part of cmp with an object Operand.
    push(holder_reg);  // Temporarily save holder on the stack.
    // Read the first word and compare to the native_context_map.
    LoadP(holder_reg, FieldMemOperand(scratch, HeapObject::kMapOffset));
    LoadRoot(ip, Heap::kNativeContextMapRootIndex);
    cmp(holder_reg, ip);
    Check(eq, kJSGlobalObjectNativeContextShouldBeANativeContext);
    pop(holder_reg);  // Restore holder.
  }

  // Check if both contexts are the same.
  LoadP(ip, FieldMemOperand(holder_reg, JSGlobalProxy::kNativeContextOffset));
  cmp(scratch, ip);
  beq(&same_contexts);

  // Check the context is a native context.
  if (emit_debug_code()) {
    // Cannot use ip as a temporary in this verification code. Due to the fact
    // that ip is clobbered as part of cmp with an object Operand.
    push(holder_reg);  // Temporarily save holder on the stack.
    mr(holder_reg, ip);  // Move ip to its holding place.
    LoadRoot(ip, Heap::kNullValueRootIndex);
    cmp(holder_reg, ip);
    Check(ne, kJSGlobalProxyContextShouldNotBeNull);

    LoadP(holder_reg, FieldMemOperand(holder_reg, HeapObject::kMapOffset));
    LoadRoot(ip, Heap::kNativeContextMapRootIndex);
    cmp(holder_reg, ip);
    Check(eq, kJSGlobalObjectNativeContextShouldBeANativeContext);
    // Restore ip is not needed. ip is reloaded below.
    pop(holder_reg);  // Restore holder.
    // Restore ip to holder's context.
    LoadP(ip, FieldMemOperand(holder_reg, JSGlobalProxy::kNativeContextOffset));
  }

  // Check that the security token in the calling global object is
  // compatible with the security token in the receiving global
  // object.
  int token_offset = Context::kHeaderSize +
                     Context::SECURITY_TOKEN_INDEX * kPointerSize;

  LoadP(scratch, FieldMemOperand(scratch, token_offset));
  LoadP(ip, FieldMemOperand(ip, token_offset));
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
  LoadP(t1, FieldMemOperand(elements, SeededNumberDictionary::kCapacityOffset));
  SmiUntag(t1);
  subi(t1, t1, Operand(1));

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
    LoadP(ip,
          FieldMemOperand(t2, SeededNumberDictionary::kElementsStartOffset));
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
  LoadP(t1, FieldMemOperand(t2, kDetailsOffset));
  LoadSmiLiteral(ip, Smi::FromInt(PropertyDetails::TypeField::kMask));
  and_(r0, t1, ip, SetRC);
  bne(miss, cr0);

  // Get the value at the masked, scaled index and return.
  const int kValueOffset =
      SeededNumberDictionary::kElementsStartOffset + kPointerSize;
  LoadP(result, FieldMemOperand(t2, kValueOffset));
}


void MacroAssembler::Allocate(int object_size,
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
    b(gc_required);
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
  ASSERT_EQ(0, static_cast<int>(object_size & kObjectAlignmentMask));

  // Check relative positions of allocation top and limit addresses.
  ExternalReference allocation_top =
      AllocationUtils::GetAllocationTopReference(isolate(), flags);
  ExternalReference allocation_limit =
      AllocationUtils::GetAllocationLimitReference(isolate(), flags);

  intptr_t top   =
      reinterpret_cast<intptr_t>(allocation_top.address());
  intptr_t limit =
      reinterpret_cast<intptr_t>(allocation_limit.address());
  ASSERT((limit - top) == kPointerSize);

  // Set up allocation top address register.
  Register topaddr = scratch1;
  mov(topaddr, Operand(allocation_top));

  // This code stores a temporary value in ip. This is OK, as the code below
  // does not need ip for implicit literal generation.
  if ((flags & RESULT_CONTAINS_TOP) == 0) {
    // Load allocation top into result and allocation limit into ip.
    LoadP(result, MemOperand(topaddr));
    LoadP(ip, MemOperand(topaddr, kPointerSize));
  } else {
    if (emit_debug_code()) {
      // Assert that result actually contains top on entry. ip is used
      // immediately below so this use of ip does not cause difference with
      // respect to register content between debug and release mode.
      LoadP(ip, MemOperand(topaddr));
      cmp(result, ip);
      Check(eq, kUnexpectedAllocationTop);
    }
    // Load allocation limit into ip. Result already contains allocation top.
    LoadP(ip, MemOperand(topaddr, limit - top), r0);
  }

  if ((flags & DOUBLE_ALIGNMENT) != 0) {
    // Align the next allocation. Storing the filler map without checking top is
    // safe in new-space because the limit of the heap is aligned there.
    ASSERT((flags & PRETENURE_OLD_POINTER_SPACE) == 0);
#if V8_TARGET_ARCH_PPC64
    STATIC_ASSERT(kPointerAlignment == kDoubleAlignment);
#else
    STATIC_ASSERT(kPointerAlignment * 2 == kDoubleAlignment);
    andi(scratch2, result, Operand(kDoubleAlignmentMask));
    Label aligned;
    beq(&aligned, cr0);
    if ((flags & PRETENURE_OLD_DATA_SPACE) != 0) {
      cmpl(result, ip);
      bge(gc_required);
    }
    mov(scratch2, Operand(isolate()->factory()->one_pointer_filler_map()));
    stw(scratch2, MemOperand(result));
    addi(result, result, Operand(kDoubleSize / 2));
    bind(&aligned);
#endif
  }

  // Calculate new top and bail out if new space is exhausted. Use result
  // to calculate the new top.
  li(r0, Operand(-1));
  if (is_int16(object_size)) {
    addic(scratch2, result, Operand(object_size));
  } else {
    mov(scratch2, Operand(object_size));
    addc(scratch2, result, scratch2);
  }
  addze(r0, r0, LeaveOE, SetRC);
  beq(gc_required, cr0);
  cmpl(scratch2, ip);
  bgt(gc_required);
  StoreP(scratch2, MemOperand(topaddr));

  // Tag object if requested.
  if ((flags & TAG_OBJECT) != 0) {
    addi(result, result, Operand(kHeapObjectTag));
  }
}


void MacroAssembler::Allocate(Register object_size,
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
    b(gc_required);
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
  ExternalReference allocation_top =
      AllocationUtils::GetAllocationTopReference(isolate(), flags);
  ExternalReference allocation_limit =
      AllocationUtils::GetAllocationLimitReference(isolate(), flags);
  intptr_t top =
      reinterpret_cast<intptr_t>(allocation_top.address());
  intptr_t limit =
      reinterpret_cast<intptr_t>(allocation_limit.address());
  ASSERT((limit - top) == kPointerSize);

  // Set up allocation top address.
  Register topaddr = scratch1;
  mov(topaddr, Operand(allocation_top));

  // This code stores a temporary value in ip. This is OK, as the code below
  // does not need ip for implicit literal generation.
  if ((flags & RESULT_CONTAINS_TOP) == 0) {
    // Load allocation top into result and allocation limit into ip.
    LoadP(result, MemOperand(topaddr));
    LoadP(ip, MemOperand(topaddr, kPointerSize));
  } else {
    if (emit_debug_code()) {
      // Assert that result actually contains top on entry. ip is used
      // immediately below so this use of ip does not cause difference with
      // respect to register content between debug and release mode.
      LoadP(ip, MemOperand(topaddr));
      cmp(result, ip);
      Check(eq, kUnexpectedAllocationTop);
    }
    // Load allocation limit into ip. Result already contains allocation top.
    LoadP(ip, MemOperand(topaddr, limit - top));
  }

  if ((flags & DOUBLE_ALIGNMENT) != 0) {
    // Align the next allocation. Storing the filler map without checking top is
    // safe in new-space because the limit of the heap is aligned there.
    ASSERT((flags & PRETENURE_OLD_POINTER_SPACE) == 0);
#if V8_TARGET_ARCH_PPC64
    STATIC_ASSERT(kPointerAlignment == kDoubleAlignment);
#else
    STATIC_ASSERT(kPointerAlignment * 2 == kDoubleAlignment);
    andi(scratch2, result, Operand(kDoubleAlignmentMask));
    Label aligned;
    beq(&aligned, cr0);
    if ((flags & PRETENURE_OLD_DATA_SPACE) != 0) {
      cmpl(result, ip);
      bge(gc_required);
    }
    mov(scratch2, Operand(isolate()->factory()->one_pointer_filler_map()));
    stw(scratch2, MemOperand(result));
    addi(result, result, Operand(kDoubleSize / 2));
    bind(&aligned);
#endif
  }

  // Calculate new top and bail out if new space is exhausted. Use result
  // to calculate the new top. Object size may be in words so a shift is
  // required to get the number of bytes.
  li(r0, Operand(-1));
  if ((flags & SIZE_IN_WORDS) != 0) {
    ShiftLeftImm(scratch2, object_size, Operand(kPointerSizeLog2));
    addc(scratch2, result, scratch2);
  } else {
    addc(scratch2, result, object_size);
  }
  addze(r0, r0, LeaveOE, SetRC);
  beq(gc_required, cr0);
  cmpl(scratch2, ip);
  bgt(gc_required);

  // Update allocation top. result temporarily holds the new top.
  if (emit_debug_code()) {
    andi(r0, scratch2, Operand(kObjectAlignmentMask));
    Check(eq, kUnalignedAllocationInNewSpace, cr0);
  }
  StoreP(scratch2, MemOperand(topaddr));

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
  LoadP(scratch, MemOperand(scratch));
  cmp(object, scratch);
  Check(lt, kUndoAllocationOfNonAllocatedMemory);
#endif
  // Write the address of the object to un-allocate as the current top.
  mov(scratch, Operand(new_space_allocation_top));
  StoreP(object, MemOperand(scratch));
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
  Allocate(scratch1,
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
  ASSERT((SeqOneByteString::kHeaderSize & kObjectAlignmentMask) == 0);
  ASSERT(kCharSize == 1);
  addi(scratch1, length,
       Operand(kObjectAlignmentMask + SeqOneByteString::kHeaderSize));
  li(r0, Operand(~kObjectAlignmentMask));
  and_(scratch1, scratch1, r0);

  // Allocate ASCII string in new space.
  Allocate(scratch1,
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
  Allocate(ConsString::kSize, result, scratch1, scratch2, gc_required,
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
  Label allocate_new_space, install_map;
  AllocationFlags flags = TAG_OBJECT;

  ExternalReference high_promotion_mode = ExternalReference::
      new_space_high_promotion_mode_active_address(isolate());
  mov(scratch1, Operand(high_promotion_mode));
  LoadP(scratch1, MemOperand(scratch1));
  cmpi(scratch1, Operand::Zero());
  beq(&allocate_new_space);

  Allocate(ConsString::kSize,
           result,
           scratch1,
           scratch2,
           gc_required,
           static_cast<AllocationFlags>(flags | PRETENURE_OLD_POINTER_SPACE));

  b(&install_map);

  bind(&allocate_new_space);
  Allocate(ConsString::kSize,
           result,
           scratch1,
           scratch2,
           gc_required,
           flags);

  bind(&install_map);

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
  Allocate(SlicedString::kSize, result, scratch1, scratch2, gc_required,
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
  Allocate(SlicedString::kSize, result, scratch1, scratch2, gc_required,
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
  LoadP(map, FieldMemOperand(object, HeapObject::kMapOffset));
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
  cmpli(scratch, Operand(Map::kMaximumBitField2FastHoleyElementValue));
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
  cmpli(scratch, Operand(Map::kMaximumBitField2FastHoleySmiElementValue));
  ble(fail);
  cmpli(scratch, Operand(Map::kMaximumBitField2FastHoleyElementValue));
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



void MacroAssembler::StoreNumberToDoubleElements(
                                      Register value_reg,
                                      Register key_reg,
                                      Register elements_reg,
                                      Register scratch1,
                                      DoubleRegister double_scratch,
                                      Label* fail,
                                      int elements_offset) {
  Label smi_value, store;

  // Handle smi values specially.
  JumpIfSmi(value_reg, &smi_value);

  // Ensure that the object is a heap number
  CheckMap(value_reg,
           scratch1,
           isolate()->factory()->heap_number_map(),
           fail,
           DONT_DO_SMI_CHECK);

  lfd(double_scratch, FieldMemOperand(value_reg, HeapNumber::kValueOffset));
  // Force a canonical NaN.
  CanonicalizeNaN(double_scratch);
  b(&store);

  bind(&smi_value);
  SmiToDouble(double_scratch, value_reg);

  bind(&store);
  SmiToDoubleArrayOffset(scratch1, key_reg);
  add(scratch1, elements_reg, scratch1);
  stfd(double_scratch,
       FieldMemOperand(scratch1,
                       FixedDoubleArray::kHeaderSize - elements_offset));
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

void MacroAssembler::SubAndCheckForOverflow(Register dst,
                                            Register left,
                                            Register right,
                                            Register overflow_dst,
                                            Register scratch) {
  ASSERT(!dst.is(overflow_dst));
  ASSERT(!dst.is(scratch));
  ASSERT(!overflow_dst.is(scratch));
  ASSERT(!overflow_dst.is(left));
  ASSERT(!overflow_dst.is(right));

  // C = A-B; C overflows if A/B have diff signs and C has diff sign than A
  if (dst.is(left)) {
    mr(scratch, left);            // Preserve left.
    sub(dst, left, right);        // Left is overwritten.
    xor_(overflow_dst, dst, scratch);
    xor_(scratch, scratch, right);
    and_(overflow_dst, overflow_dst, scratch, SetRC);
  } else if (dst.is(right)) {
    mr(scratch, right);           // Preserve right.
    sub(dst, left, right);        // Right is overwritten.
    xor_(overflow_dst, dst, left);
    xor_(scratch, left, scratch);
    and_(overflow_dst, overflow_dst, scratch, SetRC);
  } else {
    sub(dst, left, right);
    xor_(overflow_dst, dst, left);
    xor_(scratch, left, right);
    and_(overflow_dst, scratch, overflow_dst, SetRC);
  }
}


void MacroAssembler::CompareMap(Register obj,
                                Register scratch,
                                Handle<Map> map,
                                Label* early_success) {
  LoadP(scratch, FieldMemOperand(obj, HeapObject::kMapOffset));
  CompareMap(scratch, map, early_success);
}


void MacroAssembler::CompareMap(Register obj_map,
                                Handle<Map> map,
                                Label* early_success) {
  mov(r0, Operand(map));
  cmp(obj_map, r0);
}


void MacroAssembler::CheckMap(Register obj,
                              Register scratch,
                              Handle<Map> map,
                              Label* fail,
                              SmiCheckType smi_check_type) {
  if (smi_check_type == DO_SMI_CHECK) {
    JumpIfSmi(obj, fail);
  }

  Label success;
  CompareMap(obj, scratch, map, &success);
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
  LoadP(scratch, FieldMemOperand(obj, HeapObject::kMapOffset));
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
  LoadP(scratch, FieldMemOperand(obj, HeapObject::kMapOffset));
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
    LoadP(scratch,
          FieldMemOperand(function, JSFunction::kSharedFunctionInfoOffset));
    lwz(scratch,
        FieldMemOperand(scratch, SharedFunctionInfo::kCompilerHintsOffset));
    TestBit(scratch,
#if V8_TARGET_ARCH_PPC64
            SharedFunctionInfo::kBoundFunction,
#else
            SharedFunctionInfo::kBoundFunction + kSmiTagSize,
#endif
            r0);
    bne(miss, cr0);
  }

  // Make sure that the function has an instance prototype.
  Label non_instance;
  lbz(scratch, FieldMemOperand(result, Map::kBitFieldOffset));
  andi(r0, scratch, Operand(1 << Map::kHasNonInstancePrototype));
  bne(&non_instance, cr0);

  // Get the prototype or initial map from the function.
  LoadP(result,
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
  LoadP(result, FieldMemOperand(result, Map::kPrototypeOffset));
  b(&done);

  // Non-instance prototype: Fetch prototype from constructor field
  // in initial map.
  bind(&non_instance);
  LoadP(result, FieldMemOperand(result, Map::kConstructorOffset));

  // All done.
  bind(&done);
}


void MacroAssembler::CallStub(CodeStub* stub,
                              TypeFeedbackId ast_id,
                              Condition cond) {
  ASSERT(AllowThisStubCall(stub));  // Stub calls are not allowed in some stubs.
  Call(stub->GetCode(isolate()), RelocInfo::CODE_TARGET, ast_id, cond);
}


void MacroAssembler::TailCallStub(CodeStub* stub, Condition cond) {
  ASSERT(allow_stub_calls_ ||
         stub->CompilingCallsToThisStubIsGCSafe(isolate()));
  Jump(stub->GetCode(isolate()), RelocInfo::CODE_TARGET, cond);
}


static int AddressOffset(ExternalReference ref0, ExternalReference ref1) {
  return ref0.address() - ref1.address();
}


void MacroAssembler::CallApiFunctionAndReturn(
    ExternalReference function,
    Address function_address,
    ExternalReference thunk_ref,
    Register thunk_last_arg,
    int stack_space,
    MemOperand return_value_operand,
    MemOperand* context_restore_operand) {
  ExternalReference next_address =
    ExternalReference::handle_scope_next_address(isolate());
  const int kNextOffset = 0;
  const int kLimitOffset = AddressOffset(
    ExternalReference::handle_scope_limit_address(isolate()),
    next_address);
  const int kLevelOffset = AddressOffset(
    ExternalReference::handle_scope_level_address(isolate()),
    next_address);
  Register scratch = { thunk_last_arg.code() + 1 };

  // Allocate HandleScope in callee-save registers.
  // r17 - next_address
  // r14 - next_address->kNextOffset
  // r15 - next_address->kLimitOffset
  // r16 - next_address->kLevelOffset
  mov(r17, Operand(next_address));
  LoadP(r14, MemOperand(r17, kNextOffset));
  LoadP(r15, MemOperand(r17, kLimitOffset));
  lwz(r16, MemOperand(r17, kLevelOffset));
  addi(r16, r16, Operand(1));
  stw(r16, MemOperand(r17, kLevelOffset));

  if (FLAG_log_timer_events) {
    FrameScope frame(this, StackFrame::MANUAL);
    PushSafepointRegisters();
    PrepareCallCFunction(1, r3);
    mov(r3, Operand(ExternalReference::isolate_address(isolate())));
    CallCFunction(ExternalReference::log_enter_external_function(isolate()), 1);
    PopSafepointRegisters();
  }

  ASSERT(!thunk_last_arg.is(scratch));
  Label profiler_disabled;
  Label end_profiler_check;
  bool* is_profiling_flag =
      isolate()->cpu_profiler()->is_profiling_address();
  STATIC_ASSERT(sizeof(*is_profiling_flag) == 1);
  mov(scratch, Operand(reinterpret_cast<intptr_t>(is_profiling_flag)));
  lbz(scratch, MemOperand(scratch, 0));
  cmpi(scratch, Operand::Zero());
  beq(&profiler_disabled);

  // Additional parameter is the address of the actual callback.
  mov(thunk_last_arg, Operand(reinterpret_cast<intptr_t>(function_address)));
  mov(scratch, Operand(thunk_ref));
  jmp(&end_profiler_check);

  bind(&profiler_disabled);
  mov(scratch, Operand(function));
  bind(&end_profiler_check);

  // Native call returns to the DirectCEntry stub which redirects to the
  // return address pushed on stack (could have moved after GC).
  // DirectCEntry stub itself is generated early and never moves.
  DirectCEntryStub stub;
  stub.GenerateCall(this, scratch);

  if (FLAG_log_timer_events) {
    FrameScope frame(this, StackFrame::MANUAL);
    PushSafepointRegisters();
    PrepareCallCFunction(1, r3);
    mov(r3, Operand(ExternalReference::isolate_address(isolate())));
    CallCFunction(ExternalReference::log_leave_external_function(isolate()), 1);
    PopSafepointRegisters();
  }

  Label promote_scheduled_exception;
  Label exception_handled;
  Label delete_allocated_handles;
  Label leave_exit_frame;
  Label return_value_loaded;

  // load value from ReturnValue
  LoadP(r3, return_value_operand);
  bind(&return_value_loaded);
  // No more valid handles (the result handle was the last one). Restore
  // previous handle scope.
  StoreP(r14, MemOperand(r17, kNextOffset));
  if (emit_debug_code()) {
    lwz(r4, MemOperand(r17, kLevelOffset));
    cmp(r4, r16);
    Check(eq, kUnexpectedLevelAfterReturnFromApiCall);
  }
  subi(r16, r16, Operand(1));
  stw(r16, MemOperand(r17, kLevelOffset));
  LoadP(ip, MemOperand(r17, kLimitOffset));
  cmp(r15, ip);
  bne(&delete_allocated_handles);

  // Check if the function scheduled an exception.
  bind(&leave_exit_frame);
  LoadRoot(r14, Heap::kTheHoleValueRootIndex);
  mov(ip, Operand(ExternalReference::scheduled_exception_address(isolate())));
  LoadP(r15, MemOperand(ip));
  cmp(r14, r15);
  bne(&promote_scheduled_exception);
  bind(&exception_handled);

  bool restore_context = context_restore_operand != NULL;
  if (restore_context) {
    LoadP(cp, *context_restore_operand);
  }
  // LeaveExitFrame expects unwind space to be in a register.
  mov(r14, Operand(stack_space));
  LeaveExitFrame(false, r14, !restore_context);
  blr();

  bind(&promote_scheduled_exception);
  {
    FrameScope frame(this, StackFrame::INTERNAL);
    CallExternalReference(
        ExternalReference(Runtime::kPromoteScheduledException, isolate()),
        0);
  }
  jmp(&exception_handled);

  // HandleScope limit has changed. Delete allocated extensions.
  bind(&delete_allocated_handles);
  StoreP(r15, MemOperand(r17, kLimitOffset));
  mr(r14, r3);
  PrepareCallCFunction(1, r15);
  mov(r3, Operand(ExternalReference::isolate_address(isolate())));
  CallCFunction(
      ExternalReference::delete_handle_scope_extensions(isolate()), 1);
  mr(r3, r14);
  b(&leave_exit_frame);
}


bool MacroAssembler::AllowThisStubCall(CodeStub* stub) {
  if (!has_frame_ && stub->SometimesSetsUpAFrame()) return false;
  return allow_stub_calls_ || stub->CompilingCallsToThisStubIsGCSafe(isolate());
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
  STATIC_ASSERT(String::kHashShift == 2);
  STATIC_ASSERT(String::kArrayIndexValueBits == 24);
  // index = SmiTag((hash >> 2) & 0x00FFFFFF);
#if V8_TARGET_ARCH_PPC64
  ExtractBitRange(index, hash, 25, 2);
  SmiTag(index);
#else
  STATIC_ASSERT(kSmiShift == 1);
  // 32-bit can do this in one instruction:
  //    index = (hash & 0x03FFFFFC) >> 1;
  rlwinm(index, hash, 31, 7, 30);
#endif
}


void MacroAssembler::SmiToDouble(DoubleRegister value, Register smi) {
  SmiUntag(ip, smi);
  ConvertIntToDouble(ip, value);
}


void MacroAssembler::TestDoubleIsInt32(DoubleRegister double_input,
                                       Register scratch1,
                                       Register scratch2,
                                       DoubleRegister double_scratch) {
  TryDoubleToInt32Exact(scratch1, double_input, scratch2, double_scratch);
}


void MacroAssembler::TryDoubleToInt32Exact(Register result,
                                           DoubleRegister double_input,
                                           Register scratch,
                                           DoubleRegister double_scratch) {
  Label done;
  ASSERT(!double_input.is(double_scratch));

  ConvertDoubleToInt64(double_input, result,
#if !V8_TARGET_ARCH_PPC64
                       scratch,
#endif
                       double_scratch);

#if V8_TARGET_ARCH_PPC64
  TestIfInt32(result, scratch, r0);
#else
  TestIfInt32(scratch, result, r0);
#endif
  bne(&done);

  // convert back and compare
  fcfid(double_scratch, double_scratch);
  fcmpu(double_scratch, double_input);
  bind(&done);
}


void MacroAssembler::TryInt32Floor(Register result,
                                   DoubleRegister double_input,
                                   Register input_high,
                                   Register scratch,
                                   DoubleRegister double_scratch,
                                   Label* done,
                                   Label* exact) {
  ASSERT(!result.is(input_high));
  ASSERT(!double_input.is(double_scratch));
  Label exception;

  // Move high word into input_high
  stfdu(double_input, MemOperand(sp, -kDoubleSize));
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
  lwz(input_high, MemOperand(sp, 4));
#else
  lwz(input_high, MemOperand(sp, 0));
#endif
  addi(sp, sp, Operand(kDoubleSize));

  // Test for NaN/Inf
  ExtractBitMask(result, input_high, HeapNumber::kExponentMask);
  cmpli(result, Operand(0x7ff));
  beq(&exception);

  // Convert (rounding to -Inf)
  ConvertDoubleToInt64(double_input, result,
#if !V8_TARGET_ARCH_PPC64
                       scratch,
#endif
                       double_scratch,
                       kRoundToMinusInf);

  // Test for overflow
#if V8_TARGET_ARCH_PPC64
  TestIfInt32(result, scratch, r0);
#else
  TestIfInt32(scratch, result, r0);
#endif
  bne(&exception);

  // Test for exactness
  fcfid(double_scratch, double_scratch);
  fcmpu(double_scratch, double_input);
  beq(exact);
  b(done);

  bind(&exception);
}


void MacroAssembler::TryInlineTruncateDoubleToI(Register result,
                                                DoubleRegister double_input,
                                                Label* done) {
  DoubleRegister double_scratch = kScratchDoubleReg;
  Register scratch = ip;

  ConvertDoubleToInt64(double_input, result,
#if !V8_TARGET_ARCH_PPC64
                       scratch,
#endif
                       double_scratch);

  // Test for overflow
#if V8_TARGET_ARCH_PPC64
  TestIfInt32(result, scratch, r0);
#else
  TestIfInt32(scratch, result, r0);
#endif
  beq(done);
}


void MacroAssembler::TruncateDoubleToI(Register result,
                                       DoubleRegister double_input) {
  Label done;

  TryInlineTruncateDoubleToI(result, double_input, &done);

  // If we fell through then inline version didn't succeed - call stub instead.
  mflr(r0);
  push(r0);
  // Put input on stack.
  stfdu(double_input, MemOperand(sp, -kDoubleSize));

  DoubleToIStub stub(sp, result, 0, true, true);
  CallStub(&stub);

  addi(sp, sp, Operand(kDoubleSize));
  pop(r0);
  mtlr(r0);

  bind(&done);
}


void MacroAssembler::TruncateHeapNumberToI(Register result,
                                           Register object) {
  Label done;
  DoubleRegister double_scratch = kScratchDoubleReg;
  ASSERT(!result.is(object));

  lfd(double_scratch, FieldMemOperand(object, HeapNumber::kValueOffset));
  TryInlineTruncateDoubleToI(result, double_scratch, &done);

  // If we fell through then inline version didn't succeed - call stub instead.
  mflr(r0);
  push(r0);
  DoubleToIStub stub(object,
                     result,
                     HeapNumber::kValueOffset - kHeapObjectTag,
                     true,
                     true);
  CallStub(&stub);
  pop(r0);
  mtlr(r0);

  bind(&done);
}


void MacroAssembler::TruncateNumberToI(Register object,
                                       Register result,
                                       Register heap_number_map,
                                       Register scratch1,
                                       Label* not_number) {
  Label done;
  ASSERT(!result.is(object));

  UntagAndJumpIfSmi(result, object, &done);
  JumpIfNotHeapNumber(object, heap_number_map, scratch1, not_number);
  TruncateHeapNumberToI(result, object);

  bind(&done);
}


void MacroAssembler::GetLeastBitsFromSmi(Register dst,
                                         Register src,
                                         int num_least_bits) {
#if V8_TARGET_ARCH_PPC64
  rldicl(dst, src, kBitsPerPointer - kSmiShift,
         kBitsPerPointer - num_least_bits);
#else
  rlwinm(dst, src, kBitsPerPointer - kSmiShift,
         kBitsPerPointer - num_least_bits, 31);
#endif
}


void MacroAssembler::GetLeastBitsFromInt32(Register dst,
                                           Register src,
                                           int num_least_bits) {
  rlwinm(dst, src, 0, 32 - num_least_bits, 31);
}


void MacroAssembler::CallRuntime(const Runtime::Function* f,
                                 int num_arguments,
                                 SaveFPRegsMode save_doubles) {
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
#if V8_TARGET_ARCH_PPC64
  CEntryStub stub(f->result_size, save_doubles);
#else
  CEntryStub stub(1, save_doubles);
#endif
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
  Jump(stub.GetCode(isolate()), RelocInfo::CODE_TARGET);
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
  LoadP(target,
        MemOperand(cp, Context::SlotOffset(Context::GLOBAL_OBJECT_INDEX)));
  LoadP(target, FieldMemOperand(target, GlobalObject::kBuiltinsOffset));
  // Load the JavaScript builtin function from the builtins object.
  LoadP(target,
        FieldMemOperand(target,
                        JSBuiltinsObject::OffsetOfFunctionWithId(id)), r0);
}


void MacroAssembler::GetBuiltinEntry(Register target, Builtins::JavaScript id) {
  ASSERT(!target.is(r4));
  GetBuiltinFunction(r4, id);
  // Load the code entry point from the builtins object.
  LoadP(target, FieldMemOperand(r4, JSFunction::kCodeEntryOffset));
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
    subi(scratch1, scratch1, Operand(value));
    stw(scratch1, MemOperand(scratch2));
  }
}


void MacroAssembler::Assert(Condition cond, BailoutReason reason,
                            CRegister cr) {
  if (emit_debug_code())
    Check(cond, reason, cr);
}


void MacroAssembler::AssertFastElements(Register elements) {
  if (emit_debug_code()) {
    ASSERT(!elements.is(ip));
    Label ok;
    push(elements);
    LoadP(elements, FieldMemOperand(elements, HeapObject::kMapOffset));
    LoadRoot(ip, Heap::kFixedArrayMapRootIndex);
    cmp(elements, ip);
    beq(&ok);
    LoadRoot(ip, Heap::kFixedDoubleArrayMapRootIndex);
    cmp(elements, ip);
    beq(&ok);
    LoadRoot(ip, Heap::kFixedCOWArrayMapRootIndex);
    cmp(elements, ip);
    beq(&ok);
    Abort(kJSObjectWithFastElementsMapHasSlowElements);
    bind(&ok);
    pop(elements);
  }
}


void MacroAssembler::Check(Condition cond, BailoutReason reason, CRegister cr) {
  Label L;
  b(cond, &L, cr);
  Abort(reason);
  // will not return here
  bind(&L);
}


void MacroAssembler::Abort(BailoutReason reason) {
  Label abort_start;
  bind(&abort_start);
  // We want to pass the msg string like a smi to avoid GC
  // problems, however msg is not guaranteed to be aligned
  // properly. Instead, we pass an aligned pointer that is
  // a proper v8 smi, but also pass the alignment difference
  // from the real pointer as a smi.
  const char* msg = GetBailoutReason(reason);
  intptr_t p1 = reinterpret_cast<intptr_t>(msg);
  intptr_t p0 = (p1 & ~kSmiTagMask) + kSmiTag;
  ASSERT(reinterpret_cast<Object*>(p0)->IsSmi());
#ifdef DEBUG
  if (msg != NULL) {
    RecordComment("Abort message: ");
    RecordComment(msg);
  }

  if (FLAG_trap_on_abort) {
    stop(msg);
    return;
  }
#endif

  mov(r0, Operand(p0));
  push(r0);
  LoadSmiLiteral(r0, Smi::FromInt(p1 - p0));
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
}


void MacroAssembler::LoadContext(Register dst, int context_chain_length) {
  if (context_chain_length > 0) {
    // Move up the chain of contexts to the context containing the slot.
    LoadP(dst, MemOperand(cp, Context::SlotOffset(Context::PREVIOUS_INDEX)));
    for (int i = 1; i < context_chain_length; i++) {
      LoadP(dst, MemOperand(dst, Context::SlotOffset(Context::PREVIOUS_INDEX)));
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
  LoadP(scratch,
      MemOperand(cp, Context::SlotOffset(Context::GLOBAL_OBJECT_INDEX)));
  LoadP(scratch, FieldMemOperand(scratch, GlobalObject::kNativeContextOffset));

  // Check that the function's map is the same as the expected cached map.
  LoadP(scratch,
      MemOperand(scratch,
                 Context::SlotOffset(Context::JS_ARRAY_MAPS_INDEX)));
  size_t offset = expected_kind * kPointerSize +
      FixedArrayBase::kHeaderSize;
  LoadP(ip, FieldMemOperand(scratch, offset));
  cmp(map_in_out, ip);
  bne(no_map_match);

  // Use the transitioned cached map.
  offset = transitioned_kind * kPointerSize +
      FixedArrayBase::kHeaderSize;
  LoadP(map_in_out, FieldMemOperand(scratch, offset));
}


void MacroAssembler::LoadInitialArrayMap(
    Register function_in, Register scratch,
    Register map_out, bool can_have_holes) {
  ASSERT(!function_in.is(map_out));
  Label done;
  LoadP(map_out, FieldMemOperand(function_in,
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
  LoadP(function,
        MemOperand(cp, Context::SlotOffset(Context::GLOBAL_OBJECT_INDEX)));
  // Load the native context from the global or builtins object.
  LoadP(function, FieldMemOperand(function,
                                  GlobalObject::kNativeContextOffset));
  // Load the function from the native context.
  LoadP(function, MemOperand(function, Context::SlotOffset(index)), r0);
}


void MacroAssembler::LoadArrayFunction(Register function) {
  // Load the global or builtins object from the current context.
  LoadP(function,
        MemOperand(cp, Context::SlotOffset(Context::GLOBAL_OBJECT_INDEX)));
  // Load the global context from the global or builtins object.
  LoadP(function,
        FieldMemOperand(function, GlobalObject::kGlobalContextOffset));
  // Load the array function from the native context.
  LoadP(function,
        MemOperand(function,
                   Context::SlotOffset(Context::ARRAY_FUNCTION_INDEX)));
}


void MacroAssembler::LoadGlobalFunctionInitialMap(Register function,
                                                  Register map,
                                                  Register scratch) {
  // Load the initial map. The global functions all have initial maps.
  LoadP(map,
        FieldMemOperand(function, JSFunction::kPrototypeOrInitialMapOffset));
  if (emit_debug_code()) {
    Label ok, fail;
    CheckMap(map, scratch, Heap::kMetaMapRootIndex, &fail, DO_SMI_CHECK);
    b(&ok);
    bind(&fail);
    Abort(kGlobalFunctionsMustHaveInitialMap);
    bind(&ok);
  }
}


void MacroAssembler::JumpIfNotPowerOfTwoOrZero(
    Register reg,
    Register scratch,
    Label* not_power_of_two_or_zero) {
  subi(scratch, reg, Operand(1));
  cmpi(scratch, Operand::Zero());
  blt(not_power_of_two_or_zero);
  and_(r0, scratch, reg, SetRC);
  bne(not_power_of_two_or_zero, cr0);
}


void MacroAssembler::JumpIfNotPowerOfTwoOrZeroAndNeg(
    Register reg,
    Register scratch,
    Label* zero_and_neg,
    Label* not_power_of_two) {
  subi(scratch, reg, Operand(1));
  cmpi(scratch, Operand::Zero());
  blt(zero_and_neg);
  and_(r0, scratch, reg, SetRC);
  bne(not_power_of_two, cr0);
}

#if !V8_TARGET_ARCH_PPC64
void MacroAssembler::SmiTagCheckOverflow(Register reg, Register overflow) {
  ASSERT(!reg.is(overflow));
  mr(overflow, reg);  // Save original value.
  SmiTag(reg);
  xor_(overflow, overflow, reg, SetRC);  // Overflow if (value ^ 2 * value) < 0.
}


void MacroAssembler::SmiTagCheckOverflow(Register dst,
                                         Register src,
                                         Register overflow) {
  if (dst.is(src)) {
    // Fall back to slower case.
    SmiTagCheckOverflow(dst, overflow);
  } else {
    ASSERT(!dst.is(src));
    ASSERT(!dst.is(overflow));
    ASSERT(!src.is(overflow));
    SmiTag(dst, src);
    xor_(overflow, dst, src, SetRC);  // Overflow if (value ^ 2 * value) < 0.
  }
}
#endif

void MacroAssembler::JumpIfNotBothSmi(Register reg1,
                                      Register reg2,
                                      Label* on_not_both_smi) {
  STATIC_ASSERT(kSmiTag == 0);
  ASSERT_EQ(1, static_cast<int>(kSmiTagMask));
  orx(r0, reg1, reg2, LeaveRC);
  JumpIfNotSmi(r0, on_not_both_smi);
}


void MacroAssembler::UntagAndJumpIfSmi(
    Register dst, Register src, Label* smi_case) {
  STATIC_ASSERT(kSmiTag == 0);
  STATIC_ASSERT(kSmiTagSize == 1);
  TestBit(src, 0, r0);
  SmiUntag(dst, src);
  beq(smi_case, cr0);
}


void MacroAssembler::UntagAndJumpIfNotSmi(
    Register dst, Register src, Label* non_smi_case) {
  STATIC_ASSERT(kSmiTag == 0);
  STATIC_ASSERT(kSmiTagSize == 1);
  TestBit(src, 0, r0);
  SmiUntag(dst, src);
  bne(non_smi_case, cr0);
}


void MacroAssembler::JumpIfEitherSmi(Register reg1,
                                     Register reg2,
                                     Label* on_either_smi) {
  STATIC_ASSERT(kSmiTag == 0);
  JumpIfSmi(reg1, on_either_smi);
  JumpIfSmi(reg2, on_either_smi);
}


void MacroAssembler::AssertNotSmi(Register object) {
  if (emit_debug_code()) {
    STATIC_ASSERT(kSmiTag == 0);
    andi(r0, object, Operand(kSmiTagMask));
    Check(ne, kOperandIsASmi, cr0);
  }
}


void MacroAssembler::AssertSmi(Register object) {
  if (emit_debug_code()) {
    STATIC_ASSERT(kSmiTag == 0);
    andi(r0, object, Operand(kSmiTagMask));
    Check(eq, kOperandIsNotSmi, cr0);
  }
}


void MacroAssembler::AssertString(Register object) {
  if (emit_debug_code()) {
    STATIC_ASSERT(kSmiTag == 0);
    andi(r0, object, Operand(kSmiTagMask));
    Check(ne, kOperandIsASmiAndNotAString, cr0);
    push(object);
    LoadP(object, FieldMemOperand(object, HeapObject::kMapOffset));
    CompareInstanceType(object, object, FIRST_NONSTRING_TYPE);
    pop(object);
    Check(lt, kOperandIsNotAString);
  }
}


void MacroAssembler::AssertName(Register object) {
  if (emit_debug_code()) {
    STATIC_ASSERT(kSmiTag == 0);
    andi(r0, object, Operand(kSmiTagMask));
    Check(ne, kOperandIsASmiAndNotAName, cr0);
    push(object);
    LoadP(object, FieldMemOperand(object, HeapObject::kMapOffset));
    CompareInstanceType(object, object, LAST_NAME_TYPE);
    pop(object);
    Check(le, kOperandIsNotAName);
  }
}


void MacroAssembler::AssertIsRoot(Register reg, Heap::RootListIndex index) {
  if (emit_debug_code()) {
    CompareRoot(reg, index);
    Check(eq, kHeapNumberMapRegisterClobbered);
  }
}


void MacroAssembler::JumpIfNotHeapNumber(Register object,
                                         Register heap_number_map,
                                         Register scratch,
                                         Label* on_not_heap_number) {
  LoadP(scratch, FieldMemOperand(object, HeapObject::kMapOffset));
  AssertIsRoot(heap_number_map, Heap::kHeapNumberMapRootIndex);
  cmp(scratch, heap_number_map);
  bne(on_not_heap_number);
}


void MacroAssembler::LookupNumberStringCache(Register object,
                                             Register result,
                                             Register scratch1,
                                             Register scratch2,
                                             Register scratch3,
                                             Label* not_found) {
  // Use of registers. Register result is used as a temporary.
  Register number_string_cache = result;
  Register mask = scratch3;

  // Load the number string cache.
  LoadRoot(number_string_cache, Heap::kNumberStringCacheRootIndex);

  // Make the hash mask from the length of the number string cache. It
  // contains two elements (number and string) for each cache entry.
  LoadP(mask, FieldMemOperand(number_string_cache,
                              FixedArray::kLengthOffset));
  // Divide length by two (length is a smi).
  ShiftRightArithImm(mask, mask, kSmiTagSize + kSmiShiftSize + 1);
  subi(mask, mask, Operand(1));  // Make mask.

  // Calculate the entry in the number string cache. The hash value in the
  // number string cache for smis is just the smi value, and the hash for
  // doubles is the xor of the upper and lower words. See
  // Heap::GetNumberStringCache.
  Label is_smi;
  Label load_result_from_cache;
  JumpIfSmi(object, &is_smi);
  CheckMap(object,
           scratch1,
           Heap::kHeapNumberMapRootIndex,
           not_found,
           DONT_DO_SMI_CHECK);

  STATIC_ASSERT(8 == kDoubleSize);
  lwz(scratch1, FieldMemOperand(object, HeapNumber::kExponentOffset));
  lwz(scratch2, FieldMemOperand(object, HeapNumber::kMantissaOffset));
  xor_(scratch1, scratch1, scratch2);
  and_(scratch1, scratch1, mask);

  // Calculate address of entry in string cache: each entry consists
  // of two pointer sized fields.
  ShiftLeftImm(scratch1, scratch1, Operand(kPointerSizeLog2 + 1));
  add(scratch1, number_string_cache, scratch1);

  Register probe = mask;
  LoadP(probe, FieldMemOperand(scratch1, FixedArray::kHeaderSize));
  JumpIfSmi(probe, not_found);
  lfd(d0, FieldMemOperand(object, HeapNumber::kValueOffset));
  lfd(d1, FieldMemOperand(probe, HeapNumber::kValueOffset));
  fcmpu(d0, d1);
  bne(not_found);  // The cache did not contain this value.
  b(&load_result_from_cache);

  bind(&is_smi);
  Register scratch = scratch1;
  SmiUntag(scratch, object);
  and_(scratch, mask, scratch);
  // Calculate address of entry in string cache: each entry consists
  // of two pointer sized fields.
  ShiftLeftImm(scratch, scratch, Operand(kPointerSizeLog2 + 1));
  add(scratch, number_string_cache, scratch);

  // Check if the entry is the smi we are looking for.
  LoadP(probe, FieldMemOperand(scratch, FixedArray::kHeaderSize));
  cmp(object, probe);
  bne(not_found);

  // Get the result from the cache.
  bind(&load_result_from_cache);
  LoadP(result,
        FieldMemOperand(scratch, FixedArray::kHeaderSize + kPointerSize));
  IncrementCounter(isolate()->counters()->number_to_string_native(),
                   1,
                   scratch1,
                   scratch2);
}


void MacroAssembler::JumpIfNonSmisNotBothSequentialAsciiStrings(
    Register first,
    Register second,
    Register scratch1,
    Register scratch2,
    Label* failure) {
  // Test that both first and second are sequential ASCII strings.
  // Assume that they are non-smis.
  LoadP(scratch1, FieldMemOperand(first, HeapObject::kMapOffset));
  LoadP(scratch2, FieldMemOperand(second, HeapObject::kMapOffset));
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
  and_(scratch1, first, second);
  JumpIfSmi(scratch1, failure);
  JumpIfNonSmisNotBothSequentialAsciiStrings(first,
                                             second,
                                             scratch1,
                                             scratch2,
                                             failure);
}


void MacroAssembler::JumpIfNotUniqueName(Register reg,
                                         Label* not_unique_name) {
  STATIC_ASSERT(kInternalizedTag == 0 && kStringTag == 0);
  Label succeed;
  andi(r0, reg, Operand(kIsNotStringMask | kIsNotInternalizedMask));
  beq(&succeed, cr0);
  cmpi(reg, Operand(SYMBOL_TYPE));
  bne(not_unique_name);

  bind(&succeed);
}


// Allocates a heap number or jumps to the need_gc label if the young space
// is full and a scavenge is needed.
void MacroAssembler::AllocateHeapNumber(Register result,
                                        Register scratch1,
                                        Register scratch2,
                                        Register heap_number_map,
                                        Label* gc_required,
                                        TaggingMode tagging_mode) {
  // Allocate an object in the heap for the heap number and tag it as a heap
  // object.
  Allocate(HeapNumber::kSize, result, scratch1, scratch2, gc_required,
           tagging_mode == TAG_RESULT ? TAG_OBJECT : NO_ALLOCATION_FLAGS);

  // Store heap number map in the allocated object.
  AssertIsRoot(heap_number_map, Heap::kHeapNumberMapRootIndex);
  if (tagging_mode == TAG_RESULT) {
    StoreP(heap_number_map, FieldMemOperand(result, HeapObject::kMapOffset),
           r0);
  } else {
    StoreP(heap_number_map, MemOperand(result, HeapObject::kMapOffset));
  }
}


void MacroAssembler::AllocateHeapNumberWithValue(Register result,
                                                 DoubleRegister value,
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
    LoadP(tmp, FieldMemOperand(src, i * kPointerSize), r0);
    StoreP(tmp, FieldMemOperand(dst, i * kPointerSize), r0);
  }
}


void MacroAssembler::CopyBytes(Register src,
                               Register dst,
                               Register length,
                               Register scratch) {
  Label align_loop, aligned, word_loop, byte_loop, byte_loop_1, done;

  ASSERT(!scratch.is(r0));

  cmpi(length, Operand::Zero());
  beq(&done);

  // Check src alignment and length to see whether word_loop is possible
  andi(scratch, src, Operand(kPointerSize - 1));
  beq(&aligned, cr0);
  subfic(scratch, scratch, Operand(kPointerSize * 2));
  cmp(length, scratch);
  blt(&byte_loop);

  // Align src before copying in word size chunks.
  subi(scratch, scratch, Operand(kPointerSize));
  mtctr(scratch);
  bind(&align_loop);
  lbz(scratch, MemOperand(src));
  addi(src, src, Operand(1));
  subi(length, length, Operand(1));
  stb(scratch, MemOperand(dst));
  addi(dst, dst, Operand(1));
  bdnz(&align_loop);

  bind(&aligned);

  // Copy bytes in word size chunks.
  if (emit_debug_code()) {
    andi(r0, src, Operand(kPointerSize - 1));
    Assert(eq, kExpectingAlignmentForCopyBytes, cr0);
  }

  ShiftRightImm(scratch, length, Operand(kPointerSizeLog2));
  cmpi(scratch, Operand::Zero());
  beq(&byte_loop);

  mtctr(scratch);
  bind(&word_loop);
  LoadP(scratch, MemOperand(src));
  addi(src, src, Operand(kPointerSize));
  subi(length, length, Operand(kPointerSize));
  if (CpuFeatures::IsSupported(UNALIGNED_ACCESSES)) {
    // currently false for PPC - but possible future opt
    StoreP(scratch, MemOperand(dst));
    addi(dst, dst, Operand(kPointerSize));
  } else {
#if __BYTE_ORDER == __LITTLE_ENDIAN
    stb(scratch, MemOperand(dst, 0));
    ShiftRightImm(scratch, scratch, Operand(8));
    stb(scratch, MemOperand(dst, 1));
    ShiftRightImm(scratch, scratch, Operand(8));
    stb(scratch, MemOperand(dst, 2));
    ShiftRightImm(scratch, scratch, Operand(8));
    stb(scratch, MemOperand(dst, 3));
#if V8_TARGET_ARCH_PPC64
    ShiftRightImm(scratch, scratch, Operand(8));
    stb(scratch, MemOperand(dst, 4));
    ShiftRightImm(scratch, scratch, Operand(8));
    stb(scratch, MemOperand(dst, 5));
    ShiftRightImm(scratch, scratch, Operand(8));
    stb(scratch, MemOperand(dst, 6));
    ShiftRightImm(scratch, scratch, Operand(8));
    stb(scratch, MemOperand(dst, 7));
#endif
#else
#if V8_TARGET_ARCH_PPC64
    stb(scratch, MemOperand(dst, 7));
    ShiftRightImm(scratch, scratch, Operand(8));
    stb(scratch, MemOperand(dst, 6));
    ShiftRightImm(scratch, scratch, Operand(8));
    stb(scratch, MemOperand(dst, 5));
    ShiftRightImm(scratch, scratch, Operand(8));
    stb(scratch, MemOperand(dst, 4));
    ShiftRightImm(scratch, scratch, Operand(8));
#endif
    stb(scratch, MemOperand(dst, 3));
    ShiftRightImm(scratch, scratch, Operand(8));
    stb(scratch, MemOperand(dst, 2));
    ShiftRightImm(scratch, scratch, Operand(8));
    stb(scratch, MemOperand(dst, 1));
    ShiftRightImm(scratch, scratch, Operand(8));
    stb(scratch, MemOperand(dst, 0));
#endif
    addi(dst, dst, Operand(kPointerSize));
  }
  bdnz(&word_loop);

  // Copy the last bytes if any left.
  cmpi(length, Operand::Zero());
  beq(&done);

  bind(&byte_loop);
  mtctr(length);
  bind(&byte_loop_1);
  lbz(scratch, MemOperand(src));
  addi(src, src, Operand(1));
  stb(scratch, MemOperand(dst));
  addi(dst, dst, Operand(1));
  bdnz(&byte_loop_1);

  bind(&done);
}


void MacroAssembler::InitializeFieldsWithFiller(Register start_offset,
                                                Register end_offset,
                                                Register filler) {
  Label loop, entry;
  b(&entry);
  bind(&loop);
  StoreP(filler, MemOperand(start_offset), r0);
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
  const int kFlatAsciiStringMask =
      kIsNotStringMask | kStringEncodingMask | kStringRepresentationMask;
  const int kFlatAsciiStringTag =
      kStringTag | kOneByteStringTag | kSeqStringTag;
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
  const int kFlatAsciiStringMask =
      kIsNotStringMask | kStringEncodingMask | kStringRepresentationMask;
  const int kFlatAsciiStringTag =
      kStringTag | kOneByteStringTag | kSeqStringTag;
  andi(scratch, type, Operand(kFlatAsciiStringMask));
  cmpi(scratch, Operand(kFlatAsciiStringTag));
  bne(failure);
}

static const int kRegisterPassedArguments = 8;


int MacroAssembler::CalculateStackPassedWords(int num_reg_arguments,
                                              int num_double_arguments) {
  int stack_passed_words = 0;
  if (num_double_arguments > DoubleRegister::NumRegisters()) {
      stack_passed_words +=
        2 * (num_double_arguments - DoubleRegister::NumRegisters());
  }
  // Up to 8 simple arguments are passed in registers r3..r10.
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
  int stack_space = kNumRequiredStackFrameSlots;

  if (frame_alignment > kPointerSize) {
    // Make stack end at alignment and make room for stack arguments
    // -- preserving original value of sp.
    mr(scratch, sp);
    addi(sp, sp, Operand(-(stack_passed_arguments + 1) * kPointerSize));
    ASSERT(IsPowerOf2(frame_alignment));
    ClearRightImm(sp, sp, Operand(WhichPowerOf2(frame_alignment)));
    StoreP(scratch, MemOperand(sp, stack_passed_arguments * kPointerSize));
  } else {
    // Make room for stack arguments
    stack_space += stack_passed_arguments;
  }

  // Allocate frame with required slots to make ABI work.
  li(r0, Operand::Zero());
  StorePU(r0, MemOperand(sp, -stack_space * kPointerSize));
}


void MacroAssembler::PrepareCallCFunction(int num_reg_arguments,
                                          Register scratch) {
  PrepareCallCFunction(num_reg_arguments, 0, scratch);
}


void MacroAssembler::SetCallCDoubleArguments(DoubleRegister dreg) {
  Move(d1, dreg);
}


void MacroAssembler::SetCallCDoubleArguments(DoubleRegister dreg1,
                                             DoubleRegister dreg2) {
  if (dreg2.is(d1)) {
    ASSERT(!dreg1.is(d2));
    Move(d2, dreg2);
    Move(d1, dreg1);
  } else {
    Move(d1, dreg1);
    Move(d2, dreg2);
  }
}


void MacroAssembler::SetCallCDoubleArguments(DoubleRegister dreg,
                                             Register reg) {
  Move(d1, dreg);
  Move(r3, reg);
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
  // Just call directly. The function called cannot cause a GC, or
  // allow preemption, so the return address in the link register
  // stays correct.
#if ABI_USES_FUNCTION_DESCRIPTORS && !defined(USE_SIMULATOR)
  // AIX uses a function descriptor. When calling C code be aware
  // of this descriptor and pick up values from it
  LoadP(ToRegister(2), MemOperand(function, kPointerSize));
  LoadP(ip, MemOperand(function, 0));
  Register dest = ip;
#elif ABI_TOC_ADDRESSABILITY_VIA_IP
  Move(ip, function);
  Register dest = ip;
#else
  Register dest = function;
#endif

  Call(dest);

  // Remove frame bought in PrepareCallCFunction
  int stack_passed_arguments = CalculateStackPassedWords(
      num_reg_arguments, num_double_arguments);
  int stack_space = kNumRequiredStackFrameSlots + stack_passed_arguments;
  if (ActivationFrameAlignment() > kPointerSize) {
    LoadP(sp, MemOperand(sp, stack_space * kPointerSize));
  } else {
    addi(sp, sp, Operand(stack_space * kPointerSize));
  }
}


void MacroAssembler::FlushICache(Register address, size_t size,
                                 Register scratch) {
  Label done;

  dcbf(r0, address);
  sync();
  icbi(r0, address);
  isync();

  // This code handles ranges which cross a single cacheline boundary.
  // scratch is last cacheline which intersects range.
  const int kCacheLineSizeLog2 = CpuFeatures::cache_line_size_log2();

  ASSERT(size > 0 && size <= (size_t)(1 << kCacheLineSizeLog2));
  addi(scratch, address, Operand(size - 1));
  ClearRightImm(scratch, scratch, Operand(kCacheLineSizeLog2));
  cmpl(scratch, address);
  ble(&done);

  dcbf(r0, scratch);
  sync();
  icbi(r0, scratch);
  isync();

  bind(&done);
}


// This code assumes a FIXED_SEQUENCE for lis/ori
void MacroAssembler::PatchRelocatedValue(Register lis_location,
                                         Register scratch,
                                         Register new_value) {
  lwz(scratch, MemOperand(lis_location));
  // At this point scratch is a lis instruction.
  if (emit_debug_code()) {
    And(scratch, scratch, Operand(kOpcodeMask | (0x1f * B16)));
    Cmpi(scratch, Operand(ADDIS), r0);
    Check(eq, kTheInstructionToPatchShouldBeALis);
    lwz(scratch, MemOperand(lis_location));
  }

  // insert new high word into lis instruction
#if V8_TARGET_ARCH_PPC64
  srdi(ip, new_value, Operand(32));
  rlwimi(scratch, ip, 16, 16, 31);
#else
  rlwimi(scratch, new_value, 16, 16, 31);
#endif

  stw(scratch, MemOperand(lis_location));

  lwz(scratch, MemOperand(lis_location, kInstrSize));
  // scratch is now ori.
  if (emit_debug_code()) {
    And(scratch, scratch, Operand(kOpcodeMask));
    Cmpi(scratch, Operand(ORI), r0);
    Check(eq, kTheInstructionShouldBeAnOri);
    lwz(scratch, MemOperand(lis_location, kInstrSize));
  }

  // insert new low word into ori instruction
#if V8_TARGET_ARCH_PPC64
  rlwimi(scratch, ip, 0, 16, 31);
#else
  rlwimi(scratch, new_value, 0, 16, 31);
#endif
  stw(scratch, MemOperand(lis_location, kInstrSize));

#if V8_TARGET_ARCH_PPC64
  if (emit_debug_code()) {
    lwz(scratch, MemOperand(lis_location, 2*kInstrSize));
    // scratch is now sldi.
    And(scratch, scratch, Operand(kOpcodeMask|kExt5OpcodeMask));
    Cmpi(scratch, Operand(EXT5|RLDICR), r0);
    Check(eq, kTheInstructionShouldBeASldi);
  }

  lwz(scratch, MemOperand(lis_location, 3*kInstrSize));
  // scratch is now ori.
  if (emit_debug_code()) {
    And(scratch, scratch, Operand(kOpcodeMask));
    Cmpi(scratch, Operand(ORIS), r0);
    Check(eq, kTheInstructionShouldBeAnOris);
    lwz(scratch, MemOperand(lis_location, 3*kInstrSize));
  }

  rlwimi(scratch, new_value, 16, 16, 31);
  stw(scratch, MemOperand(lis_location, 3*kInstrSize));

  lwz(scratch, MemOperand(lis_location, 4*kInstrSize));
  // scratch is now ori.
  if (emit_debug_code()) {
    And(scratch, scratch, Operand(kOpcodeMask));
    Cmpi(scratch, Operand(ORI), r0);
    Check(eq, kTheInstructionShouldBeAnOri);
    lwz(scratch, MemOperand(lis_location, 4*kInstrSize));
  }
  rlwimi(scratch, new_value, 0, 16, 31);
  stw(scratch, MemOperand(lis_location, 4*kInstrSize));
#endif

  // Update the I-cache so the new lis and addic can be executed.
#if V8_TARGET_ARCH_PPC64
  FlushICache(lis_location, 5 * kInstrSize, scratch);
#else
  FlushICache(lis_location, 2 * kInstrSize, scratch);
#endif
}


// This code assumes a FIXED_SEQUENCE for lis/ori
void MacroAssembler::GetRelocatedValueLocation(Register lis_location,
                                               Register result,
                                               Register scratch) {
  lwz(result, MemOperand(lis_location));
  if (emit_debug_code()) {
    And(result, result, Operand(kOpcodeMask | (0x1f * B16)));
    Cmpi(result, Operand(ADDIS), r0);
    Check(eq, kTheInstructionShouldBeALis);
    lwz(result, MemOperand(lis_location));
  }

  // result now holds a lis instruction. Extract the immediate.
  slwi(result, result, Operand(16));

  lwz(scratch, MemOperand(lis_location, kInstrSize));
  if (emit_debug_code()) {
    And(scratch, scratch, Operand(kOpcodeMask));
    Cmpi(scratch, Operand(ORI), r0);
    Check(eq, kTheInstructionShouldBeAnOri);
    lwz(scratch, MemOperand(lis_location, kInstrSize));
  }
  // Copy the low 16bits from ori instruction into result
  rlwimi(result, scratch, 0, 16, 31);

#if V8_TARGET_ARCH_PPC64
  if (emit_debug_code()) {
    lwz(scratch, MemOperand(lis_location, 2*kInstrSize));
    // scratch is now sldi.
    And(scratch, scratch, Operand(kOpcodeMask|kExt5OpcodeMask));
    Cmpi(scratch, Operand(EXT5|RLDICR), r0);
    Check(eq, kTheInstructionShouldBeASldi);
  }

  lwz(scratch, MemOperand(lis_location, 3*kInstrSize));
  // scratch is now ori.
  if (emit_debug_code()) {
    And(scratch, scratch, Operand(kOpcodeMask));
    Cmpi(scratch, Operand(ORIS), r0);
    Check(eq, kTheInstructionShouldBeAnOris);
    lwz(scratch, MemOperand(lis_location, 3*kInstrSize));
  }
  sldi(result, result, Operand(16));
  rldimi(result, scratch, 0, 48);

  lwz(scratch, MemOperand(lis_location, 4*kInstrSize));
  // scratch is now ori.
  if (emit_debug_code()) {
    And(scratch, scratch, Operand(kOpcodeMask));
    Cmpi(scratch, Operand(ORI), r0);
    Check(eq, kTheInstructionShouldBeAnOri);
    lwz(scratch, MemOperand(lis_location, 4*kInstrSize));
  }
  sldi(result, result, Operand(16));
  rldimi(result, scratch, 0, 48);
#endif
}


void MacroAssembler::CheckPageFlag(
    Register object,
    Register scratch,  // scratch may be same register as object
    int mask,
    Condition cc,
    Label* condition_met) {
  ASSERT(cc == ne || cc == eq);
  ClearRightImm(scratch, object, Operand(kPageSizeBits));
  LoadP(scratch, MemOperand(scratch, MemoryChunk::kFlagsOffset));

  And(r0, scratch, Operand(mask), SetRC);

  if (cc == ne) {
    bne(condition_met, cr0);
  }
  if (cc == eq) {
    beq(condition_met, cr0);
  }
}


void MacroAssembler::CheckMapDeprecated(Handle<Map> map,
                                        Register scratch,
                                        Label* if_deprecated) {
  if (map->CanBeDeprecated()) {
    mov(scratch, Operand(map));
    LoadP(scratch, FieldMemOperand(scratch, Map::kBitField3Offset));
    LoadSmiLiteral(r0, Smi::FromInt(Map::Deprecated::kMask));
    and_(scratch, scratch, r0, SetRC);
    bne(if_deprecated, cr0);
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
  // Test the first bit
  and_(r0, ip, mask_scratch, SetRC);
  b(first_bit == 1 ? eq : ne, &other_color, cr0);
  // Shift left 1
  // May need to load the next cell
  slwi(mask_scratch, mask_scratch, Operand(1), SetRC);
  beq(&word_boundary, cr0);
  // Test the second bit
  and_(r0, ip, mask_scratch, SetRC);
  b(second_bit == 1 ? ne : eq, has_color, cr0);
  b(&other_color);

  bind(&word_boundary);
  lwz(ip, MemOperand(bitmap_scratch,
                     MemoryChunk::kHeaderSize + kIntSize));
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
  LoadP(scratch, FieldMemOperand(value, HeapObject::kMapOffset));
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
  const int kLowBits = kPointerSizeLog2 + Bitmap::kBitsPerCellLog2;
  ExtractBitRange(mask_reg, addr_reg,
                  kLowBits - 1,
                  kPointerSizeLog2);
  ExtractBitRange(ip, addr_reg,
                  kPageSizeBits - 1,
                  kLowBits);
  ShiftLeftImm(ip, ip, Operand(Bitmap::kBytesPerCellLog2));
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
#if V8_TARGET_ARCH_PPC64
  Label length_computed;
#endif


  // Check for heap-number
  LoadP(map, FieldMemOperand(value, HeapObject::kMapOffset));
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
  // For ASCII (char-size of 1) we untag the smi to get the length.
  // For UC16 (char-size of 2):
  //   - (32-bit) we just leave the smi tag in place, thereby getting
  //              the length multiplied by 2.
  //   - (64-bit) we compute the offset in the 2-byte array
  ASSERT(kOneByteStringTag == 4 && kStringEncodingMask == 4);
  LoadP(ip, FieldMemOperand(value, String::kLengthOffset));
  andi(r0, instance_type, Operand(kStringEncodingMask));
  beq(&is_encoded, cr0);
  SmiUntag(ip);
#if V8_TARGET_ARCH_PPC64
  b(&length_computed);
#endif
  bind(&is_encoded);
#if V8_TARGET_ARCH_PPC64
  SmiToShortArrayOffset(ip, ip);
  bind(&length_computed);
#else
  ASSERT(kSmiShift == 1);
#endif
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


// Saturate a value into 8-bit unsigned integer
//   if input_value < 0, output_value is 0
//   if input_value > 255, output_value is 255
//   otherwise output_value is the input_value
void MacroAssembler::ClampUint8(Register output_reg, Register input_reg) {
  Label done, negative_label, overflow_label;
  int satval = (1 << 8) - 1;

  cmpi(input_reg, Operand::Zero());
  blt(&negative_label);

  cmpi(input_reg, Operand(satval));
  bgt(&overflow_label);
  if (!output_reg.is(input_reg)) {
    mr(output_reg, input_reg);
  }
  b(&done);

  bind(&negative_label);
  li(output_reg, Operand::Zero());  // set to 0 if negative
  b(&done);


  bind(&overflow_label);  // set to satval if > satval
  li(output_reg, Operand(satval));

  bind(&done);
}


void MacroAssembler::SetRoundingMode(FPRoundingMode RN) {
  mtfsfi(7, RN);
}


void MacroAssembler::ResetRoundingMode() {
  mtfsfi(7, kRoundToNearest);  // reset (default is kRoundToNearest)
}


void MacroAssembler::ClampDoubleToUint8(Register result_reg,
                                        DoubleRegister input_reg,
                                        DoubleRegister double_scratch) {
  Label above_zero;
  Label done;
  Label in_bounds;

  LoadDoubleLiteral(double_scratch, 0.0, result_reg);
  fcmpu(input_reg, double_scratch);
  bgt(&above_zero);

  // Double value is less than zero, NaN or Inf, return 0.
  LoadIntLiteral(result_reg, 0);
  b(&done);

  // Double value is >= 255, return 255.
  bind(&above_zero);
  LoadDoubleLiteral(double_scratch, 255.0, result_reg);
  fcmpu(input_reg, double_scratch);
  ble(&in_bounds);
  LoadIntLiteral(result_reg, 255);
  b(&done);

  // In 0-255 range, round and truncate.
  bind(&in_bounds);

  // round to nearest (default rounding mode)
  fctiw(double_scratch, input_reg);

  // reserve a slot on the stack
  stfdu(double_scratch, MemOperand(sp, -8));
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
  lwz(result_reg, MemOperand(sp));
#else
  lwz(result_reg, MemOperand(sp, 4));
#endif
  // restore the stack
  addi(sp, sp, Operand(8));

  bind(&done);
}


void MacroAssembler::LoadInstanceDescriptors(Register map,
                                             Register descriptors) {
  LoadP(descriptors, FieldMemOperand(map, Map::kDescriptorsOffset));
}


void MacroAssembler::NumberOfOwnDescriptors(Register dst, Register map) {
  LoadP(dst, FieldMemOperand(map, Map::kBitField3Offset));
  DecodeField<Map::NumberOfOwnDescriptorsBits>(dst);
}


void MacroAssembler::EnumLength(Register dst, Register map) {
  STATIC_ASSERT(Map::EnumLengthBits::kShift == 0);
  LoadP(dst, FieldMemOperand(map, Map::kBitField3Offset));
  LoadSmiLiteral(r0, Smi::FromInt(Map::EnumLengthBits::kMask));
  and_(dst, dst, r0);
}


void MacroAssembler::CheckEnumCache(Register null_value, Label* call_runtime) {
  Register  empty_fixed_array_value = r9;
  LoadRoot(empty_fixed_array_value, Heap::kEmptyFixedArrayRootIndex);
  Label next, start;
  mr(r5, r3);

  // Check if the enum length field is properly initialized, indicating that
  // there is an enum cache.
  LoadP(r4, FieldMemOperand(r5, HeapObject::kMapOffset));

  EnumLength(r6, r4);
  CmpSmiLiteral(r6, Smi::FromInt(Map::kInvalidEnumCache), r0);
  beq(call_runtime);

  b(&start);

  bind(&next);
  LoadP(r4, FieldMemOperand(r5, HeapObject::kMapOffset));

  // For all objects but the receiver, check that the cache is empty.
  EnumLength(r6, r4);
  CmpSmiLiteral(r6, Smi::FromInt(0), r0);
  bne(call_runtime);

  bind(&start);

  // Check that there are no elements. Register r5 contains the current JS
  // object we've reached through the prototype chain.
  LoadP(r5, FieldMemOperand(r5, JSObject::kElementsOffset));
  cmp(r5, empty_fixed_array_value);
  bne(call_runtime);

  LoadP(r5, FieldMemOperand(r4, Map::kPrototypeOffset));
  cmp(r5, null_value);
  bne(&next);
}


////////////////////////////////////////////////////////////////////////////////
//
// New MacroAssembler Interfaces added for PPC
//
////////////////////////////////////////////////////////////////////////////////
void MacroAssembler::LoadIntLiteral(Register dst, int value) {
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


void MacroAssembler::LoadSmiLiteral(Register dst, Smi *smi) {
  intptr_t value = reinterpret_cast<intptr_t>(smi);
#if V8_TARGET_ARCH_PPC64
  ASSERT((value & 0xffffffff) == 0);
  LoadIntLiteral(dst, value >> 32);
  ShiftLeftImm(dst, dst, Operand(32));
#else
  LoadIntLiteral(dst, value);
#endif
}


void MacroAssembler::LoadDoubleLiteral(DoubleRegister result,
                                       double value,
                                       Register scratch) {
  addi(sp, sp, Operand(-8));  // reserve 1 temp double on the stack

  // avoid gcc strict aliasing error using union cast
  union {
    double dval;
#if V8_TARGET_ARCH_PPC64
    intptr_t ival;
#else
    intptr_t ival[2];
#endif
  } litVal;

  litVal.dval = value;
#if V8_TARGET_ARCH_PPC64
  mov(scratch, Operand(litVal.ival));
  std(scratch, MemOperand(sp));
#else
  LoadIntLiteral(scratch, litVal.ival[0]);
  stw(scratch, MemOperand(sp, 0));
  LoadIntLiteral(scratch, litVal.ival[1]);
  stw(scratch, MemOperand(sp, 4));
#endif
  lfd(result, MemOperand(sp, 0));

  addi(sp, sp, Operand(8));  // restore the stack ptr
}


void MacroAssembler::Add(Register dst, Register src,
                         intptr_t value, Register scratch) {
  if (is_int16(value)) {
    addi(dst, src, Operand(value));
  } else {
    mov(scratch, Operand(value));
    add(dst, src, scratch);
  }
}


void MacroAssembler::Cmpi(Register src1, const Operand& src2, Register scratch,
                          CRegister cr) {
  intptr_t value = src2.immediate();
  if (is_int16(value)) {
    cmpi(src1, src2, cr);
  } else {
    mov(scratch, src2);
    cmp(src1, scratch, cr);
  }
}


void MacroAssembler::Cmpli(Register src1, const Operand& src2, Register scratch,
                           CRegister cr) {
  intptr_t value = src2.immediate();
  if (is_uint16(value)) {
    cmpli(src1, src2, cr);
  } else {
    mov(scratch, src2);
    cmpl(src1, scratch, cr);
  }
}


void MacroAssembler::And(Register ra, Register rs, const Operand& rb,
                         RCBit rc) {
  if (rb.is_reg()) {
    and_(ra, rs, rb.rm(), rc);
  } else {
    if (is_uint16(rb.imm_) && RelocInfo::IsNone(rb.rmode_) && rc == SetRC) {
      andi(ra, rs, rb);
    } else {
      // mov handles the relocation.
      ASSERT(!rs.is(r0));
      mov(r0, rb);
      and_(ra, rs, r0, rc);
    }
  }
}


void MacroAssembler::Or(Register ra, Register rs, const Operand& rb, RCBit rc) {
  if (rb.is_reg()) {
    orx(ra, rs, rb.rm(), rc);
  } else {
    if (is_uint16(rb.imm_) && RelocInfo::IsNone(rb.rmode_) && rc == LeaveRC) {
      ori(ra, rs, rb);
    } else {
      // mov handles the relocation.
      ASSERT(!rs.is(r0));
      mov(r0, rb);
      orx(ra, rs, r0, rc);
    }
  }
}


void MacroAssembler::Xor(Register ra, Register rs, const Operand& rb,
                         RCBit rc) {
  if (rb.is_reg()) {
    xor_(ra, rs, rb.rm(), rc);
  } else {
    if (is_uint16(rb.imm_) && RelocInfo::IsNone(rb.rmode_) && rc == LeaveRC) {
      xori(ra, rs, rb);
    } else {
      // mov handles the relocation.
      ASSERT(!rs.is(r0));
      mov(r0, rb);
      xor_(ra, rs, r0, rc);
    }
  }
}


void MacroAssembler::CmpSmiLiteral(Register src1, Smi *smi, Register scratch,
                                   CRegister cr) {
#if V8_TARGET_ARCH_PPC64
  LoadSmiLiteral(scratch, smi);
  cmp(src1, scratch, cr);
#else
  Cmpi(src1, Operand(smi), scratch, cr);
#endif
}


void MacroAssembler::CmplSmiLiteral(Register src1, Smi *smi, Register scratch,
                                   CRegister cr) {
#if V8_TARGET_ARCH_PPC64
  LoadSmiLiteral(scratch, smi);
  cmpl(src1, scratch, cr);
#else
  Cmpli(src1, Operand(smi), scratch, cr);
#endif
}


void MacroAssembler::AddSmiLiteral(Register dst, Register src, Smi *smi,
                                   Register scratch) {
#if V8_TARGET_ARCH_PPC64
  LoadSmiLiteral(scratch, smi);
  add(dst, src, scratch);
#else
  Add(dst, src, reinterpret_cast<intptr_t>(smi), scratch);
#endif
}


void MacroAssembler::SubSmiLiteral(Register dst, Register src, Smi *smi,
                                   Register scratch) {
#if V8_TARGET_ARCH_PPC64
  LoadSmiLiteral(scratch, smi);
  sub(dst, src, scratch);
#else
  Add(dst, src, -(reinterpret_cast<intptr_t>(smi)), scratch);
#endif
}


void MacroAssembler::AndSmiLiteral(Register dst, Register src, Smi *smi,
                                   Register scratch, RCBit rc) {
#if V8_TARGET_ARCH_PPC64
  LoadSmiLiteral(scratch, smi);
  and_(dst, src, scratch, rc);
#else
  And(dst, src, Operand(smi), rc);
#endif
}


// Load a "pointer" sized value from the memory location
void MacroAssembler::LoadP(Register dst, const MemOperand& mem,
                           Register scratch) {
  int offset = mem.offset();

  if (!scratch.is(no_reg) && !is_int16(offset)) {
    /* cannot use d-form */
    LoadIntLiteral(scratch, offset);
#if V8_TARGET_ARCH_PPC64
    ldx(dst, MemOperand(mem.ra(), scratch));
#else
    lwzx(dst, MemOperand(mem.ra(), scratch));
#endif
  } else {
#if V8_TARGET_ARCH_PPC64
    int misaligned = (offset & 3);
    if (misaligned) {
      // adjust base to conform to offset alignment requirements
      // Todo: enhance to use scratch if dst is unsuitable
      ASSERT(!dst.is(r0));
      addi(dst, mem.ra(), Operand((offset & 3) - 4));
      ld(dst, MemOperand(dst, (offset & ~3) + 4));
    } else {
      ld(dst, mem);
    }
#else
    lwz(dst, mem);
#endif
  }
}


// Store a "pointer" sized value to the memory location
void MacroAssembler::StoreP(Register src, const MemOperand& mem,
                            Register scratch) {
  int offset = mem.offset();

  if (!scratch.is(no_reg) && !is_int16(offset)) {
    /* cannot use d-form */
    LoadIntLiteral(scratch, offset);
#if V8_TARGET_ARCH_PPC64
    stdx(src, MemOperand(mem.ra(), scratch));
#else
    stwx(src, MemOperand(mem.ra(), scratch));
#endif
  } else {
#if V8_TARGET_ARCH_PPC64
    int misaligned = (offset & 3);
    if (misaligned) {
      // adjust base to conform to offset alignment requirements
      // a suitable scratch is required here
      ASSERT(!scratch.is(no_reg));
      if (scratch.is(r0)) {
        LoadIntLiteral(scratch, offset);
        stdx(src, MemOperand(mem.ra(), scratch));
      } else {
        addi(scratch, mem.ra(), Operand((offset & 3) - 4));
        std(src, MemOperand(scratch, (offset & ~3) + 4));
      }
    } else {
      std(src, mem);
    }
#else
    stw(src, mem);
#endif
  }
}

void MacroAssembler::LoadWordArith(Register dst, const MemOperand& mem,
                                   Register scratch) {
  int offset = mem.offset();

  if (!scratch.is(no_reg) && !is_int16(offset)) {
    /* cannot use d-form */
    LoadIntLiteral(scratch, offset);
#if V8_TARGET_ARCH_PPC64
    // lwax(dst, MemOperand(mem.ra(), scratch));
    ASSERT(0);  // lwax not yet implemented
#else
    lwzx(dst, MemOperand(mem.ra(), scratch));
#endif
  } else {
#if V8_TARGET_ARCH_PPC64
    int misaligned = (offset & 3);
    if (misaligned) {
      // adjust base to conform to offset alignment requirements
      // Todo: enhance to use scratch if dst is unsuitable
      ASSERT(!dst.is(r0));
      addi(dst, mem.ra(), Operand((offset & 3) - 4));
      lwa(dst, MemOperand(dst, (offset & ~3) + 4));
    } else {
      lwa(dst, mem);
    }
#else
    lwz(dst, mem);
#endif
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
    LoadIntLiteral(scratch, offset);
  }

  if (!updateForm) {
    if (use_dform) {
      lwz(dst, mem);
    } else {
      lwzx(dst, MemOperand(base, scratch));
    }
  } else {
    if (use_dform) {
      lwzu(dst, mem);
    } else {
      lwzux(dst, MemOperand(base, scratch));
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
    LoadIntLiteral(scratch, offset);
  }

  if (!updateForm) {
    if (use_dform) {
      stw(src, mem);
    } else {
      stwx(src, MemOperand(base, scratch));
    }
  } else {
    if (use_dform) {
      stwu(src, mem);
    } else {
      stwux(src, MemOperand(base, scratch));
    }
  }
}


// Variable length depending on whether offset fits into immediate field
// MemOperand currently only supports d-form
void MacroAssembler::LoadHalfWord(Register dst, const MemOperand& mem,
                                  Register scratch, bool updateForm) {
  Register base = mem.ra();
  int offset = mem.offset();

  bool use_dform = true;
  if (!is_int16(offset)) {
    use_dform = false;
    LoadIntLiteral(scratch, offset);
  }

  if (!updateForm) {
    if (use_dform) {
      lhz(dst, mem);
    } else {
      lhzx(dst, MemOperand(base, scratch));
    }
  } else {
    // If updateForm is ever true, then lhzu will
    // need to be implemented
    assert(0);
#if 0  // LoadHalfWord w\ update not yet needed
    if (use_dform) {
      lhzu(dst, mem);
    } else {
      lhzux(dst, MemOperand(base, scratch));
    }
#endif
  }
}


// Variable length depending on whether offset fits into immediate field
// MemOperand current only supports d-form
void MacroAssembler::StoreHalfWord(Register src, const MemOperand& mem,
                                   Register scratch, bool updateForm) {
  Register base = mem.ra();
  int offset = mem.offset();

  bool use_dform = true;
  if (!is_int16(offset)) {
    use_dform = false;
    LoadIntLiteral(scratch, offset);
  }

  if (!updateForm) {
    if (use_dform) {
      sth(src, mem);
    } else {
      sthx(src, MemOperand(base, scratch));
    }
  } else {
    // If updateForm is ever true, then sthu will
    // need to be implemented
    assert(0);
#if 0  // StoreHalfWord w\ update not yet needed
    if (use_dform) {
      sthu(src, mem);
    } else {
      sthux(src, MemOperand(base, scratch));
    }
#endif
  }
}


// Variable length depending on whether offset fits into immediate field
// MemOperand currently only supports d-form
void MacroAssembler::LoadByte(Register dst, const MemOperand& mem,
                              Register scratch, bool updateForm) {
  Register base = mem.ra();
  int offset = mem.offset();

  bool use_dform = true;
  if (!is_int16(offset)) {
    use_dform = false;
    LoadIntLiteral(scratch, offset);
  }

  if (!updateForm) {
    if (use_dform) {
      lbz(dst, mem);
    } else {
      lbzx(dst, MemOperand(base, scratch));
    }
  } else {
    // If updateForm is ever true, then lbzu will
    // need to be implemented
    assert(0);
#if 0  // LoadByte w\ update not yet needed
    if (use_dform) {
      lbzu(dst, mem);
    } else {
      lbzux(dst, MemOperand(base, scratch));
    }
#endif
  }
}


// Variable length depending on whether offset fits into immediate field
// MemOperand current only supports d-form
void MacroAssembler::StoreByte(Register src, const MemOperand& mem,
                               Register scratch, bool updateForm) {
  Register base = mem.ra();
  int offset = mem.offset();

  bool use_dform = true;
  if (!is_int16(offset)) {
    use_dform = false;
    LoadIntLiteral(scratch, offset);
  }

  if (!updateForm) {
    if (use_dform) {
      stb(src, mem);
    } else {
      stbx(src, MemOperand(base, scratch));
    }
  } else {
    // If updateForm is ever true, then stbu will
    // need to be implemented
    assert(0);
#if 0  // StoreByte w\ update not yet needed
    if (use_dform) {
      stbu(src, mem);
    } else {
      stbux(src, MemOperand(base, scratch));
    }
#endif
  }
}


void MacroAssembler::LoadRepresentation(Register dst,
                                        const MemOperand& mem,
                                        Representation r,
                                        Register scratch) {
  ASSERT(!r.IsDouble());
  if (r.IsByte()) {
    lbz(dst, mem);
#if V8_TARGET_ARCH_PPC64
  } else if (r.IsInteger32()) {
    lwz(dst, mem);
#endif
  } else {
    LoadP(dst, mem, scratch);
  }
}


void MacroAssembler::StoreRepresentation(Register src,
                                         const MemOperand& mem,
                                         Representation r,
                                         Register scratch) {
  ASSERT(!r.IsDouble());
  if (r.IsByte()) {
    stb(src, mem);
#if V8_TARGET_ARCH_PPC64
  } else if (r.IsInteger32()) {
    stw(src, mem);
#endif
  } else {
    StoreP(src, mem, scratch);
  }
}


void MacroAssembler::TestJSArrayForAllocationMemento(
    Register receiver_reg,
    Register scratch_reg,
    Label* no_memento_found) {
  ExternalReference new_space_start =
      ExternalReference::new_space_start(isolate());
  ExternalReference new_space_allocation_top =
      ExternalReference::new_space_allocation_top_address(isolate());
  addi(scratch_reg, receiver_reg,
       Operand(JSArray::kSize + AllocationMemento::kSize - kHeapObjectTag));
  Cmpi(scratch_reg, Operand(new_space_start), r0);
  blt(no_memento_found);
  mov(ip, Operand(new_space_allocation_top));
  LoadP(ip, MemOperand(ip));
  cmp(scratch_reg, ip);
  bgt(no_memento_found);
  LoadP(scratch_reg, MemOperand(scratch_reg, -AllocationMemento::kSize));
  Cmpi(scratch_reg,
       Operand(isolate()->factory()->allocation_memento_map()), r0);
}


Register GetRegisterThatIsNotOneOf(Register reg1,
                                   Register reg2,
                                   Register reg3,
                                   Register reg4,
                                   Register reg5,
                                   Register reg6) {
  RegList regs = 0;
  if (reg1.is_valid()) regs |= reg1.bit();
  if (reg2.is_valid()) regs |= reg2.bit();
  if (reg3.is_valid()) regs |= reg3.bit();
  if (reg4.is_valid()) regs |= reg4.bit();
  if (reg5.is_valid()) regs |= reg5.bit();
  if (reg6.is_valid()) regs |= reg6.bit();

  for (int i = 0; i < Register::NumAllocatableRegisters(); i++) {
    Register candidate = Register::FromAllocationIndex(i);
    if (regs & candidate.bit()) continue;
    return candidate;
  }
  UNREACHABLE();
  return no_reg;
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


CodePatcher::CodePatcher(byte* address,
                         int instructions,
                         FlushICache flush_cache)
    : address_(address),
      size_(instructions * Assembler::kInstrSize),
      masm_(NULL, address, size_ + Assembler::kGap),
      flush_cache_(flush_cache) {
  // Create a new macro assembler pointing to the address of the code to patch.
  // The size is adjusted with kGap on order for the assembler to generate size
  // bytes of instructions without failing with buffer size constraints.
  ASSERT(masm_.reloc_info_writer.pos() == address_ + size_ + Assembler::kGap);
}


CodePatcher::~CodePatcher() {
  // Indicate that code has changed.
  if (flush_cache_ == FLUSH) {
    CPU::FlushICache(address_, size_);
  }

  // Check that the code was patched as expected.
  ASSERT(masm_.pc_ == address_ + size_);
  ASSERT(masm_.reloc_info_writer.pos() == address_ + size_ + Assembler::kGap);
}


void CodePatcher::Emit(Instr instr) {
  masm()->emit(instr);
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
