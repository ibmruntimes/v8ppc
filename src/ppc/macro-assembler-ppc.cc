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
                          Condition cond, CRegister cr) {
  Label skip;

  if (cond != al) b(NegateCondition(cond), &skip, cr);

  ASSERT(rmode == RelocInfo::CODE_TARGET ||
         rmode == RelocInfo::RUNTIME_ENTRY);

  mov(r0, Operand(target, rmode));
  mtctr(r0);
  bcr();

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
  mov(dst, Operand(value));
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


void MacroAssembler::LoadHeapObject(Register result,
                                    Handle<HeapObject> object) {
  if (isolate()->heap()->InNewSpace(*object)) {
    Handle<JSGlobalPropertyCell> cell =
        isolate()->factory()->NewJSGlobalPropertyCell(object);
    mov(result, Operand(cell));
    LoadP(result, FieldMemOperand(result, JSGlobalPropertyCell::kValueOffset));
  } else {
    mov(result, Operand(object));
  }
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
  // The compiled code assumes that record write doesn't change the
  // context register, so we check that none of the clobbered
  // registers are cp.
  ASSERT(!address.is(cp) && !value.is(cp));

  if (emit_debug_code()) {
    LoadP(ip, MemOperand(address));
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
  int doubles_size = DwVfpRegister::kNumAllocatableRegisters * kDoubleSize;
  int register_offset = SafepointRegisterStackIndex(reg.code()) * kPointerSize;
  return MemOperand(sp, doubles_size + register_offset);
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
    const int kNumRegs = DwVfpRegister::kNumVolatileRegisters;
    subi(sp, sp, Operand(kNumRegs * kDoubleSize));
    for (int i = 0; i < kNumRegs; i++) {
      DwVfpRegister reg = DwVfpRegister::from_code(i);
      stfd(reg, MemOperand(sp, i * kDoubleSize));
    }
    // Note that d0 will be accessible at
    //   fp - 2 * kPointerSize - kNumVolatileRegisters * kDoubleSize,
    // since the sp slot and code slot were pushed after the fp.
  }

  // Allocate and align the frame preparing for calling the runtime
  // function.
  stack_space += kNumRequiredStackFrameSlots;
  subi(sp, sp, Operand(stack_space * kPointerSize));
  const int frame_alignment = MacroAssembler::ActivationFrameAlignment();
  if (frame_alignment > 0) {
    ASSERT(frame_alignment == 8);
    ClearRightImm(sp, sp, Operand(3));  // equivalent to &= -8
  }

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
                                    Register argument_count) {
  // Optionally restore all double registers.
  if (save_doubles) {
    // Calculate the stack location of the saved doubles and restore them.
    const int kNumRegs = DwVfpRegister::kNumVolatileRegisters;
    const int offset = (2 * kPointerSize + kNumRegs * kDoubleSize);
    addi(r6, fp, Operand(-offset));
    for (int i = 0; i < kNumRegs; i++) {
      DwVfpRegister reg = DwVfpRegister::from_code(i);
      lfd(reg, MemOperand(r6, i * kDoubleSize));
    }
  }

  // Clear top frame.
  li(r6, Operand(0, RelocInfo::NONE));
  mov(ip, Operand(ExternalReference(Isolate::kCEntryFPAddress, isolate())));
  StoreP(r6, MemOperand(ip));

  // Restore current context from top and clear it in debug mode.
  mov(ip, Operand(ExternalReference(Isolate::kContextAddress, isolate())));
  LoadP(cp, MemOperand(ip));
#ifdef DEBUG
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
                                    const ParameterCount& actual,
                                    InvokeFlag flag,
                                    const CallWrapper& call_wrapper,
                                    CallKind call_kind) {
  // You can't call a function without a valid frame.
  ASSERT(flag == JUMP_FUNCTION || has_frame());

  // Get the function and setup the context.
  LoadHeapObject(r4, function);
  LoadP(cp, FieldMemOperand(r4, JSFunction::kContextOffset));

  ParameterCount expected(function->shared()->formal_parameter_count());
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
  STATIC_ASSERT(StackHandlerConstants::kStateSlot == 2 * kPointerSize);
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
    li(r8, Operand(0, RelocInfo::NONE));  // NULL frame pointer.
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
  StoreP(r8, MemOperand(sp, StackHandlerConstants::kStateSlot));
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
  bcr();
}


void MacroAssembler::Throw(Register value) {
  // Adjust this code if not the case.
  STATIC_ASSERT(StackHandlerConstants::kSize == 5 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kNextOffset == 0);
  STATIC_ASSERT(StackHandlerConstants::kCodeOffset == 1 * kPointerSize);
  STATIC_ASSERT(StackHandlerConstants::kStateSlot == 2 * kPointerSize);
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
  STATIC_ASSERT(StackHandlerConstants::kStateSlot == 2 * kPointerSize);
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
  LoadP(r5, MemOperand(sp, StackHandlerConstants::kStateSlot));
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
  cmpi(scratch, Operand(0, RelocInfo::NONE));
  Check(ne, "we should not have an empty lexical context");
#endif

  // Load the native context of the current context.
  int offset =
      Context::kHeaderSize + Context::GLOBAL_OBJECT_INDEX * kPointerSize;
  LoadP(scratch, FieldMemOperand(scratch, offset));
  LoadP(scratch, FieldMemOperand(scratch, GlobalObject::kNativeContextOffset));

  // Check the context is a native context.
  if (emit_debug_code()) {
    // TODO(119): avoid push(holder_reg)/pop(holder_reg)
    // Cannot use ip as a temporary in this verification code. Due to the fact
    // that ip is clobbered as part of cmp with an object Operand.
    push(holder_reg);  // Temporarily save holder on the stack.
    // Read the first word and compare to the native_context_map.
    LoadP(holder_reg, FieldMemOperand(scratch, HeapObject::kMapOffset));
    LoadRoot(ip, Heap::kNativeContextMapRootIndex);
    cmp(holder_reg, ip);
    Check(eq, "JSGlobalObject::native_context should be a native context.");
    pop(holder_reg);  // Restore holder.
  }

  // Check if both contexts are the same.
  LoadP(ip, FieldMemOperand(holder_reg, JSGlobalProxy::kNativeContextOffset));
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

    LoadP(holder_reg, FieldMemOperand(holder_reg, HeapObject::kMapOffset));
    LoadRoot(ip, Heap::kNativeContextMapRootIndex);
    cmp(holder_reg, ip);
    Check(eq, "JSGlobalObject::native_context should be a native context.");
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
    LoadP(result, MemOperand(topaddr));
    LoadP(ip, MemOperand(topaddr, kPointerSize));
  } else {
    if (emit_debug_code()) {
      // Assert that result actually contains top on entry. ip is used
      // immediately below so this use of ip does not cause difference with
      // respect to register content between debug and release mode.
      LoadP(ip, MemOperand(topaddr));
      cmp(result, ip);
      Check(eq, "Unexpected allocation top");
    }
    // Load allocation limit into ip. Result already contains allocation top.
    LoadP(ip, MemOperand(topaddr, limit - top), r0);
  }

  // Calculate new top and bail out if new space is exhausted. Use result
  // to calculate the new top.
  li(r0, Operand(-1));
  addc(scratch2, result, obj_size_reg);
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
    LoadP(result, MemOperand(topaddr));
    LoadP(ip, MemOperand(topaddr, kPointerSize));
  } else {
    if (emit_debug_code()) {
      // Assert that result actually contains top on entry. ip is used
      // immediately below so this use of ip does not cause difference with
      // respect to register content between debug and release mode.
      LoadP(ip, MemOperand(topaddr));
      cmp(result, ip);
      Check(eq, "Unexpected allocation top");
    }
    // Load allocation limit into ip. Result already contains allocation top.
    LoadP(ip, MemOperand(topaddr, limit - top));
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
    Check(eq, "Unaligned allocation in new space", cr0);
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
  Check(lt, "Undo allocation of non allocated memory");
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
#if V8_TARGET_ARCH_PPC64
  Register double_reg = scratch2;
#else
  Register mantissa_reg = scratch2;
  Register exponent_reg = scratch3;
#endif

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
#if V8_TARGET_ARCH_PPC64
  mov(scratch1, Operand(kLastNonNaNInt64));
  addi(scratch3, value_reg, Operand(-kHeapObjectTag));
  ld(double_reg, MemOperand(scratch3, HeapNumber::kValueOffset));
  cmp(double_reg, scratch1);
#else
  mov(scratch1, Operand(kNaNOrInfinityLowerBoundUpper32));
  lwz(exponent_reg, FieldMemOperand(value_reg, HeapNumber::kExponentOffset));
  cmp(exponent_reg, scratch1);
#endif
  bge(&maybe_nan);

#if !V8_TARGET_ARCH_PPC64
  lwz(mantissa_reg, FieldMemOperand(value_reg, HeapNumber::kMantissaOffset));
#endif

  bind(&have_double_value);
  SmiToDoubleArrayOffset(scratch1, key_reg);
  add(scratch1, elements_reg, scratch1);
#if V8_TARGET_ARCH_PPC64
  addi(scratch1, scratch1, Operand(-kHeapObjectTag));
  std(double_reg, MemOperand(scratch1, FixedDoubleArray::kHeaderSize));
#else
#if __BYTE_ORDER == __LITTLE_ENDIAN
  stw(mantissa_reg, FieldMemOperand(scratch1, FixedDoubleArray::kHeaderSize));
  uint32_t offset = FixedDoubleArray::kHeaderSize + sizeof(kHoleNanLower32);
  stw(exponent_reg, FieldMemOperand(scratch1, offset));
#elif __BYTE_ORDER == __BIG_ENDIAN
  stw(exponent_reg, FieldMemOperand(scratch1, FixedDoubleArray::kHeaderSize));
  uint32_t offset = FixedDoubleArray::kHeaderSize + sizeof(kHoleNanLower32);
  stw(mantissa_reg, FieldMemOperand(scratch1, offset));
#endif
#endif
  b(&done);

  bind(&maybe_nan);
  // Could be NaN or Infinity. If fraction is not zero, it's NaN, otherwise
  // it's an Infinity, and the non-NaN code path applies.
  bgt(&is_nan);
#if V8_TARGET_ARCH_PPC64
  clrldi(r0, double_reg, Operand(32), SetRC);
  beq(&have_double_value, cr0);
#else
  lwz(mantissa_reg, FieldMemOperand(value_reg, HeapNumber::kMantissaOffset));
  cmpi(mantissa_reg, Operand::Zero());
  beq(&have_double_value);
#endif
  bind(&is_nan);
  // Load canonical NaN for storing into the double array.
  uint64_t nan_int64 = BitCast<uint64_t>(
      FixedDoubleArray::canonical_not_the_hole_nan_as_double());
#if V8_TARGET_ARCH_PPC64
  mov(double_reg, Operand(nan_int64));
#else
  mov(mantissa_reg, Operand(static_cast<intptr_t>(nan_int64)));
  mov(exponent_reg, Operand(static_cast<intptr_t>(nan_int64 >> 32)));
#endif
  b(&have_double_value);

  bind(&smi_value);
  addi(scratch1, elements_reg,
       Operand(FixedDoubleArray::kHeaderSize - kHeapObjectTag));
  SmiToDoubleArrayOffset(scratch4, key_reg);
  add(scratch1, scratch1, scratch4);
  // scratch1 is now effective address of the double element

  Register untagged_value = elements_reg;
  SmiUntag(untagged_value, value_reg);
  FloatingPointHelper::ConvertIntToDouble(this,
                                          untagged_value,
                                          d0);
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
                                Label* early_success,
                                CompareMapMode mode) {
  LoadP(scratch, FieldMemOperand(obj, HeapObject::kMapOffset));
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
                                              int stack_space) {
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
  LoadP(r27, MemOperand(r26, kNextOffset));
  LoadP(r28, MemOperand(r26, kLimitOffset));
  lwz(r29, MemOperand(r26, kLevelOffset));
  addi(r29, r29, Operand(1));
  stw(r29, MemOperand(r26, kLevelOffset));

#if !ABI_RETURNS_HANDLES_IN_REGS
  // PPC LINUX ABI
  // The return value is pointer-sized non-scalar value.
  // Space has already been allocated on the stack which will pass as an
  // implicity first argument.
  addi(r3, sp, Operand((kStackFrameExtraParamSlot + 1) * kPointerSize));
#endif

  // Native call returns to the DirectCEntry stub which redirects to the
  // return address pushed on stack (could have moved after GC).
  // DirectCEntry stub itself is generated early and never moves.
  DirectCEntryStub stub;
  stub.GenerateCall(this, function);

#if !ABI_RETURNS_HANDLES_IN_REGS
  // Retrieve return value from stack buffer
  LoadP(r3, MemOperand(r3));
#endif

  Label promote_scheduled_exception;
  Label delete_allocated_handles;
  Label leave_exit_frame;
  Label skip1, skip2;

  // If result is non-zero, dereference to get the result value
  // otherwise set it to undefined.
  cmpi(r3, Operand::Zero());
  bne(&skip1);
  LoadRoot(r3, Heap::kUndefinedValueRootIndex);
  b(&skip2);
  bind(&skip1);
  LoadP(r3, MemOperand(r3));
  bind(&skip2);

  // No more valid handles (the result handle was the last one). Restore
  // previous handle scope.
  StoreP(r27, MemOperand(r26, kNextOffset));
  if (emit_debug_code()) {
    lwz(r4, MemOperand(r26, kLevelOffset));
    cmp(r4, r29);
    Check(eq, "Unexpected level after return from api call");
  }
  subi(r29, r29, Operand(1));
  stw(r29, MemOperand(r26, kLevelOffset));
  LoadP(ip, MemOperand(r26, kLimitOffset));
  cmp(r28, ip);
  bne(&delete_allocated_handles);

  // Check if the function scheduled an exception.
  bind(&leave_exit_frame);
  LoadRoot(r27, Heap::kTheHoleValueRootIndex);
  mov(ip, Operand(ExternalReference::scheduled_exception_address(isolate())));
  LoadP(r28, MemOperand(ip));
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
  StoreP(r28, MemOperand(r26, kLimitOffset));
  mr(r27, r3);
  PrepareCallCFunction(1, r28);
  mov(r3, Operand(ExternalReference::isolate_address()));
  CallCFunction(
      ExternalReference::delete_handle_scope_extensions(isolate()), 1);
  mr(r3, r27);
  b(&leave_exit_frame);
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

void MacroAssembler::SmiToDoubleFPRegister(Register smi,
                                            DwVfpRegister value,
                                            Register scratch1) {
  SmiUntag(scratch1, smi);
  FloatingPointHelper::ConvertIntToDouble(this, scratch1, value);
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
  // Retrieve double from heap
  lfd(double_scratch, FieldMemOperand(source, HeapNumber::kValueOffset));

  // Convert
  fctidz(double_scratch, double_scratch);

  addi(sp, sp, Operand(-kDoubleSize));
  stfd(double_scratch, MemOperand(sp, 0));
#if V8_TARGET_ARCH_PPC64
  ld(dest, MemOperand(sp, 0));
#else
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
  lwz(scratch, MemOperand(sp, 4));
  lwz(dest, MemOperand(sp, 0));
#else
  lwz(scratch, MemOperand(sp, 0));
  lwz(dest, MemOperand(sp, 4));
#endif
#endif
  addi(sp, sp, Operand(kDoubleSize));

  // The result is not a 32-bit integer when the high 33 bits of the
  // result are not identical.
#if V8_TARGET_ARCH_PPC64
  TestIfInt32(dest, scratch, scratch2);
#else
  TestIfInt32(scratch, dest, scratch2);
#endif
  bne(not_int32);
}


void MacroAssembler::EmitVFPTruncate(VFPRoundingMode rounding_mode,
                                     Register result,
                                     DwVfpRegister double_input,
                                     Register scratch,
                                     DwVfpRegister double_scratch,
                                     CheckForInexactConversion check_inexact) {
  // Convert
  if (rounding_mode == kRoundToZero) {
    fctidz(double_scratch, double_input);
  } else {
    SetRoundingMode(rounding_mode);
    fctid(double_scratch, double_input);
    ResetRoundingMode();
  }

  addi(sp, sp, Operand(-kDoubleSize));
  stfd(double_scratch, MemOperand(sp, 0));
#if V8_TARGET_ARCH_PPC64
  ld(result, MemOperand(sp, 0));
#else
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
  lwz(scratch, MemOperand(sp, 4));
  lwz(result, MemOperand(sp, 0));
#else
  lwz(scratch, MemOperand(sp, 0));
  lwz(result, MemOperand(sp, 4));
#endif
#endif
  addi(sp, sp, Operand(kDoubleSize));

  // The result is a 32-bit integer when the high 33 bits of the
  // result are identical.
#if V8_TARGET_ARCH_PPC64
  TestIfInt32(result, scratch, r0);
#else
  TestIfInt32(scratch, result, r0);
#endif

  if (check_inexact == kCheckForInexactConversion) {
    Label done;
    bne(&done);

    // convert back and compare
    fcfid(double_scratch, double_scratch);
    fcmpu(double_scratch, double_input);
    bind(&done);
  }
}


void MacroAssembler::EmitOutOfInt32RangeTruncate(Register result,
                                                 Register input_high,
                                                 Register input_low,
                                                 Register scratch) {
  Label done, high_shift_needed, pos_shift, neg_shift, shift_done;

  li(result, Operand::Zero());

  // check for NaN or +/-Infinity
  // by extracting exponent (mask: 0x7ff00000)
  STATIC_ASSERT(HeapNumber::kExponentMask == 0x7ff00000u);
  ExtractBitMask(scratch, input_high, HeapNumber::kExponentMask);
  cmpli(scratch, Operand(0x7ff));
  beq(&done);

  // Express exponent as delta to (number of mantissa bits + 31).
  addi(scratch,
       scratch,
       Operand(-(HeapNumber::kExponentBias + HeapNumber::kMantissaBits + 31)));

  // If the delta is strictly positive, all bits would be shifted away,
  // which means that we can return 0.
  cmpi(scratch, Operand::Zero());
  bgt(&done);

  const int kShiftBase = HeapNumber::kNonMantissaBitsInTopWord - 1;
  // Calculate shift.
  addi(scratch, scratch, Operand(kShiftBase + HeapNumber::kMantissaBits));

  // Save the sign.
  STATIC_ASSERT(HeapNumber::kSignMask == 0x80000000u);
  Register sign = result;
  result = no_reg;
  ExtractSignBit32(sign, input_high);

  // Shifts >= 32 bits should result in zero.
  // slw extracts only the 6 most significant bits of the shift value.
  cmpi(scratch, Operand(32));
  blt(&high_shift_needed);
  li(input_high, Operand::Zero());
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
  cmpi(sign, Operand::Zero());
  result = sign;
  sign = no_reg;
  mr(result, input_high);
  beq(&done);
  neg(result, result);

  bind(&done);
}


void MacroAssembler::EmitECMATruncate(Register result,
                                      DwVfpRegister double_input,
                                      DwVfpRegister double_scratch,
                                      Register scratch,
                                      Register input_high,
                                      Register input_low) {
  ASSERT(!input_high.is(result));
  ASSERT(!input_low.is(result));
  ASSERT(!input_low.is(input_high));
  ASSERT(!scratch.is(result) &&
         !scratch.is(input_high) &&
         !scratch.is(input_low));
  ASSERT(!double_scratch.is(double_input));

  Label done;

  fctidz(double_scratch, double_input);

  // reserve a slot on the stack
  stfdu(double_scratch, MemOperand(sp, -8));
#if V8_TARGET_ARCH_PPC64
  ld(result, MemOperand(sp, 0));
#else
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
  lwz(scratch, MemOperand(sp, 4));
  lwz(result, MemOperand(sp));
#else
  lwz(scratch, MemOperand(sp, 0));
  lwz(result, MemOperand(sp, 4));
#endif
#endif

  // The result is a 32-bit integer when the high 33 bits of the
  // result are identical.
#if V8_TARGET_ARCH_PPC64
  TestIfInt32(result, scratch, r0);
#else
  TestIfInt32(scratch, result, r0);
#endif
  beq(&done);

  // Load the double value and perform a manual truncation.
  stfd(double_input, MemOperand(sp));
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
  lwz(input_low, MemOperand(sp));
  lwz(input_high, MemOperand(sp, 4));
#else
  lwz(input_high, MemOperand(sp));
  lwz(input_low, MemOperand(sp, 4));
#endif
  EmitOutOfInt32RangeTruncate(result,
                              input_high,
                              input_low,
                              scratch);

  bind(&done);

  // restore the stack
  addi(sp, sp, Operand(8));
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
#if V8_TARGET_ARCH_PPC64
  CEntryStub stub(f->result_size);
#else
  CEntryStub stub(1);
#endif
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
    Abort("Global functions must have initial map");
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
    Check(ne, "Operand is a smi", cr0);
  }
}


