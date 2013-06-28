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
#include "debug.h"
#include "deoptimizer.h"
#include "full-codegen.h"
#include "runtime.h"

namespace v8 {
namespace internal {


#define __ ACCESS_MASM(masm)

#define EMIT_STUB_MARKER(stub_marker) __ marker_asm(stub_marker)

void Builtins::Generate_Adaptor(MacroAssembler* masm,
                                CFunctionId id,
                                BuiltinExtraArguments extra_args) {
  EMIT_STUB_MARKER(300);
  // ----------- S t a t e -------------
  //  -- r3                 : number of arguments excluding receiver
  //  -- r4                 : called function (only guaranteed when
  //                          extra_args requires it)
  //  -- cp                 : context
  //  -- sp[0]              : last argument
  //  -- ...
  //  -- sp[4 * (argc - 1)] : first argument (argc == r0)
  //  -- sp[4 * argc]       : receiver
  // -----------------------------------

  // Insert extra arguments.
  int num_extra_args = 0;
  if (extra_args == NEEDS_CALLED_FUNCTION) {
    num_extra_args = 1;
    __ push(r4);
  } else {
    ASSERT(extra_args == NO_EXTRA_ARGUMENTS);
  }

  // JumpToExternalReference expects r0 to contain the number of arguments
  // including the receiver and the extra arguments.
  __ addi(r3, r3, Operand(num_extra_args + 1));
  __ JumpToExternalReference(ExternalReference(id, masm->isolate()));
}


// Load the built-in InternalArray function from the current context.
static void GenerateLoadInternalArrayFunction(MacroAssembler* masm,
                                              Register result) {
  EMIT_STUB_MARKER(301);
  // Load the native context.

  __ lwz(result,
         MemOperand(cp, Context::SlotOffset(Context::GLOBAL_OBJECT_INDEX)));
  __ lwz(result,
         FieldMemOperand(result, GlobalObject::kNativeContextOffset));
  // Load the InternalArray function from the native context.
  __ lwz(result,
         MemOperand(result,
                    Context::SlotOffset(
                        Context::INTERNAL_ARRAY_FUNCTION_INDEX)));
}


// Load the built-in Array function from the current context.
static void GenerateLoadArrayFunction(MacroAssembler* masm, Register result) {
  EMIT_STUB_MARKER(302);
  // Load the native context.

  __ lwz(result,
         MemOperand(cp, Context::SlotOffset(Context::GLOBAL_OBJECT_INDEX)));
  __ lwz(result,
         FieldMemOperand(result, GlobalObject::kNativeContextOffset));
  // Load the Array function from the native context.
  __ lwz(result,
         MemOperand(result,
                    Context::SlotOffset(Context::ARRAY_FUNCTION_INDEX)));
}


// Allocate an empty JSArray. The allocated array is put into the result
// register. An elements backing store is allocated with size initial_capacity
// and filled with the hole values.
static void AllocateEmptyJSArray(MacroAssembler* masm,
                                 Register array_function,
                                 Register result,
                                 Register scratch1,
                                 Register scratch2,
                                 Register scratch3,
                                 Label* gc_required) {
  EMIT_STUB_MARKER(303);
  const int initial_capacity = JSArray::kPreallocatedArrayElements;
  STATIC_ASSERT(initial_capacity >= 0);
  __ LoadInitialArrayMap(array_function, scratch2, scratch1, false);

  // Allocate the JSArray object together with space for a fixed array with the
  // requested elements.
  int size = JSArray::kSize;
  if (initial_capacity > 0) {
    size += FixedArray::SizeFor(initial_capacity);
  }
  __ AllocateInNewSpace(size,
                        result,
                        scratch2,
                        scratch3,
                        gc_required,
                        TAG_OBJECT);

  // Allocated the JSArray. Now initialize the fields except for the elements
  // array.
  // result: JSObject
  // scratch1: initial map
  // scratch2: start of next object
  __ stw(scratch1, FieldMemOperand(result, JSObject::kMapOffset));
  __ LoadRoot(scratch1, Heap::kEmptyFixedArrayRootIndex);
  __ stw(scratch1, FieldMemOperand(result, JSArray::kPropertiesOffset));
  // Field JSArray::kElementsOffset is initialized later.
  __ li(scratch3,  Operand(0, RelocInfo::NONE));
  __ stw(scratch3, FieldMemOperand(result, JSArray::kLengthOffset));

  if (initial_capacity == 0) {
    __ stw(scratch1, FieldMemOperand(result, JSArray::kElementsOffset));
    return;
  }

  // Calculate the location of the elements array and set elements array member
  // of the JSArray.
  // result: JSObject
  // scratch2: start of next object
  __ addi(scratch1, result, Operand(JSArray::kSize));
  __ stw(scratch1, FieldMemOperand(result, JSArray::kElementsOffset));

  // Clear the heap tag on the elements array.
  __ sub(scratch1, scratch1, Operand(kHeapObjectTag));

  // Initialize the FixedArray and fill it with holes. FixedArray length is
  // stored as a smi.
  // result: JSObject
  // scratch1: elements array (untagged)
  // scratch2: start of next object
  __ LoadRoot(scratch3, Heap::kFixedArrayMapRootIndex);
  STATIC_ASSERT(0 * kPointerSize == FixedArray::kMapOffset);
  __ stw(scratch3, MemOperand(scratch1));
  __ addi(scratch1, scratch1, Operand(kPointerSize));
  __ li(scratch3,  Operand(Smi::FromInt(initial_capacity)));
  STATIC_ASSERT(1 * kPointerSize == FixedArray::kLengthOffset);
  __ stw(scratch3, MemOperand(scratch1));
  __ addi(scratch1, scratch1, Operand(kPointerSize));

  // Fill the FixedArray with the hole value. Inline the code if short.
  STATIC_ASSERT(2 * kPointerSize == FixedArray::kHeaderSize);
  __ LoadRoot(scratch3, Heap::kTheHoleValueRootIndex);
  static const int kLoopUnfoldLimit = 4;
  if (initial_capacity <= kLoopUnfoldLimit) {
    for (int i = 0; i < initial_capacity; i++) {
      __ stw(scratch3, MemOperand(scratch1));
      __ addi(scratch1, scratch1, Operand(kPointerSize));
    }
  } else {
    Label loop, entry;
    __ addi(scratch2, scratch1, Operand(initial_capacity * kPointerSize));
    __ b(&entry);
    __ bind(&loop);
    __ stw(scratch3, MemOperand(scratch1));
    __ addi(scratch1, scratch1, Operand(kPointerSize));
    __ bind(&entry);
    __ cmp(scratch1, scratch2);
    __ blt(&loop);
  }
}

// Allocate a JSArray with the number of elements stored in a register. The
// register array_function holds the built-in Array function and the register
// array_size holds the size of the array as a smi. The allocated array is put
// into the result register and beginning and end of the FixedArray elements
// storage is put into registers elements_array_storage and elements_array_end
// (see  below for when that is not the case). If the parameter fill_with_holes
// is true the allocated elements backing store is filled with the hole values
// otherwise it is left uninitialized. When the backing store is filled the
// register elements_array_storage is scratched.
static void AllocateJSArray(MacroAssembler* masm,
                            Register array_function,  // Array function.
                            Register array_size,  // As a smi, cannot be 0.
                            Register result,
                            Register elements_array_storage,
                            Register elements_array_end,
                            Register scratch1,
                            Register scratch2,
                            bool fill_with_hole,
                            Label* gc_required) {
  EMIT_STUB_MARKER(304);
  // Load the initial map from the array function.
  __ LoadInitialArrayMap(array_function, scratch2,
                         elements_array_storage, fill_with_hole);

  if (FLAG_debug_code) {  // Assert that array size is not zero.
    __ cmpi(array_size, Operand(0));
    __ Assert(ne, "array size is unexpectedly 0");
  }

  // Allocate the JSArray object together with space for a FixedArray with the
  // requested number of elements.
  STATIC_ASSERT(kSmiTagSize == 1 && kSmiTag == 0);
  __ li(elements_array_end,
         Operand((JSArray::kSize + FixedArray::kHeaderSize) / kPointerSize));
  __ srawi(scratch1, array_size, kSmiTagSize);
  __ add(elements_array_end, elements_array_end, scratch1);
  __ AllocateInNewSpace(
      elements_array_end,
      result,
      scratch1,
      scratch2,
      gc_required,
      static_cast<AllocationFlags>(TAG_OBJECT | SIZE_IN_WORDS));

  // Allocated the JSArray. Now initialize the fields except for the elements
  // array.
  // result: JSObject
  // elements_array_storage: initial map
  // array_size: size of array (smi)
  __ stw(elements_array_storage, FieldMemOperand(result, JSObject::kMapOffset));
  __ LoadRoot(elements_array_storage, Heap::kEmptyFixedArrayRootIndex);
  __ stw(elements_array_storage,
         FieldMemOperand(result, JSArray::kPropertiesOffset));
  // Field JSArray::kElementsOffset is initialized later.
  __ stw(array_size, FieldMemOperand(result, JSArray::kLengthOffset));

  // Calculate the location of the elements array and set elements array member
  // of the JSArray.
  // result: JSObject
  // array_size: size of array (smi)
  __ addi(elements_array_storage, result, Operand(JSArray::kSize));
  __ stw(elements_array_storage,
         FieldMemOperand(result, JSArray::kElementsOffset));

  // Clear the heap tag on the elements array.
  STATIC_ASSERT(kSmiTag == 0);
  __ sub(elements_array_storage,
         elements_array_storage,
         Operand(kHeapObjectTag));
  // Initialize the fixed array and fill it with holes. FixedArray length is
  // stored as a smi.
  // result: JSObject
  // elements_array_storage: elements array (untagged)
  // array_size: size of array (smi)
  __ LoadRoot(scratch1, Heap::kFixedArrayMapRootIndex);
  ASSERT_EQ(0 * kPointerSize, FixedArray::kMapOffset);
  __ stw(scratch1, MemOperand(elements_array_storage));
  __ addi(elements_array_storage, elements_array_storage,
          Operand(kPointerSize));
  STATIC_ASSERT(kSmiTag == 0);
  ASSERT_EQ(1 * kPointerSize, FixedArray::kLengthOffset);
  __ stw(array_size, MemOperand(elements_array_storage));
  __ addi(elements_array_storage, elements_array_storage,
          Operand(kPointerSize));

  // Calculate elements array and elements array end.
  // result: JSObject
  // elements_array_storage: elements array element storage
  // array_size: smi-tagged size of elements array
  STATIC_ASSERT(kSmiTag == 0 && kSmiTagSize < kPointerSizeLog2);
  __ slwi(scratch1, array_size, Operand(kPointerSizeLog2 - kSmiTagSize));
  __ add(elements_array_end, elements_array_storage, scratch1);

  // Fill the allocated FixedArray with the hole value if requested.
  // result: JSObject
  // elements_array_storage: elements array element storage
  // elements_array_end: start of next object
  if (fill_with_hole) {
    Label loop, entry;
    __ LoadRoot(scratch1, Heap::kTheHoleValueRootIndex);
    __ jmp(&entry);
    __ bind(&loop);
    __ stw(scratch1, MemOperand(elements_array_storage));
    __ addi(elements_array_storage, elements_array_storage,
           Operand(kPointerSize));
    __ bind(&entry);
    __ cmp(elements_array_storage, elements_array_end);
    __ blt(&loop);
  }
}

// Create a new array for the built-in Array function. This function allocates
// the JSArray object and the FixedArray elements array and initializes these.
// If the Array cannot be constructed in native code the runtime is called. This
// function assumes the following state:
//   r3: argc
//   r4: constructor (built-in Array function)
//   lr: return address
//   sp[0]: last argument
// This function is used for both construct and normal calls of Array. The only
// difference between handling a construct call and a normal call is that for a
// construct call the constructor function in r1 needs to be preserved for
// entering the generic code. In both cases argc in r0 needs to be preserved.
// Both registers are preserved by this code so no need to differentiate between
// construct call and normal call.
static void ArrayNativeCode(MacroAssembler* masm,
                            Label* call_generic_code) {
  EMIT_STUB_MARKER(305);
  Counters* counters = masm->isolate()->counters();
  Label argc_one_or_more, argc_two_or_more, not_empty_array, empty_array,
      has_non_smi_element, finish, cant_transition_map, not_double;

  // Check for array construction with zero arguments or one.
  __ cmpi(r3, Operand(0, RelocInfo::NONE));
  __ bne(&argc_one_or_more);

  // Handle construction of an empty array.
  __ bind(&empty_array);
  AllocateEmptyJSArray(masm,
                       r4,
                       r5,
                       r6,
                       r7,
                       r8,
                       call_generic_code);
  __ IncrementCounter(counters->array_function_native(), 1, r6, r7);
  // Set up return value, remove receiver from stack and return.
  __ mr(r3, r5);
  __ addi(sp, sp, Operand(kPointerSize));
  __ blr();

  // Check for one argument. Bail out if argument is not smi or if it is
  // negative.
  __ bind(&argc_one_or_more);
  __ cmpi(r3, Operand(1));
  __ bne(&argc_two_or_more);
  STATIC_ASSERT(kSmiTag == 0);
  __ lwz(r5, MemOperand(sp));  // Get the argument from the stack.
  __ cmpi(r5, Operand(0));
  __ bne(&not_empty_array);
  __ Drop(1);  // Adjust stack.
  __ li(r3, Operand(0));  // Treat this as a call with argc of zero.
  __ b(&empty_array);

  __ bind(&not_empty_array);
  // Posible optimization using rlwinm
  __ mov(r0, Operand(kIntptrSignBit | kSmiTagMask));
  __ and_(r6, r5, r0, SetRC);
  __ bne(call_generic_code, cr0);

  // Handle construction of an empty array of a certain size. Bail out if size
  // is too large to actually allocate an elements array.
  STATIC_ASSERT(kSmiTag == 0);
  __ mov(r0, Operand(JSObject::kInitialMaxFastElementArray << kSmiTagSize));
  __ cmp(r5, r0);
  __ bge(call_generic_code);

  // r3: argc
  // r4: constructor
  // r5: array_size (smi)
  // sp[0]: argument
  AllocateJSArray(masm,
                  r4,
                  r5,
                  r6,
                  r7,
                  r8,
                  r9,
                  r10,
                  true,
                  call_generic_code);
  __ IncrementCounter(counters->array_function_native(), 1, r5, r7);
  // Set up return value, remove receiver and argument from stack and return.
  __ mr(r3, r6);
  __ addi(sp, sp, Operand(2 * kPointerSize));
  __ blr();

  // Handle construction of an array from a list of arguments.
  __ bind(&argc_two_or_more);
  __ slwi(r5, r3, Operand(kSmiTagSize));  // Convet argc to a smi.

  // r3: argc
  // r4: constructor
  // r5: array_size (smi)
  // sp[0]: last argument
  AllocateJSArray(masm,
                  r4,
                  r5,
                  r6,
                  r7,
                  r8,
                  r9,
                  r10,
                  false,
                  call_generic_code);
  __ IncrementCounter(counters->array_function_native(), 1, r5, r9);

  // Fill arguments as array elements. Copy from the top of the stack (last
  // element) to the array backing store filling it backwards. Note:
  // elements_array_end points after the backing store therefore PreIndex is
  // used when filling the backing store.
  // r3: argc
  // r6: JSArray
  // r7: elements_array storage start (untagged)
  // r8: elements_array_end (untagged)
  // sp[0]: last argument
  Label loop, entry;
  __ mr(r10, sp);
  __ jmp(&entry);
  __ bind(&loop);
  __ lwz(r5, MemOperand(r10));
  __ addi(r10, r10, Operand(kPointerSize));
  if (FLAG_smi_only_arrays) {
    __ JumpIfNotSmi(r5, &has_non_smi_element);
  }
  __ stwu(r5, MemOperand(r8, -kPointerSize));
  __ bind(&entry);
  __ cmp(r7, r8);
  __ blt(&loop);

  __ bind(&finish);
  __ mr(sp, r10);

  // Remove caller arguments and receiver from the stack, setup return value and
  // return.
  // r3: argc
  // r6: JSArray
  // sp[0]: receiver
  __ addi(sp, sp, Operand(kPointerSize));
  __ mr(r3, r6);
  __ blr();

  __ bind(&has_non_smi_element);
  // Double values are handled by the runtime.
  __ CheckMap(
      r5, r22, Heap::kHeapNumberMapRootIndex, &not_double, DONT_DO_SMI_CHECK);
  __ bind(&cant_transition_map);
  __ UndoAllocationInNewSpace(r6, r7);
  __ b(call_generic_code);

  __ bind(&not_double);
  // Transition FAST_SMI_ELEMENTS to FAST_ELEMENTS.
  // r6: JSArray
  __ lwz(r5, FieldMemOperand(r6, HeapObject::kMapOffset));
  __ LoadTransitionedArrayMapConditional(FAST_SMI_ELEMENTS,
                                         FAST_ELEMENTS,
                                         r5,
                                         r22,
                                         &cant_transition_map);
  __ stw(r5, FieldMemOperand(r6, HeapObject::kMapOffset));
  __ RecordWriteField(r6,
                      HeapObject::kMapOffset,
                      r5,
                      r22,
                      kLRHasNotBeenSaved,
                      kDontSaveFPRegs,
                      EMIT_REMEMBERED_SET,
                      OMIT_SMI_CHECK);
  Label loop2;
  __ sub(r10, r10, Operand(kPointerSize));
  __ bind(&loop2);
  __ lwz(r5, MemOperand(r10));
  __ addi(r10, r10, Operand(kPointerSize));
  __ stwu(r5, MemOperand(r8, -kPointerSize));
  __ cmp(r7, r8);
  __ blt(&loop2);
  __ b(&finish);
}


void Builtins::Generate_InternalArrayCode(MacroAssembler* masm) {
  EMIT_STUB_MARKER(306);
  // ----------- S t a t e -------------
  //  -- r3     : number of arguments
  //  -- lr     : return address
  //  -- sp[...]: constructor arguments
  // -----------------------------------
  Label generic_array_code, one_or_more_arguments, two_or_more_arguments;

  // Get the InternalArray function.
  GenerateLoadInternalArrayFunction(masm, r4);

  if (FLAG_debug_code) {
    // Initial map for the builtin InternalArray functions should be maps.
    __ lwz(r5, FieldMemOperand(r4, JSFunction::kPrototypeOrInitialMapOffset));
    STATIC_ASSERT(kSmiTagMask < 0x8000);
    __ andi(r0, r5, Operand(kSmiTagMask));
    __ Assert(ne, "Unexpected initial map for InternalArray function", cr0);
    __ CompareObjectType(r5, r6, r7, MAP_TYPE);
    __ Assert(eq, "Unexpected initial map for InternalArray function");
  }

  // Run the native code for the InternalArray function called as a normal
  // function.
  ArrayNativeCode(masm, &generic_array_code);

  // Jump to the generic array code if the specialized code cannot handle the
  // construction.
  __ bind(&generic_array_code);

  Handle<Code> array_code =
      masm->isolate()->builtins()->InternalArrayCodeGeneric();
  __ Jump(array_code, RelocInfo::CODE_TARGET);
}


void Builtins::Generate_ArrayCode(MacroAssembler* masm) {
  EMIT_STUB_MARKER(307);
  // ----------- S t a t e -------------
  //  -- r3     : number of arguments
  //  -- lr     : return address
  //  -- sp[...]: constructor arguments
  // -----------------------------------
  Label generic_array_code, one_or_more_arguments, two_or_more_arguments;

  // Get the Array function.
  GenerateLoadArrayFunction(masm, r4);

  if (FLAG_debug_code) {
    // Initial map for the builtin Array functions should be maps.
    __ lwz(r5, FieldMemOperand(r4, JSFunction::kPrototypeOrInitialMapOffset));
    STATIC_ASSERT(kSmiTagMask < 0x8000);
    __ andi(r0, r5, Operand(kSmiTagMask));
    __ Assert(ne, "Unexpected initial map for Array function", cr0);
    __ CompareObjectType(r5, r6, r7, MAP_TYPE);
    __ Assert(eq, "Unexpected initial map for Array function");
  }

  // Run the native code for the Array function called as a normal function.
  ArrayNativeCode(masm, &generic_array_code);

  // Jump to the generic array code if the specialized code cannot handle
  // the construction.
  __ bind(&generic_array_code);

  Handle<Code> array_code =
      masm->isolate()->builtins()->ArrayCodeGeneric();
  __ Jump(array_code, RelocInfo::CODE_TARGET);
}


void Builtins::Generate_ArrayConstructCode(MacroAssembler* masm) {
  EMIT_STUB_MARKER(308);
  // ----------- S t a t e -------------
  //  -- r3     : number of arguments
  //  -- r4     : constructor function
  //  -- lr     : return address
  //  -- sp[...]: constructor arguments
  // -----------------------------------
  Label generic_constructor;

  if (FLAG_debug_code) {
    // The array construct code is only set for the builtin and internal
    // Array functions which always have a map.
    // Initial map for the builtin Array function should be a map.
    __ lwz(r5, FieldMemOperand(r4, JSFunction::kPrototypeOrInitialMapOffset));
    __ andi(r0, r5, Operand(kSmiTagMask));
    __ Assert(ne, "Unexpected initial map for Array function", cr0);
    __ CompareObjectType(r5, r6, r7, MAP_TYPE);
    __ Assert(eq, "Unexpected initial map for Array function");
  }

  // Run the native code for the Array function called as a constructor.
  ArrayNativeCode(masm, &generic_constructor);

  // Jump to the generic construct code in case the specialized code cannot
  // handle the construction.
  __ bind(&generic_constructor);
  Handle<Code> generic_construct_stub =
      masm->isolate()->builtins()->JSConstructStubGeneric();
  __ Jump(generic_construct_stub, RelocInfo::CODE_TARGET);
}


void Builtins::Generate_StringConstructCode(MacroAssembler* masm) {
  EMIT_STUB_MARKER(309);
  // ----------- S t a t e -------------
  //  -- r3                     : number of arguments
  //  -- r4                     : constructor function
  //  -- lr                     : return address
  //  -- sp[(argc - n - 1) * 4] : arg[n] (zero based)
  //  -- sp[argc * 4]           : receiver
  // -----------------------------------
  Counters* counters = masm->isolate()->counters();
  __ IncrementCounter(counters->string_ctor_calls(), 1, r5, r6);

  Register function = r4;
  if (FLAG_debug_code) {
    __ LoadGlobalFunction(Context::STRING_FUNCTION_INDEX, r5);
    __ cmp(function, r5);
    __ Assert(eq, "Unexpected String function");
  }

  // Load the first arguments in r3 and get rid of the rest.
  Label no_arguments;
  __ cmpi(r3, Operand(0, RelocInfo::NONE));
  __ beq(&no_arguments);
  // First args = sp[(argc - 1) * 4].
  __ sub(r3, r3, Operand(1));
  __ slwi(r3, r3, Operand(kPointerSizeLog2));
  __ add(sp, sp, r3);
  __ lwz(r3, MemOperand(sp));
  // sp now point to args[0], drop args[0] + receiver.
  __ Drop(2);

  Register argument = r5;
  Label not_cached, argument_is_string;
  NumberToStringStub::GenerateLookupNumberStringCache(
      masm,
      r3,        // Input.
      argument,  // Result.
      r6,        // Scratch.
      r7,        // Scratch.
      r8,        // Scratch.
      false,     // Is it a Smi?
      &not_cached);
  __ IncrementCounter(counters->string_ctor_cached_number(), 1, r6, r7);
  __ bind(&argument_is_string);

  // ----------- S t a t e -------------
  //  -- r5     : argument converted to string
  //  -- r4     : constructor function
  //  -- lr     : return address
  // -----------------------------------

  Label gc_required;
  __ AllocateInNewSpace(JSValue::kSize,
                        r3,  // Result.
                        r6,  // Scratch.
                        r7,  // Scratch.
                        &gc_required,
                        TAG_OBJECT);

  // Initialising the String Object.
  Register map = r6;
  __ LoadGlobalFunctionInitialMap(function, map, r7);
  if (FLAG_debug_code) {
    __ lbz(r7, FieldMemOperand(map, Map::kInstanceSizeOffset));
    __ cmpi(r7, Operand(JSValue::kSize >> kPointerSizeLog2));
    __ Assert(eq, "Unexpected string wrapper instance size");
    __ lbz(r7, FieldMemOperand(map, Map::kUnusedPropertyFieldsOffset));
    __ cmpi(r7, Operand(0, RelocInfo::NONE));
    __ Assert(eq, "Unexpected unused properties of string wrapper");
  }
  __ stw(map, FieldMemOperand(r3, HeapObject::kMapOffset));

  __ LoadRoot(r6, Heap::kEmptyFixedArrayRootIndex);
  __ stw(r6, FieldMemOperand(r3, JSObject::kPropertiesOffset));
  __ stw(r6, FieldMemOperand(r3, JSObject::kElementsOffset));

  __ stw(argument, FieldMemOperand(r3, JSValue::kValueOffset));

  // Ensure the object is fully initialized.
  STATIC_ASSERT(JSValue::kSize == 4 * kPointerSize);

  __ Ret();

  // The argument was not found in the number to string cache. Check
  // if it's a string already before calling the conversion builtin.
  Label convert_argument;
  __ bind(&not_cached);
  __ JumpIfSmi(r3, &convert_argument);

  // Is it a String?
  __ lwz(r5, FieldMemOperand(r3, HeapObject::kMapOffset));
  __ lbz(r6, FieldMemOperand(r5, Map::kInstanceTypeOffset));
  STATIC_ASSERT(kNotStringTag != 0);
  __ andi(r0, r6, Operand(kIsNotStringMask));
  __ bne(&convert_argument, cr0);
  __ mr(argument, r3);
  __ IncrementCounter(counters->string_ctor_conversions(), 1, r6, r7);
  __ b(&argument_is_string);

  // Invoke the conversion builtin and put the result into r5.
  __ bind(&convert_argument);
  __ push(function);  // Preserve the function.
  __ IncrementCounter(counters->string_ctor_conversions(), 1, r6, r7);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ push(r3);
    __ InvokeBuiltin(Builtins::TO_STRING, CALL_FUNCTION);
  }
  __ pop(function);
  __ mr(argument, r3);
  __ b(&argument_is_string);

