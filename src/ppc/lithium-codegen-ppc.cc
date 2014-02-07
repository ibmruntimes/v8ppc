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

#include "ppc/lithium-codegen-ppc.h"
#include "ppc/lithium-gap-resolver-ppc.h"
#include "code-stubs.h"
#include "stub-cache.h"
#include "hydrogen-osr.h"

namespace v8 {
namespace internal {


class SafepointGenerator V8_FINAL : public CallWrapper {
 public:
  SafepointGenerator(LCodeGen* codegen,
                     LPointerMap* pointers,
                     Safepoint::DeoptMode mode)
      : codegen_(codegen),
        pointers_(pointers),
        deopt_mode_(mode) { }
  virtual ~SafepointGenerator() { }

  virtual void BeforeCall(int call_size) const V8_OVERRIDE {}

  virtual void AfterCall() const V8_OVERRIDE {
    codegen_->RecordSafepoint(pointers_, deopt_mode_);
  }

 private:
  LCodeGen* codegen_;
  LPointerMap* pointers_;
  Safepoint::DeoptMode deopt_mode_;
};


#define __ masm()->

bool LCodeGen::GenerateCode() {
  LPhase phase("Z_Code generation", chunk());
  ASSERT(is_unused());
  status_ = GENERATING;

  // Open a frame scope to indicate that there is a frame on the stack.  The
  // NONE indicates that the scope shouldn't actually generate code to set up
  // the frame (that is done in GeneratePrologue).
  FrameScope frame_scope(masm_, StackFrame::NONE);

  return GeneratePrologue() &&
      GenerateBody() &&
      GenerateDeferredCode() &&
      GenerateDeoptJumpTable() &&
      GenerateSafepointTable();
}


void LCodeGen::FinishCode(Handle<Code> code) {
  ASSERT(is_done());
  code->set_stack_slots(GetStackSlotCount());
  code->set_safepoint_table_offset(safepoints_.GetCodeOffset());
  if (FLAG_weak_embedded_maps_in_optimized_code) {
    RegisterDependentCodeForEmbeddedMaps(code);
  }
  PopulateDeoptimizationData(code);
  info()->CommitDependencies(code);
}


void LCodeGen::Abort(BailoutReason reason) {
  info()->set_bailout_reason(reason);
  status_ = ABORTED;
}


bool LCodeGen::GeneratePrologue() {
  ASSERT(is_generating());

  if (info()->IsOptimizing()) {
    ProfileEntryHookStub::MaybeCallEntryHook(masm_);

#ifdef DEBUG
    if (strlen(FLAG_stop_at) > 0 &&
        info_->function()->name()->IsUtf8EqualTo(CStrVector(FLAG_stop_at))) {
      __ stop("stop_at");
    }
#endif

    // r4: Callee's JS function.
    // cp: Callee's context.
    // fp: Caller's frame pointer.
    // lr: Caller's pc.

    // Strict mode functions and builtins need to replace the receiver
    // with undefined when called as functions (without an explicit
    // receiver object). r8 is zero for method calls and non-zero for
    // function calls.
    if (!info_->is_classic_mode() || info_->is_native()) {
      Label ok;
      __ cmpi(r8, Operand::Zero());
      __ beq(&ok);
      int receiver_offset = scope()->num_parameters() * kPointerSize;
      __ LoadRoot(r5, Heap::kUndefinedValueRootIndex);
      __ StoreP(r5, MemOperand(sp, receiver_offset));
      __ bind(&ok);
    }
  }

  info()->set_prologue_offset(masm_->pc_offset());
  if (NeedsEagerFrame()) {
    __ Prologue(info()->IsStub() ? BUILD_STUB_FRAME : BUILD_FUNCTION_FRAME);
    frame_is_built_ = true;
    info_->AddNoFrameRange(0, masm_->pc_offset());
  }

  // Reserve space for the stack slots needed by the code.
  int slots = GetStackSlotCount();
  if (slots > 0) {
    __ subi(sp,  sp, Operand(slots * kPointerSize));
    if (FLAG_debug_code) {
      __ Push(r3, r4);
      __ li(r0, Operand(slots));
      __ mtctr(r0);
      __ addi(r3, sp, Operand((slots + 2) *  kPointerSize));
      __ mov(r4, Operand(kSlotsZapValue));
      Label loop;
      __ bind(&loop);
      __ StorePU(r4, MemOperand(r3, -kPointerSize));
      __ bdnz(&loop);
      __ Pop(r3, r4);
    }
  }

  if (info()->saves_caller_doubles()) {
    Comment(";;; Save clobbered callee double registers");
    int count = 0;
    BitVector* doubles = chunk()->allocated_double_registers();
    BitVector::Iterator save_iterator(doubles);
    while (!save_iterator.Done()) {
      __ stfd(DoubleRegister::FromAllocationIndex(save_iterator.Current()),
              MemOperand(sp, count * kDoubleSize));
      save_iterator.Advance();
      count++;
    }
  }

  // Possibly allocate a local context.
  int heap_slots = info()->num_heap_slots() - Context::MIN_CONTEXT_SLOTS;
  if (heap_slots > 0) {
    Comment(";;; Allocate local context");
    // Argument to NewContext is the function, which is in r4.
    __ push(r4);
    if (heap_slots <= FastNewContextStub::kMaximumSlots) {
      FastNewContextStub stub(heap_slots);
      __ CallStub(&stub);
    } else {
      __ CallRuntime(Runtime::kNewFunctionContext, 1);
    }
    RecordSafepoint(Safepoint::kNoLazyDeopt);
    // Context is returned in both r3 and cp.  It replaces the context
    // passed to us.  It's saved in the stack and kept live in cp.
    __ StoreP(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
    // Copy any necessary parameters into the context.
    int num_parameters = scope()->num_parameters();
    for (int i = 0; i < num_parameters; i++) {
      Variable* var = scope()->parameter(i);
      if (var->IsContextSlot()) {
        int parameter_offset = StandardFrameConstants::kCallerSPOffset +
            (num_parameters - 1 - i) * kPointerSize;
        // Load parameter from stack.
        __ LoadP(r3, MemOperand(fp, parameter_offset));
        // Store it in the context.
        MemOperand target = ContextOperand(cp, var->index());
        __ StoreP(r3, target, r0);
        // Update the write barrier. This clobbers r6 and r3.
        __ RecordWriteContextSlot(
            cp,
            target.offset(),
            r3,
            r6,
            GetLinkRegisterState(),
            kSaveFPRegs);
      }
    }
    Comment(";;; End allocate local context");
  }

  // Trace the call.
  if (FLAG_trace && info()->IsOptimizing()) {
    // We have not executed any compiled code yet, so cp still holds the
    // incoming context.
    __ CallRuntime(Runtime::kTraceEnter, 0);
  }
  return !is_aborted();
}


void LCodeGen::GenerateOsrPrologue() {
  // Generate the OSR entry prologue at the first unknown OSR value, or if there
  // are none, at the OSR entrypoint instruction.
  if (osr_pc_offset_ >= 0) return;

  osr_pc_offset_ = masm()->pc_offset();

  // Adjust the frame size, subsuming the unoptimized frame into the
  // optimized frame.
  int slots = GetStackSlotCount() - graph()->osr()->UnoptimizedFrameSlots();
  ASSERT(slots >= 0);
  __ subi(sp, sp, Operand(slots * kPointerSize));
}


bool LCodeGen::GenerateDeferredCode() {
  ASSERT(is_generating());
  if (deferred_.length() > 0) {
    for (int i = 0; !is_aborted() && i < deferred_.length(); i++) {
      LDeferredCode* code = deferred_[i];

      HValue* value =
          instructions_->at(code->instruction_index())->hydrogen_value();
      RecordAndWritePosition(value->position());

      Comment(";;; <@%d,#%d> "
              "-------------------- Deferred %s --------------------",
              code->instruction_index(),
              code->instr()->hydrogen_value()->id(),
              code->instr()->Mnemonic());
      __ bind(code->entry());
      if (NeedsDeferredFrame()) {
        Comment(";;; Build frame");
        ASSERT(!frame_is_built_);
        ASSERT(info()->IsStub());
        frame_is_built_ = true;
        __ mflr(r0);
        __ Push(r0, fp, cp);
        __ LoadSmiLiteral(scratch0(), Smi::FromInt(StackFrame::STUB));
        __ push(scratch0());
        __ addi(fp, sp, Operand(2 * kPointerSize));
        Comment(";;; Deferred code");
      }
      code->Generate();
      if (NeedsDeferredFrame()) {
        Comment(";;; Destroy frame");
        ASSERT(frame_is_built_);
        __ pop(ip);
        __ Pop(r0, fp, cp);
        __ mtlr(r0);
        frame_is_built_ = false;
      }
      __ b(code->exit());
    }
  }

  return !is_aborted();
}


bool LCodeGen::GenerateDeoptJumpTable() {
  if (deopt_jump_table_.length() > 0) {
    Comment(";;; -------------------- Jump table --------------------");
  }
  Label needs_frame;
  for (int i = 0; i < deopt_jump_table_.length(); i++) {
    Assembler::BlockTrampolinePoolScope block_trampoline_pool(masm_);
    __ bind(&deopt_jump_table_[i].label);
    Address entry = deopt_jump_table_[i].address;
    Deoptimizer::BailoutType type = deopt_jump_table_[i].bailout_type;
    int id = Deoptimizer::GetDeoptimizationId(isolate(), entry, type);
    if (id == Deoptimizer::kNotDeoptimizationEntry) {
      Comment(";;; jump table entry %d.", i);
    } else {
      Comment(";;; jump table entry %d: deoptimization bailout %d.", i, id);
    }
    __ mov(ip, Operand(ExternalReference::ForDeoptEntry(entry)));
    if (deopt_jump_table_[i].needs_frame) {
      if (needs_frame.is_bound()) {
        __ b(&needs_frame);
      } else {
        __ bind(&needs_frame);
        __ mflr(r0);
        __ Push(r0, fp, cp);
        // This variant of deopt can only be used with stubs. Since we don't
        // have a function pointer to install in the stack frame that we're
        // building, install a special marker there instead.
        ASSERT(info()->IsStub());
        __ LoadSmiLiteral(scratch0(), Smi::FromInt(StackFrame::STUB));
        __ push(scratch0());
        __ addi(fp, sp, Operand(2 * kPointerSize));
        __ Call(ip);
      }
    } else {
      __ Call(ip);
    }
  }

  // The deoptimization jump table is the last part of the instruction
  // sequence. Mark the generated code as done unless we bailed out.
  if (!is_aborted()) status_ = DONE;
  return !is_aborted();
}


bool LCodeGen::GenerateSafepointTable() {
  ASSERT(is_done());
  safepoints_.Emit(masm(), GetStackSlotCount());
  return !is_aborted();
}


Register LCodeGen::ToRegister(int index) const {
  return Register::FromAllocationIndex(index);
}


DoubleRegister LCodeGen::ToDoubleRegister(int index) const {
  return DoubleRegister::FromAllocationIndex(index);
}


Register LCodeGen::ToRegister(LOperand* op) const {
  ASSERT(op->IsRegister());
  return ToRegister(op->index());
}


Register LCodeGen::EmitLoadRegister(LOperand* op, Register scratch) {
  if (op->IsRegister()) {
    return ToRegister(op->index());
  } else if (op->IsConstantOperand()) {
    LConstantOperand* const_op = LConstantOperand::cast(op);
    HConstant* constant = chunk_->LookupConstant(const_op);
    Handle<Object> literal = constant->handle(isolate());
    Representation r = chunk_->LookupLiteralRepresentation(const_op);
    if (r.IsInteger32()) {
      ASSERT(literal->IsNumber());
      __ LoadIntLiteral(scratch, static_cast<int32_t>(literal->Number()));
    } else if (r.IsDouble()) {
      Abort(kEmitLoadRegisterUnsupportedDoubleImmediate);
    } else {
      ASSERT(r.IsSmiOrTagged());
      __ Move(scratch, literal);
    }
    return scratch;
  } else if (op->IsStackSlot() || op->IsArgument()) {
    __ LoadP(scratch, ToMemOperand(op));
    return scratch;
  }
  UNREACHABLE();
  return scratch;
}


void LCodeGen::EmitLoadIntegerConstant(LConstantOperand* const_op,
                                       Register dst) {
  ASSERT(IsInteger32(const_op));
  HConstant* constant = chunk_->LookupConstant(const_op);
  int32_t value = constant->Integer32Value();
  if (IsSmi(const_op)) {
    __ LoadSmiLiteral(dst, Smi::FromInt(value));
  } else {
    __ LoadIntLiteral(dst, value);
  }
}


DoubleRegister LCodeGen::ToDoubleRegister(LOperand* op) const {
  ASSERT(op->IsDoubleRegister());
  return ToDoubleRegister(op->index());
}


Handle<Object> LCodeGen::ToHandle(LConstantOperand* op) const {
  HConstant* constant = chunk_->LookupConstant(op);
  ASSERT(chunk_->LookupLiteralRepresentation(op).IsSmiOrTagged());
  return constant->handle(isolate());
}


bool LCodeGen::IsInteger32(LConstantOperand* op) const {
  return chunk_->LookupLiteralRepresentation(op).IsSmiOrInteger32();
}


bool LCodeGen::IsSmi(LConstantOperand* op) const {
  return chunk_->LookupLiteralRepresentation(op).IsSmi();
}


int32_t LCodeGen::ToInteger32(LConstantOperand* op) const {
  return ToRepresentation(op, Representation::Integer32());
}


intptr_t LCodeGen::ToRepresentation(LConstantOperand* op,
                                    const Representation& r) const {
  HConstant* constant = chunk_->LookupConstant(op);
  int32_t value = constant->Integer32Value();
  if (r.IsInteger32()) return value;
  ASSERT(r.IsSmiOrTagged());
  return reinterpret_cast<intptr_t>(Smi::FromInt(value));
}


Smi* LCodeGen::ToSmi(LConstantOperand* op) const {
  HConstant* constant = chunk_->LookupConstant(op);
  return Smi::FromInt(constant->Integer32Value());
}


double LCodeGen::ToDouble(LConstantOperand* op) const {
  HConstant* constant = chunk_->LookupConstant(op);
  ASSERT(constant->HasDoubleValue());
  return constant->DoubleValue();
}


Operand LCodeGen::ToOperand(LOperand* op) {
  if (op->IsConstantOperand()) {
    LConstantOperand* const_op = LConstantOperand::cast(op);
    HConstant* constant = chunk()->LookupConstant(const_op);
    Representation r = chunk_->LookupLiteralRepresentation(const_op);
    if (r.IsSmi()) {
      ASSERT(constant->HasSmiValue());
      return Operand(Smi::FromInt(constant->Integer32Value()));
    } else if (r.IsInteger32()) {
      ASSERT(constant->HasInteger32Value());
      return Operand(constant->Integer32Value());
    } else if (r.IsDouble()) {
      Abort(kToOperandUnsupportedDoubleImmediate);
    }
    ASSERT(r.IsTagged());
    return Operand(constant->handle(isolate()));
  } else if (op->IsRegister()) {
    return Operand(ToRegister(op));
  } else if (op->IsDoubleRegister()) {
    Abort(kToOperandIsDoubleRegisterUnimplemented);
    return Operand::Zero();
  }
  // Stack slots not implemented, use ToMemOperand instead.
  UNREACHABLE();
  return Operand::Zero();
}


MemOperand LCodeGen::ToMemOperand(LOperand* op) const {
  ASSERT(!op->IsRegister());
  ASSERT(!op->IsDoubleRegister());
  ASSERT(op->IsStackSlot() || op->IsDoubleStackSlot());
  return MemOperand(fp, StackSlotOffset(op->index()));
}


MemOperand LCodeGen::ToHighMemOperand(LOperand* op) const {
  ASSERT(op->IsDoubleStackSlot());
  return MemOperand(fp, StackSlotOffset(op->index()) + kPointerSize);
}


void LCodeGen::WriteTranslation(LEnvironment* environment,
                                Translation* translation) {
  if (environment == NULL) return;

  // The translation includes one command per value in the environment.
  int translation_size = environment->translation_size();
  // The output frame height does not include the parameters.
  int height = translation_size - environment->parameter_count();

  WriteTranslation(environment->outer(), translation);
  bool has_closure_id = !info()->closure().is_null() &&
      !info()->closure().is_identical_to(environment->closure());
  int closure_id = has_closure_id
      ? DefineDeoptimizationLiteral(environment->closure())
      : Translation::kSelfLiteralId;

  switch (environment->frame_type()) {
    case JS_FUNCTION:
      translation->BeginJSFrame(environment->ast_id(), closure_id, height);
      break;
    case JS_CONSTRUCT:
      translation->BeginConstructStubFrame(closure_id, translation_size);
      break;
    case JS_GETTER:
      ASSERT(translation_size == 1);
      ASSERT(height == 0);
      translation->BeginGetterStubFrame(closure_id);
      break;
    case JS_SETTER:
      ASSERT(translation_size == 2);
      ASSERT(height == 0);
      translation->BeginSetterStubFrame(closure_id);
      break;
    case STUB:
      translation->BeginCompiledStubFrame();
      break;
    case ARGUMENTS_ADAPTOR:
      translation->BeginArgumentsAdaptorFrame(closure_id, translation_size);
      break;
  }

  int object_index = 0;
  int dematerialized_index = 0;
  for (int i = 0; i < translation_size; ++i) {
    LOperand* value = environment->values()->at(i);
    AddToTranslation(environment,
                     translation,
                     value,
                     environment->HasTaggedValueAt(i),
                     environment->HasUint32ValueAt(i),
                     &object_index,
                     &dematerialized_index);
  }
}


void LCodeGen::AddToTranslation(LEnvironment* environment,
                                Translation* translation,
                                LOperand* op,
                                bool is_tagged,
                                bool is_uint32,
                                int* object_index_pointer,
                                int* dematerialized_index_pointer) {
  if (op == LEnvironment::materialization_marker()) {
    int object_index = (*object_index_pointer)++;
    if (environment->ObjectIsDuplicateAt(object_index)) {
      int dupe_of = environment->ObjectDuplicateOfAt(object_index);
      translation->DuplicateObject(dupe_of);
      return;
    }
    int object_length = environment->ObjectLengthAt(object_index);
    if (environment->ObjectIsArgumentsAt(object_index)) {
      translation->BeginArgumentsObject(object_length);
    } else {
      translation->BeginCapturedObject(object_length);
    }
    int dematerialized_index = *dematerialized_index_pointer;
    int env_offset = environment->translation_size() + dematerialized_index;
    *dematerialized_index_pointer += object_length;
    for (int i = 0; i < object_length; ++i) {
      LOperand* value = environment->values()->at(env_offset + i);
      AddToTranslation(environment,
                       translation,
                       value,
                       environment->HasTaggedValueAt(env_offset + i),
                       environment->HasUint32ValueAt(env_offset + i),
                       object_index_pointer,
                       dematerialized_index_pointer);
    }
    return;
  }

  if (op->IsStackSlot()) {
    if (is_tagged) {
      translation->StoreStackSlot(op->index());
    } else if (is_uint32) {
      translation->StoreUint32StackSlot(op->index());
    } else {
      translation->StoreInt32StackSlot(op->index());
    }
  } else if (op->IsDoubleStackSlot()) {
    translation->StoreDoubleStackSlot(op->index());
  } else if (op->IsArgument()) {
    ASSERT(is_tagged);
    int src_index = GetStackSlotCount() + op->index();
    translation->StoreStackSlot(src_index);
  } else if (op->IsRegister()) {
    Register reg = ToRegister(op);
    if (is_tagged) {
      translation->StoreRegister(reg);
    } else if (is_uint32) {
      translation->StoreUint32Register(reg);
    } else {
      translation->StoreInt32Register(reg);
    }
  } else if (op->IsDoubleRegister()) {
    DoubleRegister reg = ToDoubleRegister(op);
    translation->StoreDoubleRegister(reg);
  } else if (op->IsConstantOperand()) {
    HConstant* constant = chunk()->LookupConstant(LConstantOperand::cast(op));
    int src_index = DefineDeoptimizationLiteral(constant->handle(isolate()));
    translation->StoreLiteral(src_index);
  } else {
    UNREACHABLE();
  }
}


void LCodeGen::CallCode(Handle<Code> code,
                        RelocInfo::Mode mode,
                        LInstruction* instr) {
  CallCodeGeneric(code, mode, instr, RECORD_SIMPLE_SAFEPOINT);
}


void LCodeGen::CallCodeGeneric(Handle<Code> code,
                               RelocInfo::Mode mode,
                               LInstruction* instr,
                               SafepointMode safepoint_mode) {
  EnsureSpaceForLazyDeopt(Deoptimizer::patch_size());
  ASSERT(instr != NULL);
  __ Call(code, mode);
  RecordSafepointWithLazyDeopt(instr, safepoint_mode);

  // Signal that we don't inline smi code before these stubs in the
  // optimizing code generator.
  if (code->kind() == Code::BINARY_OP_IC ||
      code->kind() == Code::COMPARE_IC) {
    __ nop();
  }
}


void LCodeGen::CallRuntime(const Runtime::Function* function,
                           int num_arguments,
                           LInstruction* instr,
                           SaveFPRegsMode save_doubles) {
  ASSERT(instr != NULL);

  __ CallRuntime(function, num_arguments, save_doubles);

  RecordSafepointWithLazyDeopt(instr, RECORD_SIMPLE_SAFEPOINT);
}


void LCodeGen::LoadContextFromDeferred(LOperand* context) {
  if (context->IsRegister()) {
    __ Move(cp, ToRegister(context));
  } else if (context->IsStackSlot()) {
    __ LoadP(cp, ToMemOperand(context));
  } else if (context->IsConstantOperand()) {
    HConstant* constant =
        chunk_->LookupConstant(LConstantOperand::cast(context));
    __ Move(cp, Handle<Object>::cast(constant->handle(isolate())));
  } else {
    UNREACHABLE();
  }
}


void LCodeGen::CallRuntimeFromDeferred(Runtime::FunctionId id,
                                       int argc,
                                       LInstruction* instr,
                                       LOperand* context) {
  LoadContextFromDeferred(context);
  __ CallRuntimeSaveDoubles(id);
  RecordSafepointWithRegisters(
      instr->pointer_map(), argc, Safepoint::kNoLazyDeopt);
}


void LCodeGen::RegisterEnvironmentForDeoptimization(LEnvironment* environment,
                                                    Safepoint::DeoptMode mode) {
  if (!environment->HasBeenRegistered()) {
    // Physical stack frame layout:
    // -x ............. -4  0 ..................................... y
    // [incoming arguments] [spill slots] [pushed outgoing arguments]

    // Layout of the environment:
    // 0 ..................................................... size-1
    // [parameters] [locals] [expression stack including arguments]

    // Layout of the translation:
    // 0 ........................................................ size - 1 + 4
    // [expression stack including arguments] [locals] [4 words] [parameters]
    // |>------------  translation_size ------------<|

    int frame_count = 0;
    int jsframe_count = 0;
    for (LEnvironment* e = environment; e != NULL; e = e->outer()) {
      ++frame_count;
      if (e->frame_type() == JS_FUNCTION) {
        ++jsframe_count;
      }
    }
    Translation translation(&translations_, frame_count, jsframe_count, zone());
    WriteTranslation(environment, &translation);
    int deoptimization_index = deoptimizations_.length();
    int pc_offset = masm()->pc_offset();
    environment->Register(deoptimization_index,
                          translation.index(),
                          (mode == Safepoint::kLazyDeopt) ? pc_offset : -1);
    deoptimizations_.Add(environment, zone());
  }
}


void LCodeGen::DeoptimizeIf(Condition cond,
                            LEnvironment* environment,
                            Deoptimizer::BailoutType bailout_type,
                            CRegister cr) {
  RegisterEnvironmentForDeoptimization(environment, Safepoint::kNoLazyDeopt);
  ASSERT(environment->HasBeenRegistered());
  int id = environment->deoptimization_index();
  ASSERT(info()->IsOptimizing() || info()->IsStub());
  Address entry =
      Deoptimizer::GetDeoptimizationEntry(isolate(), id, bailout_type);
  if (entry == NULL) {
    Abort(kBailoutWasNotPrepared);
    return;
  }

  ASSERT(FLAG_deopt_every_n_times < 2);  // Other values not supported on PPC.

  if (FLAG_deopt_every_n_times == 1 &&
      !info()->IsStub() &&
      info()->opt_count() == id) {
    ASSERT(frame_is_built_);
    __ Call(entry, RelocInfo::RUNTIME_ENTRY);
    return;
  }

  if (info()->ShouldTrapOnDeopt()) {
    __ stop("trap_on_deopt", cond, kDefaultStopCode, cr);
  }

  ASSERT(info()->IsStub() || frame_is_built_);
  if (cond == al && frame_is_built_) {
    __ Call(entry, RelocInfo::RUNTIME_ENTRY);
  } else {
    // We often have several deopts to the same entry, reuse the last
    // jump entry if this is the case.
    if (deopt_jump_table_.is_empty() ||
        (deopt_jump_table_.last().address != entry) ||
        (deopt_jump_table_.last().bailout_type != bailout_type) ||
        (deopt_jump_table_.last().needs_frame != !frame_is_built_)) {
      Deoptimizer::JumpTableEntry table_entry(entry,
                                              bailout_type,
                                              !frame_is_built_);
      deopt_jump_table_.Add(table_entry, zone());
    }
    __ b(cond, &deopt_jump_table_.last().label, cr);
  }
}


void LCodeGen::DeoptimizeIf(Condition cond,
                            LEnvironment* environment,
                            CRegister cr) {
  Deoptimizer::BailoutType bailout_type = info()->IsStub()
      ? Deoptimizer::LAZY
      : Deoptimizer::EAGER;
  DeoptimizeIf(cond, environment, bailout_type, cr);
}


void LCodeGen::RegisterDependentCodeForEmbeddedMaps(Handle<Code> code) {
  ZoneList<Handle<Map> > maps(1, zone());
  ZoneList<Handle<JSObject> > objects(1, zone());
  int mode_mask = RelocInfo::ModeMask(RelocInfo::EMBEDDED_OBJECT);
  for (RelocIterator it(*code, mode_mask); !it.done(); it.next()) {
    if (Code::IsWeakEmbeddedObject(code->kind(), it.rinfo()->target_object())) {
      if (it.rinfo()->target_object()->IsMap()) {
        Handle<Map> map(Map::cast(it.rinfo()->target_object()));
        maps.Add(map, zone());
      } else if (it.rinfo()->target_object()->IsJSObject()) {
        Handle<JSObject> object(JSObject::cast(it.rinfo()->target_object()));
        objects.Add(object, zone());
      }
    }
  }
#ifdef VERIFY_HEAP
  // This disables verification of weak embedded objects after full GC.
  // AddDependentCode can cause a GC, which would observe the state where
  // this code is not yet in the depended code lists of the embedded maps.
  NoWeakObjectVerificationScope disable_verification_of_embedded_objects;
#endif
  for (int i = 0; i < maps.length(); i++) {
    maps.at(i)->AddDependentCode(DependentCode::kWeaklyEmbeddedGroup, code);
  }
  for (int i = 0; i < objects.length(); i++) {
    AddWeakObjectToCodeDependency(isolate()->heap(), objects.at(i), code);
  }
}


void LCodeGen::PopulateDeoptimizationData(Handle<Code> code) {
  int length = deoptimizations_.length();
  if (length == 0) return;
  Handle<DeoptimizationInputData> data =
      factory()->NewDeoptimizationInputData(length, TENURED);

  Handle<ByteArray> translations =
      translations_.CreateByteArray(isolate()->factory());
  data->SetTranslationByteArray(*translations);
  data->SetInlinedFunctionCount(Smi::FromInt(inlined_function_count_));

  Handle<FixedArray> literals =
      factory()->NewFixedArray(deoptimization_literals_.length(), TENURED);
  { AllowDeferredHandleDereference copy_handles;
    for (int i = 0; i < deoptimization_literals_.length(); i++) {
      literals->set(i, *deoptimization_literals_[i]);
    }
    data->SetLiteralArray(*literals);
  }

  data->SetOsrAstId(Smi::FromInt(info_->osr_ast_id().ToInt()));
  data->SetOsrPcOffset(Smi::FromInt(osr_pc_offset_));

  // Populate the deoptimization entries.
  for (int i = 0; i < length; i++) {
    LEnvironment* env = deoptimizations_[i];
    data->SetAstId(i, env->ast_id());
    data->SetTranslationIndex(i, Smi::FromInt(env->translation_index()));
    data->SetArgumentsStackHeight(i,
                                  Smi::FromInt(env->arguments_stack_height()));
    data->SetPc(i, Smi::FromInt(env->pc_offset()));
  }
  code->set_deoptimization_data(*data);
}


int LCodeGen::DefineDeoptimizationLiteral(Handle<Object> literal) {
  int result = deoptimization_literals_.length();
  for (int i = 0; i < deoptimization_literals_.length(); ++i) {
    if (deoptimization_literals_[i].is_identical_to(literal)) return i;
  }
  deoptimization_literals_.Add(literal, zone());
  return result;
}


void LCodeGen::PopulateDeoptimizationLiteralsWithInlinedFunctions() {
  ASSERT(deoptimization_literals_.length() == 0);

  const ZoneList<Handle<JSFunction> >* inlined_closures =
      chunk()->inlined_closures();

  for (int i = 0, length = inlined_closures->length();
       i < length;
       i++) {
    DefineDeoptimizationLiteral(inlined_closures->at(i));
  }

  inlined_function_count_ = deoptimization_literals_.length();
}


void LCodeGen::RecordSafepointWithLazyDeopt(
    LInstruction* instr, SafepointMode safepoint_mode) {
  if (safepoint_mode == RECORD_SIMPLE_SAFEPOINT) {
    RecordSafepoint(instr->pointer_map(), Safepoint::kLazyDeopt);
  } else {
    ASSERT(safepoint_mode == RECORD_SAFEPOINT_WITH_REGISTERS_AND_NO_ARGUMENTS);
    RecordSafepointWithRegisters(
        instr->pointer_map(), 0, Safepoint::kLazyDeopt);
  }
}


void LCodeGen::RecordSafepoint(
    LPointerMap* pointers,
    Safepoint::Kind kind,
    int arguments,
    Safepoint::DeoptMode deopt_mode) {
  ASSERT(expected_safepoint_kind_ == kind);

  const ZoneList<LOperand*>* operands = pointers->GetNormalizedOperands();
  Safepoint safepoint = safepoints_.DefineSafepoint(masm(),
      kind, arguments, deopt_mode);
  for (int i = 0; i < operands->length(); i++) {
    LOperand* pointer = operands->at(i);
    if (pointer->IsStackSlot()) {
      safepoint.DefinePointerSlot(pointer->index(), zone());
    } else if (pointer->IsRegister() && (kind & Safepoint::kWithRegisters)) {
      safepoint.DefinePointerRegister(ToRegister(pointer), zone());
    }
  }
}


void LCodeGen::RecordSafepoint(LPointerMap* pointers,
                               Safepoint::DeoptMode deopt_mode) {
  RecordSafepoint(pointers, Safepoint::kSimple, 0, deopt_mode);
}


void LCodeGen::RecordSafepoint(Safepoint::DeoptMode deopt_mode) {
  LPointerMap empty_pointers(zone());
  RecordSafepoint(&empty_pointers, deopt_mode);
}


void LCodeGen::RecordSafepointWithRegisters(LPointerMap* pointers,
                                            int arguments,
                                            Safepoint::DeoptMode deopt_mode) {
  RecordSafepoint(
      pointers, Safepoint::kWithRegisters, arguments, deopt_mode);
}


void LCodeGen::RecordSafepointWithRegistersAndDoubles(
    LPointerMap* pointers,
    int arguments,
    Safepoint::DeoptMode deopt_mode) {
  RecordSafepoint(
      pointers, Safepoint::kWithRegistersAndDoubles, arguments, deopt_mode);
}


void LCodeGen::RecordAndWritePosition(int position) {
  if (position == RelocInfo::kNoPosition) return;
  masm()->positions_recorder()->RecordPosition(position);
  masm()->positions_recorder()->WriteRecordedPositions();
}


static const char* LabelType(LLabel* label) {
  if (label->is_loop_header()) return " (loop header)";
  if (label->is_osr_entry()) return " (OSR entry)";
  return "";
}


void LCodeGen::DoLabel(LLabel* label) {
  Comment(";;; <@%d,#%d> -------------------- B%d%s --------------------",
          current_instruction_,
          label->hydrogen_value()->id(),
          label->block_id(),
          LabelType(label));
  __ bind(label->label());
  current_block_ = label->block_id();
  DoGap(label);
}


void LCodeGen::DoParallelMove(LParallelMove* move) {
  resolver_.Resolve(move);
}


void LCodeGen::DoGap(LGap* gap) {
  for (int i = LGap::FIRST_INNER_POSITION;
       i <= LGap::LAST_INNER_POSITION;
       i++) {
    LGap::InnerPosition inner_pos = static_cast<LGap::InnerPosition>(i);
    LParallelMove* move = gap->GetParallelMove(inner_pos);
    if (move != NULL) DoParallelMove(move);
  }
}


void LCodeGen::DoInstructionGap(LInstructionGap* instr) {
  DoGap(instr);
}


void LCodeGen::DoParameter(LParameter* instr) {
  // Nothing to do.
}


void LCodeGen::DoCallStub(LCallStub* instr) {
  ASSERT(ToRegister(instr->context()).is(cp));
  ASSERT(ToRegister(instr->result()).is(r3));
  switch (instr->hydrogen()->major_key()) {
    case CodeStub::RegExpConstructResult: {
      RegExpConstructResultStub stub;
      CallCode(stub.GetCode(isolate()), RelocInfo::CODE_TARGET, instr);
      break;
    }
    case CodeStub::RegExpExec: {
      RegExpExecStub stub;
      CallCode(stub.GetCode(isolate()), RelocInfo::CODE_TARGET, instr);
      break;
    }
    case CodeStub::SubString: {
      SubStringStub stub;
      CallCode(stub.GetCode(isolate()), RelocInfo::CODE_TARGET, instr);
      break;
    }
    case CodeStub::StringCompare: {
      StringCompareStub stub;
      CallCode(stub.GetCode(isolate()), RelocInfo::CODE_TARGET, instr);
      break;
    }
    case CodeStub::TranscendentalCache: {
      __ LoadP(r3, MemOperand(sp, 0));
      TranscendentalCacheStub stub(instr->transcendental_type(),
                                   TranscendentalCacheStub::TAGGED);
      CallCode(stub.GetCode(isolate()), RelocInfo::CODE_TARGET, instr);
      break;
    }
    default:
      UNREACHABLE();
  }
}


void LCodeGen::DoUnknownOSRValue(LUnknownOSRValue* instr) {
  GenerateOsrPrologue();
}


void LCodeGen::DoModI(LModI* instr) {
  HMod* hmod = instr->hydrogen();
  HValue* left = hmod->left();
  HValue* right = hmod->right();
  Register left_reg = ToRegister(instr->left());
  Register result_reg = ToRegister(instr->result());
  Label done;

  if (hmod->HasPowerOf2Divisor() || hmod->fixed_right_arg().has_value) {
    int32_t divisor = 0;

    if (hmod->HasPowerOf2Divisor()) {
      // Note: The code below even works when right contains kMinInt.
      divisor = Abs(right->GetInteger32Constant());
    } else {
      Register right_reg = ToRegister(instr->right());
      divisor = hmod->fixed_right_arg().value;

      // Check if our assumption of a fixed right operand still holds.
      __ Cmpi(right_reg, Operand(divisor), r0);
      DeoptimizeIf(ne, instr->environment());
    }

    ASSERT(IsPowerOf2(divisor));
    Label left_is_not_negative;
    if (left->CanBeNegative()) {
      __ cmpi(left_reg, Operand::Zero());
      __ bge(&left_is_not_negative);
      __ neg(result_reg, left_reg);
      __ mov(r0, Operand(divisor - 1));
      __ and_(result_reg, result_reg, r0);
      __ neg(result_reg, result_reg, LeaveOE, SetRC);
      if (hmod->CheckFlag(HValue::kBailoutOnMinusZero)) {
        DeoptimizeIf(eq, instr->environment(), cr0);
      }
      __ b(&done);
    }

    __ bind(&left_is_not_negative);
    __ And(result_reg, left_reg, Operand(divisor - 1));

  } else {
    Register right_reg = ToRegister(instr->right());
    Register scratch = scratch0();

    __ divw(scratch, left_reg, right_reg);

    // Check for x % 0.
    if (right->CanBeZero()) {
      __ cmpi(right_reg, Operand::Zero());
      DeoptimizeIf(eq, instr->environment());
    }

    // Check for kMinInt % -1, divw will return undefined, which is not what we
    // want. We have to deopt if we care about -0, because we can't return that.
    if (left->RangeCanInclude(kMinInt) && right->RangeCanInclude(-1)) {
      Label no_overflow_possible;
      __ Cmpi(left_reg, Operand(kMinInt), r0);
      __ bne(&no_overflow_possible);
      __ cmpi(right_reg, Operand(-1));
      if (hmod->CheckFlag(HValue::kBailoutOnMinusZero)) {
        DeoptimizeIf(eq, instr->environment());
      } else {
        __ bne(&no_overflow_possible);
        __ li(result_reg, Operand::Zero());
        __ b(&done);
      }
      __ bind(&no_overflow_possible);
    }

#if V8_TARGET_ARCH_PPC64
    __ extsw(scratch, scratch);
#endif
    __ Mul(scratch, right_reg, scratch);
    __ sub(result_reg, left_reg, scratch, LeaveOE, SetRC);

    // If we care about -0, test if the dividend is <0 and the result is 0.
    if (left->CanBeNegative() &&
        hmod->CanBeZero() &&
        hmod->CheckFlag(HValue::kBailoutOnMinusZero)) {
      __ bne(&done, cr0);
      __ cmpi(left_reg, Operand::Zero());
      DeoptimizeIf(lt, instr->environment());
    }
  }

  __ bind(&done);
}


void LCodeGen::DoDivI(LDivI* instr) {
  if (!instr->is_flooring() && instr->hydrogen()->HasPowerOf2Divisor()) {
    Register dividend = ToRegister(instr->left());
    int32_t divisor = instr->hydrogen()->right()->GetInteger32Constant();
    int32_t test_value = 0;
    int32_t power = 0;

    if (divisor > 0) {
      test_value = divisor - 1;
      power = WhichPowerOf2(divisor);
    } else {
      // Check for (0 / -x) that will produce negative zero.
      if (instr->hydrogen()->CheckFlag(HValue::kBailoutOnMinusZero)) {
        __ cmpi(dividend, Operand::Zero());
        DeoptimizeIf(eq, instr->environment());
      }
      // Check for (kMinInt / -1).
      if (divisor == -1 && instr->hydrogen()->CheckFlag(HValue::kCanOverflow)) {
        __ Cmpi(dividend, Operand(kMinInt), r0);
        DeoptimizeIf(eq, instr->environment());
      }
      test_value = - divisor - 1;
      power = WhichPowerOf2(-divisor);
    }

    if (test_value != 0) {
      if (instr->hydrogen()->CheckFlag(
          HInstruction::kAllUsesTruncatingToInt32)) {
        Label negative_dividend, done;
        __ cmpi(dividend, Operand::Zero());
        __ blt(&negative_dividend);
        __ ShiftRightArithImm(dividend, dividend, power);
        if (divisor < 0) __ neg(dividend, dividend);
        __ b(&done);

        __ bind(&negative_dividend);
        __ neg(dividend, dividend);
        __ ShiftRightArithImm(dividend, dividend, power);
        if (divisor > 0) __ neg(dividend, dividend);

        __ bind(&done);
        return;  // Don't fall through to "__ neg" below.
      } else {
        // Deoptimize if remainder is not 0.
        __ TestBitRange(dividend, power - 1, 0, r0);
        DeoptimizeIf(ne, instr->environment(), cr0);
        __ ShiftRightArithImm(dividend, dividend, power);
      }
    }
    if (divisor < 0) __ neg(dividend, dividend);

    return;
  }

  const Register left = ToRegister(instr->left());
  const Register right = ToRegister(instr->right());
  const Register scratch = scratch0();
  const Register result = ToRegister(instr->result());

  ASSERT(!left.is(result));
  ASSERT(!right.is(result));

  __ divw(result, left, right);

  // Check for x / 0.
  if (instr->hydrogen_value()->CheckFlag(HValue::kCanBeDivByZero)) {
    __ cmpi(right, Operand::Zero());
    DeoptimizeIf(eq, instr->environment());
  }

  // Check for (0 / -x) that will produce negative zero.
  if (instr->hydrogen_value()->CheckFlag(HValue::kBailoutOnMinusZero)) {
    Label left_not_zero;
    __ cmpi(left, Operand::Zero());
    __ bne(&left_not_zero);
    __ cmpi(right, Operand::Zero());
    DeoptimizeIf(lt, instr->environment());
    __ bind(&left_not_zero);
  }

  // Check for (kMinInt / -1).
  if (instr->hydrogen_value()->CheckFlag(HValue::kCanOverflow)) {
    Label left_not_min_int;
    __ Cmpi(left, Operand(kMinInt), r0);
    __ bne(&left_not_min_int);
    __ cmpi(right, Operand(-1));
    DeoptimizeIf(eq, instr->environment());
    __ bind(&left_not_min_int);
  }

#if V8_TARGET_ARCH_PPC64
  __ extsw(result, result);
#endif

  if (instr->is_flooring()) {
    Label done;
    // If both operands have the same sign then we are done.
    __ xor_(scratch, left, right, SetRC);
    __ bge(&done, cr0);

    __ Mul(scratch, right, result);
    __ cmp(left, scratch);
    __ beq(&done);

    // We performed a truncating division. Correct the result.
    __ subi(result, result, Operand(1));
    __ bind(&done);
  } else if (!instr->hydrogen()->CheckFlag(
               HInstruction::kAllUsesTruncatingToInt32)) {
    // Deoptimize if remainder is not 0.
    __ Mul(scratch, right, result);
    __ cmp(left, scratch);
    DeoptimizeIf(ne, instr->environment());
  }
}


void LCodeGen::DoMathFloorOfDiv(LMathFloorOfDiv* instr) {
  ASSERT(instr->right()->IsConstantOperand());

  const Register dividend = ToRegister(instr->left());
  int32_t divisor = ToInteger32(LConstantOperand::cast(instr->right()));
  const Register result = ToRegister(instr->result());

  switch (divisor) {
    case 0:
      DeoptimizeIf(al, instr->environment());
      return;

    case 1:
      __ Move(result, dividend);
      return;

    case -1: {
      OEBit oe = LeaveOE;

      if (instr->hydrogen()->CheckFlag(HValue::kCanOverflow)) {
#if V8_TARGET_ARCH_PPC64
        __ Cmpi(dividend, Operand(kMinInt), r0);
        DeoptimizeIf(eq, instr->environment());
#else
        __ li(r0, Operand::Zero());  // clear xer
        __ mtxer(r0);
        oe = SetOE;
#endif
      }
      __ neg(result, dividend, oe, SetRC);
      if (instr->hydrogen()->CheckFlag(HValue::kBailoutOnMinusZero)) {
        DeoptimizeIf(eq, instr->environment(), cr0);
      }
#if !V8_TARGET_ARCH_PPC64
      if (instr->hydrogen()->CheckFlag(HValue::kCanOverflow)) {
        DeoptimizeIf(overflow, instr->environment(), cr0);
      }
#endif
      return;
    }
  }

  uint32_t divisor_abs = abs(divisor);
  if (IsPowerOf2(divisor_abs)) {
    int32_t power = WhichPowerOf2(divisor_abs);
    if (divisor < 0) {
      __ neg(result, dividend, LeaveOE, SetRC);
      if (instr->hydrogen()->CheckFlag(HValue::kBailoutOnMinusZero)) {
        DeoptimizeIf(eq, instr->environment(), cr0);
      }
      __ ShiftRightArithImm(result, result, power);
    } else {
      __ ShiftRightArithImm(result, dividend, power);
    }
  } else {
    Register scratch = ToRegister(instr->temp());

    // Find b which: 2^b < divisor_abs < 2^(b+1).
    unsigned b = 31 - CompilerIntrinsics::CountLeadingZeros(divisor_abs);
    unsigned shift = 32 + b;  // Precision +1bit (effectively).
    double multiplier_f =
        static_cast<double>(static_cast<uint64_t>(1) << shift) / divisor_abs;
    int64_t multiplier;
    if (multiplier_f - floor(multiplier_f) < 0.5) {
        multiplier = static_cast<int64_t>(floor(multiplier_f));
    } else {
        multiplier = static_cast<int64_t>(floor(multiplier_f)) + 1;
    }
    // The multiplier is a uint32.
    ASSERT(multiplier > 0 &&
           multiplier < (static_cast<int64_t>(1) << 32));
#if V8_TARGET_ARCH_PPC64
    __ extsw(scratch, dividend);
    if (divisor < 0 &&
        instr->hydrogen()->CheckFlag(HValue::kBailoutOnMinusZero)) {
      __ neg(scratch, scratch, LeaveOE, SetRC);
      DeoptimizeIf(eq, instr->environment(), cr0);
    }
    __ mov(result, Operand(multiplier));
    __ Mul(result, result, scratch);
    __ addis(result, result, Operand(0x4000));
    __ ShiftRightArithImm(result, result, shift);
#else
    if (divisor < 0 &&
        instr->hydrogen()->CheckFlag(HValue::kBailoutOnMinusZero)) {
      __ cmpi(dividend, Operand::Zero());
      DeoptimizeIf(eq, instr->environment());
    }
    __ mov(result, Operand(multiplier));
    __ mullw(ip, result, dividend);
    __ mulhw(result, result, dividend);

    if (static_cast<int32_t>(multiplier) < 0) {
      __ add(result, result, dividend);
    }
    if (divisor < 0) {
      __ neg(result, result);

      // Subtract one from result if -(low word) < 0xC0000000
      __ neg(ip, ip);
      __ srwi(scratch, ip, Operand(30));
      __ addi(scratch, scratch, Operand(1));
      __ srwi(scratch, scratch, Operand(2));
      __ addi(scratch, scratch, Operand(-1));
      __ add(result, result, scratch);
    } else {
      // Add one to result if low word >= 0xC0000000
      __ srwi(scratch, ip, Operand(30));
      __ addi(scratch, scratch, Operand(1));
      __ srwi(scratch, scratch, Operand(2));
      __ add(result, result, scratch);
    }
    __ ShiftRightArithImm(result, result, shift - 32);
#endif
  }
}


void LCodeGen::DoMultiplyAddD(LMultiplyAddD* instr) {
  DoubleRegister addend = ToDoubleRegister(instr->addend());
  DoubleRegister multiplier = ToDoubleRegister(instr->multiplier());
  DoubleRegister multiplicand = ToDoubleRegister(instr->multiplicand());
  DoubleRegister result = ToDoubleRegister(instr->result());

  __ fmadd(result, multiplier, multiplicand, addend);
}


void LCodeGen::DoMultiplySubD(LMultiplySubD* instr) {
  DoubleRegister minuend = ToDoubleRegister(instr->minuend());
  DoubleRegister multiplier = ToDoubleRegister(instr->multiplier());
  DoubleRegister multiplicand = ToDoubleRegister(instr->multiplicand());
  DoubleRegister result = ToDoubleRegister(instr->result());

  __ fmsub(result, multiplier, multiplicand, minuend);
}


void LCodeGen::DoMulI(LMulI* instr) {
  Register scratch = scratch0();
  Register result = ToRegister(instr->result());
  // Note that result may alias left.
  Register left = ToRegister(instr->left());
  LOperand* right_op = instr->right();

  bool bailout_on_minus_zero =
    instr->hydrogen()->CheckFlag(HValue::kBailoutOnMinusZero);
  bool can_overflow = instr->hydrogen()->CheckFlag(HValue::kCanOverflow);

  if (right_op->IsConstantOperand()) {
    int32_t constant = ToInteger32(LConstantOperand::cast(right_op));

    if (bailout_on_minus_zero && (constant < 0)) {
      // The case of a null constant will be handled separately.
      // If constant is negative and left is null, the result should be -0.
      __ cmpi(left, Operand::Zero());
      DeoptimizeIf(eq, instr->environment());
    }

    switch (constant) {
      case -1:
        if (can_overflow) {
#if V8_TARGET_ARCH_PPC64
          if (instr->hydrogen()->representation().IsSmi()) {
            __ li(r0, Operand::Zero());  // clear xer
            __ mtxer(r0);
            __ neg(result, left, SetOE, SetRC);
            DeoptimizeIf(overflow, instr->environment(), cr0);
          } else {
            __ neg(result, left);
            __ TestIfInt32(result, scratch, r0);
            DeoptimizeIf(ne, instr->environment());
          }
#else
          __ li(r0, Operand::Zero());  // clear xer
          __ mtxer(r0);
          __ neg(result, left, SetOE, SetRC);
          DeoptimizeIf(overflow, instr->environment(), cr0);
#endif
        } else {
          __ neg(result, left);
        }
        break;
      case 0:
        if (bailout_on_minus_zero) {
          // If left is strictly negative and the constant is null, the
          // result is -0. Deoptimize if required, otherwise return 0.
          __ cmpi(left, Operand::Zero());
          DeoptimizeIf(lt, instr->environment());
        }
        __ li(result, Operand::Zero());
        break;
      case 1:
        __ Move(result, left);
        break;
      default:
        // Multiplying by powers of two and powers of two plus or minus
        // one can be done faster with shifted operands.
        // For other constants we emit standard code.
        int32_t mask = constant >> 31;
        uint32_t constant_abs = (constant + mask) ^ mask;

        if (IsPowerOf2(constant_abs)) {
          int32_t shift = WhichPowerOf2(constant_abs);
          __ ShiftLeftImm(result, left, Operand(shift));
          // Correct the sign of the result if the constant is negative.
          if (constant < 0)  __ neg(result, result);
        } else if (IsPowerOf2(constant_abs - 1)) {
          int32_t shift = WhichPowerOf2(constant_abs - 1);
          __ ShiftLeftImm(scratch, left, Operand(shift));
          __ add(result, scratch, left);
          // Correct the sign of the result if the constant is negative.
          if (constant < 0)  __ neg(result, result);
        } else if (IsPowerOf2(constant_abs + 1)) {
          int32_t shift = WhichPowerOf2(constant_abs + 1);
          __ ShiftLeftImm(scratch, left, Operand(shift));
          __ sub(result, scratch, left);
          // Correct the sign of the result if the constant is negative.
          if (constant < 0)  __ neg(result, result);
        } else {
          // Generate standard code.
          __ mov(ip, Operand(constant));
          __ Mul(result, left, ip);
        }
    }

  } else {
    ASSERT(right_op->IsRegister());
    Register right = ToRegister(right_op);

    if (can_overflow) {
#if V8_TARGET_ARCH_PPC64
      // result = left * right.
      if (instr->hydrogen()->representation().IsSmi()) {
        __ SmiUntag(result, left);
        __ SmiUntag(scratch, right);
        __ Mul(result, result, scratch);
      } else {
        __ Mul(result, left, right);
      }
      __ TestIfInt32(result, scratch, r0);
      DeoptimizeIf(ne, instr->environment());
      if (instr->hydrogen()->representation().IsSmi()) {
        __ SmiTag(result);
      }
#else
      // scratch:result = left * right.
      if (instr->hydrogen()->representation().IsSmi()) {
        __ SmiUntag(result, left);
        __ mulhw(scratch, result, right);
        __ mullw(result, result, right);
      } else {
        __ mulhw(scratch, left, right);
        __ mullw(result, left, right);
      }
      __ TestIfInt32(scratch, result, r0);
      DeoptimizeIf(ne, instr->environment());
#endif
    } else {
      if (instr->hydrogen()->representation().IsSmi()) {
        __ SmiUntag(result, left);
        __ Mul(result, result, right);
      } else {
        __ Mul(result, left, right);
      }
    }

    if (bailout_on_minus_zero) {
      Label done;
      __ xor_(r0, left, right, SetRC);
      __ bge(&done, cr0);
      // Bail out if the result is minus zero.
      __ cmpi(result, Operand::Zero());
      DeoptimizeIf(eq, instr->environment());
      __ bind(&done);
    }
  }
}


void LCodeGen::DoBitI(LBitI* instr) {
  LOperand* left_op = instr->left();
  LOperand* right_op = instr->right();
  ASSERT(left_op->IsRegister());
  Register left = ToRegister(left_op);
  Register result = ToRegister(instr->result());
  Operand right(no_reg);

  if (right_op->IsStackSlot() || right_op->IsArgument()) {
    right = Operand(EmitLoadRegister(right_op, ip));
  } else {
    ASSERT(right_op->IsRegister() || right_op->IsConstantOperand());
    right = ToOperand(right_op);

    if (right_op->IsConstantOperand() && is_uint16(right.immediate())) {
      switch (instr->op()) {
        case Token::BIT_AND:
          __ andi(result, left, right);
          break;
        case Token::BIT_OR:
          __ ori(result, left, right);
          break;
        case Token::BIT_XOR:
          __ xori(result, left, right);
          break;
        default:
          UNREACHABLE();
          break;
      }
      return;
    }
  }

  switch (instr->op()) {
    case Token::BIT_AND:
      __ And(result, left, right);
      break;
    case Token::BIT_OR:
      __ Or(result, left, right);
      break;
    case Token::BIT_XOR:
      if (right_op->IsConstantOperand() && right.immediate() == int32_t(~0)) {
        __ notx(result, left);
      } else {
        __ Xor(result, left, right);
      }
      break;
    default:
      UNREACHABLE();
      break;
  }
}


void LCodeGen::DoShiftI(LShiftI* instr) {
  // Both 'left' and 'right' are "used at start" (see LCodeGen::DoShift), so
  // result may alias either of them.
  LOperand* right_op = instr->right();
  Register left = ToRegister(instr->left());
  Register result = ToRegister(instr->result());
  Register scratch = scratch0();
  if (right_op->IsRegister()) {
    // Mask the right_op operand.
    __ andi(scratch, ToRegister(right_op), Operand(0x1F));
    switch (instr->op()) {
      case Token::ROR:
        // rotate_right(a, b) == rotate_left(a, 32 - b)
        __ subfic(scratch, scratch, Operand(32));
        __ rotlw(result, left, scratch);
        break;
      case Token::SAR:
        __ sraw(result, left, scratch);
        break;
      case Token::SHR:
        if (instr->can_deopt()) {
          __ srw(result, left, scratch, SetRC);
#if V8_TARGET_ARCH_PPC64
          __ extsw(result, result, SetRC);
#endif
          DeoptimizeIf(lt, instr->environment(), cr0);
        } else {
          __ srw(result, left, scratch);
        }
        break;
      case Token::SHL:
        __ slw(result, left, scratch);
#if V8_TARGET_ARCH_PPC64
        __ extsw(result, result);
#endif
        break;
      default:
        UNREACHABLE();
        break;
    }
  } else {
    // Mask the right_op operand.
    int value = ToInteger32(LConstantOperand::cast(right_op));
    uint8_t shift_count = static_cast<uint8_t>(value & 0x1F);
    switch (instr->op()) {
      case Token::ROR:
        if (shift_count != 0) {
          __ rotrwi(result, left, shift_count);
        } else {
          __ Move(result, left);
        }
        break;
      case Token::SAR:
        if (shift_count != 0) {
          __ srawi(result, left, shift_count);
        } else {
          __ Move(result, left);
        }
        break;
      case Token::SHR:
        if (shift_count != 0) {
          __ srwi(result, left, Operand(shift_count));
        } else {
          if (instr->can_deopt()) {
            __ TestSignBit32(left, r0);
            DeoptimizeIf(ne, instr->environment(), cr0);
          }
          __ Move(result, left);
        }
        break;
      case Token::SHL:
        if (shift_count != 0) {
#if V8_TARGET_ARCH_PPC64
          if (instr->hydrogen_value()->representation().IsSmi()) {
            __ sldi(result, left, Operand(shift_count));
#else
          if (instr->hydrogen_value()->representation().IsSmi() &&
              instr->can_deopt()) {
            if (shift_count != 1) {
              __ slwi(result, left, Operand(shift_count - 1));
              __ SmiTagCheckOverflow(result, result, scratch);
            } else {
              __ SmiTagCheckOverflow(result, left, scratch);
            }
            DeoptimizeIf(lt, instr->environment(), cr0);
#endif
          } else {
            __ slwi(result, left, Operand(shift_count));
#if V8_TARGET_ARCH_PPC64
            __ extsw(result, result);
#endif
          }
        } else {
          __ Move(result, left);
        }
        break;
      default:
        UNREACHABLE();
        break;
    }
  }
}


void LCodeGen::DoSubI(LSubI* instr) {
  LOperand* left = instr->left();
  LOperand* right = instr->right();
  LOperand* result = instr->result();
  bool can_overflow = instr->hydrogen()->CheckFlag(HValue::kCanOverflow);
  if (!can_overflow && right->IsConstantOperand()) {
    Operand right_operand = ToOperand(right);
    if (is_int16(right_operand.immediate())) {
      __ subi(ToRegister(result), ToRegister(left), right_operand);
      return;
    }
  }
  Register right_reg = EmitLoadRegister(right, ip);

  if (!can_overflow) {
    __ sub(ToRegister(result), ToRegister(left), right_reg);
  } else {
    __ SubAndCheckForOverflow(ToRegister(result),
                              ToRegister(left),
                              right_reg,
                              scratch0(), r0);
    // Doptimize on overflow
#if V8_TARGET_ARCH_PPC64
    if (!instr->hydrogen()->representation().IsSmi()) {
      __ extsw(scratch0(), scratch0(), SetRC);
    }
#endif
    DeoptimizeIf(lt, instr->environment(), cr0);
  }
}


void LCodeGen::DoRSubI(LRSubI* instr) {
  LOperand* left = instr->left();
  LOperand* right = instr->right();
  LOperand* result = instr->result();

  ASSERT(!instr->hydrogen()->CheckFlag(HValue::kCanOverflow) &&
         right->IsConstantOperand());

  if (is_int16(ToInteger32(LConstantOperand::cast(right)))) {
    __ subfic(ToRegister(result), ToRegister(left), ToOperand(right));
  } else {
    Register right_reg = EmitLoadRegister(right, ip);
    __ sub(ToRegister(result), right_reg, ToRegister(left));
  }
}


void LCodeGen::DoConstantI(LConstantI* instr) {
  __ mov(ToRegister(instr->result()), Operand(instr->value()));
}


void LCodeGen::DoConstantS(LConstantS* instr) {
  __ LoadSmiLiteral(ToRegister(instr->result()), instr->value());
}


// TODO(penguin): put const to constant pool instead
// of storing double to stack
void LCodeGen::DoConstantD(LConstantD* instr) {
  ASSERT(instr->result()->IsDoubleRegister());
  DoubleRegister result = ToDoubleRegister(instr->result());
  double v = instr->value();
  __ LoadDoubleLiteral(result, v, scratch0());
}


void LCodeGen::DoConstantE(LConstantE* instr) {
  __ mov(ToRegister(instr->result()), Operand(instr->value()));
}


void LCodeGen::DoConstantT(LConstantT* instr) {
  Handle<Object> value = instr->value(isolate());
  AllowDeferredHandleDereference smi_check;
  __ Move(ToRegister(instr->result()), value);
}


void LCodeGen::DoMapEnumLength(LMapEnumLength* instr) {
  Register result = ToRegister(instr->result());
  Register map = ToRegister(instr->value());
  __ EnumLength(result, map);
}


void LCodeGen::DoElementsKind(LElementsKind* instr) {
  Register result = ToRegister(instr->result());
  Register input = ToRegister(instr->value());

  // Load map into |result|.
  __ LoadP(result, FieldMemOperand(input, HeapObject::kMapOffset));
  // Load the map's "bit field 2" into |result|
  __ lbz(result, FieldMemOperand(result, Map::kBitField2Offset));
  // Retrieve elements_kind from bit field 2.
  __ ExtractBitMask(result, result, Map::kElementsKindMask);
}


void LCodeGen::DoValueOf(LValueOf* instr) {
  Register input = ToRegister(instr->value());
  Register result = ToRegister(instr->result());
  Register map = ToRegister(instr->temp());
  Label done, is_smi_or_object;

  if (!instr->hydrogen()->value()->IsHeapObject()) {
    // If the object is a smi return the object.
    __ JumpIfSmi(input, &is_smi_or_object);
  }

  // If the object is not a value type, return the object.
  __ CompareObjectType(input, map, map, JS_VALUE_TYPE);
  __ bne(&is_smi_or_object);

  __ LoadP(result, FieldMemOperand(input, JSValue::kValueOffset));
  __ b(&done);

  __ bind(&is_smi_or_object);
  __ Move(result, input);

  __ bind(&done);
}


void LCodeGen::DoDateField(LDateField* instr) {
  Register object = ToRegister(instr->date());
  Register result = ToRegister(instr->result());
  Register scratch = ToRegister(instr->temp());
  Smi* index = instr->index();
  Label runtime, done;
  ASSERT(object.is(result));
  ASSERT(object.is(r3));
  ASSERT(!scratch.is(scratch0()));
  ASSERT(!scratch.is(object));

  __ TestIfSmi(object, r0);
  DeoptimizeIf(eq, instr->environment(), cr0);
  __ CompareObjectType(object, scratch, scratch, JS_DATE_TYPE);
  DeoptimizeIf(ne, instr->environment());

  if (index->value() == 0) {
    __ LoadP(result, FieldMemOperand(object, JSDate::kValueOffset));
  } else {
    if (index->value() < JSDate::kFirstUncachedField) {
      ExternalReference stamp = ExternalReference::date_cache_stamp(isolate());
      __ mov(scratch, Operand(stamp));
      __ LoadP(scratch, MemOperand(scratch));
      __ LoadP(scratch0(), FieldMemOperand(object, JSDate::kCacheStampOffset));
      __ cmp(scratch, scratch0());
      __ bne(&runtime);
      __ LoadP(result, FieldMemOperand(object, JSDate::kValueOffset +
                                       kPointerSize * index->value()));
      __ b(&done);
    }
    __ bind(&runtime);
    __ PrepareCallCFunction(2, scratch);
    __ LoadSmiLiteral(r4, index);
    __ CallCFunction(ExternalReference::get_date_field_function(isolate()), 2);
    __ bind(&done);
  }
}


void LCodeGen::DoSeqStringSetChar(LSeqStringSetChar* instr) {
  Register string = ToRegister(instr->string());
  LOperand* index_op = instr->index();
  Register value = ToRegister(instr->value());
  Register scratch = scratch0();
  String::Encoding encoding = instr->encoding();

  if (FLAG_debug_code) {
    __ LoadP(scratch, FieldMemOperand(string, HeapObject::kMapOffset));
    __ lbz(scratch, FieldMemOperand(scratch, Map::kInstanceTypeOffset));

    __ andi(scratch, scratch,
            Operand(kStringRepresentationMask | kStringEncodingMask));
    static const uint32_t one_byte_seq_type = kSeqStringTag | kOneByteStringTag;
    static const uint32_t two_byte_seq_type = kSeqStringTag | kTwoByteStringTag;
    __ cmpi(scratch, Operand(encoding == String::ONE_BYTE_ENCODING
                             ? one_byte_seq_type : two_byte_seq_type));
    __ Check(eq, kUnexpectedStringType);
  }

  if (index_op->IsConstantOperand()) {
    int constant_index = ToInteger32(LConstantOperand::cast(index_op));
    if (encoding == String::ONE_BYTE_ENCODING) {
      __ stb(value,
             FieldMemOperand(string, SeqString::kHeaderSize + constant_index));
    } else {
      __ sth(value,
             FieldMemOperand(string, SeqString::kHeaderSize +
                             constant_index * 2));
    }
  } else {
    Register index = ToRegister(index_op);
    if (encoding == String::ONE_BYTE_ENCODING) {
      __ add(scratch, string, index);
      __ stb(value, FieldMemOperand(scratch, SeqString::kHeaderSize));
    } else {
      __ ShiftLeftImm(scratch, index, Operand(1));
      __ add(scratch, string, scratch);
      __ sth(value, FieldMemOperand(scratch, SeqString::kHeaderSize));
    }
  }
}


void LCodeGen::DoThrow(LThrow* instr) {
  Register input_reg = EmitLoadRegister(instr->value(), ip);
  __ push(input_reg);
  ASSERT(ToRegister(instr->context()).is(cp));
  CallRuntime(Runtime::kThrow, 1, instr);

  if (FLAG_debug_code) {
    __ stop("Unreachable code.");
  }
}


void LCodeGen::DoAddI(LAddI* instr) {
  LOperand* left = instr->left();
  LOperand* right = instr->right();
  LOperand* result = instr->result();
  bool can_overflow = instr->hydrogen()->CheckFlag(HValue::kCanOverflow);

  if (!can_overflow && right->IsConstantOperand()) {
    Operand right_operand = ToOperand(right);
    if (is_int16(right_operand.immediate())) {
      __ addi(ToRegister(result), ToRegister(left), right_operand);
      return;
    }
  }

  Register right_reg = EmitLoadRegister(right, ip);

  if (!can_overflow) {
    __ add(ToRegister(result), ToRegister(left), right_reg);
  } else {  // can_overflow.
    __ AddAndCheckForOverflow(ToRegister(result),
                              ToRegister(left),
                              right_reg,
                              scratch0(), r0);
#if V8_TARGET_ARCH_PPC64
    if (!instr->hydrogen()->representation().IsSmi()) {
      __ extsw(scratch0(), scratch0(), SetRC);
    }
#endif
    // Doptimize on overflow
    DeoptimizeIf(lt, instr->environment(), cr0);
  }
}


void LCodeGen::DoMathMinMax(LMathMinMax* instr) {
  LOperand* left = instr->left();
  LOperand* right = instr->right();
  HMathMinMax::Operation operation = instr->hydrogen()->operation();
  Condition cond = (operation == HMathMinMax::kMathMin) ? le : ge;
  if (instr->hydrogen()->representation().IsSmiOrInteger32()) {
    Register left_reg = ToRegister(left);
    Register right_reg = EmitLoadRegister(right, ip);
    Register result_reg = ToRegister(instr->result());
    Label return_left, done;
    __ cmp(left_reg, right_reg);
    __ b(cond, &return_left);
    __ Move(result_reg, right_reg);
    __ b(&done);
    __ bind(&return_left);
    __ Move(result_reg, left_reg);
    __ bind(&done);
  } else {
    ASSERT(instr->hydrogen()->representation().IsDouble());
    DoubleRegister left_reg = ToDoubleRegister(left);
    DoubleRegister right_reg = ToDoubleRegister(right);
    DoubleRegister result_reg = ToDoubleRegister(instr->result());
    Label check_nan_left, check_zero, return_left, return_right, done;
    __ fcmpu(left_reg, right_reg);
    __ bunordered(&check_nan_left);
    __ beq(&check_zero);
    __ b(cond, &return_left);
    __ b(&return_right);

    __ bind(&check_zero);
    __ fcmpu(left_reg, kDoubleRegZero);
    __ bne(&return_left);  // left == right != 0.

    // At this point, both left and right are either 0 or -0.
    // N.B. The following works because +0 + -0 == +0
    if (operation == HMathMinMax::kMathMin) {
      // For min we want logical-or of sign bit: -(-L + -R)
      __ fneg(left_reg, left_reg);
      __ fsub(result_reg, left_reg, right_reg);
      __ fneg(result_reg, result_reg);
    } else {
      // For max we want logical-and of sign bit: (L + R)
      __ fadd(result_reg, left_reg, right_reg);
    }
    __ b(&done);

    __ bind(&check_nan_left);
    __ fcmpu(left_reg, left_reg);
    __ bunordered(&return_left);  // left == NaN.

    __ bind(&return_right);
    if (!right_reg.is(result_reg)) {
      __ fmr(result_reg, right_reg);
    }
    __ b(&done);

    __ bind(&return_left);
    if (!left_reg.is(result_reg)) {
      __ fmr(result_reg, left_reg);
    }
    __ bind(&done);
  }
}


void LCodeGen::DoArithmeticD(LArithmeticD* instr) {
  DoubleRegister left = ToDoubleRegister(instr->left());
  DoubleRegister right = ToDoubleRegister(instr->right());
  DoubleRegister result = ToDoubleRegister(instr->result());
  switch (instr->op()) {
    case Token::ADD:
      __ fadd(result, left, right);
      break;
    case Token::SUB:
      __ fsub(result, left, right);
      break;
    case Token::MUL:
      __ fmul(result, left, right);
      break;
    case Token::DIV:
      __ fdiv(result, left, right);
      break;
    case Token::MOD: {
      // Save r3-r6 on the stack.
      __ MultiPush(r3.bit() | r4.bit() | r5.bit() | r6.bit());

      __ PrepareCallCFunction(0, 2, scratch0());
      __ SetCallCDoubleArguments(left, right);
      __ CallCFunction(
          ExternalReference::double_fp_operation(Token::MOD, isolate()),
          0, 2);
      // Move the result in the double result register.
      __ GetCFunctionDoubleResult(result);

      // Restore r3-r6.
      __ MultiPop(r3.bit() | r4.bit() | r5.bit() | r6.bit());
      break;
    }
    default:
      UNREACHABLE();
      break;
  }
}


void LCodeGen::DoArithmeticT(LArithmeticT* instr) {
  ASSERT(ToRegister(instr->context()).is(cp));
  ASSERT(ToRegister(instr->left()).is(r4));
  ASSERT(ToRegister(instr->right()).is(r3));
  ASSERT(ToRegister(instr->result()).is(r3));

  BinaryOpStub stub(instr->op(), NO_OVERWRITE);
  CallCode(stub.GetCode(isolate()), RelocInfo::CODE_TARGET, instr);
  __ nop();  // Signals no inlined code.
}


template<class InstrType>
void LCodeGen::EmitBranch(InstrType instr, Condition cond,
                          CRegister cr) {
  int left_block = instr->TrueDestination(chunk_);
  int right_block = instr->FalseDestination(chunk_);

  int next_block = GetNextEmittedBlock();

  if (right_block == left_block || cond == al) {
    EmitGoto(left_block);
  } else if (left_block == next_block) {
    __ b(NegateCondition(cond), chunk_->GetAssemblyLabel(right_block), cr);
  } else if (right_block == next_block) {
    __ b(cond, chunk_->GetAssemblyLabel(left_block), cr);
  } else {
    __ b(cond, chunk_->GetAssemblyLabel(left_block), cr);
    __ b(chunk_->GetAssemblyLabel(right_block));
  }
}


template<class InstrType>
void LCodeGen::EmitFalseBranch(InstrType instr, Condition cond,
                               CRegister cr) {
  int false_block = instr->FalseDestination(chunk_);
  __ b(cond, chunk_->GetAssemblyLabel(false_block), cr);
}


void LCodeGen::DoDebugBreak(LDebugBreak* instr) {
  __ stop("LBreak");
}


void LCodeGen::DoBranch(LBranch* instr) {
  Representation r = instr->hydrogen()->value()->representation();
  DoubleRegister dbl_scratch = double_scratch0();
  const uint crZOrNaNBits = (1 << (31 - Assembler::encode_crbit(cr7, CR_EQ)) |
                             1 << (31 - Assembler::encode_crbit(cr7, CR_FU)));

  if (r.IsInteger32() || r.IsSmi()) {
    ASSERT(!info()->IsStub());
    Register reg = ToRegister(instr->value());
    __ cmpi(reg, Operand::Zero());
    EmitBranch(instr, ne);
  } else if (r.IsDouble()) {
    ASSERT(!info()->IsStub());
    DoubleRegister reg = ToDoubleRegister(instr->value());
    // Test the double value. Zero and NaN are false.
    __ fcmpu(reg, kDoubleRegZero, cr7);
    __ mfcr(r0);
    __ andi(r0, r0, Operand(crZOrNaNBits));
    EmitBranch(instr, eq, cr0);
  } else {
    ASSERT(r.IsTagged());
    Register reg = ToRegister(instr->value());
    HType type = instr->hydrogen()->value()->type();
    if (type.IsBoolean()) {
      ASSERT(!info()->IsStub());
      __ CompareRoot(reg, Heap::kTrueValueRootIndex);
      EmitBranch(instr, eq);
    } else if (type.IsSmi()) {
      ASSERT(!info()->IsStub());
      __ cmpi(reg, Operand::Zero());
      EmitBranch(instr, ne);
    } else if (type.IsJSArray()) {
      ASSERT(!info()->IsStub());
      EmitBranch(instr, al);
    } else if (type.IsHeapNumber()) {
      ASSERT(!info()->IsStub());
      __ lfd(dbl_scratch, FieldMemOperand(reg, HeapNumber::kValueOffset));
      // Test the double value. Zero and NaN are false.
      __ fcmpu(dbl_scratch, kDoubleRegZero, cr7);
      __ mfcr(r0);
      __ andi(r0, r0, Operand(crZOrNaNBits));
      EmitBranch(instr, eq, cr0);
    } else if (type.IsString()) {
      ASSERT(!info()->IsStub());
      __ LoadP(ip, FieldMemOperand(reg, String::kLengthOffset));
      __ cmpi(ip, Operand::Zero());
      EmitBranch(instr, ne);
    } else {
      ToBooleanStub::Types expected = instr->hydrogen()->expected_input_types();
      // Avoid deopts in the case where we've never executed this path before.
      if (expected.IsEmpty()) expected = ToBooleanStub::Types::Generic();

      if (expected.Contains(ToBooleanStub::UNDEFINED)) {
        // undefined -> false.
        __ CompareRoot(reg, Heap::kUndefinedValueRootIndex);
        __ beq(instr->FalseLabel(chunk_));
      }
      if (expected.Contains(ToBooleanStub::BOOLEAN)) {
        // Boolean -> its value.
        __ CompareRoot(reg, Heap::kTrueValueRootIndex);
        __ beq(instr->TrueLabel(chunk_));
        __ CompareRoot(reg, Heap::kFalseValueRootIndex);
        __ beq(instr->FalseLabel(chunk_));
      }
      if (expected.Contains(ToBooleanStub::NULL_TYPE)) {
        // 'null' -> false.
        __ CompareRoot(reg, Heap::kNullValueRootIndex);
        __ beq(instr->FalseLabel(chunk_));
      }

      if (expected.Contains(ToBooleanStub::SMI)) {
        // Smis: 0 -> false, all other -> true.
        __ cmpi(reg, Operand::Zero());
        __ beq(instr->FalseLabel(chunk_));
        __ JumpIfSmi(reg, instr->TrueLabel(chunk_));
      } else if (expected.NeedsMap()) {
        // If we need a map later and have a Smi -> deopt.
        __ TestIfSmi(reg, r0);
        DeoptimizeIf(eq, instr->environment(), cr0);
      }

      const Register map = scratch0();
      if (expected.NeedsMap()) {
        __ LoadP(map, FieldMemOperand(reg, HeapObject::kMapOffset));

        if (expected.CanBeUndetectable()) {
          // Undetectable -> false.
          __ lbz(ip, FieldMemOperand(map, Map::kBitFieldOffset));
          __ TestBit(ip, Map::kIsUndetectable, r0);
          __ bne(instr->FalseLabel(chunk_), cr0);
        }
      }

      if (expected.Contains(ToBooleanStub::SPEC_OBJECT)) {
        // spec object -> true.
        __ CompareInstanceType(map, ip, FIRST_SPEC_OBJECT_TYPE);
        __ bge(instr->TrueLabel(chunk_));
      }

      if (expected.Contains(ToBooleanStub::STRING)) {
        // String value -> false iff empty.
        Label not_string;
        __ CompareInstanceType(map, ip, FIRST_NONSTRING_TYPE);
        __ bge(&not_string);
        __ LoadP(ip, FieldMemOperand(reg, String::kLengthOffset));
        __ cmpi(ip, Operand::Zero());
        __ bne(instr->TrueLabel(chunk_));
        __ b(instr->FalseLabel(chunk_));
        __ bind(&not_string);
      }

      if (expected.Contains(ToBooleanStub::SYMBOL)) {
        // Symbol value -> true.
        __ CompareInstanceType(map, ip, SYMBOL_TYPE);
        __ beq(instr->TrueLabel(chunk_));
      }

      if (expected.Contains(ToBooleanStub::HEAP_NUMBER)) {
        // heap number -> false iff +0, -0, or NaN.
        Label not_heap_number;
        __ CompareRoot(map, Heap::kHeapNumberMapRootIndex);
        __ bne(&not_heap_number);
        __ lfd(dbl_scratch, FieldMemOperand(reg, HeapNumber::kValueOffset));
        // Test the double value. Zero and NaN are false.
        __ fcmpu(dbl_scratch, kDoubleRegZero, cr7);
        __ mfcr(r0);
        __ andi(r0, r0, Operand(crZOrNaNBits));
        __ bne(instr->FalseLabel(chunk_), cr0);
        __ b(instr->TrueLabel(chunk_));
        __ bind(&not_heap_number);
      }

      if (!expected.IsGeneric()) {
        // We've seen something for the first time -> deopt.
        // This can only happen if we are not generic already.
        DeoptimizeIf(al, instr->environment());
      }
    }
  }
}


void LCodeGen::EmitGoto(int block) {
  if (!IsNextEmittedBlock(block)) {
    __ b(chunk_->GetAssemblyLabel(LookupDestination(block)));
  }
}


void LCodeGen::DoGoto(LGoto* instr) {
  EmitGoto(instr->block_id());
}


Condition LCodeGen::TokenToCondition(Token::Value op) {
  Condition cond = kNoCondition;
  switch (op) {
    case Token::EQ:
    case Token::EQ_STRICT:
      cond = eq;
      break;
    case Token::NE:
    case Token::NE_STRICT:
      cond = ne;
      break;
    case Token::LT:
      cond =  lt;
      break;
    case Token::GT:
      cond = gt;
      break;
    case Token::LTE:
      cond = le;
      break;
    case Token::GTE:
      cond = ge;
      break;
    case Token::IN:
    case Token::INSTANCEOF:
    default:
      UNREACHABLE();
  }
  return cond;
}


void LCodeGen::DoCompareNumericAndBranch(LCompareNumericAndBranch* instr) {
  LOperand* left = instr->left();
  LOperand* right = instr->right();
  Condition cond = TokenToCondition(instr->op());

  if (left->IsConstantOperand() && right->IsConstantOperand()) {
    // We can statically evaluate the comparison.
    double left_val = ToDouble(LConstantOperand::cast(left));
    double right_val = ToDouble(LConstantOperand::cast(right));
    int next_block = EvalComparison(instr->op(), left_val, right_val) ?
        instr->TrueDestination(chunk_) : instr->FalseDestination(chunk_);
    EmitGoto(next_block);
  } else {
    if (instr->is_double()) {
      // Compare left and right operands as doubles and load the
      // resulting flags into the normal status register.
      __ fcmpu(ToDoubleRegister(left), ToDoubleRegister(right));
      // If a NaN is involved, i.e. the result is unordered,
      // jump to false block label.
      __ bunordered(instr->FalseLabel(chunk_));
    } else {
      if (right->IsConstantOperand()) {
        int32_t value = ToInteger32(LConstantOperand::cast(right));
        if (instr->hydrogen_value()->representation().IsSmi()) {
          __ CmpSmiLiteral(ToRegister(left), Smi::FromInt(value), r0);
        } else {
          __ Cmpi(ToRegister(left), Operand(value), r0);
        }
      } else if (left->IsConstantOperand()) {
        int32_t value = ToInteger32(LConstantOperand::cast(left));
        if (instr->hydrogen_value()->representation().IsSmi()) {
          __ CmpSmiLiteral(ToRegister(right), Smi::FromInt(value), r0);
        } else {
          __ Cmpi(ToRegister(right), Operand(value), r0);
        }
        // We transposed the operands. Reverse the condition.
        cond = ReverseCondition(cond);
      } else {
        __ cmp(ToRegister(left), ToRegister(right));
      }
    }
    EmitBranch(instr, cond);
  }
}


void LCodeGen::DoCmpObjectEqAndBranch(LCmpObjectEqAndBranch* instr) {
  Register left = ToRegister(instr->left());
  Register right = ToRegister(instr->right());

  __ cmp(left, right);
  EmitBranch(instr, eq);
}


void LCodeGen::DoCmpHoleAndBranch(LCmpHoleAndBranch* instr) {
  if (instr->hydrogen()->representation().IsTagged()) {
    Register input_reg = ToRegister(instr->object());
    __ mov(ip, Operand(factory()->the_hole_value()));
    __ cmp(input_reg, ip);
    EmitBranch(instr, eq);
    return;
  }

  DoubleRegister input_reg = ToDoubleRegister(instr->object());
  __ fcmpu(input_reg, input_reg);
  EmitFalseBranch(instr, ordered);

  Register scratch = scratch0();
  __ stfdu(input_reg, MemOperand(sp, -kDoubleSize));
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
  __ lwz(scratch, MemOperand(sp, 4));
#else
  __ lwz(scratch, MemOperand(sp, 0));
#endif
  __ addi(sp, sp, Operand(kDoubleSize));
  __ Cmpi(scratch, Operand(kHoleNanUpper32), r0);
  EmitBranch(instr, eq);
}


Condition LCodeGen::EmitIsObject(Register input,
                                 Register temp1,
                                 Label* is_not_object,
                                 Label* is_object) {
  Register temp2 = scratch0();
  __ JumpIfSmi(input, is_not_object);

  __ LoadRoot(temp2, Heap::kNullValueRootIndex);
  __ cmp(input, temp2);
  __ beq(is_object);

  // Load map.
  __ LoadP(temp1, FieldMemOperand(input, HeapObject::kMapOffset));
  // Undetectable objects behave like undefined.
  __ lbz(temp2, FieldMemOperand(temp1, Map::kBitFieldOffset));
  __ TestBit(temp2, Map::kIsUndetectable, r0);
  __ bne(is_not_object, cr0);

  // Load instance type and check that it is in object type range.
  __ lbz(temp2, FieldMemOperand(temp1, Map::kInstanceTypeOffset));
  __ cmpi(temp2, Operand(FIRST_NONCALLABLE_SPEC_OBJECT_TYPE));
  __ blt(is_not_object);
  __ cmpi(temp2, Operand(LAST_NONCALLABLE_SPEC_OBJECT_TYPE));
  return le;
}


void LCodeGen::DoIsObjectAndBranch(LIsObjectAndBranch* instr) {
  Register reg = ToRegister(instr->value());
  Register temp1 = ToRegister(instr->temp());

  Condition true_cond =
      EmitIsObject(reg, temp1,
          instr->FalseLabel(chunk_), instr->TrueLabel(chunk_));

  EmitBranch(instr, true_cond);
}


Condition LCodeGen::EmitIsString(Register input,
                                 Register temp1,
                                 Label* is_not_string,
                                 SmiCheck check_needed = INLINE_SMI_CHECK) {
  if (check_needed == INLINE_SMI_CHECK) {
    __ JumpIfSmi(input, is_not_string);
  }
  __ CompareObjectType(input, temp1, temp1, FIRST_NONSTRING_TYPE);

  return lt;
}


void LCodeGen::DoIsStringAndBranch(LIsStringAndBranch* instr) {
  Register reg = ToRegister(instr->value());
  Register temp1 = ToRegister(instr->temp());

  SmiCheck check_needed =
      instr->hydrogen()->value()->IsHeapObject()
          ? OMIT_SMI_CHECK : INLINE_SMI_CHECK;
  Condition true_cond =
      EmitIsString(reg, temp1, instr->FalseLabel(chunk_), check_needed);

  EmitBranch(instr, true_cond);
}


void LCodeGen::DoIsSmiAndBranch(LIsSmiAndBranch* instr) {
  Register input_reg = EmitLoadRegister(instr->value(), ip);
  __ TestIfSmi(input_reg, r0);
  EmitBranch(instr, eq, cr0);
}


void LCodeGen::DoIsUndetectableAndBranch(LIsUndetectableAndBranch* instr) {
  Register input = ToRegister(instr->value());
  Register temp = ToRegister(instr->temp());

  if (!instr->hydrogen()->value()->IsHeapObject()) {
    __ JumpIfSmi(input, instr->FalseLabel(chunk_));
  }
  __ LoadP(temp, FieldMemOperand(input, HeapObject::kMapOffset));
  __ lbz(temp, FieldMemOperand(temp, Map::kBitFieldOffset));
  __ TestBit(temp, Map::kIsUndetectable, r0);
  EmitBranch(instr, ne, cr0);
}


static Condition ComputeCompareCondition(Token::Value op) {
  switch (op) {
    case Token::EQ_STRICT:
    case Token::EQ:
      return eq;
    case Token::LT:
      return lt;
    case Token::GT:
      return gt;
    case Token::LTE:
      return le;
    case Token::GTE:
      return ge;
    default:
      UNREACHABLE();
      return kNoCondition;
  }
}


void LCodeGen::DoStringCompareAndBranch(LStringCompareAndBranch* instr) {
  ASSERT(ToRegister(instr->context()).is(cp));
  Token::Value op = instr->op();

  Handle<Code> ic = CompareIC::GetUninitialized(isolate(), op);
  CallCode(ic, RelocInfo::CODE_TARGET, instr);
  // This instruction also signals no smi code inlined
  __ cmpi(r3, Operand::Zero());

  Condition condition = ComputeCompareCondition(op);

  EmitBranch(instr, condition);
}


static InstanceType TestType(HHasInstanceTypeAndBranch* instr) {
  InstanceType from = instr->from();
  InstanceType to = instr->to();
  if (from == FIRST_TYPE) return to;
  ASSERT(from == to || to == LAST_TYPE);
  return from;
}


static Condition BranchCondition(HHasInstanceTypeAndBranch* instr) {
  InstanceType from = instr->from();
  InstanceType to = instr->to();
  if (from == to) return eq;
  if (to == LAST_TYPE) return ge;
  if (from == FIRST_TYPE) return le;
  UNREACHABLE();
  return eq;
}


void LCodeGen::DoHasInstanceTypeAndBranch(LHasInstanceTypeAndBranch* instr) {
  Register scratch = scratch0();
  Register input = ToRegister(instr->value());

  if (!instr->hydrogen()->value()->IsHeapObject()) {
    __ JumpIfSmi(input, instr->FalseLabel(chunk_));
  }

  __ CompareObjectType(input, scratch, scratch, TestType(instr->hydrogen()));
  EmitBranch(instr, BranchCondition(instr->hydrogen()));
}


void LCodeGen::DoGetCachedArrayIndex(LGetCachedArrayIndex* instr) {
  Register input = ToRegister(instr->value());
  Register result = ToRegister(instr->result());

  __ AssertString(input);

  __ lwz(result, FieldMemOperand(input, String::kHashFieldOffset));
  __ IndexFromHash(result, result);
}


void LCodeGen::DoHasCachedArrayIndexAndBranch(
    LHasCachedArrayIndexAndBranch* instr) {
  Register input = ToRegister(instr->value());
  Register scratch = scratch0();

  __ lwz(scratch,
         FieldMemOperand(input, String::kHashFieldOffset));
  __ mov(r0, Operand(String::kContainsCachedArrayIndexMask));
  __ and_(r0, scratch, r0, SetRC);
  EmitBranch(instr, eq, cr0);
}


// Branches to a label or falls through with the answer in flags.  Trashes
// the temp registers, but not the input.
void LCodeGen::EmitClassOfTest(Label* is_true,
                               Label* is_false,
                               Handle<String>class_name,
                               Register input,
                               Register temp,
                               Register temp2) {
  ASSERT(!input.is(temp));
  ASSERT(!input.is(temp2));
  ASSERT(!temp.is(temp2));

  __ JumpIfSmi(input, is_false);

  if (class_name->IsOneByteEqualTo(STATIC_ASCII_VECTOR("Function"))) {
    // Assuming the following assertions, we can use the same compares to test
    // for both being a function type and being in the object type range.
    STATIC_ASSERT(NUM_OF_CALLABLE_SPEC_OBJECT_TYPES == 2);
    STATIC_ASSERT(FIRST_NONCALLABLE_SPEC_OBJECT_TYPE ==
                  FIRST_SPEC_OBJECT_TYPE + 1);
    STATIC_ASSERT(LAST_NONCALLABLE_SPEC_OBJECT_TYPE ==
                  LAST_SPEC_OBJECT_TYPE - 1);
    STATIC_ASSERT(LAST_SPEC_OBJECT_TYPE == LAST_TYPE);
    __ CompareObjectType(input, temp, temp2, FIRST_SPEC_OBJECT_TYPE);
    __ blt(is_false);
    __ beq(is_true);
    __ cmpi(temp2, Operand(LAST_SPEC_OBJECT_TYPE));
    __ beq(is_true);
  } else {
    // Faster code path to avoid two compares: subtract lower bound from the
    // actual type and do a signed compare with the width of the type range.
    __ LoadP(temp, FieldMemOperand(input, HeapObject::kMapOffset));
    __ lbz(temp2, FieldMemOperand(temp, Map::kInstanceTypeOffset));
    __ subi(temp2, temp2, Operand(FIRST_NONCALLABLE_SPEC_OBJECT_TYPE));
    __ cmpi(temp2, Operand(LAST_NONCALLABLE_SPEC_OBJECT_TYPE -
                          FIRST_NONCALLABLE_SPEC_OBJECT_TYPE));
    __ bgt(is_false);
  }

  // Now we are in the FIRST-LAST_NONCALLABLE_SPEC_OBJECT_TYPE range.
  // Check if the constructor in the map is a function.
  __ LoadP(temp, FieldMemOperand(temp, Map::kConstructorOffset));

  // Objects with a non-function constructor have class 'Object'.
  __ CompareObjectType(temp, temp2, temp2, JS_FUNCTION_TYPE);
  if (class_name->IsOneByteEqualTo(STATIC_ASCII_VECTOR("Object"))) {
    __ bne(is_true);
  } else {
    __ bne(is_false);
  }

  // temp now contains the constructor function. Grab the
  // instance class name from there.
  __ LoadP(temp, FieldMemOperand(temp, JSFunction::kSharedFunctionInfoOffset));
  __ LoadP(temp, FieldMemOperand(temp,
                                 SharedFunctionInfo::kInstanceClassNameOffset));
  // The class name we are testing against is internalized since it's a literal.
  // The name in the constructor is internalized because of the way the context
  // is booted.  This routine isn't expected to work for random API-created
  // classes and it doesn't have to because you can't access it with natives
  // syntax.  Since both sides are internalized it is sufficient to use an
  // identity comparison.
  __ Cmpi(temp, Operand(class_name), r0);
  // End with the answer in flags.
}


void LCodeGen::DoClassOfTestAndBranch(LClassOfTestAndBranch* instr) {
  Register input = ToRegister(instr->value());
  Register temp = scratch0();
  Register temp2 = ToRegister(instr->temp());
  Handle<String> class_name = instr->hydrogen()->class_name();

  EmitClassOfTest(instr->TrueLabel(chunk_), instr->FalseLabel(chunk_),
      class_name, input, temp, temp2);

  EmitBranch(instr, eq);
}


void LCodeGen::DoCmpMapAndBranch(LCmpMapAndBranch* instr) {
  Register reg = ToRegister(instr->value());
  Register temp = ToRegister(instr->temp());

  __ LoadP(temp, FieldMemOperand(reg, HeapObject::kMapOffset));
  __ Cmpi(temp, Operand(instr->map()), r0);
  EmitBranch(instr, eq);
}


void LCodeGen::DoInstanceOf(LInstanceOf* instr) {
  ASSERT(ToRegister(instr->context()).is(cp));
  ASSERT(ToRegister(instr->left()).is(r3));  // Object is in r3.
  ASSERT(ToRegister(instr->right()).is(r4));  // Function is in r4.

  InstanceofStub stub(InstanceofStub::kArgsInRegisters);
  Label equal, done;
  CallCode(stub.GetCode(isolate()), RelocInfo::CODE_TARGET, instr);

  __ cmpi(r3, Operand::Zero());
  __ beq(&equal);
  __ mov(r3, Operand(factory()->false_value()));
  __ b(&done);

  __ bind(&equal);
  __ mov(r3, Operand(factory()->true_value()));
  __ bind(&done);
}


void LCodeGen::DoInstanceOfKnownGlobal(LInstanceOfKnownGlobal* instr) {
  class DeferredInstanceOfKnownGlobal V8_FINAL : public LDeferredCode {
   public:
    DeferredInstanceOfKnownGlobal(LCodeGen* codegen,
                                  LInstanceOfKnownGlobal* instr)
        : LDeferredCode(codegen), instr_(instr) { }
    virtual void Generate() V8_OVERRIDE {
      codegen()->DoDeferredInstanceOfKnownGlobal(instr_, &map_check_);
    }
    virtual LInstruction* instr() V8_OVERRIDE { return instr_; }
    Label* map_check() { return &map_check_; }
   private:
    LInstanceOfKnownGlobal* instr_;
    Label map_check_;
  };

  DeferredInstanceOfKnownGlobal* deferred;
  deferred = new(zone()) DeferredInstanceOfKnownGlobal(this, instr);

  Label done, false_result;
  Register object = ToRegister(instr->value());
  Register temp = ToRegister(instr->temp());
  Register result = ToRegister(instr->result());

  ASSERT(object.is(r3));
  ASSERT(result.is(r3));

  // A Smi is not instance of anything.
  __ JumpIfSmi(object, &false_result);

  // This is the inlined call site instanceof cache. The two occurences of the
  // hole value will be patched to the last map/result pair generated by the
  // instanceof stub.
  Label cache_miss;
  Register map = temp;
  __ LoadP(map, FieldMemOperand(object, HeapObject::kMapOffset));
  {
    // Block constant pool emission to ensure the positions of instructions are
    // as expected by the patcher. See InstanceofStub::Generate().
    Assembler::BlockTrampolinePoolScope block_trampoline_pool(masm_);
    __ bind(deferred->map_check());  // Label for calculating code patching.
    // We use Factory::the_hole_value() on purpose instead of loading from the
    // root array to force relocation to be able to later patch with
    // the cached map.
    Handle<Cell> cell = factory()->NewCell(factory()->the_hole_value());
    __ mov(ip, Operand(Handle<Object>(cell)));
    __ LoadP(ip, FieldMemOperand(ip, PropertyCell::kValueOffset));
    __ cmp(map, ip);
    __ bne(&cache_miss);
    // We use Factory::the_hole_value() on purpose instead of loading from the
    // root array to force relocation to be able to later patch
    // with true or false.
    __ mov(result, Operand(factory()->the_hole_value()));
  }
  __ b(&done);

  // The inlined call site cache did not match. Check null and string before
  // calling the deferred code.
  __ bind(&cache_miss);
  // Null is not instance of anything.
  __ LoadRoot(ip, Heap::kNullValueRootIndex);
  __ cmp(object, ip);
  __ beq(&false_result);

  // String values is not instance of anything.
  Condition is_string = masm_->IsObjectStringType(object, temp);
  __ b(is_string, &false_result, cr0);

  // Go to the deferred code.
  __ b(deferred->entry());

  __ bind(&false_result);
  __ LoadRoot(result, Heap::kFalseValueRootIndex);

  // Here result has either true or false. Deferred code also produces true or
  // false object.
  __ bind(deferred->exit());
  __ bind(&done);
}


void LCodeGen::DoDeferredInstanceOfKnownGlobal(LInstanceOfKnownGlobal* instr,
                                               Label* map_check) {
  Register result = ToRegister(instr->result());
  ASSERT(result.is(r3));

  InstanceofStub::Flags flags = InstanceofStub::kNoFlags;
  flags = static_cast<InstanceofStub::Flags>(
      flags | InstanceofStub::kArgsInRegisters);
  flags = static_cast<InstanceofStub::Flags>(
      flags | InstanceofStub::kCallSiteInlineCheck);
  flags = static_cast<InstanceofStub::Flags>(
      flags | InstanceofStub::kReturnTrueFalseObject);
  InstanceofStub stub(flags);

  PushSafepointRegistersScope scope(this, Safepoint::kWithRegisters);
  LoadContextFromDeferred(instr->context());

  // Get the temp register reserved by the instruction. This needs to be r7 as
  // its slot of the pushing of safepoint registers is used to communicate the
  // offset to the location of the map check.
  Register temp = ToRegister(instr->temp());
  ASSERT(temp.is(r7));
  __ Move(InstanceofStub::right(), instr->function());
#if V8_TARGET_ARCH_PPC64
  static const int kAdditionalDelta = 13;
#else
  static const int kAdditionalDelta = 7;
#endif
  int delta = masm_->InstructionsGeneratedSince(map_check) + kAdditionalDelta;
  Label before_push_delta;
  __ bind(&before_push_delta);
  {
    Assembler::BlockTrampolinePoolScope block_trampoline_pool(masm_);
    __ mov(temp, Operand(delta * Instruction::kInstrSize));
    __ StoreToSafepointRegisterSlot(temp, temp);
  }
  CallCodeGeneric(stub.GetCode(isolate()),
                  RelocInfo::CODE_TARGET,
                  instr,
                  RECORD_SAFEPOINT_WITH_REGISTERS_AND_NO_ARGUMENTS);
  ASSERT(delta == masm_->InstructionsGeneratedSince(map_check));
  LEnvironment* env = instr->GetDeferredLazyDeoptimizationEnvironment();
  safepoints_.RecordLazyDeoptimizationIndex(env->deoptimization_index());
  // Put the result value into the result register slot and
  // restore all registers.
  __ StoreToSafepointRegisterSlot(result, result);
}


void LCodeGen::DoCmpT(LCmpT* instr) {
  ASSERT(ToRegister(instr->context()).is(cp));
  Token::Value op = instr->op();

  Handle<Code> ic = CompareIC::GetUninitialized(isolate(), op);
  CallCode(ic, RelocInfo::CODE_TARGET, instr);
  // This instruction also signals no smi code inlined
  __ cmpi(r3, Operand::Zero());

  Condition condition = ComputeCompareCondition(op);
  Label true_value, done;

  __ b(condition, &true_value);

  __ LoadRoot(ToRegister(instr->result()), Heap::kFalseValueRootIndex);
  __ b(&done);

  __ bind(&true_value);
  __ LoadRoot(ToRegister(instr->result()), Heap::kTrueValueRootIndex);

  __ bind(&done);
}


void LCodeGen::DoReturn(LReturn* instr) {
  if (FLAG_trace && info()->IsOptimizing()) {
    // Push the return value on the stack as the parameter.
    // Runtime::TraceExit returns its parameter in r3.  We're leaving the code
    // managed by the register allocator and tearing down the frame, it's
    // safe to write to the context register.
    __ push(r3);
    __ LoadP(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
    __ CallRuntime(Runtime::kTraceExit, 1);
  }
  if (info()->saves_caller_doubles()) {
    ASSERT(NeedsEagerFrame());
    BitVector* doubles = chunk()->allocated_double_registers();
    BitVector::Iterator save_iterator(doubles);
    int count = 0;
    while (!save_iterator.Done()) {
      __ lfd(DoubleRegister::FromAllocationIndex(save_iterator.Current()),
             MemOperand(sp, count * kDoubleSize));
      save_iterator.Advance();
      count++;
    }
  }
  int no_frame_start = -1;
  if (NeedsEagerFrame()) {
    __ mr(sp, fp);
    no_frame_start = masm_->pc_offset();
    __ Pop(r0, fp);
    __ mtlr(r0);
  }
  if (instr->has_constant_parameter_count()) {
    int parameter_count = ToInteger32(instr->constant_parameter_count());
    int32_t sp_delta = (parameter_count + 1) * kPointerSize;
    if (sp_delta != 0) {
      __ addi(sp, sp, Operand(sp_delta));
    }
  } else {
    Register reg = ToRegister(instr->parameter_count());
    // The argument count parameter is a smi
    __ SmiToPtrArrayOffset(r0, reg);
    __ add(sp, sp, r0);
  }

  __ blr();

  if (no_frame_start != -1) {
    info_->AddNoFrameRange(no_frame_start, masm_->pc_offset());
  }
}


void LCodeGen::DoLoadGlobalCell(LLoadGlobalCell* instr) {
  Register result = ToRegister(instr->result());
  __ mov(ip, Operand(Handle<Object>(instr->hydrogen()->cell().handle())));
  __ LoadP(result, FieldMemOperand(ip, Cell::kValueOffset));
  if (instr->hydrogen()->RequiresHoleCheck()) {
    __ LoadRoot(ip, Heap::kTheHoleValueRootIndex);
    __ cmp(result, ip);
    DeoptimizeIf(eq, instr->environment());
  }
}


void LCodeGen::DoLoadGlobalGeneric(LLoadGlobalGeneric* instr) {
  ASSERT(ToRegister(instr->context()).is(cp));
  ASSERT(ToRegister(instr->global_object()).is(r3));
  ASSERT(ToRegister(instr->result()).is(r3));

  __ mov(r5, Operand(instr->name()));
  RelocInfo::Mode mode = instr->for_typeof() ? RelocInfo::CODE_TARGET
                                             : RelocInfo::CODE_TARGET_CONTEXT;
  Handle<Code> ic = isolate()->builtins()->LoadIC_Initialize();
  CallCode(ic, mode, instr);
}


void LCodeGen::DoStoreGlobalCell(LStoreGlobalCell* instr) {
  Register value = ToRegister(instr->value());
  Register cell = scratch0();

  // Load the cell.
  __ mov(cell, Operand(instr->hydrogen()->cell().handle()));

  // If the cell we are storing to contains the hole it could have
  // been deleted from the property dictionary. In that case, we need
  // to update the property details in the property dictionary to mark
  // it as no longer deleted.
  if (instr->hydrogen()->RequiresHoleCheck()) {
    // We use a temp to check the payload (CompareRoot might clobber ip).
    Register payload = ToRegister(instr->temp());
    __ LoadP(payload, FieldMemOperand(cell, Cell::kValueOffset));
    __ CompareRoot(payload, Heap::kTheHoleValueRootIndex);
    DeoptimizeIf(eq, instr->environment());
  }

  // Store the value.
  __ StoreP(value, FieldMemOperand(cell, Cell::kValueOffset), r0);
  // Cells are always rescanned, so no write barrier here.
}


void LCodeGen::DoStoreGlobalGeneric(LStoreGlobalGeneric* instr) {
  ASSERT(ToRegister(instr->context()).is(cp));
  ASSERT(ToRegister(instr->global_object()).is(r4));
  ASSERT(ToRegister(instr->value()).is(r3));

  __ mov(r5, Operand(instr->name()));
  Handle<Code> ic = (instr->strict_mode_flag() == kStrictMode)
      ? isolate()->builtins()->StoreIC_Initialize_Strict()
      : isolate()->builtins()->StoreIC_Initialize();
  CallCode(ic, RelocInfo::CODE_TARGET_CONTEXT, instr);
}


void LCodeGen::DoLoadContextSlot(LLoadContextSlot* instr) {
  Register context = ToRegister(instr->context());
  Register result = ToRegister(instr->result());
  __ LoadP(result, ContextOperand(context, instr->slot_index()));
  if (instr->hydrogen()->RequiresHoleCheck()) {
    __ LoadRoot(ip, Heap::kTheHoleValueRootIndex);
    __ cmp(result, ip);
    if (instr->hydrogen()->DeoptimizesOnHole()) {
      DeoptimizeIf(eq, instr->environment());
    } else {
      Label skip;
      __ bne(&skip);
      __ mov(result, Operand(factory()->undefined_value()));
      __ bind(&skip);
    }
  }
}


void LCodeGen::DoStoreContextSlot(LStoreContextSlot* instr) {
  Register context = ToRegister(instr->context());
  Register value = ToRegister(instr->value());
  Register scratch = scratch0();
  MemOperand target = ContextOperand(context, instr->slot_index());

  Label skip_assignment;

  if (instr->hydrogen()->RequiresHoleCheck()) {
    __ LoadP(scratch, target);
    __ LoadRoot(ip, Heap::kTheHoleValueRootIndex);
    __ cmp(scratch, ip);
    if (instr->hydrogen()->DeoptimizesOnHole()) {
      DeoptimizeIf(eq, instr->environment());
    } else {
      __ bne(&skip_assignment);
    }
  }

  __ StoreP(value, target, r0);
  if (instr->hydrogen()->NeedsWriteBarrier()) {
    SmiCheck check_needed =
        instr->hydrogen()->value()->IsHeapObject()
            ? OMIT_SMI_CHECK : INLINE_SMI_CHECK;
    __ RecordWriteContextSlot(context,
                              target.offset(),
                              value,
                              scratch,
                              GetLinkRegisterState(),
                              kSaveFPRegs,
                              EMIT_REMEMBERED_SET,
                              check_needed);
  }

  __ bind(&skip_assignment);
}


void LCodeGen::DoLoadNamedField(LLoadNamedField* instr) {
  HObjectAccess access = instr->hydrogen()->access();
  int offset = access.offset();
  Register object = ToRegister(instr->object());

  if (access.IsExternalMemory()) {
    Register result = ToRegister(instr->result());
    MemOperand operand = MemOperand(object, offset);
    __ LoadRepresentation(result, operand, access.representation());
    return;
  }

  if (instr->hydrogen()->representation().IsDouble()) {
    DoubleRegister result = ToDoubleRegister(instr->result());
    __ lfd(result, FieldMemOperand(object, offset));
    return;
  }

  Register result = ToRegister(instr->result());
  if (!access.IsInobject()) {
    __ LoadP(result, FieldMemOperand(object, JSObject::kPropertiesOffset));
    object = result;
  }
  MemOperand operand = FieldMemOperand(object, offset);
  __ LoadRepresentation(result, operand, access.representation());
}


void LCodeGen::DoLoadNamedGeneric(LLoadNamedGeneric* instr) {
  ASSERT(ToRegister(instr->context()).is(cp));
  ASSERT(ToRegister(instr->object()).is(r3));
  ASSERT(ToRegister(instr->result()).is(r3));

  // Name is always in r5.
  __ mov(r5, Operand(instr->name()));
  Handle<Code> ic = isolate()->builtins()->LoadIC_Initialize();
  CallCode(ic, RelocInfo::CODE_TARGET, instr);
}


void LCodeGen::DoLoadFunctionPrototype(LLoadFunctionPrototype* instr) {
  Register scratch = scratch0();
  Register function = ToRegister(instr->function());
  Register result = ToRegister(instr->result());

  // Check that the function really is a function. Load map into the
  // result register.
  __ CompareObjectType(function, result, scratch, JS_FUNCTION_TYPE);
  DeoptimizeIf(ne, instr->environment());

  // Make sure that the function has an instance prototype.
  Label non_instance;
  __ lbz(scratch, FieldMemOperand(result, Map::kBitFieldOffset));
  __ TestBit(scratch, Map::kHasNonInstancePrototype, r0);
  __ bne(&non_instance, cr0);

  // Get the prototype or initial map from the function.
  __ LoadP(result,
           FieldMemOperand(function, JSFunction::kPrototypeOrInitialMapOffset));

  // Check that the function has a prototype or an initial map.
  __ LoadRoot(ip, Heap::kTheHoleValueRootIndex);
  __ cmp(result, ip);
  DeoptimizeIf(eq, instr->environment());

  // If the function does not have an initial map, we're done.
  Label done;
  __ CompareObjectType(result, scratch, scratch, MAP_TYPE);
  __ bne(&done);

  // Get the prototype from the initial map.
  __ LoadP(result, FieldMemOperand(result, Map::kPrototypeOffset));
  __ b(&done);

  // Non-instance prototype: Fetch prototype from constructor field
  // in initial map.
  __ bind(&non_instance);
  __ LoadP(result, FieldMemOperand(result, Map::kConstructorOffset));

  // All done.
  __ bind(&done);
}


void LCodeGen::DoLoadRoot(LLoadRoot* instr) {
  Register result = ToRegister(instr->result());
  __ LoadRoot(result, instr->index());
}


void LCodeGen::DoLoadExternalArrayPointer(
    LLoadExternalArrayPointer* instr) {
  Register to_reg = ToRegister(instr->result());
  Register from_reg  = ToRegister(instr->object());
  __ LoadP(to_reg, FieldMemOperand(from_reg,
                                   ExternalArray::kExternalPointerOffset));
}


void LCodeGen::DoAccessArgumentsAt(LAccessArgumentsAt* instr) {
  Register arguments = ToRegister(instr->arguments());
  Register result = ToRegister(instr->result());
  if (instr->length()->IsConstantOperand() &&
      instr->index()->IsConstantOperand()) {
    int const_index = ToInteger32(LConstantOperand::cast(instr->index()));
    int const_length = ToInteger32(LConstantOperand::cast(instr->length()));
    int index = (const_length - const_index) + 1;
    __ LoadP(result, MemOperand(arguments, index * kPointerSize));
  } else {
    Register length = ToRegister(instr->length());
    Register index = ToRegister(instr->index());
    // There are two words between the frame pointer and the last argument.
    // Subtracting from length accounts for one of them add one more.
    __ sub(length, length, index);
    __ addi(length, length, Operand(1));
    __ ShiftLeftImm(r0, length, Operand(kPointerSizeLog2));
    __ LoadPX(result, MemOperand(arguments, r0));
  }
}


void LCodeGen::DoLoadKeyedExternalArray(LLoadKeyed* instr) {
  Register external_pointer = ToRegister(instr->elements());
  Register key = no_reg;
  ElementsKind elements_kind = instr->elements_kind();
  bool key_is_constant = instr->key()->IsConstantOperand();
  int constant_key = 0;
  if (key_is_constant) {
    constant_key = ToInteger32(LConstantOperand::cast(instr->key()));
    if (constant_key & 0xF0000000) {
      Abort(kArrayIndexConstantValueTooBig);
    }
  } else {
    key = ToRegister(instr->key());
  }
  int element_size_shift = ElementsKindToShiftSize(elements_kind);
  bool key_is_smi = instr->hydrogen()->key()->representation().IsSmi();
  int additional_offset = instr->additional_index() << element_size_shift;

  if (elements_kind == EXTERNAL_FLOAT_ELEMENTS ||
      elements_kind == EXTERNAL_DOUBLE_ELEMENTS) {
    DoubleRegister result = ToDoubleRegister(instr->result());
    if (key_is_constant) {
      __ Add(scratch0(), external_pointer,
             constant_key << element_size_shift,
             r0);
    } else {
      __ IndexToArrayOffset(r0, key, element_size_shift, key_is_smi);
      __ add(scratch0(), external_pointer, r0);
    }
    if (elements_kind == EXTERNAL_FLOAT_ELEMENTS) {
      __ lfs(result, MemOperand(scratch0(), additional_offset));
    } else  {  // i.e. elements_kind == EXTERNAL_DOUBLE_ELEMENTS
      __ lfd(result, MemOperand(scratch0(), additional_offset));
    }
  } else {
    Register result = ToRegister(instr->result());
    MemOperand mem_operand = PrepareKeyedOperand(
      key, external_pointer, key_is_constant, key_is_smi, constant_key,
      element_size_shift, instr->additional_index(), additional_offset);
    switch (elements_kind) {
      case EXTERNAL_BYTE_ELEMENTS:
        if (key_is_constant) {
          __ LoadByte(result, mem_operand, r0);
        } else {
          __ lbzx(result, mem_operand);
        }
        __ extsb(result, result);
        break;
      case EXTERNAL_PIXEL_ELEMENTS:
      case EXTERNAL_UNSIGNED_BYTE_ELEMENTS:
        if (key_is_constant) {
          __ LoadByte(result, mem_operand, r0);
        } else {
          __ lbzx(result, mem_operand);
        }
        break;
      case EXTERNAL_SHORT_ELEMENTS:
        if (key_is_constant) {
          __ LoadHalfWord(result, mem_operand, r0);
        } else {
          __ lhzx(result, mem_operand);
        }
        __ extsh(result, result);
        break;
      case EXTERNAL_UNSIGNED_SHORT_ELEMENTS:
        if (key_is_constant) {
          __ LoadHalfWord(result, mem_operand, r0);
        } else {
          __ lhzx(result, mem_operand);
        }
        break;
      case EXTERNAL_INT_ELEMENTS:
        if (key_is_constant) {
          __ LoadWord(result, mem_operand, r0);
        } else {
          __ lwzx(result, mem_operand);
        }
#if V8_TARGET_ARCH_PPC64
        __ extsw(result, result);
#endif
        break;
      case EXTERNAL_UNSIGNED_INT_ELEMENTS:
        if (key_is_constant) {
          __ LoadWord(result, mem_operand, r0);
        } else {
          __ lwzx(result, mem_operand);
        }
        if (!instr->hydrogen()->CheckFlag(HInstruction::kUint32)) {
          __ lis(r0, Operand(SIGN_EXT_IMM16(0x8000)));
          __ cmpl(result, r0);
          DeoptimizeIf(ge, instr->environment());
        }
        break;
      case EXTERNAL_FLOAT_ELEMENTS:
      case EXTERNAL_DOUBLE_ELEMENTS:
      case FAST_HOLEY_DOUBLE_ELEMENTS:
      case FAST_HOLEY_ELEMENTS:
      case FAST_HOLEY_SMI_ELEMENTS:
      case FAST_DOUBLE_ELEMENTS:
      case FAST_ELEMENTS:
      case FAST_SMI_ELEMENTS:
      case DICTIONARY_ELEMENTS:
      case NON_STRICT_ARGUMENTS_ELEMENTS:
        UNREACHABLE();
        break;
    }
  }
}


void LCodeGen::DoLoadKeyedFixedDoubleArray(LLoadKeyed* instr) {
  Register elements = ToRegister(instr->elements());
  bool key_is_constant = instr->key()->IsConstantOperand();
  Register key = no_reg;
  DoubleRegister result = ToDoubleRegister(instr->result());
  Register scratch = scratch0();

  int element_size_shift = ElementsKindToShiftSize(FAST_DOUBLE_ELEMENTS);
  bool key_is_smi = instr->hydrogen()->key()->representation().IsSmi();
  int constant_key = 0;
  if (key_is_constant) {
    constant_key = ToInteger32(LConstantOperand::cast(instr->key()));
    if (constant_key & 0xF0000000) {
      Abort(kArrayIndexConstantValueTooBig);
    }
  } else {
    key = ToRegister(instr->key());
  }

  int base_offset = (FixedDoubleArray::kHeaderSize - kHeapObjectTag) +
    ((constant_key + instr->additional_index()) << element_size_shift);
  if (!key_is_constant) {
    __ IndexToArrayOffset(r0, key, element_size_shift, key_is_smi);
    __ add(scratch, elements, r0);
    elements = scratch;
  }
  if (!is_int16(base_offset)) {
    __ Add(scratch, elements, base_offset, r0);
    base_offset = 0;
    elements = scratch;
  }
  __ lfd(result, MemOperand(elements, base_offset));

  if (instr->hydrogen()->RequiresHoleCheck()) {
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
    if (is_int16(base_offset + sizeof(kHoleNanLower32))) {
      __ lwz(scratch, MemOperand(elements,
                                 base_offset + sizeof(kHoleNanLower32)));
    } else {
      __ addi(scratch, elements, Operand(base_offset));
      __ lwz(scratch, MemOperand(scratch, sizeof(kHoleNanLower32)));
    }
#else
    __ lwz(scratch, MemOperand(elements, base_offset));
#endif
    __ Cmpi(scratch, Operand(kHoleNanUpper32), r0);
    DeoptimizeIf(eq, instr->environment());
  }
}


void LCodeGen::DoLoadKeyedFixedArray(LLoadKeyed* instr) {
  Register elements = ToRegister(instr->elements());
  Register result = ToRegister(instr->result());
  Register scratch = scratch0();
  Register store_base = scratch;
  int offset = 0;

  if (instr->key()->IsConstantOperand()) {
    LConstantOperand* const_operand = LConstantOperand::cast(instr->key());
    offset = FixedArray::OffsetOfElementAt(ToInteger32(const_operand) +
                                           instr->additional_index());
    store_base = elements;
  } else {
    Register key = ToRegister(instr->key());
    // Even though the HLoadKeyed instruction forces the input
    // representation for the key to be an integer, the input gets replaced
    // during bound check elimination with the index argument to the bounds
    // check, which can be tagged, so that case must be handled here, too.
    if (instr->hydrogen()->key()->representation().IsSmi()) {
      __ SmiToPtrArrayOffset(r0, key);
    } else {
      __ ShiftLeftImm(r0, key, Operand(kPointerSizeLog2));
    }
    __ add(scratch, elements, r0);
    offset = FixedArray::OffsetOfElementAt(instr->additional_index());
  }
  __ LoadP(result, FieldMemOperand(store_base, offset));

  // Check for the hole value.
  if (instr->hydrogen()->RequiresHoleCheck()) {
    if (IsFastSmiElementsKind(instr->hydrogen()->elements_kind())) {
      __ TestIfSmi(result, r0);
      DeoptimizeIf(ne, instr->environment(), cr0);
    } else {
      __ LoadRoot(scratch, Heap::kTheHoleValueRootIndex);
      __ cmp(result, scratch);
      DeoptimizeIf(eq, instr->environment());
    }
  }
}


void LCodeGen::DoLoadKeyed(LLoadKeyed* instr) {
  if (instr->is_external()) {
    DoLoadKeyedExternalArray(instr);
  } else if (instr->hydrogen()->representation().IsDouble()) {
    DoLoadKeyedFixedDoubleArray(instr);
  } else {
    DoLoadKeyedFixedArray(instr);
  }
}


MemOperand LCodeGen::PrepareKeyedOperand(Register key,
                                         Register base,
                                         bool key_is_constant,
                                         bool key_is_smi,
                                         int constant_key,
                                         int element_size_shift,
                                         int additional_index,
                                         int additional_offset) {
  Register scratch = scratch0();

  if (key_is_constant) {
    // key_is_smi and additional_index are irrelevant in this case
    return MemOperand(base,
                      (constant_key << element_size_shift) + additional_offset);
  }

  bool needs_shift = (element_size_shift != (key_is_smi ?
                                             kSmiTagSize + kSmiShiftSize : 0));

  if (!(additional_index || needs_shift)) {
      return MemOperand(base, key);
  }

  if (additional_index) {
    if (key_is_smi) {
#if V8_TARGET_ARCH_PPC64
      // more efficient to just untag
      __ SmiUntag(scratch, key);
      key_is_smi = false;
      needs_shift = (element_size_shift != 0);
      key = scratch;
#else
      additional_index <<= kSmiTagSize + kSmiShiftSize;
#endif
    }

    __ Add(scratch, key, additional_index, r0);
    key = scratch;
  }

  if (needs_shift) {
    __ IndexToArrayOffset(scratch, key, element_size_shift, key_is_smi);
  }

  return MemOperand(base, scratch);
}


void LCodeGen::DoLoadKeyedGeneric(LLoadKeyedGeneric* instr) {
  ASSERT(ToRegister(instr->context()).is(cp));
  ASSERT(ToRegister(instr->object()).is(r4));
  ASSERT(ToRegister(instr->key()).is(r3));

  Handle<Code> ic = isolate()->builtins()->KeyedLoadIC_Initialize();
  CallCode(ic, RelocInfo::CODE_TARGET, instr);
}


void LCodeGen::DoArgumentsElements(LArgumentsElements* instr) {
  Register scratch = scratch0();
  Register result = ToRegister(instr->result());

  if (instr->hydrogen()->from_inlined()) {
    __ subi(result, sp, Operand(2 * kPointerSize));
  } else {
    // Check if the calling frame is an arguments adaptor frame.
    Label done, adapted;
    __ LoadP(scratch, MemOperand(fp, StandardFrameConstants::kCallerFPOffset));
    __ LoadP(result,
             MemOperand(scratch, StandardFrameConstants::kContextOffset));
    __ CmpSmiLiteral(result, Smi::FromInt(StackFrame::ARGUMENTS_ADAPTOR), r0);

    // Result is the frame pointer for the frame if not adapted and for the real
    // frame below the adaptor frame if adapted.
    __ beq(&adapted);
    __ mr(result, fp);
    __ b(&done);

    __ bind(&adapted);
    __ mr(result, scratch);
    __ bind(&done);
  }
}


void LCodeGen::DoArgumentsLength(LArgumentsLength* instr) {
  Register elem = ToRegister(instr->elements());
  Register result = ToRegister(instr->result());

  Label done;

  // If no arguments adaptor frame the number of arguments is fixed.
  __ cmp(fp, elem);
  __ mov(result, Operand(scope()->num_parameters()));
  __ beq(&done);

  // Arguments adaptor frame present. Get argument length from there.
  __ LoadP(result, MemOperand(fp, StandardFrameConstants::kCallerFPOffset));
  __ LoadP(result,
           MemOperand(result, ArgumentsAdaptorFrameConstants::kLengthOffset));
  __ SmiUntag(result);

  // Argument length is in result register.
  __ bind(&done);
}


void LCodeGen::DoWrapReceiver(LWrapReceiver* instr) {
  Register receiver = ToRegister(instr->receiver());
  Register function = ToRegister(instr->function());
  Register scratch = scratch0();

  // If the receiver is null or undefined, we have to pass the global
  // object as a receiver to normal functions. Values have to be
  // passed unchanged to builtins and strict-mode functions.
  Label global_object, receiver_ok;

  // Do not transform the receiver to object for strict mode
  // functions.
  __ LoadP(scratch,
           FieldMemOperand(function, JSFunction::kSharedFunctionInfoOffset));
  __ lwz(scratch,
         FieldMemOperand(scratch, SharedFunctionInfo::kCompilerHintsOffset));
  __ TestBit(scratch,
#if V8_TARGET_ARCH_PPC64
             SharedFunctionInfo::kStrictModeFunction,
#else
             SharedFunctionInfo::kStrictModeFunction + kSmiTagSize,
#endif
             r0);
  __ bne(&receiver_ok, cr0);

  // Do not transform the receiver to object for builtins.
  __ TestBit(scratch,
#if V8_TARGET_ARCH_PPC64
             SharedFunctionInfo::kNative,
#else
             SharedFunctionInfo::kNative + kSmiTagSize,
#endif
             r0);
  __ bne(&receiver_ok, cr0);

  // Normal function. Replace undefined or null with global receiver.
  __ LoadRoot(scratch, Heap::kNullValueRootIndex);
  __ cmp(receiver, scratch);
  __ beq(&global_object);
  __ LoadRoot(scratch, Heap::kUndefinedValueRootIndex);
  __ cmp(receiver, scratch);
  __ beq(&global_object);

  // Deoptimize if the receiver is not a JS object.
  __ TestIfSmi(receiver, r0);
  DeoptimizeIf(eq, instr->environment(), cr0);
  __ CompareObjectType(receiver, scratch, scratch, FIRST_SPEC_OBJECT_TYPE);
  DeoptimizeIf(lt, instr->environment());
  __ b(&receiver_ok);

  __ bind(&global_object);
  __ LoadP(receiver, GlobalObjectOperand());
  __ LoadP(receiver,
           FieldMemOperand(receiver, JSGlobalObject::kGlobalReceiverOffset));
  __ bind(&receiver_ok);
}


void LCodeGen::DoApplyArguments(LApplyArguments* instr) {
  Register receiver = ToRegister(instr->receiver());
  Register function = ToRegister(instr->function());
  Register length = ToRegister(instr->length());
  Register elements = ToRegister(instr->elements());
  Register scratch = scratch0();
  ASSERT(receiver.is(r3));  // Used for parameter count.
  ASSERT(function.is(r4));  // Required by InvokeFunction.
  ASSERT(ToRegister(instr->result()).is(r3));

  // Copy the arguments to this function possibly from the
  // adaptor frame below it.
  const uint32_t kArgumentsLimit = 1 * KB;
  __ cmpli(length, Operand(kArgumentsLimit));
  DeoptimizeIf(gt, instr->environment());

  // Push the receiver and use the register to keep the original
  // number of arguments.
  __ push(receiver);
  __ mr(receiver, length);
  // The arguments are at a one pointer size offset from elements.
  __ addi(elements, elements, Operand(1 * kPointerSize));

  // Loop through the arguments pushing them onto the execution
  // stack.
  Label invoke, loop;
  // length is a small non-negative integer, due to the test above.
  __ cmpi(length, Operand::Zero());
  __ beq(&invoke);
  __ mtctr(length);
  __ bind(&loop);
  __ ShiftLeftImm(r0, length, Operand(kPointerSizeLog2));
  __ LoadPX(scratch, MemOperand(elements, r0));
  __ push(scratch);
  __ addi(length, length, Operand(-1));
  __ bdnz(&loop);

  __ bind(&invoke);
  ASSERT(instr->HasPointerMap());
  LPointerMap* pointers = instr->pointer_map();
  SafepointGenerator safepoint_generator(
      this, pointers, Safepoint::kLazyDeopt);
  // The number of arguments is stored in receiver which is r3, as expected
  // by InvokeFunction.
  ParameterCount actual(receiver);
  __ InvokeFunction(function, actual, CALL_FUNCTION,
                    safepoint_generator, CALL_AS_METHOD);
}


void LCodeGen::DoPushArgument(LPushArgument* instr) {
  LOperand* argument = instr->value();
  if (argument->IsDoubleRegister() || argument->IsDoubleStackSlot()) {
    Abort(kDoPushArgumentNotImplementedForDoubleType);
  } else {
    Register argument_reg = EmitLoadRegister(argument, ip);
    __ push(argument_reg);
  }
}


void LCodeGen::DoDrop(LDrop* instr) {
  __ Drop(instr->count());
}


void LCodeGen::DoThisFunction(LThisFunction* instr) {
  Register result = ToRegister(instr->result());
  __ LoadP(result, MemOperand(fp, JavaScriptFrameConstants::kFunctionOffset));
}


void LCodeGen::DoContext(LContext* instr) {
  // If there is a non-return use, the context must be moved to a register.
  Register result = ToRegister(instr->result());
  if (info()->IsOptimizing()) {
    __ LoadP(result, MemOperand(fp, StandardFrameConstants::kContextOffset));
  } else {
    // If there is no frame, the context must be in cp.
    ASSERT(result.is(cp));
  }
}


void LCodeGen::DoOuterContext(LOuterContext* instr) {
  Register context = ToRegister(instr->context());
  Register result = ToRegister(instr->result());
  __ LoadP(result,
           MemOperand(context, Context::SlotOffset(Context::PREVIOUS_INDEX)));
}


void LCodeGen::DoDeclareGlobals(LDeclareGlobals* instr) {
  ASSERT(ToRegister(instr->context()).is(cp));
  __ push(cp);  // The context is the first argument.
  __ Move(scratch0(), instr->hydrogen()->pairs());
  __ push(scratch0());
  __ LoadSmiLiteral(scratch0(), Smi::FromInt(instr->hydrogen()->flags()));
  __ push(scratch0());
  CallRuntime(Runtime::kDeclareGlobals, 3, instr);
}


void LCodeGen::DoGlobalObject(LGlobalObject* instr) {
  Register context = ToRegister(instr->context());
  Register result = ToRegister(instr->result());
  __ LoadP(result, ContextOperand(context, Context::GLOBAL_OBJECT_INDEX));
}


void LCodeGen::DoGlobalReceiver(LGlobalReceiver* instr) {
  Register global = ToRegister(instr->global_object());
  Register result = ToRegister(instr->result());
  __ LoadP(result,
           FieldMemOperand(global, GlobalObject::kGlobalReceiverOffset));
}


void LCodeGen::CallKnownFunction(Handle<JSFunction> function,
                                 int formal_parameter_count,
                                 int arity,
                                 LInstruction* instr,
                                 CallKind call_kind,
                                 R4State r4_state) {
  bool dont_adapt_arguments =
      formal_parameter_count == SharedFunctionInfo::kDontAdaptArgumentsSentinel;
  bool can_invoke_directly =
      dont_adapt_arguments || formal_parameter_count == arity;

  LPointerMap* pointers = instr->pointer_map();

  if (can_invoke_directly) {
    if (r4_state == R4_UNINITIALIZED) {
      __ Move(r4, function);
    }

    // Change context.
    __ LoadP(cp, FieldMemOperand(r4, JSFunction::kContextOffset));

    // Set r3 to arguments count if adaption is not needed. Assumes that r3
    // is available to write to at this point.
    if (dont_adapt_arguments) {
      __ mov(r3, Operand(arity));
    }

    // Invoke function.
    __ SetCallKind(r8, call_kind);
    if (function.is_identical_to(info()->closure())) {
      __ CallSelf();
    } else {
      __ LoadP(ip, FieldMemOperand(r4, JSFunction::kCodeEntryOffset));
      __ Call(ip);
    }

    // Set up deoptimization.
    RecordSafepointWithLazyDeopt(instr, RECORD_SIMPLE_SAFEPOINT);
  } else {
    SafepointGenerator generator(this, pointers, Safepoint::kLazyDeopt);
    ParameterCount count(arity);
    ParameterCount expected(formal_parameter_count);
    __ InvokeFunction(
        function, expected, count, CALL_FUNCTION, generator, call_kind);
  }
}


void LCodeGen::DoCallConstantFunction(LCallConstantFunction* instr) {
  ASSERT(ToRegister(instr->result()).is(r3));
  CallKnownFunction(instr->hydrogen()->function(),
                    instr->hydrogen()->formal_parameter_count(),
                    instr->arity(),
                    instr,
                    CALL_AS_METHOD,
                    R4_UNINITIALIZED);
}


void LCodeGen::DoDeferredMathAbsTaggedHeapNumber(LMathAbs* instr) {
  ASSERT(instr->context() != NULL);
  ASSERT(ToRegister(instr->context()).is(cp));
  Register input = ToRegister(instr->value());
  Register result = ToRegister(instr->result());
  Register scratch = scratch0();

  // Deoptimize if not a heap number.
  __ LoadP(scratch, FieldMemOperand(input, HeapObject::kMapOffset));
  __ LoadRoot(ip, Heap::kHeapNumberMapRootIndex);
  __ cmp(scratch, ip);
  DeoptimizeIf(ne, instr->environment());

  Label done;
  Register exponent = scratch0();
  scratch = no_reg;
  __ lwz(exponent, FieldMemOperand(input, HeapNumber::kExponentOffset));
  // Check the sign of the argument. If the argument is positive, just
  // return it.
  __ TestSignBit32(exponent, r0);
  // Move the input to the result if necessary.
  __ Move(result, input);
  __ beq(&done, cr0);

  // Input is negative. Reverse its sign.
  // Preserve the value of all registers.
  {
    PushSafepointRegistersScope scope(this, Safepoint::kWithRegisters);

    // Registers were saved at the safepoint, so we can use
    // many scratch registers.
    Register tmp1 = input.is(r4) ? r3 : r4;
    Register tmp2 = input.is(r5) ? r3 : r5;
    Register tmp3 = input.is(r6) ? r3 : r6;
    Register tmp4 = input.is(r7) ? r3 : r7;

    // exponent: floating point exponent value.

    Label allocated, slow;
    __ LoadRoot(tmp4, Heap::kHeapNumberMapRootIndex);
    __ AllocateHeapNumber(tmp1, tmp2, tmp3, tmp4, &slow);
    __ b(&allocated);

    // Slow case: Call the runtime system to do the number allocation.
    __ bind(&slow);

    CallRuntimeFromDeferred(Runtime::kAllocateHeapNumber, 0, instr,
                            instr->context());
    // Set the pointer to the new heap number in tmp.
    if (!tmp1.is(r3)) __ mr(tmp1, r3);
    // Restore input_reg after call to runtime.
    __ LoadFromSafepointRegisterSlot(input, input);
    __ lwz(exponent, FieldMemOperand(input, HeapNumber::kExponentOffset));

    __ bind(&allocated);
    // exponent: floating point exponent value.
    // tmp1: allocated heap number.
    STATIC_ASSERT(HeapNumber::kSignMask == 0x80000000u);
    __ clrlwi(exponent, exponent, Operand(1));  // clear sign bit
    __ stw(exponent, FieldMemOperand(tmp1, HeapNumber::kExponentOffset));
    __ lwz(tmp2, FieldMemOperand(input, HeapNumber::kMantissaOffset));
    __ stw(tmp2, FieldMemOperand(tmp1, HeapNumber::kMantissaOffset));

    __ StoreToSafepointRegisterSlot(tmp1, result);
  }

  __ bind(&done);
}


void LCodeGen::EmitIntegerMathAbs(LMathAbs* instr) {
  Register input = ToRegister(instr->value());
  Register result = ToRegister(instr->result());
  Label done;
  __ cmpi(input, Operand::Zero());
  __ Move(result, input);
  __ bge(&done);
  __ li(r0, Operand::Zero());  // clear xer
  __ mtxer(r0);
  __ neg(result, result, SetOE, SetRC);
  // Deoptimize on overflow.
  DeoptimizeIf(overflow, instr->environment(), cr0);
  __ bind(&done);
}


void LCodeGen::DoMathAbs(LMathAbs* instr) {
  // Class for deferred case.
  class DeferredMathAbsTaggedHeapNumber V8_FINAL : public LDeferredCode {
   public:
    DeferredMathAbsTaggedHeapNumber(LCodeGen* codegen, LMathAbs* instr)
        : LDeferredCode(codegen), instr_(instr) { }
    virtual void Generate() V8_OVERRIDE {
      codegen()->DoDeferredMathAbsTaggedHeapNumber(instr_);
    }
    virtual LInstruction* instr() V8_OVERRIDE { return instr_; }
   private:
    LMathAbs* instr_;
  };

  Representation r = instr->hydrogen()->value()->representation();
  if (r.IsDouble()) {
    DoubleRegister input = ToDoubleRegister(instr->value());
    DoubleRegister result = ToDoubleRegister(instr->result());
    __ fabs(result, input);
  } else if (r.IsSmiOrInteger32()) {
    EmitIntegerMathAbs(instr);
  } else {
    // Representation is tagged.
    DeferredMathAbsTaggedHeapNumber* deferred =
        new(zone()) DeferredMathAbsTaggedHeapNumber(this, instr);
    Register input = ToRegister(instr->value());
    // Smi check.
    __ JumpIfNotSmi(input, deferred->entry());
    // If smi, handle it directly.
    EmitIntegerMathAbs(instr);
    __ bind(deferred->exit());
  }
}


void LCodeGen::DoMathFloor(LMathFloor* instr) {
  DoubleRegister input = ToDoubleRegister(instr->value());
  Register result = ToRegister(instr->result());
  Register input_high = scratch0();
  Register scratch = ip;
  Label done, exact;

  __ TryInt32Floor(result, input, input_high, scratch, double_scratch0(),
                   &done, &exact);
  DeoptimizeIf(al, instr->environment());

  __ bind(&exact);
  if (instr->hydrogen()->CheckFlag(HValue::kBailoutOnMinusZero)) {
    // Test for -0.
    __ cmpi(result, Operand::Zero());
    __ bne(&done);
    __ TestSignBit32(input_high, r0);
    DeoptimizeIf(ne, instr->environment(), cr0);
  }
  __ bind(&done);
}


void LCodeGen::DoMathRound(LMathRound* instr) {
  DoubleRegister input = ToDoubleRegister(instr->value());
  Register result = ToRegister(instr->result());
  DoubleRegister double_scratch1 = ToDoubleRegister(instr->temp());
  DoubleRegister input_plus_dot_five = double_scratch1;
  Register input_high = scratch0();
  Register scratch = ip;
  DoubleRegister dot_five = double_scratch0();
  Label convert, done;

  __ LoadDoubleLiteral(dot_five, 0.5, r0);
  __ fabs(double_scratch1, input);
  __ fcmpu(double_scratch1, dot_five);
  DeoptimizeIf(unordered, instr->environment());
  // If input is in [-0.5, -0], the result is -0.
  // If input is in [+0, +0.5[, the result is +0.
  // If the input is +0.5, the result is 1.
  __ bgt(&convert);  // Out of [-0.5, +0.5].
  if (instr->hydrogen()->CheckFlag(HValue::kBailoutOnMinusZero)) {
    __ stfdu(input, MemOperand(sp, -kDoubleSize));
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
    __ lwz(input_high, MemOperand(sp, 4));
#else
    __ lwz(input_high, MemOperand(sp, 0));
#endif
    __ addi(sp, sp, Operand(kDoubleSize));
    __ TestSignBit32(input_high, r0);
    DeoptimizeIf(ne, instr->environment(), cr0);  // [-0.5, -0].
  }
  Label return_zero;
  __ fcmpu(input, dot_five);
  __ bne(&return_zero);
  __ li(result, Operand(1));  // +0.5.
  __ b(&done);
  // Remaining cases: [+0, +0.5[ or [-0.5, +0.5[, depending on
  // flag kBailoutOnMinusZero.
  __ bind(&return_zero);
  __ li(result, Operand::Zero());
  __ b(&done);

  __ bind(&convert);
  __ fadd(input_plus_dot_five, input, dot_five);
  // Reuse dot_five (double_scratch0) as we no longer need this value.
  __ TryInt32Floor(result, input_plus_dot_five, input_high,
                   scratch, double_scratch0(),
                   &done, &done);
  DeoptimizeIf(al, instr->environment());
  __ bind(&done);
}


void LCodeGen::DoMathSqrt(LMathSqrt* instr) {
  DoubleRegister input = ToDoubleRegister(instr->value());
  DoubleRegister result = ToDoubleRegister(instr->result());
  __ fsqrt(result, input);
}


void LCodeGen::DoMathPowHalf(LMathPowHalf* instr) {
  DoubleRegister input = ToDoubleRegister(instr->value());
  DoubleRegister result = ToDoubleRegister(instr->result());
  DoubleRegister temp = ToDoubleRegister(instr->temp());

  // Note that according to ECMA-262 15.8.2.13:
  // Math.pow(-Infinity, 0.5) == Infinity
  // Math.sqrt(-Infinity) == NaN
  Label skip, done;

  __ LoadDoubleLiteral(temp, -V8_INFINITY, scratch0());
  __ fcmpu(input, temp);
  __ bne(&skip);
  __ fneg(result, temp);
  __ b(&done);

  // Add +0 to convert -0 to +0.
  __ bind(&skip);
  __ fadd(result, input, kDoubleRegZero);
  __ fsqrt(result, result);
  __ bind(&done);
}


void LCodeGen::DoPower(LPower* instr) {
  Representation exponent_type = instr->hydrogen()->right()->representation();
  // Having marked this as a call, we can use any registers.
  // Just make sure that the input/output registers are the expected ones.
  ASSERT(!instr->right()->IsDoubleRegister() ||
         ToDoubleRegister(instr->right()).is(d2));
  ASSERT(!instr->right()->IsRegister() ||
         ToRegister(instr->right()).is(r5));
  ASSERT(ToDoubleRegister(instr->left()).is(d1));
  ASSERT(ToDoubleRegister(instr->result()).is(d3));

  if (exponent_type.IsSmi()) {
    MathPowStub stub(MathPowStub::TAGGED);
    __ CallStub(&stub);
  } else if (exponent_type.IsTagged()) {
    Label no_deopt;
    __ JumpIfSmi(r5, &no_deopt);
    __ LoadP(r10, FieldMemOperand(r5, HeapObject::kMapOffset));
    __ LoadRoot(ip, Heap::kHeapNumberMapRootIndex);
    __ cmp(r10, ip);
    DeoptimizeIf(ne, instr->environment());
    __ bind(&no_deopt);
    MathPowStub stub(MathPowStub::TAGGED);
    __ CallStub(&stub);
  } else if (exponent_type.IsInteger32()) {
    MathPowStub stub(MathPowStub::INTEGER);
    __ CallStub(&stub);
  } else {
    ASSERT(exponent_type.IsDouble());
    MathPowStub stub(MathPowStub::DOUBLE);
    __ CallStub(&stub);
  }
}


void LCodeGen::DoRandom(LRandom* instr) {
  // Assert that the register size is indeed the size of each seed.
  static const int kSeedSize = sizeof(uint32_t);
#ifdef V8_TARGET_ARCH_PPC64
  STATIC_ASSERT(kPointerSize == 2 * kSeedSize);
#else
  STATIC_ASSERT(kPointerSize == kSeedSize);
#endif

  // Load native context
  Register global_object = ToRegister(instr->global_object());
  Register native_context = global_object;
  __ LoadP(native_context,
           FieldMemOperand(global_object, GlobalObject::kNativeContextOffset));

  // Load state (FixedArray of the native context's random seeds)
  static const int kRandomSeedOffset =
      FixedArray::kHeaderSize + Context::RANDOM_SEED_INDEX * kPointerSize;
  Register state = native_context;
  __ LoadP(state, FieldMemOperand(native_context, kRandomSeedOffset));

  // Load state[0].
  Register state0 = ToRegister(instr->scratch());
  __ lwz(state0, FieldMemOperand(state, ByteArray::kHeaderSize));
  // Load state[1].
  Register state1 = ToRegister(instr->scratch2());
  __ lwz(state1, FieldMemOperand(state, ByteArray::kHeaderSize + kSeedSize));

  // state[0] = 18273 * (state[0] & 0xFFFF) + (state[0] >> 16)
  Register scratch3 = ToRegister(instr->scratch3());
  Register scratch4 = scratch0();
  __ andi(scratch3, state0, Operand(0xFFFF));
  __ li(scratch4, Operand(18273));
  __ Mul(scratch3, scratch3, scratch4);
  __ srwi(state0, state0, Operand(16));
  __ add(state0, scratch3, state0);
  // Save state[0].
  __ stw(state0, FieldMemOperand(state, ByteArray::kHeaderSize));

  // state[1] = 36969 * (state[1] & 0xFFFF) + (state[1] >> 16)
  __ andi(scratch3, state1, Operand(0xFFFF));
  __ mov(scratch4, Operand(36969));
  __ Mul(scratch3, scratch3, scratch4);
  __ srwi(state1, state1, Operand(16));
  __ add(state1, scratch3, state1);
  // Save state[1].
  __ stw(state1, FieldMemOperand(state, ByteArray::kHeaderSize + kSeedSize));

  // Random bit pattern = (state[0] << 14) + (state[1] & 0x3FFFF)
  Register random = scratch4;
  __ ExtractBitMask(random, state1, 0x3FFFF);
  __ slwi(r0, state0, Operand(14));
  __ add(random, random, r0);

  // Allocate temp stack space to for double
  __ addi(sp, sp, Operand(-8));

  // 0x41300000 is the top half of 1.0 x 2^20 as a double.
  __ lis(scratch3, Operand(0x4130));

  // Move 0x41300000xxxxxxxx (x = random bits) to double register.
  DoubleRegister result = ToDoubleRegister(instr->result());
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
  __ stw(random, MemOperand(sp, 0));
  __ stw(scratch3, MemOperand(sp, 4));
#else
  __ stw(scratch3, MemOperand(sp, 0));
  __ stw(random, MemOperand(sp, 4));
#endif
  __ lfd(result, MemOperand(sp, 0));

  // Move 0x4130000000000000 to double register.
  DoubleRegister scratch5 = double_scratch0();
  __ li(scratch4, Operand::Zero());
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
  __ stw(scratch4, MemOperand(sp, 0));
  __ stw(scratch3, MemOperand(sp, 4));
#else
  __ stw(scratch3, MemOperand(sp, 0));
  __ stw(scratch4, MemOperand(sp, 4));
#endif
  __ lfd(scratch5, MemOperand(sp, 0));

  __ addi(sp, sp, Operand(8));

  __ fsub(result, result, scratch5);
}


void LCodeGen::DoMathExp(LMathExp* instr) {
  DoubleRegister input = ToDoubleRegister(instr->value());
  DoubleRegister result = ToDoubleRegister(instr->result());
  DoubleRegister double_scratch1 = ToDoubleRegister(instr->double_temp());
  DoubleRegister double_scratch2 = double_scratch0();
  Register temp1 = ToRegister(instr->temp1());
  Register temp2 = ToRegister(instr->temp2());

  MathExpGenerator::EmitMathExp(
      masm(), input, result, double_scratch1, double_scratch2,
      temp1, temp2, scratch0());
}


void LCodeGen::DoMathLog(LMathLog* instr) {
  ASSERT(ToDoubleRegister(instr->result()).is(d2));
  // Set the context register to a GC-safe fake value. Clobbering it is
  // OK because this instruction is marked as a call.
  __ li(cp, Operand::Zero());
  TranscendentalCacheStub stub(TranscendentalCache::LOG,
                               TranscendentalCacheStub::UNTAGGED);
  CallCode(stub.GetCode(isolate()), RelocInfo::CODE_TARGET, instr);
}


void LCodeGen::DoMathTan(LMathTan* instr) {
  ASSERT(ToDoubleRegister(instr->result()).is(d2));
  // Set the context register to a GC-safe fake value. Clobbering it is
  // OK because this instruction is marked as a call.
  __ li(cp, Operand::Zero());
  TranscendentalCacheStub stub(TranscendentalCache::TAN,
                               TranscendentalCacheStub::UNTAGGED);
  CallCode(stub.GetCode(isolate()), RelocInfo::CODE_TARGET, instr);
}


void LCodeGen::DoMathCos(LMathCos* instr) {
  ASSERT(ToDoubleRegister(instr->result()).is(d2));
  // Set the context register to a GC-safe fake value. Clobbering it is
  // OK because this instruction is marked as a call.
  __ li(cp, Operand::Zero());
  TranscendentalCacheStub stub(TranscendentalCache::COS,
                               TranscendentalCacheStub::UNTAGGED);
  CallCode(stub.GetCode(isolate()), RelocInfo::CODE_TARGET, instr);
}


void LCodeGen::DoMathSin(LMathSin* instr) {
  ASSERT(ToDoubleRegister(instr->result()).is(d2));
  // Set the context register to a GC-safe fake value. Clobbering it is
  // OK because this instruction is marked as a call.
  __ li(cp, Operand::Zero());
  TranscendentalCacheStub stub(TranscendentalCache::SIN,
                               TranscendentalCacheStub::UNTAGGED);
  CallCode(stub.GetCode(isolate()), RelocInfo::CODE_TARGET, instr);
}


void LCodeGen::DoInvokeFunction(LInvokeFunction* instr) {
  ASSERT(ToRegister(instr->context()).is(cp));
  ASSERT(ToRegister(instr->function()).is(r4));
  ASSERT(instr->HasPointerMap());

  Handle<JSFunction> known_function = instr->hydrogen()->known_function();
  if (known_function.is_null()) {
    LPointerMap* pointers = instr->pointer_map();
    SafepointGenerator generator(this, pointers, Safepoint::kLazyDeopt);
    ParameterCount count(instr->arity());
    __ InvokeFunction(r4, count, CALL_FUNCTION, generator, CALL_AS_METHOD);
  } else {
    CallKnownFunction(known_function,
                      instr->hydrogen()->formal_parameter_count(),
                      instr->arity(),
                      instr,
                      CALL_AS_METHOD,
                      R4_CONTAINS_TARGET);
  }
}


void LCodeGen::DoCallKeyed(LCallKeyed* instr) {
  ASSERT(ToRegister(instr->context()).is(cp));
  ASSERT(ToRegister(instr->result()).is(r3));

  int arity = instr->arity();
  Handle<Code> ic =
      isolate()->stub_cache()->ComputeKeyedCallInitialize(arity);
  CallCode(ic, RelocInfo::CODE_TARGET, instr);
}


void LCodeGen::DoCallNamed(LCallNamed* instr) {
  ASSERT(ToRegister(instr->context()).is(cp));
  ASSERT(ToRegister(instr->result()).is(r3));

  int arity = instr->arity();
  RelocInfo::Mode mode = RelocInfo::CODE_TARGET;
  Handle<Code> ic =
      isolate()->stub_cache()->ComputeCallInitialize(arity, mode);
  __ mov(r5, Operand(instr->name()));
  CallCode(ic, mode, instr);
}


void LCodeGen::DoCallFunction(LCallFunction* instr) {
  ASSERT(ToRegister(instr->context()).is(cp));
  ASSERT(ToRegister(instr->function()).is(r4));
  ASSERT(ToRegister(instr->result()).is(r3));

  int arity = instr->arity();
  CallFunctionStub stub(arity, NO_CALL_FUNCTION_FLAGS);
  CallCode(stub.GetCode(isolate()), RelocInfo::CODE_TARGET, instr);
}


void LCodeGen::DoCallGlobal(LCallGlobal* instr) {
  ASSERT(ToRegister(instr->context()).is(cp));
  ASSERT(ToRegister(instr->result()).is(r3));

  int arity = instr->arity();
  RelocInfo::Mode mode = RelocInfo::CODE_TARGET_CONTEXT;
  Handle<Code> ic =
      isolate()->stub_cache()->ComputeCallInitialize(arity, mode);
  __ mov(r5, Operand(instr->name()));
  CallCode(ic, mode, instr);
}


void LCodeGen::DoCallKnownGlobal(LCallKnownGlobal* instr) {
  ASSERT(ToRegister(instr->result()).is(r3));
  CallKnownFunction(instr->hydrogen()->target(),
                    instr->hydrogen()->formal_parameter_count(),
                    instr->arity(),
                    instr,
                    CALL_AS_FUNCTION,
                    R4_UNINITIALIZED);
}


void LCodeGen::DoCallNew(LCallNew* instr) {
  ASSERT(ToRegister(instr->context()).is(cp));
  ASSERT(ToRegister(instr->constructor()).is(r4));
  ASSERT(ToRegister(instr->result()).is(r3));

  __ mov(r3, Operand(instr->arity()));
  // No cell in r5 for construct type feedback in optimized code
  Handle<Object> undefined_value(isolate()->factory()->undefined_value());
  __ mov(r5, Operand(undefined_value));
  CallConstructStub stub(NO_CALL_FUNCTION_FLAGS);
  CallCode(stub.GetCode(isolate()), RelocInfo::CONSTRUCT_CALL, instr);
}


void LCodeGen::DoCallNewArray(LCallNewArray* instr) {
  ASSERT(ToRegister(instr->context()).is(cp));
  ASSERT(ToRegister(instr->constructor()).is(r4));
  ASSERT(ToRegister(instr->result()).is(r3));

  __ mov(r3, Operand(instr->arity()));
  __ mov(r5, Operand(instr->hydrogen()->property_cell()));
  ElementsKind kind = instr->hydrogen()->elements_kind();
  AllocationSiteOverrideMode override_mode =
      (AllocationSite::GetMode(kind) == TRACK_ALLOCATION_SITE)
          ? DISABLE_ALLOCATION_SITES
          : DONT_OVERRIDE;
  ContextCheckMode context_mode = CONTEXT_CHECK_NOT_REQUIRED;

  if (instr->arity() == 0) {
    ArrayNoArgumentConstructorStub stub(kind, context_mode, override_mode);
    CallCode(stub.GetCode(isolate()), RelocInfo::CONSTRUCT_CALL, instr);
  } else if (instr->arity() == 1) {
    Label done;
    if (IsFastPackedElementsKind(kind)) {
      Label packed_case;
      // We might need a change here
      // look at the first argument
      __ LoadP(r8, MemOperand(sp, 0));
      __ cmpi(r8, Operand::Zero());
      __ beq(&packed_case);

      ElementsKind holey_kind = GetHoleyElementsKind(kind);
      ArraySingleArgumentConstructorStub stub(holey_kind, context_mode,
                                              override_mode);
      CallCode(stub.GetCode(isolate()), RelocInfo::CONSTRUCT_CALL, instr);
      __ b(&done);
      __ bind(&packed_case);
    }

    ArraySingleArgumentConstructorStub stub(kind, context_mode, override_mode);
    CallCode(stub.GetCode(isolate()), RelocInfo::CONSTRUCT_CALL, instr);
    __ bind(&done);
  } else {
    ArrayNArgumentsConstructorStub stub(kind, context_mode, override_mode);
    CallCode(stub.GetCode(isolate()), RelocInfo::CONSTRUCT_CALL, instr);
  }
}


void LCodeGen::DoCallRuntime(LCallRuntime* instr) {
  CallRuntime(instr->function(), instr->arity(), instr);
}


void LCodeGen::DoStoreCodeEntry(LStoreCodeEntry* instr) {
  Register function = ToRegister(instr->function());
  Register code_object = ToRegister(instr->code_object());
  __ addi(code_object, code_object,
          Operand(Code::kHeaderSize - kHeapObjectTag));
  __ StoreP(code_object,
            FieldMemOperand(function, JSFunction::kCodeEntryOffset), r0);
}


void LCodeGen::DoInnerAllocatedObject(LInnerAllocatedObject* instr) {
  Register result = ToRegister(instr->result());
  Register base = ToRegister(instr->base_object());
  __ addi(result, base, Operand(instr->offset()));
}


void LCodeGen::DoStoreNamedField(LStoreNamedField* instr) {
  Representation representation = instr->representation();

  Register object = ToRegister(instr->object());
  Register scratch = scratch0();
  HObjectAccess access = instr->hydrogen()->access();
  int offset = access.offset();

  if (access.IsExternalMemory()) {
    Register value = ToRegister(instr->value());
    MemOperand operand = MemOperand(object, offset);
    __ StoreRepresentation(value, operand, representation);
    return;
  }

  Handle<Map> transition = instr->transition();

  if (FLAG_track_heap_object_fields && representation.IsHeapObject()) {
    Register value = ToRegister(instr->value());
    if (!instr->hydrogen()->value()->type().IsHeapObject()) {
      __ TestIfSmi(value, r0);
      DeoptimizeIf(eq, instr->environment(), cr0);
    }
  } else if (FLAG_track_double_fields && representation.IsDouble()) {
    ASSERT(transition.is_null());
    ASSERT(access.IsInobject());
    ASSERT(!instr->hydrogen()->NeedsWriteBarrier());
    DoubleRegister value = ToDoubleRegister(instr->value());
    __ stfd(value, FieldMemOperand(object, offset));
    return;
  }

  if (!transition.is_null()) {
    __ mov(scratch, Operand(transition));
    __ StoreP(scratch, FieldMemOperand(object, HeapObject::kMapOffset), r0);
    if (instr->hydrogen()->NeedsWriteBarrierForMap()) {
      Register temp = ToRegister(instr->temp());
      // Update the write barrier for the map field.
      __ RecordWriteField(object,
                          HeapObject::kMapOffset,
                          scratch,
                          temp,
                          GetLinkRegisterState(),
                          kSaveFPRegs,
                          OMIT_REMEMBERED_SET,
                          OMIT_SMI_CHECK);
    }
  }

  // Do the store.
  Register value = ToRegister(instr->value());
  ASSERT(!object.is(value));
  SmiCheck check_needed =
      instr->hydrogen()->value()->IsHeapObject()
          ? OMIT_SMI_CHECK : INLINE_SMI_CHECK;
  if (access.IsInobject()) {
    MemOperand operand = FieldMemOperand(object, offset);
    __ StoreRepresentation(value, operand, representation, r0);
    if (instr->hydrogen()->NeedsWriteBarrier()) {
      // Update the write barrier for the object for in-object properties.
      __ RecordWriteField(object,
                          offset,
                          value,
                          scratch,
                          GetLinkRegisterState(),
                          kSaveFPRegs,
                          EMIT_REMEMBERED_SET,
                          check_needed);
    }
  } else {
    __ LoadP(scratch, FieldMemOperand(object, JSObject::kPropertiesOffset));
    MemOperand operand = FieldMemOperand(scratch, offset);
    __ StoreRepresentation(value, operand, representation, r0);
    if (instr->hydrogen()->NeedsWriteBarrier()) {
      // Update the write barrier for the properties array.
      // object is used as a scratch register.
      __ RecordWriteField(scratch,
                          offset,
                          value,
                          object,
                          GetLinkRegisterState(),
                          kSaveFPRegs,
                          EMIT_REMEMBERED_SET,
                          check_needed);
    }
  }
}


void LCodeGen::DoStoreNamedGeneric(LStoreNamedGeneric* instr) {
  ASSERT(ToRegister(instr->context()).is(cp));
  ASSERT(ToRegister(instr->object()).is(r4));
  ASSERT(ToRegister(instr->value()).is(r3));

  // Name is always in r5.
  __ mov(r5, Operand(instr->name()));
  Handle<Code> ic = (instr->strict_mode_flag() == kStrictMode)
      ? isolate()->builtins()->StoreIC_Initialize_Strict()
      : isolate()->builtins()->StoreIC_Initialize();
  CallCode(ic, RelocInfo::CODE_TARGET, instr);
}


void LCodeGen::ApplyCheckIf(Condition cond, LBoundsCheck* check,
                            CRegister cr) {
  if (FLAG_debug_code && check->hydrogen()->skip_check()) {
    Label done;
    __ b(NegateCondition(cond), &done, cr);
    __ stop("eliminated bounds check failed");
    __ bind(&done);
  } else {
    DeoptimizeIf(cond, check->environment(), cr);
  }
}


void LCodeGen::DoBoundsCheck(LBoundsCheck* instr) {
  if (instr->hydrogen()->skip_check()) return;

  if (instr->index()->IsConstantOperand()) {
    int constant_index =
        ToInteger32(LConstantOperand::cast(instr->index()));
    if (instr->hydrogen()->length()->representation().IsSmi()) {
      __ LoadSmiLiteral(ip, Smi::FromInt(constant_index));
    } else {
      __ mov(ip, Operand(constant_index));
    }
    __ cmpl(ip, ToRegister(instr->length()));
  } else {
    __ cmpl(ToRegister(instr->index()), ToRegister(instr->length()));
  }
  Condition condition = instr->hydrogen()->allow_equality() ? gt : ge;
  ApplyCheckIf(condition, instr);
}


void LCodeGen::DoStoreKeyedExternalArray(LStoreKeyed* instr) {
  Register external_pointer = ToRegister(instr->elements());
  Register key = no_reg;
  ElementsKind elements_kind = instr->elements_kind();
  bool key_is_constant = instr->key()->IsConstantOperand();
  int constant_key = 0;
  if (key_is_constant) {
    constant_key = ToInteger32(LConstantOperand::cast(instr->key()));
    if (constant_key & 0xF0000000) {
      Abort(kArrayIndexConstantValueTooBig);
    }
  } else {
    key = ToRegister(instr->key());
  }
  int element_size_shift = ElementsKindToShiftSize(elements_kind);
  bool key_is_smi = instr->hydrogen()->key()->representation().IsSmi();
  int additional_offset = instr->additional_index() << element_size_shift;

  if (elements_kind == EXTERNAL_FLOAT_ELEMENTS ||
      elements_kind == EXTERNAL_DOUBLE_ELEMENTS) {
    Register address = scratch0();
    DoubleRegister value(ToDoubleRegister(instr->value()));
    if (key_is_constant) {
      if (constant_key != 0) {
        __ Add(address, external_pointer,
               constant_key << element_size_shift,
               r0);
      } else {
        address = external_pointer;
      }
    } else {
      __ IndexToArrayOffset(r0, key, element_size_shift, key_is_smi);
      __ add(address, external_pointer, r0);
    }
    if (elements_kind == EXTERNAL_FLOAT_ELEMENTS) {
      __ frsp(double_scratch0(), value);
      __ stfs(double_scratch0(), MemOperand(address, additional_offset));
    } else {  // i.e. elements_kind == EXTERNAL_DOUBLE_ELEMENTS
      __ stfd(value, MemOperand(address, additional_offset));
    }
  } else {
    Register value(ToRegister(instr->value()));
    MemOperand mem_operand = PrepareKeyedOperand(
      key, external_pointer, key_is_constant, key_is_smi, constant_key,
      element_size_shift, instr->additional_index(), additional_offset);
    switch (elements_kind) {
      case EXTERNAL_PIXEL_ELEMENTS:
      case EXTERNAL_BYTE_ELEMENTS:
      case EXTERNAL_UNSIGNED_BYTE_ELEMENTS:
        if (key_is_constant) {
          __ StoreByte(value, mem_operand, r0);
        } else {
          __ stbx(value, mem_operand);
        }
        break;
      case EXTERNAL_SHORT_ELEMENTS:
      case EXTERNAL_UNSIGNED_SHORT_ELEMENTS:
        if (key_is_constant) {
          __ StoreHalfWord(value, mem_operand, r0);
        } else {
          __ sthx(value, mem_operand);
        }
        break;
      case EXTERNAL_INT_ELEMENTS:
      case EXTERNAL_UNSIGNED_INT_ELEMENTS:
        if (key_is_constant) {
          __ StoreWord(value, mem_operand, r0);
        } else {
          __ stwx(value, mem_operand);
        }
        break;
      case EXTERNAL_FLOAT_ELEMENTS:
      case EXTERNAL_DOUBLE_ELEMENTS:
      case FAST_DOUBLE_ELEMENTS:
      case FAST_ELEMENTS:
      case FAST_SMI_ELEMENTS:
      case FAST_HOLEY_DOUBLE_ELEMENTS:
      case FAST_HOLEY_ELEMENTS:
      case FAST_HOLEY_SMI_ELEMENTS:
      case DICTIONARY_ELEMENTS:
      case NON_STRICT_ARGUMENTS_ELEMENTS:
        UNREACHABLE();
        break;
    }
  }
}


void LCodeGen::DoStoreKeyedFixedDoubleArray(LStoreKeyed* instr) {
  DoubleRegister value = ToDoubleRegister(instr->value());
  Register elements = ToRegister(instr->elements());
  Register key = no_reg;
  Register scratch = scratch0();
  DoubleRegister double_scratch = double_scratch0();
  bool key_is_constant = instr->key()->IsConstantOperand();
  int constant_key = 0;

  // Calculate the effective address of the slot in the array to store the
  // double value.
  if (key_is_constant) {
    constant_key = ToInteger32(LConstantOperand::cast(instr->key()));
    if (constant_key & 0xF0000000) {
      Abort(kArrayIndexConstantValueTooBig);
    }
  } else {
    key = ToRegister(instr->key());
  }
  int element_size_shift = ElementsKindToShiftSize(FAST_DOUBLE_ELEMENTS);
  bool key_is_smi = instr->hydrogen()->key()->representation().IsSmi();
  int dst_offset = instr->additional_index() << element_size_shift;
  if (key_is_constant) {
    __ Add(scratch, elements,
           (constant_key << element_size_shift) +
           FixedDoubleArray::kHeaderSize - kHeapObjectTag,
           r0);
  } else {
    __ IndexToArrayOffset(scratch, key, element_size_shift, key_is_smi);
    __ add(scratch, elements, scratch);
    __ addi(scratch, scratch,
            Operand(FixedDoubleArray::kHeaderSize - kHeapObjectTag));
  }

  if (instr->NeedsCanonicalization()) {
    // Force a canonical NaN.
    __ CanonicalizeNaN(double_scratch, value);
    __ stfd(double_scratch, MemOperand(scratch, dst_offset));
  } else {
    __ stfd(value, MemOperand(scratch, dst_offset));
  }
}


void LCodeGen::DoStoreKeyedFixedArray(LStoreKeyed* instr) {
  Register value = ToRegister(instr->value());
  Register elements = ToRegister(instr->elements());
  Register key = instr->key()->IsRegister() ? ToRegister(instr->key()) : no_reg;
  Register scratch = scratch0();
  Register store_base = scratch;
  int offset = 0;

  // Do the store.
  if (instr->key()->IsConstantOperand()) {
    ASSERT(!instr->hydrogen()->NeedsWriteBarrier());
    LConstantOperand* const_operand = LConstantOperand::cast(instr->key());
    offset = FixedArray::OffsetOfElementAt(ToInteger32(const_operand) +
                                           instr->additional_index());
    store_base = elements;
  } else {
    // Even though the HLoadKeyed instruction forces the input
    // representation for the key to be an integer, the input gets replaced
    // during bound check elimination with the index argument to the bounds
    // check, which can be tagged, so that case must be handled here, too.
    if (instr->hydrogen()->key()->representation().IsSmi()) {
      __ SmiToPtrArrayOffset(scratch, key);
    } else {
      __ ShiftLeftImm(scratch, key, Operand(kPointerSizeLog2));
    }
    __ add(scratch, elements, scratch);
    offset = FixedArray::OffsetOfElementAt(instr->additional_index());
  }
  __ StoreP(value, FieldMemOperand(store_base, offset), r0);

  if (instr->hydrogen()->NeedsWriteBarrier()) {
    SmiCheck check_needed =
        instr->hydrogen()->value()->IsHeapObject()
            ? OMIT_SMI_CHECK : INLINE_SMI_CHECK;
    // Compute address of modified element and store it into key register.
    __ Add(key, store_base, offset - kHeapObjectTag, r0);
    __ RecordWrite(elements,
                   key,
                   value,
                   GetLinkRegisterState(),
                   kSaveFPRegs,
                   EMIT_REMEMBERED_SET,
                   check_needed);
  }
}


void LCodeGen::DoStoreKeyed(LStoreKeyed* instr) {
  // By cases: external, fast double
  if (instr->is_external()) {
    DoStoreKeyedExternalArray(instr);
  } else if (instr->hydrogen()->value()->representation().IsDouble()) {
    DoStoreKeyedFixedDoubleArray(instr);
  } else {
    DoStoreKeyedFixedArray(instr);
  }
}


void LCodeGen::DoStoreKeyedGeneric(LStoreKeyedGeneric* instr) {
  ASSERT(ToRegister(instr->context()).is(cp));
  ASSERT(ToRegister(instr->object()).is(r5));
  ASSERT(ToRegister(instr->key()).is(r4));
  ASSERT(ToRegister(instr->value()).is(r3));

  Handle<Code> ic = (instr->strict_mode_flag() == kStrictMode)
      ? isolate()->builtins()->KeyedStoreIC_Initialize_Strict()
      : isolate()->builtins()->KeyedStoreIC_Initialize();
  CallCode(ic, RelocInfo::CODE_TARGET, instr);
}


void LCodeGen::DoTransitionElementsKind(LTransitionElementsKind* instr) {
  Register object_reg = ToRegister(instr->object());
  Register scratch = scratch0();

  Handle<Map> from_map = instr->original_map();
  Handle<Map> to_map = instr->transitioned_map();
  ElementsKind from_kind = instr->from_kind();
  ElementsKind to_kind = instr->to_kind();

  Label not_applicable;
  __ LoadP(scratch, FieldMemOperand(object_reg, HeapObject::kMapOffset));
  __ Cmpi(scratch, Operand(from_map), r0);
  __ bne(&not_applicable);

  if (IsSimpleMapChangeTransition(from_kind, to_kind)) {
    Register new_map_reg = ToRegister(instr->new_map_temp());
    __ mov(new_map_reg, Operand(to_map));
    __ StoreP(new_map_reg, FieldMemOperand(object_reg, HeapObject::kMapOffset),
              r0);
    // Write barrier.
    __ RecordWriteField(object_reg, HeapObject::kMapOffset, new_map_reg,
                        scratch, GetLinkRegisterState(), kDontSaveFPRegs);
  } else {
    ASSERT(ToRegister(instr->context()).is(cp));
    PushSafepointRegistersScope scope(
        this, Safepoint::kWithRegistersAndDoubles);
    __ Move(r3, object_reg);
    __ Move(r4, to_map);
    TransitionElementsKindStub stub(from_kind, to_kind);
    __ CallStub(&stub);
    RecordSafepointWithRegistersAndDoubles(
        instr->pointer_map(), 0, Safepoint::kNoLazyDeopt);
  }
  __ bind(&not_applicable);
}


void LCodeGen::DoTrapAllocationMemento(LTrapAllocationMemento* instr) {
  Register object = ToRegister(instr->object());
  Register temp = ToRegister(instr->temp());
  Label no_memento_found;
  __ TestJSArrayForAllocationMemento(object, temp, &no_memento_found);
  DeoptimizeIf(eq, instr->environment());
  __ bind(&no_memento_found);
}


void LCodeGen::DoStringAdd(LStringAdd* instr) {
  ASSERT(ToRegister(instr->context()).is(cp));
  __ push(ToRegister(instr->left()));
  __ push(ToRegister(instr->right()));
  StringAddStub stub(instr->hydrogen()->flags());
  CallCode(stub.GetCode(isolate()), RelocInfo::CODE_TARGET, instr);
}


void LCodeGen::DoStringCharCodeAt(LStringCharCodeAt* instr) {
  class DeferredStringCharCodeAt V8_FINAL : public LDeferredCode {
   public:
    DeferredStringCharCodeAt(LCodeGen* codegen, LStringCharCodeAt* instr)
        : LDeferredCode(codegen), instr_(instr) { }
    virtual void Generate() V8_OVERRIDE {
      codegen()->DoDeferredStringCharCodeAt(instr_);
    }
    virtual LInstruction* instr() V8_OVERRIDE { return instr_; }
   private:
    LStringCharCodeAt* instr_;
  };

  DeferredStringCharCodeAt* deferred =
      new(zone()) DeferredStringCharCodeAt(this, instr);

  StringCharLoadGenerator::Generate(masm(),
                                    ToRegister(instr->string()),
                                    ToRegister(instr->index()),
                                    ToRegister(instr->result()),
                                    deferred->entry());
  __ bind(deferred->exit());
}


void LCodeGen::DoDeferredStringCharCodeAt(LStringCharCodeAt* instr) {
  Register string = ToRegister(instr->string());
  Register result = ToRegister(instr->result());
  Register scratch = scratch0();

  // TODO(3095996): Get rid of this. For now, we need to make the
  // result register contain a valid pointer because it is already
  // contained in the register pointer map.
  __ li(result, Operand::Zero());

  PushSafepointRegistersScope scope(this, Safepoint::kWithRegisters);
  __ push(string);
  // Push the index as a smi. This is safe because of the checks in
  // DoStringCharCodeAt above.
  if (instr->index()->IsConstantOperand()) {
    int const_index = ToInteger32(LConstantOperand::cast(instr->index()));
    __ LoadSmiLiteral(scratch, Smi::FromInt(const_index));
    __ push(scratch);
  } else {
    Register index = ToRegister(instr->index());
    __ SmiTag(index);
    __ push(index);
  }
  CallRuntimeFromDeferred(Runtime::kStringCharCodeAt, 2, instr,
                          instr->context());
  __ AssertSmi(r3);
  __ SmiUntag(r3);
  __ StoreToSafepointRegisterSlot(r3, result);
}


void LCodeGen::DoStringCharFromCode(LStringCharFromCode* instr) {
  class DeferredStringCharFromCode: public LDeferredCode {
   public:
    DeferredStringCharFromCode(LCodeGen* codegen, LStringCharFromCode* instr)
        : LDeferredCode(codegen), instr_(instr) { }
    virtual void Generate() V8_OVERRIDE {
      codegen()->DoDeferredStringCharFromCode(instr_);
    }
    virtual LInstruction* instr() V8_OVERRIDE { return instr_; }
   private:
    LStringCharFromCode* instr_;
  };

  DeferredStringCharFromCode* deferred =
      new(zone()) DeferredStringCharFromCode(this, instr);

  ASSERT(instr->hydrogen()->value()->representation().IsInteger32());
  Register char_code = ToRegister(instr->char_code());
  Register result = ToRegister(instr->result());
  ASSERT(!char_code.is(result));

  __ cmpli(char_code, Operand(String::kMaxOneByteCharCode));
  __ bgt(deferred->entry());
  __ LoadRoot(result, Heap::kSingleCharacterStringCacheRootIndex);
  __ ShiftLeftImm(r0, char_code, Operand(kPointerSizeLog2));
  __ add(result, result, r0);
  __ LoadP(result, FieldMemOperand(result, FixedArray::kHeaderSize));
  __ LoadRoot(ip, Heap::kUndefinedValueRootIndex);
  __ cmp(result, ip);
  __ beq(deferred->entry());
  __ bind(deferred->exit());
}


void LCodeGen::DoDeferredStringCharFromCode(LStringCharFromCode* instr) {
  Register char_code = ToRegister(instr->char_code());
  Register result = ToRegister(instr->result());

  // TODO(3095996): Get rid of this. For now, we need to make the
  // result register contain a valid pointer because it is already
  // contained in the register pointer map.
  __ li(result, Operand::Zero());

  PushSafepointRegistersScope scope(this, Safepoint::kWithRegisters);
  __ SmiTag(char_code);
  __ push(char_code);
  CallRuntimeFromDeferred(Runtime::kCharFromCode, 1, instr, instr->context());
  __ StoreToSafepointRegisterSlot(r3, result);
}


void LCodeGen::DoInteger32ToDouble(LInteger32ToDouble* instr) {
  LOperand* input = instr->value();
  ASSERT(input->IsRegister() || input->IsStackSlot());
  LOperand* output = instr->result();
  ASSERT(output->IsDoubleRegister());
  if (input->IsStackSlot()) {
    Register scratch = scratch0();
    __ LoadP(scratch, ToMemOperand(input));
    __ ConvertIntToDouble(scratch, ToDoubleRegister(output));
  } else {
    __ ConvertIntToDouble(ToRegister(input), ToDoubleRegister(output));
  }
}


void LCodeGen::DoInteger32ToSmi(LInteger32ToSmi* instr) {
  LOperand* input = instr->value();
  LOperand* output = instr->result();

#if V8_TARGET_ARCH_PPC64
  __ SmiTag(ToRegister(output), ToRegister(input));
#else
  __ SmiTagCheckOverflow(ToRegister(output), ToRegister(input), r0);
  if (!instr->hydrogen()->value()->HasRange() ||
      !instr->hydrogen()->value()->range()->IsInSmiRange()) {
    DeoptimizeIf(lt, instr->environment(), cr0);
  }
#endif
}


void LCodeGen::DoUint32ToDouble(LUint32ToDouble* instr) {
  LOperand* input = instr->value();
  LOperand* output = instr->result();
  __ ConvertUnsignedIntToDouble(ToRegister(input), ToDoubleRegister(output));
}


void LCodeGen::DoUint32ToSmi(LUint32ToSmi* instr) {
  LOperand* input = instr->value();
  LOperand* output = instr->result();
  if (!instr->hydrogen()->value()->HasRange() ||
      !instr->hydrogen()->value()->range()->IsInSmiRange()
#if V8_TARGET_ARCH_PPC64
      || instr->hydrogen()->value()->range()->upper() == kMaxInt
#endif
    ) {
    __ TestUnsignedSmiCandidate(ToRegister(input), r0);
    DeoptimizeIf(ne, instr->environment(), cr0);
  }
  __ SmiTag(ToRegister(output), ToRegister(input));
}


void LCodeGen::DoNumberTagI(LNumberTagI* instr) {
  class DeferredNumberTagI V8_FINAL : public LDeferredCode {
   public:
    DeferredNumberTagI(LCodeGen* codegen, LNumberTagI* instr)
        : LDeferredCode(codegen), instr_(instr) { }
    virtual void Generate() V8_OVERRIDE {
      codegen()->DoDeferredNumberTagI(instr_,
                                      instr_->value(),
                                      SIGNED_INT32);
    }
    virtual LInstruction* instr() V8_OVERRIDE { return instr_; }
   private:
    LNumberTagI* instr_;
  };

  Register src = ToRegister(instr->value());
  Register dst = ToRegister(instr->result());

  DeferredNumberTagI* deferred = new(zone()) DeferredNumberTagI(this, instr);
#if V8_TARGET_ARCH_PPC64
  __ SmiTag(dst, src);
#else
  __ SmiTagCheckOverflow(dst, src, r0);
  __ BranchOnOverflow(deferred->entry());
#endif
  __ bind(deferred->exit());
}


void LCodeGen::DoNumberTagU(LNumberTagU* instr) {
  class DeferredNumberTagU V8_FINAL : public LDeferredCode {
   public:
    DeferredNumberTagU(LCodeGen* codegen, LNumberTagU* instr)
        : LDeferredCode(codegen), instr_(instr) { }
    virtual void Generate() V8_OVERRIDE {
      codegen()->DoDeferredNumberTagI(instr_,
                                      instr_->value(),
                                      UNSIGNED_INT32);
    }
    virtual LInstruction* instr() V8_OVERRIDE { return instr_; }
   private:
    LNumberTagU* instr_;
  };

  LOperand* input = instr->value();
  ASSERT(input->IsRegister() && input->Equals(instr->result()));
  Register reg = ToRegister(input);

  DeferredNumberTagU* deferred = new(zone()) DeferredNumberTagU(this, instr);
  __ Cmpli(reg, Operand(Smi::kMaxValue), r0);
  __ bgt(deferred->entry());
  __ SmiTag(reg, reg);
  __ bind(deferred->exit());
}


void LCodeGen::DoDeferredNumberTagI(LInstruction* instr,
                                    LOperand* value,
                                    IntegerSignedness signedness) {
  Label slow;
  Register src = ToRegister(value);
  Register dst = ToRegister(instr->result());
  DoubleRegister dbl_scratch = double_scratch0();

  // Preserve the value of all registers.
  PushSafepointRegistersScope scope(this, Safepoint::kWithRegisters);

  Label done;
  if (signedness == SIGNED_INT32) {
    // There was overflow, so bits 30 and 31 of the original integer
    // disagree. Try to allocate a heap number in new space and store
    // the value in there. If that fails, call the runtime system.
    if (dst.is(src)) {
      __ SmiUntag(src, dst);
      __ xoris(src, src, Operand(HeapNumber::kSignMask >> 16));
    }
    __ ConvertIntToDouble(src, dbl_scratch);
  } else {
    __ ConvertUnsignedIntToDouble(src, dbl_scratch);
  }

  if (FLAG_inline_new) {
    __ LoadRoot(scratch0(), Heap::kHeapNumberMapRootIndex);
    __ AllocateHeapNumber(r8, r6, r7, scratch0(), &slow);
    __ Move(dst, r8);
    __ b(&done);
  }

  // Slow case: Call the runtime system to do the number allocation.
  __ bind(&slow);

  // TODO(3095996): Put a valid pointer value in the stack slot where the result
  // register is stored, as this register is in the pointer map, but contains an
  // integer value.
  __ li(ip, Operand::Zero());
  __ StoreToSafepointRegisterSlot(ip, dst);
  // NumberTagI and NumberTagD use the context from the frame, rather than
  // the environment's HContext or HInlinedContext value.
  // They only call Runtime::kAllocateHeapNumber.
  // The corresponding HChange instructions are added in a phase that does
  // not have easy access to the local context.
  __ LoadP(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
  __ CallRuntimeSaveDoubles(Runtime::kAllocateHeapNumber);
  RecordSafepointWithRegisters(
      instr->pointer_map(), 0, Safepoint::kNoLazyDeopt);
  __ Move(dst, r3);

  // Done. Put the value in dbl_scratch into the value of the allocated heap
  // number.
  __ bind(&done);
  __ stfd(dbl_scratch, FieldMemOperand(dst, HeapNumber::kValueOffset));
  __ StoreToSafepointRegisterSlot(dst, dst);
}


void LCodeGen::DoNumberTagD(LNumberTagD* instr) {
  class DeferredNumberTagD V8_FINAL : public LDeferredCode {
   public:
    DeferredNumberTagD(LCodeGen* codegen, LNumberTagD* instr)
        : LDeferredCode(codegen), instr_(instr) { }
    virtual void Generate() V8_OVERRIDE {
      codegen()->DoDeferredNumberTagD(instr_);
    }
    virtual LInstruction* instr() V8_OVERRIDE { return instr_; }
   private:
    LNumberTagD* instr_;
  };

  DoubleRegister input_reg = ToDoubleRegister(instr->value());
  Register scratch = scratch0();
  Register reg = ToRegister(instr->result());
  Register temp1 = ToRegister(instr->temp());
  Register temp2 = ToRegister(instr->temp2());

  DeferredNumberTagD* deferred = new(zone()) DeferredNumberTagD(this, instr);
  if (FLAG_inline_new) {
    __ LoadRoot(scratch, Heap::kHeapNumberMapRootIndex);
    __ AllocateHeapNumber(reg, temp1, temp2, scratch, deferred->entry());
  } else {
    __ b(deferred->entry());
  }
  __ bind(deferred->exit());
  __ stfd(input_reg, FieldMemOperand(reg, HeapNumber::kValueOffset));
}


void LCodeGen::DoDeferredNumberTagD(LNumberTagD* instr) {
  // TODO(3095996): Get rid of this. For now, we need to make the
  // result register contain a valid pointer because it is already
  // contained in the register pointer map.
  Register reg = ToRegister(instr->result());
  __ li(reg, Operand::Zero());

  PushSafepointRegistersScope scope(this, Safepoint::kWithRegisters);
  // NumberTagI and NumberTagD use the context from the frame, rather than
  // the environment's HContext or HInlinedContext value.
  // They only call Runtime::kAllocateHeapNumber.
  // The corresponding HChange instructions are added in a phase that does
  // not have easy access to the local context.
  __ LoadP(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
  __ CallRuntimeSaveDoubles(Runtime::kAllocateHeapNumber);
  RecordSafepointWithRegisters(
      instr->pointer_map(), 0, Safepoint::kNoLazyDeopt);
  __ StoreToSafepointRegisterSlot(r3, reg);
}


void LCodeGen::DoSmiTag(LSmiTag* instr) {
  ASSERT(!instr->hydrogen_value()->CheckFlag(HValue::kCanOverflow));
  __ SmiTag(ToRegister(instr->result()), ToRegister(instr->value()));
}


void LCodeGen::DoSmiUntag(LSmiUntag* instr) {
  Register scratch = scratch0();
  Register input = ToRegister(instr->value());
  Register result = ToRegister(instr->result());
  if (instr->needs_check()) {
    STATIC_ASSERT(kHeapObjectTag == 1);
    // If the input is a HeapObject, value of scratch won't be zero.
    __ andi(scratch, input, Operand(kHeapObjectTag));
    __ SmiUntag(result, input);
    DeoptimizeIf(ne, instr->environment(), cr0);
  } else {
    __ SmiUntag(result, input);
  }
}


void LCodeGen::EmitNumberUntagD(Register input_reg,
                                DoubleRegister result_reg,
                                bool can_convert_undefined_to_nan,
                                bool deoptimize_on_minus_zero,
                                LEnvironment* env,
                                NumberUntagDMode mode) {
  Register scratch = scratch0();
  ASSERT(!result_reg.is(double_scratch0()));

  Label convert, load_smi, done;

  if (mode == NUMBER_CANDIDATE_IS_ANY_TAGGED) {
    // Smi check.
    __ UntagAndJumpIfSmi(scratch, input_reg, &load_smi);

    // Heap number map check.
    __ LoadP(scratch, FieldMemOperand(input_reg, HeapObject::kMapOffset));
    __ LoadRoot(ip, Heap::kHeapNumberMapRootIndex);
    __ cmp(scratch, ip);
    if (can_convert_undefined_to_nan) {
      __ bne(&convert);
    } else {
      DeoptimizeIf(ne, env);
    }
    // load heap number
    __ lfd(result_reg, FieldMemOperand(input_reg, HeapNumber::kValueOffset));
    if (deoptimize_on_minus_zero) {
      __ stfdu(result_reg, MemOperand(sp, -kDoubleSize));
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
      __ lwz(ip, MemOperand(sp, 0));
      __ lwz(scratch, MemOperand(sp, 4));
#else
      __ lwz(ip, MemOperand(sp, 4));
      __ lwz(scratch, MemOperand(sp, 0));
#endif
      __ addi(sp, sp, Operand(kDoubleSize));

      __ cmpi(ip, Operand::Zero());
      __ bne(&done);
      __ Cmpi(scratch, Operand(HeapNumber::kSignMask), r0);
      DeoptimizeIf(eq, env);
    }
    __ b(&done);
    if (can_convert_undefined_to_nan) {
      __ bind(&convert);
      // Convert undefined (and hole) to NaN.
      __ LoadRoot(ip, Heap::kUndefinedValueRootIndex);
      __ cmp(input_reg, ip);
      DeoptimizeIf(ne, env);
      __ LoadRoot(scratch, Heap::kNanValueRootIndex);
      __ lfd(result_reg, FieldMemOperand(scratch, HeapNumber::kValueOffset));
      __ b(&done);
    }
  } else {
    __ SmiUntag(scratch, input_reg);
    ASSERT(mode == NUMBER_CANDIDATE_IS_SMI);
  }
  // Smi to double register conversion
  __ bind(&load_smi);
  // scratch: untagged value of input_reg
  __ ConvertIntToDouble(scratch, result_reg);
  __ bind(&done);
}


void LCodeGen::DoDeferredTaggedToI(LTaggedToI* instr) {
  Register input_reg = ToRegister(instr->value());
  Register scratch1 = scratch0();
  Register scratch2 = ToRegister(instr->temp());
  DoubleRegister double_scratch = double_scratch0();
  DoubleRegister double_scratch2 = ToDoubleRegister(instr->temp2());

  ASSERT(!scratch1.is(input_reg) && !scratch1.is(scratch2));
  ASSERT(!scratch2.is(input_reg) && !scratch2.is(scratch1));

  Label done;

  // Heap number map check.
  __ LoadP(scratch1, FieldMemOperand(input_reg, HeapObject::kMapOffset));
  __ LoadRoot(ip, Heap::kHeapNumberMapRootIndex);
  __ cmp(scratch1, ip);

  if (instr->truncating()) {
    // Performs a truncating conversion of a floating point number as used by
    // the JS bitwise operations.
    Label no_heap_number, check_bools, check_false;
    __ bne(&no_heap_number);
    __ mr(scratch2, input_reg);
    __ TruncateHeapNumberToI(input_reg, scratch2);
    __ b(&done);

    // Check for Oddballs. Undefined/False is converted to zero and True to one
    // for truncating conversions.
    __ bind(&no_heap_number);
    __ LoadRoot(ip, Heap::kUndefinedValueRootIndex);
    __ cmp(input_reg, ip);
    __ bne(&check_bools);
    __ li(input_reg, Operand::Zero());
    __ b(&done);

    __ bind(&check_bools);
    __ LoadRoot(ip, Heap::kTrueValueRootIndex);
    __ cmp(input_reg, ip);
    __ bne(&check_false);
    __ li(input_reg, Operand(1));
    __ b(&done);

    __ bind(&check_false);
    __ LoadRoot(ip, Heap::kFalseValueRootIndex);
    __ cmp(input_reg, ip);
    DeoptimizeIf(ne, instr->environment());
    __ li(input_reg, Operand::Zero());
  } else {
    // Deoptimize if we don't have a heap number.
    DeoptimizeIf(ne, instr->environment());

    __ lfd(double_scratch2,
           FieldMemOperand(input_reg, HeapNumber::kValueOffset));
    if (instr->hydrogen()->CheckFlag(HValue::kBailoutOnMinusZero)) {
      // preserve heap number pointer in scratch2 for minus zero check below
      __ mr(scratch2, input_reg);
    }
    __ TryDoubleToInt32Exact(input_reg, double_scratch2,
                             scratch1, double_scratch);
    DeoptimizeIf(ne, instr->environment());

    if (instr->hydrogen()->CheckFlag(HValue::kBailoutOnMinusZero)) {
      __ cmpi(input_reg, Operand::Zero());
      __ bne(&done);
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
      __ lwz(scratch1, FieldMemOperand(scratch2, HeapNumber::kValueOffset + 4));
#else
      __ lwz(scratch1, FieldMemOperand(scratch2, HeapNumber::kValueOffset));
#endif
      __ TestSignBit32(scratch1, r0);
      DeoptimizeIf(ne, instr->environment(), cr0);
    }
  }
  __ bind(&done);
}


void LCodeGen::DoTaggedToI(LTaggedToI* instr) {
  class DeferredTaggedToI V8_FINAL : public LDeferredCode {
   public:
    DeferredTaggedToI(LCodeGen* codegen, LTaggedToI* instr)
        : LDeferredCode(codegen), instr_(instr) { }
    virtual void Generate() V8_OVERRIDE {
      codegen()->DoDeferredTaggedToI(instr_);
    }
    virtual LInstruction* instr() V8_OVERRIDE { return instr_; }
   private:
    LTaggedToI* instr_;
  };

  LOperand* input = instr->value();
  ASSERT(input->IsRegister());
  ASSERT(input->Equals(instr->result()));

  Register input_reg = ToRegister(input);

  if (instr->hydrogen()->value()->representation().IsSmi()) {
    __ SmiUntag(input_reg);
  } else {
    DeferredTaggedToI* deferred = new(zone()) DeferredTaggedToI(this, instr);

    // Branch to deferred code if the input is a HeapObject.
    __ JumpIfNotSmi(input_reg, deferred->entry());

    __ SmiUntag(input_reg);
    __ bind(deferred->exit());
  }
}


void LCodeGen::DoNumberUntagD(LNumberUntagD* instr) {
  LOperand* input = instr->value();
  ASSERT(input->IsRegister());
  LOperand* result = instr->result();
  ASSERT(result->IsDoubleRegister());

  Register input_reg = ToRegister(input);
  DoubleRegister result_reg = ToDoubleRegister(result);

  HValue* value = instr->hydrogen()->value();
  NumberUntagDMode mode = value->representation().IsSmi()
      ? NUMBER_CANDIDATE_IS_SMI : NUMBER_CANDIDATE_IS_ANY_TAGGED;

  EmitNumberUntagD(input_reg, result_reg,
                   instr->hydrogen()->can_convert_undefined_to_nan(),
                   instr->hydrogen()->deoptimize_on_minus_zero(),
                   instr->environment(),
                   mode);
}


void LCodeGen::DoDoubleToI(LDoubleToI* instr) {
  Register result_reg = ToRegister(instr->result());
  Register scratch1 = scratch0();
  DoubleRegister double_input = ToDoubleRegister(instr->value());
  DoubleRegister double_scratch = double_scratch0();

  if (instr->truncating()) {
    __ TruncateDoubleToI(result_reg, double_input);
  } else {
    __ TryDoubleToInt32Exact(result_reg, double_input,
                             scratch1, double_scratch);
    // Deoptimize if the input wasn't a int32 (inside a double).
    DeoptimizeIf(ne, instr->environment());
    if (instr->hydrogen()->CheckFlag(HValue::kBailoutOnMinusZero)) {
      Label done;
      __ cmpi(result_reg, Operand::Zero());
      __ bne(&done);
      __ stfdu(double_input, MemOperand(sp, -kDoubleSize));
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
      __ lwz(scratch1, MemOperand(sp, 4));
#else
      __ lwz(scratch1, MemOperand(sp, 0));
#endif
      __ addi(sp, sp, Operand(kDoubleSize));
      __ TestSignBit32(scratch1, r0);
      DeoptimizeIf(ne, instr->environment(), cr0);
      __ bind(&done);
    }
  }
}


void LCodeGen::DoDoubleToSmi(LDoubleToSmi* instr) {
  Register result_reg = ToRegister(instr->result());
  Register scratch1 = scratch0();
  DoubleRegister double_input = ToDoubleRegister(instr->value());
  DoubleRegister double_scratch = double_scratch0();

  if (instr->truncating()) {
    __ TruncateDoubleToI(result_reg, double_input);
  } else {
    __ TryDoubleToInt32Exact(result_reg, double_input,
                             scratch1, double_scratch);
    // Deoptimize if the input wasn't a int32 (inside a double).
    DeoptimizeIf(ne, instr->environment());
    if (instr->hydrogen()->CheckFlag(HValue::kBailoutOnMinusZero)) {
      Label done;
      __ cmpi(result_reg, Operand::Zero());
      __ bne(&done);
      __ stfdu(double_input, MemOperand(sp, -kDoubleSize));
#if __FLOAT_WORD_ORDER == __LITTLE_ENDIAN
      __ lwz(scratch1, MemOperand(sp, 4));
#else
      __ lwz(scratch1, MemOperand(sp, 0));
#endif
      __ addi(sp, sp, Operand(kDoubleSize));
      __ TestSignBit32(scratch1, r0);
      DeoptimizeIf(ne, instr->environment(), cr0);
      __ bind(&done);
    }
  }
#if V8_TARGET_ARCH_PPC64
  __ SmiTag(result_reg);
#else
  __ SmiTagCheckOverflow(result_reg, r0);
  DeoptimizeIf(lt, instr->environment(), cr0);
#endif
}


void LCodeGen::DoCheckSmi(LCheckSmi* instr) {
  LOperand* input = instr->value();
  __ TestIfSmi(ToRegister(input), r0);
  DeoptimizeIf(ne, instr->environment(), cr0);
}


void LCodeGen::DoCheckNonSmi(LCheckNonSmi* instr) {
  if (!instr->hydrogen()->value()->IsHeapObject()) {
    LOperand* input = instr->value();
    __ TestIfSmi(ToRegister(input), r0);
    DeoptimizeIf(eq, instr->environment(), cr0);
  }
}


void LCodeGen::DoCheckInstanceType(LCheckInstanceType* instr) {
  Register input = ToRegister(instr->value());
  Register scratch = scratch0();

  __ LoadP(scratch, FieldMemOperand(input, HeapObject::kMapOffset));
  __ lbz(scratch, FieldMemOperand(scratch, Map::kInstanceTypeOffset));

  if (instr->hydrogen()->is_interval_check()) {
    InstanceType first;
    InstanceType last;
    instr->hydrogen()->GetCheckInterval(&first, &last);

    __ cmpli(scratch, Operand(first));

    // If there is only one type in the interval check for equality.
    if (first == last) {
      DeoptimizeIf(ne, instr->environment());
    } else {
      DeoptimizeIf(lt, instr->environment());
      // Omit check for the last type.
      if (last != LAST_TYPE) {
        __ cmpli(scratch, Operand(last));
        DeoptimizeIf(gt, instr->environment());
      }
    }
  } else {
    uint8_t mask;
    uint8_t tag;
    instr->hydrogen()->GetCheckMaskAndTag(&mask, &tag);

    if (IsPowerOf2(mask)) {
      ASSERT(tag == 0 || IsPowerOf2(tag));
      __ andi(r0, scratch, Operand(mask));
      DeoptimizeIf(tag == 0 ? ne : eq, instr->environment(), cr0);
    } else {
      __ andi(scratch, scratch, Operand(mask));
      __ cmpi(scratch, Operand(tag));
      DeoptimizeIf(ne, instr->environment());
    }
  }
}


void LCodeGen::DoCheckValue(LCheckValue* instr) {
  Register reg = ToRegister(instr->value());
  Handle<HeapObject> object = instr->hydrogen()->object().handle();
  AllowDeferredHandleDereference smi_check;
  if (isolate()->heap()->InNewSpace(*object)) {
    Register reg = ToRegister(instr->value());
    Handle<Cell> cell = isolate()->factory()->NewCell(object);
    __ mov(ip, Operand(Handle<Object>(cell)));
    __ LoadP(ip, FieldMemOperand(ip, Cell::kValueOffset));
    __ cmp(reg, ip);
  } else {
    __ Cmpi(reg, Operand(object), r0);
  }
  DeoptimizeIf(ne, instr->environment());
}


void LCodeGen::DoDeferredInstanceMigration(LCheckMaps* instr, Register object) {
  {
    PushSafepointRegistersScope scope(this, Safepoint::kWithRegisters);
    __ push(object);
    __ li(cp, Operand::Zero());
    __ CallRuntimeSaveDoubles(Runtime::kMigrateInstance);
    RecordSafepointWithRegisters(
        instr->pointer_map(), 1, Safepoint::kNoLazyDeopt);
    __ StoreToSafepointRegisterSlot(r3, scratch0());
  }
  __ TestIfSmi(scratch0(), r0);
  DeoptimizeIf(eq, instr->environment(), cr0);
}


void LCodeGen::DoCheckMaps(LCheckMaps* instr) {
  class DeferredCheckMaps V8_FINAL : public LDeferredCode {
   public:
    DeferredCheckMaps(LCodeGen* codegen, LCheckMaps* instr, Register object)
        : LDeferredCode(codegen), instr_(instr), object_(object) {
      SetExit(check_maps());
    }
    virtual void Generate() V8_OVERRIDE {
      codegen()->DoDeferredInstanceMigration(instr_, object_);
    }
    Label* check_maps() { return &check_maps_; }
    virtual LInstruction* instr() V8_OVERRIDE { return instr_; }
   private:
    LCheckMaps* instr_;
    Label check_maps_;
    Register object_;
  };

  if (instr->hydrogen()->CanOmitMapChecks()) return;
  Register map_reg = scratch0();

  LOperand* input = instr->value();
  ASSERT(input->IsRegister());
  Register reg = ToRegister(input);

  __ LoadP(map_reg, FieldMemOperand(reg, HeapObject::kMapOffset));

  DeferredCheckMaps* deferred = NULL;
  if (instr->hydrogen()->has_migration_target()) {
    deferred = new(zone()) DeferredCheckMaps(this, instr, reg);
    __ bind(deferred->check_maps());
  }

  UniqueSet<Map> map_set = instr->hydrogen()->map_set();
  Label success;
  for (int i = 0; i < map_set.size() - 1; i++) {
    Handle<Map> map = map_set.at(i).handle();
    __ CompareMap(map_reg, map, &success);
    __ beq(&success);
  }

  Handle<Map> map = map_set.at(map_set.size() - 1).handle();
  __ CompareMap(map_reg, map, &success);
  if (instr->hydrogen()->has_migration_target()) {
    __ bne(deferred->entry());
  } else {
    DeoptimizeIf(ne, instr->environment());
  }

  __ bind(&success);
}


void LCodeGen::DoClampDToUint8(LClampDToUint8* instr) {
  DoubleRegister value_reg = ToDoubleRegister(instr->unclamped());
  Register result_reg = ToRegister(instr->result());
  __ ClampDoubleToUint8(result_reg, value_reg, double_scratch0());
}


void LCodeGen::DoClampIToUint8(LClampIToUint8* instr) {
  Register unclamped_reg = ToRegister(instr->unclamped());
  Register result_reg = ToRegister(instr->result());
  __ ClampUint8(result_reg, unclamped_reg);
}


void LCodeGen::DoClampTToUint8(LClampTToUint8* instr) {
  Register scratch = scratch0();
  Register input_reg = ToRegister(instr->unclamped());
  Register result_reg = ToRegister(instr->result());
  DoubleRegister temp_reg = ToDoubleRegister(instr->temp());
  Label is_smi, done, heap_number;

  // Both smi and heap number cases are handled.
  __ UntagAndJumpIfSmi(result_reg, input_reg, &is_smi);

  // Check for heap number
  __ LoadP(scratch, FieldMemOperand(input_reg, HeapObject::kMapOffset));
  __ Cmpi(scratch, Operand(factory()->heap_number_map()), r0);
  __ beq(&heap_number);

  // Check for undefined. Undefined is converted to zero for clamping
  // conversions.
  __ Cmpi(input_reg, Operand(factory()->undefined_value()), r0);
  DeoptimizeIf(ne, instr->environment());
  __ li(result_reg, Operand::Zero());
  __ b(&done);

  // Heap number
  __ bind(&heap_number);
  __ lfd(temp_reg, FieldMemOperand(input_reg, HeapNumber::kValueOffset));
  __ ClampDoubleToUint8(result_reg, temp_reg, double_scratch0());
  __ b(&done);

  // smi
  __ bind(&is_smi);
  __ ClampUint8(result_reg, result_reg);

  __ bind(&done);
}


void LCodeGen::DoAllocate(LAllocate* instr) {
  class DeferredAllocate V8_FINAL : public LDeferredCode {
   public:
    DeferredAllocate(LCodeGen* codegen, LAllocate* instr)
        : LDeferredCode(codegen), instr_(instr) { }
    virtual void Generate() V8_OVERRIDE {
      codegen()->DoDeferredAllocate(instr_);
    }
    virtual LInstruction* instr() V8_OVERRIDE { return instr_; }
   private:
    LAllocate* instr_;
  };

  DeferredAllocate* deferred =
      new(zone()) DeferredAllocate(this, instr);

  Register result = ToRegister(instr->result());
  Register scratch = ToRegister(instr->temp1());
  Register scratch2 = ToRegister(instr->temp2());

  // Allocate memory for the object.
  AllocationFlags flags = TAG_OBJECT;
  if (instr->hydrogen()->MustAllocateDoubleAligned()) {
    flags = static_cast<AllocationFlags>(flags | DOUBLE_ALIGNMENT);
  }
  if (instr->hydrogen()->IsOldPointerSpaceAllocation()) {
    ASSERT(!instr->hydrogen()->IsOldDataSpaceAllocation());
    ASSERT(!instr->hydrogen()->IsNewSpaceAllocation());
    flags = static_cast<AllocationFlags>(flags | PRETENURE_OLD_POINTER_SPACE);
  } else if (instr->hydrogen()->IsOldDataSpaceAllocation()) {
    ASSERT(!instr->hydrogen()->IsNewSpaceAllocation());
    flags = static_cast<AllocationFlags>(flags | PRETENURE_OLD_DATA_SPACE);
  }

  if (instr->size()->IsConstantOperand()) {
    int32_t size = ToInteger32(LConstantOperand::cast(instr->size()));
    __ Allocate(size, result, scratch, scratch2, deferred->entry(), flags);
  } else {
    Register size = ToRegister(instr->size());
    __ Allocate(size,
                result,
                scratch,
                scratch2,
                deferred->entry(),
                flags);
  }

  __ bind(deferred->exit());

  if (instr->hydrogen()->MustPrefillWithFiller()) {
    if (instr->size()->IsConstantOperand()) {
      int32_t size = ToInteger32(LConstantOperand::cast(instr->size()));
      __ LoadIntLiteral(scratch, size);
    } else {
      scratch = ToRegister(instr->size());
    }
    __ subi(scratch, scratch, Operand(kPointerSize));
    __ subi(result, result, Operand(kHeapObjectTag));
    Label loop;
    __ bind(&loop);
    __ mov(scratch2, Operand(isolate()->factory()->one_pointer_filler_map()));
    __ StorePX(scratch2, MemOperand(result, scratch));
    __ subi(scratch, scratch, Operand(kPointerSize));
    __ cmpi(scratch, Operand::Zero());
    __ bge(&loop);
    __ addi(result, result, Operand(kHeapObjectTag));
  }
}


void LCodeGen::DoDeferredAllocate(LAllocate* instr) {
  Register result = ToRegister(instr->result());

  // TODO(3095996): Get rid of this. For now, we need to make the
  // result register contain a valid pointer because it is already
  // contained in the register pointer map.
  __ LoadSmiLiteral(result, Smi::FromInt(0));

  PushSafepointRegistersScope scope(this, Safepoint::kWithRegisters);
  if (instr->size()->IsRegister()) {
    Register size = ToRegister(instr->size());
    ASSERT(!size.is(result));
    __ SmiTag(size);
    __ push(size);
  } else {
    int32_t size = ToInteger32(LConstantOperand::cast(instr->size()));
    __ Push(Smi::FromInt(size));
  }

  if (instr->hydrogen()->IsOldPointerSpaceAllocation()) {
    ASSERT(!instr->hydrogen()->IsOldDataSpaceAllocation());
    ASSERT(!instr->hydrogen()->IsNewSpaceAllocation());
    CallRuntimeFromDeferred(Runtime::kAllocateInOldPointerSpace, 1, instr,
                            instr->context());
  } else if (instr->hydrogen()->IsOldDataSpaceAllocation()) {
    ASSERT(!instr->hydrogen()->IsNewSpaceAllocation());
    CallRuntimeFromDeferred(Runtime::kAllocateInOldDataSpace, 1, instr,
                            instr->context());
  } else {
    CallRuntimeFromDeferred(Runtime::kAllocateInNewSpace, 1, instr,
                            instr->context());
  }
  __ StoreToSafepointRegisterSlot(r3, result);
}


void LCodeGen::DoToFastProperties(LToFastProperties* instr) {
  ASSERT(ToRegister(instr->value()).is(r3));
  __ push(r3);
  CallRuntime(Runtime::kToFastProperties, 1, instr);
}


void LCodeGen::DoRegExpLiteral(LRegExpLiteral* instr) {
  ASSERT(ToRegister(instr->context()).is(cp));
  Label materialized;
  // Registers will be used as follows:
  // r10 = literals array.
  // r4 = regexp literal.
  // r3 = regexp literal clone.
  // r5 and r7-r9 are used as temporaries.
  int literal_offset =
      FixedArray::OffsetOfElementAt(instr->hydrogen()->literal_index());
  __ Move(r10, instr->hydrogen()->literals());
  __ LoadP(r4, FieldMemOperand(r10, literal_offset));
  __ LoadRoot(ip, Heap::kUndefinedValueRootIndex);
  __ cmp(r4, ip);
  __ bne(&materialized);

  // Create regexp literal using runtime function
  // Result will be in r3.
  __ LoadSmiLiteral(r9, Smi::FromInt(instr->hydrogen()->literal_index()));
  __ mov(r8, Operand(instr->hydrogen()->pattern()));
  __ mov(r7, Operand(instr->hydrogen()->flags()));
  __ Push(r10, r9, r8, r7);
  CallRuntime(Runtime::kMaterializeRegExpLiteral, 4, instr);
  __ mr(r4, r3);

  __ bind(&materialized);
  int size = JSRegExp::kSize + JSRegExp::kInObjectFieldCount * kPointerSize;
  Label allocated, runtime_allocate;

  __ Allocate(size, r3, r5, r6, &runtime_allocate, TAG_OBJECT);
  __ b(&allocated);

  __ bind(&runtime_allocate);
  __ LoadSmiLiteral(r3, Smi::FromInt(size));
  __ Push(r4, r3);
  CallRuntime(Runtime::kAllocateInNewSpace, 1, instr);
  __ pop(r4);

  __ bind(&allocated);
  // Copy the content into the newly allocated memory.
  __ CopyFields(r3, r4, r5.bit(), size / kPointerSize);
}


void LCodeGen::DoFunctionLiteral(LFunctionLiteral* instr) {
  ASSERT(ToRegister(instr->context()).is(cp));
  // Use the fast case closure allocation code that allocates in new
  // space for nested functions that don't need literals cloning.
  bool pretenure = instr->hydrogen()->pretenure();
  if (!pretenure && instr->hydrogen()->has_no_literals()) {
    FastNewClosureStub stub(instr->hydrogen()->language_mode(),
                            instr->hydrogen()->is_generator());
    __ mov(r5, Operand(instr->hydrogen()->shared_info()));
    CallCode(stub.GetCode(isolate()), RelocInfo::CODE_TARGET, instr);
  } else {
    __ mov(r5, Operand(instr->hydrogen()->shared_info()));
    __ mov(r4, Operand(pretenure ? factory()->true_value()
                       : factory()->false_value()));
    __ Push(cp, r5, r4);
    CallRuntime(Runtime::kNewClosure, 3, instr);
  }
}


void LCodeGen::DoTypeof(LTypeof* instr) {
  Register input = ToRegister(instr->value());
  __ push(input);
  CallRuntime(Runtime::kTypeof, 1, instr);
}


void LCodeGen::DoTypeofIsAndBranch(LTypeofIsAndBranch* instr) {
  Register input = ToRegister(instr->value());

  Condition final_branch_condition = EmitTypeofIs(instr->TrueLabel(chunk_),
                                                  instr->FalseLabel(chunk_),
                                                  input,
                                                  instr->type_literal());
  if (final_branch_condition != kNoCondition) {
    EmitBranch(instr, final_branch_condition);
  }
}


Condition LCodeGen::EmitTypeofIs(Label* true_label,
                                 Label* false_label,
                                 Register input,
                                 Handle<String> type_name) {
  Condition final_branch_condition = kNoCondition;
  Register scratch = scratch0();
  if (type_name->Equals(heap()->number_string())) {
    __ JumpIfSmi(input, true_label);
    __ LoadP(input, FieldMemOperand(input, HeapObject::kMapOffset));
    __ LoadRoot(ip, Heap::kHeapNumberMapRootIndex);
    __ cmp(input, ip);
    final_branch_condition = eq;

  } else if (type_name->Equals(heap()->string_string())) {
    __ JumpIfSmi(input, false_label);
    __ CompareObjectType(input, input, scratch, FIRST_NONSTRING_TYPE);
    __ bge(false_label);
    __ lbz(ip, FieldMemOperand(input, Map::kBitFieldOffset));
    __ ExtractBit(r0, ip, Map::kIsUndetectable);
    __ cmpi(r0, Operand::Zero());
    final_branch_condition = eq;

  } else if (type_name->Equals(heap()->symbol_string())) {
    __ JumpIfSmi(input, false_label);
    __ CompareObjectType(input, input, scratch, SYMBOL_TYPE);
    final_branch_condition = eq;

  } else if (type_name->Equals(heap()->boolean_string())) {
    __ CompareRoot(input, Heap::kTrueValueRootIndex);
    __ beq(true_label);
    __ CompareRoot(input, Heap::kFalseValueRootIndex);
    final_branch_condition = eq;

  } else if (FLAG_harmony_typeof && type_name->Equals(heap()->null_string())) {
    __ CompareRoot(input, Heap::kNullValueRootIndex);
    final_branch_condition = eq;

  } else if (type_name->Equals(heap()->undefined_string())) {
    __ CompareRoot(input, Heap::kUndefinedValueRootIndex);
    __ beq(true_label);
    __ JumpIfSmi(input, false_label);
    // Check for undetectable objects => true.
    __ LoadP(input, FieldMemOperand(input, HeapObject::kMapOffset));
    __ lbz(ip, FieldMemOperand(input, Map::kBitFieldOffset));
    __ ExtractBit(r0, ip, Map::kIsUndetectable);
    __ cmpi(r0, Operand::Zero());
    final_branch_condition = ne;

  } else if (type_name->Equals(heap()->function_string())) {
    STATIC_ASSERT(NUM_OF_CALLABLE_SPEC_OBJECT_TYPES == 2);
    __ JumpIfSmi(input, false_label);
    __ CompareObjectType(input, scratch, input, JS_FUNCTION_TYPE);
    __ beq(true_label);
    __ cmpi(input, Operand(JS_FUNCTION_PROXY_TYPE));
    final_branch_condition = eq;

  } else if (type_name->Equals(heap()->object_string())) {
    __ JumpIfSmi(input, false_label);
    if (!FLAG_harmony_typeof) {
      __ CompareRoot(input, Heap::kNullValueRootIndex);
      __ beq(true_label);
    }
    __ CompareObjectType(input, input, scratch,
                         FIRST_NONCALLABLE_SPEC_OBJECT_TYPE);
    __ blt(false_label);
    __ CompareInstanceType(input, scratch, LAST_NONCALLABLE_SPEC_OBJECT_TYPE);
    __ bgt(false_label);
    // Check for undetectable objects => false.
    __ lbz(ip, FieldMemOperand(input, Map::kBitFieldOffset));
    __ ExtractBit(r0, ip, Map::kIsUndetectable);
    __ cmpi(r0, Operand::Zero());
    final_branch_condition = eq;

  } else {
    __ b(false_label);
  }

  return final_branch_condition;
}


void LCodeGen::DoIsConstructCallAndBranch(LIsConstructCallAndBranch* instr) {
  Register temp1 = ToRegister(instr->temp());

  EmitIsConstructCall(temp1, scratch0());
  EmitBranch(instr, eq);
}


void LCodeGen::EmitIsConstructCall(Register temp1, Register temp2) {
  ASSERT(!temp1.is(temp2));
  // Get the frame pointer for the calling frame.
  __ LoadP(temp1, MemOperand(fp, StandardFrameConstants::kCallerFPOffset));

  // Skip the arguments adaptor frame if it exists.
  Label check_frame_marker;
  __ LoadP(temp2, MemOperand(temp1, StandardFrameConstants::kContextOffset));
  __ CmpSmiLiteral(temp2, Smi::FromInt(StackFrame::ARGUMENTS_ADAPTOR), r0);
  __ bne(&check_frame_marker);
  __ LoadP(temp1, MemOperand(temp1, StandardFrameConstants::kCallerFPOffset));

  // Check the marker in the calling frame.
  __ bind(&check_frame_marker);
  __ LoadP(temp1, MemOperand(temp1, StandardFrameConstants::kMarkerOffset));
  __ CmpSmiLiteral(temp1, Smi::FromInt(StackFrame::CONSTRUCT), r0);
}


void LCodeGen::EnsureSpaceForLazyDeopt(int space_needed) {
  if (info()->IsStub()) return;
  // Ensure that we have enough space after the previous lazy-bailout
  // instruction for patching the code here.
  int current_pc = masm()->pc_offset();
  if (current_pc < last_lazy_deopt_pc_ + space_needed) {
    int padding_size = last_lazy_deopt_pc_ + space_needed - current_pc;
    ASSERT_EQ(0, padding_size % Assembler::kInstrSize);
    while (padding_size > 0) {
      __ nop();
      padding_size -= Assembler::kInstrSize;
    }
  }
}


void LCodeGen::DoLazyBailout(LLazyBailout* instr) {
  EnsureSpaceForLazyDeopt(Deoptimizer::patch_size());
  last_lazy_deopt_pc_ = masm()->pc_offset();
  ASSERT(instr->HasEnvironment());
  LEnvironment* env = instr->environment();
  RegisterEnvironmentForDeoptimization(env, Safepoint::kLazyDeopt);
  safepoints_.RecordLazyDeoptimizationIndex(env->deoptimization_index());
}


void LCodeGen::DoDeoptimize(LDeoptimize* instr) {
  Deoptimizer::BailoutType type = instr->hydrogen()->type();
  // TODO(danno): Stubs expect all deopts to be lazy for historical reasons (the
  // needed return address), even though the implementation of LAZY and EAGER is
  // now identical. When LAZY is eventually completely folded into EAGER, remove
  // the special case below.
  if (info()->IsStub() && type == Deoptimizer::EAGER) {
    type = Deoptimizer::LAZY;
  }

  Comment(";;; deoptimize: %s", instr->hydrogen()->reason());
  DeoptimizeIf(al, instr->environment(), type);
}


void LCodeGen::DoDummyUse(LDummyUse* instr) {
  // Nothing to see here, move on!
}


void LCodeGen::DoDeferredStackCheck(LStackCheck* instr) {
  PushSafepointRegistersScope scope(this, Safepoint::kWithRegisters);
  LoadContextFromDeferred(instr->context());
  __ CallRuntimeSaveDoubles(Runtime::kStackGuard);
  RecordSafepointWithLazyDeopt(
      instr, RECORD_SAFEPOINT_WITH_REGISTERS_AND_NO_ARGUMENTS);
  ASSERT(instr->HasEnvironment());
  LEnvironment* env = instr->environment();
  safepoints_.RecordLazyDeoptimizationIndex(env->deoptimization_index());
}


void LCodeGen::DoStackCheck(LStackCheck* instr) {
  class DeferredStackCheck V8_FINAL : public LDeferredCode {
   public:
    DeferredStackCheck(LCodeGen* codegen, LStackCheck* instr)
        : LDeferredCode(codegen), instr_(instr) { }
    virtual void Generate() V8_OVERRIDE {
      codegen()->DoDeferredStackCheck(instr_);
    }
    virtual LInstruction* instr() V8_OVERRIDE { return instr_; }
   private:
    LStackCheck* instr_;
  };

  ASSERT(instr->HasEnvironment());
  LEnvironment* env = instr->environment();
  // There is no LLazyBailout instruction for stack-checks. We have to
  // prepare for lazy deoptimization explicitly here.
  if (instr->hydrogen()->is_function_entry()) {
    // Perform stack overflow check.
    Label done;
    __ LoadRoot(ip, Heap::kStackLimitRootIndex);
    __ cmpl(sp, ip);
    __ bge(&done);
    ASSERT(instr->context()->IsRegister());
    ASSERT(ToRegister(instr->context()).is(cp));
    CallCode(isolate()->builtins()->StackCheck(),
              RelocInfo::CODE_TARGET,
              instr);
    EnsureSpaceForLazyDeopt(Deoptimizer::patch_size());
    last_lazy_deopt_pc_ = masm()->pc_offset();
    __ bind(&done);
    RegisterEnvironmentForDeoptimization(env, Safepoint::kLazyDeopt);
    safepoints_.RecordLazyDeoptimizationIndex(env->deoptimization_index());
  } else {
    ASSERT(instr->hydrogen()->is_backwards_branch());
    // Perform stack overflow check if this goto needs it before jumping.
    DeferredStackCheck* deferred_stack_check =
        new(zone()) DeferredStackCheck(this, instr);
    __ LoadRoot(ip, Heap::kStackLimitRootIndex);
    __ cmpl(sp, ip);
    __ blt(deferred_stack_check->entry());
    EnsureSpaceForLazyDeopt(Deoptimizer::patch_size());
    last_lazy_deopt_pc_ = masm()->pc_offset();
    __ bind(instr->done_label());
    deferred_stack_check->SetExit(instr->done_label());
    RegisterEnvironmentForDeoptimization(env, Safepoint::kLazyDeopt);
    // Don't record a deoptimization index for the safepoint here.
    // This will be done explicitly when emitting call and the safepoint in
    // the deferred code.
  }
}


void LCodeGen::DoOsrEntry(LOsrEntry* instr) {
  // This is a pseudo-instruction that ensures that the environment here is
  // properly registered for deoptimization and records the assembler's PC
  // offset.
  LEnvironment* environment = instr->environment();

  // If the environment were already registered, we would have no way of
  // backpatching it with the spill slot operands.
  ASSERT(!environment->HasBeenRegistered());
  RegisterEnvironmentForDeoptimization(environment, Safepoint::kNoLazyDeopt);

  GenerateOsrPrologue();
}


void LCodeGen::DoForInPrepareMap(LForInPrepareMap* instr) {
  __ LoadRoot(ip, Heap::kUndefinedValueRootIndex);
  __ cmp(r3, ip);
  DeoptimizeIf(eq, instr->environment());

  Register null_value = r8;
  __ LoadRoot(null_value, Heap::kNullValueRootIndex);
  __ cmp(r3, null_value);
  DeoptimizeIf(eq, instr->environment());

  __ TestIfSmi(r3, r0);
  DeoptimizeIf(eq, instr->environment(), cr0);

  STATIC_ASSERT(FIRST_JS_PROXY_TYPE == FIRST_SPEC_OBJECT_TYPE);
  __ CompareObjectType(r3, r4, r4, LAST_JS_PROXY_TYPE);
  DeoptimizeIf(le, instr->environment());

  Label use_cache, call_runtime;
  __ CheckEnumCache(null_value, &call_runtime);

  __ LoadP(r3, FieldMemOperand(r3, HeapObject::kMapOffset));
  __ b(&use_cache);

  // Get the set of properties to enumerate.
  __ bind(&call_runtime);
  __ push(r3);
  CallRuntime(Runtime::kGetPropertyNamesFast, 1, instr);

  __ LoadP(r4, FieldMemOperand(r3, HeapObject::kMapOffset));
  __ LoadRoot(ip, Heap::kMetaMapRootIndex);
  __ cmp(r4, ip);
  DeoptimizeIf(ne, instr->environment());
  __ bind(&use_cache);
}


void LCodeGen::DoForInCacheArray(LForInCacheArray* instr) {
  Register map = ToRegister(instr->map());
  Register result = ToRegister(instr->result());
  Label load_cache, done;
  __ EnumLength(result, map);
  __ CmpSmiLiteral(result, Smi::FromInt(0), r0);
  __ bne(&load_cache);
  __ mov(result, Operand(isolate()->factory()->empty_fixed_array()));
  __ b(&done);

  __ bind(&load_cache);
  __ LoadInstanceDescriptors(map, result);
  __ LoadP(result,
           FieldMemOperand(result, DescriptorArray::kEnumCacheOffset));
  __ LoadP(result,
           FieldMemOperand(result, FixedArray::SizeFor(instr->idx())));
  __ cmpi(result, Operand::Zero());
  DeoptimizeIf(eq, instr->environment());

  __ bind(&done);
}


void LCodeGen::DoCheckMapValue(LCheckMapValue* instr) {
  Register object = ToRegister(instr->value());
  Register map = ToRegister(instr->map());
  __ LoadP(scratch0(), FieldMemOperand(object, HeapObject::kMapOffset));
  __ cmp(map, scratch0());
  DeoptimizeIf(ne, instr->environment());
}


void LCodeGen::DoLoadFieldByIndex(LLoadFieldByIndex* instr) {
  Register object = ToRegister(instr->object());
  Register index = ToRegister(instr->index());
  Register result = ToRegister(instr->result());
  Register scratch = scratch0();

  Label out_of_object, done;
  __ cmpi(index, Operand::Zero());
  __ blt(&out_of_object);

  __ SmiToPtrArrayOffset(r0, index);
  __ add(scratch, object, r0);
  __ LoadP(result, FieldMemOperand(scratch, JSObject::kHeaderSize));

  __ b(&done);

  __ bind(&out_of_object);
  __ LoadP(result, FieldMemOperand(object, JSObject::kPropertiesOffset));
  // Index is equal to negated out of object property index plus 1.
  __ SmiToPtrArrayOffset(r0, index);
  __ sub(scratch, result, r0);
  __ LoadP(result, FieldMemOperand(scratch,
                                   FixedArray::kHeaderSize - kPointerSize));
  __ bind(&done);
}


#undef __

} }  // namespace v8::internal
