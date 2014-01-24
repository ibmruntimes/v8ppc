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

#include "ic-inl.h"
#include "codegen.h"
#include "stub-cache.h"

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm)


static void ProbeTable(Isolate* isolate,
                       MacroAssembler* masm,
                       Code::Flags flags,
                       StubCache::Table table,
                       Register receiver,
                       Register name,
                       // Number of the cache entry, not scaled.
                       Register offset,
                       Register scratch,
                       Register scratch2,
                       Register offset_scratch) {
  ExternalReference key_offset(isolate->stub_cache()->key_reference(table));
  ExternalReference value_offset(isolate->stub_cache()->value_reference(table));
  ExternalReference map_offset(isolate->stub_cache()->map_reference(table));

  uintptr_t key_off_addr = reinterpret_cast<uintptr_t>(key_offset.address());
  uintptr_t value_off_addr =
                         reinterpret_cast<uintptr_t>(value_offset.address());
  uintptr_t map_off_addr = reinterpret_cast<uintptr_t>(map_offset.address());

  // Check the relative positions of the address fields.
  ASSERT(value_off_addr > key_off_addr);
  ASSERT((value_off_addr - key_off_addr) % 4 == 0);
  ASSERT((value_off_addr - key_off_addr) < (256 * 4));
  ASSERT(map_off_addr > key_off_addr);
  ASSERT((map_off_addr - key_off_addr) % 4 == 0);
  ASSERT((map_off_addr - key_off_addr) < (256 * 4));

  Label miss;
  Register base_addr = scratch;
  scratch = no_reg;

  // Multiply by 3 because there are 3 fields per entry (name, code, map).
  __ ShiftLeftImm(offset_scratch, offset, Operand(1));
  __ add(offset_scratch, offset, offset_scratch);

  // Calculate the base address of the entry.
  __ mov(base_addr, Operand(key_offset));
  __ ShiftLeftImm(scratch2, offset_scratch, Operand(kPointerSizeLog2));
  __ add(base_addr, base_addr, scratch2);

  // Check that the key in the entry matches the name.
  __ LoadP(ip, MemOperand(base_addr, 0));
  __ cmp(name, ip);
  __ bne(&miss);

  // Check the map matches.
  __ LoadP(ip, MemOperand(base_addr, map_off_addr - key_off_addr));
  __ LoadP(scratch2, FieldMemOperand(receiver, HeapObject::kMapOffset));
  __ cmp(ip, scratch2);
  __ bne(&miss);

  // Get the code entry from the cache.
  Register code = scratch2;
  scratch2 = no_reg;
  __ LoadP(code, MemOperand(base_addr, value_off_addr - key_off_addr));

  // Check that the flags match what we're looking for.
  Register flags_reg = base_addr;
  base_addr = no_reg;
  __ lwz(flags_reg, FieldMemOperand(code, Code::kFlagsOffset));

  ASSERT(!r0.is(flags_reg));
  __ li(r0, Operand(Code::kFlagsNotUsedInLookup));
  __ andc(flags_reg, flags_reg, r0);
  __ mov(r0, Operand(flags));
  __ cmpl(flags_reg, r0);
  __ bne(&miss);

#ifdef DEBUG
    if (FLAG_test_secondary_stub_cache && table == StubCache::kPrimary) {
      __ b(&miss);
    } else if (FLAG_test_primary_stub_cache && table == StubCache::kSecondary) {
      __ b(&miss);
    }
#endif

  // Jump to the first instruction in the code stub.
  __ addi(r0, code, Operand(Code::kHeaderSize - kHeapObjectTag));
  __ mtctr(r0);
  __ bctr();

  // Miss: fall through.
  __ bind(&miss);
}


// Helper function used to check that the dictionary doesn't contain
// the property. This function may return false negatives, so miss_label
// must always call a backup property check that is complete.
// This function is safe to call if the receiver has fast properties.
// Name must be unique and receiver must be a heap object.
static void GenerateDictionaryNegativeLookup(MacroAssembler* masm,
                                             Label* miss_label,
                                             Register receiver,
                                             Handle<Name> name,
                                             Register scratch0,
                                             Register scratch1) {
  ASSERT(name->IsUniqueName());
  Counters* counters = masm->isolate()->counters();
  __ IncrementCounter(counters->negative_lookups(), 1, scratch0, scratch1);
  __ IncrementCounter(counters->negative_lookups_miss(), 1, scratch0, scratch1);

  Label done;

  const int kInterceptorOrAccessCheckNeededMask =
      (1 << Map::kHasNamedInterceptor) | (1 << Map::kIsAccessCheckNeeded);

  // Bail out if the receiver has a named interceptor or requires access checks.
  Register map = scratch1;
  __ LoadP(map, FieldMemOperand(receiver, HeapObject::kMapOffset));
  __ lbz(scratch0, FieldMemOperand(map, Map::kBitFieldOffset));
  __ andi(r0, scratch0, Operand(kInterceptorOrAccessCheckNeededMask));
  __ bne(miss_label, cr0);

  // Check that receiver is a JSObject.
  __ lbz(scratch0, FieldMemOperand(map, Map::kInstanceTypeOffset));
  __ cmpi(scratch0, Operand(FIRST_SPEC_OBJECT_TYPE));
  __ blt(miss_label);

  // Load properties array.
  Register properties = scratch0;
  __ LoadP(properties, FieldMemOperand(receiver, JSObject::kPropertiesOffset));
  // Check that the properties array is a dictionary.
  __ LoadP(map, FieldMemOperand(properties, HeapObject::kMapOffset));
  Register tmp = properties;
  __ LoadRoot(tmp, Heap::kHashTableMapRootIndex);
  __ cmp(map, tmp);
  __ bne(miss_label);

  // Restore the temporarily used register.
  __ LoadP(properties, FieldMemOperand(receiver, JSObject::kPropertiesOffset));


  NameDictionaryLookupStub::GenerateNegativeLookup(masm,
                                                   miss_label,
                                                   &done,
                                                   receiver,
                                                   properties,
                                                   name,
                                                   scratch1);
  __ bind(&done);
  __ DecrementCounter(counters->negative_lookups_miss(), 1, scratch0, scratch1);
}


void StubCache::GenerateProbe(MacroAssembler* masm,
                              Code::Flags flags,
                              Register receiver,
                              Register name,
                              Register scratch,
                              Register extra,
                              Register extra2,
                              Register extra3) {
  Isolate* isolate = masm->isolate();
  Label miss;

#if V8_TARGET_ARCH_PPC64
  // Make sure that code is valid. The multiplying code relies on the
  // entry size being 24.
  ASSERT(sizeof(Entry) == 24);
#else
  // Make sure that code is valid. The multiplying code relies on the
  // entry size being 12.
  ASSERT(sizeof(Entry) == 12);
#endif

  // Make sure the flags does not name a specific type.
  ASSERT(Code::ExtractTypeFromFlags(flags) == 0);

  // Make sure that there are no register conflicts.
  ASSERT(!scratch.is(receiver));
  ASSERT(!scratch.is(name));
  ASSERT(!extra.is(receiver));
  ASSERT(!extra.is(name));
  ASSERT(!extra.is(scratch));
  ASSERT(!extra2.is(receiver));
  ASSERT(!extra2.is(name));
  ASSERT(!extra2.is(scratch));
  ASSERT(!extra2.is(extra));

  // Check scratch, extra and extra2 registers are valid.
  ASSERT(!scratch.is(no_reg));
  ASSERT(!extra.is(no_reg));
  ASSERT(!extra2.is(no_reg));
  ASSERT(!extra3.is(no_reg));

  Counters* counters = masm->isolate()->counters();
  __ IncrementCounter(counters->megamorphic_stub_cache_probes(), 1,
                      extra2, extra3);

  // Check that the receiver isn't a smi.
  __ JumpIfSmi(receiver, &miss);

  // Get the map of the receiver and compute the hash.
  __ lwz(scratch, FieldMemOperand(name, Name::kHashFieldOffset));
  __ LoadP(ip, FieldMemOperand(receiver, HeapObject::kMapOffset));
  __ add(scratch, scratch, ip);
#if V8_TARGET_ARCH_PPC64
  // Use only the low 32 bits of the map pointer.
  __ rldicl(scratch, scratch, 0, 32);
#endif
  uint32_t mask = kPrimaryTableSize - 1;
  // We shift out the last two bits because they are not part of the hash and
  // they are always 01 for maps.
  __ ShiftRightImm(scratch, scratch, Operand(kHeapObjectTagSize));
  // Mask down the eor argument to the minimum to keep the immediate
  // encodable.
  __ xori(scratch, scratch, Operand((flags >> kHeapObjectTagSize) & mask));
  // Prefer and_ to ubfx here because ubfx takes 2 cycles.
  __ andi(scratch, scratch, Operand(mask));

  // Probe the primary table.
  ProbeTable(isolate,
             masm,
             flags,
             kPrimary,
             receiver,
             name,
             scratch,
             extra,
             extra2,
             extra3);

  // Primary miss: Compute hash for secondary probe.
  __ ShiftRightImm(extra, name, Operand(kHeapObjectTagSize));
  __ sub(scratch, scratch, extra);
  uint32_t mask2 = kSecondaryTableSize - 1;
  __ addi(scratch, scratch, Operand((flags >> kHeapObjectTagSize) & mask2));
  __ andi(scratch, scratch, Operand(mask2));

  // Probe the secondary table.
  ProbeTable(isolate,
             masm,
             flags,
             kSecondary,
             receiver,
             name,
             scratch,
             extra,
             extra2,
             extra3);

  // Cache miss: Fall-through and let caller handle the miss by
  // entering the runtime system.
  __ bind(&miss);
  __ IncrementCounter(counters->megamorphic_stub_cache_misses(), 1,
                      extra2, extra3);
}


void StubCompiler::GenerateLoadGlobalFunctionPrototype(MacroAssembler* masm,
                                                       int index,
                                                       Register prototype) {
  // Load the global or builtins object from the current context.
  __ LoadP(prototype,
           MemOperand(cp, Context::SlotOffset(Context::GLOBAL_OBJECT_INDEX)));
  // Load the native context from the global or builtins object.
  __ LoadP(prototype,
           FieldMemOperand(prototype, GlobalObject::kNativeContextOffset));
  // Load the function from the native context.
  __ LoadP(prototype, MemOperand(prototype, Context::SlotOffset(index)), r0);
  // Load the initial map.  The global functions all have initial maps.
  __ LoadP(prototype,
           FieldMemOperand(prototype,
                           JSFunction::kPrototypeOrInitialMapOffset));
  // Load the prototype from the initial map.
  __ LoadP(prototype, FieldMemOperand(prototype, Map::kPrototypeOffset));
}


void StubCompiler::GenerateDirectLoadGlobalFunctionPrototype(
    MacroAssembler* masm,
    int index,
    Register prototype,
    Label* miss) {
  Isolate* isolate = masm->isolate();
  // Check we're still in the same context.
  __ LoadP(prototype,
           MemOperand(cp, Context::SlotOffset(Context::GLOBAL_OBJECT_INDEX)));
  __ Move(ip, isolate->global_object());
  __ cmp(prototype, ip);
  __ bne(miss);
  // Get the global function with the given index.
  Handle<JSFunction> function(
      JSFunction::cast(isolate->native_context()->get(index)));
  // Load its initial map. The global functions all have initial maps.
  __ Move(prototype, Handle<Map>(function->initial_map()));
  // Load the prototype from the initial map.
  __ LoadP(prototype, FieldMemOperand(prototype, Map::kPrototypeOffset));
}


void StubCompiler::GenerateFastPropertyLoad(MacroAssembler* masm,
                                            Register dst,
                                            Register src,
                                            bool inobject,
                                            int index,
                                            Representation representation) {
  ASSERT(!FLAG_track_double_fields || !representation.IsDouble());
  int offset = index * kPointerSize;
  if (!inobject) {
    // Calculate the offset into the properties array.
    offset = offset + FixedArray::kHeaderSize;
    __ LoadP(dst, FieldMemOperand(src, JSObject::kPropertiesOffset));
    src = dst;
  }
  __ LoadP(dst, FieldMemOperand(src, offset), r0);
}


void StubCompiler::GenerateLoadArrayLength(MacroAssembler* masm,
                                           Register receiver,
                                           Register scratch,
                                           Label* miss_label) {
  // Check that the receiver isn't a smi.
  __ JumpIfSmi(receiver, miss_label);

  // Check that the object is a JS array.
  __ CompareObjectType(receiver, scratch, scratch, JS_ARRAY_TYPE);
  __ bne(miss_label);

  // Load length directly from the JS array.
  __ LoadP(r3, FieldMemOperand(receiver, JSArray::kLengthOffset));
  __ Ret();
}


// Generate code to check if an object is a string.  If the object is a
// heap object, its map's instance type is left in the scratch1 register.
// If this is not needed, scratch1 and scratch2 may be the same register.
static void GenerateStringCheck(MacroAssembler* masm,
                                Register receiver,
                                Register scratch1,
                                Register scratch2,
                                Label* smi,
                                Label* non_string_object) {
  // Check that the receiver isn't a smi.
  __ JumpIfSmi(receiver, smi);

  // Check that the object is a string.
  __ LoadP(scratch1, FieldMemOperand(receiver, HeapObject::kMapOffset));
  __ lbz(scratch1, FieldMemOperand(scratch1, Map::kInstanceTypeOffset));
  __ andi(scratch2, scratch1, Operand(kIsNotStringMask));
  // The cast is to resolve the overload for the argument of 0x0.
  __ cmpi(scratch2, Operand(static_cast<intptr_t>(kStringTag)));
  __ bne(non_string_object);
}


// Generate code to load the length from a string object and return the length.
// If the receiver object is not a string or a wrapped string object the
// execution continues at the miss label. The register containing the
// receiver is potentially clobbered.
void StubCompiler::GenerateLoadStringLength(MacroAssembler* masm,
                                            Register receiver,
                                            Register scratch1,
                                            Register scratch2,
                                            Label* miss,
                                            bool support_wrappers) {
  Label check_wrapper;

  // Check if the object is a string leaving the instance type in the
  // scratch1 register.
  GenerateStringCheck(masm, receiver, scratch1, scratch2, miss,
                      support_wrappers ? &check_wrapper : miss);

  // Load length directly from the string.
  __ LoadP(r3, FieldMemOperand(receiver, String::kLengthOffset));
  __ Ret();

  if (support_wrappers) {
    // Check if the object is a JSValue wrapper.
    __ bind(&check_wrapper);
    __ cmpi(scratch1, Operand(JS_VALUE_TYPE));
    __ bne(miss);

    // Unwrap the value and check if the wrapped value is a string.
    __ LoadP(scratch1, FieldMemOperand(receiver, JSValue::kValueOffset));
    GenerateStringCheck(masm, scratch1, scratch2, scratch2, miss, miss);
    __ LoadP(r3, FieldMemOperand(scratch1, String::kLengthOffset));
    __ Ret();
  }
}


void StubCompiler::GenerateLoadFunctionPrototype(MacroAssembler* masm,
                                                 Register receiver,
                                                 Register scratch1,
                                                 Register scratch2,
                                                 Label* miss_label) {
  __ TryGetFunctionPrototype(receiver, scratch1, scratch2, miss_label);
  __ mr(r3, scratch1);
  __ Ret();
}


// Generate code to check that a global property cell is empty. Create
// the property cell at compilation time if no cell exists for the
// property.
static void GenerateCheckPropertyCell(MacroAssembler* masm,
                                      Handle<GlobalObject> global,
                                      Handle<Name> name,
                                      Register scratch,
                                      Label* miss) {
  Handle<Cell> cell = GlobalObject::EnsurePropertyCell(global, name);
  ASSERT(cell->value()->IsTheHole());
  __ mov(scratch, Operand(cell));
  __ LoadP(scratch, FieldMemOperand(scratch, Cell::kValueOffset));
  __ LoadRoot(ip, Heap::kTheHoleValueRootIndex);
  __ cmp(scratch, ip);
  __ bne(miss);
}


void BaseStoreStubCompiler::GenerateNegativeHolderLookup(
    MacroAssembler* masm,
    Handle<JSObject> holder,
    Register holder_reg,
    Handle<Name> name,
    Label* miss) {
  if (holder->IsJSGlobalObject()) {
    GenerateCheckPropertyCell(
        masm, Handle<GlobalObject>::cast(holder), name, scratch1(), miss);
  } else if (!holder->HasFastProperties() && !holder->IsJSGlobalProxy()) {
    GenerateDictionaryNegativeLookup(
        masm, miss, holder_reg, name, scratch1(), scratch2());
  }
}