  // Load the empty string into r5, remove the receiver from the
  // stack, and jump back to the case where the argument is a string.
  __ bind(&no_arguments);
  __ LoadRoot(argument, Heap::kEmptyStringRootIndex);
  __ Drop(1);
  __ b(&argument_is_string);

  // At this point the argument is already a string. Call runtime to
  // create a string wrapper.
  __ bind(&gc_required);
  __ IncrementCounter(counters->string_ctor_gc_required(), 1, r6, r7);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ push(argument);
    __ CallRuntime(Runtime::kNewStringWrapper, 1);
  }
  __ Ret();
}


static void GenerateTailCallToSharedCode(MacroAssembler* masm) {
  EMIT_STUB_MARKER(310);
  __ lwz(r5, FieldMemOperand(r4, JSFunction::kSharedFunctionInfoOffset));
  __ lwz(r5, FieldMemOperand(r5, SharedFunctionInfo::kCodeOffset));
  __ addi(r5, r5, Operand(Code::kHeaderSize - kHeapObjectTag));
  __ mtctr(r5);
  __ bcr();
}


void Builtins::Generate_InRecompileQueue(MacroAssembler* masm) {
  GenerateTailCallToSharedCode(masm);
}


void Builtins::Generate_ParallelRecompile(MacroAssembler* masm) {
  EMIT_STUB_MARKER(311);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);

    // Push a copy of the function onto the stack.
    __ push(r4);
    // Push call kind information.
    __ push(r8);

    __ push(r4);  // Function is also the parameter to the runtime call.
    __ CallRuntime(Runtime::kParallelRecompile, 1);

    // Restore call kind information.
    __ pop(r8);
    // Restore receiver.
    __ pop(r4);

    // Tear down internal frame.
  }

  GenerateTailCallToSharedCode(masm);
}


