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

#include "v8.h"

#if defined(V8_TARGET_ARCH_PPC)

#include "codegen.h"
#include "macro-assembler.h"

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm)

#define EMIT_STUB_MARKER(stub_marker) __ marker_asm(stub_marker)

UnaryMathFunction CreateTranscendentalFunction(TranscendentalCache::Type type) {
  switch (type) {
    case TranscendentalCache::SIN: return &sin;
    case TranscendentalCache::COS: return &cos;
    case TranscendentalCache::TAN: return &tan;
    case TranscendentalCache::LOG: return &log;
    default: UNIMPLEMENTED();
  }
  return NULL;
}


UnaryMathFunction CreateSqrtFunction() {
  return &sqrt;
}

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

void ElementsTransitionGenerator::GenerateMapChangeElementsTransition(
    MacroAssembler* masm) {
  EMIT_STUB_MARKER(323);
  // ----------- S t a t e -------------
  //  -- r3    : value
  //  -- r4    : key
  //  -- r5    : receiver
  //  -- lr    : return address
  //  -- r6    : target map, scratch for subsequent call
  //  -- r7    : scratch (elements)
  // -----------------------------------
  // Set transitioned map.
  __ stw(r6, FieldMemOperand(r5, HeapObject::kMapOffset));
  __ RecordWriteField(r5,
                      HeapObject::kMapOffset,
                      r6,
                      r22,
                      kLRHasNotBeenSaved,
                      kDontSaveFPRegs,
                      EMIT_REMEMBERED_SET,
                      OMIT_SMI_CHECK);
}


void ElementsTransitionGenerator::GenerateSmiToDouble(
    MacroAssembler* masm, Label* fail) {
  EMIT_STUB_MARKER(324);
  // ----------- S t a t e -------------
  //  -- r3    : value
  //  -- r4    : key
  //  -- r5    : receiver
  //  -- lr    : return address
  //  -- r6    : target map, scratch for subsequent call
  //  -- r7    : scratch (elements)
  // -----------------------------------
  Label loop, entry, convert_hole, gc_required, only_change_map, done;
  bool vfp2_supported = CpuFeatures::IsSupported(VFP2);

  // Check for empty arrays, which only require a map transition and no changes
  // to the backing store.
  __ lwz(r7, FieldMemOperand(r5, JSObject::kElementsOffset));
  __ CompareRoot(r7, Heap::kEmptyFixedArrayRootIndex);
  __ beq(&only_change_map);

  // Preserve lr and use r30 as a temporary register.
  __ mflr(r0);
  __ Push(r0, r30);

  __ lwz(r8, FieldMemOperand(r7, FixedArray::kLengthOffset));
  // r7: source FixedArray
  // r8: number of elements (smi-tagged)

  // Allocate new FixedDoubleArray.
  __ slwi(r30, r8, Operand(2));
  __ addi(r30, r30, Operand(FixedDoubleArray::kHeaderSize + kPointerSize));
  __ AllocateInNewSpace(r30, r9, r10, r22, &gc_required, NO_ALLOCATION_FLAGS);
  // r9: destination FixedDoubleArray, not tagged as heap object.

  // Align the array conveniently for doubles.
  // Store a filler value in the unused memory.
  Label aligned, aligned_done;
  __ andi(r0, r9, Operand(kDoubleAlignmentMask));
  __ mov(ip, Operand(masm->isolate()->factory()->one_pointer_filler_map()));
  __ beq(&aligned, cr0);
  // Store at the beginning of the allocated memory and update the base pointer.
  __ stw(ip, MemOperand(r9));
  __ addi(r9, r9, Operand(kPointerSize));
  __ b(&aligned_done);

  __ bind(&aligned);
  // Store the filler at the end of the allocated memory.
  __ sub(r30, r30, Operand(kPointerSize));
  __ stwx(ip, r9, r30);

  __ bind(&aligned_done);

  // Set destination FixedDoubleArray's length and map.
  __ LoadRoot(r22, Heap::kFixedDoubleArrayMapRootIndex);
  __ stw(r8, MemOperand(r9, FixedDoubleArray::kLengthOffset));
  // Update receiver's map.
  __ stw(r22, MemOperand(r9, HeapObject::kMapOffset));

  __ stw(r6, FieldMemOperand(r5, HeapObject::kMapOffset));
  __ RecordWriteField(r5,
                      HeapObject::kMapOffset,
                      r6,
                      r22,
                      kLRHasBeenSaved,
                      kDontSaveFPRegs,
                      OMIT_REMEMBERED_SET,
                      OMIT_SMI_CHECK);
  // Replace receiver's backing store with newly created FixedDoubleArray.
  __ addi(r6, r9, Operand(kHeapObjectTag));
  __ stw(r6, FieldMemOperand(r5, JSObject::kElementsOffset));
  __ RecordWriteField(r5,
                      JSObject::kElementsOffset,
                      r6,
                      r22,
                      kLRHasBeenSaved,
                      kDontSaveFPRegs,
                      EMIT_REMEMBERED_SET,
                      OMIT_SMI_CHECK);

  // Prepare for conversion loop.
  __ addi(r6, r7, Operand(FixedArray::kHeaderSize - kHeapObjectTag));
  __ addi(r10, r9, Operand(FixedDoubleArray::kHeaderSize));
  __ slwi(r9, r8, Operand(2));
  __ add(r9, r10, r9);
  __ mov(r7, Operand(kHoleNanLower32));
  __ mov(r8, Operand(kHoleNanUpper32));
  // r6: begin of source FixedArray element fields, not tagged
  // r7: kHoleNanLower32
  // r8: kHoleNanUpper32
  // r9: end of destination FixedDoubleArray, not tagged
  // r10: begin of FixedDoubleArray element fields, not tagged
  if (!vfp2_supported) __ Push(r4, r3);

  __ b(&entry);

  __ bind(&only_change_map);
  __ stw(r6, FieldMemOperand(r5, HeapObject::kMapOffset));
  __ RecordWriteField(r5,
                      HeapObject::kMapOffset,
                      r6,
                      r22,
                      kLRHasBeenSaved,
                      kDontSaveFPRegs,
                      OMIT_REMEMBERED_SET,
                      OMIT_SMI_CHECK);
  __ b(&done);

  // Call into runtime if GC is required.
  __ bind(&gc_required);
  __ Pop(r0, r30);
  __ mtlr(r0);
  __ b(fail);

  // Convert and copy elements.
  __ bind(&loop);
  __ lwz(r22, MemOperand(r6, 4, PostIndex));
  // r22: current element
  __ UntagAndJumpIfNotSmi(r22, r22, &convert_hole);

  // Normal smi, convert to double and store.
  FloatingPointHelper::ConvertIntToDouble(
    masm, r22, FloatingPointHelper::kFPRegisters,
    d0, r7, r7,  // r7 unused as we're using kFPRegisters
    d2);
  __ stfd(d0, MemOperand(r10, 0));
  __ addi(r10, r10, Operand(8));

  __ b(&entry);

  // Hole found, store the-hole NaN.
  __ bind(&convert_hole);
  if (FLAG_debug_code) {
    // Restore a "smi-untagged" heap object.
    __ SmiTag(r22);
    __ ori(r22, r22, Operand(1));
    __ CompareRoot(r22, Heap::kTheHoleValueRootIndex);
    __ Assert(eq, "object found in smi-only array");
  }
  __ stw(r7, MemOperand(r10, 0));
  __ stw(r8, MemOperand(r10, 4));
  __ addi(r10, r10, Operand(8));

  __ bind(&entry);
  __ cmp(r10, r9);
  __ blt(&loop);

  if (!vfp2_supported) __ Pop(r4, r3);
  __ Pop(r0, r30);
  __ mtlr(r0);
  __ bind(&done);
}


