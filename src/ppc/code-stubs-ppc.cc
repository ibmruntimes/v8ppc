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

#if V8_TARGET_ARCH_PPC

#include "bootstrapper.h"
#include "code-stubs.h"
#include "regexp-macro-assembler.h"
#include "stub-cache.h"
#include "ppc/regexp-macro-assembler-ppc.h"

namespace v8 {
namespace internal {


void FastNewClosureStub::InitializeInterfaceDescriptor(
    Isolate* isolate,
    CodeStubInterfaceDescriptor* descriptor) {
  static Register registers[] = { r5 };
  descriptor->register_param_count_ = 1;
  descriptor->register_params_ = registers;
  descriptor->deoptimization_handler_ =
      Runtime::FunctionForId(Runtime::kNewClosureFromStubFailure)->entry;
}


void ToNumberStub::InitializeInterfaceDescriptor(
    Isolate* isolate,
    CodeStubInterfaceDescriptor* descriptor) {
  static Register registers[] = { r3 };
  descriptor->register_param_count_ = 1;
  descriptor->register_params_ = registers;
  descriptor->deoptimization_handler_ = NULL;
}


void NumberToStringStub::InitializeInterfaceDescriptor(
    Isolate* isolate,
    CodeStubInterfaceDescriptor* descriptor) {
  static Register registers[] = { r3 };
  descriptor->register_param_count_ = 1;
  descriptor->register_params_ = registers;
  descriptor->deoptimization_handler_ =
      Runtime::FunctionForId(Runtime::kNumberToString)->entry;
}


void FastCloneShallowArrayStub::InitializeInterfaceDescriptor(
    Isolate* isolate,
    CodeStubInterfaceDescriptor* descriptor) {
  static Register registers[] = { r6, r5, r4 };
  descriptor->register_param_count_ = 3;
  descriptor->register_params_ = registers;
  descriptor->deoptimization_handler_ =
      Runtime::FunctionForId(Runtime::kCreateArrayLiteralShallow)->entry;
}


void FastCloneShallowObjectStub::InitializeInterfaceDescriptor(
    Isolate* isolate,
    CodeStubInterfaceDescriptor* descriptor) {
  static Register registers[] = { r6, r5, r4, r3 };
  descriptor->register_param_count_ = 4;
  descriptor->register_params_ = registers;
  descriptor->deoptimization_handler_ =
      Runtime::FunctionForId(Runtime::kCreateObjectLiteral)->entry;
}


void CreateAllocationSiteStub::InitializeInterfaceDescriptor(
    Isolate* isolate,
    CodeStubInterfaceDescriptor* descriptor) {
  static Register registers[] = { r5 };
  descriptor->register_param_count_ = 1;
  descriptor->register_params_ = registers;
  descriptor->deoptimization_handler_ = NULL;
}


void KeyedLoadFastElementStub::InitializeInterfaceDescriptor(
    Isolate* isolate,
    CodeStubInterfaceDescriptor* descriptor) {
  static Register registers[] = { r4, r3 };
  descriptor->register_param_count_ = 2;
  descriptor->register_params_ = registers;
  descriptor->deoptimization_handler_ =
      FUNCTION_ADDR(KeyedLoadIC_MissFromStubFailure);
}


void LoadFieldStub::InitializeInterfaceDescriptor(
    Isolate* isolate,
    CodeStubInterfaceDescriptor* descriptor) {
  static Register registers[] = { r3 };
  descriptor->register_param_count_ = 1;
  descriptor->register_params_ = registers;
  descriptor->deoptimization_handler_ = NULL;
}


void KeyedLoadFieldStub::InitializeInterfaceDescriptor(
    Isolate* isolate,
    CodeStubInterfaceDescriptor* descriptor) {
  static Register registers[] = { r4 };
  descriptor->register_param_count_ = 1;
  descriptor->register_params_ = registers;
  descriptor->deoptimization_handler_ = NULL;
}


void KeyedStoreFastElementStub::InitializeInterfaceDescriptor(
    Isolate* isolate,
    CodeStubInterfaceDescriptor* descriptor) {
  static Register registers[] = { r5, r4, r3 };
  descriptor->register_param_count_ = 3;
  descriptor->register_params_ = registers;
  descriptor->deoptimization_handler_ =
      FUNCTION_ADDR(KeyedStoreIC_MissFromStubFailure);
}


void TransitionElementsKindStub::InitializeInterfaceDescriptor(
    Isolate* isolate,
    CodeStubInterfaceDescriptor* descriptor) {
  static Register registers[] = { r3, r4 };
  descriptor->register_param_count_ = 2;
  descriptor->register_params_ = registers;
  Address entry =
      Runtime::FunctionForId(Runtime::kTransitionElementsKind)->entry;
  descriptor->deoptimization_handler_ = FUNCTION_ADDR(entry);
}


void CompareNilICStub::InitializeInterfaceDescriptor(
    Isolate* isolate,
    CodeStubInterfaceDescriptor* descriptor) {
  static Register registers[] = { r3 };
  descriptor->register_param_count_ = 1;
  descriptor->register_params_ = registers;
  descriptor->deoptimization_handler_ =
      FUNCTION_ADDR(CompareNilIC_Miss);
  descriptor->SetMissHandler(
      ExternalReference(IC_Utility(IC::kCompareNilIC_Miss), isolate));
}


void BinaryOpStub::InitializeInterfaceDescriptor(
    Isolate* isolate,
    CodeStubInterfaceDescriptor* descriptor) {
  static Register registers[] = { r4, r3 };
  descriptor->register_param_count_ = 2;
  descriptor->register_params_ = registers;
  descriptor->deoptimization_handler_ = FUNCTION_ADDR(BinaryOpIC_Miss);
  descriptor->SetMissHandler(
      ExternalReference(IC_Utility(IC::kBinaryOpIC_Miss), isolate));
}


static void InitializeArrayConstructorDescriptor(
    Isolate* isolate,
    CodeStubInterfaceDescriptor* descriptor,
    int constant_stack_parameter_count) {
  // register state
  // r3 -- number of arguments
  // r4 -- function
  // r5 -- type info cell with elements kind
  static Register registers[] = { r4, r5 };
  descriptor->register_param_count_ = 2;
  if (constant_stack_parameter_count != 0) {
    // stack param count needs (constructor pointer, and single argument)
    descriptor->stack_parameter_count_ = r3;
  }
  descriptor->hint_stack_parameter_count_ = constant_stack_parameter_count;
  descriptor->register_params_ = registers;
  descriptor->function_mode_ = JS_FUNCTION_STUB_MODE;
  descriptor->deoptimization_handler_ =
      Runtime::FunctionForId(Runtime::kArrayConstructor)->entry;
}


static void InitializeInternalArrayConstructorDescriptor(
    Isolate* isolate,
    CodeStubInterfaceDescriptor* descriptor,
    int constant_stack_parameter_count) {
  // register state
  // r3 -- number of arguments
  // r4 -- constructor function
  static Register registers[] = { r4 };
  descriptor->register_param_count_ = 1;

  if (constant_stack_parameter_count != 0) {
    // stack param count needs (constructor pointer, and single argument)
    descriptor->stack_parameter_count_ = r3;
  }
  descriptor->hint_stack_parameter_count_ = constant_stack_parameter_count;
  descriptor->register_params_ = registers;
  descriptor->function_mode_ = JS_FUNCTION_STUB_MODE;
  descriptor->deoptimization_handler_ =
      Runtime::FunctionForId(Runtime::kInternalArrayConstructor)->entry;
}


void ArrayNoArgumentConstructorStub::InitializeInterfaceDescriptor(
    Isolate* isolate,
    CodeStubInterfaceDescriptor* descriptor) {
  InitializeArrayConstructorDescriptor(isolate, descriptor, 0);
}


void ArraySingleArgumentConstructorStub::InitializeInterfaceDescriptor(
    Isolate* isolate,
    CodeStubInterfaceDescriptor* descriptor) {
  InitializeArrayConstructorDescriptor(isolate, descriptor, 1);
}


void ArrayNArgumentsConstructorStub::InitializeInterfaceDescriptor(
    Isolate* isolate,
    CodeStubInterfaceDescriptor* descriptor) {
  InitializeArrayConstructorDescriptor(isolate, descriptor, -1);
}


void ToBooleanStub::InitializeInterfaceDescriptor(
    Isolate* isolate,
    CodeStubInterfaceDescriptor* descriptor) {
  static Register registers[] = { r3 };
  descriptor->register_param_count_ = 1;
  descriptor->register_params_ = registers;
  descriptor->deoptimization_handler_ =
      FUNCTION_ADDR(ToBooleanIC_Miss);
  descriptor->SetMissHandler(
      ExternalReference(IC_Utility(IC::kToBooleanIC_Miss), isolate));
}


void InternalArrayNoArgumentConstructorStub::InitializeInterfaceDescriptor(
    Isolate* isolate,
    CodeStubInterfaceDescriptor* descriptor) {
  InitializeInternalArrayConstructorDescriptor(isolate, descriptor, 0);
}


void InternalArraySingleArgumentConstructorStub::InitializeInterfaceDescriptor(
    Isolate* isolate,
    CodeStubInterfaceDescriptor* descriptor) {
  InitializeInternalArrayConstructorDescriptor(isolate, descriptor, 1);
}


void InternalArrayNArgumentsConstructorStub::InitializeInterfaceDescriptor(
    Isolate* isolate,
    CodeStubInterfaceDescriptor* descriptor) {
  InitializeInternalArrayConstructorDescriptor(isolate, descriptor, -1);
}


void StoreGlobalStub::InitializeInterfaceDescriptor(
    Isolate* isolate,
    CodeStubInterfaceDescriptor* descriptor) {
  static Register registers[] = { r4, r5, r3 };
  descriptor->register_param_count_ = 3;
  descriptor->register_params_ = registers;
  descriptor->deoptimization_handler_ =
      FUNCTION_ADDR(StoreIC_MissFromStubFailure);
}


void ElementsTransitionAndStoreStub::InitializeInterfaceDescriptor(
    Isolate* isolate,
    CodeStubInterfaceDescriptor* descriptor) {
  static Register registers[] = { r3, r6, r4, r5 };
  descriptor->register_param_count_ = 4;
  descriptor->register_params_ = registers;
  descriptor->deoptimization_handler_ =
      FUNCTION_ADDR(ElementsTransitionAndStoreIC_Miss);
}


#define __ ACCESS_MASM(masm)


static void EmitIdenticalObjectComparison(MacroAssembler* masm,
                                          Label* slow,
                                          Condition cond);
static void EmitSmiNonsmiComparison(MacroAssembler* masm,
                                    Register lhs,
                                    Register rhs,
                                    Label* lhs_not_nan,
                                    Label* slow,
                                    bool strict);
static void EmitStrictTwoHeapObjectCompare(MacroAssembler* masm,
                                           Register lhs,
                                           Register rhs);


void HydrogenCodeStub::GenerateLightweightMiss(MacroAssembler* masm) {
  // Update the static counter each time a new code stub is generated.
  Isolate* isolate = masm->isolate();
  isolate->counters()->code_stubs()->Increment();

  CodeStubInterfaceDescriptor* descriptor = GetInterfaceDescriptor(isolate);
  int param_count = descriptor->register_param_count_;
  {
    // Call the runtime system in a fresh internal frame.
    FrameScope scope(masm, StackFrame::INTERNAL);
    ASSERT(descriptor->register_param_count_ == 0 ||
           r3.is(descriptor->register_params_[param_count - 1]));
    // Push arguments
    for (int i = 0; i < param_count; ++i) {
      __ push(descriptor->register_params_[i]);
    }
    ExternalReference miss = descriptor->miss_handler();
    __ CallExternalReference(miss, descriptor->register_param_count_);
  }

  __ Ret();
}


void FastNewContextStub::Generate(MacroAssembler* masm) {
  // Try to allocate the context in new space.
  Label gc;
  int length = slots_ + Context::MIN_CONTEXT_SLOTS;

  // Attempt to allocate the context in new space.
  __ Allocate(FixedArray::SizeFor(length), r3, r4, r5, &gc, TAG_OBJECT);

  // Load the function from the stack.
  __ LoadP(r6, MemOperand(sp, 0));

  // Set up the object header.
  __ LoadRoot(r4, Heap::kFunctionContextMapRootIndex);
  __ LoadSmiLiteral(r5, Smi::FromInt(length));
  __ StoreP(r5, FieldMemOperand(r3, FixedArray::kLengthOffset), r0);
  __ StoreP(r4, FieldMemOperand(r3, HeapObject::kMapOffset), r0);

  // Set up the fixed slots, copy the global object from the previous context.
  __ LoadP(r5,
           MemOperand(cp, Context::SlotOffset(Context::GLOBAL_OBJECT_INDEX)));
  __ LoadSmiLiteral(r4, Smi::FromInt(0));
  __ StoreP(r6, MemOperand(r3, Context::SlotOffset(Context::CLOSURE_INDEX)),
            r0);
  __ StoreP(cp, MemOperand(r3, Context::SlotOffset(Context::PREVIOUS_INDEX)),
            r0);
  __ StoreP(r4, MemOperand(r3, Context::SlotOffset(Context::EXTENSION_INDEX)),
            r0);
  __ StoreP(r5,
            MemOperand(r3, Context::SlotOffset(Context::GLOBAL_OBJECT_INDEX)),
            r0);

  // Initialize the rest of the slots to undefined.
  __ LoadRoot(r4, Heap::kUndefinedValueRootIndex);
  for (int i = Context::MIN_CONTEXT_SLOTS; i < length; i++) {
    __ StoreP(r4, MemOperand(r3, Context::SlotOffset(i)), r0);
  }

  // Remove the on-stack argument and return.
  __ mr(cp, r3);
  __ pop();
  __ Ret();

  // Need to collect. Call into runtime system.
  __ bind(&gc);
  __ TailCallRuntime(Runtime::kNewFunctionContext, 1, 1);
}


void FastNewBlockContextStub::Generate(MacroAssembler* masm) {
  // Stack layout on entry:
  //
  // [sp]: function.
  // [sp + kPointerSize]: serialized scope info

  // Try to allocate the context in new space.
  Label gc;
  int length = slots_ + Context::MIN_CONTEXT_SLOTS;
  __ Allocate(FixedArray::SizeFor(length), r3, r4, r5, &gc, TAG_OBJECT);

  // Load the function from the stack.
  __ LoadP(r6, MemOperand(sp, 0));

  // Load the serialized scope info from the stack.
  __ LoadP(r4, MemOperand(sp, 1 * kPointerSize));

  // Set up the object header.
  __ LoadRoot(r5, Heap::kBlockContextMapRootIndex);
  __ StoreP(r5, FieldMemOperand(r3, HeapObject::kMapOffset), r0);
  __ LoadSmiLiteral(r5, Smi::FromInt(length));
  __ StoreP(r5, FieldMemOperand(r3, FixedArray::kLengthOffset), r0);

  // If this block context is nested in the native context we get a smi
  // sentinel instead of a function. The block context should get the
  // canonical empty function of the native context as its closure which
  // we still have to look up.
  Label after_sentinel;
  __ JumpIfNotSmi(r6, &after_sentinel);
  if (FLAG_debug_code) {
    __ cmpi(r6, Operand::Zero());
    __ Assert(eq, kExpected0AsASmiSentinel);
  }
  __ LoadP(r6, GlobalObjectOperand());
  __ LoadP(r6, FieldMemOperand(r6, GlobalObject::kNativeContextOffset));
  __ LoadP(r6, ContextOperand(r6, Context::CLOSURE_INDEX));
  __ bind(&after_sentinel);

  // Set up the fixed slots, copy the global object from the previous context.
  __ LoadP(r5, ContextOperand(cp, Context::GLOBAL_OBJECT_INDEX));
  __ StoreP(r6, ContextOperand(r3, Context::CLOSURE_INDEX), r0);
  __ StoreP(cp, ContextOperand(r3, Context::PREVIOUS_INDEX), r0);
  __ StoreP(r4, ContextOperand(r3, Context::EXTENSION_INDEX), r0);
  __ StoreP(r5, ContextOperand(r3, Context::GLOBAL_OBJECT_INDEX), r0);

  // Initialize the rest of the slots to the hole value.
  __ LoadRoot(r4, Heap::kTheHoleValueRootIndex);
  for (int i = 0; i < slots_; i++) {
    __ StoreP(r4, ContextOperand(r3, i + Context::MIN_CONTEXT_SLOTS), r0);
  }

  // Remove the on-stack argument and return.
  __ mr(cp, r3);
  __ addi(sp, sp, Operand(2 * kPointerSize));
  __ Ret();

  // Need to collect. Call into runtime system.
  __ bind(&gc);
  __ TailCallRuntime(Runtime::kPushBlockContext, 2, 1);
}


#if 0  // roohack unused?
// Takes a Smi and converts to an IEEE 64 bit floating point value in two
// registers.  The format is 1 sign bit, 11 exponent bits (biased 1023) and
// 52 fraction bits (20 in the first word, 32 in the second).  Zeros is a
// scratch register.  Destroys the source register.  No GC occurs during this
// stub so you don't have to set up the frame.
class ConvertToDoubleStub : public PlatformCodeStub {
 public:
  ConvertToDoubleStub(Register result_reg_1,
                      Register result_reg_2,
                      Register source_reg,
                      Register scratch_reg)
      : result1_(result_reg_1),
        result2_(result_reg_2),
        source_(source_reg),
        zeros_(scratch_reg) { }

 private:
  Register result1_;
  Register result2_;
  Register source_;
  Register zeros_;

  // Minor key encoding in 16 bits.
  class ModeBits: public BitField<OverwriteMode, 0, 2> {};
  class OpBits: public BitField<Token::Value, 2, 14> {};

  Major MajorKey() { return ConvertToDouble; }
  int MinorKey() {
    // Encode the parameters in a unique 16 bit value.
    return  result1_.code() +
           (result2_.code() << 4) +
           (source_.code() << 8) +
           (zeros_.code() << 12);
  }