static void Generate_JSConstructStubHelper(MacroAssembler* masm,
                                           bool is_api_function,
                                           bool count_constructions) {
  EMIT_STUB_MARKER(312);
  // ----------- S t a t e -------------
  //  -- r3     : number of arguments
  //  -- r4     : constructor function
  //  -- lr     : return address
  //  -- sp[...]: constructor arguments
  // -----------------------------------

  // Should never count constructions for api objects.
  ASSERT(!is_api_function || !count_constructions);

  Isolate* isolate = masm->isolate();

  // Enter a construct frame.
  {
    FrameScope scope(masm, StackFrame::CONSTRUCT);

    // Preserve the two incoming parameters on the stack.
    __ slwi(r3, r3, Operand(kSmiTagSize));
    __ push(r3);  // Smi-tagged arguments count.
    __ push(r4);  // Constructor function.

    // Try to allocate the object without transitioning into C code. If any of
    // the preconditions is not met, the code bails out to the runtime call.
    Label rt_call, allocated;
    if (FLAG_inline_new) {
      Label undo_allocation;
#ifdef ENABLE_DEBUGGER_SUPPORT
      ExternalReference debug_step_in_fp =
          ExternalReference::debug_step_in_fp_address(isolate);
      __ mov(r5, Operand(debug_step_in_fp));
      __ lwz(r5, MemOperand(r5));
      __ cmpi(r5, Operand(0));
      __ bne(&rt_call);
#endif

      // Load the initial map and verify that it is in fact a map.
      // r4: constructor function
      __ lwz(r5, FieldMemOperand(r4, JSFunction::kPrototypeOrInitialMapOffset));
      __ JumpIfSmi(r5, &rt_call);
      __ CompareObjectType(r5, r6, r7, MAP_TYPE);
      __ bne(&rt_call);

      // Check that the constructor is not constructing a JSFunction (see
      // comments in Runtime_NewObject in runtime.cc). In which case the
      // initial map's instance type would be JS_FUNCTION_TYPE.
      // r4: constructor function
      // r5: initial map
      __ CompareInstanceType(r5, r6, JS_FUNCTION_TYPE);
      __ beq(&rt_call);

      if (count_constructions) {
        Label allocate;
        // Decrease generous allocation count.
        __ lwz(r6, FieldMemOperand(r4, JSFunction::kSharedFunctionInfoOffset));
        MemOperand constructor_count =
            FieldMemOperand(r6, SharedFunctionInfo::kConstructionCountOffset);
        __ lbz(r7, constructor_count);
        __ addic(r7, r7, Operand(-1));
        __ stb(r7, constructor_count);
        __ cmpi(r7, Operand(0));
        __ bne(&allocate);

        __ push(r4);
        __ push(r5);

        __ push(r4);  // constructor
        // The call will replace the stub, so the countdown is only done once.
        __ CallRuntime(Runtime::kFinalizeInstanceSize, 1);

        __ pop(r5);
        __ pop(r4);

        __ bind(&allocate);
      }

      // Now allocate the JSObject on the heap.
      // r4: constructor function
      // r5: initial map
      __ lbz(r6, FieldMemOperand(r5, Map::kInstanceSizeOffset));
      __ AllocateInNewSpace(r6, r7, r8, r9, &rt_call, SIZE_IN_WORDS);

      // Allocated the JSObject, now initialize the fields. Map is set to
      // initial map and properties and elements are set to empty fixed array.
      // r4: constructor function
      // r5: initial map
      // r6: object size
      // r7: JSObject (not tagged)
      __ LoadRoot(r9, Heap::kEmptyFixedArrayRootIndex);
      __ mr(r8, r7);
      ASSERT_EQ(0 * kPointerSize, JSObject::kMapOffset);
      __ stw(r5, MemOperand(r8));
      __ addi(r8, r8, Operand(kPointerSize));
      ASSERT_EQ(1 * kPointerSize, JSObject::kPropertiesOffset);
      __ stw(r9, MemOperand(r8));
      __ addi(r8, r8, Operand(kPointerSize));
      ASSERT_EQ(2 * kPointerSize, JSObject::kElementsOffset);
      __ stw(r9, MemOperand(r8));
      __ addi(r8, r8, Operand(kPointerSize));

      // Fill all the in-object properties with the appropriate filler.
      // r4: constructor function
      // r5: initial map
      // r6: object size (in words)
      // r7: JSObject (not tagged)
      // r8: First in-object property of JSObject (not tagged)
      __ slwi(r9, r6, Operand(kPointerSizeLog2));
      __ add(r9, r7, r9);  // End of object.
      ASSERT_EQ(3 * kPointerSize, JSObject::kHeaderSize);
      __ LoadRoot(r10, Heap::kUndefinedValueRootIndex);
      if (count_constructions) {
        __ lwz(r3, FieldMemOperand(r5, Map::kInstanceSizesOffset));
        // Fetch Map::kPreAllocatedPropertyFieldsByte field from r3
        // and multiply by kPointerSizeLog2
        STATIC_ASSERT(2 == Map::kPreAllocatedPropertyFieldsByte);
        STATIC_ASSERT(kPointerSizeLog2 == 2);
#if defined(V8_HOST_ARCH_PPC)  // ENDIAN
        __ rlwinm(r3, r3, 26, 22, 29, LeaveRC);
#else
        __ rlwinm(r3, r3, 18, 22, 29, LeaveRC);
#endif
        __ add(r3, r8, r3);
        // r3: offset of first field after pre-allocated fields
        if (FLAG_debug_code) {
          __ cmp(r3, r9);
          __ Assert(le, "Unexpected number of pre-allocated property fields.");
        }
        __ InitializeFieldsWithFiller(r8, r3, r10);
        // To allow for truncation.
        __ LoadRoot(r10, Heap::kOnePointerFillerMapRootIndex);
      }
      __ InitializeFieldsWithFiller(r8, r9, r10);

      // Add the object tag to make the JSObject real, so that we can continue
      // and jump into the continuation code at any time from now on. Any
      // failures need to undo the allocation, so that the heap is in a
      // consistent state and verifiable.
      __ addi(r7, r7, Operand(kHeapObjectTag));

      // Check if a non-empty properties array is needed. Continue with
      // allocated object if not fall through to runtime call if it is.
      // r4: constructor function
      // r7: JSObject
      // r8: start of next object (not tagged)
      __ lbz(r6, FieldMemOperand(r5, Map::kUnusedPropertyFieldsOffset));
      // The field instance sizes contains both pre-allocated property fields
      // and in-object properties.
      __ lwz(r3, FieldMemOperand(r5, Map::kInstanceSizesOffset));
      // Fetch Map::kPreAllocatedPropertyFieldsByte field from r3
      STATIC_ASSERT(2 == Map::kPreAllocatedPropertyFieldsByte);
#if defined(V8_HOST_ARCH_PPC)  // ENDIAN
      __ rlwinm(r9, r3, 24, 24, 31, LeaveRC);
#else
      __ rlwinm(r9, r3, 16, 24, 31, LeaveRC);
#endif
      __ add(r6, r6, r9);
      STATIC_ASSERT(1 == Map::kInObjectPropertiesByte);
#if defined(V8_HOST_ARCH_PPC)  // ENDIAN
      __ rlwinm(r9, r3, 16, 24, 31, LeaveRC);
#else
      __ rlwinm(r9, r3, 24, 24, 31, LeaveRC);
#endif
      __ sub(r6, r6, r9);  // roohack - sub order may be incorrect
      __ cmpi(r6, Operand(0));

      // Done if no extra properties are to be allocated.
      __ beq(&allocated);
      __ Assert(ge, "Property allocation count failed.");

      // Scale the number of elements by pointer size and add the header for
      // FixedArrays to the start of the next object calculation from above.
      // r4: constructor
      // r6: number of elements in properties array
      // r7: JSObject
      // r8: start of next object
      __ addi(r3, r6, Operand(FixedArray::kHeaderSize / kPointerSize));
      __ AllocateInNewSpace(
          r3,
          r8,
          r9,
          r5,
          &undo_allocation,
          static_cast<AllocationFlags>(RESULT_CONTAINS_TOP | SIZE_IN_WORDS));

      // Initialize the FixedArray.
      // r4: constructor
      // r6: number of elements in properties array
      // r7: JSObject
      // r8: FixedArray (not tagged)
      __ LoadRoot(r9, Heap::kFixedArrayMapRootIndex);
      __ mr(r5, r8);
      ASSERT_EQ(0 * kPointerSize, JSObject::kMapOffset);
      __ stw(r9, MemOperand(r5));
      __ addi(r5, r5, Operand(kPointerSize));
      ASSERT_EQ(1 * kPointerSize, FixedArray::kLengthOffset);
      __ slwi(r3, r6, Operand(kSmiTagSize));
      __ stw(r3, MemOperand(r5));
      __ addi(r5, r5, Operand(kPointerSize));

      // Initialize the fields to undefined.
      // r4: constructor function
      // r5: First element of FixedArray (not tagged)
      // r6: number of elements in properties array
      // r7: JSObject
      // r8: FixedArray (not tagged)
      __ slwi(r9, r6, Operand(kPointerSizeLog2));
      __ add(r9, r5, r9);  // End of object.
      ASSERT_EQ(2 * kPointerSize, FixedArray::kHeaderSize);
      { Label loop, entry;
        if (count_constructions) {
          __ LoadRoot(r10, Heap::kUndefinedValueRootIndex);
        } else if (FLAG_debug_code) {
          __ LoadRoot(r11, Heap::kUndefinedValueRootIndex);
          __ cmp(r10, r11);
          __ Assert(eq, "Undefined value not loaded.");
        }
        __ b(&entry);
        __ bind(&loop);
        __ stw(r10, MemOperand(r5));
        __ addi(r5, r5, Operand(kPointerSize));
        __ bind(&entry);
        __ cmp(r5, r9);
        __ blt(&loop);
      }

      // Store the initialized FixedArray into the properties field of
      // the JSObject
      // r4: constructor function
      // r7: JSObject
      // r8: FixedArray (not tagged)
      __ addi(r8, r8, Operand(kHeapObjectTag));  // Add the heap tag.
      __ stw(r8, FieldMemOperand(r7, JSObject::kPropertiesOffset));

      // Continue with JSObject being successfully allocated
      // r4: constructor function
      // r7: JSObject
      __ jmp(&allocated);

      // Undo the setting of the new top so that the heap is verifiable. For
      // example, the map's unused properties potentially do not match the
      // allocated objects unused properties.
      // r7: JSObject (previous new top)
      __ bind(&undo_allocation);
      __ UndoAllocationInNewSpace(r7, r8);
    }

    // Allocate the new receiver object using the runtime call.
    // r4: constructor function
    __ bind(&rt_call);
    __ push(r4);  // argument for Runtime_NewObject
    __ CallRuntime(Runtime::kNewObject, 1);
    __ mr(r7, r3);

    // Receiver for constructor call allocated.
    // r7: JSObject
    __ bind(&allocated);
    __ push(r7);
    __ push(r7);

    // Reload the number of arguments and the constructor from the stack.
    // sp[0]: receiver
    // sp[1]: receiver
    // sp[2]: constructor function
    // sp[3]: number of arguments (smi-tagged)
    __ lwz(r4, MemOperand(sp, 2 * kPointerSize));
    __ lwz(r6, MemOperand(sp, 3 * kPointerSize));

    // Set up pointer to last argument.
    __ addi(r5, fp, Operand(StandardFrameConstants::kCallerSPOffset));

    // Set up number of arguments for function call below
    __ srwi(r3, r6, Operand(kSmiTagSize));

    // Copy arguments and receiver to the expression stack.
    // r3: number of arguments
    // r4: constructor function
    // r5: address of last argument (caller sp)
    // r6: number of arguments (smi-tagged)
    // sp[0]: receiver
    // sp[1]: receiver
    // sp[2]: constructor function
    // sp[3]: number of arguments (smi-tagged)
    Label loop, entry;
    __ b(&entry);
    __ bind(&loop);

    __ slwi(ip, r6, Operand(kPointerSizeLog2 - 1));
    __ add(ip, r5, ip);
    __ lwz(ip, MemOperand(ip));
// was   __ lwz(ip, MemOperand(r5, r6, LSL, kPointerSizeLog2 - 1));

    __ push(ip);
    __ bind(&entry);
    __ sub(r6, r6, Operand(2));
    __ cmpi(r6, Operand(0));
    __ bge(&loop);

    // Call the function.
    // r3: number of arguments
    // r4: constructor function
    if (is_api_function) {
      __ lwz(cp, FieldMemOperand(r4, JSFunction::kContextOffset));
      Handle<Code> code =
          masm->isolate()->builtins()->HandleApiCallConstruct();
      ParameterCount expected(0);
      __ InvokeCode(code, expected, expected,
                    RelocInfo::CODE_TARGET, CALL_FUNCTION, CALL_AS_METHOD);
    } else {
      ParameterCount actual(r3);
      __ InvokeFunction(r4, actual, CALL_FUNCTION,  // roohack
                        NullCallWrapper(), CALL_AS_METHOD);
    }

    // Store offset of return address for deoptimizer.
    if (!is_api_function && !count_constructions) {
      masm->isolate()->heap()->SetConstructStubDeoptPCOffset(masm->pc_offset());
    }

    // Restore context from the frame.
    // r3: result
    // sp[0]: receiver
    // sp[1]: constructor function
    // sp[2]: number of arguments (smi-tagged)
    __ lwz(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));

    // If the result is an object (in the ECMA sense), we should get rid
    // of the receiver and use the result; see ECMA-262 section 13.2.2-7
    // on page 74.
    Label use_receiver, exit;

    // If the result is a smi, it is *not* an object in the ECMA sense.
    // r3: result
    // sp[0]: receiver (newly allocated object)
    // sp[1]: constructor function
    // sp[2]: number of arguments (smi-tagged)
    __ JumpIfSmi(r3, &use_receiver);

    // If the type of the result (stored in its map) is less than
    // FIRST_SPEC_OBJECT_TYPE, it is not an object in the ECMA sense.
    __ CompareObjectType(r3, r6, r6, FIRST_SPEC_OBJECT_TYPE);
    __ bge(&exit);

    // Throw away the result of the constructor invocation and use the
    // on-stack receiver as the result.
    __ bind(&use_receiver);
    __ lwz(r3, MemOperand(sp));

    // Remove receiver from the stack, remove caller arguments, and
    // return.
    __ bind(&exit);
    // r3: result
    // sp[0]: receiver (newly allocated object)
    // sp[1]: constructor function
    // sp[2]: number of arguments (smi-tagged)
    __ lwz(r4, MemOperand(sp, 2 * kPointerSize));

    // Leave construct frame.
  }

  __ slwi(r4, r4, Operand(kPointerSizeLog2 - 1));
  __ add(sp, sp, r4);
  __ addi(sp, sp, Operand(kPointerSize));
  __ IncrementCounter(isolate->counters()->constructed_objects(), 1, r4, r5);
  __ blr();
}


