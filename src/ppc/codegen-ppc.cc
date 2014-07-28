// Copyright 2012 the V8 project authors. All rights reserved.
//
// Copyright IBM Corp. 2012, 2013. All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "v8.h"

#if V8_TARGET_ARCH_PPC

#include "codegen.h"
#include "macro-assembler.h"
#include "simulator-ppc.h"

namespace v8 {
namespace internal {


#define __ masm.


#if defined(USE_SIMULATOR)
byte* fast_exp_ppc_machine_code = NULL;
double fast_exp_simulator(double x) {
  return Simulator::current(Isolate::Current())->CallFPReturnsDouble(
      fast_exp_ppc_machine_code, x, 0);
}
#endif


UnaryMathFunction CreateExpFunction() {
  if (!FLAG_fast_math) return &std::exp;
  size_t actual_size;
  byte* buffer = static_cast<byte*>(OS::Allocate(1 * KB, &actual_size, true));
  if (buffer == NULL) return &std::exp;
  ExternalReference::InitializeMathExpData();

  MacroAssembler masm(NULL, buffer, static_cast<int>(actual_size));

  {
    DoubleRegister input = d1;
    DoubleRegister result = d2;
    DoubleRegister double_scratch1 = d3;
    DoubleRegister double_scratch2 = d4;
    Register temp1 = r7;
    Register temp2 = r8;
    Register temp3 = r9;

  // Called from C
#if ABI_USES_FUNCTION_DESCRIPTORS
    __ function_descriptor();
#endif

    __ Push(temp3, temp2, temp1);
    MathExpGenerator::EmitMathExp(
        &masm, input, result, double_scratch1, double_scratch2,
        temp1, temp2, temp3);
    __ Pop(temp3, temp2, temp1);
    __ fmr(d1, result);
    __ Ret();
  }

  CodeDesc desc;
  masm.GetCode(&desc);
#if !ABI_USES_FUNCTION_DESCRIPTORS
  ASSERT(!RelocInfo::RequiresRelocation(desc));
#endif

  CPU::FlushICache(buffer, actual_size);
  OS::ProtectCode(buffer, actual_size);

#if !defined(USE_SIMULATOR)
  return FUNCTION_CAST<UnaryMathFunction>(buffer);
#else
  fast_exp_ppc_machine_code = buffer;
  return &fast_exp_simulator;
#endif
}


UnaryMathFunction CreateSqrtFunction() {
#if defined(USE_SIMULATOR)
  return &std::sqrt;
#else
  size_t actual_size;
  byte* buffer = static_cast<byte*>(OS::Allocate(1 * KB, &actual_size, true));
  if (buffer == NULL) return &std::sqrt;

  MacroAssembler masm(NULL, buffer, static_cast<int>(actual_size));

  // Called from C
#if ABI_USES_FUNCTION_DESCRIPTORS
  __ function_descriptor();
#endif

  __ MovFromFloatParameter(d1);
  __ fsqrt(d1, d1);
  __ MovToFloatResult(d1);
  __ Ret();

  CodeDesc desc;
  masm.GetCode(&desc);
#if !ABI_USES_FUNCTION_DESCRIPTORS
  ASSERT(!RelocInfo::RequiresRelocation(desc));
#endif

  CPU::FlushICache(buffer, actual_size);
  OS::ProtectCode(buffer, actual_size);
  return FUNCTION_CAST<UnaryMathFunction>(buffer);
#endif
}

#undef __


// -------------------------------------------------------------------------
// Platform-specific RuntimeCallHelper functions.

void StubRuntimeCallHelper::BeforeCall(MacroAssembler* masm) const {
  masm->EnterFrame(StackFrame::INTERNAL);
  ASSERT(!masm->has_frame());
  masm->set_has_frame(true);
}


void StubRuntimeCallHelper::AfterCall(MacroAssembler* masm) const {
  masm->LeaveFrame(StackFrame::INTERNAL);
  ASSERT(masm->has_frame());
  masm->set_has_frame(false);
}


// -------------------------------------------------------------------------
// Code generators

#define __ ACCESS_MASM(masm)

void ElementsTransitionGenerator::GenerateMapChangeElementsTransition(
    MacroAssembler* masm, AllocationSiteMode mode,
    Label* allocation_memento_found) {
  // ----------- S t a t e -------------
  //  -- r3    : value
  //  -- r4    : key
  //  -- r5    : receiver
  //  -- lr    : return address
  //  -- r6    : target map, scratch for subsequent call
  //  -- r7    : scratch (elements)
  // -----------------------------------
  if (mode == TRACK_ALLOCATION_SITE) {
    ASSERT(allocation_memento_found != NULL);
    __ JumpIfJSArrayHasAllocationMemento(r5, r7, allocation_memento_found);
  }

  // Set transitioned map.
  __ StoreP(r6, FieldMemOperand(r5, HeapObject::kMapOffset), r0);
  __ RecordWriteField(r5,
                      HeapObject::kMapOffset,
                      r6,
                      r11,
                      kLRHasNotBeenSaved,
                      kDontSaveFPRegs,
                      EMIT_REMEMBERED_SET,
                      OMIT_SMI_CHECK);
}


void ElementsTransitionGenerator::GenerateSmiToDouble(
    MacroAssembler* masm, AllocationSiteMode mode, Label* fail) {
  // ----------- S t a t e -------------
  //  -- r3    : value
  //  -- r4    : key
  //  -- r5    : receiver
  //  -- lr    : return address
  //  -- r6    : target map, scratch for subsequent call
  //  -- r7    : scratch (elements)
  // -----------------------------------
  Label loop, entry, convert_hole, gc_required, only_change_map, done;

  if (mode == TRACK_ALLOCATION_SITE) {
    __ JumpIfJSArrayHasAllocationMemento(r5, r7, fail);
  }

  // Check for empty arrays, which only require a map transition and no changes
  // to the backing store.
  __ LoadP(r7, FieldMemOperand(r5, JSObject::kElementsOffset));
  __ CompareRoot(r7, Heap::kEmptyFixedArrayRootIndex);
  __ beq(&only_change_map);

  // Preserve lr and use r17 as a temporary register.
  __ mflr(r0);
  __ Push(r0);

  __ LoadP(r8, FieldMemOperand(r7, FixedArray::kLengthOffset));
  // r7: source FixedArray
  // r8: number of elements (smi-tagged)

  // Allocate new FixedDoubleArray.
  __ SmiToDoubleArrayOffset(r17, r8);
  __ addi(r17, r17, Operand(FixedDoubleArray::kHeaderSize));
  __ Allocate(r17, r9, r10, r11, &gc_required, DOUBLE_ALIGNMENT);
  // r9: destination FixedDoubleArray, not tagged as heap object.

  // Set destination FixedDoubleArray's length and map.
  __ LoadRoot(r11, Heap::kFixedDoubleArrayMapRootIndex);
  __ StoreP(r8, MemOperand(r9, FixedDoubleArray::kLengthOffset));
  // Update receiver's map.
  __ StoreP(r11, MemOperand(r9, HeapObject::kMapOffset));

  __ StoreP(r6, FieldMemOperand(r5, HeapObject::kMapOffset), r0);
  __ RecordWriteField(r5,
                      HeapObject::kMapOffset,
                      r6,
                      r11,
                      kLRHasBeenSaved,
                      kDontSaveFPRegs,
                      OMIT_REMEMBERED_SET,
                      OMIT_SMI_CHECK);
  // Replace receiver's backing store with newly created FixedDoubleArray.
  __ addi(r6, r9, Operand(kHeapObjectTag));
  __ StoreP(r6, FieldMemOperand(r5, JSObject::kElementsOffset), r0);
  __ RecordWriteField(r5,
                      JSObject::kElementsOffset,
                      r6,
                      r11,
                      kLRHasBeenSaved,
                      kDontSaveFPRegs,
                      EMIT_REMEMBERED_SET,
                      OMIT_SMI_CHECK);

  // Prepare for conversion loop.
  __ addi(r6, r7, Operand(FixedArray::kHeaderSize - kHeapObjectTag));
  __ addi(r10, r9, Operand(FixedDoubleArray::kHeaderSize));
  __ SmiToDoubleArrayOffset(r9, r8);
  __ add(r9, r10, r9);
#if V8_TARGET_ARCH_PPC64
  __ mov(r7, Operand(kHoleNanInt64));
#else
  __ mov(r7, Operand(kHoleNanLower32));
  __ mov(r8, Operand(kHoleNanUpper32));
#endif
  // r6: begin of source FixedArray element fields, not tagged
  // r7: kHoleNanLower32
  // r8: kHoleNanUpper32
  // r9: end of destination FixedDoubleArray, not tagged
  // r10: begin of FixedDoubleArray element fields, not tagged

  __ b(&entry);

  __ bind(&only_change_map);
  __ StoreP(r6, FieldMemOperand(r5, HeapObject::kMapOffset), r0);
  __ RecordWriteField(r5,
                      HeapObject::kMapOffset,
                      r6,
                      r11,
                      kLRHasNotBeenSaved,
                      kDontSaveFPRegs,
                      OMIT_REMEMBERED_SET,
                      OMIT_SMI_CHECK);
  __ b(&done);

  // Call into runtime if GC is required.
  __ bind(&gc_required);
  __ Pop(r0);
  __ mtlr(r0);
  __ b(fail);

  // Convert and copy elements.
  __ bind(&loop);
  __ LoadP(r11, MemOperand(r6));
  __ addi(r6, r6, Operand(kPointerSize));
  // r11: current element
  __ UntagAndJumpIfNotSmi(r11, r11, &convert_hole);

  // Normal smi, convert to double and store.
  __ ConvertIntToDouble(r11, d0);
  __ stfd(d0, MemOperand(r10, 0));
  __ addi(r10, r10, Operand(8));

  __ b(&entry);

  // Hole found, store the-hole NaN.
  __ bind(&convert_hole);
  if (FLAG_debug_code) {
    // Restore a "smi-untagged" heap object.
    __ LoadP(r11, MemOperand(r6, -kPointerSize));
    __ CompareRoot(r11, Heap::kTheHoleValueRootIndex);
    __ Assert(eq, kObjectFoundInSmiOnlyArray);
  }
#if V8_TARGET_ARCH_PPC64
  __ std(r7, MemOperand(r10, 0));
#else
  __ stw(r8, MemOperand(r10, Register::kExponentOffset));
  __ stw(r7, MemOperand(r10, Register::kMantissaOffset));
#endif
  __ addi(r10, r10, Operand(8));

  __ bind(&entry);
  __ cmp(r10, r9);
  __ blt(&loop);

  __ Pop(r0);
  __ mtlr(r0);
  __ bind(&done);
}


void ElementsTransitionGenerator::GenerateDoubleToObject(
    MacroAssembler* masm, AllocationSiteMode mode, Label* fail) {
  // ----------- S t a t e -------------
  //  -- r3    : value
  //  -- r4    : key
  //  -- r5    : receiver
  //  -- lr    : return address
  //  -- r6    : target map, scratch for subsequent call
  //  -- r7    : scratch (elements)
  // -----------------------------------
  Label entry, loop, convert_hole, gc_required, only_change_map;

  if (mode == TRACK_ALLOCATION_SITE) {
    __ JumpIfJSArrayHasAllocationMemento(r5, r7, fail);
  }

  // Check for empty arrays, which only require a map transition and no changes
  // to the backing store.
  __ LoadP(r7, FieldMemOperand(r5, JSObject::kElementsOffset));
  __ CompareRoot(r7, Heap::kEmptyFixedArrayRootIndex);
  __ beq(&only_change_map);

  __ Push(r6, r5, r4, r3);
  __ LoadP(r8, FieldMemOperand(r7, FixedArray::kLengthOffset));
  // r7: source FixedDoubleArray
  // r8: number of elements (smi-tagged)

  // Allocate new FixedArray.
  __ li(r3, Operand(FixedDoubleArray::kHeaderSize));
  __ SmiToPtrArrayOffset(r0, r8);
  __ add(r3, r3, r0);
  __ Allocate(r3, r9, r10, r11, &gc_required, NO_ALLOCATION_FLAGS);
  // r9: destination FixedArray, not tagged as heap object
  // Set destination FixedDoubleArray's length and map.
  __ LoadRoot(r11, Heap::kFixedArrayMapRootIndex);
  __ StoreP(r8, MemOperand(r9, FixedDoubleArray::kLengthOffset));
  __ StoreP(r11, MemOperand(r9, HeapObject::kMapOffset));

  // Prepare for conversion loop.
  __ addi(r7, r7, Operand(FixedDoubleArray::kHeaderSize - kHeapObjectTag));
  __ addi(r6, r9, Operand(FixedArray::kHeaderSize));
  __ addi(r9, r9, Operand(kHeapObjectTag));
  __ SmiToPtrArrayOffset(r8, r8);
  __ add(r8, r6, r8);
  __ LoadRoot(r10, Heap::kTheHoleValueRootIndex);
  __ LoadRoot(r11, Heap::kHeapNumberMapRootIndex);
  // Using offsetted addresses in r7 to fully take advantage of post-indexing.
  // r6: begin of destination FixedArray element fields, not tagged
  // r7: begin of source FixedDoubleArray element fields, not tagged
  // r8: end of destination FixedArray, not tagged
  // r9: destination FixedArray
  // r10: the-hole pointer
  // r11: heap number map
  __ b(&entry);

  // Call into runtime if GC is required.
  __ bind(&gc_required);
  __ Pop(r6, r5, r4, r3);
  __ b(fail);

  __ bind(&loop);
  __ lwz(r4, MemOperand(r7, Register::kExponentOffset));
  __ addi(r7, r7, Operand(kDoubleSize));
  // r4: current element's upper 32 bit
  // r7: address of next element's upper 32 bit
  __ Cmpi(r4, Operand(kHoleNanUpper32), r0);
  __ beq(&convert_hole);

  // Non-hole double, copy value into a heap number.
  __ AllocateHeapNumber(r5, r3, r4, r11, &gc_required);
  // r5: new heap number
#if V8_TARGET_ARCH_PPC64
  __ ld(r3, MemOperand(r7, -kDoubleSize));
  __ addi(r4, r5, Operand(-1));  // subtract tag for std
  __ std(r3, MemOperand(r4, HeapNumber::kValueOffset));
#else
  __ lwz(r3, MemOperand(r7, Register::kMantissaOffset - kDoubleSize));
  __ lwz(r4, MemOperand(r7, Register::kExponentOffset - kDoubleSize));
  __ stw(r3, FieldMemOperand(r5, HeapNumber::kMantissaOffset));
  __ stw(r4, FieldMemOperand(r5, HeapNumber::kExponentOffset));
#endif
  __ mr(r3, r6);
  __ StoreP(r5, MemOperand(r6));
  __ addi(r6, r6, Operand(kPointerSize));
  __ RecordWrite(r9,
                 r3,
                 r5,
                 kLRHasNotBeenSaved,
                 kDontSaveFPRegs,
                 EMIT_REMEMBERED_SET,
                 OMIT_SMI_CHECK);
  __ b(&entry);

  // Replace the-hole NaN with the-hole pointer.
  __ bind(&convert_hole);
  __ StoreP(r10, MemOperand(r6));
  __ addi(r6, r6, Operand(kPointerSize));

  __ bind(&entry);
  __ cmpl(r6, r8);
  __ blt(&loop);

  __ Pop(r6, r5, r4, r3);
  // Replace receiver's backing store with newly created and filled FixedArray.
  __ StoreP(r9, FieldMemOperand(r5, JSObject::kElementsOffset), r0);
  __ RecordWriteField(r5,
                      JSObject::kElementsOffset,
                      r9,
                      r11,
                      kLRHasNotBeenSaved,
                      kDontSaveFPRegs,
                      EMIT_REMEMBERED_SET,
                      OMIT_SMI_CHECK);

  __ bind(&only_change_map);
  // Update receiver's map.
  __ StoreP(r6, FieldMemOperand(r5, HeapObject::kMapOffset), r0);
  __ RecordWriteField(r5,
                      HeapObject::kMapOffset,
                      r6,
                      r11,
                      kLRHasNotBeenSaved,
                      kDontSaveFPRegs,
                      OMIT_REMEMBERED_SET,
                      OMIT_SMI_CHECK);
}


// roohack - assume ip can be used as a scratch register below
void StringCharLoadGenerator::Generate(MacroAssembler* masm,
                                       Register string,
                                       Register index,
                                       Register result,
                                       Label* call_runtime) {
  // Fetch the instance type of the receiver into result register.
  __ LoadP(result, FieldMemOperand(string, HeapObject::kMapOffset));
  __ lbz(result, FieldMemOperand(result, Map::kInstanceTypeOffset));

  // We need special handling for indirect strings.
  Label check_sequential;
  __ andi(r0, result, Operand(kIsIndirectStringMask));
  __ beq(&check_sequential, cr0);

  // Dispatch on the indirect string shape: slice or cons.
  Label cons_string;
  __ mov(ip, Operand(kSlicedNotConsMask));
  __ and_(r0, result, ip, SetRC);
  __ beq(&cons_string, cr0);

  // Handle slices.
  Label indirect_string_loaded;
  __ LoadP(result, FieldMemOperand(string, SlicedString::kOffsetOffset));
  __ LoadP(string, FieldMemOperand(string, SlicedString::kParentOffset));
  __ SmiUntag(ip, result);
  __ add(index, index, ip);
  __ b(&indirect_string_loaded);

  // Handle cons strings.
  // Check whether the right hand side is the empty string (i.e. if
  // this is really a flat string in a cons string). If that is not
  // the case we would rather go to the runtime system now to flatten
  // the string.
  __ bind(&cons_string);
  __ LoadP(result, FieldMemOperand(string, ConsString::kSecondOffset));
  __ CompareRoot(result, Heap::kempty_stringRootIndex);
  __ bne(call_runtime);
  // Get the first of the two strings and load its instance type.
  __ LoadP(string, FieldMemOperand(string, ConsString::kFirstOffset));

  __ bind(&indirect_string_loaded);
  __ LoadP(result, FieldMemOperand(string, HeapObject::kMapOffset));
  __ lbz(result, FieldMemOperand(result, Map::kInstanceTypeOffset));

  // Distinguish sequential and external strings. Only these two string
  // representations can reach here (slices and flat cons strings have been
  // reduced to the underlying sequential or external string).
  Label external_string, check_encoding;
  __ bind(&check_sequential);
  STATIC_ASSERT(kSeqStringTag == 0);
  __ andi(r0, result, Operand(kStringRepresentationMask));
  __ bne(&external_string, cr0);

  // Prepare sequential strings
  STATIC_ASSERT(SeqTwoByteString::kHeaderSize == SeqOneByteString::kHeaderSize);
  __ addi(string,
          string,
          Operand(SeqTwoByteString::kHeaderSize - kHeapObjectTag));
  __ b(&check_encoding);

  // Handle external strings.
  __ bind(&external_string);
  if (FLAG_debug_code) {
    // Assert that we do not have a cons or slice (indirect strings) here.
    // Sequential strings have already been ruled out.
    __ andi(r0, result, Operand(kIsIndirectStringMask));
    __ Assert(eq, kExternalStringExpectedButNotFound, cr0);
  }
  // Rule out short external strings.
  STATIC_CHECK(kShortExternalStringTag != 0);
  __ andi(r0, result, Operand(kShortExternalStringMask));
  __ bne(call_runtime, cr0);
  __ LoadP(string,
           FieldMemOperand(string, ExternalString::kResourceDataOffset));

  Label ascii, done;
  __ bind(&check_encoding);
  STATIC_ASSERT(kTwoByteStringTag == 0);
  __ andi(r0, result, Operand(kStringEncodingMask));
  __ bne(&ascii, cr0);
  // Two-byte string.
  __ ShiftLeftImm(result, index, Operand(1));
  __ lhzx(result, MemOperand(string, result));
  __ b(&done);
  __ bind(&ascii);
  // Ascii string.
  __ lbzx(result, MemOperand(string, index));
  __ bind(&done);
}


static MemOperand ExpConstant(int index, Register base) {
  return MemOperand(base, index * kDoubleSize);
}


void MathExpGenerator::EmitMathExp(MacroAssembler* masm,
                                   DoubleRegister input,
                                   DoubleRegister result,
                                   DoubleRegister double_scratch1,
                                   DoubleRegister double_scratch2,
                                   Register temp1,
                                   Register temp2,
                                   Register temp3) {
  ASSERT(!input.is(result));
  ASSERT(!input.is(double_scratch1));
  ASSERT(!input.is(double_scratch2));
  ASSERT(!result.is(double_scratch1));
  ASSERT(!result.is(double_scratch2));
  ASSERT(!double_scratch1.is(double_scratch2));
  ASSERT(!temp1.is(temp2));
  ASSERT(!temp1.is(temp3));
  ASSERT(!temp2.is(temp3));
  ASSERT(ExternalReference::math_exp_constants(0).address() != NULL);

  Label zero, infinity, done;

  __ mov(temp3, Operand(ExternalReference::math_exp_constants(0)));

  __ lfd(double_scratch1, ExpConstant(0, temp3));
  __ fcmpu(double_scratch1, input);
  __ fmr(result, input);
  __ bunordered(&done);
  __ bge(&zero);

  __ lfd(double_scratch2, ExpConstant(1, temp3));
  __ fcmpu(input, double_scratch2);
  __ bge(&infinity);

  __ lfd(double_scratch1, ExpConstant(3, temp3));
  __ lfd(result, ExpConstant(4, temp3));
  __ fmul(double_scratch1, double_scratch1, input);
  __ fadd(double_scratch1, double_scratch1, result);
  __ MovDoubleLowToInt(temp2, double_scratch1);
  __ fsub(double_scratch1, double_scratch1, result);
  __ lfd(result, ExpConstant(6, temp3));
  __ lfd(double_scratch2, ExpConstant(5, temp3));
  __ fmul(double_scratch1, double_scratch1, double_scratch2);
  __ fsub(double_scratch1, double_scratch1, input);
  __ fsub(result, result, double_scratch1);
  __ fmul(double_scratch2, double_scratch1, double_scratch1);
  __ fmul(result, result, double_scratch2);
  __ lfd(double_scratch2, ExpConstant(7, temp3));
  __ fmul(result, result, double_scratch2);
  __ fsub(result, result, double_scratch1);
  __ lfd(double_scratch2, ExpConstant(8, temp3));
  __ fadd(result, result, double_scratch2);
  __ srwi(temp1, temp2, Operand(11));
  __ andi(temp2, temp2, Operand(0x7ff));
  __ addi(temp1, temp1, Operand(0x3ff));

  // Must not call ExpConstant() after overwriting temp3!
  __ mov(temp3, Operand(ExternalReference::math_exp_log_table()));
  __ slwi(temp2, temp2, Operand(3));
#if V8_TARGET_ARCH_PPC64
  __ ldx(temp2, MemOperand(temp3, temp2));
  __ sldi(temp1, temp1, Operand(52));
  __ orx(temp2, temp1, temp2);
  __ MovInt64ToDouble(double_scratch1, temp2);
#else
  __ add(ip, temp3, temp2);
  __ lwz(temp3, MemOperand(ip, Register::kExponentOffset));
  __ lwz(temp2, MemOperand(ip, Register::kMantissaOffset));
  __ slwi(temp1, temp1, Operand(20));
  __ orx(temp3, temp1, temp3);
  __ MovInt64ToDouble(double_scratch1, temp3, temp2);
#endif

  __ fmul(result, result, double_scratch1);
  __ b(&done);

  __ bind(&zero);
  __ fmr(result, kDoubleRegZero);
  __ b(&done);

  __ bind(&infinity);
  __ lfd(result, ExpConstant(2, temp3));

  __ bind(&done);
}

#undef __

#ifdef DEBUG
// mflr ip
static const uint32_t kCodeAgePatchFirstInstruction = 0x7d8802a6;
#endif

CodeAgingHelper::CodeAgingHelper() {
  ASSERT(young_sequence_.length() == kNoCodeAgeSequenceLength);
  // Since patcher is a large object, allocate it dynamically when needed,
  // to avoid overloading the stack in stress conditions.
  // DONT_FLUSH is used because the CodeAgingHelper is initialized early in
  // the process, before ARM simulator ICache is setup.
  SmartPointer<CodePatcher> patcher(
      new CodePatcher(young_sequence_.start(),
                      young_sequence_.length() / Assembler::kInstrSize,
                      CodePatcher::DONT_FLUSH));
  PredictableCodeSizeScope scope(patcher->masm(), young_sequence_.length());
  patcher->masm()->PushFixedFrame(r4);
  patcher->masm()->addi(
      fp, sp, Operand(StandardFrameConstants::kFixedFrameSizeFromFp));
  for (int i = 0; i < kNoCodeAgeSequenceNops; i++) {
    patcher->masm()->nop();
  }
}


#ifdef DEBUG
bool CodeAgingHelper::IsOld(byte* candidate) const {
  return Memory::uint32_at(candidate) == kCodeAgePatchFirstInstruction;
}
#endif


bool Code::IsYoungSequence(Isolate* isolate, byte* sequence) {
  bool result = isolate->code_aging_helper()->IsYoung(sequence);
  ASSERT(result || isolate->code_aging_helper()->IsOld(sequence));
  return result;
}


void Code::GetCodeAgeAndParity(Isolate* isolate, byte* sequence, Age* age,
                               MarkingParity* parity) {
  if (IsYoungSequence(isolate, sequence)) {
    *age = kNoAgeCodeAge;
    *parity = NO_MARKING_PARITY;
  } else {
    ConstantPoolArray *constant_pool = NULL;
    Address target_address = Assembler::target_address_at(
      sequence + kCodeAgingTargetDelta, constant_pool);
    Code* stub = GetCodeFromTargetAddress(target_address);
    GetCodeAgeAndParity(stub, age, parity);
  }
}


void Code::PatchPlatformCodeAge(Isolate* isolate,
                                byte* sequence,
                                Code::Age age,
                                MarkingParity parity) {
  uint32_t young_length = isolate->code_aging_helper()->young_sequence_length();
  if (age == kNoAgeCodeAge) {
    isolate->code_aging_helper()->CopyYoungSequenceTo(sequence);
    CPU::FlushICache(sequence, young_length);
  } else {
    // FIXED_SEQUENCE
    Code* stub = GetCodeAgeStub(isolate, age, parity);
    CodePatcher patcher(sequence, young_length / Assembler::kInstrSize);
    Assembler::BlockTrampolinePoolScope block_trampoline_pool(patcher.masm());
    intptr_t target = reinterpret_cast<intptr_t>(stub->instruction_start());
    // We use Call to compute the address of this patch sequence.
    // Preserve lr since it will be clobbered.  See
    // GenerateMakeCodeYoungAgainCommon for the stub code.
    patcher.masm()->mflr(ip);
    patcher.masm()->mov(r3, Operand(target));
    patcher.masm()->Call(r3);
    for (int i = 0; i < kCodeAgingSequenceNops; i++) {
      patcher.masm()->nop();
    }
  }
}


} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_PPC