// Generate StoreTransition code, value is passed in r3 register.
// When leaving generated code after success, the receiver_reg and name_reg
// may be clobbered.  Upon branch to miss_label, the receiver and name
// registers have their original values.
void BaseStoreStubCompiler::GenerateStoreTransition(MacroAssembler* masm,
                                                    Handle<JSObject> object,
                                                    LookupResult* lookup,
                                                    Handle<Map> transition,
                                                    Handle<Name> name,
                                                    Register receiver_reg,
                                                    Register storage_reg,
                                                    Register value_reg,
                                                    Register scratch1,
                                                    Register scratch2,
                                                    Register scratch3,
                                                    Label* miss_label,
                                                    Label* slow) {
  // r3 : value
  Label exit;

  int descriptor = transition->LastAdded();
  DescriptorArray* descriptors = transition->instance_descriptors();
  PropertyDetails details = descriptors->GetDetails(descriptor);
  Representation representation = details.representation();
  ASSERT(!representation.IsNone());

  if (details.type() == CONSTANT) {
    Handle<Object> constant(descriptors->GetValue(descriptor), masm->isolate());
    __ LoadObject(scratch1, constant);
    __ cmp(value_reg, scratch1);
    __ bne(miss_label);
  } else if (FLAG_track_fields && representation.IsSmi()) {
    __ JumpIfNotSmi(value_reg, miss_label);
  } else if (FLAG_track_heap_object_fields && representation.IsHeapObject()) {
    __ JumpIfSmi(value_reg, miss_label);
  } else if (FLAG_track_double_fields && representation.IsDouble()) {
    Label do_store, heap_number;
    __ LoadRoot(scratch3, Heap::kHeapNumberMapRootIndex);
    __ AllocateHeapNumber(storage_reg, scratch1, scratch2, scratch3, slow);

    __ JumpIfNotSmi(value_reg, &heap_number);
    __ SmiUntag(scratch1, value_reg);
    __ ConvertIntToDouble(scratch1, d0);
    __ jmp(&do_store);

    __ bind(&heap_number);
    __ CheckMap(value_reg, scratch1, Heap::kHeapNumberMapRootIndex,
                miss_label, DONT_DO_SMI_CHECK);
    __ lfd(d0, FieldMemOperand(value_reg, HeapNumber::kValueOffset));

    __ bind(&do_store);
    __ stfd(d0, FieldMemOperand(storage_reg, HeapNumber::kValueOffset));
  }

  // Stub never generated for non-global objects that require access
  // checks.
  ASSERT(object->IsJSGlobalProxy() || !object->IsAccessCheckNeeded());

  // Perform map transition for the receiver if necessary.
  if (details.type() == FIELD &&
      object->map()->unused_property_fields() == 0) {
    // The properties must be extended before we can store the value.
    // We jump to a runtime call that extends the properties array.
    __ push(receiver_reg);
    __ mov(r5, Operand(transition));
    __ Push(r5, r3);
    __ TailCallExternalReference(
        ExternalReference(IC_Utility(IC::kSharedStoreIC_ExtendStorage),
                          masm->isolate()),
        3,
        1);
    return;
  }

  // Update the map of the object.
  __ mov(scratch1, Operand(transition));
  __ StoreP(scratch1, FieldMemOperand(receiver_reg, HeapObject::kMapOffset),
            r0);

  // Update the write barrier for the map field.
  __ RecordWriteField(receiver_reg,
                      HeapObject::kMapOffset,
                      scratch1,
                      scratch2,
                      kLRHasNotBeenSaved,
                      kDontSaveFPRegs,
                      OMIT_REMEMBERED_SET,
                      OMIT_SMI_CHECK);

  if (details.type() == CONSTANT) {
    ASSERT(value_reg.is(r3));
    __ Ret();
    return;
  }

  int index = transition->instance_descriptors()->GetFieldIndex(
      transition->LastAdded());

  // Adjust for the number of properties stored in the object. Even in the
  // face of a transition we can use the old map here because the size of the
  // object and the number of in-object properties is not going to change.
  index -= object->map()->inobject_properties();

  // TODO(verwaest): Share this code as a code stub.
  SmiCheck smi_check = representation.IsTagged()
      ? INLINE_SMI_CHECK : OMIT_SMI_CHECK;
  if (index < 0) {
    // Set the property straight into the object.
    int offset = object->map()->instance_size() + (index * kPointerSize);
    if (FLAG_track_double_fields && representation.IsDouble()) {
      __ StoreP(storage_reg, FieldMemOperand(receiver_reg, offset), r0);
    } else {
      __ StoreP(value_reg, FieldMemOperand(receiver_reg, offset), r0);
    }

    if (!FLAG_track_fields || !representation.IsSmi()) {
    // Update the write barrier for the array address.
      if (!FLAG_track_double_fields || !representation.IsDouble()) {
        __ mr(storage_reg, value_reg);
      }
      __ RecordWriteField(receiver_reg,
                          offset,
                          storage_reg,
                          scratch1,
                          kLRHasNotBeenSaved,
                          kDontSaveFPRegs,
                          EMIT_REMEMBERED_SET,
                          smi_check);
    }
  } else {
    // Write to the properties array.
    int offset = index * kPointerSize + FixedArray::kHeaderSize;
    // Get the properties array
    __ LoadP(scratch1,
             FieldMemOperand(receiver_reg, JSObject::kPropertiesOffset));
    if (FLAG_track_double_fields && representation.IsDouble()) {
      __ StoreP(storage_reg, FieldMemOperand(scratch1, offset), r0);
    } else {
      __ StoreP(value_reg, FieldMemOperand(scratch1, offset), r0);
    }

    if (!FLAG_track_fields || !representation.IsSmi()) {
      // Update the write barrier for the array address.
      if (!FLAG_track_double_fields || !representation.IsDouble()) {
        __ mr(storage_reg, value_reg);
      }
      __ RecordWriteField(scratch1,
                          offset,
                          storage_reg,
                          receiver_reg,
                          kLRHasNotBeenSaved,
                          kDontSaveFPRegs,
                          EMIT_REMEMBERED_SET,
                          smi_check);
    }
  }

  // Return the value (register r3).
  ASSERT(value_reg.is(r3));
  __ bind(&exit);
  __ Ret();
}


// Generate StoreField code, value is passed in r3 register.
// When leaving generated code after success, the receiver_reg and name_reg
// may be clobbered.  Upon branch to miss_label, the receiver and name
// registers have their original values.
void BaseStoreStubCompiler::GenerateStoreField(MacroAssembler* masm,
                                               Handle<JSObject> object,
                                               LookupResult* lookup,
                                               Register receiver_reg,
                                               Register name_reg,
                                               Register value_reg,
                                               Register scratch1,
                                               Register scratch2,
                                               Label* miss_label) {
  // r3 : value
  Label exit;

  // Stub never generated for non-global objects that require access
  // checks.
  ASSERT(object->IsJSGlobalProxy() || !object->IsAccessCheckNeeded());

  int index = lookup->GetFieldIndex().field_index();

  // Adjust for the number of properties stored in the object. Even in the
  // face of a transition we can use the old map here because the size of the
  // object and the number of in-object properties is not going to change.
  index -= object->map()->inobject_properties();

  Representation representation = lookup->representation();
  ASSERT(!representation.IsNone());
  if (FLAG_track_fields && representation.IsSmi()) {
    __ JumpIfNotSmi(value_reg, miss_label);
  } else if (FLAG_track_heap_object_fields && representation.IsHeapObject()) {
    __ JumpIfSmi(value_reg, miss_label);
  } else if (FLAG_track_double_fields && representation.IsDouble()) {
    // Load the double storage.
    if (index < 0) {
      int offset = object->map()->instance_size() + (index * kPointerSize);
      __ LoadP(scratch1, FieldMemOperand(receiver_reg, offset));
    } else {
      __ LoadP(scratch1,
             FieldMemOperand(receiver_reg, JSObject::kPropertiesOffset));
      int offset = index * kPointerSize + FixedArray::kHeaderSize;
      __ LoadP(scratch1, FieldMemOperand(scratch1, offset));
    }

    // Store the value into the storage.
    Label do_store, heap_number;
    __ JumpIfNotSmi(value_reg, &heap_number);
    __ SmiUntag(scratch2, value_reg);
    __ ConvertIntToDouble(scratch2, d0);
    __ jmp(&do_store);

    __ bind(&heap_number);
    __ CheckMap(value_reg, scratch2, Heap::kHeapNumberMapRootIndex,
                miss_label, DONT_DO_SMI_CHECK);
    __ lfd(d0, FieldMemOperand(value_reg, HeapNumber::kValueOffset));

    __ bind(&do_store);
    __ stfd(d0, FieldMemOperand(scratch1, HeapNumber::kValueOffset));
    // Return the value (register r3).
    ASSERT(value_reg.is(r3));
    __ Ret();
    return;
  }

  // TODO(verwaest): Share this code as a code stub.
  SmiCheck smi_check = representation.IsTagged()
      ? INLINE_SMI_CHECK : OMIT_SMI_CHECK;
  if (index < 0) {
    // Set the property straight into the object.
    int offset = object->map()->instance_size() + (index * kPointerSize);
    __ StoreP(value_reg, FieldMemOperand(receiver_reg, offset), r0);

    if (!FLAG_track_fields || !representation.IsSmi()) {
      // Skip updating write barrier if storing a smi.
      __ JumpIfSmi(value_reg, &exit);

      // Update the write barrier for the array address.
      // Pass the now unused name_reg as a scratch register.
      __ mr(name_reg, value_reg);
      __ RecordWriteField(receiver_reg,
                          offset,
                          name_reg,
                          scratch1,
                          kLRHasNotBeenSaved,
                          kDontSaveFPRegs,
                          EMIT_REMEMBERED_SET,
                          smi_check);
    }
  } else {
    // Write to the properties array.
    int offset = index * kPointerSize + FixedArray::kHeaderSize;
    // Get the properties array
    __ LoadP(scratch1,
           FieldMemOperand(receiver_reg, JSObject::kPropertiesOffset));
    __ StoreP(value_reg, FieldMemOperand(scratch1, offset), r0);

    if (!FLAG_track_fields || !representation.IsSmi()) {
      // Skip updating write barrier if storing a smi.
      __ JumpIfSmi(value_reg, &exit);

      // Update the write barrier for the array address.
      // Ok to clobber receiver_reg and name_reg, since we return.
      __ mr(name_reg, value_reg);
      __ RecordWriteField(scratch1,
                          offset,
                          name_reg,
                          receiver_reg,
                          kLRHasNotBeenSaved,
                          kDontSaveFPRegs,
                          EMIT_REMEMBERED_SET,
                          smi_check);
    }
  }

  // Return the value (register r3).
  ASSERT(value_reg.is(r3));
  __ bind(&exit);
  __ Ret();
}


void BaseStoreStubCompiler::GenerateRestoreName(MacroAssembler* masm,
                                                Label* label,
                                                Handle<Name> name) {
  if (!label->is_unused()) {
    __ bind(label);
    __ mov(this->name(), Operand(name));
  }
}

static void GenerateCallFunction(MacroAssembler* masm,
                                 Handle<Object> object,
                                 const ParameterCount& arguments,
                                 Label* miss,
                                 Code::ExtraICState extra_ic_state) {
  // ----------- S t a t e -------------
  //  -- r3: receiver
  //  -- r4: function to call
  // -----------------------------------

  // Check that the function really is a function.
  __ JumpIfSmi(r4, miss);
  __ CompareObjectType(r4, r6, r6, JS_FUNCTION_TYPE);
  __ bne(miss);

  // Patch the receiver on the stack with the global proxy if
  // necessary.
  if (object->IsGlobalObject()) {
    __ LoadP(r6, FieldMemOperand(r3, GlobalObject::kGlobalReceiverOffset));
    __ StoreP(r6, MemOperand(sp, arguments.immediate() * kPointerSize), r0);
  }

  // Invoke the function.
  CallKind call_kind = CallICBase::Contextual::decode(extra_ic_state)
      ? CALL_AS_FUNCTION
      : CALL_AS_METHOD;
  __ InvokeFunction(r4, arguments, JUMP_FUNCTION, NullCallWrapper(), call_kind);
}


static void PushInterceptorArguments(MacroAssembler* masm,
                                     Register receiver,
                                     Register holder,
                                     Register name,
                                     Handle<JSObject> holder_obj) {
  __ push(name);
  Handle<InterceptorInfo> interceptor(holder_obj->GetNamedInterceptor());
  ASSERT(!masm->isolate()->heap()->InNewSpace(*interceptor));
  Register scratch = name;
  __ mov(scratch, Operand(interceptor));
  __ push(scratch);
  __ push(receiver);
  __ push(holder);
  __ LoadP(scratch, FieldMemOperand(scratch, InterceptorInfo::kDataOffset));
  __ push(scratch);
  __ mov(scratch, Operand(ExternalReference::isolate_address(masm->isolate())));
  __ push(scratch);
}


static void CompileCallLoadPropertyWithInterceptor(
    MacroAssembler* masm,
    Register receiver,
    Register holder,
    Register name,
    Handle<JSObject> holder_obj) {
  PushInterceptorArguments(masm, receiver, holder, name, holder_obj);

  ExternalReference ref =
      ExternalReference(IC_Utility(IC::kLoadPropertyWithInterceptorOnly),
                        masm->isolate());
  __ li(r3, Operand(6));
  __ mov(r4, Operand(ref));

  CEntryStub stub(1);
  __ CallStub(&stub);
}


static const int kFastApiCallArguments = FunctionCallbackArguments::kArgsLength;

// Reserves space for the extra arguments to API function in the
// caller's frame.
//
// These arguments are set by CheckPrototypes and GenerateFastApiDirectCall.
static void ReserveSpaceForFastApiCall(MacroAssembler* masm,
                                       Register scratch) {
  __ LoadSmiLiteral(scratch, Smi::FromInt(0));
  for (int i = 0; i < kFastApiCallArguments; i++) {
    __ push(scratch);
  }
}


// Undoes the effects of ReserveSpaceForFastApiCall.
static void FreeSpaceForFastApiCall(MacroAssembler* masm) {
  __ Drop(kFastApiCallArguments);
}


static void GenerateFastApiDirectCall(MacroAssembler* masm,
                                      const CallOptimization& optimization,
                                      int argc) {
  // ----------- S t a t e -------------
  //  -- sp[0]              : holder (set by CheckPrototypes)
  //  -- sp[4]              : callee JS function
  //  -- sp[8]              : call data
  //  -- sp[12]             : isolate
  //  -- sp[16]             : ReturnValue default value
  //  -- sp[20]             : ReturnValue
  //  -- sp[24]             : last JS argument
  //  -- ...
  //  -- sp[(argc + 5) * 4] : first JS argument
  //  -- sp[(argc + 6) * 4] : receiver
  // -----------------------------------
  // Get the function and setup the context.
  Handle<JSFunction> function = optimization.constant_function();
  __ LoadHeapObject(r8, function);
  __ LoadP(cp, FieldMemOperand(r8, JSFunction::kContextOffset));

  // Pass the additional arguments.
  Handle<CallHandlerInfo> api_call_info = optimization.api_call_info();
  Handle<Object> call_data(api_call_info->data(), masm->isolate());
  if (masm->isolate()->heap()->InNewSpace(*call_data)) {
    __ Move(r3, api_call_info);
    __ LoadP(r9, FieldMemOperand(r3, CallHandlerInfo::kDataOffset));
  } else {
    __ Move(r9, call_data);
  }
  __ mov(r10, Operand(ExternalReference::isolate_address(masm->isolate())));
  // Store JS function, call data, isolate ReturnValue default and ReturnValue.
  __ StoreP(r8, MemOperand(sp, 1 * kPointerSize));
  __ StoreP(r9, MemOperand(sp, 2 * kPointerSize));
  __ StoreP(r10, MemOperand(sp, 3 * kPointerSize));
  __ LoadRoot(r8, Heap::kUndefinedValueRootIndex);
  __ StoreP(r8, MemOperand(sp, 4 * kPointerSize), r0);
  __ StoreP(r8, MemOperand(sp, 5 * kPointerSize), r0);

  // Prepare arguments.
  __ addi(r5, sp, Operand(5 * kPointerSize));

  Address function_address = v8::ToCData<Address>(api_call_info->callback());
  bool returns_handle =
      !CallbackTable::ReturnsVoid(masm->isolate(), function_address);
#if !ABI_RETURNS_HANDLES_IN_REGS
  bool alloc_return_buf = returns_handle;
#else
  bool alloc_return_buf = false;
#endif

  // Allocate the v8::Arguments structure in the arguments' space since
  // it's not controlled by GC.
  // PPC LINUX ABI:
  //
  // Create 5 or 6 extra slots on stack (depending on alloc_return_buf):
  //    [0] space for DirectCEntryStub's LR save
  //    [1] (optional) space for pointer-sized non-scalar return value (r3)
  //    [2-5] v8:Arguments
  //
  // If alloc_return_buf, we shift the arguments over a register
  // (e.g. r3 -> r4) to allow for the return value buffer in implicit
  // first arg.  CallApiFunctionAndReturn will setup r3.
  int kApiStackSpace = 5 + (alloc_return_buf ? 1 : 0);
  int kArgumentsSlot = kStackFrameExtraParamSlot + (alloc_return_buf ? 2 : 1);
  Register arg0 = alloc_return_buf ? r4 : r3;

  FrameScope frame_scope(masm, StackFrame::MANUAL);
  __ EnterExitFrame(false, kApiStackSpace);

  // scalar and return

  // arg0 = v8::Arguments&
  // Arguments is after the return address.
  __ addi(arg0, sp, Operand(kArgumentsSlot * kPointerSize));
  // v8::Arguments::implicit_args_
  __ StoreP(r5, MemOperand(arg0, 0 * kPointerSize));
  // v8::Arguments::values_
  __ addi(ip, r5, Operand(argc * kPointerSize));
  __ StoreP(ip, MemOperand(arg0, 1 * kPointerSize));
  // v8::Arguments::length_ = argc
  __ li(ip, Operand(argc));
  __ stw(ip, MemOperand(arg0, 2 * kPointerSize));
  // v8::Arguments::is_construct_call = 0
  __ li(ip, Operand::Zero());
  __ stw(ip, MemOperand(arg0, 2 * kPointerSize + kIntSize));

  const int kStackUnwindSpace = argc + kFastApiCallArguments + 1;
  ApiFunction fun(function_address);
  ExternalReference::Type type =
      returns_handle ?
          ExternalReference::DIRECT_API_CALL :
          ExternalReference::DIRECT_API_CALL_NEW;
  ExternalReference ref = ExternalReference(&fun,
                                            type,
                                            masm->isolate());
  Address thunk_address = returns_handle
      ? FUNCTION_ADDR(&InvokeInvocationCallback)
      : FUNCTION_ADDR(&InvokeFunctionCallback);
  ExternalReference::Type thunk_type =
      returns_handle ?
          ExternalReference::PROFILING_API_CALL :
          ExternalReference::PROFILING_API_CALL_NEW;
  ApiFunction thunk_fun(thunk_address);
  ExternalReference thunk_ref = ExternalReference(&thunk_fun, thunk_type,
      masm->isolate());
  Register thunk_arg = { arg0.code() + 1 };

  AllowExternalCallThatCantCauseGC scope(masm);
  __ CallApiFunctionAndReturn(ref,
                              function_address,
                              thunk_ref,
                              thunk_arg,
                              kStackUnwindSpace,
                              returns_handle,
                              kFastApiCallArguments + 1);
}