void ElementsTransitionGenerator::GenerateDoubleToObject(
    MacroAssembler* masm, Label* fail) {
  EMIT_STUB_MARKER(325);
  // ----------- S t a t e -------------
  //  -- r3    : value
  //  -- r4    : key
  //  -- r5    : receiver
  //  -- lr    : return address
  //  -- r6    : target map, scratch for subsequent call
  //  -- r7    : scratch (elements)
  // -----------------------------------
  // we also use ip as a scratch register
  Label entry, loop, convert_hole, gc_required, only_change_map;

  // Check for empty arrays, which only require a map transition and no changes
  // to the backing store.
  __ lwz(r7, FieldMemOperand(r5, JSObject::kElementsOffset));
  __ CompareRoot(r7, Heap::kEmptyFixedArrayRootIndex);
  __ beq(&only_change_map);

  __ Push(r6, r5, r4, r3);
  __ lwz(r8, FieldMemOperand(r7, FixedArray::kLengthOffset));
  // r7: source FixedDoubleArray
  // r8: number of elements (smi-tagged)

  // Allocate new FixedArray.
  __ li(r3, Operand(FixedDoubleArray::kHeaderSize));
  __ slwi(ip, r8, Operand(1));
  __ add(r3, r3, ip);
  __ AllocateInNewSpace(r3, r9, r10, r22, &gc_required, NO_ALLOCATION_FLAGS);
  // r9: destination FixedArray, not tagged as heap object
  // Set destination FixedDoubleArray's length and map.
  __ LoadRoot(r22, Heap::kFixedArrayMapRootIndex);
  __ stw(r8, MemOperand(r9, FixedDoubleArray::kLengthOffset));
  __ stw(r22, MemOperand(r9, HeapObject::kMapOffset));

  // Prepare for conversion loop.
  __ addi(r7, r7, Operand(FixedDoubleArray::kHeaderSize - kHeapObjectTag + 4));
  __ addi(r6, r9, Operand(FixedArray::kHeaderSize));
  __ addi(r9, r9, Operand(kHeapObjectTag));
  __ slwi(r8, r8, Operand(1));
  __ add(r8, r6, r8);
  __ LoadRoot(r10, Heap::kTheHoleValueRootIndex);
  __ LoadRoot(r22, Heap::kHeapNumberMapRootIndex);
  // Using offsetted addresses in r7 to fully take advantage of post-indexing.
  // r6: begin of destination FixedArray element fields, not tagged
  // r7: begin of source FixedDoubleArray element fields, not tagged, +4
  // r8: end of destination FixedArray, not tagged
  // r9: destination FixedArray
  // r10: the-hole pointer
  // r22: heap number map
  __ b(&entry);

  // Call into runtime if GC is required.
  __ bind(&gc_required);
  __ Pop(r6, r5, r4, r3);
  __ b(fail);

  __ bind(&loop);
  __ lwz(r4, MemOperand(r7));
  __ addi(r7, r7, Operand(8));
  // ip: current element's upper 32 bit
  // r7: address of next element's upper 32 bit
  __ mov(r0, Operand(kHoleNanUpper32));
  __ cmp(r4, r0);
  __ beq(&convert_hole);

  // Non-hole double, copy value into a heap number.
  __ push(r4);
  __ mr(r4, ip);
  __ AllocateHeapNumber(r5, r3, r4, r22, &gc_required);
  __ mr(ip, r4);
  __ pop(r4);
  // r5: new heap number
  __ lwz(r3, MemOperand(r7, -12));
  __ stw(r3, FieldMemOperand(r5, HeapNumber::kValueOffset));
  __ stw(r4, FieldMemOperand(r5, HeapNumber::kValueOffset+4));
  __ mr(r3, r6);
  __ stw(r5, MemOperand(r6));
  __ addi(r6, r6, Operand(4));
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
  __ stw(r10, MemOperand(r6));
  __ addi(r6, r6, Operand(4));

  __ bind(&entry);
  __ cmpl(r6, r8);
  __ blt(&loop);

  __ Pop(r6, r5, r4, r3);
  // Replace receiver's backing store with newly created and filled FixedArray.
  __ stw(r9, FieldMemOperand(r5, JSObject::kElementsOffset));
  __ RecordWriteField(r5,
                      JSObject::kElementsOffset,
                      r9,
                      r22,
                      kLRHasNotBeenSaved,
                      kDontSaveFPRegs,
                      EMIT_REMEMBERED_SET,
                      OMIT_SMI_CHECK);

  __ bind(&only_change_map);
  // Update receiver's map.
  __ stw(r6, FieldMemOperand(r5, HeapObject::kMapOffset));
  __ RecordWriteField(r5,
                      HeapObject::kMapOffset,
                      r6,
                      r22,
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
  EMIT_STUB_MARKER(326);
  // Fetch the instance type of the receiver into result register.
  __ lwz(result, FieldMemOperand(string, HeapObject::kMapOffset));
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
  __ lwz(result, FieldMemOperand(string, SlicedString::kOffsetOffset));
  __ lwz(string, FieldMemOperand(string, SlicedString::kParentOffset));
  __ srawi(ip, result, kSmiTagSize);
  __ add(index, index, ip);
  __ jmp(&indirect_string_loaded);

  // Handle cons strings.
  // Check whether the right hand side is the empty string (i.e. if
  // this is really a flat string in a cons string). If that is not
  // the case we would rather go to the runtime system now to flatten
  // the string.
  __ bind(&cons_string);
  __ lwz(result, FieldMemOperand(string, ConsString::kSecondOffset));
  __ CompareRoot(result, Heap::kEmptyStringRootIndex);
  __ bne(call_runtime);
  // Get the first of the two strings and load its instance type.
  __ lwz(string, FieldMemOperand(string, ConsString::kFirstOffset));

  __ bind(&indirect_string_loaded);
  __ lwz(result, FieldMemOperand(string, HeapObject::kMapOffset));
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
  STATIC_ASSERT(SeqTwoByteString::kHeaderSize == SeqAsciiString::kHeaderSize);
  __ addi(string,
          string,
          Operand(SeqTwoByteString::kHeaderSize - kHeapObjectTag));
  __ jmp(&check_encoding);

  // Handle external strings.
  __ bind(&external_string);
  if (FLAG_debug_code) {
    // Assert that we do not have a cons or slice (indirect strings) here.
    // Sequential strings have already been ruled out.
    __ andi(r0, result, Operand(kIsIndirectStringMask));
    __ Assert(eq, "external string expected, but not found", cr0);
  }
  // Rule out short external strings.
  STATIC_CHECK(kShortExternalStringTag != 0);
  __ andi(r0, result, Operand(kShortExternalStringMask));
  __ bne(call_runtime, cr0);
  __ lwz(string, FieldMemOperand(string, ExternalString::kResourceDataOffset));

  Label ascii, done;
  __ bind(&check_encoding);
  STATIC_ASSERT(kTwoByteStringTag == 0);
  __ andi(r0, result, Operand(kStringEncodingMask));
  __ bne(&ascii, cr0);
  // Two-byte string.
  __ slwi(result, index, Operand(1));
  __ add(result, result, string);
  __ lhz(result, MemOperand(result));
  __ jmp(&done);
  __ bind(&ascii);
  // Ascii string.
  __ add(result, string, index);
  __ lbz(result, MemOperand(result));
  __ bind(&done);
}

#undef __

} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_PPC