void Builtins::Generate_JSConstructStubCountdown(MacroAssembler* masm) {
  Generate_JSConstructStubHelper(masm, false, true);
}


void Builtins::Generate_JSConstructStubGeneric(MacroAssembler* masm) {
  Generate_JSConstructStubHelper(masm, false, false);
}


void Builtins::Generate_JSConstructStubApi(MacroAssembler* masm) {
  Generate_JSConstructStubHelper(masm, true, false);
}


static void Generate_JSEntryTrampolineHelper(MacroAssembler* masm,
                                             bool is_construct) {
  EMIT_STUB_MARKER(313);
  // Called from Generate_JS_Entry
  // r3: code entry
  // r4: function
  // r5: receiver
  // r6: argc
  // r7: argv
  // r0,r8-r9, cp may be clobbered

  // Clear the context before we push it when entering the internal frame.
  __ li(cp, Operand(0, RelocInfo::NONE));

  // Enter an internal frame.
  {
    FrameScope scope(masm, StackFrame::INTERNAL);

    // Set up the context from the function argument.
    __ lwz(cp, FieldMemOperand(r4, JSFunction::kContextOffset));

    __ InitializeRootRegister();

    // Push the function and the receiver onto the stack.
    __ push(r4);
    __ push(r5);

    // Copy arguments to the stack in a loop.
    // r4: function
    // r6: argc
    // r7: argv, i.e. points to first arg
    Label loop, entry;
    __ slwi(r0, r6, Operand(kPointerSizeLog2));
    __ add(r5, r7, r0);
    // r5 points past last arg.
    __ b(&entry);
    __ bind(&loop);
    __ lwz(r8, MemOperand(r7));  // read next parameter
    __ addi(r7, r7, Operand(kPointerSize));
    __ lwz(r0, MemOperand(r8));  // dereference handle
    __ push(r0);  // push parameter
    __ bind(&entry);
    __ cmp(r7, r5);
    __ bne(&loop);

    // Initialize all JavaScript callee-saved registers, since they will be seen
    // by the garbage collector as part of handlers.
    __ LoadRoot(r7, Heap::kUndefinedValueRootIndex);
    __ mr(r14, r7);
    __ mr(r15, r7);
    __ mr(r16, r7);
    __ mr(r22, r7);  // hmmm, possibly should be reassigned to r17

    // Invoke the code and pass argc as r3.
    __ mr(r3, r6);
    if (is_construct) {
      CallConstructStub stub(NO_CALL_FUNCTION_FLAGS);
      __ CallStub(&stub);
    } else {
      ParameterCount actual(r3);
      __ InvokeFunction(r4, actual, CALL_FUNCTION,
                        NullCallWrapper(), CALL_AS_METHOD);
    }
    // Exit the JS frame and remove the parameters (except function), and
    // return.
    // Respect ABI stack constraint. (ARM?)
  }
  __ blr();

  // r3: result
}