class CallInterceptorCompiler BASE_EMBEDDED {
 public:
  CallInterceptorCompiler(StubCompiler* stub_compiler,
                          const ParameterCount& arguments,
                          Register name,
                          Code::ExtraICState extra_ic_state)
      : stub_compiler_(stub_compiler),
        arguments_(arguments),
        name_(name),
        extra_ic_state_(extra_ic_state) {}

  void Compile(MacroAssembler* masm,
               Handle<JSObject> object,
               Handle<JSObject> holder,
               Handle<Name> name,
               LookupResult* lookup,
               Register receiver,
               Register scratch1,
               Register scratch2,
               Register scratch3,
               Label* miss) {
    ASSERT(holder->HasNamedInterceptor());
    ASSERT(!holder->GetNamedInterceptor()->getter()->IsUndefined());

    // Check that the receiver isn't a smi.
    __ JumpIfSmi(receiver, miss);
    CallOptimization optimization(lookup);
    if (optimization.is_constant_call()) {
      CompileCacheable(masm, object, receiver, scratch1, scratch2, scratch3,
                       holder, lookup, name, optimization, miss);
    } else {
      CompileRegular(masm, object, receiver, scratch1, scratch2, scratch3,
                     name, holder, miss);
    }
  }

 private:
  void CompileCacheable(MacroAssembler* masm,
                        Handle<JSObject> object,
                        Register receiver,
                        Register scratch1,
                        Register scratch2,
                        Register scratch3,
                        Handle<JSObject> interceptor_holder,
                        LookupResult* lookup,
                        Handle<Name> name,
                        const CallOptimization& optimization,
                        Label* miss_label) {
    ASSERT(optimization.is_constant_call());
    ASSERT(!lookup->holder()->IsGlobalObject());
    Counters* counters = masm->isolate()->counters();
    int depth1 = kInvalidProtoDepth;
    int depth2 = kInvalidProtoDepth;
    bool can_do_fast_api_call = false;
    if (optimization.is_simple_api_call() &&
        !lookup->holder()->IsGlobalObject()) {
      depth1 = optimization.GetPrototypeDepthOfExpectedType(
          object, interceptor_holder);
      if (depth1 == kInvalidProtoDepth) {
        depth2 = optimization.GetPrototypeDepthOfExpectedType(
            interceptor_holder, Handle<JSObject>(lookup->holder()));
      }
      can_do_fast_api_call =
          depth1 != kInvalidProtoDepth || depth2 != kInvalidProtoDepth;
    }

    __ IncrementCounter(counters->call_const_interceptor(), 1,
                        scratch1, scratch2);

    if (can_do_fast_api_call) {
      __ IncrementCounter(counters->call_const_interceptor_fast_api(), 1,
                          scratch1, scratch2);
      ReserveSpaceForFastApiCall(masm, scratch1);
    }

    // Check that the maps from receiver to interceptor's holder
    // haven't changed and thus we can invoke interceptor.
    Label miss_cleanup;
    Label* miss = can_do_fast_api_call ? &miss_cleanup : miss_label;
    Register holder =
        stub_compiler_->CheckPrototypes(object, receiver, interceptor_holder,
                                        scratch1, scratch2, scratch3,
                                        name, depth1, miss);

    // Invoke an interceptor and if it provides a value,
    // branch to |regular_invoke|.
    Label regular_invoke;
    LoadWithInterceptor(masm, receiver, holder, interceptor_holder, scratch2,
                        &regular_invoke);

    // Interceptor returned nothing for this property.  Try to use cached
    // constant function.

    // Check that the maps from interceptor's holder to constant function's
    // holder haven't changed and thus we can use cached constant function.
    if (*interceptor_holder != lookup->holder()) {
      stub_compiler_->CheckPrototypes(interceptor_holder, receiver,
                                      Handle<JSObject>(lookup->holder()),
                                      scratch1, scratch2, scratch3,
                                      name, depth2, miss);
    } else {
      // CheckPrototypes has a side effect of fetching a 'holder'
      // for API (object which is instanceof for the signature).  It's
      // safe to omit it here, as if present, it should be fetched
      // by the previous CheckPrototypes.
      ASSERT(depth2 == kInvalidProtoDepth);
    }

    // Invoke function.
    if (can_do_fast_api_call) {
      GenerateFastApiDirectCall(masm, optimization, arguments_.immediate());
    } else {
      CallKind call_kind = CallICBase::Contextual::decode(extra_ic_state_)
          ? CALL_AS_FUNCTION
          : CALL_AS_METHOD;
      Handle<JSFunction> function = optimization.constant_function();
      ParameterCount expected(function);
      __ InvokeFunction(function, expected, arguments_,
                        JUMP_FUNCTION, NullCallWrapper(), call_kind);
    }

    // Deferred code for fast API call case---clean preallocated space.
    if (can_do_fast_api_call) {
      __ bind(&miss_cleanup);
      FreeSpaceForFastApiCall(masm);
      __ b(miss_label);
    }

    // Invoke a regular function.
    __ bind(&regular_invoke);
    if (can_do_fast_api_call) {
      FreeSpaceForFastApiCall(masm);
    }
  }

  void CompileRegular(MacroAssembler* masm,
                      Handle<JSObject> object,
                      Register receiver,
                      Register scratch1,
                      Register scratch2,
                      Register scratch3,
                      Handle<Name> name,
                      Handle<JSObject> interceptor_holder,
                      Label* miss_label) {
    Register holder =
        stub_compiler_->CheckPrototypes(object, receiver, interceptor_holder,
                                        scratch1, scratch2, scratch3,
                                        name, miss_label);

    // Call a runtime function to load the interceptor property.
    FrameScope scope(masm, StackFrame::INTERNAL);
    // Save the name_ register across the call.
    __ push(name_);
    PushInterceptorArguments(masm, receiver, holder, name_, interceptor_holder);
    __ CallExternalReference(
        ExternalReference(IC_Utility(IC::kLoadPropertyWithInterceptorForCall),
                          masm->isolate()),
        6);
    // Restore the name_ register.
    __ pop(name_);
    // Leave the internal frame.
  }

  void LoadWithInterceptor(MacroAssembler* masm,
                           Register receiver,
                           Register holder,
                           Handle<JSObject> holder_obj,
                           Register scratch,
                           Label* interceptor_succeeded) {
    {
      FrameScope scope(masm, StackFrame::INTERNAL);
      __ Push(holder, name_);
      CompileCallLoadPropertyWithInterceptor(masm,
                                             receiver,
                                             holder,
                                             name_,
                                             holder_obj);
      __ pop(name_);  // Restore the name.
      __ pop(receiver);  // Restore the holder.
    }
    // If interceptor returns no-result sentinel, call the constant function.
    __ LoadRoot(scratch, Heap::kNoInterceptorResultSentinelRootIndex);
    __ cmp(r3, scratch);
    __ bne(interceptor_succeeded);
  }

  StubCompiler* stub_compiler_;
  const ParameterCount& arguments_;
  Register name_;
  Code::ExtraICState extra_ic_state_;
};


// Calls GenerateCheckPropertyCell for each global object in the prototype chain
// from object to (but not including) holder.
static void GenerateCheckPropertyCells(MacroAssembler* masm,
                                       Handle<JSObject> object,
                                       Handle<JSObject> holder,
                                       Handle<Name> name,
                                       Register scratch,
                                       Label* miss) {
  Handle<JSObject> current = object;
  while (!current.is_identical_to(holder)) {
    if (current->IsGlobalObject()) {
      GenerateCheckPropertyCell(masm,
                                Handle<GlobalObject>::cast(current),
                                name,
                                scratch,
                                miss);
    }
    current = Handle<JSObject>(JSObject::cast(current->GetPrototype()));
  }
}

#if 0  // Unused
// Convert and store int passed in register ival to IEEE 754 single precision
// floating point value at memory location (dst + 4 * wordoffset)
// If VFP3 is available use it for conversion.
static void StoreIntAsFloat(MacroAssembler* masm,
                            Register dst,
                            Register wordoffset,
                            Register ival,
                            Register scratch1) {
  __ vmov(s0, ival);
  __ add(scratch1, dst, Operand(wordoffset, LSL, 2));
  __ vcvt_f32_s32(s0, s0);
  __ vstr(s0, scratch1, 0);
}
#endif


void StubCompiler::GenerateTailCall(MacroAssembler* masm, Handle<Code> code) {
  __ Jump(code, RelocInfo::CODE_TARGET);
}


#undef __
#define __ ACCESS_MASM(masm())


Register StubCompiler::CheckPrototypes(Handle<JSObject> object,
                                       Register object_reg,
                                       Handle<JSObject> holder,
                                       Register holder_reg,
                                       Register scratch1,
                                       Register scratch2,
                                       Handle<Name> name,
                                       int save_at_depth,
                                       Label* miss,
                                       PrototypeCheckType check) {
  // Make sure that the type feedback oracle harvests the receiver map.
  // TODO(svenpanne) Remove this hack when all ICs are reworked.
  __ mov(scratch1, Operand(Handle<Map>(object->map())));

  Handle<JSObject> first = object;
  // Make sure there's no overlap between holder and object registers.
  ASSERT(!scratch1.is(object_reg) && !scratch1.is(holder_reg));
  ASSERT(!scratch2.is(object_reg) && !scratch2.is(holder_reg)
         && !scratch2.is(scratch1));

  // Keep track of the current object in register reg.
  Register reg = object_reg;
  int depth = 0;

  if (save_at_depth == depth) {
    __ StoreP(reg, MemOperand(sp));
  }

  // Check the maps in the prototype chain.
  // Traverse the prototype chain from the object and do map checks.
  Handle<JSObject> current = object;
  while (!current.is_identical_to(holder)) {
    ++depth;

    // Only global objects and objects that do not require access
    // checks are allowed in stubs.
    ASSERT(current->IsJSGlobalProxy() || !current->IsAccessCheckNeeded());

    Handle<JSObject> prototype(JSObject::cast(current->GetPrototype()));
    if (!current->HasFastProperties() &&
        !current->IsJSGlobalObject() &&
        !current->IsJSGlobalProxy()) {
      if (!name->IsUniqueName()) {
        ASSERT(name->IsString());
        name = factory()->InternalizeString(Handle<String>::cast(name));
      }
      ASSERT(current->property_dictionary()->FindEntry(*name) ==
             NameDictionary::kNotFound);

      GenerateDictionaryNegativeLookup(masm(), miss, reg, name,
                                       scratch1, scratch2);

      __ LoadP(scratch1, FieldMemOperand(reg, HeapObject::kMapOffset));
      reg = holder_reg;  // From now on the object will be in holder_reg.
      __ LoadP(reg, FieldMemOperand(scratch1, Map::kPrototypeOffset));
    } else {
      Register map_reg = scratch1;
      if (!current.is_identical_to(first) || check == CHECK_ALL_MAPS) {
        Handle<Map> current_map(current->map());
        // CheckMap implicitly loads the map of |reg| into |map_reg|.
        __ CheckMap(reg, map_reg, current_map, miss, DONT_DO_SMI_CHECK);
      } else {
        __ LoadP(map_reg, FieldMemOperand(reg, HeapObject::kMapOffset));
      }

      // Check access rights to the global object.  This has to happen after
      // the map check so that we know that the object is actually a global
      // object.
      if (current->IsJSGlobalProxy()) {
        __ CheckAccessGlobalProxy(reg, scratch2, miss);
      }
      reg = holder_reg;  // From now on the object will be in holder_reg.

      if (heap()->InNewSpace(*prototype)) {
        // The prototype is in new space; we cannot store a reference to it
        // in the code.  Load it from the map.
        __ LoadP(reg, FieldMemOperand(map_reg, Map::kPrototypeOffset));
      } else {
        // The prototype is in old space; load it directly.
        __ mov(reg, Operand(prototype));
      }
    }

    if (save_at_depth == depth) {
      __ StoreP(reg, MemOperand(sp));
    }

    // Go to the next object in the prototype chain.
    current = prototype;
  }

  // Log the check depth.
  LOG(isolate(), IntEvent("check-maps-depth", depth + 1));

  if (!holder.is_identical_to(first) || check == CHECK_ALL_MAPS) {
    // Check the holder map.
    __ CheckMap(reg, scratch1, Handle<Map>(holder->map()), miss,
                DONT_DO_SMI_CHECK);
  }

  // Perform security check for access to the global object.
  ASSERT(holder->IsJSGlobalProxy() || !holder->IsAccessCheckNeeded());
  if (holder->IsJSGlobalProxy()) {
    __ CheckAccessGlobalProxy(reg, scratch1, miss);
  }

  // If we've skipped any global objects, it's not enough to verify that
  // their maps haven't changed.  We also need to check that the property
  // cell for the property is still empty.
  GenerateCheckPropertyCells(masm(), object, holder, name, scratch1, miss);

  // Return the register containing the holder.
  return reg;
}


void BaseLoadStubCompiler::HandlerFrontendFooter(Handle<Name> name,
                                                 Label* success,
                                                 Label* miss) {
  if (!miss->is_unused()) {
    __ b(success);
    __ bind(miss);
    TailCallBuiltin(masm(), MissBuiltin(kind()));
  }
}


void BaseStoreStubCompiler::HandlerFrontendFooter(Handle<Name> name,
                                                  Label* success,
                                                  Label* miss) {
  if (!miss->is_unused()) {
    __ b(success);
    GenerateRestoreName(masm(), miss, name);
    TailCallBuiltin(masm(), MissBuiltin(kind()));
  }
}


Register BaseLoadStubCompiler::CallbackHandlerFrontend(
    Handle<JSObject> object,
    Register object_reg,
    Handle<JSObject> holder,
    Handle<Name> name,
    Label* success,
    Handle<ExecutableAccessorInfo> callback) {
  Label miss;

  Register reg = HandlerFrontendHeader(object, object_reg, holder, name, &miss);

  if (!holder->HasFastProperties() && !holder->IsJSGlobalObject()) {
    ASSERT(!reg.is(scratch2()));
    ASSERT(!reg.is(scratch3()));
    ASSERT(!reg.is(scratch4()));

    // Load the properties dictionary.
    Register dictionary = scratch4();
    __ LoadP(dictionary, FieldMemOperand(reg, JSObject::kPropertiesOffset));

    // Probe the dictionary.
    Label probe_done;
    NameDictionaryLookupStub::GeneratePositiveLookup(masm(),
                                                     &miss,
                                                     &probe_done,
                                                     dictionary,
                                                     this->name(),
                                                     scratch2(),
                                                     scratch3());
    __ bind(&probe_done);

    // If probing finds an entry in the dictionary, scratch3 contains the
    // pointer into the dictionary. Check that the value is the callback.
    Register pointer = scratch3();
    const int kElementsStartOffset = NameDictionary::kHeaderSize +
        NameDictionary::kElementsStartIndex * kPointerSize;
    const int kValueOffset = kElementsStartOffset + kPointerSize;
    __ LoadP(scratch2(), FieldMemOperand(pointer, kValueOffset));
    __ mov(scratch3(), Operand(callback));
    __ cmp(scratch2(), scratch3());
    __ bne(&miss);
  }

  HandlerFrontendFooter(name, success, &miss);
  return reg;
}


void BaseLoadStubCompiler::NonexistentHandlerFrontend(
    Handle<JSObject> object,
    Handle<JSObject> last,
    Handle<Name> name,
    Label* success,
    Handle<GlobalObject> global) {
  Label miss;

  HandlerFrontendHeader(object, receiver(), last, name, &miss);

  // If the last object in the prototype chain is a global object,
  // check that the global property cell is empty.
  if (!global.is_null()) {
    GenerateCheckPropertyCell(masm(), global, name, scratch2(), &miss);
  }

  HandlerFrontendFooter(name, success, &miss);
}


void BaseLoadStubCompiler::GenerateLoadField(Register reg,
                                             Handle<JSObject> holder,
                                             PropertyIndex field,
                                             Representation representation) {
  if (!reg.is(receiver())) __ mr(receiver(), reg);
  if (kind() == Code::LOAD_IC) {
    LoadFieldStub stub(field.is_inobject(holder),
                       field.translate(holder),
                       representation);
    GenerateTailCall(masm(), stub.GetCode(isolate()));
  } else {
    KeyedLoadFieldStub stub(field.is_inobject(holder),
                            field.translate(holder),
                            representation);
    GenerateTailCall(masm(), stub.GetCode(isolate()));
  }
}