void MacroAssembler::AssertSmi(Register object) {
  if (emit_debug_code()) {
    STATIC_ASSERT(kSmiTag == 0);
    andi(r0, object, Operand(kSmiTagMask));
    Check(eq, "Operand is not smi", cr0);
  }
}


void MacroAssembler::AssertString(Register object) {
  if (emit_debug_code()) {
    STATIC_ASSERT(kSmiTag == 0);
    andi(r0, object, Operand(kSmiTagMask));
    Check(ne, "Operand is not a string", cr0);
    push(object);
    LoadP(object, FieldMemOperand(object, HeapObject::kMapOffset));
    CompareInstanceType(object, object, FIRST_NONSTRING_TYPE);
    pop(object);
    Check(lt, "Operand is not a string");
  }
}



void MacroAssembler::AssertRootValue(Register src,
                                     Heap::RootListIndex root_value_index,
                                     const char* message) {
  if (emit_debug_code()) {
    CompareRoot(src, root_value_index);
    Check(eq, message);
  }
}


void MacroAssembler::JumpIfNotHeapNumber(Register object,
                                         Register heap_number_map,
                                         Register scratch,
                                         Label* on_not_heap_number) {
  LoadP(scratch, FieldMemOperand(object, HeapObject::kMapOffset));
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
                                        Label* gc_required,
                                        TaggingMode tagging_mode) {
  // Allocate an object in the heap for the heap number and tag it as a heap
  // object.
  AllocateInNewSpace(HeapNumber::kSize,
                     result,
                     scratch1,
                     scratch2,
                     gc_required,
                     tagging_mode == TAG_RESULT ? TAG_OBJECT :
                                                  NO_ALLOCATION_FLAGS);

  // Store heap number map in the allocated object.
  AssertRegisterIsRoot(heap_number_map, Heap::kHeapNumberMapRootIndex);
  if (tagging_mode == TAG_RESULT) {
    StoreP(heap_number_map, FieldMemOperand(result, HeapObject::kMapOffset),
           r0);
  } else {
    StoreP(heap_number_map, MemOperand(result, HeapObject::kMapOffset));
  }
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
    Assert(eq, "Expecting alignment for CopyBytes", cr0);
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

static const int kRegisterPassedArguments = 8;


int MacroAssembler::CalculateStackPassedWords(int num_reg_arguments,
                                              int num_double_arguments) {
  int stack_passed_words = 0;
  if (num_double_arguments > DoubleRegister::kNumRegisters) {
      stack_passed_words +=
          2 * (num_double_arguments - DoubleRegister::kNumRegisters);
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
    // Make stack end at alignment and make room for stack arguments,
    // the original value of sp and, on native, the required slots to
    // make ABI work.
    mr(scratch, sp);
#if !defined(USE_SIMULATOR)
    subi(sp, sp, Operand((stack_passed_arguments +
                          kNumRequiredStackFrameSlots) * kPointerSize));
#else
    subi(sp, sp, Operand((stack_passed_arguments + 1) * kPointerSize));
#endif
    ASSERT(IsPowerOf2(frame_alignment));
    li(r0, Operand(-frame_alignment));
    and_(sp, sp, r0);
#if !defined(USE_SIMULATOR)
    // On the simulator we pass args on the stack
    StoreP(scratch, MemOperand(sp));
#else
    // On the simulator we pass args on the stack
    StoreP(scratch,
           MemOperand(sp, stack_passed_arguments * kPointerSize), r0);
#endif
  } else {
    subi(sp, sp, Operand((stack_passed_arguments +
                          kNumRequiredStackFrameSlots) * kPointerSize));
  }
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
  // Make sure that the stack is aligned before calling a C function unless
  // running in the simulator. The simulator has its own alignment check which
  // provides more information.

  // Just call directly. The function called cannot cause a GC, or
  // allow preemption, so the return address in the link register
  // stays correct.
#if ABI_USES_FUNCTION_DESCRIPTORS && !defined(USE_SIMULATOR)
  // AIX uses a function descriptor. When calling C code be aware
  // of this descriptor and pick up values from it
  Register dest = ip;
  LoadP(ToRegister(2), MemOperand(function, kPointerSize));
  LoadP(dest, MemOperand(function, 0));
#elif ABI_TOC_ADDRESSABILITY_VIA_IP
  Register dest = ip;
  Move(ip, function);
#else
  Register dest = function;
#endif

  Call(dest);

  int stack_passed_arguments = CalculateStackPassedWords(
      num_reg_arguments, num_double_arguments);
  if (ActivationFrameAlignment() > kPointerSize) {
#if !defined(USE_SIMULATOR)
    // On real hardware we follow the ABI
    LoadP(sp, MemOperand(sp));
#else
    // On the simulator we pass args on the stack
    LoadP(sp, MemOperand(sp, stack_passed_arguments * kPointerSize), r0);
#endif
  } else {
    addi(sp, sp, Operand((stack_passed_arguments +
                          kNumRequiredStackFrameSlots) * kPointerSize));
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
  ASSERT(size > 0 && size <= kCacheLineSize);
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
    Check(eq, "The instruction to patch should be a lis.");
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
    Check(eq, "The instruction should be an ori");
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
    Check(eq, "The instruction should be an sldi");
  }

  lwz(scratch, MemOperand(lis_location, 3*kInstrSize));
  // scratch is now ori.
  if (emit_debug_code()) {
    And(scratch, scratch, Operand(kOpcodeMask));
    Cmpi(scratch, Operand(ORIS), r0);
    Check(eq, "The instruction should be an oris");
    lwz(scratch, MemOperand(lis_location, 3*kInstrSize));
  }

  rlwimi(scratch, new_value, 16, 16, 31);
  stw(scratch, MemOperand(lis_location, 3*kInstrSize));

  lwz(scratch, MemOperand(lis_location, 4*kInstrSize));
  // scratch is now ori.
  if (emit_debug_code()) {
    And(scratch, scratch, Operand(kOpcodeMask));
    Cmpi(scratch, Operand(ORI), r0);
    Check(eq, "The instruction should be an ori");
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
    Check(eq, "The instruction should be a lis.");
    lwz(result, MemOperand(lis_location));
  }

  // result now holds a lis instruction. Extract the immediate.
  slwi(result, result, Operand(16));

  lwz(scratch, MemOperand(lis_location, kInstrSize));
  if (emit_debug_code()) {
    And(scratch, scratch, Operand(kOpcodeMask));
    Cmpi(scratch, Operand(ORI), r0);
    Check(eq, "The instruction should be an ori");
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
    Check(eq, "The instruction should be an sldi");
  }

  lwz(scratch, MemOperand(lis_location, 3*kInstrSize));
  // scratch is now ori.
  if (emit_debug_code()) {
    And(scratch, scratch, Operand(kOpcodeMask));
    Cmpi(scratch, Operand(ORIS), r0);
    Check(eq, "The instruction should be an oris");
    lwz(scratch, MemOperand(lis_location, 3*kInstrSize));
  }
  sldi(result, result, Operand(16));
  rldimi(result, scratch, 0, 48);

  lwz(scratch, MemOperand(lis_location, 4*kInstrSize));
  // scratch is now ori.
  if (emit_debug_code()) {
    And(scratch, scratch, Operand(kOpcodeMask));
    Cmpi(scratch, Operand(ORI), r0);
    Check(eq, "The instruction should be an ori");
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
  ASSERT(kAsciiStringTag == 4 && kStringEncodingMask == 4);
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

void MacroAssembler::SetRoundingMode(VFPRoundingMode RN) {
  mtfsfi(7, RN);
}

void MacroAssembler::ResetRoundingMode() {
  mtfsfi(7, kRoundToNearest);  // reset (default is kRoundToNearest)
}

void MacroAssembler::ClampDoubleToUint8(Register result_reg,
                                        DoubleRegister input_reg,
                                        DoubleRegister temp_double_reg,
                                        DoubleRegister temp_double_reg2) {
  Label above_zero;
  Label done;
  Label in_bounds;

  LoadDoubleLiteral(temp_double_reg, 0.0, result_reg);
  fcmpu(input_reg, temp_double_reg);
  bgt(&above_zero);

  // Double value is less than zero, NaN or Inf, return 0.
  LoadIntLiteral(result_reg, 0);
  b(&done);

  // Double value is >= 255, return 255.
  bind(&above_zero);
  LoadDoubleLiteral(temp_double_reg, 255.0, result_reg);
  fcmpu(input_reg, temp_double_reg);
  ble(&in_bounds);
  LoadIntLiteral(result_reg, 255);
  b(&done);

  // In 0-255 range, round and truncate.
  bind(&in_bounds);

  // round to nearest (default rounding mode)
  fctiw(temp_double_reg, input_reg);

  // reserve a slot on the stack
  stfdu(temp_double_reg, MemOperand(sp, -8));
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

void MacroAssembler::LoadDoubleLiteral(DwVfpRegister result,
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
    if (is_uint16(rb.imm_) && rb.rmode_ == RelocInfo::NONE
        && rc == SetRC) {
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
    if (is_uint16(rb.imm_) && rb.rmode_ == RelocInfo::NONE && rc == LeaveRC) {
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
    if (is_uint16(rb.imm_) && rb.rmode_ == RelocInfo::NONE && rc == LeaveRC) {
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
