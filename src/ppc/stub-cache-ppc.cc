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

#include "ic-inl.h"
#include "codegen.h"
#include "stub-cache.h"

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm)

#define EMIT_STUB_MARKER(stub_marker) __ marker_asm(stub_marker)

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
  EMIT_STUB_MARKER(0);

  ExternalReference key_offset(isolate->stub_cache()->key_reference(table));
  ExternalReference value_offset(isolate->stub_cache()->value_reference(table));
  ExternalReference map_offset(isolate->stub_cache()->map_reference(table));

  uint32_t key_off_addr = reinterpret_cast<uint32_t>(key_offset.address());
  uint32_t value_off_addr = reinterpret_cast<uint32_t>(value_offset.address());
  uint32_t map_off_addr = reinterpret_cast<uint32_t>(map_offset.address());

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
  __ slwi(offset_scratch, offset, Operand(1));
  __ add(offset_scratch, offset, offset_scratch);

  // Calculate the base address of the entry.
  __ mov(base_addr, Operand(key_offset));
  __ slwi(scratch2, offset_scratch, Operand(kPointerSizeLog2));
  __ add(base_addr, base_addr, scratch2);

  // Check that the key in the entry matches the name.
  __ lwz(ip, MemOperand(base_addr, 0));
  __ cmp(name, ip);
  __ bne(&miss);

  // Check the map matches.
  __ lwz(ip, MemOperand(base_addr, map_off_addr - key_off_addr));
  __ lwz(scratch2, FieldMemOperand(receiver, HeapObject::kMapOffset));
  __ cmp(ip, scratch2);
  __ bne(&miss);

  // Get the code entry from the cache.
  Register code = scratch2;
  scratch2 = no_reg;
  __ lwz(code, MemOperand(base_addr, value_off_addr - key_off_addr));

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
      __ jmp(&miss);
    } else if (FLAG_test_primary_stub_cache && table == StubCache::kSecondary) {
      __ jmp(&miss);
    }
#endif

  // Jump to the first instruction in the code stub.
  __ addi(r0, code, Operand(Code::kHeaderSize - kHeapObjectTag));
  __ mtctr(r0);
  __ bcr();

  // Miss: fall through.
  __ bind(&miss);
}