void BaseLoadStubCompiler::GenerateLoadConstant(Handle<Object> value) {
  // Return the constant value.
  __ LoadObject(r3, value);
  __ Ret();
}

void BaseLoadStubCompiler::GenerateLoadCallback(
    Register reg,
    Handle<ExecutableAccessorInfo> callback) {
  // Build AccessorInfo::args_ list on the stack and push property name below
  // the exit frame to make GC aware of them and store pointers to them.
  __ push(receiver());
  __ mr(scratch2(), sp);  // scratch2 = AccessorInfo::args_
  if (heap()->InNewSpace(callback->data())) {
    __ Move(scratch3(), callback);
    __ LoadP(scratch3(), FieldMemOperand(scratch3(),
                                       ExecutableAccessorInfo::kDataOffset));
  } else {
    __ Move(scratch3(), Handle<Object>(callback->data(), isolate()));
  }
  __ Push(reg, scratch3());
  __ LoadRoot(scratch3(), Heap::kUndefinedValueRootIndex);
  __ mr(scratch4(), scratch3());
  __ Push(scratch3(), scratch4());
  __ mov(scratch4(),
         Operand(ExternalReference::isolate_address(isolate())));
  __ Push(scratch4(), name());

  Address getter_address = v8::ToCData<Address>(callback->getter());
  bool returns_handle =
      !CallbackTable::ReturnsVoid(isolate(), getter_address);
#if !ABI_RETURNS_HANDLES_IN_REGS
  bool alloc_return_buf = returns_handle;
#else
  bool alloc_return_buf = false;
#endif

  // If ABI passes Handles (pointer-sized struct) in a register:
  //
  // Create 2 or 3 extra slots on stack (depending on alloc_return_buf):
  //    [0] space for DirectCEntryStub's LR save
  //    [1] (optional) space for pointer-sized non-scalar return value (r3)
  //    [2] AccessorInfo
  //
  // Otherwise:
  //
  // Create 3 or 4 extra slots on stack (depending on alloc_return_buf):
  //    [0] space for DirectCEntryStub's LR save
  //    [1] (optional) space for pointer-sized non-scalar return value (r3)
  //    [2] copy of Handle (first arg)
  //    [3] AccessorInfo
  //
  // If alloc_return_buf, we shift the arguments over a register
  // (e.g. r3 -> r4) to allow for the return value buffer in implicit
  // first arg.  CallApiFunctionAndReturn will setup r3.
#if ABI_PASSES_HANDLES_IN_REGS
  int kAccessorInfoSlot = kStackFrameExtraParamSlot +
    (alloc_return_buf ? 2 : 1);
#else
  int kArg0Slot = kStackFrameExtraParamSlot + (alloc_return_buf ? 2 : 1);
  int kAccessorInfoSlot = kArg0Slot + 1;
#endif
  int kApiStackSpace = kAccessorInfoSlot - kStackFrameExtraParamSlot + 1;
  Register arg0 = (alloc_return_buf ? r4 : r3);
  Register arg1 = (alloc_return_buf ? r5 : r4);

  __ mr(arg1, scratch2());  // Saved in case scratch2 == arg0.
  __ mr(arg0, sp);  // arg0 = Handle<Name>

  FrameScope frame_scope(masm(), StackFrame::MANUAL);
  __ EnterExitFrame(false, kApiStackSpace);

#if !ABI_PASSES_HANDLES_IN_REGS
  // pass 1st arg by reference
  __ StoreP(arg0,
            MemOperand(sp, kArg0Slot * kPointerSize));
  __ addi(arg0, sp, Operand(kArg0Slot * kPointerSize));
#endif

  // Create AccessorInfo instance on the stack above the exit frame with
  // scratch2 (internal::Object** args_) as the data.
  __ StoreP(arg1, MemOperand(sp, kAccessorInfoSlot * kPointerSize));
  // arg1 = AccessorInfo&
  __ addi(arg1, sp, Operand(kAccessorInfoSlot * kPointerSize));

  const int kStackUnwindSpace = kFastApiCallArguments + 1;

  ApiFunction fun(getter_address);
  ExternalReference::Type type =
      returns_handle ?
          ExternalReference::DIRECT_GETTER_CALL :
          ExternalReference::DIRECT_GETTER_CALL_NEW;
  ExternalReference ref = ExternalReference(&fun, type, isolate());

  Address thunk_address = returns_handle
      ? FUNCTION_ADDR(&InvokeAccessorGetter)
      : FUNCTION_ADDR(&InvokeAccessorGetterCallback);
  ExternalReference::Type thunk_type =
      returns_handle ?
          ExternalReference::PROFILING_GETTER_CALL :
          ExternalReference::PROFILING_GETTER_CALL_NEW;
  ApiFunction thunk_fun(thunk_address);
  ExternalReference thunk_ref = ExternalReference(&thunk_fun, thunk_type,
      isolate());
  Register thunk_arg = { arg1.code() + 1 };
  __ CallApiFunctionAndReturn(ref,
                              getter_address,
                              thunk_ref,
                              thunk_arg,
                              kStackUnwindSpace,
                              returns_handle,
                              5);
}


void BaseLoadStubCompiler::GenerateLoadInterceptor(
    Register holder_reg,
    Handle<JSObject> object,
    Handle<JSObject> interceptor_holder,
    LookupResult* lookup,
    Handle<Name> name) {
  ASSERT(interceptor_holder->HasNamedInterceptor());
  ASSERT(!interceptor_holder->GetNamedInterceptor()->getter()->IsUndefined());

  // So far the most popular follow ups for interceptor loads are FIELD
  // and CALLBACKS, so inline only them, other cases may be added
  // later.
  bool compile_followup_inline = false;
  if (lookup->IsFound() && lookup->IsCacheable()) {
    if (lookup->IsField()) {
      compile_followup_inline = true;
    } else if (lookup->type() == CALLBACKS &&
               lookup->GetCallbackObject()->IsExecutableAccessorInfo()) {
      ExecutableAccessorInfo* callback =
          ExecutableAccessorInfo::cast(lookup->GetCallbackObject());
      compile_followup_inline = callback->getter() != NULL &&
          callback->IsCompatibleReceiver(*object);
    }
  }

  if (compile_followup_inline) {
    // Compile the interceptor call, followed by inline code to load the
    // property from further up the prototype chain if the call fails.
    // Check that the maps haven't changed.
    ASSERT(holder_reg.is(receiver()) || holder_reg.is(scratch1()));

    // Preserve the receiver register explicitly whenever it is different from
    // the holder and it is needed should the interceptor return without any
    // result. The CALLBACKS case needs the receiver to be passed into C++ code,
    // the FIELD case might cause a miss during the prototype check.
    bool must_perfrom_prototype_check = *interceptor_holder != lookup->holder();
    bool must_preserve_receiver_reg = !receiver().is(holder_reg) &&
        (lookup->type() == CALLBACKS || must_perfrom_prototype_check);

    // Save necessary data before invoking an interceptor.
    // Requires a frame to make GC aware of pushed pointers.
    {
      FrameScope frame_scope(masm(), StackFrame::INTERNAL);
      if (must_preserve_receiver_reg) {
        __ Push(receiver(), holder_reg, this->name());
      } else {
        __ Push(holder_reg, this->name());
      }
      // Invoke an interceptor.  Note: map checks from receiver to
      // interceptor's holder has been compiled before (see a caller
      // of this method.)
      CompileCallLoadPropertyWithInterceptor(masm(),
                                             receiver(),
                                             holder_reg,
                                             this->name(),
                                             interceptor_holder);
      // Check if interceptor provided a value for property.  If it's
      // the case, return immediately.
      Label interceptor_failed;
      __ LoadRoot(scratch1(), Heap::kNoInterceptorResultSentinelRootIndex);
      __ cmp(r3, scratch1());
      __ beq(&interceptor_failed);
      frame_scope.GenerateLeaveFrame();
      __ Ret();

      __ bind(&interceptor_failed);
      __ pop(this->name());
      __ pop(holder_reg);
      if (must_preserve_receiver_reg) {
        __ pop(receiver());
      }
      // Leave the internal frame.
    }

    GenerateLoadPostInterceptor(holder_reg, interceptor_holder, name, lookup);
  } else {  // !compile_followup_inline
    // Call the runtime system to load the interceptor.
    // Check that the maps haven't changed.
    PushInterceptorArguments(masm(), receiver(), holder_reg,
                             this->name(), interceptor_holder);

    ExternalReference ref =
        ExternalReference(IC_Utility(IC::kLoadPropertyWithInterceptorForLoad),
                          isolate());
    __ TailCallExternalReference(ref, 6, 1);
  }
}


void CallStubCompiler::GenerateNameCheck(Handle<Name> name, Label* miss) {
  if (kind_ == Code::KEYED_CALL_IC) {
    __ Cmpi(r5, Operand(name), r0);
    __ bne(miss);
  }
}


void CallStubCompiler::GenerateGlobalReceiverCheck(Handle<JSObject> object,
                                                   Handle<JSObject> holder,
                                                   Handle<Name> name,
                                                   Label* miss) {
  ASSERT(holder->IsGlobalObject());

  // Get the number of arguments.
  const int argc = arguments().immediate();

  // Get the receiver from the stack.
  __ LoadP(r3, MemOperand(sp, argc * kPointerSize), r0);

  // Check that the maps haven't changed.
  __ JumpIfSmi(r3, miss);
  CheckPrototypes(object, r3, holder, r6, r4, r7, name, miss);
}


void CallStubCompiler::GenerateLoadFunctionFromCell(
    Handle<Cell> cell,
    Handle<JSFunction> function,
    Label* miss) {
  // Get the value from the cell.
  __ mov(r6, Operand(cell));
  __ LoadP(r4, FieldMemOperand(r6, Cell::kValueOffset));

  // Check that the cell contains the same function.
  if (heap()->InNewSpace(*function)) {
    // We can't embed a pointer to a function in new space so we have
    // to verify that the shared function info is unchanged. This has
    // the nice side effect that multiple closures based on the same
    // function can all use this call IC. Before we load through the
    // function, we have to verify that it still is a function.
    __ JumpIfSmi(r4, miss);
    __ CompareObjectType(r4, r6, r6, JS_FUNCTION_TYPE);
    __ bne(miss);

    // Check the shared function info. Make sure it hasn't changed.
    __ Move(r6, Handle<SharedFunctionInfo>(function->shared()));
    __ LoadP(r7, FieldMemOperand(r4, JSFunction::kSharedFunctionInfoOffset));
    __ cmp(r7, r6);
  } else {
    __ mov(r6, Operand(function));
    __ cmp(r4, r6);
  }
  __ bne(miss);
}


void CallStubCompiler::GenerateMissBranch() {
  Handle<Code> code =
      isolate()->stub_cache()->ComputeCallMiss(arguments().immediate(),
                                               kind_,
                                               extra_state_);
  __ Jump(code, RelocInfo::CODE_TARGET);
}


Handle<Code> CallStubCompiler::CompileCallField(Handle<JSObject> object,
                                                Handle<JSObject> holder,
                                                PropertyIndex index,
                                                Handle<Name> name) {
  // ----------- S t a t e -------------
  //  -- r5    : name
  //  -- lr    : return address
  // -----------------------------------
  Label miss;

  GenerateNameCheck(name, &miss);

  const int argc = arguments().immediate();

  // Get the receiver of the function from the stack into r3.
  __ LoadP(r3, MemOperand(sp, argc * kPointerSize), r0);
  // Check that the receiver isn't a smi.
  __ JumpIfSmi(r3, &miss);

  // Do the right check and compute the holder register.
  Register reg = CheckPrototypes(object, r3, holder, r4, r6, r7, name, &miss);
  GenerateFastPropertyLoad(masm(), r4, reg, index.is_inobject(holder),
                           index.translate(holder), Representation::Tagged());

  GenerateCallFunction(masm(), object, arguments(), &miss, extra_state_);

  // Handle call cache miss.
  __ bind(&miss);
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(Code::FIELD, name);
}


Handle<Code> CallStubCompiler::CompileArrayCodeCall(
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<Cell> cell,
    Handle<JSFunction> function,
    Handle<String> name,
    Code::StubType type) {
  Label miss;

  // Check that function is still array
  const int argc = arguments().immediate();
  GenerateNameCheck(name, &miss);
  Register receiver = r4;

  if (cell.is_null()) {
    __ LoadP(receiver, MemOperand(sp, argc * kPointerSize));

    // Check that the receiver isn't a smi.
    __ JumpIfSmi(receiver, &miss);

    // Check that the maps haven't changed.
    CheckPrototypes(Handle<JSObject>::cast(object), receiver, holder, r6, r3,
                    r7, name, &miss);
  } else {
    ASSERT(cell->value() == *function);
    GenerateGlobalReceiverCheck(Handle<JSObject>::cast(object), holder, name,
                                &miss);
    GenerateLoadFunctionFromCell(cell, function, &miss);
  }

  Handle<AllocationSite> site = isolate()->factory()->NewAllocationSite();
  site->set_transition_info(Smi::FromInt(GetInitialFastElementsKind()));
  Handle<Cell> site_feedback_cell = isolate()->factory()->NewCell(site);
  __ mov(r3, Operand(argc));
  __ mov(r5, Operand(site_feedback_cell));
  __ mov(r4, Operand(function));

  ArrayConstructorStub stub(isolate());
  __ TailCallStub(&stub);

  __ bind(&miss);
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(type, name);
}