void Builtins::Generate_JSEntryTrampoline(MacroAssembler* masm) {
  Generate_JSEntryTrampolineHelper(masm, false);
}


void Builtins::Generate_JSConstructEntryTrampoline(MacroAssembler* masm) {
  Generate_JSEntryTrampolineHelper(masm, true);
}


void Builtins::Generate_LazyCompile(MacroAssembler* masm) {
  EMIT_STUB_MARKER(314);
  // Enter an internal frame.
  {
    FrameScope scope(masm, StackFrame::INTERNAL);

    // Preserve the function.
    __ push(r4);
    // Push call kind information.
    __ push(r8);

    // Push the function on the stack as the argument to the runtime function.
    __ push(r4);
    __ CallRuntime(Runtime::kLazyCompile, 1);
    // Calculate the entry point.
    __ addi(r5, r3, Operand(Code::kHeaderSize - kHeapObjectTag));

    // Restore call kind information.
    __ pop(r8);
    // Restore saved function.
    __ pop(r4);

    // Tear down internal frame.
  }

  // Do a tail-call of the compiled function.
  __ Jump(r5);
}


void Builtins::Generate_LazyRecompile(MacroAssembler* masm) {
  EMIT_STUB_MARKER(315);
  // Enter an internal frame.
  {
    FrameScope scope(masm, StackFrame::INTERNAL);

    // Preserve the function.
    __ push(r4);
    // Push call kind information.
    __ push(r8);

    // Push the function on the stack as the argument to the runtime function.
    __ push(r4);
    __ CallRuntime(Runtime::kLazyRecompile, 1);
    // Calculate the entry point.
    __ addi(r5, r3, Operand(Code::kHeaderSize - kHeapObjectTag));

    // Restore call kind information.
    __ pop(r8);
    // Restore saved function.
    __ pop(r4);

    // Tear down internal frame.
  }

  // Do a tail-call of the compiled function.
  __ Jump(r5);
}