// Helper function used to check that the dictionary doesn't contain
// the property. This function may return false negatives, so miss_label
// must always call a backup property check that is complete.
// This function is safe to call if the receiver has fast properties.
// Name must be a symbol and receiver must be a heap object.
static void GenerateDictionaryNegativeLookup(MacroAssembler* masm,
                                             Label* miss_label,
                                             Register receiver,
                                             Handle<String> name,
                                             Register scratch0,
                                             Register scratch1) {
  EMIT_STUB_MARKER(1);

  ASSERT(name->IsSymbol());
  Counters* counters = masm->isolate()->counters();
  __ IncrementCounter(counters->negative_lookups(), 1, scratch0, scratch1);
  __ IncrementCounter(counters->negative_lookups_miss(), 1, scratch0, scratch1);

  Label done;

  const int kInterceptorOrAccessCheckNeededMask =
      (1 << Map::kHasNamedInterceptor) | (1 << Map::kIsAccessCheckNeeded);

  // Bail out if the receiver has a named interceptor or requires access checks.
  Register map = scratch1;
  __ lwz(map, FieldMemOperand(receiver, HeapObject::kMapOffset));
  __ lbz(scratch0, FieldMemOperand(map, Map::kBitFieldOffset));
  __ andi(r0, scratch0, Operand(kInterceptorOrAccessCheckNeededMask));
  __ bne(miss_label, cr0);

  // Check that receiver is a JSObject.
  __ lbz(scratch0, FieldMemOperand(map, Map::kInstanceTypeOffset));
  __ cmpi(scratch0, Operand(FIRST_SPEC_OBJECT_TYPE));
  __ blt(miss_label);

  // Load properties array.
  Register properties = scratch0;
  __ lwz(properties, FieldMemOperand(receiver, JSObject::kPropertiesOffset));
  // Check that the properties array is a dictionary.
  __ lwz(map, FieldMemOperand(properties, HeapObject::kMapOffset));
  Register tmp = properties;
  __ LoadRoot(tmp, Heap::kHashTableMapRootIndex);
  __ cmp(map, tmp);
  __ bne(miss_label);

  // Restore the temporarily used register.
  __ lwz(properties, FieldMemOperand(receiver, JSObject::kPropertiesOffset));


  StringDictionaryLookupStub::GenerateNegativeLookup(masm,
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
  EMIT_STUB_MARKER(2);

  Isolate* isolate = masm->isolate();
  Label miss;

  // Make sure that code is valid. The multiplying code relies on the
  // entry size being 12.
  ASSERT(sizeof(Entry) == 12);

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
  __ lwz(scratch, FieldMemOperand(name, String::kHashFieldOffset));
  __ lwz(ip, FieldMemOperand(receiver, HeapObject::kMapOffset));
  __ add(scratch, scratch, ip);
  uint32_t mask = kPrimaryTableSize - 1;
  // We shift out the last two bits because they are not part of the hash and
  // they are always 01 for maps.
  __ srwi(scratch, scratch, Operand(kHeapObjectTagSize));
  // Mask down the eor argument to the minimum to keep the immediate
  // ARM-encodable.
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
  __ srwi(extra, scratch, Operand(kHeapObjectTagSize));
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
  EMIT_STUB_MARKER(3);

  // Load the global or builtins object from the current context.
  __ lwz(prototype,
         MemOperand(cp, Context::SlotOffset(Context::GLOBAL_OBJECT_INDEX)));
  // Load the native context from the global or builtins object.
  __ lwz(prototype,
         FieldMemOperand(prototype, GlobalObject::kNativeContextOffset));
  // Load the function from the native context.
  __ lwz(prototype, MemOperand(prototype, Context::SlotOffset(index)));
  // Load the initial map.  The global functions all have initial maps.
  __ lwz(prototype,
         FieldMemOperand(prototype, JSFunction::kPrototypeOrInitialMapOffset));
  // Load the prototype from the initial map.
  __ lwz(prototype, FieldMemOperand(prototype, Map::kPrototypeOffset));
}


void StubCompiler::GenerateDirectLoadGlobalFunctionPrototype(
    MacroAssembler* masm,
    int index,
    Register prototype,
    Label* miss) {
  EMIT_STUB_MARKER(4);

  Isolate* isolate = masm->isolate();
  // Check we're still in the same context.
  __ lwz(prototype,
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
  __ lwz(prototype, FieldMemOperand(prototype, Map::kPrototypeOffset));
}


// Load a fast property out of a holder object (src). In-object properties
// are loaded directly otherwise the property is loaded from the properties
// fixed array.
void StubCompiler::GenerateFastPropertyLoad(MacroAssembler* masm,
                                            Register dst,
                                            Register src,
                                            Handle<JSObject> holder,
                                            int index) {
  EMIT_STUB_MARKER(5);

  // Adjust for the number of properties stored in the holder.
  index -= holder->map()->inobject_properties();
  if (index < 0) {
    // Get the property straight out of the holder.
    int offset = holder->map()->instance_size() + (index * kPointerSize);
    __ lwz(dst, FieldMemOperand(src, offset));
  } else {
    // Calculate the offset into the properties array.
    int offset = index * kPointerSize + FixedArray::kHeaderSize;
    __ lwz(dst, FieldMemOperand(src, JSObject::kPropertiesOffset));
    __ lwz(dst, FieldMemOperand(dst, offset));
  }
}


void StubCompiler::GenerateLoadArrayLength(MacroAssembler* masm,
                                           Register receiver,
                                           Register scratch,
                                           Label* miss_label) {
  EMIT_STUB_MARKER(6);

  // Check that the receiver isn't a smi.
  __ JumpIfSmi(receiver, miss_label);

  // Check that the object is a JS array.
  __ CompareObjectType(receiver, scratch, scratch, JS_ARRAY_TYPE);
  __ bne(miss_label);

  // Load length directly from the JS array.
  __ lwz(r3, FieldMemOperand(receiver, JSArray::kLengthOffset));
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
  EMIT_STUB_MARKER(7);

  // Check that the receiver isn't a smi.
  __ JumpIfSmi(receiver, smi);

  // Check that the object is a string.
  __ lwz(scratch1, FieldMemOperand(receiver, HeapObject::kMapOffset));
  __ lbz(scratch1, FieldMemOperand(scratch1, Map::kInstanceTypeOffset));
  __ andi(scratch2, scratch1, Operand(kIsNotStringMask));
  // The cast is to resolve the overload for the argument of 0x0.
  __ cmpi(scratch2, Operand(static_cast<int32_t>(kStringTag)));
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
  EMIT_STUB_MARKER(8);

  Label check_wrapper;

  // Check if the object is a string leaving the instance type in the
  // scratch1 register.
  GenerateStringCheck(masm, receiver, scratch1, scratch2, miss,
                      support_wrappers ? &check_wrapper : miss);

  // Load length directly from the string.
  __ lwz(r3, FieldMemOperand(receiver, String::kLengthOffset));
  __ Ret();

  if (support_wrappers) {
    // Check if the object is a JSValue wrapper.
    __ bind(&check_wrapper);
    __ cmpi(scratch1, Operand(JS_VALUE_TYPE));
    __ bne(miss);

    // Unwrap the value and check if the wrapped value is a string.
    __ lwz(scratch1, FieldMemOperand(receiver, JSValue::kValueOffset));
    GenerateStringCheck(masm, scratch1, scratch2, scratch2, miss, miss);
    __ lwz(r3, FieldMemOperand(scratch1, String::kLengthOffset));
    __ Ret();
  }
}


void StubCompiler::GenerateLoadFunctionPrototype(MacroAssembler* masm,
                                                 Register receiver,
                                                 Register scratch1,
                                                 Register scratch2,
                                                 Label* miss_label) {
  EMIT_STUB_MARKER(9);
  __ TryGetFunctionPrototype(receiver, scratch1, scratch2, miss_label);
  __ mr(r3, scratch1);
  __ Ret();
}


// Generate StoreField code, value is passed in r3 register.
// When leaving generated code after success, the receiver_reg and name_reg
// may be clobbered.  Upon branch to miss_label, the receiver and name
// registers have their original values.
void StubCompiler::GenerateStoreField(MacroAssembler* masm,
                                      Handle<JSObject> object,
                                      int index,
                                      Handle<Map> transition,
                                      Handle<String> name,
                                      Register receiver_reg,
                                      Register name_reg,
                                      Register scratch1,
                                      Register scratch2,
                                      Label* miss_label) {
  EMIT_STUB_MARKER(10);

  // r3 : value
  Label exit;

  LookupResult lookup(masm->isolate());
  object->Lookup(*name, &lookup);
  if (lookup.IsFound() && (lookup.IsReadOnly() || !lookup.IsCacheable())) {
    // In sloppy mode, we could just return the value and be done. However, we
    // might be in strict mode, where we have to throw. Since we cannot tell,
    // go into slow case unconditionally.
    __ jmp(miss_label);
    return;
  }

  // Check that the map of the object hasn't changed.
  CompareMapMode mode = transition.is_null() ? ALLOW_ELEMENT_TRANSITION_MAPS
                                             : REQUIRE_EXACT_MAP;
  __ CheckMap(receiver_reg, scratch1, Handle<Map>(object->map()), miss_label,
              DO_SMI_CHECK, mode);

  // Perform global security token check if needed.
  if (object->IsJSGlobalProxy()) {
    __ CheckAccessGlobalProxy(receiver_reg, scratch1, miss_label);
  }

  // Check that we are allowed to write this.
  if (!transition.is_null() && object->GetPrototype()->IsJSObject()) {
    JSObject* holder;
    if (lookup.IsFound()) {
      holder = lookup.holder();
    } else {
      // Find the top object.
      holder = *object;
      do {
        holder = JSObject::cast(holder->GetPrototype());
      } while (holder->GetPrototype()->IsJSObject());
    }
    // We need an extra register, push
    __ push(name_reg);
    Label miss_pop, done_check;
    CheckPrototypes(object, receiver_reg, Handle<JSObject>(holder), name_reg,
                    scratch1, scratch2, name, &miss_pop);
    __ jmp(&done_check);
    __ bind(&miss_pop);
    __ pop(name_reg);
    __ jmp(miss_label);
    __ bind(&done_check);
    __ pop(name_reg);
  }

  // Stub never generated for non-global objects that require access
  // checks.
  ASSERT(object->IsJSGlobalProxy() || !object->IsAccessCheckNeeded());

  // Perform map transition for the receiver if necessary.
  if (!transition.is_null() && (object->map()->unused_property_fields() == 0)) {
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

  if (!transition.is_null()) {
    // Update the map of the object.
    __ mov(scratch1, Operand(transition));
    __ stw(scratch1, FieldMemOperand(receiver_reg, HeapObject::kMapOffset));

    // Update the write barrier for the map field and pass the now unused
    // name_reg as scratch register.
    __ RecordWriteField(receiver_reg,
                        HeapObject::kMapOffset,
                        scratch1,
                        name_reg,
                        kLRHasNotBeenSaved,
                        kDontSaveFPRegs,
                        OMIT_REMEMBERED_SET,
                        OMIT_SMI_CHECK);
  }

  // Adjust for the number of properties stored in the object. Even in the
  // face of a transition we can use the old map here because the size of the
  // object and the number of in-object properties is not going to change.
  index -= object->map()->inobject_properties();

  if (index < 0) {
    // Set the property straight into the object.
    int offset = object->map()->instance_size() + (index * kPointerSize);
    __ stw(r3, FieldMemOperand(receiver_reg, offset));

    // Skip updating write barrier if storing a smi.
    __ JumpIfSmi(r3, &exit);

    // Update the write barrier for the array address.
    // Pass the now unused name_reg as a scratch register.
    __ mr(name_reg, r3);
    __ RecordWriteField(receiver_reg,
                        offset,
                        name_reg,
                        scratch1,
                        kLRHasNotBeenSaved,
                        kDontSaveFPRegs);
  } else {
    // Write to the properties array.
    int offset = index * kPointerSize + FixedArray::kHeaderSize;
    // Get the properties array
    __ lwz(scratch1,
           FieldMemOperand(receiver_reg, JSObject::kPropertiesOffset));
    __ stw(r3, FieldMemOperand(scratch1, offset));

    // Skip updating write barrier if storing a smi.
    __ JumpIfSmi(r3, &exit);

    // Update the write barrier for the array address.
    // Ok to clobber receiver_reg and name_reg, since we return.
    __ mr(name_reg, r3);
    __ RecordWriteField(scratch1,
                        offset,
                        name_reg,
                        receiver_reg,
                        kLRHasNotBeenSaved,
                        kDontSaveFPRegs);
  }

  // Return the value (register r3).
  __ bind(&exit);
  __ Ret();
}


void StubCompiler::GenerateLoadMiss(MacroAssembler* masm, Code::Kind kind) {
  EMIT_STUB_MARKER(11);
  ASSERT(kind == Code::LOAD_IC || kind == Code::KEYED_LOAD_IC);
  Handle<Code> code = (kind == Code::LOAD_IC)
      ? masm->isolate()->builtins()->LoadIC_Miss()
      : masm->isolate()->builtins()->KeyedLoadIC_Miss();
  __ Jump(code, RelocInfo::CODE_TARGET);
}


static void GenerateCallFunction(MacroAssembler* masm,
                                 Handle<Object> object,
                                 const ParameterCount& arguments,
                                 Label* miss,
                                 Code::ExtraICState extra_ic_state) {
  EMIT_STUB_MARKER(12);

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
    __ lwz(r6, FieldMemOperand(r3, GlobalObject::kGlobalReceiverOffset));
    __ stw(r6, MemOperand(sp, arguments.immediate() * kPointerSize));
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
  EMIT_STUB_MARKER(13);

  __ push(name);
  Handle<InterceptorInfo> interceptor(holder_obj->GetNamedInterceptor());
  ASSERT(!masm->isolate()->heap()->InNewSpace(*interceptor));
  Register scratch = name;
  __ mov(scratch, Operand(interceptor));
  __ push(scratch);
  __ push(receiver);
  __ push(holder);
  __ lwz(scratch, FieldMemOperand(scratch, InterceptorInfo::kDataOffset));
  __ push(scratch);
  __ mov(scratch, Operand(ExternalReference::isolate_address()));
  __ push(scratch);
}


static void CompileCallLoadPropertyWithInterceptor(
    MacroAssembler* masm,
    Register receiver,
    Register holder,
    Register name,
    Handle<JSObject> holder_obj) {
  EMIT_STUB_MARKER(14);

  PushInterceptorArguments(masm, receiver, holder, name, holder_obj);

  ExternalReference ref =
      ExternalReference(IC_Utility(IC::kLoadPropertyWithInterceptorOnly),
                        masm->isolate());
  __ li(r3, Operand(6));
  __ mov(r4, Operand(ref));

  CEntryStub stub(1);
  __ CallStub(&stub);
}


static const int kFastApiCallArguments = 4;

// Reserves space for the extra arguments to API function in the
// caller's frame.
//
// These arguments are set by CheckPrototypes and GenerateFastApiDirectCall.
static void ReserveSpaceForFastApiCall(MacroAssembler* masm,
                                       Register scratch) {
  EMIT_STUB_MARKER(15);
  __ li(scratch, Operand(Smi::FromInt(0)));
  for (int i = 0; i < kFastApiCallArguments; i++) {
    __ push(scratch);
  }
}


// Undoes the effects of ReserveSpaceForFastApiCall.
static void FreeSpaceForFastApiCall(MacroAssembler* masm) {
  EMIT_STUB_MARKER(16);
  __ Drop(kFastApiCallArguments);
}


static void GenerateFastApiDirectCall(MacroAssembler* masm,
                                      const CallOptimization& optimization,
                                      int argc) {
  EMIT_STUB_MARKER(17);
  // ----------- S t a t e -------------
  //  -- sp[0]              : holder (set by CheckPrototypes)
  //  -- sp[4]              : callee JS function
  //  -- sp[8]              : call data
  //  -- sp[12]             : isolate
  //  -- sp[16]             : last JS argument
  //  -- ...
  //  -- sp[(argc + 3) * 4] : first JS argument
  //  -- sp[(argc + 4) * 4] : receiver
  // -----------------------------------
  // Get the function and setup the context.
  Handle<JSFunction> function = optimization.constant_function();
  __ LoadHeapObject(r8, function);
  __ lwz(cp, FieldMemOperand(r8, JSFunction::kContextOffset));

  // Pass the additional arguments.
  Handle<CallHandlerInfo> api_call_info = optimization.api_call_info();
  Handle<Object> call_data(api_call_info->data());
  if (masm->isolate()->heap()->InNewSpace(*call_data)) {
    __ Move(r3, api_call_info);
    __ lwz(r9, FieldMemOperand(r3, CallHandlerInfo::kDataOffset));
  } else {
    __ Move(r9, call_data);
  }
  __ mov(r10, Operand(ExternalReference::isolate_address()));
  // Store JS function, call data and isolate.
  __ stw(r8, MemOperand(sp, 1 * kPointerSize));
  __ stw(r9, MemOperand(sp, 2 * kPointerSize));
  __ stw(r10, MemOperand(sp, 3 * kPointerSize));

  // Prepare arguments.
  __ addi(r5, sp, Operand(3 * kPointerSize));

  // Allocate the v8::Arguments structure in the arguments' space since
  // it's not controlled by GC.
  const int kApiStackSpace = 4;

  FrameScope frame_scope(masm, StackFrame::MANUAL);
  __ EnterExitFrame(false, kApiStackSpace);

  // r3 = v8::Arguments&
  // Arguments is after the return address.
  __ addi(r3, sp, Operand(1 * kPointerSize));
  // v8::Arguments::implicit_args_
  __ stw(r5, MemOperand(r3, 0 * kPointerSize));
  // v8::Arguments::values_
  __ addi(ip, r5, Operand(argc * kPointerSize));
  __ stw(ip, MemOperand(r3, 1 * kPointerSize));
  // v8::Arguments::length_ = argc
  __ li(ip, Operand(argc));
  __ stw(ip, MemOperand(r3, 2 * kPointerSize));
  // v8::Arguments::is_construct_call = 0
  __ li(ip, Operand(0));
  __ stw(ip, MemOperand(r3, 3 * kPointerSize));

  const int kStackUnwindSpace = argc + kFastApiCallArguments + 1;
  Address function_address = v8::ToCData<Address>(api_call_info->callback());
  ApiFunction fun(function_address);
  ExternalReference ref = ExternalReference(&fun,
                                            ExternalReference::DIRECT_API_CALL,
                                            masm->isolate());
  AllowExternalCallThatCantCauseGC scope(masm);

  __ CallApiFunctionAndReturn(ref, kStackUnwindSpace, CallType_ScalarArg);
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
               Handle<String> name,
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
                        Handle<String> name,
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
      __ InvokeFunction(optimization.constant_function(), arguments_,
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
                      Handle<String> name,
                      Handle<JSObject> interceptor_holder,
                      Label* miss_label) {
    EMIT_STUB_MARKER(18);

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
    EMIT_STUB_MARKER(19);

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


// Generate code to check that a global property cell is empty. Create
// the property cell at compilation time if no cell exists for the
// property.
static void GenerateCheckPropertyCell(MacroAssembler* masm,
                                      Handle<GlobalObject> global,
                                      Handle<String> name,
                                      Register scratch,
                                      Label* miss) {
  EMIT_STUB_MARKER(20);

  Handle<JSGlobalPropertyCell> cell =
      GlobalObject::EnsurePropertyCell(global, name);
  ASSERT(cell->value()->IsTheHole());
  __ mov(scratch, Operand(cell));
  __ lwz(scratch,
         FieldMemOperand(scratch, JSGlobalPropertyCell::kValueOffset));
  __ LoadRoot(ip, Heap::kTheHoleValueRootIndex);
  __ cmp(scratch, ip);
  __ bne(miss);
}


// Calls GenerateCheckPropertyCell for each global object in the prototype chain
// from object to (but not including) holder.
static void GenerateCheckPropertyCells(MacroAssembler* masm,
                                       Handle<JSObject> object,
                                       Handle<JSObject> holder,
                                       Handle<String> name,
                                       Register scratch,
                                       Label* miss) {
  EMIT_STUB_MARKER(21);

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

#undef __
#define __ ACCESS_MASM(masm())


Register StubCompiler::CheckPrototypes(Handle<JSObject> object,
                                       Register object_reg,
                                       Handle<JSObject> holder,
                                       Register holder_reg,
                                       Register scratch1,
                                       Register scratch2,
                                       Handle<String> name,
                                       int save_at_depth,
                                       Label* miss) {
  EMIT_STUB_MARKER(22);

  // Make sure there's no overlap between holder and object registers.
  ASSERT(!scratch1.is(object_reg) && !scratch1.is(holder_reg));
  ASSERT(!scratch2.is(object_reg) && !scratch2.is(holder_reg)
         && !scratch2.is(scratch1));

  // Keep track of the current object in register reg.
  Register reg = object_reg;
  int depth = 0;

  if (save_at_depth == depth) {
    __ stw(reg, MemOperand(sp));
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
      if (!name->IsSymbol()) {
        name = factory()->LookupSymbol(name);
      }
      ASSERT(current->property_dictionary()->FindEntry(*name) ==
             StringDictionary::kNotFound);

      GenerateDictionaryNegativeLookup(masm(), miss, reg, name,
                                       scratch1, scratch2);

      __ lwz(scratch1, FieldMemOperand(reg, HeapObject::kMapOffset));
      reg = holder_reg;  // From now on the object will be in holder_reg.
      __ lwz(reg, FieldMemOperand(scratch1, Map::kPrototypeOffset));
    } else {
      Handle<Map> current_map(current->map());
      __ CheckMap(reg, scratch1, current_map, miss, DONT_DO_SMI_CHECK,
                  ALLOW_ELEMENT_TRANSITION_MAPS);

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
        __ lwz(reg, FieldMemOperand(scratch1, Map::kPrototypeOffset));
      } else {
        // The prototype is in old space; load it directly.
        __ mov(reg, Operand(prototype));
      }
    }

    if (save_at_depth == depth) {
      __ stw(reg, MemOperand(sp));
    }

    // Go to the next object in the prototype chain.
    current = prototype;
  }

  // Log the check depth.
  LOG(masm()->isolate(), IntEvent("check-maps-depth", depth + 1));

  // Check the holder map.
  __ CheckMap(reg, scratch1, Handle<Map>(current->map()), miss,
              DONT_DO_SMI_CHECK, ALLOW_ELEMENT_TRANSITION_MAPS);

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


void StubCompiler::GenerateLoadField(Handle<JSObject> object,
                                     Handle<JSObject> holder,
                                     Register receiver,
                                     Register scratch1,
                                     Register scratch2,
                                     Register scratch3,
                                     int index,
                                     Handle<String> name,
                                     Label* miss) {
  EMIT_STUB_MARKER(23);

  // Check that the receiver isn't a smi.
  __ JumpIfSmi(receiver, miss);

  // Check that the maps haven't changed.
  Register reg = CheckPrototypes(
      object, receiver, holder, scratch1, scratch2, scratch3, name, miss);
  GenerateFastPropertyLoad(masm(), r3, reg, holder, index);
  __ Ret();
}


void StubCompiler::GenerateLoadConstant(Handle<JSObject> object,
                                        Handle<JSObject> holder,
                                        Register receiver,
                                        Register scratch1,
                                        Register scratch2,
                                        Register scratch3,
                                        Handle<JSFunction> value,
                                        Handle<String> name,
                                        Label* miss) {
  EMIT_STUB_MARKER(24);

  // Check that the receiver isn't a smi.
  __ JumpIfSmi(receiver, miss);

  // Check that the maps haven't changed.
  CheckPrototypes(
      object, receiver, holder, scratch1, scratch2, scratch3, name, miss);

  // Return the constant value.
  __ LoadHeapObject(r3, value);
  __ Ret();
}


void StubCompiler::GenerateDictionaryLoadCallback(Register receiver,
                                                  Register name_reg,
                                                  Register scratch1,
                                                  Register scratch2,
                                                  Register scratch3,
                                                  Handle<AccessorInfo> callback,
                                                  Handle<String> name,
                                                  Label* miss) {
  EMIT_STUB_MARKER(25);

  ASSERT(!receiver.is(scratch1));
  ASSERT(!receiver.is(scratch2));
  ASSERT(!receiver.is(scratch3));

  // Load the properties dictionary.
  Register dictionary = scratch1;
  __ lwz(dictionary, FieldMemOperand(receiver, JSObject::kPropertiesOffset));

  // Probe the dictionary.
  Label probe_done;
  StringDictionaryLookupStub::GeneratePositiveLookup(masm(),
                                                     miss,
                                                     &probe_done,
                                                     dictionary,
                                                     name_reg,
                                                     scratch2,
                                                     scratch3);
  __ bind(&probe_done);

  // If probing finds an entry in the dictionary, scratch3 contains the
  // pointer into the dictionary. Check that the value is the callback.
  Register pointer = scratch3;
  const int kElementsStartOffset = StringDictionary::kHeaderSize +
      StringDictionary::kElementsStartIndex * kPointerSize;
  const int kValueOffset = kElementsStartOffset + kPointerSize;
  __ lwz(scratch2, FieldMemOperand(pointer, kValueOffset));
  __ mov(scratch3, Operand(callback));
  __ cmp(scratch2, scratch3);
  __ bne(miss);
}


void StubCompiler::GenerateLoadCallback(Handle<JSObject> object,
                                        Handle<JSObject> holder,
                                        Register receiver,
                                        Register name_reg,
                                        Register scratch1,
                                        Register scratch2,
                                        Register scratch3,
                                        Register scratch4,
                                        Handle<AccessorInfo> callback,
                                        Handle<String> name,
                                        Label* miss) {
  EMIT_STUB_MARKER(26);

  // Check that the receiver isn't a smi.
  __ JumpIfSmi(receiver, miss);

  // Check that the maps haven't changed.
  Register reg = CheckPrototypes(object, receiver, holder, scratch1,
                                 scratch2, scratch3, name, miss);

  if (!holder->HasFastProperties() && !holder->IsJSGlobalObject()) {
    GenerateDictionaryLoadCallback(
        reg, name_reg, scratch2, scratch3, scratch4, callback, name, miss);
  }

  // Build AccessorInfo::args_ list on the stack and push property name below
  // the exit frame to make GC aware of them and store pointers to them.
  __ push(receiver);
  __ mr(scratch2, sp);  // scratch2 = AccessorInfo::args_
  if (heap()->InNewSpace(callback->data())) {
    __ Move(scratch3, callback);
    __ lwz(scratch3, FieldMemOperand(scratch3, AccessorInfo::kDataOffset));
  } else {
    __ Move(scratch3, Handle<Object>(callback->data()));
  }
  __ Push(reg, scratch3);
  __ mov(scratch3, Operand(ExternalReference::isolate_address()));
  __ Push(scratch3, name_reg);
  __ mr(r3, sp);  // r3 = Handle<String>

  const int kApiStackSpace = 1;
  FrameScope frame_scope(masm(), StackFrame::MANUAL);
  __ EnterExitFrame(false, kApiStackSpace);

  // Create AccessorInfo instance on the stack above the exit frame with
  // scratch2 (internal::Object** args_) as the data.
  __ stw(scratch2, MemOperand(sp, 1 * kPointerSize));
  __ addi(r4, sp, Operand(1 * kPointerSize));  // r4 = AccessorInfo&

  const int kStackUnwindSpace = 5;
  Address getter_address = v8::ToCData<Address>(callback->getter());
  ApiFunction fun(getter_address);
  ExternalReference ref =
      ExternalReference(&fun,
                        ExternalReference::DIRECT_GETTER_CALL,
                        masm()->isolate());
  __ CallApiFunctionAndReturn(ref, kStackUnwindSpace, CallType_NonScalarArg);
}


void StubCompiler::GenerateLoadInterceptor(Handle<JSObject> object,
                                           Handle<JSObject> interceptor_holder,
                                           LookupResult* lookup,
                                           Register receiver,
                                           Register name_reg,
                                           Register scratch1,
                                           Register scratch2,
                                           Register scratch3,
                                           Handle<String> name,
                                           Label* miss) {
  EMIT_STUB_MARKER(27);

  ASSERT(interceptor_holder->HasNamedInterceptor());
  ASSERT(!interceptor_holder->GetNamedInterceptor()->getter()->IsUndefined());

  // Check that the receiver isn't a smi.
  __ JumpIfSmi(receiver, miss);

  // So far the most popular follow ups for interceptor loads are FIELD
  // and CALLBACKS, so inline only them, other cases may be added
  // later.
  bool compile_followup_inline = false;
  if (lookup->IsFound() && lookup->IsCacheable()) {
    if (lookup->IsField()) {
      compile_followup_inline = true;
    } else if (lookup->type() == CALLBACKS &&
               lookup->GetCallbackObject()->IsAccessorInfo()) {
      AccessorInfo* callback = AccessorInfo::cast(lookup->GetCallbackObject());
      compile_followup_inline = callback->getter() != NULL &&
          callback->IsCompatibleReceiver(*object);
    }
  }

  if (compile_followup_inline) {
    // Compile the interceptor call, followed by inline code to load the
    // property from further up the prototype chain if the call fails.
    // Check that the maps haven't changed.
    Register holder_reg = CheckPrototypes(object, receiver, interceptor_holder,
                                          scratch1, scratch2, scratch3,
                                          name, miss);
    ASSERT(holder_reg.is(receiver) || holder_reg.is(scratch1));

    // Preserve the receiver register explicitly whenever it is different from
    // the holder and it is needed should the interceptor return without any
    // result. The CALLBACKS case needs the receiver to be passed into C++ code,
    // the FIELD case might cause a miss during the prototype check.
    bool must_perfrom_prototype_check = *interceptor_holder != lookup->holder();
    bool must_preserve_receiver_reg = !receiver.is(holder_reg) &&
        (lookup->type() == CALLBACKS || must_perfrom_prototype_check);

    // Save necessary data before invoking an interceptor.
    // Requires a frame to make GC aware of pushed pointers.
    {
      FrameScope frame_scope(masm(), StackFrame::INTERNAL);
      if (must_preserve_receiver_reg) {
        __ Push(receiver, holder_reg, name_reg);
      } else {
        __ Push(holder_reg, name_reg);
      }
      // Invoke an interceptor.  Note: map checks from receiver to
      // interceptor's holder has been compiled before (see a caller
      // of this method.)
      CompileCallLoadPropertyWithInterceptor(masm(),
                                             receiver,
                                             holder_reg,
                                             name_reg,
                                             interceptor_holder);
      // Check if interceptor provided a value for property.  If it's
      // the case, return immediately.
      Label interceptor_failed;
      __ LoadRoot(scratch1, Heap::kNoInterceptorResultSentinelRootIndex);
      __ cmp(r3, scratch1);
      __ beq(&interceptor_failed);
      frame_scope.GenerateLeaveFrame();
      __ Ret();

      __ bind(&interceptor_failed);
      __ pop(name_reg);
      __ pop(holder_reg);
      if (must_preserve_receiver_reg) {
        __ pop(receiver);
      }
      // Leave the internal frame.
    }
    // Check that the maps from interceptor's holder to lookup's holder
    // haven't changed.  And load lookup's holder into |holder| register.
    if (must_perfrom_prototype_check) {
      holder_reg = CheckPrototypes(interceptor_holder,
                                   holder_reg,
                                   Handle<JSObject>(lookup->holder()),
                                   scratch1,
                                   scratch2,
                                   scratch3,
                                   name,
                                   miss);
    }

    if (lookup->IsField()) {
      // We found FIELD property in prototype chain of interceptor's holder.
      // Retrieve a field from field's holder.
      GenerateFastPropertyLoad(masm(), r3, holder_reg,
                               Handle<JSObject>(lookup->holder()),
                               lookup->GetFieldIndex());
      __ Ret();
    } else {
      // We found CALLBACKS property in prototype chain of interceptor's
      // holder.
      ASSERT(lookup->type() == CALLBACKS);
      Handle<AccessorInfo> callback(
          AccessorInfo::cast(lookup->GetCallbackObject()));
      ASSERT(callback->getter() != NULL);

      // Tail call to runtime.
      // Important invariant in CALLBACKS case: the code above must be
      // structured to never clobber |receiver| register.
      __ Move(scratch2, callback);
      // holder_reg is either receiver or scratch1.
      if (!receiver.is(holder_reg)) {
        ASSERT(scratch1.is(holder_reg));
        __ Push(receiver, holder_reg);
      } else {
        __ push(receiver);
        __ push(holder_reg);
      }
      __ lwz(scratch3,
             FieldMemOperand(scratch2, AccessorInfo::kDataOffset));
      __ mov(scratch1, Operand(ExternalReference::isolate_address()));
      __ Push(scratch3, scratch1, scratch2, name_reg);

      ExternalReference ref =
          ExternalReference(IC_Utility(IC::kLoadCallbackProperty),
                            masm()->isolate());
      __ TailCallExternalReference(ref, 6, 1);
    }
  } else {  // !compile_followup_inline
    // Call the runtime system to load the interceptor.
    // Check that the maps haven't changed.
    Register holder_reg = CheckPrototypes(object, receiver, interceptor_holder,
                                          scratch1, scratch2, scratch3,
                                          name, miss);
    PushInterceptorArguments(masm(), receiver, holder_reg,
                             name_reg, interceptor_holder);

    ExternalReference ref =
        ExternalReference(IC_Utility(IC::kLoadPropertyWithInterceptorForLoad),
                          masm()->isolate());
    __ TailCallExternalReference(ref, 6, 1);
  }
}


void CallStubCompiler::GenerateNameCheck(Handle<String> name, Label* miss) {
  EMIT_STUB_MARKER(28);

  if (kind_ == Code::KEYED_CALL_IC) {
    __ mov(r0, Operand(name));
    __ cmp(r5, r0);
    __ bne(miss);
  }
}


void CallStubCompiler::GenerateGlobalReceiverCheck(Handle<JSObject> object,
                                                   Handle<JSObject> holder,
                                                   Handle<String> name,
                                                   Label* miss) {
  EMIT_STUB_MARKER(29);

  ASSERT(holder->IsGlobalObject());

  // Get the number of arguments.
  const int argc = arguments().immediate();

  // Get the receiver from the stack.
  __ lwz(r3, MemOperand(sp, argc * kPointerSize));

  // Check that the maps haven't changed.
  __ JumpIfSmi(r3, miss);
  CheckPrototypes(object, r3, holder, r6, r4, r7, name, miss);
}


void CallStubCompiler::GenerateLoadFunctionFromCell(
    Handle<JSGlobalPropertyCell> cell,
    Handle<JSFunction> function,
    Label* miss) {
  EMIT_STUB_MARKER(30);

  // Get the value from the cell.
  __ mov(r6, Operand(cell));
  __ lwz(r4, FieldMemOperand(r6, JSGlobalPropertyCell::kValueOffset));

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
    __ lwz(r7, FieldMemOperand(r4, JSFunction::kSharedFunctionInfoOffset));
    __ cmp(r7, r6);
  } else {
    __ mov(r6, Operand(function));
    __ cmp(r4, r6);
  }
  __ bne(miss);
}


void CallStubCompiler::GenerateMissBranch() {
  EMIT_STUB_MARKER(31);

  Handle<Code> code =
      isolate()->stub_cache()->ComputeCallMiss(arguments().immediate(),
                                               kind_,
                                               extra_state_);
  __ Jump(code, RelocInfo::CODE_TARGET);
}


Handle<Code> CallStubCompiler::CompileCallField(Handle<JSObject> object,
                                                Handle<JSObject> holder,
                                                int index,
                                                Handle<String> name) {
  EMIT_STUB_MARKER(32);

  // ----------- S t a t e -------------
  //  -- r5    : name
  //  -- lr    : return address
  // -----------------------------------
  Label miss;

  GenerateNameCheck(name, &miss);

  const int argc = arguments().immediate();

  // Get the receiver of the function from the stack into r3.
  __ lwz(r3, MemOperand(sp, argc * kPointerSize));
  // Check that the receiver isn't a smi.
  __ JumpIfSmi(r3, &miss);

  // Do the right check and compute the holder register.
  Register reg = CheckPrototypes(object, r3, holder, r4, r6, r7, name, &miss);
  GenerateFastPropertyLoad(masm(), r4, reg, holder, index);

  GenerateCallFunction(masm(), object, arguments(), &miss, extra_state_);

  // Handle call cache miss.
  __ bind(&miss);
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(Code::FIELD, name);
}


Handle<Code> CallStubCompiler::CompileArrayPushCall(
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<JSGlobalPropertyCell> cell,
    Handle<JSFunction> function,
    Handle<String> name) {
  EMIT_STUB_MARKER(33);

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
  __ lwz(receiver, MemOperand(sp, argc * kPointerSize));

  // Check that the receiver isn't a smi.
  __ JumpIfSmi(receiver, &miss);

  // Check that the maps haven't changed.
  CheckPrototypes(Handle<JSObject>::cast(object), receiver, holder, r6, r3, r7,
                  name, &miss);

  if (argc == 0) {
    // Nothing to do, just return the length.
    __ lwz(r3, FieldMemOperand(receiver, JSArray::kLengthOffset));
    __ Drop(argc + 1);
    __ Ret();
  } else {
    Label call_builtin;

    if (argc == 1) {  // Otherwise fall through to call the builtin.
      Label attempt_to_grow_elements;

      Register elements = r9;
      Register end_elements = r8;
      // Get the elements array of the object.
      __ lwz(elements, FieldMemOperand(receiver, JSArray::kElementsOffset));

      // Check that the elements are in fast mode and writable.
      __ CheckMap(elements,
                  r3,
                  Heap::kFixedArrayMapRootIndex,
                  &call_builtin,
                  DONT_DO_SMI_CHECK);


      // Get the array's length into r3 and calculate new length.
      __ lwz(r3, FieldMemOperand(receiver, JSArray::kLengthOffset));
      STATIC_ASSERT(kSmiTagSize == 1);
      STATIC_ASSERT(kSmiTag == 0);
      __ addi(r3, r3, Operand(Smi::FromInt(argc)));

      // Get the elements' length.
      __ lwz(r7, FieldMemOperand(elements, FixedArray::kLengthOffset));

      // Check if we could survive without allocation.
      __ cmp(r3, r7);
      __ bgt(&attempt_to_grow_elements);

      // Check if value is a smi.
      Label with_write_barrier;
      __ lwz(r7, MemOperand(sp, (argc - 1) * kPointerSize));
      __ JumpIfNotSmi(r7, &with_write_barrier);

      // Save new length.
      __ stw(r3, FieldMemOperand(receiver, JSArray::kLengthOffset));

      // Store the value.
      // We may need a register containing the address end_elements below,
      // so write back the value in end_elements.
      __ slwi(end_elements, r3, Operand(kPointerSizeLog2 - kSmiTagSize));
      __ add(end_elements, elements, end_elements);
      const int kEndElementsOffset =
          FixedArray::kHeaderSize - kHeapObjectTag - argc * kPointerSize;
      __ Add(end_elements, end_elements, kEndElementsOffset, r0);
      __ stw(r7, MemOperand(end_elements));

      // Check for a smi.
      __ Drop(argc + 1);
      __ Ret();

      __ bind(&with_write_barrier);

      __ lwz(r6, FieldMemOperand(receiver, HeapObject::kMapOffset));

      if (FLAG_smi_only_arrays  && !FLAG_trace_elements_transitions) {
        Label fast_object, not_fast_object;
        __ CheckFastObjectElements(r6, r10, &not_fast_object);
        __ jmp(&fast_object);
        // In case of fast smi-only, convert to fast object, otherwise bail out.
        __ bind(&not_fast_object);
        __ CheckFastSmiElements(r6, r10, &call_builtin);
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
            GenerateMapChangeElementsTransition(masm());
        __ jmp(&fast_object);

        __ bind(&try_holey_map);
        __ LoadTransitionedArrayMapConditional(FAST_HOLEY_SMI_ELEMENTS,
                                               FAST_HOLEY_ELEMENTS,
                                               r6,
                                               r10,
                                               &call_builtin);
        __ mr(r5, receiver);
        ElementsTransitionGenerator::
            GenerateMapChangeElementsTransition(masm());
        __ bind(&fast_object);
      } else {
        __ CheckFastObjectElements(r6, r6, &call_builtin);
      }

      // Save new length.
      __ stw(r3, FieldMemOperand(receiver, JSArray::kLengthOffset));

      // Store the value.
      // We may need a register containing the address end_elements below,
      // so write back the value in end_elements.
      __ slwi(end_elements, r3, Operand(kPointerSizeLog2 - kSmiTagSize));
      __ add(end_elements, elements, end_elements);
      __ Add(end_elements, end_elements, kEndElementsOffset, r0);
      __ stw(r7, MemOperand(end_elements));

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

      __ lwz(r5, MemOperand(sp, (argc - 1) * kPointerSize));
      // Growing elements that are SMI-only requires special handling in case
      // the new element is non-Smi. For now, delegate to the builtin.
      Label no_fast_elements_check;
      __ JumpIfSmi(r5, &no_fast_elements_check);
      __ lwz(r10, FieldMemOperand(receiver, HeapObject::kMapOffset));
      __ CheckFastObjectElements(r10, r10, &call_builtin);
      __ bind(&no_fast_elements_check);

      Isolate* isolate = masm()->isolate();
      ExternalReference new_space_allocation_top =
          ExternalReference::new_space_allocation_top_address(isolate);
      ExternalReference new_space_allocation_limit =
          ExternalReference::new_space_allocation_limit_address(isolate);

      const int kAllocationDelta = 4;
      // Load top and check if it is the end of elements.
      __ slwi(end_elements, r3, Operand(kPointerSizeLog2 - kSmiTagSize));
      __ add(end_elements, elements, end_elements);
      __ Add(end_elements, end_elements, kEndElementsOffset, r0);
      __ mov(r10, Operand(new_space_allocation_top));
      __ lwz(r6, MemOperand(r10));
      __ cmp(end_elements, r6);
      __ bne(&call_builtin);

      __ mov(r22, Operand(new_space_allocation_limit));
      __ lwz(r22, MemOperand(r22));
      __ addi(r6, r6, Operand(kAllocationDelta * kPointerSize));
      __ cmpl(r6, r22);
      __ bgt(&call_builtin);

      // We fit and could grow elements.
      // Update new_space_allocation_top.
      __ stw(r6, MemOperand(r10));
      // Push the argument.
      __ stw(r5, MemOperand(end_elements));
      // Fill the rest with holes.
      __ LoadRoot(r6, Heap::kTheHoleValueRootIndex);
      for (int i = 1; i < kAllocationDelta; i++) {
        __ stw(r6, MemOperand(end_elements, i * kPointerSize));
      }

      // Update elements' and array's sizes.
      __ stw(r3, FieldMemOperand(receiver, JSArray::kLengthOffset));
      __ addi(r7, r7, Operand(Smi::FromInt(kAllocationDelta)));
      __ stw(r7, FieldMemOperand(elements, FixedArray::kLengthOffset));

      // Elements are in new space, so write barrier is not required.
      __ Drop(argc + 1);
      __ Ret();
    }
    __ bind(&call_builtin);
    __ TailCallExternalReference(ExternalReference(Builtins::c_ArrayPush,
                                                   masm()->isolate()),
                                 argc + 1,
                                 1);
  }

  // Handle call cache miss.
  __ bind(&miss);
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(function);
}


Handle<Code> CallStubCompiler::CompileArrayPopCall(
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<JSGlobalPropertyCell> cell,
    Handle<JSFunction> function,
    Handle<String> name) {
  EMIT_STUB_MARKER(34);

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
  __ lwz(receiver, MemOperand(sp, argc * kPointerSize));
  // Check that the receiver isn't a smi.
  __ JumpIfSmi(receiver, &miss);

  // Check that the maps haven't changed.
  CheckPrototypes(Handle<JSObject>::cast(object), receiver, holder, elements,
                  r7, r3, name, &miss);

  // Get the elements array of the object.
  __ lwz(elements, FieldMemOperand(receiver, JSArray::kElementsOffset));

  // Check that the elements are in fast mode and writable.
  __ CheckMap(elements,
              r3,
              Heap::kFixedArrayMapRootIndex,
              &call_builtin,
              DONT_DO_SMI_CHECK);

  // Get the array's length into r7 and calculate new length.
  __ lwz(r7, FieldMemOperand(receiver, JSArray::kLengthOffset));
  __ sub(r7, r7, Operand(Smi::FromInt(1)));
  __ cmpi(r7, Operand(0));
  __ blt(&return_undefined);

  // Get the last element.
  __ LoadRoot(r9, Heap::kTheHoleValueRootIndex);
  STATIC_ASSERT(kSmiTagSize == 1);
  STATIC_ASSERT(kSmiTag == 0);
  // We can't address the last element in one operation. Compute the more
  // expensive shift first, and use an offset later on.
  __ slwi(r3, r7, Operand(kPointerSizeLog2 - kSmiTagSize));
  __ add(elements, elements, r3);
  __ lwz(r3, FieldMemOperand(elements, FixedArray::kHeaderSize));
  __ cmp(r3, r9);
  __ beq(&call_builtin);

  // Set the array's length.
  __ stw(r7, FieldMemOperand(receiver, JSArray::kLengthOffset));

  // Fill with the hole.
  __ stw(r9, FieldMemOperand(elements, FixedArray::kHeaderSize));
  __ Drop(argc + 1);
  __ Ret();

  __ bind(&return_undefined);
  __ LoadRoot(r3, Heap::kUndefinedValueRootIndex);
  __ Drop(argc + 1);
  __ Ret();

  __ bind(&call_builtin);
  __ TailCallExternalReference(ExternalReference(Builtins::c_ArrayPop,
                                                 masm()->isolate()),
                               argc + 1,
                               1);

  // Handle call cache miss.
  __ bind(&miss);
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(function);
}


Handle<Code> CallStubCompiler::CompileStringCharCodeAtCall(
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<JSGlobalPropertyCell> cell,
    Handle<JSFunction> function,
    Handle<String> name) {
  EMIT_STUB_MARKER(35);

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
  CheckPrototypes(Handle<JSObject>(JSObject::cast(object->GetPrototype())),
                  r3, holder, r4, r6, r7, name, &miss);

  Register receiver = r4;
  Register index = r7;
  Register result = r3;
  __ lwz(receiver, MemOperand(sp, argc * kPointerSize));
  if (argc > 0) {
    __ lwz(index, MemOperand(sp, (argc - 1) * kPointerSize));
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
  return GetCode(function);
}


Handle<Code> CallStubCompiler::CompileStringCharAtCall(
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<JSGlobalPropertyCell> cell,
    Handle<JSFunction> function,
    Handle<String> name) {
  EMIT_STUB_MARKER(36);

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
  CheckPrototypes(Handle<JSObject>(JSObject::cast(object->GetPrototype())),
                  r3, holder, r4, r6, r7, name, &miss);

  Register receiver = r3;
  Register index = r7;
  Register scratch = r6;
  Register result = r3;
  __ lwz(receiver, MemOperand(sp, argc * kPointerSize));
  if (argc > 0) {
    __ lwz(index, MemOperand(sp, (argc - 1) * kPointerSize));
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
    __ LoadRoot(r3, Heap::kEmptyStringRootIndex);
    __ Drop(argc + 1);
    __ Ret();
  }

  __ bind(&miss);
  // Restore function name in r5.
  __ Move(r5, name);
  __ bind(&name_miss);
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(function);
}


Handle<Code> CallStubCompiler::CompileStringFromCharCodeCall(
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<JSGlobalPropertyCell> cell,
    Handle<JSFunction> function,
    Handle<String> name) {
  EMIT_STUB_MARKER(37);

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
    __ lwz(r4, MemOperand(sp, 1 * kPointerSize));

    STATIC_ASSERT(kSmiTag == 0);
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
  __ lwz(code, MemOperand(sp, 0 * kPointerSize));

  // Check the code is a smi.
  Label slow;
  STATIC_ASSERT(kSmiTag == 0);
  __ JumpIfNotSmi(code, &slow);

  // Convert the smi code to uint16.
  __ mov(r0, Operand(Smi::FromInt(0xffff)));
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
  __ InvokeFunction(
      function,  arguments(), JUMP_FUNCTION, NullCallWrapper(), CALL_AS_METHOD);

  __ bind(&miss);
  // r5: function name.
  GenerateMissBranch();

  // Return the generated code.
  return cell.is_null() ? GetCode(function) : GetCode(Code::NORMAL, name);
}


Handle<Code> CallStubCompiler::CompileMathFloorCall(
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<JSGlobalPropertyCell> cell,
    Handle<JSFunction> function,
    Handle<String> name) {
  EMIT_STUB_MARKER(38);

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
    __ lwz(r4, MemOperand(sp, 1 * kPointerSize));
    STATIC_ASSERT(kSmiTag == 0);
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
  __ lwz(r3, MemOperand(sp, 0 * kPointerSize));

  // If the argument is a smi, just return.
  STATIC_ASSERT(kSmiTag == 0);
  __ andi(r0, r3, Operand(kSmiTagMask));
  __ bne(&not_smi, cr0);
  __ Drop(argc + 1);
  __ Ret();
  __ bind(&not_smi);

  __ CheckMap(r3, r4, Heap::kHeapNumberMapRootIndex, &slow, DONT_DO_SMI_CHECK);

  // Load the HeapNumber value.
  __ lfd(d1, FieldMemOperand(r3, HeapNumber::kValueOffset));

  // Round to integer minus
  __ frim(d1, d1);
  // Convert the argument to an integer.
  __ fctiwz(d1, d1);
  __ sub(sp, sp, Operand(8));
  __ stfd(d1, MemOperand(sp, 0));
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
  __ lwz(r3, MemOperand(sp, 0));
#else
  __ lwz(r3, MemOperand(sp, 4));
#endif
  __ addi(sp, sp, Operand(8));

  // if resulting conversion is negative, invert for bit tests
  __ rlwinm(r0, r3, 1, 31, 31, SetRC);
  __ mr(r0, r3);
  __ beq(&positive, cr0);
  __ neg(r0, r3);
  __ bind(&positive);

  // if either of the high two bits are set, fail to generic
  __ rlwinm(r0, r0, 2, 30, 31, SetRC);
  __ bne(&slow, cr0);

  // Tag the result.
  STATIC_ASSERT(kSmiTag == 0);
  __ slwi(r3, r3, Operand(kSmiTagSize));

  // Check for -0
  __ cmpi(r3, Operand(0));
  __ bne(&drop_arg_return);

  __ lwz(r4, MemOperand(sp, 0 * kPointerSize));
  __ lwz(r4, FieldMemOperand(r4, HeapNumber::kExponentOffset));
  __ rlwinm(r0, r4, 1, 31, 31, SetRC);
  __ beq(&drop_arg_return, cr0);
  // If our HeapNumber is negative it was -0, so load its address and return.
  __ lwz(r3, MemOperand(sp));

  __ bind(&drop_arg_return);
  __ Drop(argc + 1);
  __ Ret();

  __ bind(&slow);
  // Tail call the full function. We do not have to patch the receiver
  // because the function makes no use of it.
  __ InvokeFunction(
      function, arguments(), JUMP_FUNCTION, NullCallWrapper(), CALL_AS_METHOD);

  __ bind(&miss);
  // r5: function name.
  GenerateMissBranch();

  // Return the generated code.
  return cell.is_null() ? GetCode(function) : GetCode(Code::NORMAL, name);
}


Handle<Code> CallStubCompiler::CompileMathAbsCall(
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<JSGlobalPropertyCell> cell,
    Handle<JSFunction> function,
    Handle<String> name) {
  EMIT_STUB_MARKER(39);

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
    __ lwz(r4, MemOperand(sp, 1 * kPointerSize));
    STATIC_ASSERT(kSmiTag == 0);
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
  __ lwz(r3, MemOperand(sp, 0 * kPointerSize));

  // Check if the argument is a smi.
  Label not_smi;
  STATIC_ASSERT(kSmiTag == 0);
  __ JumpIfNotSmi(r3, &not_smi);

  // Do bitwise not or do nothing depending on the sign of the
  // argument.
  __ srawi(r0, r3, kBitsPerInt - 1);
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
  __ bne(&negative_sign);
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
  __ InvokeFunction(
      function, arguments(), JUMP_FUNCTION, NullCallWrapper(), CALL_AS_METHOD);

  __ bind(&miss);
  // r5: function name.
  GenerateMissBranch();

  // Return the generated code.
  return cell.is_null() ? GetCode(function) : GetCode(Code::NORMAL, name);
}


Handle<Code> CallStubCompiler::CompileFastApiCall(
    const CallOptimization& optimization,
    Handle<Object> object,
    Handle<JSObject> holder,
    Handle<JSGlobalPropertyCell> cell,
    Handle<JSFunction> function,
    Handle<String> name) {
  EMIT_STUB_MARKER(40);

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
  __ lwz(r4, MemOperand(sp, argc * kPointerSize));

  // Check that the receiver isn't a smi.
  __ JumpIfSmi(r4, &miss_before_stack_reserved);

  __ IncrementCounter(counters->call_const(), 1, r3, r6);
  __ IncrementCounter(counters->call_const_fast_api(), 1, r3, r6);

  ReserveSpaceForFastApiCall(masm(), r3);

  // Check that the maps haven't changed and find a Holder as a side effect.
  CheckPrototypes(Handle<JSObject>::cast(object), r4, holder, r3, r6, r7, name,
                  depth, &miss);

#if defined(V8_HOST_ARCH_PPC)
  // PPC passes C++ object by reference not value
  // roohack - this is wrong
  // __ addi(r4, sp, Operand((argc + kFastApiCallArguments) * kPointerSize));
#endif

  GenerateFastApiDirectCall(masm(), optimization, argc);

  __ bind(&miss);
  FreeSpaceForFastApiCall(masm());

  __ bind(&miss_before_stack_reserved);
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(function);
}


Handle<Code> CallStubCompiler::CompileCallConstant(Handle<Object> object,
                                                   Handle<JSObject> holder,
                                                   Handle<JSFunction> function,
                                                   Handle<String> name,
                                                   CheckType check) {
  EMIT_STUB_MARKER(41);

  // ----------- S t a t e -------------
  //  -- r5    : name
  //  -- lr    : return address
  // -----------------------------------
  if (HasCustomCallGenerator(function)) {
    Handle<Code> code = CompileCustomCall(object, holder,
                                          Handle<JSGlobalPropertyCell>::null(),
                                          function, name);
    // A null handle means bail out to the regular compiler code below.
    if (!code.is_null()) return code;
  }

  Label miss;
  GenerateNameCheck(name, &miss);

  // Get the receiver from the stack
  const int argc = arguments().immediate();
  __ lwz(r4, MemOperand(sp, argc * kPointerSize));

  // Check that the receiver isn't a smi.
  if (check != NUMBER_CHECK) {
    __ JumpIfSmi(r4, &miss);
  }

  // Make sure that it's okay not to patch the on stack receiver
  // unless we're doing a receiver map check.
  ASSERT(!object->IsGlobalObject() || check == RECEIVER_MAP_CHECK);
  switch (check) {
    case RECEIVER_MAP_CHECK:
      __ IncrementCounter(masm()->isolate()->counters()->call_const(),
                          1, r3, r6);

      // Check that the maps haven't changed.
      CheckPrototypes(Handle<JSObject>::cast(object), r4, holder, r3, r6, r7,
                      name, &miss);

      // Patch the receiver on the stack with the global proxy if
      // necessary.
      if (object->IsGlobalObject()) {
        __ lwz(r6, FieldMemOperand(r4, GlobalObject::kGlobalReceiverOffset));
        __ stw(r6, MemOperand(sp, argc * kPointerSize));
      }
      break;

    case STRING_CHECK:
      if (function->IsBuiltin() || !function->shared()->is_classic_mode()) {
        // Check that the object is a two-byte string or a symbol.
        __ CompareObjectType(r4, r6, r6, FIRST_NONSTRING_TYPE);
        __ bge(&miss);
        // Check that the maps starting from the prototype haven't changed.
        GenerateDirectLoadGlobalFunctionPrototype(
            masm(), Context::STRING_FUNCTION_INDEX, r3, &miss);
        CheckPrototypes(
            Handle<JSObject>(JSObject::cast(object->GetPrototype())),
            r3, holder, r6, r4, r7, name, &miss);
      } else {
        // Calling non-strict non-builtins with a value as the receiver
        // requires boxing.
        __ jmp(&miss);
      }
      break;

    case NUMBER_CHECK:
      if (function->IsBuiltin() || !function->shared()->is_classic_mode()) {
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
            Handle<JSObject>(JSObject::cast(object->GetPrototype())),
            r3, holder, r6, r4, r7, name, &miss);
      } else {
        // Calling non-strict non-builtins with a value as the receiver
        // requires boxing.
        __ jmp(&miss);
      }
      break;

    case BOOLEAN_CHECK:
      if (function->IsBuiltin() || !function->shared()->is_classic_mode()) {
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
            Handle<JSObject>(JSObject::cast(object->GetPrototype())),
            r3, holder, r6, r4, r7, name, &miss);
      } else {
        // Calling non-strict non-builtins with a value as the receiver
        // requires boxing.
        __ jmp(&miss);
      }
      break;
  }

  CallKind call_kind = CallICBase::Contextual::decode(extra_state_)
      ? CALL_AS_FUNCTION
      : CALL_AS_METHOD;
  __ InvokeFunction(
      function, arguments(), JUMP_FUNCTION, NullCallWrapper(), call_kind);

  // Handle call cache miss.
  __ bind(&miss);
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(function);
}


Handle<Code> CallStubCompiler::CompileCallInterceptor(Handle<JSObject> object,
                                                      Handle<JSObject> holder,
                                                      Handle<String> name) {
  EMIT_STUB_MARKER(42);

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
  __ lwz(r4, MemOperand(sp, argc * kPointerSize));

  CallInterceptorCompiler compiler(this, arguments(), r5, extra_state_);
  compiler.Compile(masm(), object, holder, name, &lookup, r4, r6, r7, r3,
                   &miss);

  // Move returned value, the function to call, to r4.
  __ mr(r4, r3);
  // Restore receiver.
  __ lwz(r3, MemOperand(sp, argc * kPointerSize));

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
    Handle<JSGlobalPropertyCell> cell,
    Handle<JSFunction> function,
    Handle<String> name) {
  EMIT_STUB_MARKER(43);

  // ----------- S t a t e -------------
  //  -- r5    : name
  //  -- lr    : return address
  // -----------------------------------
  if (HasCustomCallGenerator(function)) {
    Handle<Code> code = CompileCustomCall(object, holder, cell, function, name);
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
    __ lwz(r6, FieldMemOperand(r3, GlobalObject::kGlobalReceiverOffset));
    __ stw(r6, MemOperand(sp, argc * kPointerSize));
  }

  // Set up the context (function already in r4).
  __ lwz(cp, FieldMemOperand(r4, JSFunction::kContextOffset));

  // Jump to the cached code (tail call).
  Counters* counters = masm()->isolate()->counters();
  __ IncrementCounter(counters->call_global_inline(), 1, r6, r7);
  ParameterCount expected(function->shared()->formal_parameter_count());
  CallKind call_kind = CallICBase::Contextual::decode(extra_state_)
      ? CALL_AS_FUNCTION
      : CALL_AS_METHOD;
  // We call indirectly through the code field in the function to
  // allow recompilation to take effect without changing any of the
  // call sites.
  __ lwz(r6, FieldMemOperand(r4, JSFunction::kCodeEntryOffset));
  __ InvokeCode(r6, expected, arguments(), JUMP_FUNCTION,
                NullCallWrapper(), call_kind);

  // Handle call cache miss.
  __ bind(&miss);
  __ IncrementCounter(counters->call_global_inline_miss(), 1, r4, r6);
  GenerateMissBranch();

  // Return the generated code.
  return GetCode(Code::NORMAL, name);
}


Handle<Code> StoreStubCompiler::CompileStoreField(Handle<JSObject> object,
                                                  int index,
                                                  Handle<Map> transition,
                                                  Handle<String> name) {
  EMIT_STUB_MARKER(44);

  // ----------- S t a t e -------------
  //  -- r3    : value
  //  -- r4    : receiver
  //  -- r5    : name
  //  -- lr    : return address
  // -----------------------------------
  Label miss;

  GenerateStoreField(masm(),
                     object,
                     index,
                     transition,
                     name,
                     r4, r5, r6, r7,
                     &miss);
  __ bind(&miss);
  Handle<Code> ic = masm()->isolate()->builtins()->StoreIC_Miss();
  __ Jump(ic, RelocInfo::CODE_TARGET);

  // Return the generated code.
  return GetCode(transition.is_null()
                 ? Code::FIELD
                 : Code::MAP_TRANSITION, name);
}


Handle<Code> StoreStubCompiler::CompileStoreCallback(
    Handle<String> name,
    Handle<JSObject> receiver,
    Handle<JSObject> holder,
    Handle<AccessorInfo> callback) {
  EMIT_STUB_MARKER(45);

  // ----------- S t a t e -------------
  //  -- r3    : value
  //  -- r4    : receiver
  //  -- r5    : name
  //  -- lr    : return address
  // -----------------------------------
  Label miss;
  // Check that the maps haven't changed.
  __ JumpIfSmi(r4, &miss);
  CheckPrototypes(receiver, r4, holder, r6, r7, r8, name, &miss);

  // Stub never generated for non-global objects that require access checks.
  ASSERT(holder->IsJSGlobalProxy() || !holder->IsAccessCheckNeeded());

  __ push(r4);  // receiver
  __ mov(ip, Operand(callback));  // callback info
  __ Push(ip, r5, r3);

  // Do tail-call to the runtime system.
  ExternalReference store_callback_property =
      ExternalReference(IC_Utility(IC::kStoreCallbackProperty),
                        masm()->isolate());
  __ TailCallExternalReference(store_callback_property, 4, 1);

  // Handle store cache miss.
  __ bind(&miss);
  Handle<Code> ic = masm()->isolate()->builtins()->StoreIC_Miss();
  __ Jump(ic, RelocInfo::CODE_TARGET);

  // Return the generated code.
  return GetCode(Code::CALLBACKS, name);
}


#undef __
#define __ ACCESS_MASM(masm)


void StoreStubCompiler::GenerateStoreViaSetter(
    MacroAssembler* masm,
    Handle<JSFunction> setter) {
  EMIT_STUB_MARKER(46);

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
      __ InvokeFunction(setter, actual, CALL_FUNCTION, NullCallWrapper(),
                        CALL_AS_METHOD);
    } else {
      // If we generate a global code snippet for deoptimization only, remember
      // the place to continue after deoptimization.
      masm->isolate()->heap()->SetSetterStubDeoptPCOffset(masm->pc_offset());
    }

    // We have to return the passed value, not the return value of the setter.
    __ pop(r3);

    // Restore context register.
    __ lwz(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
  }
  __ Ret();
}


#undef __
#define __ ACCESS_MASM(masm())


Handle<Code> StoreStubCompiler::CompileStoreViaSetter(
    Handle<String> name,
    Handle<JSObject> receiver,
    Handle<JSObject> holder,
    Handle<JSFunction> setter) {
  EMIT_STUB_MARKER(47);

  // ----------- S t a t e -------------
  //  -- r3    : value
  //  -- r4    : receiver
  //  -- r5    : name
  //  -- lr    : return address
  // -----------------------------------
  Label miss;

  // Check that the maps haven't changed.
  __ JumpIfSmi(r4, &miss);
  CheckPrototypes(receiver, r4, holder, r6, r7, r8, name, &miss);

  GenerateStoreViaSetter(masm(), setter);

  __ bind(&miss);
  Handle<Code> ic = masm()->isolate()->builtins()->StoreIC_Miss();
  __ Jump(ic, RelocInfo::CODE_TARGET);

  // Return the generated code.
  return GetCode(Code::CALLBACKS, name);
}


Handle<Code> StoreStubCompiler::CompileStoreInterceptor(
    Handle<JSObject> receiver,
    Handle<String> name) {
  EMIT_STUB_MARKER(48);

  // ----------- S t a t e -------------
  //  -- r3    : value
  //  -- r4    : receiver
  //  -- r5    : name
  //  -- lr    : return address
  // -----------------------------------
  Label miss;

  // Check that the map of the object hasn't changed.
  __ CheckMap(r4, r6, Handle<Map>(receiver->map()), &miss,
              DO_SMI_CHECK, ALLOW_ELEMENT_TRANSITION_MAPS);

  // Perform global security token check if needed.
  if (receiver->IsJSGlobalProxy()) {
    __ CheckAccessGlobalProxy(r4, r6, &miss);
  }

  // Stub is never generated for non-global objects that require access
  // checks.
  ASSERT(receiver->IsJSGlobalProxy() || !receiver->IsAccessCheckNeeded());

  __ Push(r4, r5, r3);  // Receiver, name, value.

  __ li(r3, Operand(Smi::FromInt(strict_mode_)));
  __ push(r3);  // strict mode

  // Do tail-call to the runtime system.
  ExternalReference store_ic_property =
      ExternalReference(IC_Utility(IC::kStoreInterceptorProperty),
                        masm()->isolate());
  __ TailCallExternalReference(store_ic_property, 4, 1);

  // Handle store cache miss.
  __ bind(&miss);
  Handle<Code> ic = masm()->isolate()->builtins()->StoreIC_Miss();
  __ Jump(ic, RelocInfo::CODE_TARGET);

  // Return the generated code.
  return GetCode(Code::INTERCEPTOR, name);
}


Handle<Code> StoreStubCompiler::CompileStoreGlobal(
    Handle<GlobalObject> object,
    Handle<JSGlobalPropertyCell> cell,
    Handle<String> name) {
  EMIT_STUB_MARKER(49);

  // ----------- S t a t e -------------
  //  -- r3    : value
  //  -- r4    : receiver
  //  -- r5    : name
  //  -- lr    : return address
  // -----------------------------------
  Label miss;

  // Check that the map of the global has not changed.
  __ lwz(r6, FieldMemOperand(r4, HeapObject::kMapOffset));
  __ mov(r7, Operand(Handle<Map>(object->map())));
  __ cmp(r6, r7);
  __ bne(&miss);

  // Check that the value in the cell is not the hole. If it is, this
  // cell could have been deleted and reintroducing the global needs
  // to update the property details in the property dictionary of the
  // global object. We bail out to the runtime system to do that.
  __ mov(r7, Operand(cell));
  __ LoadRoot(r8, Heap::kTheHoleValueRootIndex);
  __ lwz(r9, FieldMemOperand(r7, JSGlobalPropertyCell::kValueOffset));
  __ cmp(r8, r9);
  __ beq(&miss);

  // Store the value in the cell.
  __ stw(r3, FieldMemOperand(r7, JSGlobalPropertyCell::kValueOffset));
  // Cells are always rescanned, so no write barrier here.

  Counters* counters = masm()->isolate()->counters();
  __ IncrementCounter(counters->named_store_global_inline(), 1, r7, r6);
  __ Ret();

  // Handle store cache miss.
  __ bind(&miss);
  __ IncrementCounter(counters->named_store_global_inline_miss(), 1, r7, r6);
  Handle<Code> ic = masm()->isolate()->builtins()->StoreIC_Miss();
  __ Jump(ic, RelocInfo::CODE_TARGET);

  // Return the generated code.
  return GetCode(Code::NORMAL, name);
}


Handle<Code> LoadStubCompiler::CompileLoadNonexistent(Handle<String> name,
                                                      Handle<JSObject> object,
                                                      Handle<JSObject> last) {
  EMIT_STUB_MARKER(50);

  // ----------- S t a t e -------------
  //  -- r3    : receiver
  //  -- lr    : return address
  // -----------------------------------
  Label miss;

  // Check that receiver is not a smi.
  __ JumpIfSmi(r3, &miss);

  // Check the maps of the full prototype chain.
  CheckPrototypes(object, r3, last, r6, r4, r7, name, &miss);

  // If the last object in the prototype chain is a global object,
  // check that the global property cell is empty.
  if (last->IsGlobalObject()) {
    GenerateCheckPropertyCell(
        masm(), Handle<GlobalObject>::cast(last), name, r4, &miss);
  }

  // Return undefined if maps of the full prototype chain are still the
  // same and no global property with this name contains a value.
  __ LoadRoot(r3, Heap::kUndefinedValueRootIndex);
  __ Ret();

  __ bind(&miss);
  GenerateLoadMiss(masm(), Code::LOAD_IC);

  // Return the generated code.
  return GetCode(Code::NONEXISTENT, factory()->empty_string());
}


Handle<Code> LoadStubCompiler::CompileLoadField(Handle<JSObject> object,
                                                Handle<JSObject> holder,
                                                int index,
                                                Handle<String> name) {
  EMIT_STUB_MARKER(51);

  // ----------- S t a t e -------------
  //  -- r3    : receiver
  //  -- r5    : name
  //  -- lr    : return address
  // -----------------------------------
  Label miss;

  GenerateLoadField(object, holder, r3, r6, r4, r7, index, name, &miss);
  __ bind(&miss);
  GenerateLoadMiss(masm(), Code::LOAD_IC);

  // Return the generated code.
  return GetCode(Code::FIELD, name);
}


Handle<Code> LoadStubCompiler::CompileLoadCallback(
    Handle<String> name,
    Handle<JSObject> object,
    Handle<JSObject> holder,
    Handle<AccessorInfo> callback) {
  EMIT_STUB_MARKER(52);

  // ----------- S t a t e -------------
  //  -- r3    : receiver
  //  -- r5    : name
  //  -- lr    : return address
  // -----------------------------------
  Label miss;
  GenerateLoadCallback(object, holder, r3, r5, r6, r4, r7, r8, callback, name,
                       &miss);
  __ bind(&miss);
  GenerateLoadMiss(masm(), Code::LOAD_IC);

  // Return the generated code.
  return GetCode(Code::CALLBACKS, name);
}


#undef __
#define __ ACCESS_MASM(masm)


void LoadStubCompiler::GenerateLoadViaGetter(MacroAssembler* masm,
                                             Handle<JSFunction> getter) {
  EMIT_STUB_MARKER(53);

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
      __ InvokeFunction(getter, actual, CALL_FUNCTION, NullCallWrapper(),
                        CALL_AS_METHOD);
    } else {
      // If we generate a global code snippet for deoptimization only, remember
      // the place to continue after deoptimization.
      masm->isolate()->heap()->SetGetterStubDeoptPCOffset(masm->pc_offset());
    }

    // Restore context register.
    __ lwz(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
  }
  __ Ret();
}


#undef __
#define __ ACCESS_MASM(masm())


Handle<Code> LoadStubCompiler::CompileLoadViaGetter(
    Handle<String> name,
    Handle<JSObject> receiver,
    Handle<JSObject> holder,
    Handle<JSFunction> getter) {
  EMIT_STUB_MARKER(54);

  // ----------- S t a t e -------------
  //  -- r3    : receiver
  //  -- r5    : name
  //  -- lr    : return address
  // -----------------------------------
  Label miss;

  // Check that the maps haven't changed.
  __ JumpIfSmi(r3, &miss);
  CheckPrototypes(receiver, r3, holder, r6, r7, r4, name, &miss);

  GenerateLoadViaGetter(masm(), getter);

  __ bind(&miss);
  GenerateLoadMiss(masm(), Code::LOAD_IC);

  // Return the generated code.
  return GetCode(Code::CALLBACKS, name);
}


Handle<Code> LoadStubCompiler::CompileLoadConstant(Handle<JSObject> object,
                                                   Handle<JSObject> holder,
                                                   Handle<JSFunction> value,
                                                   Handle<String> name) {
  EMIT_STUB_MARKER(55);

  // ----------- S t a t e -------------
  //  -- r3    : receiver
  //  -- r5    : name
  //  -- lr    : return address
  // -----------------------------------
  Label miss;

  GenerateLoadConstant(object, holder, r3, r6, r4, r7, value, name, &miss);
  __ bind(&miss);
  GenerateLoadMiss(masm(), Code::LOAD_IC);

  // Return the generated code.
  return GetCode(Code::CONSTANT_FUNCTION, name);
}


Handle<Code> LoadStubCompiler::CompileLoadInterceptor(Handle<JSObject> object,
                                                      Handle<JSObject> holder,
                                                      Handle<String> name) {
  EMIT_STUB_MARKER(56);

  // ----------- S t a t e -------------
  //  -- r3    : receiver
  //  -- r5    : name
  //  -- lr    : return address
  // -----------------------------------
  Label miss;

  LookupResult lookup(isolate());
  LookupPostInterceptor(holder, name, &lookup);
  GenerateLoadInterceptor(object, holder, &lookup, r3, r5, r6, r4, r7, name,
                          &miss);
  __ bind(&miss);
  GenerateLoadMiss(masm(), Code::LOAD_IC);

  // Return the generated code.
  return GetCode(Code::INTERCEPTOR, name);
}


Handle<Code> LoadStubCompiler::CompileLoadGlobal(
    Handle<JSObject> object,
    Handle<GlobalObject> holder,
    Handle<JSGlobalPropertyCell> cell,
    Handle<String> name,
    bool is_dont_delete) {
  EMIT_STUB_MARKER(57);

  // ----------- S t a t e -------------
  //  -- r3    : receiver
  //  -- r5    : name
  //  -- lr    : return address
  // -----------------------------------
  Label miss;

  // Check that the map of the global has not changed.
  __ JumpIfSmi(r3, &miss);
  CheckPrototypes(object, r3, holder, r6, r7, r4, name, &miss);

  // Get the value from the cell.
  __ mov(r6, Operand(cell));
  __ lwz(r7, FieldMemOperand(r6, JSGlobalPropertyCell::kValueOffset));

  // Check for deleted property if property can actually be deleted.
  if (!is_dont_delete) {
    __ LoadRoot(ip, Heap::kTheHoleValueRootIndex);
    __ cmp(r7, ip);
    __ beq(&miss);
  }

  __ mr(r3, r7);
  Counters* counters = masm()->isolate()->counters();
  __ IncrementCounter(counters->named_load_global_stub(), 1, r4, r6);
  __ Ret();

  __ bind(&miss);
  __ IncrementCounter(counters->named_load_global_stub_miss(), 1, r4, r6);
  GenerateLoadMiss(masm(), Code::LOAD_IC);

  // Return the generated code.
  return GetCode(Code::NORMAL, name);
}


Handle<Code> KeyedLoadStubCompiler::CompileLoadField(Handle<String> name,
                                                     Handle<JSObject> receiver,
                                                     Handle<JSObject> holder,
                                                     int index) {
  EMIT_STUB_MARKER(58);

  // ----------- S t a t e -------------
  //  -- lr    : return address
  //  -- r3    : key
  //  -- r4    : receiver
  // -----------------------------------
  Label miss;

  // Check the key is the cached one.
  __ mov(r0, Operand(name));
  __ cmp(r3, r0);
  __ bne(&miss);

  GenerateLoadField(receiver, holder, r4, r5, r6, r7, index, name, &miss);
  __ bind(&miss);
  GenerateLoadMiss(masm(), Code::KEYED_LOAD_IC);

  return GetCode(Code::FIELD, name);
}


Handle<Code> KeyedLoadStubCompiler::CompileLoadCallback(
    Handle<String> name,
    Handle<JSObject> receiver,
    Handle<JSObject> holder,
    Handle<AccessorInfo> callback) {
  EMIT_STUB_MARKER(59);

  // ----------- S t a t e -------------
  //  -- lr    : return address
  //  -- r3    : key
  //  -- r4    : receiver
  // -----------------------------------
  Label miss;

  // Check the key is the cached one.
  __ mov(r0, Operand(name));
  __ cmp(r3, r0);
  __ bne(&miss);

  GenerateLoadCallback(receiver, holder, r4, r3, r5, r6, r7, r8, callback, name,
                       &miss);
  __ bind(&miss);
  GenerateLoadMiss(masm(), Code::KEYED_LOAD_IC);

  return GetCode(Code::CALLBACKS, name);
}


Handle<Code> KeyedLoadStubCompiler::CompileLoadConstant(
    Handle<String> name,
    Handle<JSObject> receiver,
    Handle<JSObject> holder,
    Handle<JSFunction> value) {
  EMIT_STUB_MARKER(60);

  // ----------- S t a t e -------------
  //  -- lr    : return address
  //  -- r3    : key
  //  -- r4    : receiver
  // -----------------------------------
  Label miss;

  // Check the key is the cached one.
  __ mov(r5, Operand(name));
  __ cmp(r3, r5);
  __ bne(&miss);

  GenerateLoadConstant(receiver, holder, r4, r5, r6, r7, value, name, &miss);
  __ bind(&miss);
  GenerateLoadMiss(masm(), Code::KEYED_LOAD_IC);

  // Return the generated code.
  return GetCode(Code::CONSTANT_FUNCTION, name);
}


Handle<Code> KeyedLoadStubCompiler::CompileLoadInterceptor(
    Handle<JSObject> receiver,
    Handle<JSObject> holder,
    Handle<String> name) {
  EMIT_STUB_MARKER(61);

  // ----------- S t a t e -------------
  //  -- lr    : return address
  //  -- r3    : key
  //  -- r4    : receiver
  // -----------------------------------
  Label miss;

  // Check the key is the cached one.
  __ mov(r0, Operand(name));
  __ cmp(r3, r0);
  __ bne(&miss);

  LookupResult lookup(isolate());
  LookupPostInterceptor(holder, name, &lookup);
  GenerateLoadInterceptor(receiver, holder, &lookup, r4, r3, r5, r6, r7, name,
                          &miss);
  __ bind(&miss);
  GenerateLoadMiss(masm(), Code::KEYED_LOAD_IC);

  return GetCode(Code::INTERCEPTOR, name);
}


Handle<Code> KeyedLoadStubCompiler::CompileLoadArrayLength(
    Handle<String> name) {
  EMIT_STUB_MARKER(62);

  // ----------- S t a t e -------------
  //  -- lr    : return address
  //  -- r3    : key
  //  -- r4    : receiver
  // -----------------------------------
  Label miss;

  // Check the key is the cached one.
  __ mov(r0, Operand(name));
  __ cmp(r3, r0);
  __ bne(&miss);

  GenerateLoadArrayLength(masm(), r4, r5, &miss);
  __ bind(&miss);
  GenerateLoadMiss(masm(), Code::KEYED_LOAD_IC);

  return GetCode(Code::CALLBACKS, name);
}


Handle<Code> KeyedLoadStubCompiler::CompileLoadStringLength(
    Handle<String> name) {
  EMIT_STUB_MARKER(63);

  // ----------- S t a t e -------------
  //  -- lr    : return address
  //  -- r3    : key
  //  -- r4    : receiver
  // -----------------------------------
  Label miss;

  Counters* counters = masm()->isolate()->counters();
  __ IncrementCounter(counters->keyed_load_string_length(), 1, r5, r6);

  // Check the key is the cached one.
  __ mov(r0, Operand(name));
  __ cmp(r3, r0);
  __ bne(&miss);

  GenerateLoadStringLength(masm(), r4, r5, r6, &miss, true);
  __ bind(&miss);
  __ DecrementCounter(counters->keyed_load_string_length(), 1, r5, r6);

  GenerateLoadMiss(masm(), Code::KEYED_LOAD_IC);

  return GetCode(Code::CALLBACKS, name);
}


Handle<Code> KeyedLoadStubCompiler::CompileLoadFunctionPrototype(
    Handle<String> name) {
  EMIT_STUB_MARKER(64);

  // ----------- S t a t e -------------
  //  -- lr    : return address
  //  -- r3    : key
  //  -- r4    : receiver
  // -----------------------------------
  Label miss;

  Counters* counters = masm()->isolate()->counters();
  __ IncrementCounter(counters->keyed_load_function_prototype(), 1, r5, r6);

  // Check the name hasn't changed.
  __ mov(r0, Operand(name));
  __ cmp(r3, r0);
  __ bne(&miss);

  GenerateLoadFunctionPrototype(masm(), r4, r5, r6, &miss);
  __ bind(&miss);
  __ DecrementCounter(counters->keyed_load_function_prototype(), 1, r5, r6);
  GenerateLoadMiss(masm(), Code::KEYED_LOAD_IC);

  return GetCode(Code::CALLBACKS, name);
}


Handle<Code> KeyedLoadStubCompiler::CompileLoadElement(
    Handle<Map> receiver_map) {
  EMIT_STUB_MARKER(65);

  // ----------- S t a t e -------------
  //  -- lr    : return address
  //  -- r3    : key
  //  -- r4    : receiver
  // -----------------------------------
  ElementsKind elements_kind = receiver_map->elements_kind();
  Handle<Code> stub = KeyedLoadElementStub(elements_kind).GetCode();

  __ DispatchMap(r4, r5, receiver_map, stub, DO_SMI_CHECK);

  Handle<Code> ic = isolate()->builtins()->KeyedLoadIC_Miss();
  __ Jump(ic, RelocInfo::CODE_TARGET);

  // Return the generated code.
  return GetCode(Code::NORMAL, factory()->empty_string());
}


Handle<Code> KeyedLoadStubCompiler::CompileLoadPolymorphic(
    MapHandleList* receiver_maps,
    CodeHandleList* handler_ics) {
  EMIT_STUB_MARKER(66);

  // ----------- S t a t e -------------
  //  -- lr    : return address
  //  -- r3    : key
  //  -- r4    : receiver
  // -----------------------------------
  Label miss;
  __ JumpIfSmi(r4, &miss);

  int receiver_count = receiver_maps->length();
  __ lwz(r5, FieldMemOperand(r4, HeapObject::kMapOffset));
  for (int current = 0; current < receiver_count; ++current) {
    Label no_match;
    __ mov(ip, Operand(receiver_maps->at(current)));
    __ cmp(r5, ip);
    __ bne(&no_match);
    __ Jump(handler_ics->at(current), RelocInfo::CODE_TARGET, al);
    __ bind(&no_match);
  }

  __ bind(&miss);
  Handle<Code> miss_ic = isolate()->builtins()->KeyedLoadIC_Miss();
  __ Jump(miss_ic, RelocInfo::CODE_TARGET, al);

  // Return the generated code.
  return GetCode(Code::NORMAL, factory()->empty_string(), MEGAMORPHIC);
}


Handle<Code> KeyedStoreStubCompiler::CompileStoreField(Handle<JSObject> object,
                                                       int index,
                                                       Handle<Map> transition,
                                                       Handle<String> name) {
  EMIT_STUB_MARKER(67);

  // ----------- S t a t e -------------
  //  -- r3    : value
  //  -- r4    : name
  //  -- r5    : receiver
  //  -- lr    : return address
  // -----------------------------------
  Label miss;

  Counters* counters = masm()->isolate()->counters();
  __ IncrementCounter(counters->keyed_store_field(), 1, r6, r7);

  // Check that the name has not changed.
  __ mov(r0, Operand(name));
  __ cmp(r4, r0);
  __ bne(&miss);

  // r6 is used as scratch register. r4 and r5 keep their values if a jump to
  // the miss label is generated.
  GenerateStoreField(masm(),
                     object,
                     index,
                     transition,
                     name,
                     r5, r4, r6, r7,
                     &miss);
  __ bind(&miss);

  __ DecrementCounter(counters->keyed_store_field(), 1, r6, r7);
  Handle<Code> ic = masm()->isolate()->builtins()->KeyedStoreIC_Miss();
  __ Jump(ic, RelocInfo::CODE_TARGET);

  // Return the generated code.
  return GetCode(transition.is_null()
                 ? Code::FIELD
                 : Code::MAP_TRANSITION, name);
}


Handle<Code> KeyedStoreStubCompiler::CompileStoreElement(
    Handle<Map> receiver_map) {
  EMIT_STUB_MARKER(68);

  // ----------- S t a t e -------------
  //  -- r3    : value
  //  -- r4    : key
  //  -- r5    : receiver
  //  -- lr    : return address
  //  -- r6    : scratch
  // -----------------------------------
  ElementsKind elements_kind = receiver_map->elements_kind();
  bool is_js_array = receiver_map->instance_type() == JS_ARRAY_TYPE;
  Handle<Code> stub =
      KeyedStoreElementStub(is_js_array, elements_kind, grow_mode_).GetCode();

  __ DispatchMap(r5, r6, receiver_map, stub, DO_SMI_CHECK);

  Handle<Code> ic = isolate()->builtins()->KeyedStoreIC_Miss();
  __ Jump(ic, RelocInfo::CODE_TARGET);

  // Return the generated code.
  return GetCode(Code::NORMAL, factory()->empty_string());
}


Handle<Code> KeyedStoreStubCompiler::CompileStorePolymorphic(
    MapHandleList* receiver_maps,
    CodeHandleList* handler_stubs,
    MapHandleList* transitioned_maps) {
  EMIT_STUB_MARKER(69);

  // ----------- S t a t e -------------
  //  -- r3    : value
  //  -- r4    : key
  //  -- r5    : receiver
  //  -- lr    : return address
  //  -- r6    : scratch
  // -----------------------------------
  Label miss;
  __ JumpIfSmi(r5, &miss);

  int receiver_count = receiver_maps->length();
  __ lwz(r6, FieldMemOperand(r5, HeapObject::kMapOffset));
  for (int i = 0; i < receiver_count; ++i) {
    __ mov(ip, Operand(receiver_maps->at(i)));
    __ cmp(r6, ip);
    if (transitioned_maps->at(i).is_null()) {
      Label skip;
      __ bne(&skip);
      __ Jump(handler_stubs->at(i), RelocInfo::CODE_TARGET);
      __ bind(&skip);
    } else {
      Label next_map;
      __ bne(&next_map);
      __ mov(r6, Operand(transitioned_maps->at(i)));
      __ Jump(handler_stubs->at(i), RelocInfo::CODE_TARGET, al);
      __ bind(&next_map);
    }
  }

  __ bind(&miss);
  Handle<Code> miss_ic = isolate()->builtins()->KeyedStoreIC_Miss();
  __ Jump(miss_ic, RelocInfo::CODE_TARGET, al);

  // Return the generated code.
  return GetCode(Code::NORMAL, factory()->empty_string(), MEGAMORPHIC);
}


Handle<Code> ConstructStubCompiler::CompileConstructStub(
    Handle<JSFunction> function) {
  EMIT_STUB_MARKER(70);

  // ----------- S t a t e -------------
  //  -- r3    : argc
  //  -- r4    : constructor
  //  -- lr    : return address
  //  -- [sp]  : last argument
  // -----------------------------------
  Label generic_stub_call;

  // Use r10 for holding undefined which is used in several places below.
  __ LoadRoot(r10, Heap::kUndefinedValueRootIndex);

#ifdef ENABLE_DEBUGGER_SUPPORT
  // Check to see whether there are any break points in the function code. If
  // there are jump to the generic constructor stub which calls the actual
  // code for the function thereby hitting the break points.
  __ lwz(r5, FieldMemOperand(r4, JSFunction::kSharedFunctionInfoOffset));
  __ lwz(r5, FieldMemOperand(r5, SharedFunctionInfo::kDebugInfoOffset));
  __ cmp(r5, r10);
  __ bne(&generic_stub_call);
#endif

  // Load the initial map and verify that it is in fact a map.
  // r4: constructor function
  // r10: undefined
  __ lwz(r5, FieldMemOperand(r4, JSFunction::kPrototypeOrInitialMapOffset));
  __ JumpIfSmi(r5, &generic_stub_call);
  __ CompareObjectType(r5, r6, r7, MAP_TYPE);
  __ bne(&generic_stub_call);

#ifdef DEBUG
  // Cannot construct functions this way.
  // r3: argc
  // r4: constructor function
  // r5: initial map
  // r10: undefined
  __ CompareInstanceType(r5, r6, JS_FUNCTION_TYPE);
  __ Check(ne, "Function constructed by construct stub.");
#endif

  // Now allocate the JSObject in new space.
  // r3: argc
  // r4: constructor function
  // r5: initial map
  // r10: undefined
  __ lbz(r6, FieldMemOperand(r5, Map::kInstanceSizeOffset));
  __ AllocateInNewSpace(r6, r7, r8, r9, &generic_stub_call, SIZE_IN_WORDS);

  // Allocated the JSObject, now initialize the fields. Map is set to initial
  // map and properties and elements are set to empty fixed array.
  // r3: argc
  // r4: constructor function
  // r5: initial map
  // r6: object size (in words)
  // r7: JSObject (not tagged)
  // r10: undefined
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

  // Calculate the location of the first argument. The stack contains only the
  // argc arguments.
  __ slwi(r4, r3, Operand(kPointerSizeLog2));
  __ add(r4, sp, r4);

  // Fill all the in-object properties with undefined.
  // r3: argc
  // r4: first argument
  // r6: object size (in words)
  // r7: JSObject (not tagged)
  // r8: First in-object property of JSObject (not tagged)
  // r10: undefined
  // Fill the initialized properties with a constant value or a passed argument
  // depending on the this.x = ...; assignment in the function.
  Handle<SharedFunctionInfo> shared(function->shared());
  for (int i = 0; i < shared->this_property_assignments_count(); i++) {
    if (shared->IsThisPropertyAssignmentArgument(i)) {
      Label not_passed, next;
      // Check if the argument assigned to the property is actually passed.
      int arg_number = shared->GetThisPropertyAssignmentArgument(i);
      __ cmpi(r3, Operand(arg_number));
      __ ble(&not_passed);
      // Argument passed - find it on the stack.
      __ lwz(r5, MemOperand(r4, (arg_number + 1) * -kPointerSize));
      __ stw(r5, MemOperand(r8));
      __ addi(r8, r8, Operand(kPointerSize));
      __ b(&next);
      __ bind(&not_passed);
      // Set the property to undefined.
      __ stw(r10, MemOperand(r8));
      __ addi(r8, r8, Operand(kPointerSize));
      __ bind(&next);
    } else {
      // Set the property to the constant value.
      Handle<Object> constant(shared->GetThisPropertyAssignmentConstant(i));
      __ mov(r5, Operand(constant));
      __ stw(r5, MemOperand(r8));
      __ addi(r8, r8, Operand(kPointerSize));
    }
  }

  // Fill the unused in-object property fields with undefined.
  ASSERT(function->has_initial_map());
  for (int i = shared->this_property_assignments_count();
       i < function->initial_map()->inobject_properties();
       i++) {
      __ stw(r10, MemOperand(r8));
      __ addi(r8, r8, Operand(kPointerSize));
  }

  // r3: argc
  // r7: JSObject (not tagged)
  // Move argc to r4 and the JSObject to return to r3 and tag it.
  __ mr(r4, r3);
  __ mr(r3, r7);
  __ ori(r3, r3, Operand(kHeapObjectTag));

  // r3: JSObject
  // r4: argc
  // Remove caller arguments and receiver from the stack and return.
  __ slwi(r4, r4, Operand(kPointerSizeLog2));
  __ add(sp, sp, r4);
  __ addi(sp, sp, Operand(kPointerSize));
  Counters* counters = masm()->isolate()->counters();
  __ IncrementCounter(counters->constructed_objects(), 1, r4, r5);
  __ IncrementCounter(counters->constructed_objects_stub(), 1, r4, r5);
  __ blr();

  // Jump to the generic stub in case the specialized code cannot handle the
  // construction.
  __ bind(&generic_stub_call);
  Handle<Code> code = masm()->isolate()->builtins()->JSConstructStubGeneric();
  __ Jump(code, RelocInfo::CODE_TARGET);

  // Return the generated code.
  return GetCode();
}


#undef __
#define __ ACCESS_MASM(masm)


void KeyedLoadStubCompiler::GenerateLoadDictionaryElement(
    MacroAssembler* masm) {
  EMIT_STUB_MARKER(71);

  // ---------- S t a t e --------------
  //  -- lr     : return address
  //  -- r3     : key
  //  -- r4     : receiver
  // -----------------------------------
  Label slow, miss_force_generic;

  Register key = r3;
  Register receiver = r4;

  __ JumpIfNotSmi(key, &miss_force_generic);
  __ srawi(r5, key, kSmiTagSize);
  __ lwz(r7, FieldMemOperand(receiver, JSObject::kElementsOffset));
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
  Handle<Code> slow_ic =
      masm->isolate()->builtins()->KeyedLoadIC_Slow();
  __ Jump(slow_ic, RelocInfo::CODE_TARGET);

  // Miss case, call the runtime.
  __ bind(&miss_force_generic);

  // ---------- S t a t e --------------
  //  -- lr     : return address
  //  -- r3     : key
  //  -- r4     : receiver
  // -----------------------------------

  Handle<Code> miss_ic =
      masm->isolate()->builtins()->KeyedLoadIC_MissForceGeneric();
  __ Jump(miss_ic, RelocInfo::CODE_TARGET);
}


#if 0  // never used roohack
static bool IsElementTypeSigned(ElementsKind elements_kind) {
  switch (elements_kind) {
    case EXTERNAL_BYTE_ELEMENTS:
    case EXTERNAL_SHORT_ELEMENTS:
    case EXTERNAL_INT_ELEMENTS:
      return true;

    case EXTERNAL_UNSIGNED_BYTE_ELEMENTS:
    case EXTERNAL_UNSIGNED_SHORT_ELEMENTS:
    case EXTERNAL_UNSIGNED_INT_ELEMENTS:
    case EXTERNAL_PIXEL_ELEMENTS:
      return false;

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
      return false;
  }
  return false;
}
#endif


static void GenerateSmiKeyCheck(MacroAssembler* masm,
                                Register key,
                                Register scratch0,
                                Register scratch1,
                                DwVfpRegister double_scratch0,
                                Label* fail) {
  EMIT_STUB_MARKER(72);

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
  __ sub(ip, key, Operand(kHeapObjectTag));
  __ lfd(double_scratch0, MemOperand(ip, HeapNumber::kValueOffset));
  __ EmitVFPTruncate(kRoundToZero,
                     d11,
                     double_scratch0,
                     scratch0,
                     scratch1,
                     kCheckForInexactConversion);
  __ boverflow(fail);
  __ fctiwz(double_scratch0, double_scratch0);
  __ sub(sp, sp, Operand(8));
  __ stfd(double_scratch0, MemOperand(sp, 0));
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
  __ lwz(scratch0, MemOperand(sp, 0));
#else
  __ lwz(scratch0, MemOperand(sp, 4));
#endif
  __ addi(sp, sp, Operand(8));
  __ TrySmiTag(scratch0, fail, scratch1);
  __ mr(key, scratch0);
  __ bind(&key_ok);
}


void KeyedLoadStubCompiler::GenerateLoadExternalArray(
    MacroAssembler* masm,
    ElementsKind elements_kind) {
  EMIT_STUB_MARKER(73);

  // ---------- S t a t e --------------
  //  -- lr     : return address
  //  -- r3     : key
  //  -- r4     : receiver
  // -----------------------------------
  Label miss_force_generic, slow, failed_allocation;

  Register key = r3;
  Register receiver = r4;

  // This stub is meant to be tail-jumped to, the receiver must already
  // have been verified by the caller to not be a smi.

  // Check that the key is a smi or a heap number convertible to a smi.
  GenerateSmiKeyCheck(masm, key, r7, r8, d1, &miss_force_generic);

  __ lwz(r6, FieldMemOperand(receiver, JSObject::kElementsOffset));
  // r6: elements array

  // Check that the index is in range.
  __ lwz(ip, FieldMemOperand(r6, ExternalArray::kLengthOffset));
  __ cmpl(key, ip);
  // Unsigned comparison catches both negative and too-large values.
  __ bge(&miss_force_generic);

  __ lwz(r6, FieldMemOperand(r6, ExternalArray::kExternalPointerOffset));
  // r6: base pointer of external storage

  // We are not untagging smi key and instead work with it
  // as if it was premultiplied by 2.
  STATIC_ASSERT((kSmiTag == 0) && (kSmiTagSize == 1));

  Register value = r5;
  switch (elements_kind) {
    case EXTERNAL_BYTE_ELEMENTS:
      __ srwi(value, key, Operand(1));
      __ add(value, r6, value);
      __ lbz(value, MemOperand(value));
      __ extsb(value, value);
      break;
    case EXTERNAL_PIXEL_ELEMENTS:
    case EXTERNAL_UNSIGNED_BYTE_ELEMENTS:
      __ srwi(value, key, Operand(1));
      __ add(value, r6, value);
      __ lbz(value, MemOperand(value));
      break;
    case EXTERNAL_SHORT_ELEMENTS:
      __ add(value, r6, key);
      __ lhz(value, MemOperand(value));
      __ extsh(value, value);
      break;
    case EXTERNAL_UNSIGNED_SHORT_ELEMENTS:
      __ add(value, r6, key);
      __ lhz(value, MemOperand(value));
      break;
    case EXTERNAL_INT_ELEMENTS:
    case EXTERNAL_UNSIGNED_INT_ELEMENTS:
      __ slwi(value, key, Operand(1));
      __ add(value, value, r6);
      __ lwz(value, MemOperand(value));
      break;
    case EXTERNAL_FLOAT_ELEMENTS:
      __ slwi(value, key, Operand(1));
      __ add(value, value, r6);
      __ lfs(d0, MemOperand(value, 0));
      break;
    case EXTERNAL_DOUBLE_ELEMENTS:
      __ slwi(value, key, Operand(2));
      __ add(value, value, r6);
      __ lfd(d0, MemOperand(value, 0));
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

  // For integer array types:
  // r5: value
  // For float array type:
  // d0: single value
  // For double array type:
  // d0: double value

  if (elements_kind == EXTERNAL_INT_ELEMENTS) {
    // For the Int and UnsignedInt array types, we need to see whether
    // the value can be represented in a Smi. If not, we need to convert
    // it to a HeapNumber.
    Label box_int, smi_ok;
    __ rlwinm(r0, value, 2, 30, 31, SetRC);
    __ beq(&smi_ok, cr0);     // If both high bits are clear smi is ok
    __ cmpi(r0, Operand(3));
    __ bne(&box_int);         // If both high bits are not set, we box it
    __ bind(&smi_ok);
    // Tag integer as smi and return it.
    __ slwi(r3, value, Operand(kSmiTagSize));
    __ Ret();

    __ bind(&box_int);
    // Allocate a HeapNumber for the result and perform int-to-double
    // conversion.  Don't touch r3 or r4 as they are needed if allocation
    // fails.
    __ LoadRoot(r9, Heap::kHeapNumberMapRootIndex);
    __ AllocateHeapNumber(r8, r6, r7, r9, &slow);
    // Now we can use r3 for the result as key is not needed any more.
    __ mr(r3, r8);

    FloatingPointHelper::ConvertIntToDouble(
      masm, value, FloatingPointHelper::kFPRegisters,
      d0, r7, r7,  // r7 unused as we're using kFPRegisters
      d2);
    __ stfd(d0, FieldMemOperand(r3, HeapNumber::kValueOffset));
    __ Ret();

  } else if (elements_kind == EXTERNAL_UNSIGNED_INT_ELEMENTS) {
    // The test is different for unsigned int values. Since we need
    // the value to be in the range of a positive smi, we can't
    // handle either of the top two bits being set in the value.
    Label box_int;
    __ rlwinm(r0, value, 2, 30, 31, SetRC);
    __ bne(&box_int, cr0);   // If either two high bits are set, box

    // Tag integer as smi and return it.
    __ slwi(r3, value, Operand(kSmiTagSize));
    __ Ret();

    __ bind(&box_int);
    // Allocate a HeapNumber for the result and perform int-to-double
    // conversion. Don't use r3 and r4 as AllocateHeapNumber clobbers all
    // registers - also when jumping due to exhausted young space.
    __ LoadRoot(r9, Heap::kHeapNumberMapRootIndex);
    __ AllocateHeapNumber(r8, r6, r7, r9, &slow);
    __ mr(r3, r8);

    FloatingPointHelper::ConvertUnsignedIntToDouble(
      masm, value, FloatingPointHelper::kFPRegisters,
      d0, r7, r7,  // r7 unused as we're using kFPRegisters
      d2);
    __ stfd(d0, FieldMemOperand(r3, HeapNumber::kValueOffset));
    __ Ret();

  } else if (elements_kind == EXTERNAL_FLOAT_ELEMENTS) {
    // For the floating-point array type, we need to always allocate a
    // HeapNumber.
    // Allocate a HeapNumber for the result. Don't use r3 and r4 as
    // AllocateHeapNumber clobbers all registers - also when jumping due to
    // exhausted young space.
    __ LoadRoot(r9, Heap::kHeapNumberMapRootIndex);
    __ AllocateHeapNumber(r5, r6, r7, r9, &slow);
    __ stfd(d0, FieldMemOperand(r5, HeapNumber::kValueOffset));
    __ mr(r3, r5);
    __ Ret();

  } else if (elements_kind == EXTERNAL_DOUBLE_ELEMENTS) {
    // Allocate a HeapNumber for the result. Don't use r3 and r4 as
    // AllocateHeapNumber clobbers all registers - also when jumping due to
    // exhausted young space.
    __ LoadRoot(r9, Heap::kHeapNumberMapRootIndex);
    __ AllocateHeapNumber(r5, r6, r7, r9, &slow);
    __ stfd(d0, FieldMemOperand(r5, HeapNumber::kValueOffset));
    __ mr(r3, r5);
    __ Ret();

  } else {
    // Tag integer as smi and return it.
    __ slwi(r3, value, Operand(kSmiTagSize));
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

  __ Push(r4, r3);

  __ TailCallRuntime(Runtime::kKeyedGetProperty, 2, 1);

  __ bind(&miss_force_generic);
  Handle<Code> stub =
      masm->isolate()->builtins()->KeyedLoadIC_MissForceGeneric();
  __ Jump(stub, RelocInfo::CODE_TARGET);
}


void KeyedStoreStubCompiler::GenerateStoreExternalArray(
    MacroAssembler* masm,
    ElementsKind elements_kind) {
  EMIT_STUB_MARKER(74);

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
  GenerateSmiKeyCheck(masm, key, r7, r8, d1, &miss_force_generic);

  __ lwz(r6, FieldMemOperand(receiver, JSObject::kElementsOffset));

  // Check that the index is in range
  __ lwz(ip, FieldMemOperand(r6, ExternalArray::kLengthOffset));
  __ cmpl(key, ip);
  // Unsigned comparison catches both negative and too-large values.
  __ bge(&miss_force_generic);

  // Handle both smis and HeapNumbers in the fast path. Go to the
  // runtime for all other kinds of values.
  // r6: external array.
  if (elements_kind == EXTERNAL_PIXEL_ELEMENTS) {
    // Double to pixel conversion is only implemented in the runtime for now.
    __ JumpIfNotSmi(value, &slow);
  } else {
    __ JumpIfNotSmi(value, &check_heap_number);
  }
  __ SmiUntag(r8, value);
  __ lwz(r6, FieldMemOperand(r6, ExternalArray::kExternalPointerOffset));

  // r6: base pointer of external storage.
  // r8: value (integer).
  // r10: scratch register
  switch (elements_kind) {
    case EXTERNAL_PIXEL_ELEMENTS:
      // Clamp the value to [0..255].
//      __ Usat(r8, 8, Operand(r8));  not needed on PPC
      __ srwi(r10, key, Operand(1));
      __ add(r10, r6, r10);
      __ stb(r8, MemOperand(r10));
      break;
    case EXTERNAL_BYTE_ELEMENTS:
    case EXTERNAL_UNSIGNED_BYTE_ELEMENTS:
      __ srwi(r10, key, Operand(1));
      __ add(r10, r6, r10);
      __ stb(r8, MemOperand(r10));
      break;
    case EXTERNAL_SHORT_ELEMENTS:
    case EXTERNAL_UNSIGNED_SHORT_ELEMENTS:
      __ add(r10, r6, key);
      __ sth(r8, MemOperand(r10));
      break;
    case EXTERNAL_INT_ELEMENTS:
    case EXTERNAL_UNSIGNED_INT_ELEMENTS:
      __ slwi(r10, key, Operand(1));
      __ add(r10, r6, r10);
      __ stw(r8, MemOperand(r10));
      break;
    case EXTERNAL_FLOAT_ELEMENTS:
      // Perform int-to-float conversion and store to memory.
      __ slwi(r10, key, Operand(1));
      __ add(r10, r6, r10);
      // r10: efective address of the float element
      FloatingPointHelper::ConvertIntToFloat(masm, d0, r8, r9);
      __ stfs(d0, MemOperand(r10, 0));
      break;
    case EXTERNAL_DOUBLE_ELEMENTS:
      __ slwi(r10, key, Operand(2));
      __ add(r6, r6, r10);
      // r6: effective address of the double element
      FloatingPointHelper::ConvertIntToDouble(
          masm, r8, FloatingPointHelper::kFPRegisters,
          d0, r9, r10,  // These are: double_dst, dst1, dst2.
          d2);  // These are: scratch2, single_scratch.
      __ stfd(d0, MemOperand(r6, 0));
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

      __ lwz(r6, FieldMemOperand(r6, ExternalArray::kExternalPointerOffset));

      // r6: base pointer of external storage.

      // The WebGL specification leaves the behavior of storing NaN and
      // +/-Infinity into integer arrays basically undefined. For more
      // reproducible behavior, convert these to zero.

      if (elements_kind == EXTERNAL_FLOAT_ELEMENTS) {
        // vldr requires offset to be a multiple of 4 so we can not
        // include -kHeapObjectTag into it.
        __ lfd(d0, FieldMemOperand(r3, HeapNumber::kValueOffset));
        __ slwi(r8, key, Operand(1));
        __ add(r8, r6, r8);
        __ frsp(d0, d0);
        __ stfs(d0, MemOperand(r8, 0));
      } else if (elements_kind == EXTERNAL_DOUBLE_ELEMENTS) {
        __ lfd(d0, FieldMemOperand(r3, HeapNumber::kValueOffset));
        __ slwi(r8, key, Operand(2));
        __ add(r8, r6, r8);
        __ stfd(d0, MemOperand(r8, 0));
      } else {
        // Hoisted load.
        __ mr(r8, value);
        __ lfd(d0, FieldMemOperand(r8, HeapNumber::kValueOffset));

        if (elements_kind == EXTERNAL_UNSIGNED_INT_ELEMENTS) {
            // Perform float-to-uint conversion with truncation (round-to-zero)
            // behavior.
            FloatingPointHelper::ConvertDoubleToUnsignedInt(masm, d0, r8, r9,
                                                            d1);
        } else {
            // Perform float-to-int conversion with truncation (round-to-zero)
            // behavior.
            FloatingPointHelper::ConvertDoubleToInt(masm, d0, r8, r9, d1);
        }

        switch (elements_kind) {
          case EXTERNAL_BYTE_ELEMENTS:
          case EXTERNAL_UNSIGNED_BYTE_ELEMENTS:
            __ srwi(r10, key, Operand(1));
            __ add(r10, r6, r10);
            __ stb(r8, MemOperand(r10));
            break;
          case EXTERNAL_SHORT_ELEMENTS:
          case EXTERNAL_UNSIGNED_SHORT_ELEMENTS:
            __ add(r10, r6, key);
            __ sth(r8, MemOperand(r10));
            break;
          case EXTERNAL_INT_ELEMENTS:
          case EXTERNAL_UNSIGNED_INT_ELEMENTS:
            __ slwi(r10, key, Operand(1));
            __ add(r10, r6, r10);
            __ stw(r8, MemOperand(r10));
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
  Handle<Code> slow_ic =
      masm->isolate()->builtins()->KeyedStoreIC_Slow();
  __ Jump(slow_ic, RelocInfo::CODE_TARGET);

  // Miss case, call the runtime.
  __ bind(&miss_force_generic);

  // ---------- S t a t e --------------
  //  -- lr     : return address
  //  -- r3     : key
  //  -- r4     : receiver
  // -----------------------------------

  Handle<Code> miss_ic =
      masm->isolate()->builtins()->KeyedStoreIC_MissForceGeneric();
  __ Jump(miss_ic, RelocInfo::CODE_TARGET);
}


void KeyedLoadStubCompiler::GenerateLoadFastElement(MacroAssembler* masm) {
  EMIT_STUB_MARKER(75);

  // ----------- S t a t e -------------
  //  -- lr    : return address
  //  -- r3    : key
  //  -- r4    : receiver
  // -----------------------------------
  Label miss_force_generic;

  // This stub is meant to be tail-jumped to, the receiver must already
  // have been verified by the caller to not be a smi.

  // Check that the key is a smi or a heap number convertible to a smi.
  GenerateSmiKeyCheck(masm, r3, r7, r8, d1, &miss_force_generic);

  // Get the elements array.
  __ lwz(r5, FieldMemOperand(r4, JSObject::kElementsOffset));
  __ AssertFastElements(r5);

  // Check that the key is within bounds.
  __ lwz(r6, FieldMemOperand(r5, FixedArray::kLengthOffset));
  __ cmpl(r3, r6);
  __ bge(&miss_force_generic);

  // Load the result and make sure it's not the hole.
  __ addi(r6, r5, Operand(FixedArray::kHeaderSize - kHeapObjectTag));
  STATIC_ASSERT(kSmiTag == 0 && kSmiTagSize < kPointerSizeLog2);
  __ slwi(r7, r3, Operand(kPointerSizeLog2 - kSmiTagSize));
  __ add(r7, r6, r7);
  __ lwz(r7, MemOperand(r7));
  __ LoadRoot(ip, Heap::kTheHoleValueRootIndex);
  __ cmp(r7, ip);
  __ beq(&miss_force_generic);
  __ mr(r3, r7);
  __ Ret();

  __ bind(&miss_force_generic);
  Handle<Code> stub =
      masm->isolate()->builtins()->KeyedLoadIC_MissForceGeneric();
  __ Jump(stub, RelocInfo::CODE_TARGET);
}


void KeyedLoadStubCompiler::GenerateLoadFastDoubleElement(
    MacroAssembler* masm) {
  EMIT_STUB_MARKER(76);

  // ----------- S t a t e -------------
  //  -- lr    : return address
  //  -- r3    : key
  //  -- r4    : receiver
  // -----------------------------------
  Label miss_force_generic, slow_allocate_heapnumber;

  Register key_reg = r3;
  Register receiver_reg = r4;
  Register elements_reg = r5;
  Register heap_number_reg = r5;
  Register indexed_double_offset = r6;
  Register scratch = r7;
  Register scratch2 = r8;
  Register scratch3 = r9;
  Register heap_number_map = r10;

  // This stub is meant to be tail-jumped to, the receiver must already
  // have been verified by the caller to not be a smi.

  // Check that the key is a smi or a heap number convertible to a smi.
  GenerateSmiKeyCheck(masm, key_reg, r7, r8, d1, &miss_force_generic);

  // Get the elements array.
  __ lwz(elements_reg,
         FieldMemOperand(receiver_reg, JSObject::kElementsOffset));

  // Check that the key is within bounds.
  __ lwz(scratch, FieldMemOperand(elements_reg, FixedArray::kLengthOffset));
  __ cmpl(key_reg, scratch);
  __ bge(&miss_force_generic);

  // Load the upper word of the double in the fixed array and test for NaN.
  __ slwi(indexed_double_offset, key_reg,
         Operand(kDoubleSizeLog2 - kSmiTagSize));
  __ add(indexed_double_offset, elements_reg, indexed_double_offset);
#if defined(V8_HOST_ARCH_PPC)
  uint32_t upper_32_offset = FixedArray::kHeaderSize;
#else
  uint32_t upper_32_offset = FixedArray::kHeaderSize + sizeof(kHoleNanLower32);
#endif
  __ lwz(scratch, FieldMemOperand(indexed_double_offset, upper_32_offset));
  __ mov(r0, Operand(kHoleNanUpper32));
  __ cmp(scratch, r0);
  __ beq(&miss_force_generic);

  // Non-NaN. Allocate a new heap number and copy the double value into it.
  __ LoadRoot(heap_number_map, Heap::kHeapNumberMapRootIndex);
  __ AllocateHeapNumber(heap_number_reg, scratch2, scratch3,
                        heap_number_map, &slow_allocate_heapnumber);

  // Don't need to reload the upper 32 bits of the double, it's already in
  // scratch.
  __ stw(scratch, FieldMemOperand(heap_number_reg,
                                  HeapNumber::kExponentOffset));
#if defined(V8_HOST_ARCH_PPC)
  __ lwz(scratch, FieldMemOperand(indexed_double_offset,
                                  FixedArray::kHeaderSize+4));
#else
  __ lwz(scratch, FieldMemOperand(indexed_double_offset,
                                  FixedArray::kHeaderSize));
#endif
  __ stw(scratch, FieldMemOperand(heap_number_reg,
                                  HeapNumber::kMantissaOffset));

  __ mr(r3, heap_number_reg);
  __ Ret();

  __ bind(&slow_allocate_heapnumber);
  Handle<Code> slow_ic =
      masm->isolate()->builtins()->KeyedLoadIC_Slow();
  __ Jump(slow_ic, RelocInfo::CODE_TARGET);

  __ bind(&miss_force_generic);
  Handle<Code> miss_ic =
      masm->isolate()->builtins()->KeyedLoadIC_MissForceGeneric();
  __ Jump(miss_ic, RelocInfo::CODE_TARGET);
}


void KeyedStoreStubCompiler::GenerateStoreFastElement(
    MacroAssembler* masm,
    bool is_js_array,
    ElementsKind elements_kind,
    KeyedAccessGrowMode grow_mode) {
  EMIT_STUB_MARKER(77);

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
  GenerateSmiKeyCheck(masm, key_reg, r7, r8, d1, &miss_force_generic);

  if (IsFastSmiElementsKind(elements_kind)) {
    __ JumpIfNotSmi(value_reg, &transition_elements_kind);
  }

  // Check that the key is within bounds.
  __ lwz(elements_reg,
         FieldMemOperand(receiver_reg, JSObject::kElementsOffset));
  if (is_js_array) {
    __ lwz(scratch, FieldMemOperand(receiver_reg, JSArray::kLengthOffset));
  } else {
    __ lwz(scratch, FieldMemOperand(elements_reg, FixedArray::kLengthOffset));
  }
  // Compare smis.
  __ cmpl(key_reg, scratch);
  if (is_js_array && grow_mode == ALLOW_JSARRAY_GROWTH) {
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
    STATIC_ASSERT(kSmiTag == 0 && kSmiTagSize < kPointerSizeLog2);
    __ slwi(scratch2, key_reg, Operand(kPointerSizeLog2 - kSmiTagSize));
    __ add(scratch, scratch, scratch2);
    __ stw(value_reg, MemOperand(scratch));
  } else {
    ASSERT(IsFastObjectElementsKind(elements_kind));
    __ addi(scratch,
            elements_reg,
            Operand(FixedArray::kHeaderSize - kHeapObjectTag));
    STATIC_ASSERT(kSmiTag == 0 && kSmiTagSize < kPointerSizeLog2);
    __ slwi(scratch2, key_reg, Operand(kPointerSizeLog2 - kSmiTagSize));
    __ add(scratch, scratch, scratch2);
    __ stw(value_reg, MemOperand(scratch));
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
  Handle<Code> ic =
      masm->isolate()->builtins()->KeyedStoreIC_MissForceGeneric();
  __ Jump(ic, RelocInfo::CODE_TARGET);

  __ bind(&transition_elements_kind);
  Handle<Code> ic_miss = masm->isolate()->builtins()->KeyedStoreIC_Miss();
  __ Jump(ic_miss, RelocInfo::CODE_TARGET);

  if (is_js_array && grow_mode == ALLOW_JSARRAY_GROWTH) {
    // Grow the array by a single element if possible.
    __ bind(&grow);

    // Make sure the array is only growing by a single element, anything else
    // must be handled by the runtime. Flags already set by previous compare.
    __ bne(&miss_force_generic);

    // Check for the empty array, and preallocate a small backing store if
    // possible.
    __ lwz(length_reg,
           FieldMemOperand(receiver_reg, JSArray::kLengthOffset));
    __ lwz(elements_reg,
           FieldMemOperand(receiver_reg, JSObject::kElementsOffset));
    __ CompareRoot(elements_reg, Heap::kEmptyFixedArrayRootIndex);
    __ bne(&check_capacity);

    int size = FixedArray::SizeFor(JSArray::kPreallocatedArrayElements);
    __ AllocateInNewSpace(size, elements_reg, scratch, scratch2, &slow,
                          TAG_OBJECT);

    __ LoadRoot(scratch, Heap::kFixedArrayMapRootIndex);
    __ stw(scratch, FieldMemOperand(elements_reg, JSObject::kMapOffset));
    __ mov(scratch, Operand(Smi::FromInt(JSArray::kPreallocatedArrayElements)));
    __ stw(scratch, FieldMemOperand(elements_reg, FixedArray::kLengthOffset));
    __ LoadRoot(scratch, Heap::kTheHoleValueRootIndex);
    for (int i = 1; i < JSArray::kPreallocatedArrayElements; ++i) {
      __ stw(scratch, FieldMemOperand(elements_reg, FixedArray::SizeFor(i)));
    }

    // Store the element at index zero.
    __ stw(value_reg, FieldMemOperand(elements_reg, FixedArray::SizeFor(0)));

    // Install the new backing store in the JSArray.
    __ stw(elements_reg,
           FieldMemOperand(receiver_reg, JSObject::kElementsOffset));
    __ RecordWriteField(receiver_reg, JSObject::kElementsOffset, elements_reg,
                        scratch, kLRHasNotBeenSaved, kDontSaveFPRegs,
                        EMIT_REMEMBERED_SET, OMIT_SMI_CHECK);

    // Increment the length of the array.
    __ li(length_reg, Operand(Smi::FromInt(1)));
    __ stw(length_reg, FieldMemOperand(receiver_reg, JSArray::kLengthOffset));
    __ Ret();

    __ bind(&check_capacity);
    // Check for cow elements, in general they are not handled by this stub
    __ CheckMap(elements_reg,
                scratch,
                Heap::kFixedCOWArrayMapRootIndex,
                &miss_force_generic,
                DONT_DO_SMI_CHECK);

    __ lwz(scratch, FieldMemOperand(elements_reg, FixedArray::kLengthOffset));
    __ cmpl(length_reg, scratch);
    __ bge(&slow);

    // Grow the array and finish the store.
    __ addi(length_reg, length_reg, Operand(Smi::FromInt(1)));
    __ stw(length_reg, FieldMemOperand(receiver_reg, JSArray::kLengthOffset));
    __ jmp(&finish_store);

    __ bind(&slow);
    Handle<Code> ic_slow = masm->isolate()->builtins()->KeyedStoreIC_Slow();
    __ Jump(ic_slow, RelocInfo::CODE_TARGET);
  }
}


void KeyedStoreStubCompiler::GenerateStoreFastDoubleElement(
    MacroAssembler* masm,
    bool is_js_array,
    KeyedAccessGrowMode grow_mode) {
  EMIT_STUB_MARKER(78);

  // ----------- S t a t e -------------
  //  -- r3    : value
  //  -- r4    : key
  //  -- r5    : receiver
  //  -- lr    : return address
  //  -- r6    : scratch
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
  Register scratch3 = r9;
  Register scratch4 = r10;
  Register length_reg = r10;

  // This stub is meant to be tail-jumped to, the receiver must already
  // have been verified by the caller to not be a smi.

  // Check that the key is a smi or a heap number convertible to a smi.
  GenerateSmiKeyCheck(masm, key_reg, r7, r8, d1, &miss_force_generic);

  __ lwz(elements_reg,
         FieldMemOperand(receiver_reg, JSObject::kElementsOffset));

  // Check that the key is within bounds.
  if (is_js_array) {
    __ lwz(scratch1, FieldMemOperand(receiver_reg, JSArray::kLengthOffset));
  } else {
    __ lwz(scratch1,
           FieldMemOperand(elements_reg, FixedArray::kLengthOffset));
  }
  // Compare smis, unsigned compare catches both negative and out-of-bound
  // indexes.
  __ cmpl(key_reg, scratch1);
  if (grow_mode == ALLOW_JSARRAY_GROWTH) {
    __ bge(&grow);
  } else {
    __ bge(&miss_force_generic);
  }

  __ bind(&finish_store);
  __ StoreNumberToDoubleElements(value_reg,
                                 key_reg,
                                 receiver_reg,
                                 // All registers after this are overwritten.
                                 elements_reg,
                                 scratch1,
                                 scratch2,
                                 scratch3,
                                 scratch4,
                                 &transition_elements_kind);
  __ Ret();

  // Handle store cache miss, replacing the ic with the generic stub.
  __ bind(&miss_force_generic);
  Handle<Code> ic =
      masm->isolate()->builtins()->KeyedStoreIC_MissForceGeneric();
  __ Jump(ic, RelocInfo::CODE_TARGET);

  __ bind(&transition_elements_kind);
  Handle<Code> ic_miss = masm->isolate()->builtins()->KeyedStoreIC_Miss();
  __ Jump(ic_miss, RelocInfo::CODE_TARGET);

  if (is_js_array && grow_mode == ALLOW_JSARRAY_GROWTH) {
    // Grow the array by a single element if possible.
    __ bind(&grow);

    // Make sure the array is only growing by a single element, anything else
    // must be handled by the runtime. Flags already set by previous compare.
    __ bne(&miss_force_generic);

    // Transition on values that can't be stored in a FixedDoubleArray.
    Label value_is_smi;
    __ JumpIfSmi(value_reg, &value_is_smi);
    __ lwz(scratch1, FieldMemOperand(value_reg, HeapObject::kMapOffset));
    __ CompareRoot(scratch1, Heap::kHeapNumberMapRootIndex);
    __ bne(&transition_elements_kind);
    __ bind(&value_is_smi);

    // Check for the empty array, and preallocate a small backing store if
    // possible.
    __ lwz(length_reg,
           FieldMemOperand(receiver_reg, JSArray::kLengthOffset));
    __ lwz(elements_reg,
           FieldMemOperand(receiver_reg, JSObject::kElementsOffset));
    __ CompareRoot(elements_reg, Heap::kEmptyFixedArrayRootIndex);
    __ bne(&check_capacity);

    int size = FixedDoubleArray::SizeFor(JSArray::kPreallocatedArrayElements);
    __ AllocateInNewSpace(size, elements_reg, scratch1, scratch2, &slow,
                          TAG_OBJECT);

    // Initialize the new FixedDoubleArray. Leave elements unitialized for
    // efficiency, they are guaranteed to be initialized before use.
    __ LoadRoot(scratch1, Heap::kFixedDoubleArrayMapRootIndex);
    __ stw(scratch1, FieldMemOperand(elements_reg, JSObject::kMapOffset));
    __ mov(scratch1,
           Operand(Smi::FromInt(JSArray::kPreallocatedArrayElements)));
    __ stw(scratch1,
           FieldMemOperand(elements_reg, FixedDoubleArray::kLengthOffset));

    // Install the new backing store in the JSArray.
    __ stw(elements_reg,
           FieldMemOperand(receiver_reg, JSObject::kElementsOffset));
    __ RecordWriteField(receiver_reg, JSObject::kElementsOffset, elements_reg,
                        scratch1, kLRHasNotBeenSaved, kDontSaveFPRegs,
                        EMIT_REMEMBERED_SET, OMIT_SMI_CHECK);

    // Increment the length of the array.
    __ li(length_reg, Operand(Smi::FromInt(1)));
    __ stw(length_reg, FieldMemOperand(receiver_reg, JSArray::kLengthOffset));
    __ lwz(elements_reg,
           FieldMemOperand(receiver_reg, JSObject::kElementsOffset));
    __ jmp(&finish_store);

    __ bind(&check_capacity);
    // Make sure that the backing store can hold additional elements.
    __ lwz(scratch1,
           FieldMemOperand(elements_reg, FixedDoubleArray::kLengthOffset));
    __ cmpl(length_reg, scratch1);
    __ bge(&slow);

    // Grow the array and finish the store.
    __ addi(length_reg, length_reg, Operand(Smi::FromInt(1)));
    __ stw(length_reg, FieldMemOperand(receiver_reg, JSArray::kLengthOffset));
    __ jmp(&finish_store);

    __ bind(&slow);
    Handle<Code> ic_slow = masm->isolate()->builtins()->KeyedStoreIC_Slow();
    __ Jump(ic_slow, RelocInfo::CODE_TARGET);
  }
}


#undef __

} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_PPC