Handle<Code> CallStubCompiler::CompileArrayPushCall(
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<Cell> cell,
    Handle<JSFunction> function,
    Handle<String> name,
    Code::StubType type) {
  // ----------- S t a t e -------------
  //  -- r5    : name
  //  -- lr    : return address
  //  -- sp[(argc - n - 1) * 4] : arg[n] (zero-based)
  //  -- ...
  //  -- sp[argc * 4]           : receiver
  // -----------------------------------

  // If object is not an array, bail out to regular call.
  if (!object->IsJSArray() || !cell.is_null()) return Handle<Code>::null();

  Label miss;
  GenerateNameCheck(name, &miss);

  Register receiver = r4;
  // Get the receiver from the stack
  const int argc = arguments().immediate();
  __ LoadP(receiver, MemOperand(sp, argc * kPointerSize), r0);

  // Check that the receiver isn't a smi.
  __ JumpIfSmi(receiver, &miss);

  // Check that the maps haven't changed.
  CheckPrototypes(Handle<JSObject>::cast(object), receiver, holder, r6, r3, r7,
                  name, &miss);

  if (argc == 0) {
    // Nothing to do, just return the length.
    __ LoadP(r3, FieldMemOperand(receiver, JSArray::kLengthOffset));
    __ Drop(argc + 1);
    __ Ret();
  } else {
    Label call_builtin;

    if (argc == 1) {  // Otherwise fall through to call the builtin.
      Label attempt_to_grow_elements, with_write_barrier, check_double;

      Register elements = r9;
      Register end_elements = r8;
      // Get the elements array of the object.
      __ LoadP(elements, FieldMemOperand(receiver, JSArray::kElementsOffset));

      // Check that the elements are in fast mode and writable.
      __ CheckMap(elements,
                  r3,
                  Heap::kFixedArrayMapRootIndex,
                  &check_double,
                  DONT_DO_SMI_CHECK);


      // Get the array's length into r3 and calculate new length.
      __ LoadP(r3, FieldMemOperand(receiver, JSArray::kLengthOffset));
      __ AddSmiLiteral(r3, r3, Smi::FromInt(argc), r0);

      // Get the elements' length.
      __ LoadP(r7, FieldMemOperand(elements, FixedArray::kLengthOffset));

      // Check if we could survive without allocation.
      __ cmp(r3, r7);
      __ bgt(&attempt_to_grow_elements);

      // Check if value is a smi.
      __ LoadP(r7, MemOperand(sp, (argc - 1) * kPointerSize), r0);
      __ JumpIfNotSmi(r7, &with_write_barrier);

      // Save new length.
      __ StoreP(r3, FieldMemOperand(receiver, JSArray::kLengthOffset), r0);

      // Store the value.
      // We may need a register containing the address end_elements below,
      // so write back the value in end_elements.
      __ SmiToPtrArrayOffset(end_elements, r3);
      __ add(end_elements, elements, end_elements);
      const int kEndElementsOffset =
          FixedArray::kHeaderSize - kHeapObjectTag - argc * kPointerSize;
      __ Add(end_elements, end_elements, kEndElementsOffset, r0);
      __ StoreP(r7, MemOperand(end_elements));

      // Check for a smi.
      __ Drop(argc + 1);
      __ Ret();

      __ bind(&check_double);

      // Check that the elements are in fast mode and writable.
      __ CheckMap(elements,
                  r3,
                  Heap::kFixedDoubleArrayMapRootIndex,
                  &call_builtin,
                  DONT_DO_SMI_CHECK);

      // Get the array's length into r3 and calculate new length.
      __ LoadP(r3, FieldMemOperand(receiver, JSArray::kLengthOffset));
      __ AddSmiLiteral(r3, r3, Smi::FromInt(argc), r0);

      // Get the elements' length.
      __ LoadP(r7, FieldMemOperand(elements, FixedArray::kLengthOffset));

      // Check if we could survive without allocation.
      __ cmp(r3, r7);
      __ bgt(&call_builtin);

      __ LoadP(r7, MemOperand(sp, (argc - 1) * kPointerSize));
      __ StoreNumberToDoubleElements(r7, r3, elements, r8, d0,
                                     &call_builtin, argc * kDoubleSize);

      // Save new length.
      __ StoreP(r3, FieldMemOperand(receiver, JSArray::kLengthOffset), r0);

      // Check for a smi.
      __ Drop(argc + 1);
      __ Ret();

      __ bind(&with_write_barrier);

      __ LoadP(r6, FieldMemOperand(receiver, HeapObject::kMapOffset));

      if (FLAG_smi_only_arrays  && !FLAG_trace_elements_transitions) {
        Label fast_object, not_fast_object;
        __ CheckFastObjectElements(r6, r10, &not_fast_object);
        __ b(&fast_object);
        // In case of fast smi-only, convert to fast object, otherwise bail out.
        __ bind(&not_fast_object);
        __ CheckFastSmiElements(r6, r10, &call_builtin);

        __ LoadP(r10, FieldMemOperand(r7, HeapObject::kMapOffset));
        __ LoadRoot(ip, Heap::kHeapNumberMapRootIndex);
        __ cmp(r10, ip);
        __ beq(&call_builtin);
        // r4: receiver
        // r6: map
        Label try_holey_map;
        __ LoadTransitionedArrayMapConditional(FAST_SMI_ELEMENTS,
                                               FAST_ELEMENTS,
                                               r6,
                                               r10,
                                               &try_holey_map);
        __ mr(r5, receiver);
        ElementsTransitionGenerator::
            GenerateMapChangeElementsTransition(masm(),
                                                DONT_TRACK_ALLOCATION_SITE,
                                                NULL);
        __ b(&fast_object);

        __ bind(&try_holey_map);
        __ LoadTransitionedArrayMapConditional(FAST_HOLEY_SMI_ELEMENTS,
                                               FAST_HOLEY_ELEMENTS,
                                               r6,
                                               r10,
                                               &call_builtin);
        __ mr(r5, receiver);
        ElementsTransitionGenerator::
            GenerateMapChangeElementsTransition(masm(),
                                                DONT_TRACK_ALLOCATION_SITE,
                                                NULL);
        __ bind(&fast_object);
      } else {
        __ CheckFastObjectElements(r6, r6, &call_builtin);
      }

      // Save new length.
      __ StoreP(r3, FieldMemOperand(receiver, JSArray::kLengthOffset), r0);

      // Store the value.
      // We may need a register containing the address end_elements below,
      // so write back the value in end_elements.
      __ SmiToPtrArrayOffset(end_elements, r3);
      __ add(end_elements, elements, end_elements);
      __ Add(end_elements, end_elements, kEndElementsOffset, r0);
      __ StoreP(r7, MemOperand(end_elements));

      __ RecordWrite(elements,
                     end_elements,
                     r7,
                     kLRHasNotBeenSaved,
                     kDontSaveFPRegs,
                     EMIT_REMEMBERED_SET,
                     OMIT_SMI_CHECK);
      __ Drop(argc + 1);
      __ Ret();

      __ bind(&attempt_to_grow_elements);
      // r3: array's length + 1.
      // r7: elements' length.

      if (!FLAG_inline_new) {
        __ b(&call_builtin);
      }

      __ LoadP(r5, MemOperand(sp, (argc - 1) * kPointerSize), r0);
      // Growing elements that are SMI-only requires special handling in case
      // the new element is non-Smi. For now, delegate to the builtin.
      Label no_fast_elements_check;
      __ JumpIfSmi(r5, &no_fast_elements_check);
      __ LoadP(r10, FieldMemOperand(receiver, HeapObject::kMapOffset));
      __ CheckFastObjectElements(r10, r10, &call_builtin);
      __ bind(&no_fast_elements_check);

      ExternalReference new_space_allocation_top =
          ExternalReference::new_space_allocation_top_address(isolate());
      ExternalReference new_space_allocation_limit =
          ExternalReference::new_space_allocation_limit_address(isolate());

      const int kAllocationDelta = 4;
      // Load top and check if it is the end of elements.
      __ SmiToPtrArrayOffset(end_elements, r3);
      __ add(end_elements, elements, end_elements);
      __ Add(end_elements, end_elements, kEndElementsOffset, r0);
      __ mov(r10, Operand(new_space_allocation_top));
      __ LoadP(r6, MemOperand(r10));
      __ cmp(end_elements, r6);
      __ bne(&call_builtin);

      __ mov(r11, Operand(new_space_allocation_limit));
      __ LoadP(r11, MemOperand(r11));
      __ addi(r6, r6, Operand(kAllocationDelta * kPointerSize));
      __ cmpl(r6, r11);
      __ bgt(&call_builtin);

      // We fit and could grow elements.
      // Update new_space_allocation_top.
      __ StoreP(r6, MemOperand(r10));
      // Push the argument.
      __ StoreP(r5, MemOperand(end_elements));
      // Fill the rest with holes.
      __ LoadRoot(r6, Heap::kTheHoleValueRootIndex);
      for (int i = 1; i < kAllocationDelta; i++) {
        __ StoreP(r6, MemOperand(end_elements, i * kPointerSize), r0);
      }

      // Update elements' and array's sizes.
      __ StoreP(r3, FieldMemOperand(receiver, JSArray::kLengthOffset), r0);
      __ AddSmiLiteral(r7, r7, Smi::FromInt(kAllocationDelta), r0);
      __ StoreP(r7, FieldMemOperand(elements, FixedArray::kLengthOffset), r0);

      // Elements are in new space, so write barrier is not required.
      __ Drop(argc + 1);
      __ Ret();
    }
    __ bind(&call_builtin);
    __ TailCallExternalReference(
        ExternalReference(Builtins::c_ArrayPush, isolate()), argc + 1, 1);
  }

  // Handle call cache miss.
  __ bind(&miss);
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(type, name);
}


Handle<Code> CallStubCompiler::CompileArrayPopCall(
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<Cell> cell,
    Handle<JSFunction> function,
    Handle<String> name,
    Code::StubType type) {
  // ----------- S t a t e -------------
  //  -- r5    : name
  //  -- lr    : return address
  //  -- sp[(argc - n - 1) * 4] : arg[n] (zero-based)
  //  -- ...
  //  -- sp[argc * 4]           : receiver
  // -----------------------------------

  // If object is not an array, bail out to regular call.
  if (!object->IsJSArray() || !cell.is_null()) return Handle<Code>::null();

  Label miss, return_undefined, call_builtin;
  Register receiver = r4;
  Register elements = r6;
  GenerateNameCheck(name, &miss);

  // Get the receiver from the stack
  const int argc = arguments().immediate();
  __ LoadP(receiver, MemOperand(sp, argc * kPointerSize), r0);
  // Check that the receiver isn't a smi.
  __ JumpIfSmi(receiver, &miss);

  // Check that the maps haven't changed.
  CheckPrototypes(Handle<JSObject>::cast(object), receiver, holder, elements,
                  r7, r3, name, &miss);

  // Get the elements array of the object.
  __ LoadP(elements, FieldMemOperand(receiver, JSArray::kElementsOffset));

  // Check that the elements are in fast mode and writable.
  __ CheckMap(elements,
              r3,
              Heap::kFixedArrayMapRootIndex,
              &call_builtin,
              DONT_DO_SMI_CHECK);

  // Get the array's length into r7 and calculate new length.
  __ LoadP(r7, FieldMemOperand(receiver, JSArray::kLengthOffset));
  __ SubSmiLiteral(r7, r7, Smi::FromInt(1), r0);
  __ cmpi(r7, Operand::Zero());
  __ blt(&return_undefined);

  // Get the last element.
  __ LoadRoot(r9, Heap::kTheHoleValueRootIndex);
  // We can't address the last element in one operation. Compute the more
  // expensive shift first, and use an offset later on.
  __ SmiToPtrArrayOffset(r3, r7);
  __ add(elements, elements, r3);
  __ LoadP(r3, FieldMemOperand(elements, FixedArray::kHeaderSize));
  __ cmp(r3, r9);
  __ beq(&call_builtin);

  // Set the array's length.
  __ StoreP(r7, FieldMemOperand(receiver, JSArray::kLengthOffset), r0);

  // Fill with the hole.
  __ StoreP(r9, FieldMemOperand(elements, FixedArray::kHeaderSize), r0);
  __ Drop(argc + 1);
  __ Ret();

  __ bind(&return_undefined);
  __ LoadRoot(r3, Heap::kUndefinedValueRootIndex);
  __ Drop(argc + 1);
  __ Ret();

  __ bind(&call_builtin);
  __ TailCallExternalReference(
      ExternalReference(Builtins::c_ArrayPop, isolate()), argc + 1, 1);

  // Handle call cache miss.
  __ bind(&miss);
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(type, name);
}


Handle<Code> CallStubCompiler::CompileStringCharCodeAtCall(
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<Cell> cell,
    Handle<JSFunction> function,
    Handle<String> name,
    Code::StubType type) {
  // ----------- S t a t e -------------
  //  -- r5                     : function name
  //  -- lr                     : return address
  //  -- sp[(argc - n - 1) * 4] : arg[n] (zero-based)
  //  -- ...
  //  -- sp[argc * 4]           : receiver
  // -----------------------------------

  // If object is not a string, bail out to regular call.
  if (!object->IsString() || !cell.is_null()) return Handle<Code>::null();

  const int argc = arguments().immediate();
  Label miss;
  Label name_miss;
  Label index_out_of_range;
  Label* index_out_of_range_label = &index_out_of_range;

  if (kind_ == Code::CALL_IC &&
      (CallICBase::StringStubState::decode(extra_state_) ==
       DEFAULT_STRING_STUB)) {
    index_out_of_range_label = &miss;
  }
  GenerateNameCheck(name, &name_miss);

  // Check that the maps starting from the prototype haven't changed.
  GenerateDirectLoadGlobalFunctionPrototype(masm(),
                                            Context::STRING_FUNCTION_INDEX,
                                            r3,
                                            &miss);
  ASSERT(!object.is_identical_to(holder));
  CheckPrototypes(
      Handle<JSObject>(JSObject::cast(object->GetPrototype(isolate()))),
                  r3, holder, r4, r6, r7, name, &miss);

  Register receiver = r4;
  Register index = r7;
  Register result = r3;
  __ LoadP(receiver, MemOperand(sp, argc * kPointerSize), r0);
  if (argc > 0) {
    __ LoadP(index, MemOperand(sp, (argc - 1) * kPointerSize), r0);
  } else {
    __ LoadRoot(index, Heap::kUndefinedValueRootIndex);
  }

  StringCharCodeAtGenerator generator(receiver,
                                      index,
                                      result,
                                      &miss,  // When not a string.
                                      &miss,  // When not a number.
                                      index_out_of_range_label,
                                      STRING_INDEX_IS_NUMBER);
  generator.GenerateFast(masm());
  __ Drop(argc + 1);
  __ Ret();

  StubRuntimeCallHelper call_helper;
  generator.GenerateSlow(masm(), call_helper);

  if (index_out_of_range.is_linked()) {
    __ bind(&index_out_of_range);
    __ LoadRoot(r3, Heap::kNanValueRootIndex);
    __ Drop(argc + 1);
    __ Ret();
  }

  __ bind(&miss);
  // Restore function name in r5.
  __ Move(r5, name);
  __ bind(&name_miss);
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(type, name);
}


Handle<Code> CallStubCompiler::CompileStringCharAtCall(
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<Cell> cell,
    Handle<JSFunction> function,
    Handle<String> name,
    Code::StubType type) {
  // ----------- S t a t e -------------
  //  -- r5                     : function name
  //  -- lr                     : return address
  //  -- sp[(argc - n - 1) * 4] : arg[n] (zero-based)
  //  -- ...
  //  -- sp[argc * 4]           : receiver
  // -----------------------------------

  // If object is not a string, bail out to regular call.
  if (!object->IsString() || !cell.is_null()) return Handle<Code>::null();

  const int argc = arguments().immediate();
  Label miss;
  Label name_miss;
  Label index_out_of_range;
  Label* index_out_of_range_label = &index_out_of_range;
  if (kind_ == Code::CALL_IC &&
      (CallICBase::StringStubState::decode(extra_state_) ==
       DEFAULT_STRING_STUB)) {
    index_out_of_range_label = &miss;
  }
  GenerateNameCheck(name, &name_miss);

  // Check that the maps starting from the prototype haven't changed.
  GenerateDirectLoadGlobalFunctionPrototype(masm(),
                                            Context::STRING_FUNCTION_INDEX,
                                            r3,
                                            &miss);
  ASSERT(!object.is_identical_to(holder));
  CheckPrototypes(
      Handle<JSObject>(JSObject::cast(object->GetPrototype(isolate()))),
                  r3, holder, r4, r6, r7, name, &miss);

  Register receiver = r3;
  Register index = r7;
  Register scratch = r6;
  Register result = r3;
  __ LoadP(receiver, MemOperand(sp, argc * kPointerSize), r0);
  if (argc > 0) {
    __ LoadP(index, MemOperand(sp, (argc - 1) * kPointerSize), r0);
  } else {
    __ LoadRoot(index, Heap::kUndefinedValueRootIndex);
  }

  StringCharAtGenerator generator(receiver,
                                  index,
                                  scratch,
                                  result,
                                  &miss,  // When not a string.
                                  &miss,  // When not a number.
                                  index_out_of_range_label,
                                  STRING_INDEX_IS_NUMBER);
  generator.GenerateFast(masm());
  __ Drop(argc + 1);
  __ Ret();

  StubRuntimeCallHelper call_helper;
  generator.GenerateSlow(masm(), call_helper);

  if (index_out_of_range.is_linked()) {
    __ bind(&index_out_of_range);
    __ LoadRoot(r3, Heap::kempty_stringRootIndex);
    __ Drop(argc + 1);
    __ Ret();
  }

  __ bind(&miss);
  // Restore function name in r5.
  __ Move(r5, name);
  __ bind(&name_miss);
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(type, name);
}


Handle<Code> CallStubCompiler::CompileStringFromCharCodeCall(
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<Cell> cell,
    Handle<JSFunction> function,
    Handle<String> name,
    Code::StubType type) {
  // ----------- S t a t e -------------
  //  -- r5                     : function name
  //  -- lr                     : return address
  //  -- sp[(argc - n - 1) * 4] : arg[n] (zero-based)
  //  -- ...
  //  -- sp[argc * 4]           : receiver
  // -----------------------------------

  const int argc = arguments().immediate();

  // If the object is not a JSObject or we got an unexpected number of
  // arguments, bail out to the regular call.
  if (!object->IsJSObject() || argc != 1) return Handle<Code>::null();

  Label miss;
  GenerateNameCheck(name, &miss);

  if (cell.is_null()) {
    __ LoadP(r4, MemOperand(sp, 1 * kPointerSize));

    __ JumpIfSmi(r4, &miss);

    CheckPrototypes(Handle<JSObject>::cast(object), r4, holder, r3, r6, r7,
                    name, &miss);
  } else {
    ASSERT(cell->value() == *function);
    GenerateGlobalReceiverCheck(Handle<JSObject>::cast(object), holder, name,
                                &miss);
    GenerateLoadFunctionFromCell(cell, function, &miss);
  }

  // Load the char code argument.
  Register code = r4;
  __ LoadP(code, MemOperand(sp, 0 * kPointerSize));

  // Check the code is a smi.
  Label slow;
  __ JumpIfNotSmi(code, &slow);

  // Convert the smi code to uint16.
  __ LoadSmiLiteral(r0, Smi::FromInt(0xffff));
  __ and_(code, code, r0);

  StringCharFromCodeGenerator generator(code, r3);
  generator.GenerateFast(masm());
  __ Drop(argc + 1);
  __ Ret();

  StubRuntimeCallHelper call_helper;
  generator.GenerateSlow(masm(), call_helper);

  // Tail call the full function. We do not have to patch the receiver
  // because the function makes no use of it.
  __ bind(&slow);
  ParameterCount expected(function);
  __ InvokeFunction(function, expected, arguments(),
                    JUMP_FUNCTION, NullCallWrapper(), CALL_AS_METHOD);

  __ bind(&miss);
  // r5: function name.
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(type, name);
}