static void Generate_NotifyDeoptimizedHelper(MacroAssembler* masm,
                                             Deoptimizer::BailoutType type) {
  EMIT_STUB_MARKER(316);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    // Pass the function and deoptimization type to the runtime system.
    __ li(r3, Operand(Smi::FromInt(static_cast<int>(type))));
    __ push(r3);
    __ CallRuntime(Runtime::kNotifyDeoptimized, 1);
  }

  // Get the full codegen state from the stack and untag it -> r9.
  __ lwz(r9, MemOperand(sp, 0 * kPointerSize));
  __ SmiUntag(r9);
  // Switch on the state.
  Label with_tos_register, unknown_state;
  __ cmpi(r9, Operand(FullCodeGenerator::NO_REGISTERS));
  __ bne(&with_tos_register);
  __ addi(sp, sp, Operand(1 * kPointerSize));  // Remove state.
  __ Ret();

  __ bind(&with_tos_register);
  __ lwz(r3, MemOperand(sp, 1 * kPointerSize));
  __ cmpi(r9, Operand(FullCodeGenerator::TOS_REG));
  __ bne(&unknown_state);
  __ addi(sp, sp, Operand(2 * kPointerSize));  // Remove state.
  __ Ret();

  __ bind(&unknown_state);
  __ stop("no cases left");
}


void Builtins::Generate_NotifyDeoptimized(MacroAssembler* masm) {
  Generate_NotifyDeoptimizedHelper(masm, Deoptimizer::EAGER);
}


void Builtins::Generate_NotifyLazyDeoptimized(MacroAssembler* masm) {
  Generate_NotifyDeoptimizedHelper(masm, Deoptimizer::LAZY);
}


void Builtins::Generate_NotifyOSR(MacroAssembler* masm) {
  EMIT_STUB_MARKER(317);
  // For now, we are relying on the fact that Runtime::NotifyOSR
  // doesn't do any garbage collection which allows us to save/restore
  // the registers without worrying about which of them contain
  // pointers. This seems a bit fragile.
  __ mflr(r0);
  RegList saved_regs =
      (kJSCallerSaved | kCalleeSaved | r0.bit() | fp.bit()) & ~sp.bit();
  __ MultiPush(saved_regs);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ CallRuntime(Runtime::kNotifyOSR, 0);
  }
  __ MultiPop(saved_regs);
  __ mtlr(r0);
  __ Ret();
}


void Builtins::Generate_OnStackReplacement(MacroAssembler* masm) {
  EMIT_STUB_MARKER(318);
  CpuFeatures::TryForceFeatureScope scope(VFP3);
  if (!CPU::SupportsCrankshaft()) {
    __ Abort("Unreachable code: Cannot optimize without VFP3 support.");
    return;
  }

  // Lookup the function in the JavaScript frame and push it as an
  // argument to the on-stack replacement function.
  __ lwz(r3, MemOperand(fp, JavaScriptFrameConstants::kFunctionOffset));
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ push(r3);
    __ CallRuntime(Runtime::kCompileForOnStackReplacement, 1);
  }

  // If the result was -1 it means that we couldn't optimize the
  // function. Just return and continue in the unoptimized version.
  Label skip;
  __ cmpi(r3, Operand(Smi::FromInt(-1)));
  __ bne(&skip);
  __ Ret();

  __ bind(&skip);
  // Untag the AST id and push it on the stack.
  __ SmiUntag(r3);
  __ push(r3);

  // Generate the code for doing the frame-to-frame translation using
  // the deoptimizer infrastructure.
  Deoptimizer::EntryGenerator generator(masm, Deoptimizer::OSR);
  generator.Generate();
}