  void Generate(MacroAssembler* masm);
};


void ConvertToDoubleStub::Generate(MacroAssembler* masm) {
  Register exponent = result1_;
  Register mantissa = result2_;

  Label not_special;
  __ SmiUntag(source_);
  // Move sign bit from source to destination.  This works because the sign bit
  // in the exponent word of the double has the same position and polarity as
  // the 2's complement sign bit in a Smi.
  STATIC_ASSERT(HeapNumber::kSignMask == 0x80000000u);
  __ and_(exponent, source_, Operand(HeapNumber::kSignMask), SetCC);
  // Subtract from 0 if source was negative.
  __ rsb(source_, source_, Operand::Zero(), LeaveCC, ne);

  // We have -1, 0 or 1, which we treat specially. Register source_ contains
  // absolute value: it is either equal to 1 (special case of -1 and 1),
  // greater than 1 (not a special case) or less than 1 (special case of 0).
  __ cmp(source_, Operand(1));
  __ b(gt, &not_special);

  // For 1 or -1 we need to or in the 0 exponent (biased to 1023).
  const uint32_t exponent_word_for_1 =
      HeapNumber::kExponentBias << HeapNumber::kExponentShift;
  __ orr(exponent, exponent, Operand(exponent_word_for_1), LeaveCC, eq);
  // 1, 0 and -1 all have 0 for the second word.
  __ mov(mantissa, Operand::Zero());
  __ Ret();

  __ bind(&not_special);
  __ clz(zeros_, source_);
  // Compute exponent and or it into the exponent register.
  // We use mantissa as a scratch register here.  Use a fudge factor to
  // divide the constant 31 + HeapNumber::kExponentBias, 0x41d, into two parts
  // that fit in the ARM's constant field.
  int fudge = 0x400;
  __ rsb(mantissa, zeros_, Operand(31 + HeapNumber::kExponentBias - fudge));
  __ add(mantissa, mantissa, Operand(fudge));
  __ orr(exponent,
         exponent,
         Operand(mantissa, LSL, HeapNumber::kExponentShift));
  // Shift up the source chopping the top bit off.
  __ add(zeros_, zeros_, Operand(1));
  // This wouldn't work for 1.0 or -1.0 as the shift would be 32 which means 0.
  __ mov(source_, Operand(source_, LSL, zeros_));
  // Compute lower part of fraction (last 12 bits).
  __ mov(mantissa, Operand(source_, LSL, HeapNumber::kMantissaBitsInTopWord));
  // And the top (top 20 bits).
  __ orr(exponent,
         exponent,
         Operand(source_, LSR, 32 - HeapNumber::kMantissaBitsInTopWord));
  __ Ret();
}
#endif  // roohack


void DoubleToIStub::Generate(MacroAssembler* masm) {
  Label out_of_range, only_low, negate, done, fastpath_done;
  Register input_reg = source();
  Register result_reg = destination();
  int double_offset = offset();

  // Immediate values for this stub fit in instructions, so it's safe to use ip.
  Register scratch = ip;
  Register scratch_low =
      GetRegisterThatIsNotOneOf(input_reg, result_reg, scratch);
  Register scratch_high =
      GetRegisterThatIsNotOneOf(input_reg, result_reg, scratch, scratch_low);
  DoubleRegister double_scratch = kScratchDoubleReg;

  if (!skip_fastpath()) {
    // Load double input.
    __ lfd(double_scratch, MemOperand(input_reg, double_offset));

    // Do fast-path convert from double to int.
    __ ConvertDoubleToInt64(double_scratch, result_reg,
#if !V8_TARGET_ARCH_PPC64
                            scratch,
#endif
                            d0);

    // Test for overflow
#if V8_TARGET_ARCH_PPC64
    __ TestIfInt32(result_reg, scratch, r0);
#else
    __ TestIfInt32(scratch, result_reg, r0);
#endif
    __ beq(&fastpath_done);
  }

  __ Push(scratch_high, scratch_low);

  // Account for saved regs if input is sp.
  if (input_reg.is(sp)) double_offset += 2 * kPointerSize;
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
  __ lwz(scratch_high, MemOperand(input_reg, double_offset + 4));
  __ lwz(scratch_low, MemOperand(input_reg, double_offset));
#else
  __ lwz(scratch_high, MemOperand(input_reg, double_offset));
  __ lwz(scratch_low, MemOperand(input_reg, double_offset + 4));
#endif

  __ ExtractBitMask(scratch, scratch_high, HeapNumber::kExponentMask);
  // Load scratch with exponent - 1. This is faster than loading
  // with exponent because Bias + 1 = 1024 which is a *PPC* immediate value.
  STATIC_ASSERT(HeapNumber::kExponentBias + 1 == 1024);
  __ subi(scratch, scratch, Operand(HeapNumber::kExponentBias + 1));
  // If exponent is greater than or equal to 84, the 32 less significant
  // bits are 0s (2^84 = 1, 52 significant bits, 32 uncoded bits),
  // the result is 0.
  // Compare exponent with 84 (compare exponent - 1 with 83).
  __ cmpi(scratch, Operand(83));
  __ bge(&out_of_range);

  // If we reach this code, 31 <= exponent <= 83.
  // So, we don't have to handle cases where 0 <= exponent <= 20 for
  // which we would need to shift right the high part of the mantissa.
  // Scratch contains exponent - 1.
  // Load scratch with 52 - exponent (load with 51 - (exponent - 1)).
  __ subfic(scratch, scratch, Operand(51));
  __ cmpi(scratch, Operand::Zero());
  __ ble(&only_low);
  // 21 <= exponent <= 51, shift scratch_low and scratch_high
  // to generate the result.
  __ srw(scratch_low, scratch_low, scratch);
  // Scratch contains: 52 - exponent.
  // We needs: exponent - 20.
  // So we use: 32 - scratch = 32 - 52 + exponent = exponent - 20.
  __ subfic(scratch, scratch, Operand(32));
  __ ExtractBitMask(result_reg, scratch_high, HeapNumber::kMantissaMask);
  // Set the implicit 1 before the mantissa part in scratch_high.
  STATIC_ASSERT(HeapNumber::kMantissaBitsInTopWord >= 16);
  __ oris(result_reg, result_reg,
          Operand(1 << ((HeapNumber::kMantissaBitsInTopWord) - 16)));
  __ slw(r0, result_reg, scratch);
  __ orx(result_reg, scratch_low, r0);
  __ b(&negate);

  __ bind(&out_of_range);
  __ mov(result_reg, Operand::Zero());
  __ b(&done);

  __ bind(&only_low);
  // 52 <= exponent <= 83, shift only scratch_low.
  // On entry, scratch contains: 52 - exponent.
  __ neg(scratch, scratch);
  __ slw(result_reg, scratch_low, scratch);

  __ bind(&negate);
  // If input was positive, scratch_high ASR 31 equals 0 and
  // scratch_high LSR 31 equals zero.
  // New result = (result eor 0) + 0 = result.
  // If the input was negative, we have to negate the result.
  // Input_high ASR 31 equals 0xffffffff and scratch_high LSR 31 equals 1.
  // New result = (result eor 0xffffffff) + 1 = 0 - result.
  __ srawi(r0, scratch_high, 31);
  __ xor_(result_reg, result_reg, r0);
  __ srwi(r0, scratch_high, Operand(31));
  __ add(result_reg, result_reg, r0);

  __ bind(&done);
  __ Pop(scratch_high, scratch_low);

  __ bind(&fastpath_done);
  __ Ret();
}


#if 0  // roohack unused?
bool WriteInt32ToHeapNumberStub::IsPregenerated(Isolate* isolate) {
  // These variants are compiled ahead of time.  See next method.
  if (the_int_.is(r4) && the_heap_number_.is(r3) && scratch_.is(r5)) {
    return true;
  }
  if (the_int_.is(r5) && the_heap_number_.is(r3) && scratch_.is(r6)) {
    return true;
  }
  // Other register combinations are generated as and when they are needed,
  // so it is unsafe to call them from stubs (we can't generate a stub while
  // we are generating a stub).
  return false;
}


void WriteInt32ToHeapNumberStub::GenerateFixedRegStubsAheadOfTime(
    Isolate* isolate) {
  WriteInt32ToHeapNumberStub stub1(r4, r3, r5);
  WriteInt32ToHeapNumberStub stub2(r5, r3, r6);
  stub1.GetCode(isolate)->set_is_pregenerated(true);
  stub2.GetCode(isolate)->set_is_pregenerated(true);
}


// See comment for class.
void WriteInt32ToHeapNumberStub::Generate(MacroAssembler* masm) {
  Label max_negative_int;
  // the_int_ has the answer which is a signed int32 but not a Smi.
  // We test for the special value that has a different exponent.  This test
  // has the neat side effect of setting the flags according to the sign.
  STATIC_ASSERT(HeapNumber::kSignMask == 0x80000000u);
  __ cmp(the_int_, Operand(0x80000000u));
  __ b(eq, &max_negative_int);
  // Set up the correct exponent in scratch_.  All non-Smi int32s have the same.
  // A non-Smi integer is 1.xxx * 2^30 so the exponent is 30 (biased).
  uint32_t non_smi_exponent =
      (HeapNumber::kExponentBias + 30) << HeapNumber::kExponentShift;
  __ mov(scratch_, Operand(non_smi_exponent));
  // Set the sign bit in scratch_ if the value was negative.
  __ orr(scratch_, scratch_, Operand(HeapNumber::kSignMask), LeaveCC, cs);
  // Subtract from 0 if the value was negative.
  __ rsb(the_int_, the_int_, Operand::Zero(), LeaveCC, cs);
  // We should be masking the implict first digit of the mantissa away here,
  // but it just ends up combining harmlessly with the last digit of the
  // exponent that happens to be 1.  The sign bit is 0 so we shift 10 to get
  // the most significant 1 to hit the last bit of the 12 bit sign and exponent.
  ASSERT(((1 << HeapNumber::kExponentShift) & non_smi_exponent) != 0);
  const int shift_distance = HeapNumber::kNonMantissaBitsInTopWord - 2;
  __ orr(scratch_, scratch_, Operand(the_int_, LSR, shift_distance));
  __ str(scratch_, FieldMemOperand(the_heap_number_,
                                   HeapNumber::kExponentOffset));
  __ mov(scratch_, Operand(the_int_, LSL, 32 - shift_distance));
  __ str(scratch_, FieldMemOperand(the_heap_number_,
                                   HeapNumber::kMantissaOffset));
  __ Ret();

  __ bind(&max_negative_int);
  // The max negative int32 is stored as a positive number in the mantissa of
  // a double because it uses a sign bit instead of using two's complement.
  // The actual mantissa bits stored are all 0 because the implicit most
  // significant 1 bit is not stored.
  non_smi_exponent += 1 << HeapNumber::kExponentShift;
  __ mov(ip, Operand(HeapNumber::kSignMask | non_smi_exponent));
  __ str(ip, FieldMemOperand(the_heap_number_, HeapNumber::kExponentOffset));
  __ mov(ip, Operand::Zero());
  __ str(ip, FieldMemOperand(the_heap_number_, HeapNumber::kMantissaOffset));
  __ Ret();
}
#endif  // roohack

// Handle the case where the lhs and rhs are the same object.
// Equality is almost reflexive (everything but NaN), so this is a test
// for "identity and not NaN".
static void EmitIdenticalObjectComparison(MacroAssembler* masm,
                                          Label* slow,
                                          Condition cond) {
  Label not_identical;
  Label heap_number, return_equal;
  __ cmp(r3, r4);
  __ bne(&not_identical);

  // Test for NaN. Sadly, we can't just compare to Factory::nan_value(),
  // so we do the second best thing - test it ourselves.
  // They are both equal and they are not both Smis so both of them are not
  // Smis.  If it's not a heap number, then return equal.
  if (cond == lt || cond == gt) {
    __ CompareObjectType(r3, r7, r7, FIRST_SPEC_OBJECT_TYPE);
    __ bge(slow);
  } else {
    __ CompareObjectType(r3, r7, r7, HEAP_NUMBER_TYPE);
    __ beq(&heap_number);
    // Comparing JS objects with <=, >= is complicated.
    if (cond != eq) {
      __ cmpi(r7, Operand(FIRST_SPEC_OBJECT_TYPE));
      __ bge(slow);
      // Normally here we fall through to return_equal, but undefined is
      // special: (undefined == undefined) == true, but
      // (undefined <= undefined) == false!  See ECMAScript 11.8.5.
      if (cond == le || cond == ge) {
        __ cmpi(r7, Operand(ODDBALL_TYPE));
        __ bne(&return_equal);
        __ LoadRoot(r5, Heap::kUndefinedValueRootIndex);
        __ cmp(r3, r5);
        __ bne(&return_equal);
        if (cond == le) {
          // undefined <= undefined should fail.
          __ li(r3, Operand(GREATER));
        } else  {
          // undefined >= undefined should fail.
          __ li(r3, Operand(LESS));
        }
        __ Ret();
      }
    }
  }

  __ bind(&return_equal);
  if (cond == lt) {
    __ li(r3, Operand(GREATER));  // Things aren't less than themselves.
  } else if (cond == gt) {
    __ li(r3, Operand(LESS));     // Things aren't greater than themselves.
  } else {
    __ li(r3, Operand(EQUAL));    // Things are <=, >=, ==, === themselves.
  }
  __ Ret();

  // For less and greater we don't have to check for NaN since the result of
  // x < x is false regardless.  For the others here is some code to check
  // for NaN.
  if (cond != lt && cond != gt) {
    __ bind(&heap_number);
    // It is a heap number, so return non-equal if it's NaN and equal if it's
    // not NaN.

    // The representation of NaN values has all exponent bits (52..62) set,
    // and not all mantissa bits (0..51) clear.
    // Read top bits of double representation (second word of value).
    __ lwz(r5, FieldMemOperand(r3, HeapNumber::kExponentOffset));
    // Test that exponent bits are all set.
    STATIC_ASSERT(HeapNumber::kExponentMask == 0x7ff00000u);
    __ ExtractBitMask(r6, r5, HeapNumber::kExponentMask);
    __ cmpli(r6, Operand(0x7ff));
    __ bne(&return_equal);

    // Shift out flag and all exponent bits, retaining only mantissa.
    __ slwi(r5, r5, Operand(HeapNumber::kNonMantissaBitsInTopWord));
    // Or with all low-bits of mantissa.
    __ lwz(r6, FieldMemOperand(r3, HeapNumber::kMantissaOffset));
    __ orx(r3, r6, r5);
    __ cmpi(r3, Operand::Zero());
    // For equal we already have the right value in r3:  Return zero (equal)
    // if all bits in mantissa are zero (it's an Infinity) and non-zero if
    // not (it's a NaN).  For <= and >= we need to load r0 with the failing
    // value if it's a NaN.
    if (cond != eq) {
      Label not_equal;
      __ bne(&not_equal);
      // All-zero means Infinity means equal.
      __ Ret();
      __ bind(&not_equal);
      if (cond == le) {
        __ li(r3, Operand(GREATER));  // NaN <= NaN should fail.
      } else {
        __ li(r3, Operand(LESS));     // NaN >= NaN should fail.
      }
    }
    __ Ret();
  }
  // No fall through here.

  __ bind(&not_identical);
}


// See comment at call site.
static void EmitSmiNonsmiComparison(MacroAssembler* masm,
                                    Register lhs,
                                    Register rhs,
                                    Label* lhs_not_nan,
                                    Label* slow,
                                    bool strict) {
  ASSERT((lhs.is(r3) && rhs.is(r4)) ||
         (lhs.is(r4) && rhs.is(r3)));

  Label rhs_is_smi;
  __ JumpIfSmi(rhs, &rhs_is_smi);

  // Lhs is a Smi.  Check whether the rhs is a heap number.
  __ CompareObjectType(rhs, r6, r7, HEAP_NUMBER_TYPE);
  if (strict) {
    // If rhs is not a number and lhs is a Smi then strict equality cannot
    // succeed.  Return non-equal
    // If rhs is r3 then there is already a non zero value in it.
    Label skip;
    __ beq(&skip);
    if (!rhs.is(r3)) {
      __ mov(r3, Operand(NOT_EQUAL));
    }
    __ Ret();
    __ bind(&skip);
  } else {
    // Smi compared non-strictly with a non-Smi non-heap-number.  Call
    // the runtime.
    __ bne(slow);
  }

  // Lhs is a smi, rhs is a number.
  // Convert lhs to a double in d7.
  __ SmiToDouble(d7, lhs);
  // Load the double from rhs, tagged HeapNumber r3, to d6.
  __ lfd(d6, FieldMemOperand(rhs, HeapNumber::kValueOffset));

  // We now have both loaded as doubles but we can skip the lhs nan check
  // since it's a smi.
  __ b(lhs_not_nan);

  __ bind(&rhs_is_smi);
  // Rhs is a smi.  Check whether the non-smi lhs is a heap number.
  __ CompareObjectType(lhs, r7, r7, HEAP_NUMBER_TYPE);
  if (strict) {
    // If lhs is not a number and rhs is a smi then strict equality cannot
    // succeed.  Return non-equal.
    // If lhs is r3 then there is already a non zero value in it.
    Label skip;
    __ beq(&skip);
    if (!lhs.is(r3)) {
      __ mov(r3, Operand(NOT_EQUAL));
    }
    __ Ret();
    __ bind(&skip);
  } else {
    // Smi compared non-strictly with a non-smi non-heap-number.  Call
    // the runtime.
    __ bne(slow);
  }

  // Rhs is a smi, lhs is a heap number.
  // Load the double from lhs, tagged HeapNumber r4, to d7.
  __ lfd(d7, FieldMemOperand(lhs, HeapNumber::kValueOffset));
  // Convert rhs to a double in d6.
  __ SmiToDouble(d6, rhs);
  // Fall through to both_loaded_as_doubles.
}


// See comment at call site.
static void EmitStrictTwoHeapObjectCompare(MacroAssembler* masm,
                                           Register lhs,
                                           Register rhs) {
    ASSERT((lhs.is(r3) && rhs.is(r4)) ||
           (lhs.is(r4) && rhs.is(r3)));

    // If either operand is a JS object or an oddball value, then they are
    // not equal since their pointers are different.
    // There is no test for undetectability in strict equality.
    STATIC_ASSERT(LAST_TYPE == LAST_SPEC_OBJECT_TYPE);
    Label first_non_object;
    // Get the type of the first operand into r5 and compare it with
    // FIRST_SPEC_OBJECT_TYPE.
    __ CompareObjectType(rhs, r5, r5, FIRST_SPEC_OBJECT_TYPE);
    __ blt(&first_non_object);

    // Return non-zero (r3 is not zero)
    Label return_not_equal;
    __ bind(&return_not_equal);
    __ Ret();

    __ bind(&first_non_object);
    // Check for oddballs: true, false, null, undefined.
    __ cmpi(r5, Operand(ODDBALL_TYPE));
    __ beq(&return_not_equal);

    __ CompareObjectType(lhs, r6, r6, FIRST_SPEC_OBJECT_TYPE);
    __ bge(&return_not_equal);

    // Check for oddballs: true, false, null, undefined.
    __ cmpi(r6, Operand(ODDBALL_TYPE));
    __ beq(&return_not_equal);

    // Now that we have the types we might as well check for
    // internalized-internalized.
    STATIC_ASSERT(kInternalizedTag == 0 && kStringTag == 0);
    __ orx(r5, r5, r6);
    __ andi(r0, r5, Operand(kIsNotStringMask | kIsNotInternalizedMask));
    __ beq(&return_not_equal, cr0);
}


// See comment at call site.
static void EmitCheckForTwoHeapNumbers(MacroAssembler* masm,
                                       Register lhs,
                                       Register rhs,
                                       Label* both_loaded_as_doubles,
                                       Label* not_heap_numbers,
                                       Label* slow) {
  ASSERT((lhs.is(r3) && rhs.is(r4)) ||
         (lhs.is(r4) && rhs.is(r3)));

  __ CompareObjectType(rhs, r6, r5, HEAP_NUMBER_TYPE);
  __ bne(not_heap_numbers);
  __ LoadP(r5, FieldMemOperand(lhs, HeapObject::kMapOffset));
  __ cmp(r5, r6);
  __ bne(slow);  // First was a heap number, second wasn't.  Go slow case.

  // Both are heap numbers.  Load them up then jump to the code we have
  // for that.
  __ lfd(d6, FieldMemOperand(rhs, HeapNumber::kValueOffset));
  __ lfd(d7, FieldMemOperand(lhs, HeapNumber::kValueOffset));

  __ b(both_loaded_as_doubles);
}


// Fast negative check for internalized-to-internalized equality.
static void EmitCheckForInternalizedStringsOrObjects(MacroAssembler* masm,
                                                     Register lhs,
                                                     Register rhs,
                                                     Label* possible_strings,
                                                     Label* not_both_strings) {
  ASSERT((lhs.is(r3) && rhs.is(r4)) ||
         (lhs.is(r4) && rhs.is(r3)));

  // r5 is object type of rhs.
  Label object_test;
  STATIC_ASSERT(kInternalizedTag == 0 && kStringTag == 0);
  __ andi(r0, r5, Operand(kIsNotStringMask));
  __ bne(&object_test, cr0);
  __ andi(r0, r5, Operand(kIsNotInternalizedMask));
  __ bne(possible_strings, cr0);
  __ CompareObjectType(lhs, r6, r6, FIRST_NONSTRING_TYPE);
  __ bge(not_both_strings);
  __ andi(r0, r6, Operand(kIsNotInternalizedMask));
  __ bne(possible_strings, cr0);

  // Both are internalized.  We already checked they weren't the same pointer
  // so they are not equal.
  __ li(r3, Operand(NOT_EQUAL));
  __ Ret();

  __ bind(&object_test);
  __ cmpi(r5, Operand(FIRST_SPEC_OBJECT_TYPE));
  __ blt(not_both_strings);
  __ CompareObjectType(lhs, r5, r6, FIRST_SPEC_OBJECT_TYPE);
  __ blt(not_both_strings);
  // If both objects are undetectable, they are equal. Otherwise, they
  // are not equal, since they are different objects and an object is not
  // equal to undefined.
  __ LoadP(r6, FieldMemOperand(rhs, HeapObject::kMapOffset));
  __ lbz(r5, FieldMemOperand(r5, Map::kBitFieldOffset));
  __ lbz(r6, FieldMemOperand(r6, Map::kBitFieldOffset));
  __ and_(r3, r5, r6);
  __ andi(r3, r3, Operand(1 << Map::kIsUndetectable));
  __ xori(r3, r3, Operand(1 << Map::kIsUndetectable));
  __ Ret();
}


static void ICCompareStub_CheckInputType(MacroAssembler* masm,
                                         Register input,
                                         Register scratch,
                                         CompareIC::State expected,
                                         Label* fail) {
  Label ok;
  if (expected == CompareIC::SMI) {
    __ JumpIfNotSmi(input, fail);
  } else if (expected == CompareIC::NUMBER) {
    __ JumpIfSmi(input, &ok);
    __ CheckMap(input, scratch, Heap::kHeapNumberMapRootIndex, fail,
                DONT_DO_SMI_CHECK);
  }
  // We could be strict about internalized/non-internalized here, but as long as
  // hydrogen doesn't care, the stub doesn't have to care either.
  __ bind(&ok);
}


// On entry r4 and r5 are the values to be compared.
// On exit r3 is 0, positive or negative to indicate the result of
// the comparison.
void ICCompareStub::GenerateGeneric(MacroAssembler* masm) {
  Register lhs = r4;
  Register rhs = r3;
  Condition cc = GetCondition();

  Label miss;
  ICCompareStub_CheckInputType(masm, lhs, r5, left_, &miss);
  ICCompareStub_CheckInputType(masm, rhs, r6, right_, &miss);

  Label slow;  // Call builtin.
  Label not_smis, both_loaded_as_doubles, lhs_not_nan;

  Label not_two_smis, smi_done;
  __ orx(r5, r4, r3);
  __ JumpIfNotSmi(r5, &not_two_smis);
  __ SmiUntag(r4);
  __ SmiUntag(r3);
  __ sub(r3, r4, r3);
  __ Ret();
  __ bind(&not_two_smis);

  // NOTICE! This code is only reached after a smi-fast-case check, so
  // it is certain that at least one operand isn't a smi.

  // Handle the case where the objects are identical.  Either returns the answer
  // or goes to slow.  Only falls through if the objects were not identical.
  EmitIdenticalObjectComparison(masm, &slow, cc);

  // If either is a Smi (we know that not both are), then they can only
  // be strictly equal if the other is a HeapNumber.
  STATIC_ASSERT(kSmiTag == 0);
  ASSERT_EQ(0, Smi::FromInt(0));
  __ and_(r5, lhs, rhs);
  __ JumpIfNotSmi(r5, &not_smis);
  // One operand is a smi.  EmitSmiNonsmiComparison generates code that can:
  // 1) Return the answer.
  // 2) Go to slow.
  // 3) Fall through to both_loaded_as_doubles.
  // 4) Jump to lhs_not_nan.
  // In cases 3 and 4 we have found out we were dealing with a number-number
  // comparison.  The double values of the numbers have been loaded
  // into d7 and d6.
  EmitSmiNonsmiComparison(masm, lhs, rhs, &lhs_not_nan, &slow, strict());

  __ bind(&both_loaded_as_doubles);
  // The arguments have been converted to doubles and stored in d6 and d7
  Isolate* isolate = masm->isolate();
  __ bind(&lhs_not_nan);
  Label no_nan;
  __ fcmpu(d7, d6);

  Label nan, equal, less_than;
  __ bunordered(&nan);
  __ beq(&equal);
  __ blt(&less_than);
  __ li(r3, Operand(GREATER));
  __ Ret();
  __ bind(&equal);
  __ li(r3, Operand(EQUAL));
  __ Ret();
  __ bind(&less_than);
  __ li(r3, Operand(LESS));
  __ Ret();

  __ bind(&nan);
  // If one of the sides was a NaN then the v flag is set.  Load r3 with
  // whatever it takes to make the comparison fail, since comparisons with NaN
  // always fail.
  if (cc == lt || cc == le) {
    __ li(r3, Operand(GREATER));
  } else {
    __ li(r3, Operand(LESS));
  }
  __ Ret();

  __ bind(&not_smis);
  // At this point we know we are dealing with two different objects,
  // and neither of them is a Smi.  The objects are in rhs_ and lhs_.
  if (strict()) {
    // This returns non-equal for some object types, or falls through if it
    // was not lucky.
    EmitStrictTwoHeapObjectCompare(masm, lhs, rhs);
  }

  Label check_for_internalized_strings;
  Label flat_string_check;
  // Check for heap-number-heap-number comparison.  Can jump to slow case,
  // or load both doubles into r3, r4, r5, r6 and jump to the code that handles
  // that case.  If the inputs are not doubles then jumps to
  // check_for_internalized_strings.
  // In this case r5 will contain the type of rhs_.  Never falls through.
  EmitCheckForTwoHeapNumbers(masm,
                             lhs,
                             rhs,
                             &both_loaded_as_doubles,
                             &check_for_internalized_strings,
                             &flat_string_check);

  __ bind(&check_for_internalized_strings);
  // In the strict case the EmitStrictTwoHeapObjectCompare already took care of
  // internalized strings.
  if (cc == eq && !strict()) {
    // Returns an answer for two internalized strings or two detectable objects.
    // Otherwise jumps to string case or not both strings case.
    // Assumes that r5 is the type of rhs_ on entry.
    EmitCheckForInternalizedStringsOrObjects(
        masm, lhs, rhs, &flat_string_check, &slow);
  }

  // Check for both being sequential ASCII strings, and inline if that is the
  // case.
  __ bind(&flat_string_check);

  __ JumpIfNonSmisNotBothSequentialAsciiStrings(lhs, rhs, r5, r6, &slow);

  __ IncrementCounter(isolate->counters()->string_compare_native(), 1, r5, r6);
  if (cc == eq) {
    StringCompareStub::GenerateFlatAsciiStringEquals(masm,
                                                     lhs,
                                                     rhs,
                                                     r5,
                                                     r6);
  } else {
    StringCompareStub::GenerateCompareFlatAsciiStrings(masm,
                                                       lhs,
                                                       rhs,
                                                       r5,
                                                       r6,
                                                       r7);
  }
  // Never falls through to here.

  __ bind(&slow);

  __ Push(lhs, rhs);
  // Figure out which native to call and setup the arguments.
  Builtins::JavaScript native;
  if (cc == eq) {
    native = strict() ? Builtins::STRICT_EQUALS : Builtins::EQUALS;
  } else {
    native = Builtins::COMPARE;
    int ncr;  // NaN compare result
    if (cc == lt || cc == le) {
      ncr = GREATER;
    } else {
      ASSERT(cc == gt || cc == ge);  // remaining cases
      ncr = LESS;
    }
    __ LoadSmiLiteral(r3, Smi::FromInt(ncr));
    __ push(r3);
  }

  // Call the native; it returns -1 (less), 0 (equal), or 1 (greater)
  // tagged as a small integer.
  __ InvokeBuiltin(native, JUMP_FUNCTION);

  __ bind(&miss);
  GenerateMiss(masm);
}


void StoreBufferOverflowStub::Generate(MacroAssembler* masm) {
  // We don't allow a GC during a store buffer overflow so there is no need to
  // store the registers in any particular way, but we do have to store and
  // restore them.
  __ mflr(r0);
  __ MultiPush(kJSCallerSaved | r0.bit());
  if (save_doubles_ == kSaveFPRegs) {
    const int kNumRegs = DoubleRegister::kNumVolatileRegisters;
    __ subi(sp, sp, Operand(kDoubleSize * kNumRegs));
    for (int i = 0; i < kNumRegs; i++) {
      DoubleRegister reg = DoubleRegister::from_code(i);
      __ stfd(reg, MemOperand(sp, i * kDoubleSize));
    }
  }
  const int argument_count = 1;
  const int fp_argument_count = 0;
  const Register scratch = r4;

  AllowExternalCallThatCantCauseGC scope(masm);
  __ PrepareCallCFunction(argument_count, fp_argument_count, scratch);
  __ mov(r3, Operand(ExternalReference::isolate_address(masm->isolate())));
  __ CallCFunction(
      ExternalReference::store_buffer_overflow_function(masm->isolate()),
      argument_count);
  if (save_doubles_ == kSaveFPRegs) {
    const int kNumRegs = DoubleRegister::kNumVolatileRegisters;
    for (int i = 0; i < kNumRegs; i++) {
      DoubleRegister reg = DoubleRegister::from_code(i);
      __ lfd(reg, MemOperand(sp, i * kDoubleSize));
    }
    __ addi(sp, sp, Operand(kDoubleSize * kNumRegs));
  }
  __ MultiPop(kJSCallerSaved | r0.bit());
  __ mtlr(r0);
  __ Ret();
}


void TranscendentalCacheStub::Generate(MacroAssembler* masm) {
  // Untagged case: double input in d2, double result goes
  //   into d2.
  // Tagged case: tagged input on top of stack and in r3,
  //   tagged result (heap number) goes into r3.

  Label input_not_smi;
  Label loaded;
  Label calculate;
  Label invalid_cache;
  const Register scratch0 = r11;
  const Register scratch1 = r10;
  const Register cache_entry = r3;
  const bool tagged = (argument_type_ == TAGGED);

  if (tagged) {
    // Argument is a number and is on stack and in r3.
    // Load argument and check if it is a smi.
    __ JumpIfNotSmi(r3, &input_not_smi);

    // Input is a smi. Convert to double and load the low and high words
    // of the double into r5, r6.

    // roohack consider a macro here for smi to double into r5, r6
    __ SmiToDouble(d6, r3);
    __ subi(sp, sp, Operand(8));
    __ stfd(d6, MemOperand(sp, 0));
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
    __ lwz(r5, MemOperand(sp));
    __ lwz(r6, MemOperand(sp, 4));
#else
    __ lwz(r5, MemOperand(sp, 4));
    __ lwz(r6, MemOperand(sp));
#endif
    __ addi(sp, sp, Operand(8));
    __ b(&loaded);

    __ bind(&input_not_smi);
    // Check if input is a HeapNumber.
    __ CheckMap(r3,
                r4,
                Heap::kHeapNumberMapRootIndex,
                &calculate,
                DONT_DO_SMI_CHECK);
    // Input is a HeapNumber. Load it to a double register and store the
    // low and high words into r5, r6.
    __ lwz(r6, FieldMemOperand(r3, HeapNumber::kExponentOffset));
    __ lwz(r5, FieldMemOperand(r3, HeapNumber::kMantissaOffset));
  } else {
    // Input is untagged double in d2. Output goes to d2.
    __ subi(sp, sp, Operand(8));
    __ stfd(d2, MemOperand(sp, 0));
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
    __ lwz(r5, MemOperand(sp, 4));
    __ lwz(r6, MemOperand(sp));
#else
    __ lwz(r5, MemOperand(sp));
    __ lwz(r6, MemOperand(sp, 4));
#endif
    __ addi(sp, sp, Operand(8));
  }
    __ bind(&loaded);
  // r5 = low 32 bits of double value
  // r6 = high 32 bits of double value
  // Compute hash (the shifts are arithmetic):
  //   h = (low ^ high); h ^= h >> 16; h ^= h >> 8; h = h & (cacheSize - 1);
  __ xor_(r4, r5, r6);
  __ srawi(scratch0, r4, 16);
  __ xor_(r4, r4, scratch0);
  __ srawi(scratch0, r4, 8);
  __ xor_(r4, r4, scratch0);
  ASSERT(IsPowerOf2(TranscendentalCache::SubCache::kCacheSize));
  __ andi(r4, r4, Operand(TranscendentalCache::SubCache::kCacheSize - 1));

  // r5 = low 32 bits of double value.
  // r6 = high 32 bits of double value.
  // r4 = TranscendentalCache::hash(double value).
  Isolate* isolate = masm->isolate();
  ExternalReference cache_array =
      ExternalReference::transcendental_cache_array_address(isolate);
  __ mov(cache_entry, Operand(cache_array));
  // cache_entry points to cache array.
  int cache_array_index
      = type_ * sizeof(isolate->transcendental_cache()->caches_[0]);
  __ LoadP(cache_entry, MemOperand(cache_entry, cache_array_index), r0);
  // r3 points to the cache for the type type_.
  // If NULL, the cache hasn't been initialized yet, so go through runtime.
  __ cmpi(cache_entry, Operand::Zero());
  __ beq(&invalid_cache);

#ifdef DEBUG
  // Check that the layout of cache elements match expectations.
  { TranscendentalCache::SubCache::Element test_elem[2];
    char* elem_start = reinterpret_cast<char*>(&test_elem[0]);
    char* elem2_start = reinterpret_cast<char*>(&test_elem[1]);
    char* elem_in0 = reinterpret_cast<char*>(&(test_elem[0].in[0]));
    char* elem_in1 = reinterpret_cast<char*>(&(test_elem[0].in[1]));
    char* elem_out = reinterpret_cast<char*>(&(test_elem[0].output));
    // Two uint_32's and a pointer.
#if V8_TARGET_ARCH_PPC64
    CHECK_EQ(16, static_cast<int>(elem2_start - elem_start));
#else
    CHECK_EQ(12, static_cast<int>(elem2_start - elem_start));
#endif
    CHECK_EQ(0, static_cast<int>(elem_in0 - elem_start));
    CHECK_EQ(kIntSize, static_cast<int>(elem_in1 - elem_start));
    CHECK_EQ(2 * kIntSize, static_cast<int>(elem_out - elem_start));
  }
#endif

#if V8_TARGET_ARCH_PPC64
  // Find the address of the r4'th entry in the cache, i.e., &r3[r4*16].
  __ ShiftLeftImm(scratch0, r4, Operand(4));
#else
  // Find the address of the r4'th entry in the cache, i.e., &r3[r4*12].
  __ ShiftLeftImm(scratch0, r4, Operand(1));
  __ add(r4, r4, scratch0);
  __ ShiftLeftImm(scratch0, r4, Operand(2));
#endif
  __ add(cache_entry, cache_entry, scratch0);
  // Check if cache matches: Double value is stored in uint32_t[2] array.
  __ lwz(r7, MemOperand(cache_entry, 0));
  __ lwz(r8, MemOperand(cache_entry, 4));
  __ LoadP(r9, MemOperand(cache_entry, 8));
  __ cmp(r5, r7);
  __ bne(&calculate);
  __ cmp(r6, r8);
  __ bne(&calculate);
  // Cache hit. Load result, cleanup and return.
  Counters* counters = masm->isolate()->counters();
  __ IncrementCounter(
      counters->transcendental_cache_hit(), 1, scratch0, scratch1);
  if (tagged) {
    // Pop input value from stack and load result into r3.
    __ pop();
    __ mr(r3, r9);
  } else {
    // Load result into d2.
    __ lfd(d2, FieldMemOperand(r9, HeapNumber::kValueOffset));
  }
  __ Ret();

  __ bind(&calculate);
  __ IncrementCounter(
      counters->transcendental_cache_miss(), 1, scratch0, scratch1);
  if (tagged) {
    __ bind(&invalid_cache);
    ExternalReference runtime_function =
        ExternalReference(RuntimeFunction(), masm->isolate());
    __ TailCallExternalReference(runtime_function, 1, 1);
  } else {
    Label no_update;
    Label skip_cache;

    // Call C function to calculate the result and update the cache.
    // r3: precalculated cache entry address.
    // r5 and r6: parts of the double value.
    // Store r3, r5 and r6 on stack for later before calling C function.
    __ Push(r6, r5, cache_entry);
    GenerateCallCFunction(masm, scratch0);
    __ GetCFunctionDoubleResult(d2);

    // Try to update the cache. If we cannot allocate a
    // heap number, we return the result without updating.
    __ Pop(r6, r5, cache_entry);
    __ LoadRoot(r8, Heap::kHeapNumberMapRootIndex);
    __ AllocateHeapNumber(r9, scratch0, scratch1, r8, &no_update);
    __ stfd(d2, FieldMemOperand(r9, HeapNumber::kValueOffset));
    __ stw(r5, MemOperand(cache_entry, 0));
    __ stw(r6, MemOperand(cache_entry, 4));
    __ StoreP(r9, MemOperand(cache_entry, 8));
    __ Ret();

    __ bind(&invalid_cache);
    // The cache is invalid. Call runtime which will recreate the
    // cache.
    __ LoadRoot(r8, Heap::kHeapNumberMapRootIndex);
    __ AllocateHeapNumber(r3, scratch0, scratch1, r8, &skip_cache);
    __ stfd(d2, FieldMemOperand(r3, HeapNumber::kValueOffset));
    {
      FrameScope scope(masm, StackFrame::INTERNAL);
      __ push(r3);
      __ CallRuntime(RuntimeFunction(), 1);
    }
    __ lfd(d2, FieldMemOperand(r3, HeapNumber::kValueOffset));
    __ Ret();

    __ bind(&skip_cache);
    // Call C function to calculate the result and answer directly
    // without updating the cache.
    GenerateCallCFunction(masm, scratch0);
    __ GetCFunctionDoubleResult(d2);
    __ bind(&no_update);

    // We return the value in d2 without adding it to the cache, but
    // we cause a scavenging GC so that future allocations will succeed.
    {
      FrameScope scope(masm, StackFrame::INTERNAL);

      // Allocate an aligned object larger than a HeapNumber.
      ASSERT(2 * kDoubleSize >= HeapNumber::kSize);
      __ LoadSmiLiteral(scratch0, Smi::FromInt(2 * kDoubleSize));
      __ push(scratch0);
      __ CallRuntimeSaveDoubles(Runtime::kAllocateInNewSpace);
    }
    __ Ret();
  }
}


void TranscendentalCacheStub::GenerateCallCFunction(MacroAssembler* masm,
                                                    Register scratch) {
  Isolate* isolate = masm->isolate();

  __ mflr(r0);
  __ push(r0);
  __ PrepareCallCFunction(0, 1, scratch);
  __ fmr(d1, d2);
  AllowExternalCallThatCantCauseGC scope(masm);
  switch (type_) {
    case TranscendentalCache::SIN:
      __ CallCFunction(ExternalReference::math_sin_double_function(isolate),
          0, 1);
      break;
    case TranscendentalCache::COS:
      __ CallCFunction(ExternalReference::math_cos_double_function(isolate),
          0, 1);
      break;
    case TranscendentalCache::TAN:
      __ CallCFunction(ExternalReference::math_tan_double_function(isolate),
          0, 1);
      break;
    case TranscendentalCache::LOG:
      __ CallCFunction(ExternalReference::math_log_double_function(isolate),
          0, 1);
      break;
    default:
      UNIMPLEMENTED();
      break;
  }
  __ pop(r0);
  __ mtlr(r0);
}


Runtime::FunctionId TranscendentalCacheStub::RuntimeFunction() {
  switch (type_) {
    // Add more cases when necessary.
    case TranscendentalCache::SIN: return Runtime::kMath_sin;
    case TranscendentalCache::COS: return Runtime::kMath_cos;
    case TranscendentalCache::TAN: return Runtime::kMath_tan;
    case TranscendentalCache::LOG: return Runtime::kMath_log;
    default:
      UNIMPLEMENTED();
      return Runtime::kAbort;
  }
}


void MathPowStub::Generate(MacroAssembler* masm) {
  const Register base = r4;
  const Register exponent = r5;
  const Register heapnumbermap = r8;
  const Register heapnumber = r3;
  const DoubleRegister double_base = d1;
  const DoubleRegister double_exponent = d2;
  const DoubleRegister double_result = d3;
  const DoubleRegister double_scratch = d0;
  const Register scratch = r11;
  const Register scratch2 = r10;

  Label call_runtime, done, int_exponent;
  if (exponent_type_ == ON_STACK) {
    Label base_is_smi, unpack_exponent;
    // The exponent and base are supplied as arguments on the stack.
    // This can only happen if the stub is called from non-optimized code.
    // Load input parameters from stack to double registers.
    __ LoadP(base, MemOperand(sp, 1 * kPointerSize));
    __ LoadP(exponent, MemOperand(sp, 0 * kPointerSize));

    __ LoadRoot(heapnumbermap, Heap::kHeapNumberMapRootIndex);

    __ UntagAndJumpIfSmi(scratch, base, &base_is_smi);
    __ LoadP(scratch, FieldMemOperand(base, JSObject::kMapOffset));
    __ cmp(scratch, heapnumbermap);
    __ bne(&call_runtime);

    __ lfd(double_base, FieldMemOperand(base, HeapNumber::kValueOffset));
    __ b(&unpack_exponent);

    __ bind(&base_is_smi);
    __ ConvertIntToDouble(scratch, double_base);
    __ bind(&unpack_exponent);

    __ UntagAndJumpIfSmi(scratch, exponent, &int_exponent);
    __ LoadP(scratch, FieldMemOperand(exponent, JSObject::kMapOffset));
    __ cmp(scratch, heapnumbermap);
    __ bne(&call_runtime);

    __ lfd(double_exponent,
           FieldMemOperand(exponent, HeapNumber::kValueOffset));
  } else if (exponent_type_ == TAGGED) {
    // Base is already in double_base.
    __ UntagAndJumpIfSmi(scratch, exponent, &int_exponent);

    __ lfd(double_exponent,
           FieldMemOperand(exponent, HeapNumber::kValueOffset));
  }

  if (exponent_type_ != INTEGER) {
    // Detect integer exponents stored as double.
    __ TryDoubleToInt32Exact(scratch, double_exponent,
                             scratch2, double_scratch);
    __ beq(&int_exponent);

    if (exponent_type_ == ON_STACK) {
      // Detect square root case.  Crankshaft detects constant +/-0.5 at
      // compile time and uses DoMathPowHalf instead.  We then skip this check
      // for non-constant cases of +/-0.5 as these hardly occur.
      Label not_plus_half, not_minus_inf1, not_minus_inf2;

      // Test for 0.5.
      __ LoadDoubleLiteral(double_scratch, 0.5, scratch);
      __ fcmpu(double_exponent, double_scratch);
      __ bne(&not_plus_half);

      // Calculates square root of base.  Check for the special case of
      // Math.pow(-Infinity, 0.5) == Infinity (ECMA spec, 15.8.2.13).
      __ LoadDoubleLiteral(double_scratch, -V8_INFINITY, scratch);
      __ fcmpu(double_base, double_scratch);
      __ bne(&not_minus_inf1);
      __ fneg(double_result, double_scratch);
      __ b(&done);
      __ bind(&not_minus_inf1);

      // Add +0 to convert -0 to +0.
      __ fadd(double_scratch, double_base, kDoubleRegZero);
      __ fsqrt(double_result, double_scratch);
      __ b(&done);

      __ bind(&not_plus_half);
      __ LoadDoubleLiteral(double_scratch, -0.5, scratch);
      __ fcmpu(double_exponent, double_scratch);
      __ bne(&call_runtime);

      // Calculates square root of base.  Check for the special case of
      // Math.pow(-Infinity, -0.5) == 0 (ECMA spec, 15.8.2.13).
      __ LoadDoubleLiteral(double_scratch, -V8_INFINITY, scratch);
      __ fcmpu(double_base, double_scratch);
      __ bne(&not_minus_inf2);
      __ fmr(double_result, kDoubleRegZero);
      __ b(&done);
      __ bind(&not_minus_inf2);

      // Add +0 to convert -0 to +0.
      __ fadd(double_scratch, double_base, kDoubleRegZero);
      __ LoadDoubleLiteral(double_result, 1.0, scratch);
      __ fsqrt(double_scratch, double_scratch);
      __ fdiv(double_result, double_result, double_scratch);
      __ b(&done);
    }

    __ mflr(r0);
    __ push(r0);
    {
      AllowExternalCallThatCantCauseGC scope(masm);
      __ PrepareCallCFunction(0, 2, scratch);
      __ SetCallCDoubleArguments(double_base, double_exponent);
      __ CallCFunction(
          ExternalReference::power_double_double_function(masm->isolate()),
          0, 2);
    }
    __ pop(r0);
    __ mtlr(r0);
    __ GetCFunctionDoubleResult(double_result);
    __ b(&done);
  }

  // Calculate power with integer exponent.
  __ bind(&int_exponent);

  // Get two copies of exponent in the registers scratch and exponent.
  if (exponent_type_ == INTEGER) {
    __ mr(scratch, exponent);
  } else {
    // Exponent has previously been stored into scratch as untagged integer.
    __ mr(exponent, scratch);
  }
  __ fmr(double_scratch, double_base);  // Back up base.
  __ li(scratch2, Operand(1));
  __ ConvertIntToDouble(scratch2, double_result);

  // Get absolute value of exponent.
  Label positive_exponent;
  __ cmpi(scratch, Operand::Zero());
  __ bge(&positive_exponent);
  __ neg(scratch, scratch);
  __ bind(&positive_exponent);

  Label while_true, no_carry, loop_end;
  __ bind(&while_true);
  __ andi(scratch2, scratch, Operand(1));
  __ beq(&no_carry, cr0);
  __ fmul(double_result, double_result, double_scratch);
  __ bind(&no_carry);
  __ ShiftRightArithImm(scratch, scratch, 1, SetRC);
  __ beq(&loop_end, cr0);
  __ fmul(double_scratch, double_scratch, double_scratch);
  __ b(&while_true);
  __ bind(&loop_end);

  __ cmpi(exponent, Operand::Zero());
  __ bge(&done);

  __ li(scratch2, Operand(1));
  __ ConvertIntToDouble(scratch2, double_scratch);
  __ fdiv(double_result, double_scratch, double_result);
  // Test whether result is zero.  Bail out to check for subnormal result.
  // Due to subnormals, x^-y == (1/x)^y does not hold in all cases.
  __ fcmpu(double_result, kDoubleRegZero);
  __ bne(&done);
  // double_exponent may not containe the exponent value if the input was a
  // smi.  We set it with exponent value before bailing out.
  __ ConvertIntToDouble(exponent, double_exponent);

  // Returning or bailing out.
  Counters* counters = masm->isolate()->counters();
  if (exponent_type_ == ON_STACK) {
    // The arguments are still on the stack.
    __ bind(&call_runtime);
    __ TailCallRuntime(Runtime::kMath_pow_cfunction, 2, 1);

    // The stub is called from non-optimized code, which expects the result
    // as heap number in exponent.
    __ bind(&done);
    __ AllocateHeapNumber(
        heapnumber, scratch, scratch2, heapnumbermap, &call_runtime);
    __ stfd(double_result,
            FieldMemOperand(heapnumber, HeapNumber::kValueOffset));
    ASSERT(heapnumber.is(r3));
    __ IncrementCounter(counters->math_pow(), 1, scratch, scratch2);
    __ Ret(2);
  } else {
    __ mflr(r0);
    __ push(r0);
    {
      AllowExternalCallThatCantCauseGC scope(masm);
      __ PrepareCallCFunction(0, 2, scratch);
      __ SetCallCDoubleArguments(double_base, double_exponent);
      __ CallCFunction(
          ExternalReference::power_double_double_function(masm->isolate()),
          0, 2);
    }
    __ pop(r0);
    __ mtlr(r0);
    __ GetCFunctionDoubleResult(double_result);

    __ bind(&done);
    __ IncrementCounter(counters->math_pow(), 1, scratch, scratch2);
    __ Ret();
  }
}


bool CEntryStub::NeedsImmovableCode() {
  return true;
}


bool CEntryStub::IsPregenerated(Isolate* isolate) {
  return (!save_doubles_ || isolate->fp_stubs_generated()) &&
          result_size_ == 1;
}


void CodeStub::GenerateStubsAheadOfTime(Isolate* isolate) {
  CEntryStub::GenerateAheadOfTime(isolate);
//  WriteInt32ToHeapNumberStub::GenerateFixedRegStubsAheadOfTime(isolate);
  StoreBufferOverflowStub::GenerateFixedRegStubsAheadOfTime(isolate);
  StubFailureTrampolineStub::GenerateAheadOfTime(isolate);
  RecordWriteStub::GenerateFixedRegStubsAheadOfTime(isolate);
  ArrayConstructorStubBase::GenerateStubsAheadOfTime(isolate);
  CreateAllocationSiteStub::GenerateAheadOfTime(isolate);
//  BinaryOpStub::GenerateAheadOfTime(isolate);
}


void CodeStub::GenerateFPStubs(Isolate* isolate) {
  SaveFPRegsMode mode = kSaveFPRegs;
  CEntryStub save_doubles(1, mode);
  StoreBufferOverflowStub stub(mode);
  // These stubs might already be in the snapshot, detect that and don't
  // regenerate, which would lead to code stub initialization state being messed
  // up.
  Code* save_doubles_code;
  if (!save_doubles.FindCodeInCache(&save_doubles_code, isolate)) {
    save_doubles_code = *save_doubles.GetCode(isolate);
  }
  Code* store_buffer_overflow_code;
  if (!stub.FindCodeInCache(&store_buffer_overflow_code, isolate)) {
      store_buffer_overflow_code = *stub.GetCode(isolate);
  }
  save_doubles_code->set_is_pregenerated(true);
  store_buffer_overflow_code->set_is_pregenerated(true);
  isolate->set_fp_stubs_generated(true);
}


void CEntryStub::GenerateAheadOfTime(Isolate* isolate) {
  CEntryStub stub(1, kDontSaveFPRegs);
  Handle<Code> code = stub.GetCode(isolate);
  code->set_is_pregenerated(true);
}


static void JumpIfOOM(MacroAssembler* masm,
                      Register value,
                      Register scratch,
                      Label* oom_label) {
  STATIC_ASSERT(Failure::OUT_OF_MEMORY_EXCEPTION == 3);
  STATIC_ASSERT(kFailureTag == 3);
  __ andi(scratch, value, Operand(0xf));
  __ cmpi(scratch, Operand(0xf));
  __ beq(oom_label);
}


void CEntryStub::GenerateCore(MacroAssembler* masm,
                              Label* throw_normal_exception,
                              Label* throw_termination_exception,
                              Label* throw_out_of_memory_exception,
                              bool do_gc,
                              bool always_allocate) {
  // r3: result parameter for PerformGC, if any
  // r14: number of arguments including receiver  (C callee-saved)
  // r15: pointer to builtin function  (C callee-saved)
  // r16: pointer to the first argument (C callee-saved)
  Isolate* isolate = masm->isolate();
  Register isolate_reg = no_reg;

  if (do_gc) {
    // Passing r3.
    __ PrepareCallCFunction(2, 0, r4);
    __ mov(r4, Operand(ExternalReference::isolate_address(masm->isolate())));
    __ CallCFunction(ExternalReference::perform_gc_function(isolate),
        2, 0);
  }

  ExternalReference scope_depth =
      ExternalReference::heap_always_allocate_scope_depth(isolate);
  if (always_allocate) {
    __ mov(r3, Operand(scope_depth));
    __ lwz(r4, MemOperand(r3));
    __ addi(r4, r4, Operand(1));
    __ stw(r4, MemOperand(r3));
  }

  // Call C built-in.
#if V8_TARGET_ARCH_PPC64 && !ABI_RETURNS_OBJECT_PAIRS_IN_REGS
  if (result_size_ < 2) {
    __ mr(r3, r14);
    __ mr(r4, r16);
    isolate_reg = r5;
  } else {
    ASSERT_EQ(2, result_size_);
    // The return value is 16-byte non-scalar value.
    // Use frame storage reserved by calling function to pass return
    // buffer as implicit first argument.
    __ addi(r3, sp, Operand((kStackFrameExtraParamSlot + 1) * kPointerSize));
    __ mr(r4, r14);
    __ mr(r5, r16);
    isolate_reg = r6;
  }
#else
  // r3 = argc, r4 = argv
  __ mr(r3, r14);
  __ mr(r4, r16);
  isolate_reg = r5;
#endif

  __ mov(isolate_reg, Operand(ExternalReference::isolate_address(isolate)));

#if ABI_USES_FUNCTION_DESCRIPTORS && !defined(USE_SIMULATOR)
  // Native AIX/PPC64 Linux use a function descriptor.
  __ LoadP(ToRegister(2), MemOperand(r15, kPointerSize));  // TOC
  __ LoadP(ip, MemOperand(r15, 0));  // Instruction address
  Register target = ip;
#else
  Register target = r15;
#endif

  // To let the GC traverse the return address of the exit frames, we need to
  // know where the return address is. The CEntryStub is unmovable, so
  // we can store the address on the stack to be able to find it again and
  // we never have to restore it, because it will not change.
  // Compute the return address in lr to return to after the jump below. Pc is
  // already at '+ 8' from the current instruction but return is after three
  // instructions so add another 4 to pc to get the return address.
  { Assembler::BlockTrampolinePoolScope block_trampoline_pool(masm);
    Label here;
    __ b(&here, SetLK);
    __ bind(&here);
    __ mflr(r8);

// Constant used below is dependent on size of Call() macro instructions
    __ addi(r0, r8, Operand(20));

    __ StoreP(r0, MemOperand(sp, kStackFrameExtraParamSlot * kPointerSize));
    __ Call(target);
  }

  // roohack - do we need to (re)set FPU state?

  if (always_allocate) {
    // It's okay to clobber r5 and r6 here. Don't mess with r3 and r4
    // though (contain the result).
    __ mov(r5, Operand(scope_depth));
    __ lwz(r6, MemOperand(r5));
    __ subi(r6, r6, Operand(1));
    __ stw(r6, MemOperand(r5));
  }

  // check for failure result
  Label failure_returned;
  STATIC_ASSERT(((kFailureTag + 1) & kFailureTagMask) == 0);
#if V8_TARGET_ARCH_PPC64 && !ABI_RETURNS_OBJECT_PAIRS_IN_REGS
  // If return value is on the stack, pop it to registers.
  if (result_size_ > 1) {
    ASSERT_EQ(2, result_size_);
    __ LoadP(r4, MemOperand(r3, kPointerSize));
    __ LoadP(r3, MemOperand(r3));
  }
#endif
  // Lower 2 bits of r5 are 0 iff r3 has failure tag.
  __ addi(r5, r3, Operand(1));
  STATIC_ASSERT(kFailureTagMask < 0x8000);
  __ andi(r0, r5, Operand(kFailureTagMask));
  __ beq(&failure_returned, cr0);

  // Exit C frame and return.
  // r3:r4: result
  // sp: stack pointer
  // fp: frame pointer
  //  Callee-saved register r14 still holds argc.
  __ LeaveExitFrame(save_doubles_, r14, true);
  __ blr();

  // check if we should retry or throw exception
  Label retry;
  __ bind(&failure_returned);
  STATIC_ASSERT(Failure::RETRY_AFTER_GC == 0);
  __ andi(r0, r3, Operand(((1 << kFailureTypeTagSize) - 1) << kFailureTagSize));
  __ beq(&retry, cr0);

  // Special handling of out of memory exceptions.
  JumpIfOOM(masm, r3, ip, throw_out_of_memory_exception);

  // Retrieve the pending exception.
  __ mov(ip, Operand(ExternalReference(Isolate::kPendingExceptionAddress,
                                       isolate)));
  __ LoadP(r3, MemOperand(ip));

  // See if we just retrieved an OOM exception.
  JumpIfOOM(masm, r3, ip, throw_out_of_memory_exception);

  // Clear the pending exception.
  __ mov(r6, Operand(isolate->factory()->the_hole_value()));
  __ mov(ip, Operand(ExternalReference(Isolate::kPendingExceptionAddress,
                                       isolate)));
  __ StoreP(r6, MemOperand(ip));

  // Special handling of termination exceptions which are uncatchable
  // by javascript code.
  __ mov(r6, Operand(isolate->factory()->termination_exception()));
  __ cmp(r3, r6);
  __ beq(throw_termination_exception);

  // Handle normal exception.
  __ b(throw_normal_exception);

  __ bind(&retry);  // pass last failure (r3) as parameter (r3) when retrying
}


void CEntryStub::Generate(MacroAssembler* masm) {
  // Called from JavaScript; parameters are on stack as if calling JS function
  // r3: number of arguments including receiver
  // r4: pointer to builtin function
  // fp: frame pointer  (restored after C call)
  // sp: stack pointer  (restored as callee's sp after C call)
  // cp: current context  (C callee-saved)

  ProfileEntryHookStub::MaybeCallEntryHook(masm);

  // Result returned in r3 or r3+r4 by default.

  // NOTE: Invocations of builtins may return failure objects
  // instead of a proper result. The builtin entry handles
  // this by performing a garbage collection and retrying the
  // builtin once.

  // Compute the argv pointer in a callee-saved register.
  __ ShiftLeftImm(r16, r3, Operand(kPointerSizeLog2));
  __ add(r16, r16, sp);
  __ subi(r16, r16, Operand(kPointerSize));

  // Enter the exit frame that transitions from JavaScript to C++.
  FrameScope scope(masm, StackFrame::MANUAL);

  // Need at least one extra slot for return address location.
  int arg_stack_space = 1;

  // PPC LINUX ABI:
#if V8_TARGET_ARCH_PPC64 && !ABI_RETURNS_OBJECT_PAIRS_IN_REGS
  // Pass buffer for return value on stack if necessary
  if (result_size_ > 1) {
    ASSERT_EQ(2, result_size_);
    arg_stack_space += 2;
  }
#endif

  __ EnterExitFrame(save_doubles_, arg_stack_space);

  // Set up argc and the builtin function in callee-saved registers.
  __ mr(r14, r3);
  __ mr(r15, r4);

  // r14: number of arguments (C callee-saved)
  // r15: pointer to builtin function (C callee-saved)
  // r16: pointer to first argument (C callee-saved)

  Label throw_normal_exception;
  Label throw_termination_exception;
  Label throw_out_of_memory_exception;

  // Call into the runtime system.
  GenerateCore(masm,
               &throw_normal_exception,
               &throw_termination_exception,
               &throw_out_of_memory_exception,
               false,
               false);

  // Do space-specific GC and retry runtime call.
  GenerateCore(masm,
               &throw_normal_exception,
               &throw_termination_exception,
               &throw_out_of_memory_exception,
               true,
               false);

  // Do full GC and retry runtime call one final time.
  Failure* failure = Failure::InternalError();
  __ mov(r3, Operand(reinterpret_cast<intptr_t>(failure)));
  GenerateCore(masm,
               &throw_normal_exception,
               &throw_termination_exception,
               &throw_out_of_memory_exception,
               true,
               true);

  __ bind(&throw_out_of_memory_exception);
  // Set external caught exception to false.
  Isolate* isolate = masm->isolate();
  ExternalReference external_caught(Isolate::kExternalCaughtExceptionAddress,
                                    isolate);
  __ li(r3, Operand(false, RelocInfo::NONE32));
  __ mov(r5, Operand(external_caught));
  __ StoreP(r3, MemOperand(r5));

  // Set pending exception and r0 to out of memory exception.
  Label already_have_failure;
  JumpIfOOM(masm, r0, ip, &already_have_failure);
  Failure* out_of_memory = Failure::OutOfMemoryException(0x1);
  __ mov(r3, Operand(reinterpret_cast<intptr_t>(out_of_memory)));
  __ bind(&already_have_failure);
  __ mov(r5, Operand(ExternalReference(Isolate::kPendingExceptionAddress,
                                       isolate)));
  __ StoreP(r3, MemOperand(r5));
  // Fall through to the next label.

  __ bind(&throw_termination_exception);
  __ ThrowUncatchable(r3);

  __ bind(&throw_normal_exception);
  __ Throw(r3);
}


void JSEntryStub::GenerateBody(MacroAssembler* masm, bool is_construct) {
  // r3: code entry
  // r4: function
  // r5: receiver
  // r6: argc
  // [sp+0]: argv

  Label invoke, handler_entry, exit;

  // Called from C
#if ABI_USES_FUNCTION_DESCRIPTORS
  __ function_descriptor();
#endif

  ProfileEntryHookStub::MaybeCallEntryHook(masm);

  // PPC LINUX ABI:
  // preserve LR in pre-reserved slot in caller's frame
  __ mflr(r0);
  __ StoreP(r0, MemOperand(sp, kStackFrameLRSlot * kPointerSize));

  // Save callee saved registers on the stack.
  __ MultiPush(kCalleeSaved);

  // Floating point regs FPR0 - FRP13 are volatile
  // FPR14-FPR31 are non-volatile, but sub-calls will save them for us

//  int offset_to_argv = kPointerSize * 22; // matches (22*4) above
//  __ lwz(r7, MemOperand(sp, offset_to_argv));

  // Push a frame with special values setup to mark it as an entry frame.
  // r3: code entry
  // r4: function
  // r5: receiver
  // r6: argc
  // r7: argv
  Isolate* isolate = masm->isolate();
  __ li(r0, Operand(-1));  // Push a bad frame pointer to fail if it is used.
  __ push(r0);
  int marker = is_construct ? StackFrame::ENTRY_CONSTRUCT : StackFrame::ENTRY;
  __ LoadSmiLiteral(r0, Smi::FromInt(marker));
  __ push(r0);
  __ push(r0);
  // Save copies of the top frame descriptor on the stack.
  __ mov(r8, Operand(ExternalReference(Isolate::kCEntryFPAddress, isolate)));
  __ LoadP(r0, MemOperand(r8));
  __ push(r0);

  // Set up frame pointer for the frame to be pushed.
  __ addi(fp, sp, Operand(-EntryFrameConstants::kCallerFPOffset));

  // If this is the outermost JS call, set js_entry_sp value.
  Label non_outermost_js;
  ExternalReference js_entry_sp(Isolate::kJSEntrySPAddress, isolate);
  __ mov(r8, Operand(ExternalReference(js_entry_sp)));
  __ LoadP(r9, MemOperand(r8));
  __ cmpi(r9, Operand::Zero());
  __ bne(&non_outermost_js);
  __ StoreP(fp, MemOperand(r8));
  __ LoadSmiLiteral(ip, Smi::FromInt(StackFrame::OUTERMOST_JSENTRY_FRAME));
  Label cont;
  __ b(&cont);
  __ bind(&non_outermost_js);
  __ LoadSmiLiteral(ip, Smi::FromInt(StackFrame::INNER_JSENTRY_FRAME));
  __ bind(&cont);
  __ push(ip);  // frame-type

  // Jump to a faked try block that does the invoke, with a faked catch
  // block that sets the pending exception.
  __ b(&invoke);

  __ bind(&handler_entry);
  handler_offset_ = handler_entry.pos();
  // Caught exception: Store result (exception) in the pending exception
  // field in the JSEnv and return a failure sentinel.  Coming in here the
  // fp will be invalid because the PushTryHandler below sets it to 0 to
  // signal the existence of the JSEntry frame.
  __ mov(ip, Operand(ExternalReference(Isolate::kPendingExceptionAddress,
                                       isolate)));

  __ StoreP(r3, MemOperand(ip));
  __ mov(r3, Operand(reinterpret_cast<intptr_t>(Failure::Exception())));
  __ b(&exit);

  // Invoke: Link this frame into the handler chain.  There's only one
  // handler block in this code object, so its index is 0.
  __ bind(&invoke);
  // Must preserve r0-r4, r5-r7 are available. (needs update for PPC)
  __ PushTryHandler(StackHandler::JS_ENTRY, 0);
  // If an exception not caught by another handler occurs, this handler
  // returns control to the code after the b(&invoke) above, which
  // restores all kCalleeSaved registers (including cp and fp) to their
  // saved values before returning a failure to C.

  // Clear any pending exceptions.
  __ mov(r8, Operand(isolate->factory()->the_hole_value()));
  __ mov(ip, Operand(ExternalReference(Isolate::kPendingExceptionAddress,
                                       isolate)));
  __ StoreP(r8, MemOperand(ip));

  // Invoke the function by calling through JS entry trampoline builtin.
  // Notice that we cannot store a reference to the trampoline code directly in
  // this stub, because runtime stubs are not traversed when doing GC.

  // Expected registers by Builtins::JSEntryTrampoline
  // r3: code entry
  // r4: function
  // r5: receiver
  // r6: argc
  // r7: argv
  if (is_construct) {
    ExternalReference construct_entry(Builtins::kJSConstructEntryTrampoline,
                                      isolate);
    __ mov(ip, Operand(construct_entry));
  } else {
    ExternalReference entry(Builtins::kJSEntryTrampoline, isolate);
    __ mov(ip, Operand(entry));
  }
  __ LoadP(ip, MemOperand(ip));  // deref address

  // Branch and link to JSEntryTrampoline.
  // the address points to the start of the code object, skip the header
  __ addi(r0, ip, Operand(Code::kHeaderSize - kHeapObjectTag));
  __ mtlr(r0);
  __ bclr(BA, SetLK);  // make the call

  // Unlink this frame from the handler chain.
  __ PopTryHandler();

  __ bind(&exit);  // r3 holds result
  // Check if the current stack frame is marked as the outermost JS frame.
  Label non_outermost_js_2;
  __ pop(r8);
  __ CmpSmiLiteral(r8, Smi::FromInt(StackFrame::OUTERMOST_JSENTRY_FRAME), r0);
  __ bne(&non_outermost_js_2);
  __ mov(r9, Operand::Zero());
  __ mov(r8, Operand(ExternalReference(js_entry_sp)));
  __ StoreP(r9, MemOperand(r8));
  __ bind(&non_outermost_js_2);

  // Restore the top frame descriptors from the stack.
  __ pop(r6);
  __ mov(ip,
         Operand(ExternalReference(Isolate::kCEntryFPAddress, isolate)));
  __ StoreP(r6, MemOperand(ip));

  // Reset the stack to the callee saved registers.
  __ addi(sp, sp, Operand(-EntryFrameConstants::kCallerFPOffset));

  // Restore callee-saved registers and return.
#ifdef DEBUG
  if (FLAG_debug_code) {
    Label here;
    __ b(&here, SetLK);
    __ bind(&here);
  }
#endif

  __ MultiPop(kCalleeSaved);

  __ LoadP(r0, MemOperand(sp, kStackFrameLRSlot * kPointerSize));
  __ mtctr(r0);
  __ bctr();
}


// Uses registers r3 to r7.
// Expected input (depending on whether args are in registers or on the stack):
// * object: r3 or at sp + 1 * kPointerSize.
// * function: r4 or at sp.
//
// An inlined call site may have been generated before calling this stub.
// In this case the offset to the inline site to patch is passed on the stack,
// in the safepoint slot for register r7.
// (See LCodeGen::DoInstanceOfKnownGlobal)
void InstanceofStub::Generate(MacroAssembler* masm) {
  // Call site inlining and patching implies arguments in registers.
  ASSERT(HasArgsInRegisters() || !HasCallSiteInlineCheck());
  // ReturnTrueFalse is only implemented for inlined call sites.
  ASSERT(!ReturnTrueFalseObject() || HasCallSiteInlineCheck());

  // Fixed register usage throughout the stub:
  const Register object = r3;  // Object (lhs).
  Register map = r6;  // Map of the object.
  const Register function = r4;  // Function (rhs).
  const Register prototype = r7;  // Prototype of the function.
  const Register inline_site = r9;
  const Register scratch = r5;
  const Register scratch2 = r8;
  Register scratch3 = no_reg;

#if V8_TARGET_ARCH_PPC64
  const int32_t kDeltaToLoadBoolResult = 9 * Assembler::kInstrSize;
#else
  const int32_t kDeltaToLoadBoolResult = 5 * Assembler::kInstrSize;
#endif

  Label slow, loop, is_instance, is_not_instance, not_js_object;

  if (!HasArgsInRegisters()) {
    __ LoadP(object, MemOperand(sp, 1 * kPointerSize));
    __ LoadP(function, MemOperand(sp, 0));
  }

  // Check that the left hand is a JS object and load map.
  __ JumpIfSmi(object, &not_js_object);
  __ IsObjectJSObjectType(object, map, scratch, &not_js_object);

  // If there is a call site cache don't look in the global cache, but do the
  // real lookup and update the call site cache.
  if (!HasCallSiteInlineCheck()) {
    Label miss;
    __ CompareRoot(function, Heap::kInstanceofCacheFunctionRootIndex);
    __ bne(&miss);
    __ CompareRoot(map, Heap::kInstanceofCacheMapRootIndex);
    __ bne(&miss);
    __ LoadRoot(r3, Heap::kInstanceofCacheAnswerRootIndex);
    __ Ret(HasArgsInRegisters() ? 0 : 2);

    __ bind(&miss);
  }

  // Get the prototype of the function.
  __ TryGetFunctionPrototype(function, prototype, scratch, &slow, true);

  // Check that the function prototype is a JS object.
  __ JumpIfSmi(prototype, &slow);
  __ IsObjectJSObjectType(prototype, scratch, scratch, &slow);

  // Update the global instanceof or call site inlined cache with the current
  // map and function. The cached answer will be set when it is known below.
  if (!HasCallSiteInlineCheck()) {
    __ StoreRoot(function, Heap::kInstanceofCacheFunctionRootIndex);
    __ StoreRoot(map, Heap::kInstanceofCacheMapRootIndex);
  } else {
    ASSERT(HasArgsInRegisters());
    // Patch the (relocated) inlined map check.

    // The offset was stored in r7 safepoint slot.
    // (See LCodeGen::DoDeferredLInstanceOfKnownGlobal)
    __ LoadFromSafepointRegisterSlot(scratch, r7);
    __ mflr(inline_site);
    __ sub(inline_site, inline_site, scratch);
    // Get the map location in scratch and patch it.
    __ GetRelocatedValueLocation(inline_site, scratch, scratch2);
    __ StoreP(map, FieldMemOperand(scratch, Cell::kValueOffset), r0);
  }

  // Register mapping: r6 is object map and r7 is function prototype.
  // Get prototype of object into r5.
  __ LoadP(scratch, FieldMemOperand(map, Map::kPrototypeOffset));

  // We don't need map any more. Use it as a scratch register.
  scratch3 = map;
  map = no_reg;

  // Loop through the prototype chain looking for the function prototype.
  __ LoadRoot(scratch3, Heap::kNullValueRootIndex);
  __ bind(&loop);
  __ cmp(scratch, prototype);
  __ beq(&is_instance);
  __ cmp(scratch, scratch3);
  __ beq(&is_not_instance);
  __ LoadP(scratch, FieldMemOperand(scratch, HeapObject::kMapOffset));
  __ LoadP(scratch, FieldMemOperand(scratch, Map::kPrototypeOffset));
  __ b(&loop);

  __ bind(&is_instance);
  if (!HasCallSiteInlineCheck()) {
    __ LoadSmiLiteral(r3, Smi::FromInt(0));
    __ StoreRoot(r3, Heap::kInstanceofCacheAnswerRootIndex);
  } else {
    // Patch the call site to return true.
    __ LoadRoot(r3, Heap::kTrueValueRootIndex);
    __ addi(inline_site, inline_site, Operand(kDeltaToLoadBoolResult));
    // Get the boolean result location in scratch and patch it.
    __ PatchRelocatedValue(inline_site, scratch, r3);

    if (!ReturnTrueFalseObject()) {
      __ LoadSmiLiteral(r3, Smi::FromInt(0));
    }
  }
  __ Ret(HasArgsInRegisters() ? 0 : 2);

  __ bind(&is_not_instance);
  if (!HasCallSiteInlineCheck()) {
    __ LoadSmiLiteral(r3, Smi::FromInt(1));
    __ StoreRoot(r3, Heap::kInstanceofCacheAnswerRootIndex);
  } else {
    // Patch the call site to return false.
    __ LoadRoot(r3, Heap::kFalseValueRootIndex);
    __ addi(inline_site, inline_site, Operand(kDeltaToLoadBoolResult));
    // Get the boolean result location in scratch and patch it.
    __ PatchRelocatedValue(inline_site, scratch, r3);

    if (!ReturnTrueFalseObject()) {
      __ LoadSmiLiteral(r3, Smi::FromInt(1));
    }
  }
  __ Ret(HasArgsInRegisters() ? 0 : 2);

  Label object_not_null, object_not_null_or_smi;
  __ bind(&not_js_object);
  // Before null, smi and string value checks, check that the rhs is a function
  // as for a non-function rhs an exception needs to be thrown.
  __ JumpIfSmi(function, &slow);
  __ CompareObjectType(function, scratch3, scratch, JS_FUNCTION_TYPE);
  __ bne(&slow);

  // Null is not instance of anything.
  __ Cmpi(scratch, Operand(masm->isolate()->factory()->null_value()), r0);
  __ bne(&object_not_null);
  __ LoadSmiLiteral(r3, Smi::FromInt(1));
  __ Ret(HasArgsInRegisters() ? 0 : 2);

  __ bind(&object_not_null);
  // Smi values are not instances of anything.
  __ JumpIfNotSmi(object, &object_not_null_or_smi);
  __ LoadSmiLiteral(r3, Smi::FromInt(1));
  __ Ret(HasArgsInRegisters() ? 0 : 2);

  __ bind(&object_not_null_or_smi);
  // String values are not instances of anything.
  __ IsObjectJSStringType(object, scratch, &slow);
  __ LoadSmiLiteral(r3, Smi::FromInt(1));
  __ Ret(HasArgsInRegisters() ? 0 : 2);

  // Slow-case.  Tail call builtin.
  __ bind(&slow);
  if (!ReturnTrueFalseObject()) {
    if (HasArgsInRegisters()) {
      __ Push(r3, r4);
    }
  __ InvokeBuiltin(Builtins::INSTANCE_OF, JUMP_FUNCTION);
  } else {
    {
      FrameScope scope(masm, StackFrame::INTERNAL);
      __ Push(r3, r4);
      __ InvokeBuiltin(Builtins::INSTANCE_OF, CALL_FUNCTION);
    }
    Label true_value, done;
    __ cmpi(r3, Operand::Zero());
    __ beq(&true_value);

    __ LoadRoot(r3, Heap::kFalseValueRootIndex);
    __ b(&done);

    __ bind(&true_value);
    __ LoadRoot(r3, Heap::kTrueValueRootIndex);

    __ bind(&done);
    __ Ret(HasArgsInRegisters() ? 0 : 2);
  }
}


void FunctionPrototypeStub::Generate(MacroAssembler* masm) {
  Label miss;
  Register receiver;
  if (kind() == Code::KEYED_LOAD_IC) {
    // ----------- S t a t e -------------
    //  -- lr    : return address
    //  -- r3    : key
    //  -- r4    : receiver
    // -----------------------------------
    __ mov(r0, Operand(masm->isolate()->factory()->prototype_string()));
    __ cmp(r3, r0);
    __ bne(&miss);
    receiver = r4;
  } else {
    ASSERT(kind() == Code::LOAD_IC);
    // ----------- S t a t e -------------
    //  -- r5    : name
    //  -- lr    : return address
    //  -- r3    : receiver
    //  -- sp[0] : receiver
    // -----------------------------------
    receiver = r3;
  }

  StubCompiler::GenerateLoadFunctionPrototype(masm, receiver, r6, r7, &miss);
  __ bind(&miss);
  StubCompiler::TailCallBuiltin(
      masm, BaseLoadStoreStubCompiler::MissBuiltin(kind()));
}


void StringLengthStub::Generate(MacroAssembler* masm) {
  Label miss;
  Register receiver;
  if (kind() == Code::KEYED_LOAD_IC) {
    // ----------- S t a t e -------------
    //  -- lr    : return address
    //  -- r3    : key
    //  -- r4    : receiver
    // -----------------------------------
    __ mov(r0, Operand(masm->isolate()->factory()->length_string()));
    __ cmp(r3, r0);
    __ bne(&miss);
    receiver = r4;
  } else {
    ASSERT(kind() == Code::LOAD_IC);
    // ----------- S t a t e -------------
    //  -- r5    : name
    //  -- lr    : return address
    //  -- r3    : receiver
    //  -- sp[0] : receiver
    // -----------------------------------
    receiver = r3;
  }

  StubCompiler::GenerateLoadStringLength(masm, receiver, r6, r7, &miss);

  __ bind(&miss);
  StubCompiler::TailCallBuiltin(
      masm, BaseLoadStoreStubCompiler::MissBuiltin(kind()));
}


void StoreArrayLengthStub::Generate(MacroAssembler* masm) {
  // This accepts as a receiver anything JSArray::SetElementsLength accepts
  // (currently anything except for external arrays which means anything with
  // elements of FixedArray type).  Value must be a number, but only smis are
  // accepted as the most common case.
  Label miss;

  Register receiver;
  Register value;
  if (kind() == Code::KEYED_STORE_IC) {
    // ----------- S t a t e -------------
    //  -- lr    : return address
    //  -- r3    : value
    //  -- r4    : key
    //  -- r5    : receiver
    // -----------------------------------
    __ mov(r0, Operand(masm->isolate()->factory()->length_string()));
    __ cmp(r4, r0);
    __ bne(&miss);
    receiver = r5;
    value = r3;
  } else {
    ASSERT(kind() == Code::STORE_IC);
    // ----------- S t a t e -------------
    //  -- lr    : return address
    //  -- r3    : value
    //  -- r4    : receiver
    //  -- r5    : key
    // -----------------------------------
    receiver = r4;
    value = r3;
  }
  Register scratch = r6;

  // Check that the receiver isn't a smi.
  __ JumpIfSmi(receiver, &miss);

  // Check that the object is a JS array.
  __ CompareObjectType(receiver, scratch, scratch, JS_ARRAY_TYPE);
  __ bne(&miss);

  // Check that elements are FixedArray.
  // We rely on StoreIC_ArrayLength below to deal with all types of
  // fast elements (including COW).
  __ LoadP(scratch, FieldMemOperand(receiver, JSArray::kElementsOffset));
  __ CompareObjectType(scratch, scratch, scratch, FIXED_ARRAY_TYPE);
  __ bne(&miss);

  // Check that the array has fast properties, otherwise the length
  // property might have been redefined.
  __ LoadP(scratch, FieldMemOperand(receiver, JSArray::kPropertiesOffset));
  __ LoadP(scratch, FieldMemOperand(scratch, FixedArray::kMapOffset));
  __ CompareRoot(scratch, Heap::kHashTableMapRootIndex);
  __ beq(&miss);

  // Check that value is a smi.
  __ JumpIfNotSmi(value, &miss);

  // Prepare tail call to StoreIC_ArrayLength.
  __ Push(receiver, value);

  ExternalReference ref =
      ExternalReference(IC_Utility(IC::kStoreIC_ArrayLength), masm->isolate());
  __ TailCallExternalReference(ref, 2, 1);

  __ bind(&miss);

  StubCompiler::TailCallBuiltin(
      masm, BaseLoadStoreStubCompiler::MissBuiltin(kind()));
}


Register InstanceofStub::left() { return r3; }


Register InstanceofStub::right() { return r4; }


void ArgumentsAccessStub::GenerateReadElement(MacroAssembler* masm) {
  // The displacement is the offset of the last parameter (if any)
  // relative to the frame pointer.
  const int kDisplacement =
      StandardFrameConstants::kCallerSPOffset - kPointerSize;

  // Check that the key is a smi.
  Label slow;
  __ JumpIfNotSmi(r4, &slow);

  // Check if the calling frame is an arguments adaptor frame.
  Label adaptor;
  __ LoadP(r5, MemOperand(fp, StandardFrameConstants::kCallerFPOffset));
  __ LoadP(r6, MemOperand(r5, StandardFrameConstants::kContextOffset));
  STATIC_ASSERT(StackFrame::ARGUMENTS_ADAPTOR < 0x3fffu);
  __ CmpSmiLiteral(r6, Smi::FromInt(StackFrame::ARGUMENTS_ADAPTOR), r0);
  __ beq(&adaptor);

  // Check index against formal parameters count limit passed in
  // through register r3. Use unsigned comparison to get negative
  // check for free.
  __ cmpl(r4, r3);
  __ bge(&slow);

  // Read the argument from the stack and return it.
  __ sub(r6, r3, r4);
  __ SmiToPtrArrayOffset(r6, r6);
  __ add(r6, fp, r6);
  __ LoadP(r3, MemOperand(r6, kDisplacement));
  __ blr();

  // Arguments adaptor case: Check index against actual arguments
  // limit found in the arguments adaptor frame. Use unsigned
  // comparison to get negative check for free.
  __ bind(&adaptor);
  __ LoadP(r3, MemOperand(r5, ArgumentsAdaptorFrameConstants::kLengthOffset));
  __ cmpl(r4, r3);
  __ bge(&slow);

  // Read the argument from the adaptor frame and return it.
  __ sub(r6, r3, r4);
  __ SmiToPtrArrayOffset(r6, r6);
  __ add(r6, r5, r6);
  __ LoadP(r3, MemOperand(r6, kDisplacement));
  __ blr();

  // Slow-case: Handle non-smi or out-of-bounds access to arguments
  // by calling the runtime system.
  __ bind(&slow);
  __ push(r4);
  __ TailCallRuntime(Runtime::kGetArgumentsProperty, 1, 1);
}


void ArgumentsAccessStub::GenerateNewNonStrictSlow(MacroAssembler* masm) {
  // sp[0] : number of parameters
  // sp[1] : receiver displacement
  // sp[2] : function

  // Check if the calling frame is an arguments adaptor frame.
  Label runtime;
  __ LoadP(r6, MemOperand(fp, StandardFrameConstants::kCallerFPOffset));
  __ LoadP(r5, MemOperand(r6, StandardFrameConstants::kContextOffset));
  STATIC_ASSERT(StackFrame::ARGUMENTS_ADAPTOR < 0x3fffu);
  __ CmpSmiLiteral(r5, Smi::FromInt(StackFrame::ARGUMENTS_ADAPTOR), r0);
  __ bne(&runtime);

  // Patch the arguments.length and the parameters pointer in the current frame.
  __ LoadP(r5, MemOperand(r6, ArgumentsAdaptorFrameConstants::kLengthOffset));
  __ StoreP(r5, MemOperand(sp, 0 * kPointerSize));
  __ SmiToPtrArrayOffset(r5, r5);
  __ add(r6, r6, r5);
  __ addi(r6, r6, Operand(StandardFrameConstants::kCallerSPOffset));
  __ StoreP(r6, MemOperand(sp, 1 * kPointerSize));

  __ bind(&runtime);
  __ TailCallRuntime(Runtime::kNewArgumentsFast, 3, 1);
}


void ArgumentsAccessStub::GenerateNewNonStrictFast(MacroAssembler* masm) {
  // Stack layout:
  //  sp[0] : number of parameters (tagged)
  //  sp[1] : address of receiver argument
  //  sp[2] : function
  // Registers used over whole function:
  //  r9 : allocated object (tagged)
  //  r11 : mapped parameter count (tagged)

  __ LoadP(r4, MemOperand(sp, 0 * kPointerSize));
  // r4 = parameter count (tagged)

  // Check if the calling frame is an arguments adaptor frame.
  Label runtime;
  Label adaptor_frame, try_allocate;
  __ LoadP(r6, MemOperand(fp, StandardFrameConstants::kCallerFPOffset));
  __ LoadP(r5, MemOperand(r6, StandardFrameConstants::kContextOffset));
  STATIC_ASSERT(StackFrame::ARGUMENTS_ADAPTOR < 0x3fffu);
  __ CmpSmiLiteral(r5, Smi::FromInt(StackFrame::ARGUMENTS_ADAPTOR), r0);
  __ beq(&adaptor_frame);

  // No adaptor, parameter count = argument count.
  __ mr(r5, r4);
  __ b(&try_allocate);

  // We have an adaptor frame. Patch the parameters pointer.
  __ bind(&adaptor_frame);
  __ LoadP(r5, MemOperand(r6, ArgumentsAdaptorFrameConstants::kLengthOffset));
  __ SmiToPtrArrayOffset(r7, r5);
  __ add(r6, r6, r7);
  __ addi(r6, r6, Operand(StandardFrameConstants::kCallerSPOffset));
  __ StoreP(r6, MemOperand(sp, 1 * kPointerSize));

  // r4 = parameter count (tagged)
  // r5 = argument count (tagged)
  // Compute the mapped parameter count = min(r4, r5) in r4.
  Label skip;
  __ cmp(r4, r5);
  __ blt(&skip);
  __ mr(r4, r5);
  __ bind(&skip);

  __ bind(&try_allocate);

  // Compute the sizes of backing store, parameter map, and arguments object.
  // 1. Parameter map, has 2 extra words containing context and backing store.
  const int kParameterMapHeaderSize =
      FixedArray::kHeaderSize + 2 * kPointerSize;
  // If there are no mapped parameters, we do not need the parameter_map.
  Label skip2, skip3;
  __ CmpSmiLiteral(r4, Smi::FromInt(0), r0);
  __ bne(&skip2);
  __ li(r11, Operand::Zero());
  __ b(&skip3);
  __ bind(&skip2);
  __ SmiToPtrArrayOffset(r11, r4);
  __ addi(r11, r11, Operand(kParameterMapHeaderSize));
  __ bind(&skip3);

  // 2. Backing store.
  __ SmiToPtrArrayOffset(r7, r5);
  __ add(r11, r11, r7);
  __ addi(r11, r11, Operand(FixedArray::kHeaderSize));

  // 3. Arguments object.
  __ addi(r11, r11, Operand(Heap::kArgumentsObjectSize));

  // Do the allocation of all three objects in one go.
  __ Allocate(r11, r3, r6, r7, &runtime, TAG_OBJECT);

  // r3 = address of new object(s) (tagged)
  // r5 = argument count (tagged)
  // Get the arguments boilerplate from the current native context into r4.
  const int kNormalOffset =
      Context::SlotOffset(Context::ARGUMENTS_BOILERPLATE_INDEX);
  const int kAliasedOffset =
      Context::SlotOffset(Context::ALIASED_ARGUMENTS_BOILERPLATE_INDEX);

  __ LoadP(r7, MemOperand(cp,
             Context::SlotOffset(Context::GLOBAL_OBJECT_INDEX)));
  __ LoadP(r7, FieldMemOperand(r7, GlobalObject::kNativeContextOffset));
  Label skip4, skip5;
  __ cmpi(r4, Operand::Zero());
  __ bne(&skip4);
  __ LoadP(r7, MemOperand(r7, kNormalOffset));
  __ b(&skip5);
  __ bind(&skip4);
  __ LoadP(r7, MemOperand(r7, kAliasedOffset));
  __ bind(&skip5);

  // r3 = address of new object (tagged)
  // r4 = mapped parameter count (tagged)
  // r5 = argument count (tagged)
  // r7 = address of boilerplate object (tagged)
  // Copy the JS object part.
  for (int i = 0; i < JSObject::kHeaderSize; i += kPointerSize) {
    __ LoadP(r6, FieldMemOperand(r7, i));
    __ StoreP(r6, FieldMemOperand(r3, i), r0);
  }

  // Set up the callee in-object property.
  STATIC_ASSERT(Heap::kArgumentsCalleeIndex == 1);
  __ LoadP(r6, MemOperand(sp, 2 * kPointerSize));
  const int kCalleeOffset = JSObject::kHeaderSize +
      Heap::kArgumentsCalleeIndex * kPointerSize;
  __ StoreP(r6, FieldMemOperand(r3, kCalleeOffset), r0);

  // Use the length (smi tagged) and set that as an in-object property too.
  STATIC_ASSERT(Heap::kArgumentsLengthIndex == 0);
  const int kLengthOffset = JSObject::kHeaderSize +
      Heap::kArgumentsLengthIndex * kPointerSize;
  __ StoreP(r5, FieldMemOperand(r3, kLengthOffset), r0);

  // Set up the elements pointer in the allocated arguments object.
  // If we allocated a parameter map, r7 will point there, otherwise
  // it will point to the backing store.
  __ addi(r7, r3, Operand(Heap::kArgumentsObjectSize));
  __ StoreP(r7, FieldMemOperand(r3, JSObject::kElementsOffset), r0);

  // r3 = address of new object (tagged)
  // r4 = mapped parameter count (tagged)
  // r5 = argument count (tagged)
  // r7 = address of parameter map or backing store (tagged)
  // Initialize parameter map. If there are no mapped arguments, we're done.
  Label skip_parameter_map, skip6;
  __ CmpSmiLiteral(r4, Smi::FromInt(0), r0);
  __ bne(&skip6);
  // Move backing store address to r6, because it is
  // expected there when filling in the unmapped arguments.
  __ mr(r6, r7);
  __ b(&skip_parameter_map);
  __ bind(&skip6);

  __ LoadRoot(r9, Heap::kNonStrictArgumentsElementsMapRootIndex);
  __ StoreP(r9, FieldMemOperand(r7, FixedArray::kMapOffset), r0);
  __ AddSmiLiteral(r9, r4, Smi::FromInt(2), r0);
  __ StoreP(r9, FieldMemOperand(r7, FixedArray::kLengthOffset), r0);
  __ StoreP(cp, FieldMemOperand(r7,
                                FixedArray::kHeaderSize + 0 * kPointerSize),
            r0);
  __ SmiToPtrArrayOffset(r9, r4);
  __ add(r9, r7, r9);
  __ addi(r9, r9, Operand(kParameterMapHeaderSize));
  __ StoreP(r9, FieldMemOperand(r7,
                                FixedArray::kHeaderSize + 1 * kPointerSize),
            r0);

  // Copy the parameter slots and the holes in the arguments.
  // We need to fill in mapped_parameter_count slots. They index the context,
  // where parameters are stored in reverse order, at
  //   MIN_CONTEXT_SLOTS .. MIN_CONTEXT_SLOTS+parameter_count-1
  // The mapped parameter thus need to get indices
  //   MIN_CONTEXT_SLOTS+parameter_count-1 ..
  //       MIN_CONTEXT_SLOTS+parameter_count-mapped_parameter_count
  // We loop from right to left.
  Label parameters_loop, parameters_test;
  __ mr(r9, r4);
  __ LoadP(r11, MemOperand(sp, 0 * kPointerSize));
  __ AddSmiLiteral(r11, r11, Smi::FromInt(Context::MIN_CONTEXT_SLOTS), r0);
  __ sub(r11, r11, r4);
  __ LoadRoot(r10, Heap::kTheHoleValueRootIndex);
  __ SmiToPtrArrayOffset(r6, r9);
  __ add(r6, r7, r6);
  __ addi(r6, r6, Operand(kParameterMapHeaderSize));

  // r9 = loop variable (tagged)
  // r4 = mapping index (tagged)
  // r6 = address of backing store (tagged)
  // r7 = address of parameter map (tagged)
  // r8 = temporary scratch (a.o., for address calculation)
  // r10 = the hole value
  __ b(&parameters_test);

  __ bind(&parameters_loop);
  __ SubSmiLiteral(r9, r9, Smi::FromInt(1), r0);
  __ SmiToPtrArrayOffset(r8, r9);
  __ addi(r8, r8, Operand(kParameterMapHeaderSize - kHeapObjectTag));
  __ StorePX(r11, MemOperand(r8, r7));
  __ subi(r8, r8, Operand(kParameterMapHeaderSize - FixedArray::kHeaderSize));
  __ StorePX(r10, MemOperand(r8, r6));
  __ AddSmiLiteral(r11, r11, Smi::FromInt(1), r0);
  __ bind(&parameters_test);
  __ CmpSmiLiteral(r9, Smi::FromInt(0), r0);
  __ bne(&parameters_loop);

  __ bind(&skip_parameter_map);
  // r5 = argument count (tagged)
  // r6 = address of backing store (tagged)
  // r8 = scratch
  // Copy arguments header and remaining slots (if there are any).
  __ LoadRoot(r8, Heap::kFixedArrayMapRootIndex);
  __ StoreP(r8, FieldMemOperand(r6, FixedArray::kMapOffset), r0);
  __ StoreP(r5, FieldMemOperand(r6, FixedArray::kLengthOffset), r0);

  Label arguments_loop, arguments_test;
  __ mr(r11, r4);
  __ LoadP(r7, MemOperand(sp, 1 * kPointerSize));
  __ SmiToPtrArrayOffset(r8, r11);
  __ sub(r7, r7, r8);
  __ b(&arguments_test);

  __ bind(&arguments_loop);
  __ subi(r7, r7, Operand(kPointerSize));
  __ LoadP(r9, MemOperand(r7, 0));
  __ SmiToPtrArrayOffset(r8, r11);
  __ add(r8, r6, r8);
  __ StoreP(r9, FieldMemOperand(r8, FixedArray::kHeaderSize), r0);
  __ AddSmiLiteral(r11, r11, Smi::FromInt(1), r0);

  __ bind(&arguments_test);
  __ cmp(r11, r5);
  __ blt(&arguments_loop);

  // Return and remove the on-stack parameters.
  __ addi(sp, sp, Operand(3 * kPointerSize));
  __ Ret();

  // Do the runtime call to allocate the arguments object.
  // r5 = argument count (tagged)
  __ bind(&runtime);
  __ StoreP(r5, MemOperand(sp, 0 * kPointerSize));  // Patch argument count.
  __ TailCallRuntime(Runtime::kNewArgumentsFast, 3, 1);
}


void ArgumentsAccessStub::GenerateNewStrict(MacroAssembler* masm) {
  // sp[0] : number of parameters
  // sp[4] : receiver displacement
  // sp[8] : function
  // Check if the calling frame is an arguments adaptor frame.
  Label adaptor_frame, try_allocate, runtime;
  __ LoadP(r5, MemOperand(fp, StandardFrameConstants::kCallerFPOffset));
  __ LoadP(r6, MemOperand(r5, StandardFrameConstants::kContextOffset));
  STATIC_ASSERT(StackFrame::ARGUMENTS_ADAPTOR < 0x3fffu);
  __ CmpSmiLiteral(r6, Smi::FromInt(StackFrame::ARGUMENTS_ADAPTOR), r0);
  __ beq(&adaptor_frame);

  // Get the length from the frame.
  __ LoadP(r4, MemOperand(sp, 0));
  __ b(&try_allocate);

  // Patch the arguments.length and the parameters pointer.
  __ bind(&adaptor_frame);
  __ LoadP(r4, MemOperand(r5, ArgumentsAdaptorFrameConstants::kLengthOffset));
  __ StoreP(r4, MemOperand(sp, 0));
  __ SmiToPtrArrayOffset(r6, r4);
  __ add(r6, r5, r6);
  __ addi(r6, r6, Operand(StandardFrameConstants::kCallerSPOffset));
  __ StoreP(r6, MemOperand(sp, 1 * kPointerSize));

  // Try the new space allocation. Start out with computing the size
  // of the arguments object and the elements array in words.
  Label add_arguments_object;
  __ bind(&try_allocate);
  __ cmpi(r4, Operand::Zero());
  __ beq(&add_arguments_object);
  __ SmiUntag(r4);
  __ addi(r4, r4, Operand(FixedArray::kHeaderSize / kPointerSize));
  __ bind(&add_arguments_object);
  __ addi(r4, r4, Operand(Heap::kArgumentsObjectSizeStrict / kPointerSize));

  // Do the allocation of both objects in one go.
  __ Allocate(r4, r3, r5, r6, &runtime,
              static_cast<AllocationFlags>(TAG_OBJECT | SIZE_IN_WORDS));

  // Get the arguments boilerplate from the current native context.
  __ LoadP(r7,
           MemOperand(cp, Context::SlotOffset(Context::GLOBAL_OBJECT_INDEX)));
  __ LoadP(r7, FieldMemOperand(r7, GlobalObject::kNativeContextOffset));
  __ LoadP(r7, MemOperand(r7, Context::SlotOffset(
      Context::STRICT_MODE_ARGUMENTS_BOILERPLATE_INDEX)));

  // Copy the JS object part.
  __ CopyFields(r3, r7, r6.bit(), JSObject::kHeaderSize / kPointerSize);

  // Get the length (smi tagged) and set that as an in-object property too.
  STATIC_ASSERT(Heap::kArgumentsLengthIndex == 0);
  __ LoadP(r4, MemOperand(sp, 0 * kPointerSize));
  __ StoreP(r4, FieldMemOperand(r3, JSObject::kHeaderSize +
                                Heap::kArgumentsLengthIndex * kPointerSize),
            r0);

  // If there are no actual arguments, we're done.
  Label done;
  __ cmpi(r4, Operand::Zero());
  __ beq(&done);

  // Get the parameters pointer from the stack.
  __ LoadP(r5, MemOperand(sp, 1 * kPointerSize));

  // Set up the elements pointer in the allocated arguments object and
  // initialize the header in the elements fixed array.
  __ addi(r7, r3, Operand(Heap::kArgumentsObjectSizeStrict));
  __ StoreP(r7, FieldMemOperand(r3, JSObject::kElementsOffset), r0);
  __ LoadRoot(r6, Heap::kFixedArrayMapRootIndex);
  __ StoreP(r6, FieldMemOperand(r7, FixedArray::kMapOffset), r0);
  __ StoreP(r4, FieldMemOperand(r7, FixedArray::kLengthOffset), r0);
  // Untag the length for the loop.
  __ SmiUntag(r4);

  // Copy the fixed array slots.
  Label loop;
  // Set up r7 to point to the first array slot.
  __ addi(r7, r7, Operand(FixedArray::kHeaderSize - kHeapObjectTag));
  __ bind(&loop);
  // Pre-decrement r5 with kPointerSize on each iteration.
  // Pre-decrement in order to skip receiver.
  __ LoadPU(r6, MemOperand(r5, -kPointerSize));
  // Post-increment r7 with kPointerSize on each iteration.
  __ StoreP(r6, MemOperand(r7));
  __ addi(r7, r7, Operand(kPointerSize));
  __ subi(r4, r4, Operand(1));
  __ cmpi(r4, Operand::Zero());
  __ bne(&loop);

  // Return and remove the on-stack parameters.
  __ bind(&done);
  __ addi(sp, sp, Operand(3 * kPointerSize));
  __ Ret();

  // Do the runtime call to allocate the arguments object.
  __ bind(&runtime);
  __ TailCallRuntime(Runtime::kNewStrictArgumentsFast, 3, 1);
}


void RegExpExecStub::Generate(MacroAssembler* masm) {
  // Just jump directly to runtime if native RegExp is not selected at compile
  // time or if regexp entry in generated code is turned off runtime switch or
  // at compilation.
#ifdef V8_INTERPRETED_REGEXP
  __ TailCallRuntime(Runtime::kRegExpExec, 4, 1);
#else  // V8_INTERPRETED_REGEXP

  // Stack frame on entry.
  //  sp[0]: last_match_info (expected JSArray)
  //  sp[4]: previous index
  //  sp[8]: subject string
  //  sp[12]: JSRegExp object

  const int kLastMatchInfoOffset = 0 * kPointerSize;
  const int kPreviousIndexOffset = 1 * kPointerSize;
  const int kSubjectOffset = 2 * kPointerSize;
  const int kJSRegExpOffset = 3 * kPointerSize;

  Label runtime, br_over, encoding_type_UC16;

  // Allocation of registers for this function. These are in callee save
  // registers and will be preserved by the call to the native RegExp code, as
  // this code is called using the normal C calling convention. When calling
  // directly from generated code the native RegExp code will not do a GC and
  // therefore the content of these registers are safe to use after the call.
  Register subject = r14;
  Register regexp_data = r15;
  Register last_match_info_elements = r16;
  Register code = r17;

  // Ensure register assigments are consistent with callee save masks
  ASSERT(subject.bit() & kCalleeSaved);
  ASSERT(regexp_data.bit() & kCalleeSaved);
  ASSERT(last_match_info_elements.bit() & kCalleeSaved);
  ASSERT(code.bit() & kCalleeSaved);

  // Ensure that a RegExp stack is allocated.
  Isolate* isolate = masm->isolate();
  ExternalReference address_of_regexp_stack_memory_address =
      ExternalReference::address_of_regexp_stack_memory_address(isolate);
  ExternalReference address_of_regexp_stack_memory_size =
      ExternalReference::address_of_regexp_stack_memory_size(isolate);
  __ mov(r3, Operand(address_of_regexp_stack_memory_size));
  __ LoadP(r3, MemOperand(r3, 0));
  __ cmpi(r3, Operand::Zero());
  __ beq(&runtime);

  // Check that the first argument is a JSRegExp object.
  __ LoadP(r3, MemOperand(sp, kJSRegExpOffset));
  __ JumpIfSmi(r3, &runtime);
  __ CompareObjectType(r3, r4, r4, JS_REGEXP_TYPE);
  __ bne(&runtime);

  // Check that the RegExp has been compiled (data contains a fixed array).
  __ LoadP(regexp_data, FieldMemOperand(r3, JSRegExp::kDataOffset));
  if (FLAG_debug_code) {
    __ TestIfSmi(regexp_data, r0);
    __ Check(ne, kUnexpectedTypeForRegExpDataFixedArrayExpected, cr0);
    __ CompareObjectType(regexp_data, r3, r3, FIXED_ARRAY_TYPE);
    __ Check(eq, kUnexpectedTypeForRegExpDataFixedArrayExpected);
  }

  // regexp_data: RegExp data (FixedArray)
  // Check the type of the RegExp. Only continue if type is JSRegExp::IRREGEXP.
  __ LoadP(r3, FieldMemOperand(regexp_data, JSRegExp::kDataTagOffset));
  // ASSERT(Smi::FromInt(JSRegExp::IRREGEXP) < (char *)0xffffu);
  __ CmpSmiLiteral(r3, Smi::FromInt(JSRegExp::IRREGEXP), r0);
  __ bne(&runtime);

  // regexp_data: RegExp data (FixedArray)
  // Check that the number of captures fit in the static offsets vector buffer.
  __ LoadP(r5,
         FieldMemOperand(regexp_data, JSRegExp::kIrregexpCaptureCountOffset));
  // Check (number_of_captures + 1) * 2 <= offsets vector size
  // Or          number_of_captures * 2 <= offsets vector size - 2
  // SmiToShortArrayOffset accomplishes the multiplication by 2 and
  // SmiUntag (which is a nop for 32-bit).
  __ SmiToShortArrayOffset(r5, r5);
  STATIC_ASSERT(Isolate::kJSRegexpStaticOffsetsVectorSize >= 2);
  __ cmpli(r5, Operand(Isolate::kJSRegexpStaticOffsetsVectorSize - 2));
  __ bgt(&runtime);

  // Reset offset for possibly sliced string.
  __ li(r11, Operand::Zero());
  __ LoadP(subject, MemOperand(sp, kSubjectOffset));
  __ JumpIfSmi(subject, &runtime);
  __ mr(r6, subject);  // Make a copy of the original subject string.
  __ LoadP(r3, FieldMemOperand(subject, HeapObject::kMapOffset));
  __ lbz(r3, FieldMemOperand(r3, Map::kInstanceTypeOffset));
  // subject: subject string
  // r6: subject string
  // r3: subject string instance type
  // regexp_data: RegExp data (FixedArray)
  // Handle subject string according to its encoding and representation:
  // (1) Sequential string?  If yes, go to (5).
  // (2) Anything but sequential or cons?  If yes, go to (6).
  // (3) Cons string.  If the string is flat, replace subject with first string.
  //     Otherwise bailout.
  // (4) Is subject external?  If yes, go to (7).
  // (5) Sequential string.  Load regexp code according to encoding.
  // (E) Carry on.
  /// [...]

  // Deferred code at the end of the stub:
  // (6) Not a long external string?  If yes, go to (8).
  // (7) External string.  Make it, offset-wise, look like a sequential string.
  //     Go to (5).
  // (8) Short external string or not a string?  If yes, bail out to runtime.
  // (9) Sliced string.  Replace subject with parent.  Go to (4).

  Label seq_string /* 5 */, external_string /* 7 */,
        check_underlying /* 4 */, not_seq_nor_cons /* 6 */,
        not_long_external /* 8 */;

  // (1) Sequential string?  If yes, go to (5).
  STATIC_ASSERT((kIsNotStringMask |
                  kStringRepresentationMask |
                  kShortExternalStringMask) == 0x93);
  __ andi(r4,
          r3,
          Operand(kIsNotStringMask |
                  kStringRepresentationMask |
                  kShortExternalStringMask));
  STATIC_ASSERT((kStringTag | kSeqStringTag) == 0);
  __ beq(&seq_string, cr0);  // Go to (5).

  // (2) Anything but sequential or cons?  If yes, go to (6).
  STATIC_ASSERT(kConsStringTag < kExternalStringTag);
  STATIC_ASSERT(kSlicedStringTag > kExternalStringTag);
  STATIC_ASSERT(kIsNotStringMask > kExternalStringTag);
  STATIC_ASSERT(kShortExternalStringTag > kExternalStringTag);
  STATIC_ASSERT(kExternalStringTag < 0xffffu);
  __ cmpi(r4, Operand(kExternalStringTag));
  __ bge(&not_seq_nor_cons);  // Go to (6).

  // (3) Cons string.  Check that it's flat.
  // Replace subject with first string and reload instance type.
  __ LoadP(r3, FieldMemOperand(subject, ConsString::kSecondOffset));
  __ CompareRoot(r3, Heap::kempty_stringRootIndex);
  __ bne(&runtime);
  __ LoadP(subject, FieldMemOperand(subject, ConsString::kFirstOffset));

  // (4) Is subject external?  If yes, go to (7).
  __ bind(&check_underlying);
  __ LoadP(r3, FieldMemOperand(subject, HeapObject::kMapOffset));
  __ lbz(r3, FieldMemOperand(r3, Map::kInstanceTypeOffset));
  STATIC_ASSERT(kSeqStringTag == 0);
  STATIC_ASSERT(kStringRepresentationMask == 3);
  __ andi(r0, r3, Operand(kStringRepresentationMask));
  // The underlying external string is never a short external string.
  STATIC_CHECK(ExternalString::kMaxShortLength < ConsString::kMinLength);
  STATIC_CHECK(ExternalString::kMaxShortLength < SlicedString::kMinLength);
  __ bne(&external_string, cr0);  // Go to (7).

  // (5) Sequential string.  Load regexp code according to encoding.
  __ bind(&seq_string);
  // subject: sequential subject string (or look-alike, external string)
  // r6: original subject string
  // Load previous index and check range before r6 is overwritten.  We have to
  // use r6 instead of subject here because subject might have been only made
  // to look like a sequential string when it actually is an external string.
  __ LoadP(r4, MemOperand(sp, kPreviousIndexOffset));
  __ JumpIfNotSmi(r4, &runtime);
  __ LoadP(r6, FieldMemOperand(r6, String::kLengthOffset));
  __ cmpl(r6, r4);
  __ ble(&runtime);
  __ SmiUntag(r4);

  STATIC_ASSERT(4 == kOneByteStringTag);
  STATIC_ASSERT(kTwoByteStringTag == 0);
  STATIC_ASSERT(kStringEncodingMask == 4);
  __ ExtractBitMask(r6, r3, kStringEncodingMask, SetRC);
  __ beq(&encoding_type_UC16, cr0);
  __ LoadP(code, FieldMemOperand(regexp_data, JSRegExp::kDataAsciiCodeOffset));
  __ b(&br_over);
  __ bind(&encoding_type_UC16);
  __ LoadP(code, FieldMemOperand(regexp_data, JSRegExp::kDataUC16CodeOffset));
  __ bind(&br_over);

  // (E) Carry on.  String handling is done.
  // code: irregexp code
  // Check that the irregexp code has been generated for the actual string
  // encoding. If it has, the field contains a code object otherwise it contains
  // a smi (code flushing support).
  __ JumpIfSmi(code, &runtime);

  // r4: previous index
  // r6: encoding of subject string (1 if ASCII, 0 if two_byte);
  // code: Address of generated regexp code
  // subject: Subject string
  // regexp_data: RegExp data (FixedArray)
  // All checks done. Now push arguments for native regexp code.
  __ IncrementCounter(isolate->counters()->regexp_entry_native(), 1, r3, r5);

  // Isolates: note we add an additional parameter here (isolate pointer).
  const int kRegExpExecuteArguments = 10;
  const int kParameterRegisters = 8;
  __ EnterExitFrame(false, kRegExpExecuteArguments - kParameterRegisters);

  // Stack pointer now points to cell where return address is to be written.
  // Arguments are before that on the stack or in registers.

  // Argument 10 (in stack parameter area): Pass current isolate address.
  __ mov(r3, Operand(ExternalReference::isolate_address(isolate)));
  __ StoreP(r3, MemOperand(sp, (kStackFrameExtraParamSlot + 1) * kPointerSize));

  // Argument 9 is a dummy that reserves the space used for
  // the return address added by the ExitFrame in native calls.

  // Argument 8 (r10): Indicate that this is a direct call from JavaScript.
  __ li(r10, Operand(1));

  // Argument 7 (r9): Start (high end) of backtracking stack memory area.
  __ mov(r3, Operand(address_of_regexp_stack_memory_address));
  __ LoadP(r3, MemOperand(r3, 0));
  __ mov(r5, Operand(address_of_regexp_stack_memory_size));
  __ LoadP(r5, MemOperand(r5, 0));
  __ add(r9, r3, r5);

  // Argument 6 (r8): Set the number of capture registers to zero to force
  // global egexps to behave as non-global.  This does not affect non-global
  // regexps.
  __ li(r8, Operand::Zero());

  // Argument 5 (r7): static offsets vector buffer.
  __ mov(r7,
         Operand(ExternalReference::address_of_static_offsets_vector(isolate)));

  // For arguments 4 (r6) and 3 (r5) get string length, calculate start of
  // string data and calculate the shift of the index (0 for ASCII and 1 for
  // two byte).
  __ addi(r18, subject, Operand(SeqString::kHeaderSize - kHeapObjectTag));
  __ xori(r6, r6, Operand(1));
  // Load the length from the original subject string from the previous stack
  // frame. Therefore we have to use fp, which points exactly to two pointer
  // sizes below the previous sp. (Because creating a new stack frame pushes
  // the previous fp onto the stack and moves up sp by 2 * kPointerSize.)
  __ LoadP(subject, MemOperand(fp, kSubjectOffset + 2 * kPointerSize));
  // If slice offset is not 0, load the length from the original sliced string.
  // Argument 4, r6: End of string data
  // Argument 3, r5: Start of string data
  // Prepare start and end index of the input.
  __ ShiftLeft(r11, r11, r6);
  __ add(r11, r18, r11);
  __ ShiftLeft(r5, r4, r6);
  __ add(r5, r11, r5);

  __ LoadP(r18, FieldMemOperand(subject, String::kLengthOffset));
  __ SmiUntag(r18);
  __ ShiftLeft(r6, r18, r6);
  __ add(r6, r11, r6);

  // Argument 2 (r4): Previous index.
  // Already there

  // Argument 1 (r3): Subject string.
  __ mr(r3, subject);

  // Locate the code entry and call it.
  __ addi(code, code, Operand(Code::kHeaderSize - kHeapObjectTag));


#if ABI_USES_FUNCTION_DESCRIPTORS && defined(USE_SIMULATOR)
  // Even Simulated AIX/PPC64 Linux uses a function descriptor for the
  // RegExp routine.  Extract the instruction address here since
  // DirectCEntryStub::GenerateCall will not do it for calls out to
  // what it thinks is C code compiled for the simulator/host
  // platform.
  __ LoadP(code, MemOperand(code, 0));  // Instruction address
#endif

  DirectCEntryStub stub;
  stub.GenerateCall(masm, code);

  __ LeaveExitFrame(false, no_reg, true);

  // r3: result
  // subject: subject string (callee saved)
  // regexp_data: RegExp data (callee saved)
  // last_match_info_elements: Last match info elements (callee saved)
  // Check the result.
  Label success;
  __ cmpi(r3, Operand(1));
  // We expect exactly one result since we force the called regexp to behave
  // as non-global.
  __ beq(&success);
  Label failure;
  __ cmpi(r3, Operand(NativeRegExpMacroAssembler::FAILURE));
  __ beq(&failure);
  __ cmpi(r3, Operand(NativeRegExpMacroAssembler::EXCEPTION));
  // If not exception it can only be retry. Handle that in the runtime system.
  __ bne(&runtime);
  // Result must now be exception. If there is no pending exception already a
  // stack overflow (on the backtrack stack) was detected in RegExp code but
  // haven't created the exception yet. Handle that in the runtime system.
  // TODO(592): Rerunning the RegExp to get the stack overflow exception.
  __ mov(r4, Operand(isolate->factory()->the_hole_value()));
  __ mov(r5, Operand(ExternalReference(Isolate::kPendingExceptionAddress,
                                       isolate)));
  __ LoadP(r3, MemOperand(r5, 0));
  __ cmp(r3, r4);
  __ beq(&runtime);

  __ StoreP(r4, MemOperand(r5, 0));  // Clear pending exception.

  // Check if the exception is a termination. If so, throw as uncatchable.
  __ CompareRoot(r3, Heap::kTerminationExceptionRootIndex);

  Label termination_exception;
  __ beq(&termination_exception);

  __ Throw(r3);

  __ bind(&termination_exception);
  __ ThrowUncatchable(r3);

  __ bind(&failure);
  // For failure and exception return null.
  __ mov(r3, Operand(masm->isolate()->factory()->null_value()));
  __ addi(sp, sp, Operand(4 * kPointerSize));
  __ Ret();

  // Process the result from the native regexp code.
  __ bind(&success);
  __ LoadP(r4,
           FieldMemOperand(regexp_data, JSRegExp::kIrregexpCaptureCountOffset));
  // Calculate number of capture registers (number_of_captures + 1) * 2.
  // SmiToShortArrayOffset accomplishes the multiplication by 2 and
  // SmiUntag (which is a nop for 32-bit).
  __ SmiToShortArrayOffset(r4, r4);
  __ addi(r4, r4, Operand(2));

  __ LoadP(r3, MemOperand(sp, kLastMatchInfoOffset));
  __ JumpIfSmi(r3, &runtime);
  __ CompareObjectType(r3, r5, r5, JS_ARRAY_TYPE);
  __ bne(&runtime);
  // Check that the JSArray is in fast case.
  __ LoadP(last_match_info_elements,
           FieldMemOperand(r3, JSArray::kElementsOffset));
  __ LoadP(r3,
           FieldMemOperand(last_match_info_elements, HeapObject::kMapOffset));
  __ CompareRoot(r3, Heap::kFixedArrayMapRootIndex);
  __ bne(&runtime);
  // Check that the last match info has space for the capture registers and the
  // additional information.
  __ LoadP(r3,
         FieldMemOperand(last_match_info_elements, FixedArray::kLengthOffset));
  __ addi(r5, r4, Operand(RegExpImpl::kLastMatchOverhead));
  __ SmiUntag(r0, r3);
  __ cmp(r5, r0);
  __ bgt(&runtime);

  // r4: number of capture registers
  // subject: subject string
  // Store the capture count.
  __ SmiTag(r5, r4);
  __ StoreP(r5, FieldMemOperand(last_match_info_elements,
                                RegExpImpl::kLastCaptureCountOffset), r0);
  // Store last subject and last input.
  __ StoreP(subject,
            FieldMemOperand(last_match_info_elements,
                            RegExpImpl::kLastSubjectOffset), r0);
  __ mr(r5, subject);
  __ RecordWriteField(last_match_info_elements,
                      RegExpImpl::kLastSubjectOffset,
                      subject,
                      r10,
                      kLRHasNotBeenSaved,
                      kDontSaveFPRegs);
  __ mr(subject, r5);
  __ StoreP(subject,
            FieldMemOperand(last_match_info_elements,
                            RegExpImpl::kLastInputOffset), r0);
  __ RecordWriteField(last_match_info_elements,
                      RegExpImpl::kLastInputOffset,
                      subject,
                      r10,
                      kLRHasNotBeenSaved,
                      kDontSaveFPRegs);

  // Get the static offsets vector filled by the native regexp code.
  ExternalReference address_of_static_offsets_vector =
      ExternalReference::address_of_static_offsets_vector(isolate);
  __ mov(r5, Operand(address_of_static_offsets_vector));

  // r4: number of capture registers
  // r5: offsets vector
  Label next_capture;
  // Capture register counter starts from number of capture registers and
  // counts down until wraping after zero.
  __ addi(r3,
          last_match_info_elements,
          Operand(RegExpImpl::kFirstCaptureOffset - kHeapObjectTag -
                  kPointerSize));
  __ addi(r5, r5, Operand(-kIntSize));  // bias down for lwzu
  __ mtctr(r4);
  __ bind(&next_capture);
  // Read the value from the static offsets vector buffer.
  __ lwzu(r6, MemOperand(r5, kIntSize));
  // Store the smi value in the last match info.
  __ SmiTag(r6);
  __ StorePU(r6, MemOperand(r3, kPointerSize));
  __ bdnz(&next_capture);

  // Return last match info.
  __ LoadP(r3, MemOperand(sp, kLastMatchInfoOffset));
  __ addi(sp, sp, Operand(4 * kPointerSize));
  __ Ret();

  // Do the runtime call to execute the regexp.
  __ bind(&runtime);
  __ TailCallRuntime(Runtime::kRegExpExec, 4, 1);

  // Deferred code for string handling.
  // (6) Not a long external string?  If yes, go to (8).
  __ bind(&not_seq_nor_cons);
  // Compare flags are still set.
  __ bgt(&not_long_external);  // Go to (8).

  // (7) External string.  Make it, offset-wise, look like a sequential string.
  __ bind(&external_string);
  __ LoadP(r3, FieldMemOperand(subject, HeapObject::kMapOffset));
  __ lbz(r3, FieldMemOperand(r3, Map::kInstanceTypeOffset));
  if (FLAG_debug_code) {
    // Assert that we do not have a cons or slice (indirect strings) here.
    // Sequential strings have already been ruled out.
    STATIC_ASSERT(kIsIndirectStringMask == 1);
    __ andi(r0, r3, Operand(kIsIndirectStringMask));
    __ Assert(eq, kExternalStringExpectedButNotFound, cr0);
  }
  __ LoadP(subject,
         FieldMemOperand(subject, ExternalString::kResourceDataOffset));
  // Move the pointer so that offset-wise, it looks like a sequential string.
  STATIC_ASSERT(SeqTwoByteString::kHeaderSize == SeqOneByteString::kHeaderSize);
  __ subi(subject,
         subject,
         Operand(SeqTwoByteString::kHeaderSize - kHeapObjectTag));
  __ b(&seq_string);  // Go to (5).

  // (8) Short external string or not a string?  If yes, bail out to runtime.
  __ bind(&not_long_external);
  STATIC_ASSERT(kNotStringTag != 0 && kShortExternalStringTag !=0);
  __ andi(r0, r4, Operand(kIsNotStringMask | kShortExternalStringMask));
  __ bne(&runtime, cr0);

  // (9) Sliced string.  Replace subject with parent.  Go to (4).
  // Load offset into r11 and replace subject string with parent.
  __ LoadP(r11, FieldMemOperand(subject, SlicedString::kOffsetOffset));
  __ SmiUntag(r11);
  __ LoadP(subject, FieldMemOperand(subject, SlicedString::kParentOffset));
  __ b(&check_underlying);  // Go to (4).
#endif  // V8_INTERPRETED_REGEXP
}


void RegExpConstructResultStub::Generate(MacroAssembler* masm) {
  const int kMaxInlineLength = 100;
  Label slowcase;
  Label done;
  Factory* factory = masm->isolate()->factory();

  __ LoadP(r4, MemOperand(sp, kPointerSize * 2));
  __ JumpIfNotSmi(r4, &slowcase);
  __ CmplSmiLiteral(r4, Smi::FromInt(kMaxInlineLength), r0);
  __ bgt(&slowcase);
  // Allocate RegExpResult followed by FixedArray with size in ebx.
  // JSArray:   [Map][empty properties][Elements][Length-smi][index][input]
  // Elements:  [Map][Length][..elements..]
  // Size of JSArray with two in-object properties and the header of a
  // FixedArray.
  int objects_size =
      (JSRegExpResult::kSize + FixedArray::kHeaderSize) / kPointerSize;
  __ SmiUntag(r8, r4);
  __ addi(r5, r8, Operand(objects_size));
  // Future optimization: defer tagging the result pointer for more
  // efficient 64-bit memory accesses (due to alignment requirements
  // on the memoperand offset).
  __ Allocate(
      r5,  // In: Size, in words.
      r3,  // Out: Start of allocation (tagged).
      r6,  // Scratch register.
      r7,  // Scratch register.
      &slowcase,
      static_cast<AllocationFlags>(TAG_OBJECT | SIZE_IN_WORDS));
  // r3: Start of allocated area, object-tagged.
  // r4: Number of elements in array, as smi.
  // r8: Number of elements, untagged.

  // Set JSArray map to global.regexp_result_map().
  // Set empty properties FixedArray.
  // Set elements to point to FixedArray allocated right after the JSArray.
  // Interleave operations for better latency.
  __ LoadP(r5, ContextOperand(cp, Context::GLOBAL_OBJECT_INDEX));
  __ addi(r6, r3, Operand(JSRegExpResult::kSize));
  __ mov(r7, Operand(factory->empty_fixed_array()));
  __ LoadP(r5, FieldMemOperand(r5, GlobalObject::kNativeContextOffset));
  __ StoreP(r6, FieldMemOperand(r3, JSObject::kElementsOffset), r0);
  __ LoadP(r5, ContextOperand(r5, Context::REGEXP_RESULT_MAP_INDEX));
  __ StoreP(r7, FieldMemOperand(r3, JSObject::kPropertiesOffset), r0);
  __ StoreP(r5, FieldMemOperand(r3, HeapObject::kMapOffset), r0);

  // Set input, index and length fields from arguments.
  __ LoadP(r4, MemOperand(sp, kPointerSize * 0));
  __ LoadP(r5, MemOperand(sp, kPointerSize * 1));
  __ LoadP(r9, MemOperand(sp, kPointerSize * 2));
  __ StoreP(r4, FieldMemOperand(r3, JSRegExpResult::kInputOffset), r0);
  __ StoreP(r5, FieldMemOperand(r3, JSRegExpResult::kIndexOffset), r0);
  __ StoreP(r9, FieldMemOperand(r3, JSArray::kLengthOffset), r0);

  // Fill out the elements FixedArray.
  // r3: JSArray, tagged.
  // r6: FixedArray, tagged.
  // r8: Number of elements in array, untagged.

  // Set map.
  __ mov(r5, Operand(factory->fixed_array_map()));
  __ StoreP(r5, FieldMemOperand(r6, HeapObject::kMapOffset), r0);
  // Set FixedArray length.
  __ SmiTag(r9, r8);
  __ StoreP(r9, FieldMemOperand(r6, FixedArray::kLengthOffset), r0);
  // Fill contents of fixed-array with undefined.
  __ LoadRoot(r5, Heap::kUndefinedValueRootIndex);
  __ addi(r6, r6, Operand(FixedArray::kHeaderSize - kHeapObjectTag));
  // Fill fixed array elements with undefined.
  // r3: JSArray, tagged.
  // r5: undefined.
  // r6: Start of elements in FixedArray.
  // r8: Number of elements to fill.
  Label loop;
  __ cmpi(r8, Operand::Zero());
  __ bind(&loop);
  __ ble(&done);  // Jump if r8 is negative or zero.
  __ subi(r8, r8, Operand(1));
  __ ShiftLeftImm(ip, r8, Operand(kPointerSizeLog2));
  __ StorePX(r5, MemOperand(ip, r6));
  __ cmpi(r8, Operand::Zero());
  __ b(&loop);

  __ bind(&done);
  __ addi(sp, sp, Operand(3 * kPointerSize));
  __ Ret();

  __ bind(&slowcase);
  __ TailCallRuntime(Runtime::kRegExpConstructResult, 3, 1);
}


static void GenerateRecordCallTarget(MacroAssembler* masm) {
  // Cache the called function in a global property cell.  Cache states
  // are uninitialized, monomorphic (indicated by a JSFunction), and
  // megamorphic.
  // r3 : number of arguments to the construct function
  // r4 : the function to call
  // r5 : cache cell for call target
  Label initialize, done, miss, megamorphic, not_array_function;

  ASSERT_EQ(*TypeFeedbackCells::MegamorphicSentinel(masm->isolate()),
            masm->isolate()->heap()->undefined_value());
  ASSERT_EQ(*TypeFeedbackCells::UninitializedSentinel(masm->isolate()),
            masm->isolate()->heap()->the_hole_value());

  // Load the cache state into r6.
  __ LoadP(r6, FieldMemOperand(r5, Cell::kValueOffset));

  // A monomorphic cache hit or an already megamorphic state: invoke the
  // function without changing the state.
  __ cmp(r6, r4);
  __ b(eq, &done);

  // If we came here, we need to see if we are the array function.
  // If we didn't have a matching function, and we didn't find the megamorph
  // sentinel, then we have in the cell either some other function or an
  // AllocationSite. Do a map check on the object in ecx.
  __ LoadP(r8, FieldMemOperand(r6, 0));
  __ CompareRoot(r8, Heap::kAllocationSiteMapRootIndex);
  __ bne(&miss);

  // Make sure the function is the Array() function
  __ LoadArrayFunction(r6);
  __ cmp(r4, r6);
  __ bne(&megamorphic);
  __ jmp(&done);

  __ bind(&miss);

  // A monomorphic miss (i.e, here the cache is not uninitialized) goes
  // megamorphic.
  __ CompareRoot(r6, Heap::kTheHoleValueRootIndex);
  __ beq(&initialize);
  // MegamorphicSentinel is an immortal immovable object (undefined) so no
  // write-barrier is needed.
  __ bind(&megamorphic);
  __ LoadRoot(ip, Heap::kUndefinedValueRootIndex);
  __ StoreP(ip, FieldMemOperand(r5, Cell::kValueOffset), r0);
  __ jmp(&done);

  // An uninitialized cache is patched with the function or sentinel to
  // indicate the ElementsKind if function is the Array constructor.
  __ bind(&initialize);
  // Make sure the function is the Array() function
  __ LoadArrayFunction(r6);
  __ cmp(r4, r6);
  __ bne(&not_array_function);

  // The target function is the Array constructor,
  // Create an AllocationSite if we don't already have it, store it in the cell
  {
    FrameScope scope(masm, StackFrame::INTERNAL);

    // Arguments register must be smi-tagged to call out.
    __ SmiTag(r3);
    __ push(r3);
    __ push(r4);
    __ push(r5);

    CreateAllocationSiteStub create_stub;
    __ CallStub(&create_stub);

    __ pop(r5);
    __ pop(r4);
    __ pop(r3);
    __ SmiUntag(r3);
  }
  __ b(&done);

  __ bind(&not_array_function);
  __ StoreP(r4, FieldMemOperand(r5, Cell::kValueOffset), r0);
  // No need for a write barrier here - cells are rescanned.

  __ bind(&done);
}


void CallFunctionStub::Generate(MacroAssembler* masm) {
  // r4 : the function to call
  // r5 : cache cell for call target
  Label slow, non_function;

  // The receiver might implicitly be the global object. This is
  // indicated by passing the hole as the receiver to the call
  // function stub.
  if (ReceiverMightBeImplicit()) {
    Label call;
    // Get the receiver from the stack.
    // function, receiver [, arguments]
    __ LoadP(r7, MemOperand(sp, argc_ * kPointerSize), r0);
    // Call as function is indicated with the hole.
    __ CompareRoot(r7, Heap::kTheHoleValueRootIndex);
    __ bne(&call);
    // Patch the receiver on the stack with the global receiver object.
    __ LoadP(r6,
             MemOperand(cp, Context::SlotOffset(Context::GLOBAL_OBJECT_INDEX)));
    __ LoadP(r6, FieldMemOperand(r6, GlobalObject::kGlobalReceiverOffset));
    __ StoreP(r6, MemOperand(sp, argc_ * kPointerSize), r0);
    __ bind(&call);
  }

  // Check that the function is really a JavaScript function.
  // r4: pushed function (to be verified)
  __ JumpIfSmi(r4, &non_function);
  // Get the map of the function object.
  __ CompareObjectType(r4, r6, r6, JS_FUNCTION_TYPE);
  __ bne(&slow);

  if (RecordCallTarget()) {
    GenerateRecordCallTarget(masm);
  }

  // Fast-case: Invoke the function now.
  // r4: pushed function
  ParameterCount actual(argc_);

  if (ReceiverMightBeImplicit()) {
    Label call_as_function;
    __ CompareRoot(r7, Heap::kTheHoleValueRootIndex);
    __ beq(&call_as_function);
    __ InvokeFunction(r4,
                      actual,
                      JUMP_FUNCTION,
                      NullCallWrapper(),
                      CALL_AS_METHOD);
    __ bind(&call_as_function);
  }
  __ InvokeFunction(r4,
                    actual,
                    JUMP_FUNCTION,
                    NullCallWrapper(),
                    CALL_AS_FUNCTION);

  // Slow-case: Non-function called.
  __ bind(&slow);
  if (RecordCallTarget()) {
    // If there is a call target cache, mark it megamorphic in the
    // non-function case.  MegamorphicSentinel is an immortal immovable
    // object (undefined) so no write barrier is needed.
    ASSERT_EQ(*TypeFeedbackCells::MegamorphicSentinel(masm->isolate()),
              masm->isolate()->heap()->undefined_value());
    __ LoadRoot(ip, Heap::kUndefinedValueRootIndex);
    __ StoreP(ip, FieldMemOperand(r5, Cell::kValueOffset), r0);
  }
  // Check for function proxy.
  STATIC_ASSERT(JS_FUNCTION_PROXY_TYPE < 0xffffu);
  __ cmpi(r6, Operand(JS_FUNCTION_PROXY_TYPE));
  __ bne(&non_function);
  __ push(r4);  // put proxy as additional argument
  __ li(r3, Operand(argc_ + 1));
  __ li(r5, Operand::Zero());
  __ GetBuiltinEntry(r6, Builtins::CALL_FUNCTION_PROXY);
  __ SetCallKind(r8, CALL_AS_METHOD);
  {
    Handle<Code> adaptor =
      masm->isolate()->builtins()->ArgumentsAdaptorTrampoline();
    __ Jump(adaptor, RelocInfo::CODE_TARGET);
  }

  // CALL_NON_FUNCTION expects the non-function callee as receiver (instead
  // of the original receiver from the call site).
  __ bind(&non_function);
  __ StoreP(r4, MemOperand(sp, argc_ * kPointerSize), r0);
  __ li(r3, Operand(argc_));  // Set up the number of arguments.
  __ li(r5, Operand::Zero());
  __ GetBuiltinEntry(r6, Builtins::CALL_NON_FUNCTION);
  __ SetCallKind(r8, CALL_AS_METHOD);
  __ Jump(masm->isolate()->builtins()->ArgumentsAdaptorTrampoline(),
          RelocInfo::CODE_TARGET);
}


void CallConstructStub::Generate(MacroAssembler* masm) {
  // r3 : number of arguments
  // r4 : the function to call
  // r5 : cache cell for call target
  Label slow, non_function_call;

  // Check that the function is not a smi.
  __ JumpIfSmi(r4, &non_function_call);
  // Check that the function is a JSFunction.
  __ CompareObjectType(r4, r6, r6, JS_FUNCTION_TYPE);
  __ bne(&slow);

  if (RecordCallTarget()) {
    GenerateRecordCallTarget(masm);
  }

  // Jump to the function-specific construct stub.
  Register jmp_reg = r6;
  __ LoadP(jmp_reg, FieldMemOperand(r4, JSFunction::kSharedFunctionInfoOffset));
  __ LoadP(jmp_reg, FieldMemOperand(jmp_reg,
                                  SharedFunctionInfo::kConstructStubOffset));
  __ addi(r0, jmp_reg, Operand(Code::kHeaderSize - kHeapObjectTag));
  __ Jump(r0);

  // r3: number of arguments
  // r4: called object
  // r6: object type
  Label do_call;
  __ bind(&slow);
  STATIC_ASSERT(JS_FUNCTION_PROXY_TYPE < 0xffffu);
  __ cmpi(r6, Operand(JS_FUNCTION_PROXY_TYPE));
  __ bne(&non_function_call);
  __ GetBuiltinEntry(r6, Builtins::CALL_FUNCTION_PROXY_AS_CONSTRUCTOR);
  __ b(&do_call);

  __ bind(&non_function_call);
  __ GetBuiltinEntry(r6, Builtins::CALL_NON_FUNCTION_AS_CONSTRUCTOR);
  __ bind(&do_call);
  // Set expected number of arguments to zero (not changing r3).
  __ li(r5, Operand::Zero());
  __ SetCallKind(r8, CALL_AS_METHOD);
  __ Jump(masm->isolate()->builtins()->ArgumentsAdaptorTrampoline(),
          RelocInfo::CODE_TARGET);
}


// StringCharCodeAtGenerator
void StringCharCodeAtGenerator::GenerateFast(MacroAssembler* masm) {
  Label flat_string;
  Label ascii_string;
  Label got_char_code;
  Label sliced_string;

  // If the receiver is a smi trigger the non-string case.
  __ JumpIfSmi(object_, receiver_not_string_);

  // Fetch the instance type of the receiver into result register.
  __ LoadP(result_, FieldMemOperand(object_, HeapObject::kMapOffset));
  __ lbz(result_, FieldMemOperand(result_, Map::kInstanceTypeOffset));
  // If the receiver is not a string trigger the non-string case.
  __ andi(r0, result_, Operand(kIsNotStringMask));
  __ bne(receiver_not_string_, cr0);

  // If the index is non-smi trigger the non-smi case.
  __ JumpIfNotSmi(index_, &index_not_smi_);
  __ bind(&got_smi_index_);

  // Check for index out of range.
  __ LoadP(ip, FieldMemOperand(object_, String::kLengthOffset));
  __ cmpl(ip, index_);
  __ ble(index_out_of_range_);

  __ SmiUntag(index_);

  StringCharLoadGenerator::Generate(masm,
                                    object_,
                                    index_,
                                    result_,
                                    &call_runtime_);

  __ SmiTag(result_);
  __ bind(&exit_);
}


void StringCharCodeAtGenerator::GenerateSlow(
    MacroAssembler* masm,
    const RuntimeCallHelper& call_helper) {
  __ Abort(kUnexpectedFallthroughToCharCodeAtSlowCase);

  // Index is not a smi.
  __ bind(&index_not_smi_);
  // If index is a heap number, try converting it to an integer.
  __ CheckMap(index_,
              result_,
              Heap::kHeapNumberMapRootIndex,
              index_not_number_,
              DONT_DO_SMI_CHECK);
  call_helper.BeforeCall(masm);
  __ push(object_);
  __ push(index_);  // Consumed by runtime conversion function.
  if (index_flags_ == STRING_INDEX_IS_NUMBER) {
    __ CallRuntime(Runtime::kNumberToIntegerMapMinusZero, 1);
  } else {
    ASSERT(index_flags_ == STRING_INDEX_IS_ARRAY_INDEX);
    // NumberToSmi discards numbers that are not exact integers.
    __ CallRuntime(Runtime::kNumberToSmi, 1);
  }
  // Save the conversion result before the pop instructions below
  // have a chance to overwrite it.
  __ Move(index_, r3);
  __ pop(object_);
  // Reload the instance type.
  __ LoadP(result_, FieldMemOperand(object_, HeapObject::kMapOffset));
  __ lbz(result_, FieldMemOperand(result_, Map::kInstanceTypeOffset));
  call_helper.AfterCall(masm);
  // If index is still not a smi, it must be out of range.
  __ JumpIfNotSmi(index_, index_out_of_range_);
  // Otherwise, return to the fast path.
  __ b(&got_smi_index_);

  // Call runtime. We get here when the receiver is a string and the
  // index is a number, but the code of getting the actual character
  // is too complex (e.g., when the string needs to be flattened).
  __ bind(&call_runtime_);
  call_helper.BeforeCall(masm);
  __ SmiTag(index_);
  __ Push(object_, index_);
  __ CallRuntime(Runtime::kStringCharCodeAt, 2);
  __ Move(result_, r3);
  call_helper.AfterCall(masm);
  __ b(&exit_);

  __ Abort(kUnexpectedFallthroughFromCharCodeAtSlowCase);
}


// -------------------------------------------------------------------------
// StringCharFromCodeGenerator