Handle<Code> CallStubCompiler::CompileMathFloorCall(
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<Cell> cell,
    Handle<JSFunction> function,
    Handle<String> name,
    Code::StubType type) {
  // ----------- S t a t e -------------
  //  -- r5                     : function name
  //  -- lr                     : return address
  //  -- sp[(argc - n - 1) * 4] : arg[n] (zero-based)
  //  -- ...
  //  -- sp[argc * 4]           : receiver
  // -----------------------------------

  const int argc = arguments().immediate();
  // If the object is not a JSObject or we got an unexpected number of
  // arguments, bail out to the regular call.
  if (!object->IsJSObject() || argc != 1) return Handle<Code>::null();

  Label miss, slow, not_smi, positive, drop_arg_return;
  GenerateNameCheck(name, &miss);

  if (cell.is_null()) {
    __ LoadP(r4, MemOperand(sp, 1 * kPointerSize));
    __ JumpIfSmi(r4, &miss);
    CheckPrototypes(Handle<JSObject>::cast(object), r4, holder, r3, r6, r7,
                    name, &miss);
  } else {
    ASSERT(cell->value() == *function);
    GenerateGlobalReceiverCheck(Handle<JSObject>::cast(object), holder, name,
                                &miss);
    GenerateLoadFunctionFromCell(cell, function, &miss);
  }

  // Load the (only) argument into r3.
  __ LoadP(r3, MemOperand(sp, 0 * kPointerSize));

  // If the argument is a smi, just return.
  STATIC_ASSERT(kSmiTag == 0);
  __ JumpIfNotSmi(r3, &not_smi);
  __ Drop(argc + 1);
  __ Ret();
  __ bind(&not_smi);

  __ CheckMap(r3, r4, Heap::kHeapNumberMapRootIndex, &slow, DONT_DO_SMI_CHECK);

  // Load the HeapNumber value.
  __ lfd(d1, FieldMemOperand(r3, HeapNumber::kValueOffset));

  // Round to integer minus
  if (CpuFeatures::IsSupported(FPU)) {
    // The frim instruction is only supported on POWER5
    // and higher
    __ frim(d1, d1);
#if V8_TARGET_ARCH_PPC64
    __ fctidz(d1, d1);
#else
    __ fctiwz(d1, d1);
#endif
  } else {
    // This sequence is more portable (avoids frim)
    // This should be evaluated to determine if frim provides any
    // perf benefit or if we can simply use the compatible sequence
    // always
    __ SetRoundingMode(kRoundToMinusInf);
#if V8_TARGET_ARCH_PPC64
    __ fctid(d1, d1);
#else
    __ fctiw(d1, d1);
#endif
    __ ResetRoundingMode();
  }
  // Convert the argument to an integer.
  __ stfdu(d1, MemOperand(sp, -8));
#if V8_TARGET_ARCH_PPC64
  __ ld(r3, MemOperand(sp, 0));
#else
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
  __ lwz(r3, MemOperand(sp, 0));
#else
  __ lwz(r3, MemOperand(sp, 4));
#endif
#endif
  __ addi(sp, sp, Operand(8));

  // if resulting conversion is negative, invert for bit tests
  __ TestSignBit(r3, r0);
  __ mr(r0, r3);
  __ beq(&positive, cr0);
  __ neg(r0, r3);
  __ bind(&positive);

  // if any of the high bits are set, fail to generic
  __ JumpIfNotUnsignedSmiCandidate(r0, r0, &slow);

  // Tag the result.
  STATIC_ASSERT(kSmiTag == 0);
  __ SmiTag(r3);

  // Check for -0
  __ cmpi(r3, Operand::Zero());
  __ bne(&drop_arg_return);

  __ LoadP(r4, MemOperand(sp, 0 * kPointerSize));
  __ lwz(r4, FieldMemOperand(r4, HeapNumber::kExponentOffset));
  __ TestSignBit32(r4, r0);
  __ beq(&drop_arg_return, cr0);
  // If our HeapNumber is negative it was -0, so load its address and return.
  __ LoadP(r3, MemOperand(sp));

  __ bind(&drop_arg_return);
  __ Drop(argc + 1);
  __ Ret();

  __ bind(&slow);
  // Tail call the full function. We do not have to patch the receiver
  // because the function makes no use of it.
  ParameterCount expected(function);
  __ InvokeFunction(function, expected, arguments(),
                    JUMP_FUNCTION, NullCallWrapper(), CALL_AS_METHOD);

  __ bind(&miss);
  // r5: function name.
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(type, name);
}


Handle<Code> CallStubCompiler::CompileMathAbsCall(
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<Cell> cell,
    Handle<JSFunction> function,
    Handle<String> name,
    Code::StubType type) {
  // ----------- S t a t e -------------
  //  -- r5                     : function name
  //  -- lr                     : return address
  //  -- sp[(argc - n - 1) * 4] : arg[n] (zero-based)
  //  -- ...
  //  -- sp[argc * 4]           : receiver
  // -----------------------------------

  const int argc = arguments().immediate();
  // If the object is not a JSObject or we got an unexpected number of
  // arguments, bail out to the regular call.
  if (!object->IsJSObject() || argc != 1) return Handle<Code>::null();

  Label miss;
  GenerateNameCheck(name, &miss);
  if (cell.is_null()) {
    __ LoadP(r4, MemOperand(sp, 1 * kPointerSize));
    __ JumpIfSmi(r4, &miss);
    CheckPrototypes(Handle<JSObject>::cast(object), r4, holder, r3, r6, r7,
                    name, &miss);
  } else {
    ASSERT(cell->value() == *function);
    GenerateGlobalReceiverCheck(Handle<JSObject>::cast(object), holder, name,
                                &miss);
    GenerateLoadFunctionFromCell(cell, function, &miss);
  }

  // Load the (only) argument into r3.
  __ LoadP(r3, MemOperand(sp, 0 * kPointerSize));

  // Check if the argument is a smi.
  Label not_smi;
  __ JumpIfNotSmi(r3, &not_smi);

  // Do bitwise not or do nothing depending on the sign of the
  // argument.
  __ ShiftRightArithImm(r0, r3, kBitsPerPointer - 1);
  __ xor_(r4, r3, r0);

  // Add 1 or do nothing depending on the sign of the argument.
  __ sub(r3, r4, r0, LeaveOE, SetRC);

  // If the result is still negative, go to the slow case.
  // This only happens for the most negative smi.
  Label slow;
  __ blt(&slow, cr0);

  // Smi case done.
  __ Drop(argc + 1);
  __ Ret();

  // Check if the argument is a heap number and load its exponent and
  // sign.
  __ bind(&not_smi);
  __ CheckMap(r3, r4, Heap::kHeapNumberMapRootIndex, &slow, DONT_DO_SMI_CHECK);
  __ lwz(r4, FieldMemOperand(r3, HeapNumber::kExponentOffset));

  // Check the sign of the argument. If the argument is positive,
  // just return it.
  Label negative_sign;
  __ andis(r0, r4, Operand(HeapNumber::kSignMask >> 16));
  __ bne(&negative_sign, cr0);
  __ Drop(argc + 1);
  __ Ret();

  // If the argument is negative, clear the sign, and return a new
  // number.
  __ bind(&negative_sign);
  STATIC_ASSERT(HeapNumber::kSignMask == 0x80000000u);
  __ xoris(r4, r4, Operand(HeapNumber::kSignMask >> 16));
  __ lwz(r6, FieldMemOperand(r3, HeapNumber::kMantissaOffset));
  __ LoadRoot(r9, Heap::kHeapNumberMapRootIndex);
  __ AllocateHeapNumber(r3, r7, r8, r9, &slow);
  __ stw(r4, FieldMemOperand(r3, HeapNumber::kExponentOffset));
  __ stw(r6, FieldMemOperand(r3, HeapNumber::kMantissaOffset));
  __ Drop(argc + 1);
  __ Ret();

  // Tail call the full function. We do not have to patch the receiver
  // because the function makes no use of it.
  __ bind(&slow);
  ParameterCount expected(function);
  __ InvokeFunction(function, expected, arguments(),
                    JUMP_FUNCTION, NullCallWrapper(), CALL_AS_METHOD);

  __ bind(&miss);
  // r5: function name.
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(type, name);
}


Handle<Code> CallStubCompiler::CompileFastApiCall(
    const CallOptimization& optimization,
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<Cell> cell,
    Handle<JSFunction> function,
    Handle<String> name) {
  Counters* counters = isolate()->counters();

  ASSERT(optimization.is_simple_api_call());
  // Bail out if object is a global object as we don't want to
  // repatch it to global receiver.
  if (object->IsGlobalObject()) return Handle<Code>::null();
  if (!cell.is_null()) return Handle<Code>::null();
  if (!object->IsJSObject()) return Handle<Code>::null();
  int depth = optimization.GetPrototypeDepthOfExpectedType(
      Handle<JSObject>::cast(object), holder);
  if (depth == kInvalidProtoDepth) return Handle<Code>::null();

  Label miss, miss_before_stack_reserved;
  GenerateNameCheck(name, &miss_before_stack_reserved);

  // Get the receiver from the stack.
  const int argc = arguments().immediate();
  __ LoadP(r4, MemOperand(sp, argc * kPointerSize), r0);

  // Check that the receiver isn't a smi.
  __ JumpIfSmi(r4, &miss_before_stack_reserved);

  __ IncrementCounter(counters->call_const(), 1, r3, r6);
  __ IncrementCounter(counters->call_const_fast_api(), 1, r3, r6);

  ReserveSpaceForFastApiCall(masm(), r3);

  // Check that the maps haven't changed and find a Holder as a side effect.
  CheckPrototypes(Handle<JSObject>::cast(object), r4, holder, r3, r6, r7, name,
                  depth, &miss);

  GenerateFastApiDirectCall(masm(), optimization, argc);

  __ bind(&miss);
  FreeSpaceForFastApiCall(masm());

  __ bind(&miss_before_stack_reserved);
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(function);
}


void CallStubCompiler::CompileHandlerFrontend(Handle<Object> object,
                                              Handle<JSObject> holder,
                                              Handle<Name> name,
                                              CheckType check,
                                              Label* success) {
  // ----------- S t a t e -------------
  //  -- r5    : name
  //  -- lr    : return address
  // -----------------------------------
  Label miss;
  GenerateNameCheck(name, &miss);

  // Get the receiver from the stack
  const int argc = arguments().immediate();
  __ LoadP(r4, MemOperand(sp, argc * kPointerSize), r0);

  // Check that the receiver isn't a smi.
  if (check != NUMBER_CHECK) {
    __ JumpIfSmi(r4, &miss);
  }

  // Make sure that it's okay not to patch the on stack receiver
  // unless we're doing a receiver map check.
  ASSERT(!object->IsGlobalObject() || check == RECEIVER_MAP_CHECK);
  switch (check) {
    case RECEIVER_MAP_CHECK:
      __ IncrementCounter(isolate()->counters()->call_const(), 1, r3, r6);

      // Check that the maps haven't changed.
      CheckPrototypes(Handle<JSObject>::cast(object), r4, holder, r3, r6, r7,
                      name, &miss);

      // Patch the receiver on the stack with the global proxy if
      // necessary.
      if (object->IsGlobalObject()) {
        __ LoadP(r6, FieldMemOperand(r4, GlobalObject::kGlobalReceiverOffset));
        __ StoreP(r6, MemOperand(sp, argc * kPointerSize));
      }
      break;

    case STRING_CHECK:
      // Check that the object is a string.
      __ CompareObjectType(r4, r6, r6, FIRST_NONSTRING_TYPE);
      __ bge(&miss);
      // Check that the maps starting from the prototype haven't changed.
      GenerateDirectLoadGlobalFunctionPrototype(
          masm(), Context::STRING_FUNCTION_INDEX, r3, &miss);
      CheckPrototypes(
          Handle<JSObject>(JSObject::cast(object->GetPrototype(isolate()))),
          r3, holder, r6, r4, r7, name, &miss);
      break;

    case SYMBOL_CHECK:
      // Check that the object is a symbol.
      __ CompareObjectType(r4, r4, r6, SYMBOL_TYPE);
      __ bne(&miss);
      // Check that the maps starting from the prototype haven't changed.
      GenerateDirectLoadGlobalFunctionPrototype(
          masm(), Context::SYMBOL_FUNCTION_INDEX, r3, &miss);
      CheckPrototypes(
          Handle<JSObject>(JSObject::cast(object->GetPrototype(isolate()))),
          r3, holder, r6, r4, r7, name, &miss);
      break;

    case NUMBER_CHECK: {
      Label fast;
      // Check that the object is a smi or a heap number.
      __ JumpIfSmi(r4, &fast);
      __ CompareObjectType(r4, r3, r3, HEAP_NUMBER_TYPE);
      __ bne(&miss);
      __ bind(&fast);
      // Check that the maps starting from the prototype haven't changed.
      GenerateDirectLoadGlobalFunctionPrototype(
          masm(), Context::NUMBER_FUNCTION_INDEX, r3, &miss);
      CheckPrototypes(
          Handle<JSObject>(JSObject::cast(object->GetPrototype(isolate()))),
          r3, holder, r6, r4, r7, name, &miss);
      break;
    }
    case BOOLEAN_CHECK: {
      Label fast;
      // Check that the object is a boolean.
      __ LoadRoot(ip, Heap::kTrueValueRootIndex);
      __ cmp(r4, ip);
      __ beq(&fast);
      __ LoadRoot(ip, Heap::kFalseValueRootIndex);
      __ cmp(r4, ip);
      __ bne(&miss);
      __ bind(&fast);
      // Check that the maps starting from the prototype haven't changed.
      GenerateDirectLoadGlobalFunctionPrototype(
          masm(), Context::BOOLEAN_FUNCTION_INDEX, r3, &miss);
      CheckPrototypes(
          Handle<JSObject>(JSObject::cast(object->GetPrototype(isolate()))),
            r3, holder, r6, r4, r7, name, &miss);
      break;
    }
  }

  __ b(success);

  // Handle call cache miss.
  __ bind(&miss);
  GenerateMissBranch();
}


void CallStubCompiler::CompileHandlerBackend(Handle<JSFunction> function) {
  CallKind call_kind = CallICBase::Contextual::decode(extra_state_)
      ? CALL_AS_FUNCTION
      : CALL_AS_METHOD;
  ParameterCount expected(function);
  __ InvokeFunction(function, expected, arguments(),
                    JUMP_FUNCTION, NullCallWrapper(), call_kind);
}


Handle<Code> CallStubCompiler::CompileCallConstant(
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<Name> name,
    CheckType check,
    Handle<JSFunction> function) {
  if (HasCustomCallGenerator(function)) {
    Handle<Code> code = CompileCustomCall(object, holder,
                                          Handle<Cell>::null(),
                                          function, Handle<String>::cast(name),
                                          Code::CONSTANT);
    // A null handle means bail out to the regular compiler code below.
    if (!code.is_null()) return code;
  }

  Label success;

  CompileHandlerFrontend(object, holder, name, check, &success);
  __ bind(&success);
  CompileHandlerBackend(function);

  // Return the generated code.
  return GetCode(function);
}


Handle<Code> CallStubCompiler::CompileCallInterceptor(Handle<JSObject> object,
                                                      Handle<JSObject> holder,
                                                      Handle<Name> name) {
  // ----------- S t a t e -------------
  //  -- r5    : name
  //  -- lr    : return address
  // -----------------------------------
  Label miss;
  GenerateNameCheck(name, &miss);

  // Get the number of arguments.
  const int argc = arguments().immediate();
  LookupResult lookup(isolate());
  LookupPostInterceptor(holder, name, &lookup);

  // Get the receiver from the stack.
  __ LoadP(r4, MemOperand(sp, argc * kPointerSize), r0);

  CallInterceptorCompiler compiler(this, arguments(), r5, extra_state_);
  compiler.Compile(masm(), object, holder, name, &lookup, r4, r6, r7, r3,
                   &miss);

  // Move returned value, the function to call, to r4.
  __ mr(r4, r3);
  // Restore receiver.
  __ LoadP(r3, MemOperand(sp, argc * kPointerSize), r0);

  GenerateCallFunction(masm(), object, arguments(), &miss, extra_state_);

  // Handle call cache miss.
  __ bind(&miss);
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(Code::INTERCEPTOR, name);
}


Handle<Code> CallStubCompiler::CompileCallGlobal(
    Handle<JSObject> object,
    Handle<GlobalObject> holder,
    Handle<PropertyCell> cell,
    Handle<JSFunction> function,
    Handle<Name> name) {
  // ----------- S t a t e -------------
  //  -- r5    : name
  //  -- lr    : return address
  // -----------------------------------
  if (HasCustomCallGenerator(function)) {
    Handle<Code> code = CompileCustomCall(
        object, holder, cell, function, Handle<String>::cast(name),
        Code::NORMAL);
    // A null handle means bail out to the regular compiler code below.
    if (!code.is_null()) return code;
  }

  Label miss;
  GenerateNameCheck(name, &miss);

  // Get the number of arguments.
  const int argc = arguments().immediate();
  GenerateGlobalReceiverCheck(object, holder, name, &miss);
  GenerateLoadFunctionFromCell(cell, function, &miss);

  // Patch the receiver on the stack with the global proxy if
  // necessary.
  if (object->IsGlobalObject()) {
    __ LoadP(r6, FieldMemOperand(r3, GlobalObject::kGlobalReceiverOffset));
    __ StoreP(r6, MemOperand(sp, argc * kPointerSize), r0);
  }

  // Set up the context (function already in r4).
  __ LoadP(cp, FieldMemOperand(r4, JSFunction::kContextOffset));

  // Jump to the cached code (tail call).
  Counters* counters = isolate()->counters();
  __ IncrementCounter(counters->call_global_inline(), 1, r6, r7);
  ParameterCount expected(function->shared()->formal_parameter_count());
  CallKind call_kind = CallICBase::Contextual::decode(extra_state_)
      ? CALL_AS_FUNCTION
      : CALL_AS_METHOD;
  // We call indirectly through the code field in the function to
  // allow recompilation to take effect without changing any of the
  // call sites.
  __ LoadP(r6, FieldMemOperand(r4, JSFunction::kCodeEntryOffset));
  __ InvokeCode(r6, expected, arguments(), JUMP_FUNCTION,
                NullCallWrapper(), call_kind);

  // Handle call cache miss.
  __ bind(&miss);
  __ IncrementCounter(counters->call_global_inline_miss(), 1, r4, r6);
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(Code::NORMAL, name);
}