void Builtins::Generate_FunctionCall(MacroAssembler* masm) {
  EMIT_STUB_MARKER(319);
  // 1. Make sure we have at least one argument.
  // r3: actual number of arguments
  { Label done;
    __ cmpi(r3, Operand(0));
    __ bne(&done);
    __ LoadRoot(r5, Heap::kUndefinedValueRootIndex);
    __ push(r5);
    __ addi(r3, r3, Operand(1));
    __ bind(&done);
  }

  // 2. Get the function to call (passed as receiver) from the stack, check
  //    if it is a function.
  // r3: actual number of arguments
  Label slow, non_function;
  __ slwi(r4, r3, Operand(kPointerSizeLog2));
  __ add(r4, sp, r4);
  __ lwz(r4, MemOperand(r4));
  __ JumpIfSmi(r4, &non_function);
  __ CompareObjectType(r4, r5, r5, JS_FUNCTION_TYPE);
  __ bne(&slow);

  // 3a. Patch the first argument if necessary when calling a function.
  // r3: actual number of arguments
  // r4: function
  Label shift_arguments;
  __ li(r7, Operand(0, RelocInfo::NONE));  // indicate regular JS_FUNCTION
  { Label convert_to_object, use_global_receiver, patch_receiver;
    // Change context eagerly in case we need the global receiver.
    __ lwz(cp, FieldMemOperand(r4, JSFunction::kContextOffset));

    // Do not transform the receiver for strict mode functions.
    __ lwz(r5, FieldMemOperand(r4, JSFunction::kSharedFunctionInfoOffset));
    __ lwz(r6, FieldMemOperand(r5, SharedFunctionInfo::kCompilerHintsOffset));
    __ andi(r0, r6, Operand(1 << (SharedFunctionInfo::kStrictModeFunction +
                             kSmiTagSize)));
    __ bne(&shift_arguments, cr0);

    // Do not transform the receiver for native (Compilerhints already in r6).
    STATIC_ASSERT((1 << (SharedFunctionInfo::kNative + kSmiTagSize)) < 0x8000);
    __ andi(r0, r6, Operand(1 << (SharedFunctionInfo::kNative + kSmiTagSize)));
    __ bne(&shift_arguments, cr0);

    // Compute the receiver in non-strict mode.
    __ slwi(ip, r3, Operand(kPointerSizeLog2));
    __ add(r5, sp, ip);
    __ lwz(r5, MemOperand(r5, -kPointerSize));
    // r3: actual number of arguments
    // r4: function
    // r5: first argument
    __ JumpIfSmi(r5, &convert_to_object);

    __ LoadRoot(r6, Heap::kUndefinedValueRootIndex);
    __ cmp(r5, r6);
    __ beq(&use_global_receiver);
    __ LoadRoot(r6, Heap::kNullValueRootIndex);
    __ cmp(r5, r6);
    __ beq(&use_global_receiver);

    STATIC_ASSERT(LAST_SPEC_OBJECT_TYPE == LAST_TYPE);
    __ CompareObjectType(r5, r6, r6, FIRST_SPEC_OBJECT_TYPE);
    __ bge(&shift_arguments);

    __ bind(&convert_to_object);

    {
      // Enter an internal frame in order to preserve argument count.
      FrameScope scope(masm, StackFrame::INTERNAL);
      __ slwi(r3, r3, Operand(kSmiTagSize));  // Smi-tagged.
      __ push(r3);

      __ push(r5);
      __ InvokeBuiltin(Builtins::TO_OBJECT, CALL_FUNCTION);
      __ mr(r5, r3);

      __ pop(r3);
      __ srawi(r3, r3, kSmiTagSize);

      // Exit the internal frame.
    }

    // Restore the function to r4, and the flag to r7.
    __ slwi(r7, r3, Operand(kPointerSizeLog2));
    __ add(r7, sp, r7);
    __ lwz(r4, MemOperand(r7));
    __ li(r7, Operand(0, RelocInfo::NONE));
    __ jmp(&patch_receiver);

    // Use the global receiver object from the called function as the
    // receiver.
    __ bind(&use_global_receiver);
    const int kGlobalIndex =
        Context::kHeaderSize + Context::GLOBAL_OBJECT_INDEX * kPointerSize;
    __ lwz(r5, FieldMemOperand(cp, kGlobalIndex));
    __ lwz(r5, FieldMemOperand(r5, GlobalObject::kNativeContextOffset));
    __ lwz(r5, FieldMemOperand(r5, kGlobalIndex));
    __ lwz(r5, FieldMemOperand(r5, GlobalObject::kGlobalReceiverOffset));

    __ bind(&patch_receiver);
    __ slwi(ip, r3, Operand(kPointerSizeLog2));
    __ add(r6, sp, ip);
    __ stw(r5, MemOperand(r6, -kPointerSize));

    __ jmp(&shift_arguments);
  }

  // 3b. Check for function proxy.
  __ bind(&slow);
  __ li(r7, Operand(1, RelocInfo::NONE));  // indicate function proxy
  __ cmpi(r5, Operand(JS_FUNCTION_PROXY_TYPE));
  __ beq(&shift_arguments);
  __ bind(&non_function);
  __ li(r7, Operand(2, RelocInfo::NONE));  // indicate non-function

  // 3c. Patch the first argument when calling a non-function.  The
  //     CALL_NON_FUNCTION builtin expects the non-function callee as
  //     receiver, so overwrite the first argument which will ultimately
  //     become the receiver.
  // r3: actual number of arguments
  // r4: function
  // r7: call type (0: JS function, 1: function proxy, 2: non-function)
  __ slwi(ip, r3, Operand(kPointerSizeLog2));
  __ add(r5, sp, ip);
  __ stw(r4, MemOperand(r5, -kPointerSize));

  // 4. Shift arguments and return address one slot down on the stack
  //    (overwriting the original receiver).  Adjust argument count to make
  //    the original first argument the new receiver.
  // r3: actual number of arguments
  // r4: function
  // r7: call type (0: JS function, 1: function proxy, 2: non-function)
  __ bind(&shift_arguments);
  { Label loop;
    // Calculate the copy start address (destination). Copy end address is sp.
    __ slwi(ip, r3, Operand(kPointerSizeLog2));
    __ add(r5, sp, ip);

    __ bind(&loop);
    __ lwz(ip, MemOperand(r5, -kPointerSize));
    __ stw(ip, MemOperand(r5));
    __ sub(r5, r5, Operand(kPointerSize));
    __ cmp(r5, sp);
    __ bne(&loop);
    // Adjust the actual number of arguments and remove the top element
    // (which is a copy of the last argument).
    __ sub(r3, r3, Operand(1));
    __ pop();
  }

  // 5a. Call non-function via tail call to CALL_NON_FUNCTION builtin,
  //     or a function proxy via CALL_FUNCTION_PROXY.
  // r3: actual number of arguments
  // r4: function
  // r7: call type (0: JS function, 1: function proxy, 2: non-function)
  { Label function, non_proxy;
    __ cmpi(r7, Operand(0));
    __ beq(&function);
    // Expected number of arguments is 0 for CALL_NON_FUNCTION.
    __ li(r5, Operand(0, RelocInfo::NONE));
    __ SetCallKind(r8, CALL_AS_METHOD);
    __ cmpi(r7, Operand(1));
    __ bne(&non_proxy);

    __ push(r4);  // re-add proxy object as additional argument
    __ addi(r3, r3, Operand(1));
    __ GetBuiltinEntry(r6, Builtins::CALL_FUNCTION_PROXY);
    __ Jump(masm->isolate()->builtins()->ArgumentsAdaptorTrampoline(),
            RelocInfo::CODE_TARGET);

    __ bind(&non_proxy);
    __ GetBuiltinEntry(r6, Builtins::CALL_NON_FUNCTION);
    __ Jump(masm->isolate()->builtins()->ArgumentsAdaptorTrampoline(),
            RelocInfo::CODE_TARGET);
    __ bind(&function);
  }

  // 5b. Get the code to call from the function and check that the number of
  //     expected arguments matches what we're providing.  If so, jump
  //     (tail-call) to the code in register edx without checking arguments.
  // r3: actual number of arguments
  // r4: function
  __ lwz(r6, FieldMemOperand(r4, JSFunction::kSharedFunctionInfoOffset));
  __ lwz(r5,
         FieldMemOperand(r6, SharedFunctionInfo::kFormalParameterCountOffset));
  __ srawi(r5, r5, kSmiTagSize);
  __ lwz(r6, FieldMemOperand(r4, JSFunction::kCodeEntryOffset));
  __ SetCallKind(r8, CALL_AS_METHOD);
  __ cmp(r5, r3);  // Check formal and actual parameter counts.
  Label skip;
  __ beq(&skip);
  __ Jump(masm->isolate()->builtins()->ArgumentsAdaptorTrampoline(),
          RelocInfo::CODE_TARGET);

  __ bind(&skip);
  ParameterCount expected(0);
  __ InvokeCode(r6, expected, expected, JUMP_FUNCTION,
                NullCallWrapper(), CALL_AS_METHOD);
}