  void StringCharFromCodeGenerator::GenerateFast(MacroAssembler* masm) {
  // Fast case of Heap::LookupSingleCharacterStringFromCode.
  ASSERT(IsPowerOf2(String::kMaxOneByteCharCode + 1));
  __ LoadSmiLiteral(r0, Smi::FromInt(~String::kMaxOneByteCharCode));
  __ ori(r0, r0, Operand(kSmiTagMask));
  __ and_(r0, code_, r0);
  __ cmpi(r0, Operand::Zero());
  __ bne(&slow_case_);

  __ LoadRoot(result_, Heap::kSingleCharacterStringCacheRootIndex);
  // At this point code register contains smi tagged ASCII char code.
  __ mr(r0, code_);
  __ SmiToPtrArrayOffset(code_, code_);
  __ add(result_, result_, code_);
  __ mr(code_, r0);
  __ LoadP(result_, FieldMemOperand(result_, FixedArray::kHeaderSize));
  __ CompareRoot(result_, Heap::kUndefinedValueRootIndex);
  __ beq(&slow_case_);
  __ bind(&exit_);
}


void StringCharFromCodeGenerator::GenerateSlow(
    MacroAssembler* masm,
    const RuntimeCallHelper& call_helper) {
  __ Abort(kUnexpectedFallthroughToCharFromCodeSlowCase);

  __ bind(&slow_case_);
  call_helper.BeforeCall(masm);
  __ push(code_);
  __ CallRuntime(Runtime::kCharFromCode, 1);
  __ Move(result_, r3);
  call_helper.AfterCall(masm);
  __ b(&exit_);

  __ Abort(kUnexpectedFallthroughFromCharFromCodeSlowCase);
}


void StringHelper::GenerateCopyCharacters(MacroAssembler* masm,
                                          Register dest,
                                          Register src,
                                          Register count,
                                          Register scratch,
                                          bool ascii) {
  Label loop;
  __ bind(&loop);
  // This loop just copies one character at a time, as it is only used for very
  // short strings.
  if (ascii) {
    __ lbz(scratch, MemOperand(src));
    __ stb(scratch, MemOperand(dest));
    __ addi(src, src, Operand(1));
    __ addi(dest, dest, Operand(1));
  } else {
    __ lhz(scratch, MemOperand(src));
    __ sth(scratch, MemOperand(dest));
    __ addi(src, src, Operand(2));
    __ addi(dest, dest, Operand(2));
  }
  __ subi(count, count, Operand(1));
  __ cmpi(count, Operand::Zero());
  __ bgt(&loop);
}


enum CopyCharactersFlags {
  COPY_ASCII = 1,
  DEST_ALWAYS_ALIGNED = 2
};


// roohack - optimization opportunity here, stringcopy is important
// and the current version below is very dumb
void StringHelper::GenerateCopyCharactersLong(MacroAssembler* masm,
                                              Register dest,
                                              Register src,
                                              Register count,
                                              Register scratch1,
                                              Register scratch2,
                                              Register scratch3,
                                              Register scratch4,
                                              Register scratch5,
                                              int flags) {
  bool ascii = (flags & COPY_ASCII) != 0;
  bool dest_always_aligned = (flags & DEST_ALWAYS_ALIGNED) != 0;

  if (dest_always_aligned && FLAG_debug_code) {
    // Check that destination is actually word aligned if the flag says
    // that it is.
    __ andi(r0, dest, Operand(kPointerAlignmentMask));
    __ Check(eq, kDestinationOfCopyNotAligned, cr0);
  }

  // Nothing to do for zero characters.
  Label done;
  if (!ascii) {  // for non-ascii, double the length
    __ add(count, count, count);
  }
  __ cmpi(count, Operand::Zero());
  __ beq(&done);

  // Assume that you cannot read (or write) unaligned.
  Label byte_loop;
  __ add(count, dest, count);
  Register limit = count;  // Read until src equals this.
  // Copy bytes from src to dst until dst hits limit.
  __ bind(&byte_loop);
  __ cmp(dest, limit);
  __ bge(&done);
  __ lbz(scratch1, MemOperand(src));
  __ addi(src, src, Operand(1));
  __ stb(scratch1, MemOperand(dest));
  __ addi(dest, dest, Operand(1));
  __ b(&byte_loop);

  __ bind(&done);
}


void StringHelper::GenerateTwoCharacterStringTableProbe(MacroAssembler* masm,
                                                        Register c1,
                                                        Register c2,
                                                        Register scratch1,
                                                        Register scratch2,
                                                        Register scratch3,
                                                        Register scratch4,
                                                        Register scratch5,
                                                        Label* not_found) {
  // Register scratch3 is the general scratch register in this function.
  Register scratch = scratch3;

  // Make sure that both characters are not digits as such strings has a
  // different hash algorithm. Don't try to look for these in the string table.
  Label not_array_index;
  __ subi(scratch, c1, Operand(static_cast<intptr_t>('0')));
  __ cmpli(scratch, Operand(static_cast<intptr_t>('9' - '0')));
  __ bgt(&not_array_index);
  __ subi(scratch, c2, Operand(static_cast<intptr_t>('0')));
  __ cmpli(scratch, Operand(static_cast<intptr_t>('9' - '0')));
  __ bgt(&not_array_index);

  // If check failed combine both characters into single halfword.
  // This is required by the contract of the method: code at the
  // not_found branch expects this combination in c1 register
#if __BYTE_ORDER == __BIG_ENDIAN
  __ ShiftLeftImm(c1, c1, Operand(kBitsPerByte));
  __ orx(c1, c1, c2);
#else
  __ ShiftLeftImm(r0, c2, Operand(kBitsPerByte));
  __ orx(c1, c1, r0);
#endif
  __ b(not_found);

  __ bind(&not_array_index);
  // Calculate the two character string hash.
  Register hash = scratch1;
  StringHelper::GenerateHashInit(masm, hash, c1, scratch);
  StringHelper::GenerateHashAddCharacter(masm, hash, c2, scratch);
  StringHelper::GenerateHashGetHash(masm, hash, scratch);

  // Collect the two characters in a register.
  Register chars = c1;
#if __BYTE_ORDER == __BIG_ENDIAN
  __ ShiftLeftImm(c1, c1, Operand(kBitsPerByte));
  __ orx(chars, c1, c2);
#else
  __ ShiftLeftImm(r0, c2, Operand(kBitsPerByte));
  __ orx(chars, c1, r0);
#endif

  // chars: two character string, char 1 in byte 0 and char 2 in byte 1.
  // hash:  hash of two character string.

  // Load string table
  // Load address of first element of the string table.
  Register string_table = c2;
  __ LoadRoot(string_table, Heap::kStringTableRootIndex);

  Register undefined = scratch4;
  __ LoadRoot(undefined, Heap::kUndefinedValueRootIndex);

  // Calculate capacity mask from the string table capacity.
  Register mask = scratch2;
  __ LoadP(mask, FieldMemOperand(string_table, StringTable::kCapacityOffset));
  __ SmiUntag(mask);
  __ subi(mask, mask, Operand(1));

  // Calculate untagged address of the first element of the string table.
  Register first_string_table_element = string_table;
  __ addi(first_string_table_element, string_table,
         Operand(StringTable::kElementsStartOffset - kHeapObjectTag));

  // Registers
  // chars: two character string, char 1 in byte 0 and char 2 in byte 1.
  // hash:  hash of two character string
  // mask:  capacity mask
  // first_string_table_element: address of the first element of
  //                             the string table
  // undefined: the undefined object
  // scratch: -

  // Perform a number of probes in the string table.
  const int kProbes = 4;
  Label found_in_string_table;
  Label next_probe[kProbes];
  Register candidate = scratch5;  // Scratch register contains candidate.
  for (int i = 0; i < kProbes; i++) {
    // Calculate entry in string table.
    if (i > 0) {
      __ addi(candidate, hash, Operand(StringTable::GetProbeOffset(i)));
    } else {
      __ mr(candidate, hash);
    }

    __ and_(candidate, candidate, mask);

    // Load the entry from the symble table.
    STATIC_ASSERT(StringTable::kEntrySize == 1);
    __ ShiftLeftImm(scratch, candidate, Operand(kPointerSizeLog2));
    __ LoadPX(candidate, MemOperand(scratch, first_string_table_element));

    // If entry is undefined no string with this hash can be found.
    Label is_string;
    __ CompareObjectType(candidate, scratch, scratch, ODDBALL_TYPE);
    __ bne(&is_string);

    __ cmp(undefined, candidate);
    __ beq(not_found);
    // Must be the hole (deleted entry).
    if (FLAG_debug_code) {
      __ LoadRoot(ip, Heap::kTheHoleValueRootIndex);
      __ cmp(ip, candidate);
      __ Assert(eq, kOddballInStringTableIsNotUndefinedOrTheHole);
    }
    __ b(&next_probe[i]);

    __ bind(&is_string);

    // Check that the candidate is a non-external ASCII string.  The instance
    // type is still in the scratch register from the CompareObjectType
    // operation.
    __ JumpIfInstanceTypeIsNotSequentialAscii(scratch, scratch, &next_probe[i]);

    // If length is not 2 the string is not a candidate.
    __ LoadP(scratch, FieldMemOperand(candidate, String::kLengthOffset));
    __ CmpSmiLiteral(scratch, Smi::FromInt(2), r0);
    __ bne(&next_probe[i]);

    // Check if the two characters match.
    __ lhz(scratch, FieldMemOperand(candidate, SeqOneByteString::kHeaderSize));
    __ cmp(chars, scratch);
    __ beq(&found_in_string_table);
    __ bind(&next_probe[i]);
  }

  // No matching 2 character string found by probing.
  __ b(not_found);

  // Scratch register contains result when we fall through to here.
  Register result = candidate;
  __ bind(&found_in_string_table);
  __ mr(r3, result);
}


void StringHelper::GenerateHashInit(MacroAssembler* masm,
                                    Register hash,
                                    Register character,
                                    Register scratch) {
  // hash = character + (character << 10);
  __ LoadRoot(hash, Heap::kHashSeedRootIndex);
  // Untag smi seed and add the character.
  __ SmiUntag(scratch, hash);
  __ add(hash, character, scratch);
  // hash += hash << 10;
  __ slwi(scratch, hash, Operand(10));
  __ add(hash, hash, scratch);
  // hash ^= hash >> 6;
  __ srwi(scratch, hash, Operand(6));
  __ xor_(hash, hash, scratch);
}


void StringHelper::GenerateHashAddCharacter(MacroAssembler* masm,
                                            Register hash,
                                            Register character,
                                            Register scratch) {
  // hash += character;
  __ add(hash, hash, character);
  // hash += hash << 10;
  __ slwi(scratch, hash, Operand(10));
  __ add(hash, hash, scratch);
  // hash ^= hash >> 6;
  __ srwi(scratch, hash, Operand(6));
  __ xor_(hash, hash, scratch);
}


void StringHelper::GenerateHashGetHash(MacroAssembler* masm,
                                       Register hash,
                                       Register scratch) {
  // hash += hash << 3;
  __ slwi(scratch, hash, Operand(3));
  __ add(hash, hash, scratch);
  // hash ^= hash >> 11;
  __ srwi(scratch, hash, Operand(11));
  __ xor_(hash, hash, scratch);
  // hash += hash << 15;
  __ slwi(scratch, hash, Operand(15));
  __ add(hash, hash, scratch);

  __ mov(scratch, Operand(String::kHashBitMask));
  __ and_(hash, hash, scratch, SetRC);

  // if (hash == 0) hash = 27;
  Label done;
  __ bne(&done, cr0);
  __ li(hash, Operand(StringHasher::kZeroHash));
  __ bind(&done);
}


void SubStringStub::Generate(MacroAssembler* masm) {
  Label runtime;

  // Stack frame on entry.
  //  lr: return address
  //  sp[0]: to
  //  sp[4]: from
  //  sp[8]: string

  // This stub is called from the native-call %_SubString(...), so
  // nothing can be assumed about the arguments. It is tested that:
  //  "string" is a sequential string,
  //  both "from" and "to" are smis, and
  //  0 <= from <= to <= string.length.
  // If any of these assumptions fail, we call the runtime system.

  const int kToOffset = 0 * kPointerSize;
  const int kFromOffset = 1 * kPointerSize;
  const int kStringOffset = 2 * kPointerSize;

  __ LoadP(r5, MemOperand(sp, kToOffset));
  __ LoadP(r6, MemOperand(sp, kFromOffset));

  // If either to or from had the smi tag bit set, then fail to generic runtime
  __ JumpIfNotSmi(r5, &runtime);
  __ JumpIfNotSmi(r6, &runtime);
  __ SmiUntag(r5);
  __ SmiUntag(r6, SetRC);
  // Both r5 and r6 are untagged integers.

  // We want to bailout to runtime here if From is negative.
  __ blt(&runtime, cr0);  // From < 0.

  __ cmpl(r6, r5);
  __ bgt(&runtime);  // Fail if from > to.
  __ sub(r5, r5, r6);

  // Make sure first argument is a string.
  __ LoadP(r3, MemOperand(sp, kStringOffset));
  __ JumpIfSmi(r3, &runtime);
  Condition is_string = masm->IsObjectStringType(r3, r4);
  __ b(NegateCondition(is_string), &runtime, cr0);

  Label single_char;
  __ cmpi(r5, Operand(1));
  __ b(eq, &single_char);

  // Short-cut for the case of trivial substring.
  Label return_r3;
  // r3: original string
  // r5: result string length
  __ LoadP(r7, FieldMemOperand(r3, String::kLengthOffset));
  __ SmiUntag(r0, r7);
  __ cmpl(r5, r0);
  // Return original string.
  __ beq(&return_r3);
  // Longer than original string's length or negative: unsafe arguments.
  __ bgt(&runtime);
  // Shorter than original string's length: an actual substring.

  // Deal with different string types: update the index if necessary
  // and put the underlying string into r8.
  // r3: original string
  // r4: instance type
  // r5: length
  // r6: from index (untagged)
  Label underlying_unpacked, sliced_string, seq_or_external_string;
  // If the string is not indirect, it can only be sequential or external.
  STATIC_ASSERT(kIsIndirectStringMask == (kSlicedStringTag & kConsStringTag));
  STATIC_ASSERT(kIsIndirectStringMask != 0);
  __ andi(r0, r4, Operand(kIsIndirectStringMask));
  __ beq(&seq_or_external_string, cr0);

  __ andi(r0, r4, Operand(kSlicedNotConsMask));
  __ bne(&sliced_string, cr0);
  // Cons string.  Check whether it is flat, then fetch first part.
  __ LoadP(r8, FieldMemOperand(r3, ConsString::kSecondOffset));
  __ CompareRoot(r8, Heap::kempty_stringRootIndex);
  __ bne(&runtime);
  __ LoadP(r8, FieldMemOperand(r3, ConsString::kFirstOffset));
  // Update instance type.
  __ LoadP(r4, FieldMemOperand(r8, HeapObject::kMapOffset));
  __ lbz(r4, FieldMemOperand(r4, Map::kInstanceTypeOffset));
  __ b(&underlying_unpacked);

  __ bind(&sliced_string);
  // Sliced string.  Fetch parent and correct start index by offset.
  __ LoadP(r8, FieldMemOperand(r3, SlicedString::kParentOffset));
  __ LoadP(r7, FieldMemOperand(r3, SlicedString::kOffsetOffset));
  __ SmiUntag(r4, r7);
  __ add(r6, r6, r4);  // Add offset to index.
  // Update instance type.
  __ LoadP(r4, FieldMemOperand(r8, HeapObject::kMapOffset));
  __ lbz(r4, FieldMemOperand(r4, Map::kInstanceTypeOffset));
  __ b(&underlying_unpacked);

  __ bind(&seq_or_external_string);
  // Sequential or external string.  Just move string to the expected register.
  __ mr(r8, r3);

  __ bind(&underlying_unpacked);

  if (FLAG_string_slices) {
    Label copy_routine;
    // r8: underlying subject string
    // r4: instance type of underlying subject string
    // r5: length
    // r6: adjusted start index (untagged)
    __ cmpi(r5, Operand(SlicedString::kMinLength));
    // Short slice.  Copy instead of slicing.
    __ blt(&copy_routine);
    // Allocate new sliced string.  At this point we do not reload the instance
    // type including the string encoding because we simply rely on the info
    // provided by the original string.  It does not matter if the original
    // string's encoding is wrong because we always have to recheck encoding of
    // the newly created string's parent anyways due to externalized strings.
    Label two_byte_slice, set_slice_header;
    STATIC_ASSERT((kStringEncodingMask & kOneByteStringTag) != 0);
    STATIC_ASSERT((kStringEncodingMask & kTwoByteStringTag) == 0);
    __ andi(r0, r4, Operand(kStringEncodingMask));
    __ beq(&two_byte_slice, cr0);
    __ AllocateAsciiSlicedString(r3, r5, r9, r10, &runtime);
    __ b(&set_slice_header);
    __ bind(&two_byte_slice);
    __ AllocateTwoByteSlicedString(r3, r5, r9, r10, &runtime);
    __ bind(&set_slice_header);
    __ SmiTag(r6);
    __ StoreP(r8, FieldMemOperand(r3, SlicedString::kParentOffset), r0);
    __ StoreP(r6, FieldMemOperand(r3, SlicedString::kOffsetOffset), r0);
    __ b(&return_r3);

    __ bind(&copy_routine);
  }

  // r8: underlying subject string
  // r4: instance type of underlying subject string
  // r5: length
  // r6: adjusted start index (untagged)
  Label two_byte_sequential, sequential_string, allocate_result;
  STATIC_ASSERT(kExternalStringTag != 0);
  STATIC_ASSERT(kSeqStringTag == 0);
  __ andi(r0, r4, Operand(kExternalStringTag));
  __ beq(&sequential_string, cr0);

  // Handle external string.
  // Rule out short external strings.
  STATIC_CHECK(kShortExternalStringTag != 0);
  __ andi(r0, r4, Operand(kShortExternalStringTag));
  __ bne(&runtime, cr0);
  __ LoadP(r8, FieldMemOperand(r8, ExternalString::kResourceDataOffset));
  // r8 already points to the first character of underlying string.
  __ b(&allocate_result);

  __ bind(&sequential_string);
  // Locate first character of underlying subject string.
  STATIC_ASSERT(SeqTwoByteString::kHeaderSize == SeqOneByteString::kHeaderSize);
  __ addi(r8, r8, Operand(SeqOneByteString::kHeaderSize - kHeapObjectTag));

  __ bind(&allocate_result);
  // Sequential acii string.  Allocate the result.
  STATIC_ASSERT((kOneByteStringTag & kStringEncodingMask) != 0);
  __ andi(r0, r4, Operand(kStringEncodingMask));
  __ beq(&two_byte_sequential, cr0);

  // Allocate and copy the resulting ASCII string.
  __ AllocateAsciiString(r3, r5, r7, r9, r10, &runtime);

  // Locate first character of substring to copy.
  __ add(r8, r8, r6);
  // Locate first character of result.
  __ addi(r4, r3, Operand(SeqOneByteString::kHeaderSize - kHeapObjectTag));

  // r3: result string
  // r4: first character of result string
  // r5: result string length
  // r8: first character of substring to copy
  STATIC_ASSERT((SeqOneByteString::kHeaderSize & kObjectAlignmentMask) == 0);
  StringHelper::GenerateCopyCharactersLong(masm, r4, r8, r5, r6, r7, r9,
                            r10, r11, COPY_ASCII | DEST_ALWAYS_ALIGNED);
  __ b(&return_r3);

  // Allocate and copy the resulting two-byte string.
  __ bind(&two_byte_sequential);
  __ AllocateTwoByteString(r3, r5, r7, r9, r10, &runtime);

  // Locate first character of substring to copy.
  __ ShiftLeftImm(r4, r6, Operand(1));
  __ add(r8, r8, r4);
  // Locate first character of result.
  __ addi(r4, r3, Operand(SeqTwoByteString::kHeaderSize - kHeapObjectTag));

  // r3: result string.
  // r4: first character of result.
  // r5: result length.
  // r8: first character of substring to copy.
  STATIC_ASSERT((SeqTwoByteString::kHeaderSize & kObjectAlignmentMask) == 0);
  StringHelper::GenerateCopyCharactersLong(
      masm, r4, r8, r5, r6, r7, r9, r10, r11, DEST_ALWAYS_ALIGNED);

  __ bind(&return_r3);
  Counters* counters = masm->isolate()->counters();
  __ IncrementCounter(counters->sub_string_native(), 1, r6, r7);
  __ Drop(3);
  __ Ret();

  // Just jump to runtime to create the sub string.
  __ bind(&runtime);
  __ TailCallRuntime(Runtime::kSubString, 3, 1);

  __ bind(&single_char);
  // r3: original string
  // r4: instance type
  // r5: length
  // r6: from index (untagged)
  __ SmiTag(r6, r6);
  StringCharAtGenerator generator(
      r3, r6, r5, r3, &runtime, &runtime, &runtime, STRING_INDEX_IS_NUMBER);
  generator.GenerateFast(masm);
  __ Drop(3);
  __ Ret();
  generator.SkipSlow(masm, &runtime);
}


void StringCompareStub::GenerateFlatAsciiStringEquals(MacroAssembler* masm,
                                                      Register left,
                                                      Register right,
                                                      Register scratch1,
                                                      Register scratch2) {
  Register length = scratch1;

  // Compare lengths.
  Label strings_not_equal, check_zero_length;
  __ LoadP(length, FieldMemOperand(left, String::kLengthOffset));
  __ LoadP(scratch2, FieldMemOperand(right, String::kLengthOffset));
  __ cmp(length, scratch2);
  __ beq(&check_zero_length);
  __ bind(&strings_not_equal);
  __ LoadSmiLiteral(r3, Smi::FromInt(NOT_EQUAL));
  __ Ret();

  // Check if the length is zero.
  Label compare_chars;
  __ bind(&check_zero_length);
  STATIC_ASSERT(kSmiTag == 0);
  __ cmpi(length, Operand::Zero());
  __ bne(&compare_chars);
  __ LoadSmiLiteral(r3, Smi::FromInt(EQUAL));
  __ Ret();

  // Compare characters.
  __ bind(&compare_chars);
  GenerateAsciiCharsCompareLoop(masm,
                                left, right, length, scratch2,
                                &strings_not_equal);

  // Characters are equal.
  __ LoadSmiLiteral(r3, Smi::FromInt(EQUAL));
  __ Ret();
}


void StringCompareStub::GenerateCompareFlatAsciiStrings(MacroAssembler* masm,
                                                        Register left,
                                                        Register right,
                                                        Register scratch1,
                                                        Register scratch2,
                                                        Register scratch3) {
  Label skip, result_not_equal, compare_lengths;
  // Find minimum length and length difference.
  __ LoadP(scratch1, FieldMemOperand(left, String::kLengthOffset));
  __ LoadP(scratch2, FieldMemOperand(right, String::kLengthOffset));
  __ sub(scratch3, scratch1, scratch2, LeaveOE, SetRC);
  Register length_delta = scratch3;
  __ ble(&skip, cr0);
  __ mr(scratch1, scratch2);
  __ bind(&skip);
  Register min_length = scratch1;
  STATIC_ASSERT(kSmiTag == 0);
  __ cmpi(min_length, Operand::Zero());
  __ beq(&compare_lengths);

  // Compare loop.
  GenerateAsciiCharsCompareLoop(masm,
                                left, right, min_length, scratch2,
                                &result_not_equal);

  // Compare lengths - strings up to min-length are equal.
  __ bind(&compare_lengths);
  ASSERT(Smi::FromInt(EQUAL) == static_cast<Smi*>(0));
  // Use length_delta as result if it's zero.
  __ mr(r3, length_delta);
  __ cmpi(r3, Operand::Zero());
  __ bind(&result_not_equal);
  // Conditionally update the result based either on length_delta or
  // the last comparion performed in the loop above.
  Label less_equal, equal;
  __ ble(&less_equal);
  __ LoadSmiLiteral(r3, Smi::FromInt(GREATER));
  __ Ret();
  __ bind(&less_equal);
  __ beq(&equal);
  __ LoadSmiLiteral(r3, Smi::FromInt(LESS));
  __ bind(&equal);
  __ Ret();
}


void StringCompareStub::GenerateAsciiCharsCompareLoop(
    MacroAssembler* masm,
    Register left,
    Register right,
    Register length,
    Register scratch1,
    Label* chars_not_equal) {
  // Change index to run from -length to -1 by adding length to string
  // start. This means that loop ends when index reaches zero, which
  // doesn't need an additional compare.
  __ SmiUntag(length);
  __ addi(scratch1, length,
          Operand(SeqOneByteString::kHeaderSize - kHeapObjectTag));
  __ add(left, left, scratch1);
  __ add(right, right, scratch1);
  __ subfic(length, length, Operand::Zero());
  Register index = length;  // index = -length;

  // Compare loop.
  Label loop;
  __ bind(&loop);
  __ lbzx(scratch1, MemOperand(left, index));
  __ lbzx(r0, MemOperand(right, index));
  __ cmp(scratch1, r0);
  __ bne(chars_not_equal);
  __ addi(index, index, Operand(1));
  __ cmpi(index, Operand::Zero());
  __ bne(&loop);
}


void StringCompareStub::Generate(MacroAssembler* masm) {
  Label runtime;

  Counters* counters = masm->isolate()->counters();

  // Stack frame on entry.
  //  sp[0]: right string
  //  sp[4]: left string
  __ LoadP(r3, MemOperand(sp));  // Load right in r3, left in r4.
  __ LoadP(r4, MemOperand(sp, kPointerSize));

  Label not_same;
  __ cmp(r3, r4);
  __ bne(&not_same);
  STATIC_ASSERT(EQUAL == 0);
  STATIC_ASSERT(kSmiTag == 0);
  __ LoadSmiLiteral(r3, Smi::FromInt(EQUAL));
  __ IncrementCounter(counters->string_compare_native(), 1, r4, r5);
  __ addi(sp, sp, Operand(2 * kPointerSize));
  __ Ret();

  __ bind(&not_same);

  // Check that both objects are sequential ASCII strings.
  __ JumpIfNotBothSequentialAsciiStrings(r4, r3, r5, r6, &runtime);

  // Compare flat ASCII strings natively. Remove arguments from stack first.
  __ IncrementCounter(counters->string_compare_native(), 1, r5, r6);
  __ addi(sp, sp, Operand(2 * kPointerSize));
  GenerateCompareFlatAsciiStrings(masm, r4, r3, r5, r6, r7);

  // Call the runtime; it returns -1 (less), 0 (equal), or 1 (greater)
  // tagged as a small integer.
  __ bind(&runtime);
  __ TailCallRuntime(Runtime::kStringCompare, 2, 1);
}


void StringAddStub::Generate(MacroAssembler* masm) {
  Label call_runtime, call_builtin;
  Builtins::JavaScript builtin_id = Builtins::ADD;

  Counters* counters = masm->isolate()->counters();

  // Stack on entry:
  // sp[0]: second argument (right).
  // sp[4]: first argument (left).

  // Load the two arguments.
  __ LoadP(r3, MemOperand(sp, 1 * kPointerSize));  // First argument.
  __ LoadP(r4, MemOperand(sp, 0 * kPointerSize));  // Second argument.

  // Make sure that both arguments are strings if not known in advance.
  // Otherwise, at least one of the arguments is definitely a string,
  // and we convert the one that is not known to be a string.
  if ((flags_ & STRING_ADD_CHECK_BOTH) == STRING_ADD_CHECK_BOTH) {
    ASSERT((flags_ & STRING_ADD_CHECK_LEFT) == STRING_ADD_CHECK_LEFT);
    ASSERT((flags_ & STRING_ADD_CHECK_RIGHT) == STRING_ADD_CHECK_RIGHT);
    __ JumpIfEitherSmi(r3, r4, &call_runtime);
    // Load instance types.
    __ LoadP(r7, FieldMemOperand(r3, HeapObject::kMapOffset));
    __ LoadP(r8, FieldMemOperand(r4, HeapObject::kMapOffset));
    __ lbz(r7, FieldMemOperand(r7, Map::kInstanceTypeOffset));
    __ lbz(r8, FieldMemOperand(r8, Map::kInstanceTypeOffset));
    STATIC_ASSERT(kStringTag == 0);
    // If either is not a string, go to runtime.
    __ andi(r0, r7, Operand(kIsNotStringMask));
    __ bne(&call_runtime, cr0);
    __ andi(r0, r8, Operand(kIsNotStringMask));
    __ bne(&call_runtime, cr0);
  } else if ((flags_ & STRING_ADD_CHECK_LEFT) == STRING_ADD_CHECK_LEFT) {
    ASSERT((flags_ & STRING_ADD_CHECK_RIGHT) == 0);
    GenerateConvertArgument(
        masm, 1 * kPointerSize, r3, r5, r6, r7, r8, &call_builtin);
    builtin_id = Builtins::STRING_ADD_RIGHT;
  } else if ((flags_ & STRING_ADD_CHECK_RIGHT) == STRING_ADD_CHECK_RIGHT) {
    ASSERT((flags_ & STRING_ADD_CHECK_LEFT) == 0);
    GenerateConvertArgument(
        masm, 0 * kPointerSize, r4, r5, r6, r7, r8, &call_builtin);
    builtin_id = Builtins::STRING_ADD_LEFT;
  }

  // Both arguments are strings.
  // r3: first string
  // r4: second string
  // r7: first string instance type (if flags_ == NO_STRING_ADD_FLAGS)
  // r8: second string instance type (if flags_ == NO_STRING_ADD_FLAGS)
  {
    Label first_not_empty, return_second, strings_not_empty;
    // Check if either of the strings are empty. In that case return the other.
    __ LoadP(r5, FieldMemOperand(r3, String::kLengthOffset));
    __ LoadP(r6, FieldMemOperand(r4, String::kLengthOffset));
    STATIC_ASSERT(kSmiTag == 0);
    // Test if first string is empty.
    __ CmpSmiLiteral(r5, Smi::FromInt(0), r0);
    __ bne(&first_not_empty);
    __ mr(r3, r4);  // If first is empty, return second.
    __ b(&return_second);
    STATIC_ASSERT(kSmiTag == 0);
    __ bind(&first_not_empty);
     // Else test if second string is empty.
    __ CmpSmiLiteral(r6, Smi::FromInt(0), r0);
    __ bne(&strings_not_empty);  // If either string was empty, return r3.

    __ bind(&return_second);
    __ IncrementCounter(counters->string_add_native(), 1, r5, r6);
    __ addi(sp, sp, Operand(2 * kPointerSize));
    __ Ret();

    __ bind(&strings_not_empty);
  }

  __ SmiUntag(r5);
  __ SmiUntag(r6);
  // Both strings are non-empty.
  // r3: first string
  // r4: second string
  // r5: length of first string
  // r6: length of second string
  // r7: first string instance type (if flags_ == NO_STRING_ADD_FLAGS)
  // r8: second string instance type (if flags_ == NO_STRING_ADD_FLAGS)
  // Look at the length of the result of adding the two strings.
  Label string_add_flat_result, longer_than_two;
  // Adding two lengths can't overflow.
  STATIC_ASSERT(String::kMaxLength < String::kMaxLength * 2);
  __ add(r9, r5, r6);
  // Use the string table when adding two one character strings, as it
  // helps later optimizations to return a string here.
  __ cmpi(r9, Operand(2));
  __ bne(&longer_than_two);

  // Check that both strings are non-external ASCII strings.
  if ((flags_ & STRING_ADD_CHECK_BOTH) != STRING_ADD_CHECK_BOTH) {
    __ LoadP(r7, FieldMemOperand(r3, HeapObject::kMapOffset));
    __ LoadP(r8, FieldMemOperand(r4, HeapObject::kMapOffset));
    __ lbz(r7, FieldMemOperand(r7, Map::kInstanceTypeOffset));
    __ lbz(r8, FieldMemOperand(r8, Map::kInstanceTypeOffset));
  }
  __ JumpIfBothInstanceTypesAreNotSequentialAscii(r7, r8, r9, r10,
                                                  &call_runtime);

  // Get the two characters forming the sub string.
  __ lbz(r5, FieldMemOperand(r3, SeqOneByteString::kHeaderSize));
  __ lbz(r6, FieldMemOperand(r4, SeqOneByteString::kHeaderSize));

  // Try to lookup two character string in string table. If it is not found
  // just allocate a new one.
  Label make_two_character_string;
  StringHelper::GenerateTwoCharacterStringTableProbe(
      masm, r5, r6, r9, r10, r7, r8, r11, &make_two_character_string);
  __ IncrementCounter(counters->string_add_native(), 1, r5, r6);
  __ addi(sp, sp, Operand(2 * kPointerSize));
  __ Ret();

  __ bind(&make_two_character_string);
  // Resulting string has length 2 and first chars of two strings
  // are combined into single halfword in r5 register.
  // So we can fill resulting string without two loops by a single
  // halfword store instruction
  __ li(r9, Operand(2));
  __ AllocateAsciiString(r3, r9, r7, r8, r11, &call_runtime);
  __ sth(r5, FieldMemOperand(r3, SeqOneByteString::kHeaderSize));
  __ IncrementCounter(counters->string_add_native(), 1, r5, r6);
  __ addi(sp, sp, Operand(2 * kPointerSize));
  __ Ret();

  __ bind(&longer_than_two);
  // Check if resulting string will be flat.
  __ cmpi(r9, Operand(ConsString::kMinLength));
  __ blt(&string_add_flat_result);
  // Handle exceptionally long strings in the runtime system.
  STATIC_ASSERT((String::kMaxLength & 0x80000000) == 0);
  ASSERT(IsPowerOf2(String::kMaxLength + 1));
  // kMaxLength + 1 is representable as shifted literal, kMaxLength is not.
  __ mov(r10, Operand(String::kMaxLength + 1));
  __ cmpl(r9, r10);
  __ bge(&call_runtime);

  // If result is not supposed to be flat, allocate a cons string object.
  // If both strings are ASCII the result is an ASCII cons string.
  if ((flags_ & STRING_ADD_CHECK_BOTH) != STRING_ADD_CHECK_BOTH) {
    __ LoadP(r7, FieldMemOperand(r3, HeapObject::kMapOffset));
    __ LoadP(r8, FieldMemOperand(r4, HeapObject::kMapOffset));
    __ lbz(r7, FieldMemOperand(r7, Map::kInstanceTypeOffset));
    __ lbz(r8, FieldMemOperand(r8, Map::kInstanceTypeOffset));
  }
  Label non_ascii, allocated, ascii_data;
  STATIC_ASSERT(kTwoByteStringTag == 0);
  __ andi(r0, r7, Operand(kStringEncodingMask));
  __ beq(&non_ascii, cr0);
  __ andi(r0, r8, Operand(kStringEncodingMask));
  __ beq(&non_ascii, cr0);

  // Allocate an ASCII cons string.
  __ bind(&ascii_data);
  __ AllocateAsciiConsString(r10, r9, r7, r8, &call_runtime);
  __ bind(&allocated);
  // Fill the fields of the cons string.
  Label skip_write_barrier, after_writing;
  ExternalReference high_promotion_mode = ExternalReference::
      new_space_high_promotion_mode_active_address(masm->isolate());
  __ mov(r7, Operand(high_promotion_mode));
  __ LoadP(r7, MemOperand(r7, 0));
  __ cmpi(r7, Operand::Zero());
  __ beq(&skip_write_barrier);

  __ StoreP(r3, FieldMemOperand(r10, ConsString::kFirstOffset), r0);
  __ RecordWriteField(r10,
                      ConsString::kFirstOffset,
                      r3,
                      r7,
                      kLRHasNotBeenSaved,
                      kDontSaveFPRegs);
  __ StoreP(r4, FieldMemOperand(r10, ConsString::kSecondOffset), r0);
  __ RecordWriteField(r10,
                      ConsString::kSecondOffset,
                      r4,
                      r7,
                      kLRHasNotBeenSaved,
                      kDontSaveFPRegs);
  __ jmp(&after_writing);

  __ bind(&skip_write_barrier);
  __ StoreP(r3, FieldMemOperand(r10, ConsString::kFirstOffset), r0);
  __ StoreP(r4, FieldMemOperand(r10, ConsString::kSecondOffset), r0);

  __ bind(&after_writing);

  __ mr(r3, r10);
  __ IncrementCounter(counters->string_add_native(), 1, r5, r6);
  __ addi(sp, sp, Operand(2 * kPointerSize));
  __ Ret();

  __ bind(&non_ascii);
  // At least one of the strings is two-byte. Check whether it happens
  // to contain only ASCII characters.
  // r7: first instance type.
  // r8: second instance type.
  __ andi(r0, r7, Operand(kOneByteDataHintMask));
  __ bne(&ascii_data, cr0);
  __ andi(r0, r8, Operand(kOneByteDataHintMask));
  __ bne(&ascii_data, cr0);
  __ xor_(r7, r7, r8);
  STATIC_ASSERT(kOneByteStringTag != 0 && kOneByteDataHintTag != 0);
  __ andi(r7, r7, Operand(kOneByteStringTag | kOneByteDataHintTag));
  __ cmpi(r7, Operand(kOneByteStringTag | kOneByteDataHintTag));
  __ beq(&ascii_data);

  // Allocate a two byte cons string.
  __ AllocateTwoByteConsString(r10, r9, r7, r8, &call_runtime);
  __ b(&allocated);

  // We cannot encounter sliced strings or cons strings here since:
  STATIC_ASSERT(SlicedString::kMinLength >= ConsString::kMinLength);
  // Handle creating a flat result from either external or sequential strings.
  // Locate the first characters' locations.
  // r3: first string
  // r4: second string
  // r5: length of first string
  // r6: length of second string
  // r7: first string instance type (if flags_ == NO_STRING_ADD_FLAGS)
  // r8: second string instance type (if flags_ == NO_STRING_ADD_FLAGS)
  // r9: sum of lengths.
  Label first_prepared, second_prepared, external_string1, external_string2;
  __ bind(&string_add_flat_result);
  if ((flags_ & STRING_ADD_CHECK_BOTH) != STRING_ADD_CHECK_BOTH) {
    __ LoadP(r7, FieldMemOperand(r3, HeapObject::kMapOffset));
    __ LoadP(r8, FieldMemOperand(r4, HeapObject::kMapOffset));
    __ lbz(r7, FieldMemOperand(r7, Map::kInstanceTypeOffset));
    __ lbz(r8, FieldMemOperand(r8, Map::kInstanceTypeOffset));
  }

  // Check whether both strings have same encoding
  __ xor_(r10, r7, r8);
  __ andi(r0, r10, Operand(kStringEncodingMask));
  __ bne(&call_runtime, cr0);

  STATIC_ASSERT(kSeqStringTag == 0);
  __ andi(r0, r7, Operand(kStringRepresentationMask));
  __ bne(&external_string1, cr0);
  STATIC_ASSERT(SeqOneByteString::kHeaderSize == SeqTwoByteString::kHeaderSize);
  __ addi(r10, r3, Operand(SeqOneByteString::kHeaderSize - kHeapObjectTag));
  __ b(&first_prepared);
  // External string: rule out short external string and load string resource.
  STATIC_ASSERT(kShortExternalStringTag != 0);
  __ bind(&external_string1);
  __ andi(r0, r7, Operand(kShortExternalStringMask));
  __ bne(&call_runtime, cr0);
  __ LoadP(r10, FieldMemOperand(r3, ExternalString::kResourceDataOffset));
  __ bind(&first_prepared);

  STATIC_ASSERT(kSeqStringTag == 0);
  __ andi(r0, r8, Operand(kStringRepresentationMask));
  __ bne(&external_string2, cr0);
  STATIC_ASSERT(SeqOneByteString::kHeaderSize == SeqTwoByteString::kHeaderSize);
  __ addi(r4, r4, Operand(SeqOneByteString::kHeaderSize - kHeapObjectTag));
  __ b(&second_prepared);
  // External string: rule out short external string and load string resource.
  STATIC_ASSERT(kShortExternalStringTag != 0);
  __ bind(&external_string2);
  __ andi(r0, r8, Operand(kShortExternalStringMask));
  __ bne(&call_runtime, cr0);
  __ LoadP(r4, FieldMemOperand(r4, ExternalString::kResourceDataOffset));
  __ bind(&second_prepared);

  Label non_ascii_string_add_flat_result;
  // r10: first character of first string
  // r4: first character of second string
  // r5: length of first string.
  // r6: length of second string.
  // r9: sum of lengths.
  // Both strings have the same encoding.
  STATIC_ASSERT(kTwoByteStringTag == 0);
  __ andi(r0, r8, Operand(kStringEncodingMask));
  __ beq(&non_ascii_string_add_flat_result, cr0);

  __ AllocateAsciiString(r3, r9, r7, r8, r11, &call_runtime);
  __ addi(r9, r3, Operand(SeqOneByteString::kHeaderSize - kHeapObjectTag));
  // r3: result string.
  // r10: first character of first string.
  // r4: first character of second string.
  // r5: length of first string.
  // r6: length of second string.
  // r9: first character of result.
  StringHelper::GenerateCopyCharacters(masm, r9, r10, r5, r7, true);
  // r9: next character of result.
  StringHelper::GenerateCopyCharacters(masm, r9, r4, r6, r7, true);
  __ IncrementCounter(counters->string_add_native(), 1, r5, r6);
  __ addi(sp, sp, Operand(2 * kPointerSize));
  __ Ret();

  __ bind(&non_ascii_string_add_flat_result);
  __ AllocateTwoByteString(r3, r9, r7, r8, r11, &call_runtime);
  __ addi(r9, r3, Operand(SeqTwoByteString::kHeaderSize - kHeapObjectTag));
  // r3: result string.
  // r10: first character of first string.
  // r4: first character of second string.
  // r5: length of first string.
  // r6: length of second string.
  // r9: first character of result.
  StringHelper::GenerateCopyCharacters(masm, r9, r10, r5, r7, false);
  // r9: next character of result.
  StringHelper::GenerateCopyCharacters(masm, r9, r4, r6, r7, false);
  __ IncrementCounter(counters->string_add_native(), 1, r5, r6);
  __ addi(sp, sp, Operand(2 * kPointerSize));
  __ Ret();

  // Just jump to runtime to add the two strings.
  __ bind(&call_runtime);
  __ TailCallRuntime(Runtime::kStringAdd, 2, 1);

  if (call_builtin.is_linked()) {
    __ bind(&call_builtin);
    __ InvokeBuiltin(builtin_id, JUMP_FUNCTION);
  }
}


void StringAddStub::GenerateRegisterArgsPush(MacroAssembler* masm) {
  __ push(r3);
  __ push(r4);
}


void StringAddStub::GenerateRegisterArgsPop(MacroAssembler* masm) {
  __ pop(r4);
  __ pop(r3);
}


void StringAddStub::GenerateConvertArgument(MacroAssembler* masm,
                                            int stack_offset,
                                            Register arg,
                                            Register scratch1,
                                            Register scratch2,
                                            Register scratch3,
                                            Register scratch4,
                                            Label* slow) {
  // First check if the argument is already a string.
  Label not_string, done;
  __ JumpIfSmi(arg, &not_string);
  __ CompareObjectType(arg, scratch1, scratch1, FIRST_NONSTRING_TYPE);
  __ blt(&done);

  // Check the number to string cache.
  __ bind(&not_string);
  // Puts the cached result into scratch1.
  __ LookupNumberStringCache(arg, scratch1, scratch2, scratch3, scratch4, slow);
  __ mr(arg, scratch1);
  __ StoreP(arg, MemOperand(sp, stack_offset));
  __ bind(&done);
}


void ICCompareStub::GenerateSmis(MacroAssembler* masm) {
  ASSERT(state_ == CompareIC::SMI);
  Label miss;
  __ orx(r5, r4, r3);
  __ JumpIfNotSmi(r5, &miss);

  if (GetCondition() == eq) {
    // For equality we do not care about the sign of the result.
    // __ sub(r3, r3, r4, SetCC);
     __ sub(r3, r3, r4);
  } else {
    // Untag before subtracting to avoid handling overflow.
    __ SmiUntag(r4);
    __ SmiUntag(r3);
    __ sub(r3, r4, r3);
  }
  __ Ret();

  __ bind(&miss);
  GenerateMiss(masm);
}


void ICCompareStub::GenerateNumbers(MacroAssembler* masm) {
  ASSERT(state_ == CompareIC::NUMBER);

  Label generic_stub;
  Label unordered, maybe_undefined1, maybe_undefined2;
  Label miss;
  Label equal, less_than;

  if (left_ == CompareIC::SMI) {
    __ JumpIfNotSmi(r4, &miss);
  }
  if (right_ == CompareIC::SMI) {
    __ JumpIfNotSmi(r3, &miss);
  }

  // Inlining the double comparison and falling back to the general compare
  // stub if NaN is involved.
  // Load left and right operand.
  Label done, left, left_smi, right_smi;
  __ JumpIfSmi(r3, &right_smi);
  __ CheckMap(r3, r5, Heap::kHeapNumberMapRootIndex, &maybe_undefined1,
              DONT_DO_SMI_CHECK);
  __ lfd(d1, FieldMemOperand(r3, HeapNumber::kValueOffset));
  __ b(&left);
  __ bind(&right_smi);
  __ SmiToDouble(d1, r3);

  __ bind(&left);
  __ JumpIfSmi(r4, &left_smi);
  __ CheckMap(r4, r5, Heap::kHeapNumberMapRootIndex, &maybe_undefined2,
              DONT_DO_SMI_CHECK);
  __ lfd(d0, FieldMemOperand(r4, HeapNumber::kValueOffset));
  __ b(&done);
  __ bind(&left_smi);
  __ SmiToDouble(d0, r4);

  __ bind(&done);

  // Compare operands
  __ fcmpu(d0, d1);

  // Don't base result on status bits when a NaN is involved.
  __ bunordered(&unordered);

  // Return a result of -1, 0, or 1, based on status bits.
  __ beq(&equal);
  __ blt(&less_than);
  //  assume greater than
  __ li(r3, Operand(GREATER));
  __ Ret();
  __ bind(&equal);
  __ li(r3, Operand(EQUAL));
  __ Ret();
  __ bind(&less_than);
  __ li(r3, Operand(LESS));
  __ Ret();

  __ bind(&unordered);
  __ bind(&generic_stub);
  ICCompareStub stub(op_, CompareIC::GENERIC, CompareIC::GENERIC,
                     CompareIC::GENERIC);
  __ Jump(stub.GetCode(masm->isolate()), RelocInfo::CODE_TARGET);

  __ bind(&maybe_undefined1);
  if (Token::IsOrderedRelationalCompareOp(op_)) {
    __ CompareRoot(r3, Heap::kUndefinedValueRootIndex);
    __ bne(&miss);
    __ JumpIfSmi(r4, &unordered);
    __ CompareObjectType(r4, r5, r5, HEAP_NUMBER_TYPE);
    __ bne(&maybe_undefined2);
    __ b(&unordered);
  }

  __ bind(&maybe_undefined2);
  if (Token::IsOrderedRelationalCompareOp(op_)) {
    __ CompareRoot(r4, Heap::kUndefinedValueRootIndex);
    __ beq(&unordered);
  }

  __ bind(&miss);
  GenerateMiss(masm);
}


void ICCompareStub::GenerateInternalizedStrings(MacroAssembler* masm) {
  ASSERT(state_ == CompareIC::INTERNALIZED_STRING);
  Label miss, not_equal;

  // Registers containing left and right operands respectively.
  Register left = r4;
  Register right = r3;
  Register tmp1 = r5;
  Register tmp2 = r6;

  // Check that both operands are heap objects.
  __ JumpIfEitherSmi(left, right, &miss);

  // Check that both operands are symbols.
  __ LoadP(tmp1, FieldMemOperand(left, HeapObject::kMapOffset));
  __ LoadP(tmp2, FieldMemOperand(right, HeapObject::kMapOffset));
  __ lbz(tmp1, FieldMemOperand(tmp1, Map::kInstanceTypeOffset));
  __ lbz(tmp2, FieldMemOperand(tmp2, Map::kInstanceTypeOffset));
  STATIC_ASSERT(kInternalizedTag == 0 && kStringTag == 0);
  __ orx(tmp1, tmp1, tmp2);
  __ andi(r0, tmp1, Operand(kIsNotStringMask | kIsNotInternalizedMask));
  __ bne(&miss, cr0);

  // Internalized strings are compared by identity.
  __ cmp(left, right);
  __ bne(&not_equal);
  // Make sure r3 is non-zero. At this point input operands are
  // guaranteed to be non-zero.
  ASSERT(right.is(r3));
  STATIC_ASSERT(EQUAL == 0);
  STATIC_ASSERT(kSmiTag == 0);
  __ LoadSmiLiteral(r3, Smi::FromInt(EQUAL));
  __ bind(&not_equal);
  __ Ret();

  __ bind(&miss);
  GenerateMiss(masm);
}


void ICCompareStub::GenerateUniqueNames(MacroAssembler* masm) {
  ASSERT(state_ == CompareIC::UNIQUE_NAME);
  ASSERT(GetCondition() == eq);
  Label miss;

  // Registers containing left and right operands respectively.
  Register left = r4;
  Register right = r3;
  Register tmp1 = r5;
  Register tmp2 = r6;

  // Check that both operands are heap objects.
  __ JumpIfEitherSmi(left, right, &miss);

  // Check that both operands are unique names. This leaves the instance
  // types loaded in tmp1 and tmp2.
  __ LoadP(tmp1, FieldMemOperand(left, HeapObject::kMapOffset));
  __ LoadP(tmp2, FieldMemOperand(right, HeapObject::kMapOffset));
  __ lbz(tmp1, FieldMemOperand(tmp1, Map::kInstanceTypeOffset));
  __ lbz(tmp2, FieldMemOperand(tmp2, Map::kInstanceTypeOffset));

  __ JumpIfNotUniqueName(tmp1, &miss);
  __ JumpIfNotUniqueName(tmp2, &miss);

  // Unique names are compared by identity.
  __ cmp(left, right);
  __ bne(&miss);
  // Make sure r3 is non-zero. At this point input operands are
  // guaranteed to be non-zero.
  ASSERT(right.is(r3));
  STATIC_ASSERT(EQUAL == 0);
  STATIC_ASSERT(kSmiTag == 0);
  __ LoadSmiLiteral(r3, Smi::FromInt(EQUAL));
  __ Ret();

  __ bind(&miss);
  GenerateMiss(masm);
}


void ICCompareStub::GenerateStrings(MacroAssembler* masm) {
  ASSERT(state_ == CompareIC::STRING);
  Label miss, not_identical, is_symbol;

  bool equality = Token::IsEqualityOp(op_);

  // Registers containing left and right operands respectively.
  Register left = r4;
  Register right = r3;
  Register tmp1 = r5;
  Register tmp2 = r6;
  Register tmp3 = r7;
  Register tmp4 = r8;

  // Check that both operands are heap objects.
  __ JumpIfEitherSmi(left, right, &miss);

  // Check that both operands are strings. This leaves the instance
  // types loaded in tmp1 and tmp2.
  __ LoadP(tmp1, FieldMemOperand(left, HeapObject::kMapOffset));
  __ LoadP(tmp2, FieldMemOperand(right, HeapObject::kMapOffset));
  __ lbz(tmp1, FieldMemOperand(tmp1, Map::kInstanceTypeOffset));
  __ lbz(tmp2, FieldMemOperand(tmp2, Map::kInstanceTypeOffset));
  STATIC_ASSERT(kNotStringTag != 0);
  __ orx(tmp3, tmp1, tmp2);
  __ andi(r0, tmp3, Operand(kIsNotStringMask));
  __ bne(&miss, cr0);

  // Fast check for identical strings.
  __ cmp(left, right);
  STATIC_ASSERT(EQUAL == 0);
  STATIC_ASSERT(kSmiTag == 0);
  __ bne(&not_identical);
  __ LoadSmiLiteral(r3, Smi::FromInt(EQUAL));
  __ Ret();
  __ bind(&not_identical);

  // Handle not identical strings.

  // Check that both strings are internalized strings. If they are, we're done
  // because we already know they are not identical. We know they are both
  // strings.
  if (equality) {
    ASSERT(GetCondition() == eq);
    STATIC_ASSERT(kInternalizedTag == 0);
    __ orx(tmp3, tmp1, tmp2);
    __ andi(r0, tmp3, Operand(kIsNotInternalizedMask));
    __ bne(&is_symbol, cr0);
    // Make sure r3 is non-zero. At this point input operands are
    // guaranteed to be non-zero.
    ASSERT(right.is(r3));
    __ Ret();
    __ bind(&is_symbol);
  }

  // Check that both strings are sequential ASCII.
  Label runtime;
  __ JumpIfBothInstanceTypesAreNotSequentialAscii(
      tmp1, tmp2, tmp3, tmp4, &runtime);

  // Compare flat ASCII strings. Returns when done.
  if (equality) {
    StringCompareStub::GenerateFlatAsciiStringEquals(
        masm, left, right, tmp1, tmp2);
  } else {
    StringCompareStub::GenerateCompareFlatAsciiStrings(
        masm, left, right, tmp1, tmp2, tmp3);
  }

  // Handle more complex cases in runtime.
  __ bind(&runtime);
  __ Push(left, right);
  if (equality) {
    __ TailCallRuntime(Runtime::kStringEquals, 2, 1);
  } else {
    __ TailCallRuntime(Runtime::kStringCompare, 2, 1);
  }

  __ bind(&miss);
  GenerateMiss(masm);
}


void ICCompareStub::GenerateObjects(MacroAssembler* masm) {
  ASSERT(state_ == CompareIC::OBJECT);
  Label miss;
  __ and_(r5, r4, r3);
  __ JumpIfSmi(r5, &miss);

  __ CompareObjectType(r3, r5, r5, JS_OBJECT_TYPE);
  __ bne(&miss);
  __ CompareObjectType(r4, r5, r5, JS_OBJECT_TYPE);
  __ bne(&miss);

  ASSERT(GetCondition() == eq);
  __ sub(r3, r3, r4);
  __ Ret();

  __ bind(&miss);
  GenerateMiss(masm);
}


void ICCompareStub::GenerateKnownObjects(MacroAssembler* masm) {
  Label miss;
  __ and_(r5, r4, r3);
  __ JumpIfSmi(r5, &miss);
  __ LoadP(r5, FieldMemOperand(r3, HeapObject::kMapOffset));
  __ LoadP(r6, FieldMemOperand(r4, HeapObject::kMapOffset));
  __ Cmpi(r5, Operand(known_map_), r0);
  __ bne(&miss);
  __ Cmpi(r6, Operand(known_map_), r0);
  __ bne(&miss);

  __ sub(r3, r3, r4);
  __ Ret();

  __ bind(&miss);
  GenerateMiss(masm);
}



void ICCompareStub::GenerateMiss(MacroAssembler* masm) {
  {
    // Call the runtime system in a fresh internal frame.
    ExternalReference miss =
        ExternalReference(IC_Utility(IC::kCompareIC_Miss), masm->isolate());

    FrameScope scope(masm, StackFrame::INTERNAL);
    __ Push(r4, r3);
    __ mflr(r0);
    __ push(r0);
    __ Push(r4, r3);
    __ LoadSmiLiteral(ip, Smi::FromInt(op_));
    __ push(ip);
    __ CallExternalReference(miss, 3);
    // Compute the entry point of the rewritten stub.
    __ addi(r5, r3, Operand(Code::kHeaderSize - kHeapObjectTag));
    // Restore registers.
    __ pop(r0);
    __ mtlr(r0);
    __ pop(r3);
    __ pop(r4);
  }

  __ Jump(r5);
}


// This stub is paired with DirectCEntryStub::GenerateCall
void DirectCEntryStub::Generate(MacroAssembler* masm) {
  // Place the return address on the stack, making the call
  // GC safe. The RegExp backend also relies on this.
  __ mflr(r0);
  __ StoreP(r0, MemOperand(sp, kStackFrameExtraParamSlot * kPointerSize));
  __ Call(ip);  // Call the C++ function.
  __ LoadP(r0, MemOperand(sp, kStackFrameExtraParamSlot * kPointerSize));
  __ mtlr(r0);
  __ blr();
}


void DirectCEntryStub::GenerateCall(MacroAssembler* masm,
                                    Register target) {
#if ABI_USES_FUNCTION_DESCRIPTORS && !defined(USE_SIMULATOR)
  // Native AIX/PPC64 Linux use a function descriptor.
  __ LoadP(ToRegister(2), MemOperand(target, kPointerSize));  // TOC
  __ LoadP(target, MemOperand(target, 0));  // Instruction address
#endif

  intptr_t code =
      reinterpret_cast<intptr_t>(GetCode(masm->isolate()).location());
  __ Move(ip, target);
  __ mov(r0, Operand(code, RelocInfo::CODE_TARGET));
  __ Call(r0);  // Call the stub.
}


void NameDictionaryLookupStub::GenerateNegativeLookup(MacroAssembler* masm,
                                                        Label* miss,
                                                        Label* done,
                                                        Register receiver,
                                                        Register properties,
                                                      Handle<Name> name,
                                                        Register scratch0) {
  ASSERT(name->IsUniqueName());
  // If names of slots in range from 1 to kProbes - 1 for the hash value are
  // not equal to the name and kProbes-th slot is not used (its name is the
  // undefined value), it guarantees the hash table doesn't contain the
  // property. It's true even if some slots represent deleted properties
  // (their names are the hole value).
  for (int i = 0; i < kInlinedProbes; i++) {
    // scratch0 points to properties hash.
    // Compute the masked index: (hash + i + i * i) & mask.
    Register index = scratch0;
    // Capacity is smi 2^n.
    __ LoadP(index, FieldMemOperand(properties, kCapacityOffset));
    __ subi(index, index, Operand(1));
    __ LoadSmiLiteral(ip, Smi::FromInt(name->Hash() +
                                       NameDictionary::GetProbeOffset(i)));
    __ and_(index, index, ip);

    // Scale the index by multiplying by the entry size.
    ASSERT(NameDictionary::kEntrySize == 3);
    __ ShiftLeftImm(ip, index, Operand(1));
    __ add(index, index, ip);  // index *= 3.

    Register entity_name = scratch0;
    // Having undefined at this place means the name is not contained.
    Register tmp = properties;
    __ SmiToPtrArrayOffset(ip, index);
    __ add(tmp, properties, ip);
    __ LoadP(entity_name, FieldMemOperand(tmp, kElementsStartOffset));

    ASSERT(!tmp.is(entity_name));
    __ LoadRoot(tmp, Heap::kUndefinedValueRootIndex);
    __ cmp(entity_name, tmp);
    __ beq(done);

    // Load the hole ready for use below:
    __ LoadRoot(tmp, Heap::kTheHoleValueRootIndex);

    // Stop if found the property.
    __ Cmpi(entity_name, Operand(Handle<Name>(name)), r0);
    __ beq(miss);

    Label good;
    __ cmp(entity_name, tmp);
    __ beq(&good);

    // Check if the entry name is not a unique name.
    __ LoadP(entity_name, FieldMemOperand(entity_name,
                                          HeapObject::kMapOffset));
    __ lbz(entity_name,
           FieldMemOperand(entity_name, Map::kInstanceTypeOffset));
    __ JumpIfNotUniqueName(entity_name, miss);
    __ bind(&good);

    // Restore the properties.
    __ LoadP(properties,
             FieldMemOperand(receiver, JSObject::kPropertiesOffset));
  }

  const int spill_mask =
      (r0.bit() | r9.bit() | r8.bit() | r7.bit() | r6.bit() |
       r5.bit() | r4.bit() | r3.bit());

  __ mflr(r0);
  __ MultiPush(spill_mask);

  __ LoadP(r3, FieldMemOperand(receiver, JSObject::kPropertiesOffset));
  __ mov(r4, Operand(Handle<Name>(name)));
  NameDictionaryLookupStub stub(NEGATIVE_LOOKUP);
  __ CallStub(&stub);
  __ cmpi(r3, Operand::Zero());

  __ MultiPop(spill_mask);  // MultiPop does not touch condition flags
  __ mtlr(r0);

  __ beq(done);
  __ bne(miss);
}


// Probe the name dictionary in the |elements| register. Jump to the
// |done| label if a property with the given name is found. Jump to
// the |miss| label otherwise.
// If lookup was successful |scratch2| will be equal to elements + 4 * index.
void NameDictionaryLookupStub::GeneratePositiveLookup(MacroAssembler* masm,
                                                        Label* miss,
                                                        Label* done,
                                                        Register elements,
                                                        Register name,
                                                        Register scratch1,
                                                        Register scratch2) {
  ASSERT(!elements.is(scratch1));
  ASSERT(!elements.is(scratch2));
  ASSERT(!name.is(scratch1));
  ASSERT(!name.is(scratch2));

  __ AssertName(name);

  // Compute the capacity mask.
  __ LoadP(scratch1, FieldMemOperand(elements, kCapacityOffset));
  __ SmiUntag(scratch1);  // convert smi to int
  __ subi(scratch1, scratch1, Operand(1));

  // Generate an unrolled loop that performs a few probes before
  // giving up. Measurements done on Gmail indicate that 2 probes
  // cover ~93% of loads from dictionaries.
  for (int i = 0; i < kInlinedProbes; i++) {
    // Compute the masked index: (hash + i + i * i) & mask.
    __ lwz(scratch2, FieldMemOperand(name, Name::kHashFieldOffset));
    if (i > 0) {
      // Add the probe offset (i + i * i) left shifted to avoid right shifting
      // the hash in a separate instruction. The value hash + i + i * i is right
      // shifted in the following and instruction.
      ASSERT(NameDictionary::GetProbeOffset(i) <
             1 << (32 - Name::kHashFieldOffset));
      __ addi(scratch2, scratch2, Operand(
                  NameDictionary::GetProbeOffset(i) << Name::kHashShift));
    }
    __ srwi(scratch2, scratch2, Operand(Name::kHashShift));
    __ and_(scratch2, scratch1, scratch2);

    // Scale the index by multiplying by the element size.
    ASSERT(NameDictionary::kEntrySize == 3);
    // scratch2 = scratch2 * 3.
    __ ShiftLeftImm(ip, scratch2, Operand(1));
    __ add(scratch2, scratch2, ip);

    // Check if the key is identical to the name.
    __ ShiftLeftImm(ip, scratch2, Operand(kPointerSizeLog2));
    __ add(scratch2, elements, ip);
    __ LoadP(ip, FieldMemOperand(scratch2, kElementsStartOffset));
    __ cmp(name, ip);
    __ beq(done);
  }

  const int spill_mask =
      (r0.bit() | r9.bit() | r8.bit() | r7.bit() |
       r6.bit() | r5.bit() | r4.bit() | r3.bit()) &
      ~(scratch1.bit() | scratch2.bit());

  __ mflr(r0);
  __ MultiPush(spill_mask);
  if (name.is(r3)) {
    ASSERT(!elements.is(r4));
    __ mr(r4, name);
    __ mr(r3, elements);
  } else {
    __ mr(r3, elements);
    __ mr(r4, name);
  }
  NameDictionaryLookupStub stub(POSITIVE_LOOKUP);
  __ CallStub(&stub);
  __ cmpi(r3, Operand::Zero());
  __ mr(scratch2, r5);
  __ MultiPop(spill_mask);
  __ mtlr(r0);

  __ bne(done);
  __ beq(miss);
}


void NameDictionaryLookupStub::Generate(MacroAssembler* masm) {
  // This stub overrides SometimesSetsUpAFrame() to return false.  That means
  // we cannot call anything that could cause a GC from this stub.
  // Registers:
  //  result: NameDictionary to probe
  //  r4: key
  //  dictionary: NameDictionary to probe.
  //  index: will hold an index of entry if lookup is successful.
  //         might alias with result_.
  // Returns:
  //  result_ is zero if lookup failed, non zero otherwise.

  Register result = r3;
  Register dictionary = r3;
  Register key = r4;
  Register index = r5;
  Register mask = r6;
  Register hash = r7;
  Register undefined = r8;
  Register entry_key = r9;
  Register scratch = r9;

  Label in_dictionary, maybe_in_dictionary, not_in_dictionary;

  __ LoadP(mask, FieldMemOperand(dictionary, kCapacityOffset));
  __ SmiUntag(mask);
  __ subi(mask, mask, Operand(1));

  __ lwz(hash, FieldMemOperand(key, Name::kHashFieldOffset));

  __ LoadRoot(undefined, Heap::kUndefinedValueRootIndex);

  for (int i = kInlinedProbes; i < kTotalProbes; i++) {
    // Compute the masked index: (hash + i + i * i) & mask.
    // Capacity is smi 2^n.
    if (i > 0) {
      // Add the probe offset (i + i * i) left shifted to avoid right shifting
      // the hash in a separate instruction. The value hash + i + i * i is right
      // shifted in the following and instruction.
      ASSERT(NameDictionary::GetProbeOffset(i) <
             1 << (32 - Name::kHashFieldOffset));
      __ addi(index, hash, Operand(
                  NameDictionary::GetProbeOffset(i) << Name::kHashShift));
    } else {
      __ mr(index, hash);
    }
    __ srwi(r0, index, Operand(Name::kHashShift));
    __ and_(index, mask, r0);

    // Scale the index by multiplying by the entry size.
    ASSERT(NameDictionary::kEntrySize == 3);
    __ ShiftLeftImm(scratch, index, Operand(1));
    __ add(index, index, scratch);  // index *= 3.

    ASSERT_EQ(kSmiTagSize, 1);
    __ ShiftLeftImm(scratch, index, Operand(kPointerSizeLog2));
    __ add(index, dictionary, scratch);
    __ LoadP(entry_key, FieldMemOperand(index, kElementsStartOffset));

    // Having undefined at this place means the name is not contained.
    __ cmp(entry_key, undefined);
    __ beq(&not_in_dictionary);

    // Stop if found the property.
    __ cmp(entry_key, key);
    __ beq(&in_dictionary);

    if (i != kTotalProbes - 1 && mode_ == NEGATIVE_LOOKUP) {
      // Check if the entry name is not a unique name.
      __ LoadP(entry_key, FieldMemOperand(entry_key, HeapObject::kMapOffset));
      __ lbz(entry_key,
              FieldMemOperand(entry_key, Map::kInstanceTypeOffset));
      __ JumpIfNotUniqueName(entry_key, &maybe_in_dictionary);
    }
  }

  __ bind(&maybe_in_dictionary);
  // If we are doing negative lookup then probing failure should be
  // treated as a lookup success. For positive lookup probing failure
  // should be treated as lookup failure.
  if (mode_ == POSITIVE_LOOKUP) {
    __ li(result, Operand::Zero());
    __ Ret();
  }

  __ bind(&in_dictionary);
  __ li(result, Operand(1));
  __ Ret();

  __ bind(&not_in_dictionary);
  __ li(result, Operand::Zero());
  __ Ret();
}


struct AheadOfTimeWriteBarrierStubList {
  Register object, value, address;
  RememberedSetAction action;
};


#define REG(Name) { kRegister_ ## Name ## _Code }

static const AheadOfTimeWriteBarrierStubList kAheadOfTime[] = {
  // Used in RegExpExecStub.
  { REG(r16), REG(r14), REG(r10), EMIT_REMEMBERED_SET },
  // Used in CompileArrayPushCall.
  // Also used in StoreIC::GenerateNormal via GenerateDictionaryStore.
  // Also used in KeyedStoreIC::GenerateGeneric.
  { REG(r6), REG(r7), REG(r8), EMIT_REMEMBERED_SET },
  // Used in StoreStubCompiler::CompileStoreField via GenerateStoreField.
  { REG(r4), REG(r5), REG(r6), EMIT_REMEMBERED_SET },
  { REG(r6), REG(r5), REG(r4), EMIT_REMEMBERED_SET },
  // Used in KeyedStoreStubCompiler::CompileStoreField via GenerateStoreField.
  { REG(r5), REG(r4), REG(r6), EMIT_REMEMBERED_SET },
  { REG(r6), REG(r4), REG(r5), EMIT_REMEMBERED_SET },
  // KeyedStoreStubCompiler::GenerateStoreFastElement.
  { REG(r6), REG(r5), REG(r7), EMIT_REMEMBERED_SET },
  { REG(r5), REG(r6), REG(r7), EMIT_REMEMBERED_SET },
  // ElementsTransitionGenerator::GenerateMapChangeElementTransition
  // and ElementsTransitionGenerator::GenerateSmiToDouble
  // and ElementsTransitionGenerator::GenerateDoubleToObject
  { REG(r5), REG(r6), REG(r11), EMIT_REMEMBERED_SET },
  { REG(r5), REG(r6), REG(r11), OMIT_REMEMBERED_SET },
  // ElementsTransitionGenerator::GenerateDoubleToObject
  { REG(r9), REG(r5), REG(r3), EMIT_REMEMBERED_SET },
  { REG(r5), REG(r9), REG(r11), EMIT_REMEMBERED_SET },
  // StoreArrayLiteralElementStub::Generate
  { REG(r8), REG(r3), REG(r9), EMIT_REMEMBERED_SET },
  // FastNewClosureStub::Generate
  { REG(r5), REG(r7), REG(r4), EMIT_REMEMBERED_SET },
  // StringAddStub::Generate
  { REG(r10), REG(r4), REG(r7), EMIT_REMEMBERED_SET },
  { REG(r10), REG(r3), REG(r7), EMIT_REMEMBERED_SET },
  // Null termination.
  { REG(no_reg), REG(no_reg), REG(no_reg), EMIT_REMEMBERED_SET}
};

#undef REG


bool RecordWriteStub::IsPregenerated(Isolate* isolate) {
  for (const AheadOfTimeWriteBarrierStubList* entry = kAheadOfTime;
       !entry->object.is(no_reg);
       entry++) {
    if (object_.is(entry->object) &&
        value_.is(entry->value) &&
        address_.is(entry->address) &&
        remembered_set_action_ == entry->action &&
        save_fp_regs_mode_ == kDontSaveFPRegs) {
      return true;
    }
  }
  return false;
}


void StoreBufferOverflowStub::GenerateFixedRegStubsAheadOfTime(
    Isolate* isolate) {
  StoreBufferOverflowStub stub1(kDontSaveFPRegs);
  stub1.GetCode(isolate)->set_is_pregenerated(true);
  // Hydrogen code stubs need stub2 at snapshot time.
  StoreBufferOverflowStub stub2(kSaveFPRegs);
  stub2.GetCode(isolate)->set_is_pregenerated(true);
}


void RecordWriteStub::GenerateFixedRegStubsAheadOfTime(Isolate* isolate) {
  for (const AheadOfTimeWriteBarrierStubList* entry = kAheadOfTime;
       !entry->object.is(no_reg);
       entry++) {
    RecordWriteStub stub(entry->object,
                         entry->value,
                         entry->address,
                         entry->action,
                         kDontSaveFPRegs);
    stub.GetCode(isolate)->set_is_pregenerated(true);
  }
}


bool CodeStub::CanUseFPRegisters() {
  return true;
}


// Takes the input in 3 registers: address_ value_ and object_.  A pointer to
// the value has just been written into the object, now this stub makes sure
// we keep the GC informed.  The word in the object where the value has been
// written is in the address register.
void RecordWriteStub::Generate(MacroAssembler* masm) {
  Label skip_to_incremental_noncompacting;
  Label skip_to_incremental_compacting;
  const int crBit = Assembler::encode_crbit(cr2, CR_LT);

  // The first two branch instructions are generated with labels so as to
  // get the offset fixed up correctly by the bind(Label*) call.  We patch
  // it back and forth between branch condition True and False
  // when we start and stop incremental heap marking.
  // See RecordWriteStub::Patch for details.

  // Clear the bit, branch on True for NOP action initially
  __ crxor(crBit, crBit, crBit);
  __ blt(&skip_to_incremental_noncompacting, cr2);
  __ blt(&skip_to_incremental_compacting, cr2);

  if (remembered_set_action_ == EMIT_REMEMBERED_SET) {
    __ RememberedSetHelper(object_,
                           address_,
                           value_,
                           save_fp_regs_mode_,
                           MacroAssembler::kReturnAtEnd);
  }
  __ Ret();

  __ bind(&skip_to_incremental_noncompacting);
  GenerateIncremental(masm, INCREMENTAL);

  __ bind(&skip_to_incremental_compacting);
  GenerateIncremental(masm, INCREMENTAL_COMPACTION);

  // Initial mode of the stub is expected to be STORE_BUFFER_ONLY.
  // Will be checked in IncrementalMarking::ActivateGeneratedStub.
  // patching not required on PPC as the initial path is effectively NOP
}


void RecordWriteStub::GenerateIncremental(MacroAssembler* masm, Mode mode) {
  regs_.Save(masm);

  if (remembered_set_action_ == EMIT_REMEMBERED_SET) {
    Label dont_need_remembered_set;

    __ LoadP(regs_.scratch0(), MemOperand(regs_.address(), 0));
    __ JumpIfNotInNewSpace(regs_.scratch0(),  // Value.
                           regs_.scratch0(),
                           &dont_need_remembered_set);

    __ CheckPageFlag(regs_.object(),
                     regs_.scratch0(),
                     1 << MemoryChunk::SCAN_ON_SCAVENGE,
                     ne,
                     &dont_need_remembered_set);

    // First notify the incremental marker if necessary, then update the
    // remembered set.
    CheckNeedsToInformIncrementalMarker(
        masm, kUpdateRememberedSetOnNoNeedToInformIncrementalMarker, mode);
    InformIncrementalMarker(masm, mode);
    regs_.Restore(masm);
    __ RememberedSetHelper(object_,
                           address_,
                           value_,
                           save_fp_regs_mode_,
                           MacroAssembler::kReturnAtEnd);

    __ bind(&dont_need_remembered_set);
  }

  CheckNeedsToInformIncrementalMarker(
      masm, kReturnOnNoNeedToInformIncrementalMarker, mode);
  InformIncrementalMarker(masm, mode);
  regs_.Restore(masm);
  __ Ret();
}


void RecordWriteStub::InformIncrementalMarker(MacroAssembler* masm, Mode mode) {
  regs_.SaveCallerSaveRegisters(masm, save_fp_regs_mode_);
  int argument_count = 3;
  __ PrepareCallCFunction(argument_count, regs_.scratch0());
  Register address =
      r3.is(regs_.address()) ? regs_.scratch0() : regs_.address();
  ASSERT(!address.is(regs_.object()));
  ASSERT(!address.is(r3));
  __ mr(address, regs_.address());
  __ mr(r3, regs_.object());
  __ mr(r4, address);
  __ mov(r5, Operand(ExternalReference::isolate_address(masm->isolate())));

  AllowExternalCallThatCantCauseGC scope(masm);
  if (mode == INCREMENTAL_COMPACTION) {
    __ CallCFunction(
        ExternalReference::incremental_evacuation_record_write_function(
            masm->isolate()),
        argument_count);
  } else {
    ASSERT(mode == INCREMENTAL);
    __ CallCFunction(
        ExternalReference::incremental_marking_record_write_function(
            masm->isolate()),
        argument_count);
  }
  regs_.RestoreCallerSaveRegisters(masm, save_fp_regs_mode_);
}


void RecordWriteStub::CheckNeedsToInformIncrementalMarker(
    MacroAssembler* masm,
    OnNoNeedToInformIncrementalMarker on_no_need,
    Mode mode) {
  Label on_black;
  Label need_incremental;
  Label need_incremental_pop_scratch;

  ASSERT((~Page::kPageAlignmentMask & 0xffff) == 0);
  __ lis(r0, Operand((~Page::kPageAlignmentMask >> 16)));
  __ and_(regs_.scratch0(), regs_.object(), r0);
  __ LoadP(regs_.scratch1(),
         MemOperand(regs_.scratch0(),
                    MemoryChunk::kWriteBarrierCounterOffset));
  __ subi(regs_.scratch1(), regs_.scratch1(), Operand(1));
  __ StoreP(regs_.scratch1(),
            MemOperand(regs_.scratch0(),
                       MemoryChunk::kWriteBarrierCounterOffset));
  __ cmpi(regs_.scratch1(), Operand::Zero());  // PPC, we could do better here
  __ blt(&need_incremental);

  // Let's look at the color of the object:  If it is not black we don't have
  // to inform the incremental marker.
  __ JumpIfBlack(regs_.object(), regs_.scratch0(), regs_.scratch1(), &on_black);

  regs_.Restore(masm);
  if (on_no_need == kUpdateRememberedSetOnNoNeedToInformIncrementalMarker) {
    __ RememberedSetHelper(object_,
                           address_,
                           value_,
                           save_fp_regs_mode_,
                           MacroAssembler::kReturnAtEnd);
  } else {
    __ Ret();
  }

  __ bind(&on_black);

  // Get the value from the slot.
  __ LoadP(regs_.scratch0(), MemOperand(regs_.address(), 0));

  if (mode == INCREMENTAL_COMPACTION) {
    Label ensure_not_white;

    __ CheckPageFlag(regs_.scratch0(),  // Contains value.
                     regs_.scratch1(),  // Scratch.
                     MemoryChunk::kEvacuationCandidateMask,
                     eq,
                     &ensure_not_white);

    __ CheckPageFlag(regs_.object(),
                     regs_.scratch1(),  // Scratch.
                     MemoryChunk::kSkipEvacuationSlotsRecordingMask,
                     eq,
                     &need_incremental);

    __ bind(&ensure_not_white);
  }

  // We need extra registers for this, so we push the object and the address
  // register temporarily.
  __ Push(regs_.object(), regs_.address());
  __ EnsureNotWhite(regs_.scratch0(),  // The value.
                    regs_.scratch1(),  // Scratch.
                    regs_.object(),  // Scratch.
                    regs_.address(),  // Scratch.
                    &need_incremental_pop_scratch);
  __ Pop(regs_.object(), regs_.address());

  regs_.Restore(masm);
  if (on_no_need == kUpdateRememberedSetOnNoNeedToInformIncrementalMarker) {
    __ RememberedSetHelper(object_,
                           address_,
                           value_,
                           save_fp_regs_mode_,
                           MacroAssembler::kReturnAtEnd);
  } else {
    __ Ret();
  }

  __ bind(&need_incremental_pop_scratch);
  __ Pop(regs_.object(), regs_.address());

  __ bind(&need_incremental);

  // Fall through when we need to inform the incremental marker.
}


void StoreArrayLiteralElementStub::Generate(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- r3    : element value to store
  //  -- r6    : element index as smi
  //  -- sp[0] : array literal index in function as smi
  //  -- sp[4] : array literal
  // clobbers r3, r5, r7
  // -----------------------------------

  Label element_done;
  Label double_elements;
  Label smi_element;
  Label slow_elements;
  Label fast_elements;

  // Get array literal index, array literal and its map.
  __ LoadP(r7, MemOperand(sp, 0 * kPointerSize));
  __ LoadP(r4, MemOperand(sp, 1 * kPointerSize));
  __ LoadP(r5, FieldMemOperand(r4, JSObject::kMapOffset));

  __ CheckFastElements(r5, r8, &double_elements);
  // FAST_*_SMI_ELEMENTS or FAST_*_ELEMENTS
  __ JumpIfSmi(r3, &smi_element);
  __ CheckFastSmiElements(r5, r8, &fast_elements);

  // Store into the array literal requires a elements transition. Call into
  // the runtime.
  __ bind(&slow_elements);
  // call.
  __ Push(r4, r6, r3);
  __ LoadP(r8, MemOperand(fp, JavaScriptFrameConstants::kFunctionOffset));
  __ LoadP(r8, FieldMemOperand(r8, JSFunction::kLiteralsOffset));
  __ Push(r8, r7);
  __ TailCallRuntime(Runtime::kStoreArrayLiteralElement, 5, 1);

  // Array literal has ElementsKind of FAST_*_ELEMENTS and value is an object.
  __ bind(&fast_elements);
  __ LoadP(r8, FieldMemOperand(r4, JSObject::kElementsOffset));
  __ SmiToPtrArrayOffset(r9, r6);
  __ add(r9, r8, r9);
#if V8_TARGET_ARCH_PPC64
  // add due to offset alignment requirements of StorePU
  __ addi(r9, r9, Operand(FixedArray::kHeaderSize - kHeapObjectTag));
  __ StoreP(r3, MemOperand(r9));
#else
  __ StorePU(r3, MemOperand(r9, FixedArray::kHeaderSize - kHeapObjectTag));
#endif
  // Update the write barrier for the array store.
  __ RecordWrite(r8, r9, r3, kLRHasNotBeenSaved, kDontSaveFPRegs,
                 EMIT_REMEMBERED_SET, OMIT_SMI_CHECK);
  __ Ret();

  // Array literal has ElementsKind of FAST_*_SMI_ELEMENTS or FAST_*_ELEMENTS,
  // and value is Smi.
  __ bind(&smi_element);
  __ LoadP(r8, FieldMemOperand(r4, JSObject::kElementsOffset));
  __ SmiToPtrArrayOffset(r9, r6);
  __ add(r9, r8, r9);
  __ StoreP(r3, FieldMemOperand(r9, FixedArray::kHeaderSize), r0);
  __ Ret();

  // Array literal has ElementsKind of FAST_DOUBLE_ELEMENTS.
  __ bind(&double_elements);
  __ LoadP(r8, FieldMemOperand(r4, JSObject::kElementsOffset));
  __ StoreNumberToDoubleElements(r3, r6, r8, r9, d0, &slow_elements);
  __ Ret();
}


void StubFailureTrampolineStub::Generate(MacroAssembler* masm) {
  CEntryStub ces(1, fp_registers_ ? kSaveFPRegs : kDontSaveFPRegs);
  __ Call(ces.GetCode(masm->isolate()), RelocInfo::CODE_TARGET);
  int parameter_count_offset =
      StubFailureTrampolineFrame::kCallerStackParameterCountFrameOffset;
  __ LoadP(r4, MemOperand(fp, parameter_count_offset));
  if (function_mode_ == JS_FUNCTION_STUB_MODE) {
    __ addi(r4, r4, Operand(1));
  }
  masm->LeaveFrame(StackFrame::STUB_FAILURE_TRAMPOLINE);
  __ slwi(r4, r4, Operand(kPointerSizeLog2));
  __ add(sp, sp, r4);
  __ Ret();
}


void ProfileEntryHookStub::MaybeCallEntryHook(MacroAssembler* masm) {
  if (masm->isolate()->function_entry_hook() != NULL) {
    PredictableCodeSizeScope predictable(masm,
#if V8_TARGET_ARCH_PPC64
                                         12 * Assembler::kInstrSize);
#else
                                         9 * Assembler::kInstrSize);
#endif
    AllowStubCallsScope allow_stub_calls(masm, true);
    ProfileEntryHookStub stub;
    __ mflr(r0);
    __ push(r0);
    __ CallStub(&stub);
    __ pop(r0);
    __ mtlr(r0);
  }
}


void ProfileEntryHookStub::Generate(MacroAssembler* masm) {
  // The entry hook is a "push lr" instruction, followed by a call.
  const int32_t kReturnAddressDistanceFromFunctionStart =
      Assembler::kCallTargetAddressOffset + 2 * Assembler::kInstrSize;

  // This should contain all kJSCallerSaved registers.
  const RegList kSavedRegs =
     kJSCallerSaved |  // Caller saved registers.
     r15.bit();        // Saved stack pointer.

  // We also save lr, so the count here is one higher than the mask indicates.
  const int32_t kNumSavedRegs = kNumJSCallerSaved + 2;

  // Save all caller-save registers as this may be called from anywhere.
  __ mflr(r0);
  __ MultiPush(kSavedRegs | r0.bit());

  // Compute the function's address for the first argument.
  __ mr(r3, r0);
  __ subi(r3, r3, Operand(kReturnAddressDistanceFromFunctionStart));

  // The caller's return address is above the saved temporaries.
  // Grab that for the second argument to the hook.
  __ addi(r4, sp, Operand(kNumSavedRegs * kPointerSize));

  // Align the stack if necessary.
  int frame_alignment = masm->ActivationFrameAlignment();
  if (frame_alignment > kPointerSize) {
    __ mr(r15, sp);
    ASSERT(IsPowerOf2(frame_alignment));
    __ ClearRightImm(sp, sp, Operand(WhichPowerOf2(frame_alignment)));
  }

#if !defined(USE_SIMULATOR)
  uintptr_t entry_hook =
      reinterpret_cast<uintptr_t>(masm->isolate()->function_entry_hook());
  __ mov(ip, Operand(entry_hook));

#if ABI_USES_FUNCTION_DESCRIPTORS
  // Function descriptor
  __ LoadP(ToRegister(2), MemOperand(ip, kPointerSize));
  __ LoadP(ip, MemOperand(ip, 0));
#endif

  // PPC LINUX ABI:
  __ li(r0, Operand::Zero());
  __ StorePU(r0, MemOperand(sp, -kNumRequiredStackFrameSlots * kPointerSize));
#else
  // Under the simulator we need to indirect the entry hook through a
  // trampoline function at a known address.
  // It additionally takes an isolate as a third parameter
  __ mov(r5, Operand(ExternalReference::isolate_address(masm->isolate())));

  ApiFunction dispatcher(FUNCTION_ADDR(EntryHookTrampoline));
  __ mov(ip, Operand(ExternalReference(&dispatcher,
                                       ExternalReference::BUILTIN_CALL,
                                       masm->isolate())));
#endif
  __ Call(ip);

#if !defined(USE_SIMULATOR)
  __ addi(sp, sp, Operand(kNumRequiredStackFrameSlots * kPointerSize));
#endif

  // Restore the stack pointer if needed.
  if (frame_alignment > kPointerSize) {
    __ mr(sp, r15);
  }

  // Also pop lr to get Ret(0).
  __ MultiPop(kSavedRegs | r0.bit());
  __ mtlr(r0);
  __ Ret();
}


template<class T>
static void CreateArrayDispatch(MacroAssembler* masm,
                                AllocationSiteOverrideMode mode) {
  if (mode == DISABLE_ALLOCATION_SITES) {
    T stub(GetInitialFastElementsKind(),
           CONTEXT_CHECK_REQUIRED,
           mode);
    __ TailCallStub(&stub);
  } else if (mode == DONT_OVERRIDE) {
    int last_index = GetSequenceIndexFromFastElementsKind(
        TERMINAL_FAST_ELEMENTS_KIND);
    for (int i = 0; i <= last_index; ++i) {
      Label next;
      ElementsKind kind = GetFastElementsKindFromSequenceIndex(i);
      __ Cmpi(r6, Operand(kind), r0);
      __ bne(&next);
      T stub(kind);
      __ TailCallStub(&stub);
      __ bind(&next);
    }

    // If we reached this point there is a problem.
    __ Abort(kUnexpectedElementsKindInArrayConstructor);
  } else {
    UNREACHABLE();
  }
}


static void CreateArrayDispatchOneArgument(MacroAssembler* masm,
                                           AllocationSiteOverrideMode mode) {
  // r5 - type info cell (if mode != DISABLE_ALLOCATION_SITES)
  // r6 - kind (if mode != DISABLE_ALLOCATION_SITES)
  // r3 - number of arguments
  // r4 - constructor?
  // sp[0] - last argument
  Label normal_sequence;
  if (mode == DONT_OVERRIDE) {
    ASSERT(FAST_SMI_ELEMENTS == 0);
    ASSERT(FAST_HOLEY_SMI_ELEMENTS == 1);
    ASSERT(FAST_ELEMENTS == 2);
    ASSERT(FAST_HOLEY_ELEMENTS == 3);
    ASSERT(FAST_DOUBLE_ELEMENTS == 4);
    ASSERT(FAST_HOLEY_DOUBLE_ELEMENTS == 5);

    // is the low bit set? If so, we are holey and that is good.
    __ andi(r0, r6, Operand(1));
    __ bne(&normal_sequence, cr0);
  }

  // look at the first argument
  __ LoadP(r8, MemOperand(sp, 0));
  __ cmpi(r8, Operand::Zero());
  __ beq(&normal_sequence);

  if (mode == DISABLE_ALLOCATION_SITES) {
    ElementsKind initial = GetInitialFastElementsKind();
    ElementsKind holey_initial = GetHoleyElementsKind(initial);

    ArraySingleArgumentConstructorStub stub_holey(holey_initial,
                                                  CONTEXT_CHECK_REQUIRED,
                                                  DISABLE_ALLOCATION_SITES);
    __ TailCallStub(&stub_holey);

    __ bind(&normal_sequence);
    ArraySingleArgumentConstructorStub stub(initial,
                                            CONTEXT_CHECK_REQUIRED,
                                            DISABLE_ALLOCATION_SITES);
    __ TailCallStub(&stub);
  } else if (mode == DONT_OVERRIDE) {
    // We are going to create a holey array, but our kind is non-holey.
    // Fix kind and retry (only if we have an allocation site in the cell).
    __ addi(r6, r6, Operand(1));
    __ LoadP(r8, FieldMemOperand(r5, Cell::kValueOffset));

    if (FLAG_debug_code) {
      __ LoadP(r8, FieldMemOperand(r8, 0));
      __ CompareRoot(r8, Heap::kAllocationSiteMapRootIndex);
      __ Assert(eq, kExpectedAllocationSiteInCell);
      __ LoadP(r8, FieldMemOperand(r5, Cell::kValueOffset));
    }

    // Save the resulting elements kind in type info
    __ SmiTag(r6);
    __ LoadP(r8, FieldMemOperand(r5, Cell::kValueOffset));
    __ StoreP(r6, FieldMemOperand(r8, AllocationSite::kTransitionInfoOffset),
              r0);
    __ SmiUntag(r6);

    __ bind(&normal_sequence);
    int last_index = GetSequenceIndexFromFastElementsKind(
        TERMINAL_FAST_ELEMENTS_KIND);
    for (int i = 0; i <= last_index; ++i) {
      Label next;
      ElementsKind kind = GetFastElementsKindFromSequenceIndex(i);
      __ mov(r0, Operand(kind));
      __ cmp(r6, r0);
      __ bne(&next);
      ArraySingleArgumentConstructorStub stub(kind);
      __ TailCallStub(&stub);
      __ bind(&next);
    }

    // If we reached this point there is a problem.
    __ Abort(kUnexpectedElementsKindInArrayConstructor);
  } else {
    UNREACHABLE();
  }
}


template<class T>
static void ArrayConstructorStubAheadOfTimeHelper(Isolate* isolate) {
  ElementsKind initial_kind = GetInitialFastElementsKind();
  ElementsKind initial_holey_kind = GetHoleyElementsKind(initial_kind);

  int to_index = GetSequenceIndexFromFastElementsKind(
      TERMINAL_FAST_ELEMENTS_KIND);
  for (int i = 0; i <= to_index; ++i) {
    ElementsKind kind = GetFastElementsKindFromSequenceIndex(i);
    T stub(kind);
    stub.GetCode(isolate)->set_is_pregenerated(true);
    if (AllocationSite::GetMode(kind) != DONT_TRACK_ALLOCATION_SITE ||
        (!FLAG_track_allocation_sites &&
         (kind == initial_kind || kind == initial_holey_kind))) {
      T stub1(kind, CONTEXT_CHECK_REQUIRED, DISABLE_ALLOCATION_SITES);
      stub1.GetCode(isolate)->set_is_pregenerated(true);
    }
  }
}


void ArrayConstructorStubBase::GenerateStubsAheadOfTime(Isolate* isolate) {
  ArrayConstructorStubAheadOfTimeHelper<ArrayNoArgumentConstructorStub>(
      isolate);
  ArrayConstructorStubAheadOfTimeHelper<ArraySingleArgumentConstructorStub>(
      isolate);
  ArrayConstructorStubAheadOfTimeHelper<ArrayNArgumentsConstructorStub>(
      isolate);
}


void InternalArrayConstructorStubBase::GenerateStubsAheadOfTime(
    Isolate* isolate) {
  ElementsKind kinds[2] = { FAST_ELEMENTS, FAST_HOLEY_ELEMENTS };
  for (int i = 0; i < 2; i++) {
    // For internal arrays we only need a few things
    InternalArrayNoArgumentConstructorStub stubh1(kinds[i]);
    stubh1.GetCode(isolate)->set_is_pregenerated(true);
    InternalArraySingleArgumentConstructorStub stubh2(kinds[i]);
    stubh2.GetCode(isolate)->set_is_pregenerated(true);
    InternalArrayNArgumentsConstructorStub stubh3(kinds[i]);
    stubh3.GetCode(isolate)->set_is_pregenerated(true);
  }
}


void ArrayConstructorStub::GenerateDispatchToArrayStub(
    MacroAssembler* masm,
    AllocationSiteOverrideMode mode) {
  if (argument_count_ == ANY) {
    Label not_zero_case, not_one_case;
    __ cmpi(r3, Operand::Zero());
    __ bne(&not_zero_case);
    CreateArrayDispatch<ArrayNoArgumentConstructorStub>(masm, mode);

    __ bind(&not_zero_case);
    __ cmpi(r3, Operand(1));
    __ bgt(&not_one_case);
    CreateArrayDispatchOneArgument(masm, mode);

    __ bind(&not_one_case);
    CreateArrayDispatch<ArrayNArgumentsConstructorStub>(masm, mode);
  } else if (argument_count_ == NONE) {
    CreateArrayDispatch<ArrayNoArgumentConstructorStub>(masm, mode);
  } else if (argument_count_ == ONE) {
    CreateArrayDispatchOneArgument(masm, mode);
  } else if (argument_count_ == MORE_THAN_ONE) {
    CreateArrayDispatch<ArrayNArgumentsConstructorStub>(masm, mode);
  } else {
    UNREACHABLE();
  }
}


void ArrayConstructorStub::Generate(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- r3 : argc (only if argument_count_ == ANY)
  //  -- r4 : constructor
  //  -- r5 : type info cell
  //  -- sp[0] : return address
  //  -- sp[4] : last argument
  // -----------------------------------
  if (FLAG_debug_code) {
    // The array construct code is only set for the global and natives
    // builtin Array functions which always have maps.

    // Initial map for the builtin Array function should be a map.
    __ LoadP(r6, FieldMemOperand(r4, JSFunction::kPrototypeOrInitialMapOffset));
    // Will both indicate a NULL and a Smi.
    __ andi(r0, r6, Operand(kSmiTagMask));
    __ Assert(ne, kUnexpectedInitialMapForArrayFunction, cr0);
    __ CompareObjectType(r6, r6, r7, MAP_TYPE);
    __ Assert(eq, kUnexpectedInitialMapForArrayFunction);

    // We should either have undefined in ebx or a valid cell
    Label okay_here;
    Handle<Map> cell_map = masm->isolate()->factory()->cell_map();
    __ CompareRoot(r5, Heap::kUndefinedValueRootIndex);
    __ beq(&okay_here);
    __ LoadP(r6, FieldMemOperand(r5, 0));
    __ mov(r0, Operand(cell_map));
    __ cmp(r6, r0);
    __ Assert(eq, kExpectedPropertyCellInRegisterEbx);
    __ bind(&okay_here);
  }

  Label no_info;
  // Get the elements kind and case on that.
  __ CompareRoot(r5, Heap::kUndefinedValueRootIndex);
  __ beq(&no_info);
  __ LoadP(r6, FieldMemOperand(r5, Cell::kValueOffset));

  // If the type cell is undefined, or contains anything other than an
  // AllocationSite, call an array constructor that doesn't use AllocationSites.
  __ LoadP(r7, FieldMemOperand(r6, 0));
  __ CompareRoot(r7, Heap::kAllocationSiteMapRootIndex);
  __ bne(&no_info);

  __ LoadP(r6, FieldMemOperand(r6, AllocationSite::kTransitionInfoOffset));
  __ SmiUntag(r6);
  GenerateDispatchToArrayStub(masm, DONT_OVERRIDE);

  __ bind(&no_info);
  GenerateDispatchToArrayStub(masm, DISABLE_ALLOCATION_SITES);
}


void InternalArrayConstructorStub::GenerateCase(
    MacroAssembler* masm, ElementsKind kind) {
  Label not_zero_case, not_one_case;
  Label normal_sequence;

  __ cmpi(r3, Operand::Zero());
  __ bne(&not_zero_case);
  InternalArrayNoArgumentConstructorStub stub0(kind);
  __ TailCallStub(&stub0);

  __ bind(&not_zero_case);
  __ cmpi(r3, Operand(1));
  __ bgt(&not_one_case);

  if (IsFastPackedElementsKind(kind)) {
    // We might need to create a holey array
    // look at the first argument
    __ LoadP(r6, MemOperand(sp, 0));
    __ cmpi(r6, Operand::Zero());
    __ beq(&normal_sequence);

    InternalArraySingleArgumentConstructorStub
        stub1_holey(GetHoleyElementsKind(kind));
    __ TailCallStub(&stub1_holey);
  }

  __ bind(&normal_sequence);
  InternalArraySingleArgumentConstructorStub stub1(kind);
  __ TailCallStub(&stub1);

  __ bind(&not_one_case);
  InternalArrayNArgumentsConstructorStub stubN(kind);
  __ TailCallStub(&stubN);
}


void InternalArrayConstructorStub::Generate(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- r3 : argc
  //  -- r4 : constructor
  //  -- sp[0] : return address
  //  -- sp[4] : last argument
  // -----------------------------------

  if (FLAG_debug_code) {
    // The array construct code is only set for the global and natives
    // builtin Array functions which always have maps.

    // Initial map for the builtin Array function should be a map.
    __ LoadP(r6, FieldMemOperand(r4, JSFunction::kPrototypeOrInitialMapOffset));
    // Will both indicate a NULL and a Smi.
    __ andi(r0, r6, Operand(kSmiTagMask));
    __ Assert(ne, kUnexpectedInitialMapForArrayFunction, cr0);
    __ CompareObjectType(r6, r6, r7, MAP_TYPE);
    __ Assert(eq, kUnexpectedInitialMapForArrayFunction);
  }

  // Figure out the right elements kind
  __ LoadP(r6, FieldMemOperand(r4, JSFunction::kPrototypeOrInitialMapOffset));
  // Load the map's "bit field 2" into |result|.
  __ lbz(r6, FieldMemOperand(r6, Map::kBitField2Offset));
  // Retrieve elements_kind from bit field 2.
  __ ExtractBitMask(r6, r6, Map::kElementsKindMask);

  if (FLAG_debug_code) {
    Label done;
    __ cmpi(r6, Operand(FAST_ELEMENTS));
    __ beq(&done);
    __ cmpi(r6, Operand(FAST_HOLEY_ELEMENTS));
    __ Assert(eq,
              kInvalidElementsKindForInternalArrayOrInternalPackedArray);
    __ bind(&done);
  }

  Label fast_elements_case;
  __ cmpi(r6, Operand(FAST_ELEMENTS));
  __ beq(&fast_elements_case);
  GenerateCase(masm, FAST_HOLEY_ELEMENTS);

  __ bind(&fast_elements_case);
  GenerateCase(masm, FAST_ELEMENTS);
}


#undef __

} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_PPC