Handle<Code> StoreStubCompiler::CompileStoreCallback(
    Handle<JSObject> object,
    Handle<JSObject> holder,
    Handle<Name> name,
    Handle<ExecutableAccessorInfo> callback) {
  Label success;
  HandlerFrontend(object, receiver(), holder, name, &success);
  __ bind(&success);

  // Stub never generated for non-global objects that require access checks.
  ASSERT(holder->IsJSGlobalProxy() || !holder->IsAccessCheckNeeded());

  __ push(receiver());  // receiver
  __ mov(ip, Operand(callback));  // callback info
  __ push(ip);
  __ mov(ip, Operand(name));
  __ Push(ip, value());

  // Do tail-call to the runtime system.
  ExternalReference store_callback_property =
      ExternalReference(IC_Utility(IC::kStoreCallbackProperty), isolate());
  __ TailCallExternalReference(store_callback_property, 4, 1);

  // Return the generated code.
  return GetCode(kind(), Code::CALLBACKS, name);
}


#undef __
#define __ ACCESS_MASM(masm)


void StoreStubCompiler::GenerateStoreViaSetter(
    MacroAssembler* masm,
    Handle<JSFunction> setter) {
  // ----------- S t a t e -------------
  //  -- r3    : value
  //  -- r4    : receiver
  //  -- r5    : name
  //  -- lr    : return address
  // -----------------------------------
  {
    FrameScope scope(masm, StackFrame::INTERNAL);

    // Save value register, so we can restore it later.
    __ push(r3);

    if (!setter.is_null()) {
      // Call the JavaScript setter with receiver and value on the stack.
      __ Push(r4, r3);
      ParameterCount actual(1);
      ParameterCount expected(setter);
      __ InvokeFunction(setter, expected, actual,
                        CALL_FUNCTION, NullCallWrapper(), CALL_AS_METHOD);
    } else {
      // If we generate a global code snippet for deoptimization only, remember
      // the place to continue after deoptimization.
      masm->isolate()->heap()->SetSetterStubDeoptPCOffset(masm->pc_offset());
    }

    // We have to return the passed value, not the return value of the setter.
    __ pop(r3);

    // Restore context register.
    __ LoadP(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
  }
  __ Ret();
}


#undef __
#define __ ACCESS_MASM(masm())


Handle<Code> StoreStubCompiler::CompileStoreInterceptor(
    Handle<JSObject> object,
    Handle<Name> name) {
  Label miss;

  // Check that the map of the object hasn't changed.
  __ CheckMap(receiver(), scratch1(), Handle<Map>(object->map()), &miss,
              DO_SMI_CHECK);

  // Perform global security token check if needed.
  if (object->IsJSGlobalProxy()) {
    __ CheckAccessGlobalProxy(receiver(), scratch1(), &miss);
  }

  // Stub is never generated for non-global objects that require access
  // checks.
  ASSERT(object->IsJSGlobalProxy() || !object->IsAccessCheckNeeded());

  __ Push(receiver(), this->name(), value());

  __ LoadSmiLiteral(scratch1(), Smi::FromInt(strict_mode()));
  __ push(scratch1());  // strict mode

  // Do tail-call to the runtime system.
  ExternalReference store_ic_property =
      ExternalReference(IC_Utility(IC::kStoreInterceptorProperty), isolate());
  __ TailCallExternalReference(store_ic_property, 4, 1);

  // Handle store cache miss.
  __ bind(&miss);
  TailCallBuiltin(masm(), MissBuiltin(kind()));

  // Return the generated code.
  return GetICCode(kind(), Code::INTERCEPTOR, name);
}


Handle<Code> StoreStubCompiler::CompileStoreGlobal(
    Handle<GlobalObject> object,
    Handle<PropertyCell> cell,
    Handle<Name> name) {
  Label miss;

  // Check that the map of the global has not changed.
  __ LoadP(scratch1(), FieldMemOperand(receiver(), HeapObject::kMapOffset));
  __ mov(r7, Operand(Handle<Map>(object->map())));
  __ cmp(scratch1(), r7);
  __ bne(&miss);

  // Check that the value in the cell is not the hole. If it is, this
  // cell could have been deleted and reintroducing the global needs
  // to update the property details in the property dictionary of the
  // global object. We bail out to the runtime system to do that.
  __ mov(scratch1(), Operand(cell));
  __ LoadRoot(scratch2(), Heap::kTheHoleValueRootIndex);
  __ LoadP(scratch3(), FieldMemOperand(scratch1(), Cell::kValueOffset));
  __ cmp(scratch3(), scratch2());
  __ beq(&miss);

  // Store the value in the cell.
  __ StoreP(value(), FieldMemOperand(scratch1(), Cell::kValueOffset), r0);
  // Cells are always rescanned, so no write barrier here.

  Counters* counters = isolate()->counters();
  __ IncrementCounter(
      counters->named_store_global_inline(), 1, scratch1(), scratch2());
  __ Ret();

  // Handle store cache miss.
  __ bind(&miss);
  __ IncrementCounter(
      counters->named_store_global_inline_miss(), 1, scratch1(), scratch2());
  TailCallBuiltin(masm(), MissBuiltin(kind()));

  // Return the generated code.
  return GetICCode(kind(), Code::NORMAL, name);
}


Handle<Code> LoadStubCompiler::CompileLoadNonexistent(
    Handle<JSObject> object,
    Handle<JSObject> last,
    Handle<Name> name,
    Handle<GlobalObject> global) {
  Label success;

  NonexistentHandlerFrontend(object, last, name, &success, global);

  __ bind(&success);
  // Return undefined if maps of the full prototype chain are still the
  // same and no global property with this name contains a value.
  __ LoadRoot(r3, Heap::kUndefinedValueRootIndex);
  __ Ret();

  // Return the generated code.
  return GetCode(kind(), Code::NONEXISTENT, name);
}


Register* LoadStubCompiler::registers() {
  // receiver, name, scratch1, scratch2, scratch3, scratch4.
  static Register registers[] = { r3, r5, r6, r4, r7, r8 };
  return registers;
}


Register* KeyedLoadStubCompiler::registers() {
  // receiver, name, scratch1, scratch2, scratch3, scratch4.
  static Register registers[] = { r4, r3, r5, r6, r7, r8 };
  return registers;
}


Register* StoreStubCompiler::registers() {
  // receiver, name, value, scratch1, scratch2, scratch3.
  static Register registers[] = { r4, r5, r3, r6, r7, r8 };
  return registers;
}


Register* KeyedStoreStubCompiler::registers() {
  // receiver, name, value, scratch1, scratch2, scratch3.
  static Register registers[] = { r5, r4, r3, r6, r7, r8 };
  return registers;
}


void KeyedLoadStubCompiler::GenerateNameCheck(Handle<Name> name,
                                              Register name_reg,
                                              Label* miss) {
  __ mov(r0, Operand(name));
  __ cmp(name_reg, r0);
  __ bne(miss);
}


void KeyedStoreStubCompiler::GenerateNameCheck(Handle<Name> name,
                                               Register name_reg,
                                               Label* miss) {
  __ mov(r0, Operand(name));
  __ cmp(name_reg, r0);
  __ bne(miss);
}


#undef __
#define __ ACCESS_MASM(masm)


void LoadStubCompiler::GenerateLoadViaGetter(MacroAssembler* masm,
                                             Handle<JSFunction> getter) {
  // ----------- S t a t e -------------
  //  -- r3    : receiver
  //  -- r5    : name
  //  -- lr    : return address
  // -----------------------------------
  {
    FrameScope scope(masm, StackFrame::INTERNAL);

    if (!getter.is_null()) {
      // Call the JavaScript getter with the receiver on the stack.
      __ push(r3);
      ParameterCount actual(0);
      ParameterCount expected(getter);
      __ InvokeFunction(getter, expected, actual,
                        CALL_FUNCTION, NullCallWrapper(), CALL_AS_METHOD);
    } else {
      // If we generate a global code snippet for deoptimization only, remember
      // the place to continue after deoptimization.
      masm->isolate()->heap()->SetGetterStubDeoptPCOffset(masm->pc_offset());
    }

    // Restore context register.
    __ LoadP(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
  }
  __ Ret();
}


#undef __
#define __ ACCESS_MASM(masm())


Handle<Code> LoadStubCompiler::CompileLoadGlobal(
    Handle<JSObject> object,
    Handle<GlobalObject> global,
    Handle<PropertyCell> cell,
    Handle<Name> name,
    bool is_dont_delete) {
  Label success, miss;

  __ CheckMap(
      receiver(), scratch1(), Handle<Map>(object->map()), &miss, DO_SMI_CHECK);
  HandlerFrontendHeader(
      object, receiver(), Handle<JSObject>::cast(global), name, &miss);

  // Get the value from the cell.
  __ mov(r6, Operand(cell));
  __ LoadP(r7, FieldMemOperand(r6, Cell::kValueOffset));

  // Check for deleted property if property can actually be deleted.
  if (!is_dont_delete) {
    __ LoadRoot(ip, Heap::kTheHoleValueRootIndex);
    __ cmp(r7, ip);
    __ beq(&miss);
  }

  HandlerFrontendFooter(name, &success, &miss);
  __ bind(&success);

  Counters* counters = isolate()->counters();
  __ IncrementCounter(counters->named_load_global_stub(), 1, r4, r6);
  __ mr(r3, r7);
  __ Ret();

  // Return the generated code.
  return GetICCode(kind(), Code::NORMAL, name);
}


Handle<Code> BaseLoadStoreStubCompiler::CompilePolymorphicIC(
    MapHandleList* receiver_maps,
    CodeHandleList* handlers,
    Handle<Name> name,
    Code::StubType type,
    IcCheckType check) {
  Label miss;

  if (check == PROPERTY) {
    GenerateNameCheck(name, this->name(), &miss);
  }

  __ JumpIfSmi(receiver(), &miss);
  Register map_reg = scratch1();

  int receiver_count = receiver_maps->length();
  int number_of_handled_maps = 0;
  __ LoadP(map_reg, FieldMemOperand(receiver(), HeapObject::kMapOffset));
  for (int current = 0; current < receiver_count; ++current) {
    Handle<Map> map = receiver_maps->at(current);
    if (!map->is_deprecated()) {
      Label no_match;
      number_of_handled_maps++;
      __ mov(ip, Operand(receiver_maps->at(current)));
      __ cmp(map_reg, ip);
      __ bne(&no_match);
      __ Jump(handlers->at(current), RelocInfo::CODE_TARGET);
      __ bind(&no_match);
    }
  }
  ASSERT(number_of_handled_maps != 0);

  __ bind(&miss);
  TailCallBuiltin(masm(), MissBuiltin(kind()));

  // Return the generated code.
  InlineCacheState state =
      number_of_handled_maps > 1 ? POLYMORPHIC : MONOMORPHIC;
  return GetICCode(kind(), type, name, state);
}


Handle<Code> KeyedStoreStubCompiler::CompileStorePolymorphic(
    MapHandleList* receiver_maps,
    CodeHandleList* handler_stubs,
    MapHandleList* transitioned_maps) {
  Label miss;
  __ JumpIfSmi(receiver(), &miss);

  int receiver_count = receiver_maps->length();
  __ LoadP(scratch1(), FieldMemOperand(receiver(), HeapObject::kMapOffset));
  for (int i = 0; i < receiver_count; ++i) {
    __ mov(ip, Operand(receiver_maps->at(i)));
    __ cmp(scratch1(), ip);
    if (transitioned_maps->at(i).is_null()) {
      Label skip;
      __ bne(&skip);
      __ Jump(handler_stubs->at(i), RelocInfo::CODE_TARGET);
      __ bind(&skip);
    } else {
      Label next_map;
      __ bne(&next_map);
      __ mov(transition_map(), Operand(transitioned_maps->at(i)));
      __ Jump(handler_stubs->at(i), RelocInfo::CODE_TARGET, al);
      __ bind(&next_map);
    }
  }

  __ bind(&miss);
  TailCallBuiltin(masm(), MissBuiltin(kind()));

  // Return the generated code.
  return GetICCode(
      kind(), Code::NORMAL, factory()->empty_string(), POLYMORPHIC);
}


#undef __
#define __ ACCESS_MASM(masm)


void KeyedLoadStubCompiler::GenerateLoadDictionaryElement(
    MacroAssembler* masm) {
  // ---------- S t a t e --------------
  //  -- lr     : return address
  //  -- r3     : key
  //  -- r4     : receiver
  // -----------------------------------
  Label slow, miss_force_generic;

  Register key = r3;
  Register receiver = r4;

  __ UntagAndJumpIfNotSmi(r5, key, &miss_force_generic);
  __ LoadP(r7, FieldMemOperand(receiver, JSObject::kElementsOffset));
  __ LoadFromNumberDictionary(&slow, r7, key, r3, r5, r6, r8);
  __ Ret();

  __ bind(&slow);
  __ IncrementCounter(
      masm->isolate()->counters()->keyed_load_external_array_slow(),
      1, r5, r6);

  // ---------- S t a t e --------------
  //  -- lr     : return address
  //  -- r3     : key
  //  -- r4     : receiver
  // -----------------------------------
  TailCallBuiltin(masm, Builtins::kKeyedLoadIC_Slow);

  // Miss case, call the runtime.
  __ bind(&miss_force_generic);

  // ---------- S t a t e --------------
  //  -- lr     : return address
  //  -- r3     : key
  //  -- r4     : receiver
  // -----------------------------------
  TailCallBuiltin(masm, Builtins::kKeyedLoadIC_MissForceGeneric);
}


static void GenerateSmiKeyCheck(MacroAssembler* masm,
                                Register key,
                                Register scratch0,
                                Register scratch1,
                                DoubleRegister double_scratch0,
                                DoubleRegister double_scratch1,
                                Label* fail) {
  Label key_ok;
  // Check for smi or a smi inside a heap number.  We convert the heap
  // number and check if the conversion is exact and fits into the smi
  // range.
  __ JumpIfSmi(key, &key_ok);
  __ CheckMap(key,
              scratch0,
              Heap::kHeapNumberMapRootIndex,
              fail,
              DONT_DO_SMI_CHECK);
  __ lfd(double_scratch0, FieldMemOperand(key, HeapNumber::kValueOffset));
  __ TryDoubleToInt32Exact(scratch0, double_scratch0, scratch1,
                           double_scratch1);
  __ bne(fail);
#if V8_TARGET_ARCH_PPC64
  __ SmiTag(key, scratch0);
#else
  __ SmiTagCheckOverflow(scratch1, scratch0, r0);
  __ BranchOnOverflow(fail);
  __ mr(key, scratch1);
#endif
  __ bind(&key_ok);
}


void KeyedStoreStubCompiler::GenerateStoreExternalArray(
    MacroAssembler* masm,
    ElementsKind elements_kind) {
  // ---------- S t a t e --------------
  //  -- r3     : value
  //  -- r4     : key
  //  -- r5     : receiver
  //  -- lr     : return address
  // -----------------------------------
  Label slow, check_heap_number, miss_force_generic;

  // Register usage.
  Register value = r3;
  Register key = r4;
  Register receiver = r5;
  // r6 mostly holds the elements array or the destination external array.

  // This stub is meant to be tail-jumped to, the receiver must already
  // have been verified by the caller to not be a smi.

  // Check that the key is a smi or a heap number convertible to a smi.
  GenerateSmiKeyCheck(masm, key, r7, r8, d1, d2, &miss_force_generic);

  __ LoadP(r6, FieldMemOperand(receiver, JSObject::kElementsOffset));

  // Check that the index is in range
  __ LoadP(ip, FieldMemOperand(r6, ExternalArray::kLengthOffset));
  __ cmpl(key, ip);
  // Unsigned comparison catches both negative and too-large values.
  __ bge(&miss_force_generic);

  // Handle both smis and HeapNumbers in the fast path. Go to the
  // runtime for all other kinds of values.
  // r6: external array.
  if (elements_kind == EXTERNAL_PIXEL_ELEMENTS) {
    // Double to pixel conversion is only implemented in the runtime for now.
    __ UntagAndJumpIfNotSmi(r8, value, &slow);
  } else {
    __ UntagAndJumpIfNotSmi(r8, value, &check_heap_number);
  }
  __ LoadP(r6, FieldMemOperand(r6, ExternalArray::kExternalPointerOffset));

  // r6: base pointer of external storage.
  // r8: value (integer).
  // r10: scratch register
  switch (elements_kind) {
    case EXTERNAL_PIXEL_ELEMENTS:
      // Clamp the value to [0..255].
      __ ClampUint8(r8, r8);
      __ SmiToByteArrayOffset(r10, key);
      __ stbx(r8, MemOperand(r6, r10));
      break;
    case EXTERNAL_BYTE_ELEMENTS:
    case EXTERNAL_UNSIGNED_BYTE_ELEMENTS:
      __ SmiToByteArrayOffset(r10, key);
      __ stbx(r8, MemOperand(r6, r10));
      break;
    case EXTERNAL_SHORT_ELEMENTS:
    case EXTERNAL_UNSIGNED_SHORT_ELEMENTS:
      __ SmiToShortArrayOffset(r10, key);
      __ sthx(r8, MemOperand(r6, r10));
      break;
    case EXTERNAL_INT_ELEMENTS:
    case EXTERNAL_UNSIGNED_INT_ELEMENTS:
      __ SmiToIntArrayOffset(r10, key);
      __ stwx(r8, MemOperand(r6, r10));
      break;
    case EXTERNAL_FLOAT_ELEMENTS:
      // Perform int-to-float conversion and store to memory.
      __ SmiToFloatArrayOffset(r10, key);
      // r10: efective address of the float element
      __ ConvertIntToFloat(d0, r8, r9);
      __ stfsx(d0, MemOperand(r6, r10));
      break;
    case EXTERNAL_DOUBLE_ELEMENTS:
      __ SmiToDoubleArrayOffset(r10, key);
      // __ add(r6, r6, r10);
      // r6: effective address of the double element
      __ ConvertIntToDouble(r8,  d0);
      __ stfdx(d0, MemOperand(r6, r10));
      break;
    case FAST_ELEMENTS:
    case FAST_SMI_ELEMENTS:
    case FAST_DOUBLE_ELEMENTS:
    case FAST_HOLEY_ELEMENTS:
    case FAST_HOLEY_SMI_ELEMENTS:
    case FAST_HOLEY_DOUBLE_ELEMENTS:
    case DICTIONARY_ELEMENTS:
    case NON_STRICT_ARGUMENTS_ELEMENTS:
      UNREACHABLE();
      break;
  }

  // Entry registers are intact, r3 holds the value which is the return value.
  __ Ret();

  if (elements_kind != EXTERNAL_PIXEL_ELEMENTS) {
      // r6: external array.
      __ bind(&check_heap_number);
      __ CompareObjectType(value, r8, r9, HEAP_NUMBER_TYPE);
      __ bne(&slow);

      __ LoadP(r6, FieldMemOperand(r6, ExternalArray::kExternalPointerOffset));

      // r6: base pointer of external storage.

      // The WebGL specification leaves the behavior of storing NaN and
      // +/-Infinity into integer arrays basically undefined. For more
      // reproducible behavior, convert these to zero.

      if (elements_kind == EXTERNAL_FLOAT_ELEMENTS) {
        __ lfd(d0, FieldMemOperand(r3, HeapNumber::kValueOffset));
        __ SmiToFloatArrayOffset(r8, key);
        __ frsp(d0, d0);
        __ stfsx(d0, MemOperand(r6, r8));
      } else if (elements_kind == EXTERNAL_DOUBLE_ELEMENTS) {
        __ lfd(d0, FieldMemOperand(r3, HeapNumber::kValueOffset));
        __ SmiToDoubleArrayOffset(r8, key);
        __ stfdx(d0, MemOperand(r6, r8));
      } else {
        // Hoisted load.
        __ mr(r8, value);
        __ lfd(d0, FieldMemOperand(r8, HeapNumber::kValueOffset));

        __ ECMAToInt32(r8, d0, r10, r7, r9, d1);
        switch (elements_kind) {
          case EXTERNAL_BYTE_ELEMENTS:
          case EXTERNAL_UNSIGNED_BYTE_ELEMENTS:
            __ SmiToByteArrayOffset(r10, key);
            __ stbx(r8, MemOperand(r6, r10));
            break;
          case EXTERNAL_SHORT_ELEMENTS:
          case EXTERNAL_UNSIGNED_SHORT_ELEMENTS:
            __ SmiToShortArrayOffset(r10, key);
            __ sthx(r8, MemOperand(r6, r10));
            break;
          case EXTERNAL_INT_ELEMENTS:
          case EXTERNAL_UNSIGNED_INT_ELEMENTS:
            __ SmiToIntArrayOffset(r10, key);
            __ stwx(r8, MemOperand(r6, r10));
            break;
          case EXTERNAL_PIXEL_ELEMENTS:
          case EXTERNAL_FLOAT_ELEMENTS:
          case EXTERNAL_DOUBLE_ELEMENTS:
          case FAST_ELEMENTS:
          case FAST_SMI_ELEMENTS:
          case FAST_DOUBLE_ELEMENTS:
          case FAST_HOLEY_ELEMENTS:
          case FAST_HOLEY_SMI_ELEMENTS:
          case FAST_HOLEY_DOUBLE_ELEMENTS:
          case DICTIONARY_ELEMENTS:
          case NON_STRICT_ARGUMENTS_ELEMENTS:
            UNREACHABLE();
            break;
        }
      }

    // Entry registers are intact, r3 holds the value which is the return
    // value.
    __ Ret();
  }

  // Slow case, key and receiver still in r3 and r4.
  __ bind(&slow);
  __ IncrementCounter(
      masm->isolate()->counters()->keyed_load_external_array_slow(),
      1, r5, r6);

  // ---------- S t a t e --------------
  //  -- lr     : return address
  //  -- r3     : key
  //  -- r4     : receiver
  // -----------------------------------
  TailCallBuiltin(masm, Builtins::kKeyedStoreIC_Slow);

  // Miss case, call the runtime.
  __ bind(&miss_force_generic);

  // ---------- S t a t e --------------
  //  -- lr     : return address
  //  -- r3     : key
  //  -- r4     : receiver
  // -----------------------------------
  TailCallBuiltin(masm, Builtins::kKeyedStoreIC_MissForceGeneric);
}


void KeyedStoreStubCompiler::GenerateStoreFastElement(
    MacroAssembler* masm,
    bool is_js_array,
    ElementsKind elements_kind,
    KeyedAccessStoreMode store_mode) {
  // ----------- S t a t e -------------
  //  -- r3    : value
  //  -- r4    : key
  //  -- r5    : receiver
  //  -- lr    : return address
  //  -- r6    : scratch
  //  -- r7    : scratch (elements)
  // -----------------------------------
  Label miss_force_generic, transition_elements_kind, grow, slow;
  Label finish_store, check_capacity;

  Register value_reg = r3;
  Register key_reg = r4;
  Register receiver_reg = r5;
  Register scratch = r7;
  Register elements_reg = r6;
  Register length_reg = r8;
  Register scratch2 = r9;

  // This stub is meant to be tail-jumped to, the receiver must already
  // have been verified by the caller to not be a smi.

  // Check that the key is a smi or a heap number convertible to a smi.
  GenerateSmiKeyCheck(masm, key_reg, r7, r8, d1, d2, &miss_force_generic);

  if (IsFastSmiElementsKind(elements_kind)) {
    __ JumpIfNotSmi(value_reg, &transition_elements_kind);
  }

  // Check that the key is within bounds.
  __ LoadP(elements_reg,
           FieldMemOperand(receiver_reg, JSObject::kElementsOffset));
  if (is_js_array) {
    __ LoadP(scratch, FieldMemOperand(receiver_reg, JSArray::kLengthOffset));
  } else {
    __ LoadP(scratch, FieldMemOperand(elements_reg, FixedArray::kLengthOffset));
  }
  // Compare smis.
  __ cmpl(key_reg, scratch);
  if (is_js_array && IsGrowStoreMode(store_mode)) {
    __ bge(&grow);
  } else {
    __ bge(&miss_force_generic);
  }

  // Make sure elements is a fast element array, not 'cow'.
  __ CheckMap(elements_reg,
              scratch,
              Heap::kFixedArrayMapRootIndex,
              &miss_force_generic,
              DONT_DO_SMI_CHECK);

  __ bind(&finish_store);
  if (IsFastSmiElementsKind(elements_kind)) {
    __ addi(scratch,
            elements_reg,
            Operand(FixedArray::kHeaderSize - kHeapObjectTag));
    __ SmiToPtrArrayOffset(scratch2, key_reg);
    __ StorePX(value_reg, MemOperand(scratch, scratch2));
  } else {
    ASSERT(IsFastObjectElementsKind(elements_kind));
    __ addi(scratch,
            elements_reg,
            Operand(FixedArray::kHeaderSize - kHeapObjectTag));
    __ SmiToPtrArrayOffset(scratch2, key_reg);
    __ StorePUX(value_reg, MemOperand(scratch, scratch2));
    __ mr(receiver_reg, value_reg);
    __ RecordWrite(elements_reg,  // Object.
                   scratch,       // Address.
                   receiver_reg,  // Value.
                   kLRHasNotBeenSaved,
                   kDontSaveFPRegs);
  }
  // value_reg (r3) is preserved.
  // Done.
  __ Ret();

  __ bind(&miss_force_generic);
  TailCallBuiltin(masm, Builtins::kKeyedStoreIC_MissForceGeneric);

  __ bind(&transition_elements_kind);
  TailCallBuiltin(masm, Builtins::kKeyedStoreIC_Miss);

  if (is_js_array && IsGrowStoreMode(store_mode)) {
    // Grow the array by a single element if possible.
    __ bind(&grow);

    // Make sure the array is only growing by a single element, anything else
    // must be handled by the runtime. Flags already set by previous compare.
    __ bne(&miss_force_generic);

    // Check for the empty array, and preallocate a small backing store if
    // possible.
    __ LoadP(length_reg,
             FieldMemOperand(receiver_reg, JSArray::kLengthOffset));
    __ LoadP(elements_reg,
             FieldMemOperand(receiver_reg, JSObject::kElementsOffset));
    __ CompareRoot(elements_reg, Heap::kEmptyFixedArrayRootIndex);
    __ bne(&check_capacity);

    int size = FixedArray::SizeFor(JSArray::kPreallocatedArrayElements);
    __ Allocate(size, elements_reg, scratch, scratch2, &slow, TAG_OBJECT);

    __ LoadRoot(scratch, Heap::kFixedArrayMapRootIndex);
    __ StoreP(scratch, FieldMemOperand(elements_reg, JSObject::kMapOffset), r0);
    __ LoadSmiLiteral(scratch,
                      Smi::FromInt(JSArray::kPreallocatedArrayElements));
    __ StoreP(scratch, FieldMemOperand(elements_reg, FixedArray::kLengthOffset),
              r0);
    __ LoadRoot(scratch, Heap::kTheHoleValueRootIndex);
    for (int i = 1; i < JSArray::kPreallocatedArrayElements; ++i) {
      __ StoreP(scratch,
                FieldMemOperand(elements_reg, FixedArray::SizeFor(i)), r0);
    }

    // Store the element at index zero.
    __ StoreP(value_reg, FieldMemOperand(elements_reg, FixedArray::SizeFor(0)),
              r0);

    // Install the new backing store in the JSArray.
    __ StoreP(elements_reg,
              FieldMemOperand(receiver_reg, JSObject::kElementsOffset), r0);
    __ RecordWriteField(receiver_reg, JSObject::kElementsOffset, elements_reg,
                        scratch, kLRHasNotBeenSaved, kDontSaveFPRegs,
                        EMIT_REMEMBERED_SET, OMIT_SMI_CHECK);

    // Increment the length of the array.
    __ LoadSmiLiteral(length_reg, Smi::FromInt(1));
    __ StoreP(length_reg, FieldMemOperand(receiver_reg, JSArray::kLengthOffset),
              r0);
    __ Ret();

    __ bind(&check_capacity);
    // Check for cow elements, in general they are not handled by this stub
    __ CheckMap(elements_reg,
                scratch,
                Heap::kFixedCOWArrayMapRootIndex,
                &miss_force_generic,
                DONT_DO_SMI_CHECK);

    __ LoadP(scratch, FieldMemOperand(elements_reg, FixedArray::kLengthOffset));
    __ cmpl(length_reg, scratch);
    __ bge(&slow);

    // Grow the array and finish the store.
    __ AddSmiLiteral(length_reg, length_reg, Smi::FromInt(1), r0);
    __ StoreP(length_reg, FieldMemOperand(receiver_reg, JSArray::kLengthOffset),
              r0);
    __ b(&finish_store);

    __ bind(&slow);
    TailCallBuiltin(masm, Builtins::kKeyedStoreIC_Slow);
  }
}


void KeyedStoreStubCompiler::GenerateStoreFastDoubleElement(
    MacroAssembler* masm,
    bool is_js_array,
    KeyedAccessStoreMode store_mode) {
  // ----------- S t a t e -------------
  //  -- r3    : value
  //  -- r4    : key
  //  -- r5    : receiver
  //  -- lr    : return address
  //  -- r6    : scratch (elements backing store)
  //  -- r7    : scratch
  //  -- r8    : scratch
  // -----------------------------------
  Label miss_force_generic, transition_elements_kind, grow, slow;
  Label finish_store, check_capacity;

  Register value_reg = r3;
  Register key_reg = r4;
  Register receiver_reg = r5;
  Register elements_reg = r6;
  Register scratch1 = r7;
  Register scratch2 = r8;
  Register length_reg = r10;

  // This stub is meant to be tail-jumped to, the receiver must already
  // have been verified by the caller to not be a smi.

  // Check that the key is a smi or a heap number convertible to a smi.
  GenerateSmiKeyCheck(masm, key_reg, r7, r8, d1, d2, &miss_force_generic);

  __ LoadP(elements_reg,
           FieldMemOperand(receiver_reg, JSObject::kElementsOffset));

  // Check that the key is within bounds.
  if (is_js_array) {
    __ LoadP(scratch1, FieldMemOperand(receiver_reg, JSArray::kLengthOffset));
  } else {
    __ LoadP(scratch1,
             FieldMemOperand(elements_reg, FixedArray::kLengthOffset));
  }
  // Compare smis, unsigned compare catches both negative and out-of-bound
  // indexes.
  __ cmpl(key_reg, scratch1);
  if (IsGrowStoreMode(store_mode)) {
    __ bge(&grow);
  } else {
    __ bge(&miss_force_generic);
  }

  __ bind(&finish_store);
  __ StoreNumberToDoubleElements(value_reg, key_reg, elements_reg,
                                 scratch1, d0, &transition_elements_kind);
  __ Ret();

  // Handle store cache miss, replacing the ic with the generic stub.
  __ bind(&miss_force_generic);
  TailCallBuiltin(masm, Builtins::kKeyedStoreIC_MissForceGeneric);

  __ bind(&transition_elements_kind);
  TailCallBuiltin(masm, Builtins::kKeyedStoreIC_Miss);

  if (is_js_array && IsGrowStoreMode(store_mode)) {
    // Grow the array by a single element if possible.
    __ bind(&grow);

    // Make sure the array is only growing by a single element, anything else
    // must be handled by the runtime. Flags already set by previous compare.
    __ bne(&miss_force_generic);

    // Transition on values that can't be stored in a FixedDoubleArray.
    Label value_is_smi;
    __ JumpIfSmi(value_reg, &value_is_smi);
    __ LoadP(scratch1, FieldMemOperand(value_reg, HeapObject::kMapOffset));
    __ CompareRoot(scratch1, Heap::kHeapNumberMapRootIndex);
    __ bne(&transition_elements_kind);
    __ bind(&value_is_smi);

    // Check for the empty array, and preallocate a small backing store if
    // possible.
    __ LoadP(length_reg,
             FieldMemOperand(receiver_reg, JSArray::kLengthOffset));
    __ LoadP(elements_reg,
             FieldMemOperand(receiver_reg, JSObject::kElementsOffset));
    __ CompareRoot(elements_reg, Heap::kEmptyFixedArrayRootIndex);
    __ bne(&check_capacity);

    int size = FixedDoubleArray::SizeFor(JSArray::kPreallocatedArrayElements);
    __ Allocate(size, elements_reg, scratch1, scratch2, &slow, TAG_OBJECT);

    // Initialize the new FixedDoubleArray.
    __ LoadRoot(scratch1, Heap::kFixedDoubleArrayMapRootIndex);
    __ StoreP(scratch1, FieldMemOperand(elements_reg, JSObject::kMapOffset),
              r0);
    __ LoadSmiLiteral(scratch1,
                      Smi::FromInt(JSArray::kPreallocatedArrayElements));
    __ StoreP(scratch1,
              FieldMemOperand(elements_reg, FixedDoubleArray::kLengthOffset),
              r0);

    __ mr(scratch1, elements_reg);
    __ StoreNumberToDoubleElements(value_reg, key_reg, scratch1,
                                   scratch2, d0, &transition_elements_kind);

    __ mov(scratch1, Operand(kHoleNanLower32));
    __ mov(scratch2, Operand(kHoleNanUpper32));
    for (int i = 1; i < JSArray::kPreallocatedArrayElements; i++) {
      int offset = FixedDoubleArray::OffsetOfElementAt(i);
      __ StoreP(scratch1, FieldMemOperand(elements_reg, offset), r0);
      __ StoreP(scratch2,
                FieldMemOperand(elements_reg, offset + kPointerSize), r0);
    }
    // Install the new backing store in the JSArray.
    __ StoreP(elements_reg,
              FieldMemOperand(receiver_reg, JSObject::kElementsOffset), r0);
    __ RecordWriteField(receiver_reg, JSObject::kElementsOffset, elements_reg,
                        scratch1, kLRHasNotBeenSaved, kDontSaveFPRegs,
                        EMIT_REMEMBERED_SET, OMIT_SMI_CHECK);

    // Increment the length of the array.
    __ LoadSmiLiteral(length_reg, Smi::FromInt(1));
    __ StoreP(length_reg, FieldMemOperand(receiver_reg, JSArray::kLengthOffset),
              r0);
    __ LoadP(elements_reg,
             FieldMemOperand(receiver_reg, JSObject::kElementsOffset));
    __ Ret();

    __ bind(&check_capacity);
    // Make sure that the backing store can hold additional elements.
    __ LoadP(scratch1,
             FieldMemOperand(elements_reg, FixedDoubleArray::kLengthOffset));
    __ cmpl(length_reg, scratch1);
    __ bge(&slow);

    // Grow the array and finish the store.
    __ AddSmiLiteral(length_reg, length_reg, Smi::FromInt(1), r0);
    __ StoreP(length_reg, FieldMemOperand(receiver_reg, JSArray::kLengthOffset),
              r0);
    __ b(&finish_store);

    __ bind(&slow);
    TailCallBuiltin(masm, Builtins::kKeyedStoreIC_Slow);
  }
}


#undef __

} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_PPC