void Builtins::Generate_FunctionApply(MacroAssembler* masm) {
  const int kIndexOffset    = -5 * kPointerSize;
  const int kLimitOffset    = -4 * kPointerSize;
  const int kArgsOffset     =  2 * kPointerSize;
  const int kRecvOffset     =  3 * kPointerSize;
  const int kFunctionOffset =  4 * kPointerSize;

  {
    EMIT_STUB_MARKER(320);
    FrameScope frame_scope(masm, StackFrame::INTERNAL);

    __ lwz(r3, MemOperand(fp, kFunctionOffset));  // get the function
    __ push(r3);
    __ lwz(r3, MemOperand(fp, kArgsOffset));  // get the args array
    __ push(r3);
    __ InvokeBuiltin(Builtins::APPLY_PREPARE, CALL_FUNCTION);

    // Check the stack for overflow. We are not trying to catch
    // interruptions (e.g. debug break and preemption) here, so the "real stack
    // limit" is checked.
    Label okay;
    __ LoadRoot(r5, Heap::kRealStackLimitRootIndex);
    // Make r5 the space we have left. The stack might already be overflowed
    // here which will cause r5 to become negative.
    __ sub(r5, sp, r5);
    // Check if the arguments will overflow the stack.
    __ slwi(r0, r3, Operand(kPointerSizeLog2 - kSmiTagSize));
    __ cmp(r5, r0);
    __ bgt(&okay);  // Signed comparison.

    // Out of stack space.
    __ lwz(r4, MemOperand(fp, kFunctionOffset));
    __ push(r4);
    __ push(r3);
    __ InvokeBuiltin(Builtins::APPLY_OVERFLOW, CALL_FUNCTION);
    // End of stack check.

    // Push current limit and index.
    __ bind(&okay);
    __ push(r3);  // limit
    __ li(r4, Operand(0, RelocInfo::NONE));  // initial index
    __ push(r4);

    // Get the receiver.
    __ lwz(r3, MemOperand(fp, kRecvOffset));

    // Check that the function is a JS function (otherwise it must be a proxy).
    Label push_receiver;
    __ lwz(r4, MemOperand(fp, kFunctionOffset));
    __ CompareObjectType(r4, r5, r5, JS_FUNCTION_TYPE);
    __ bne(&push_receiver);

    // Change context eagerly to get the right global object if necessary.
    __ lwz(cp, FieldMemOperand(r4, JSFunction::kContextOffset));
    // Load the shared function info while the function is still in r4.
    __ lwz(r5, FieldMemOperand(r4, JSFunction::kSharedFunctionInfoOffset));

    // Compute the receiver.
    // Do not transform the receiver for strict mode functions.
    Label call_to_object, use_global_receiver;
    __ lwz(r5, FieldMemOperand(r5, SharedFunctionInfo::kCompilerHintsOffset));
    __ andi(r0, r5, Operand(1 << (SharedFunctionInfo::kStrictModeFunction +
                             kSmiTagSize)));
    __ bne(&push_receiver, cr0);

    // Do not transform the receiver for strict mode functions.
    __ andi(r0, r5, Operand(1 << (SharedFunctionInfo::kNative + kSmiTagSize)));
    __ bne(&push_receiver, cr0);

    // Compute the receiver in non-strict mode.
    __ JumpIfSmi(r3, &call_to_object);
    __ LoadRoot(r4, Heap::kNullValueRootIndex);
    __ cmp(r3, r4);
    __ beq(&use_global_receiver);
    __ LoadRoot(r4, Heap::kUndefinedValueRootIndex);
    __ cmp(r3, r4);
    __ beq(&use_global_receiver);

    // Check if the receiver is already a JavaScript object.
    // r3: receiver
    STATIC_ASSERT(LAST_SPEC_OBJECT_TYPE == LAST_TYPE);
    __ CompareObjectType(r3, r4, r4, FIRST_SPEC_OBJECT_TYPE);
    __ bge(&push_receiver);

    // Convert the receiver to a regular object.
    // r3: receiver
    __ bind(&call_to_object);
    __ push(r3);
    __ InvokeBuiltin(Builtins::TO_OBJECT, CALL_FUNCTION);
    __ b(&push_receiver);

    // Use the current global receiver object as the receiver.
    __ bind(&use_global_receiver);
    const int kGlobalOffset =
        Context::kHeaderSize + Context::GLOBAL_OBJECT_INDEX * kPointerSize;
    __ lwz(r3, FieldMemOperand(cp, kGlobalOffset));
    __ lwz(r3, FieldMemOperand(r3, GlobalObject::kNativeContextOffset));
    __ lwz(r3, FieldMemOperand(r3, kGlobalOffset));
    __ lwz(r3, FieldMemOperand(r3, GlobalObject::kGlobalReceiverOffset));

    // Push the receiver.
    // r3: receiver
    __ bind(&push_receiver);
    __ push(r3);

    // Copy all arguments from the array to the stack.
    Label entry, loop;
    __ lwz(r3, MemOperand(fp, kIndexOffset));
    __ b(&entry);

    // Load the current argument from the arguments array and push it to the
    // stack.
    // r3: current argument index
    __ bind(&loop);
    __ lwz(r4, MemOperand(fp, kArgsOffset));
    __ push(r4);
    __ push(r3);

    // Call the runtime to access the property in the arguments array.
    __ CallRuntime(Runtime::kGetProperty, 2);
    __ push(r3);

    // Use inline caching to access the arguments.
    __ lwz(r3, MemOperand(fp, kIndexOffset));
    __ addi(r3, r3, Operand(1 << kSmiTagSize));
    __ stw(r3, MemOperand(fp, kIndexOffset));

    // Test if the copy loop has finished copying all the elements from the
    // arguments object.
    __ bind(&entry);
    __ lwz(r4, MemOperand(fp, kLimitOffset));
    __ cmp(r3, r4);
    __ bne(&loop);

    // Invoke the function.
    Label call_proxy;
    ParameterCount actual(r3);
    __ srawi(r3, r3, kSmiTagSize);
    __ lwz(r4, MemOperand(fp, kFunctionOffset));
    __ CompareObjectType(r4, r5, r5, JS_FUNCTION_TYPE);
    __ bne(&call_proxy);
    __ InvokeFunction(r4, actual, CALL_FUNCTION,
                      NullCallWrapper(), CALL_AS_METHOD);

    frame_scope.GenerateLeaveFrame();
    __ addi(sp, sp, Operand(3 * kPointerSize));
    __ blr();

    // Invoke the function proxy.
    __ bind(&call_proxy);
    __ push(r4);  // add function proxy as last argument
    __ addi(r3, r3, Operand(1));
    __ li(r5, Operand(0, RelocInfo::NONE));
    __ SetCallKind(r8, CALL_AS_METHOD);
    __ GetBuiltinEntry(r6, Builtins::CALL_FUNCTION_PROXY);
    __ Call(masm->isolate()->builtins()->ArgumentsAdaptorTrampoline(),
            RelocInfo::CODE_TARGET);

    // Tear down the internal frame and remove function, receiver and args.
  }
  __ addi(sp, sp, Operand(3 * kPointerSize));
  __ blr();
}


static void EnterArgumentsAdaptorFrame(MacroAssembler* masm) {
  EMIT_STUB_MARKER(321);
  __ slwi(r3, r3, Operand(kSmiTagSize));
  __ li(r7, Operand(Smi::FromInt(StackFrame::ARGUMENTS_ADAPTOR)));
  __ mflr(r0);
  __ push(r0);
  __ Push(fp, r7, r4, r3);
  __ addi(fp, sp, Operand(3 * kPointerSize));
}


static void LeaveArgumentsAdaptorFrame(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- r3 : result being passed through
  // -----------------------------------
  // Get the number of arguments passed (as a smi), tear down the frame and
  // then tear down the parameters.
  __ lwz(r4, MemOperand(fp, -3 * kPointerSize));
  __ mr(sp, fp);
  __ lwz(fp, MemOperand(sp));
  __ lwz(r0, MemOperand(sp, kPointerSize));
  __ mtlr(r0);
  __ slwi(r4, r4, Operand(kPointerSizeLog2 - kSmiTagSize));
  __ add(sp, sp, r4);  // roohack - ok to destroy r4?
  __ addi(sp, sp, Operand(3 * kPointerSize));  // adjust for receiver + fp + lr
}


void Builtins::Generate_ArgumentsAdaptorTrampoline(MacroAssembler* masm) {
  EMIT_STUB_MARKER(322);
  // ----------- S t a t e -------------
  //  -- r3 : actual number of arguments
  //  -- r4 : function (passed through to callee)
  //  -- r5 : expected number of arguments
  //  -- r6 : code entry to call
  //  -- r8 : call kind information
  // -----------------------------------

  Label invoke, dont_adapt_arguments;

  Label enough, too_few;
  __ cmp(r3, r5);
  __ blt(&too_few);
  __ cmpi(r5, Operand(SharedFunctionInfo::kDontAdaptArgumentsSentinel));
  __ beq(&dont_adapt_arguments);

  {  // Enough parameters: actual >= expected
    __ bind(&enough);
    EnterArgumentsAdaptorFrame(masm);

    // Calculate copy start address into r3 and copy end address into r5.
    // r3: actual number of arguments as a smi
    // r4: function
    // r5: expected number of arguments
    // r6: code entry to call
    __ slwi(r3, r3, Operand(kPointerSizeLog2 - kSmiTagSize));
    __ add(r3, r3, fp);
    // adjust for return address and receiver
    __ addi(r3, r3, Operand(2 * kPointerSize));
    __ slwi(r5, r5, Operand(kPointerSizeLog2));
    __ sub(r5, r3, r5);

    // Copy the arguments (including the receiver) to the new stack frame.
    // r3: copy start address
    // r4: function
    // r5: copy end address
    // r6: code entry to call

    Label copy;
    __ bind(&copy);
    __ lwz(ip, MemOperand(r3, 0));
    __ push(ip);
    __ cmp(r3, r5);  // Compare before moving to next argument.
    __ sub(r3, r3, Operand(kPointerSize));
    __ bne(&copy);

    __ b(&invoke);
  }

  {  // Too few parameters: Actual < expected
    __ bind(&too_few);
    EnterArgumentsAdaptorFrame(masm);

    // Calculate copy start address into r0 and copy end address is fp.
    // r3: actual number of arguments as a smi
    // r4: function
    // r5: expected number of arguments
    // r6: code entry to call
    __ slwi(r3, r3, Operand(kPointerSizeLog2 - kSmiTagSize));
    __ add(r3, r3, fp);

    // Copy the arguments (including the receiver) to the new stack frame.
    // r3: copy start address
    // r4: function
    // r5: expected number of arguments
    // r6: code entry to call
    Label copy;
    __ bind(&copy);
    // Adjust load for return address and receiver.
    __ lwz(ip, MemOperand(r3, 2 * kPointerSize));
    __ push(ip);
    __ cmp(r3, fp);  // Compare before moving to next argument.
    __ sub(r3, r3, Operand(kPointerSize));
    __ bne(&copy);

    // Fill the remaining expected arguments with undefined.
    // r4: function
    // r5: expected number of arguments
    // r6: code entry to call
    __ LoadRoot(ip, Heap::kUndefinedValueRootIndex);
    __ slwi(r5, r5, Operand(kPointerSizeLog2));
    __ sub(r5, fp, r5);
    __ sub(r5, r5, Operand(4 * kPointerSize));  // Adjust for frame.

    Label fill;
    __ bind(&fill);
    __ push(ip);
    __ cmp(sp, r5);
    __ bne(&fill);
  }

  // Call the entry point.
  __ bind(&invoke);
  __ Call(r6);

  // Store offset of return address for deoptimizer.
  masm->isolate()->heap()->SetArgumentsAdaptorDeoptPCOffset(masm->pc_offset());

  // Exit frame and return.
  LeaveArgumentsAdaptorFrame(masm);
  __ blr();


  // -------------------------------------------
  // Dont adapt arguments.
  // -------------------------------------------
  __ bind(&dont_adapt_arguments);
  __ Jump(r6);
}


#undef __

} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_PPC
