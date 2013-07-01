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

#include "code-stubs.h"
#include "codegen.h"
#include "compiler.h"
#include "debug.h"
#include "full-codegen.h"
#include "isolate-inl.h"
#include "parser.h"
#include "scopes.h"
#include "stub-cache.h"

#include "ppc/code-stubs-ppc.h"
#include "ppc/macro-assembler-ppc.h"

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm_)

#define EMIT_STUB_MARKER(stub_marker) __ marker_asm(stub_marker)

// A patch site is a location in the code which it is possible to patch. This
// class has a number of methods to emit the code which is patchable and the
// method EmitPatchInfo to record a marker back to the patchable code. This
// marker is a cmpi rx, #yyy instruction, and x * 0x0000ffff + yyy (raw 16 bit
// immediate value is used) is the delta from the pc to the first instruction of
// the patchable code.
// See PatchInlinedSmiCode in ic-ppc.cc for the code that patches it
class JumpPatchSite BASE_EMBEDDED {
 public:
  explicit JumpPatchSite(MacroAssembler* masm) : masm_(masm) {
#ifdef DEBUG
    info_emitted_ = false;
#endif
  }

  ~JumpPatchSite() {
    ASSERT(patch_site_.is_bound() == info_emitted_);
  }

  // When initially emitting this ensure that a jump is always generated to skip
  // the inlined smi code.
  void EmitJumpIfNotSmi(Register reg, Label* target) {
    EMIT_STUB_MARKER(200);
    ASSERT(!patch_site_.is_bound() && !info_emitted_);
    Assembler::BlockConstPoolScope block_const_pool(masm_);
    __ bind(&patch_site_);
    __ cmp(reg, reg, cr0);
    __ beq(target, cr0);  // Always taken before patched.
  }

  // When initially emitting this ensure that a jump is never generated to skip
  // the inlined smi code.
  void EmitJumpIfSmi(Register reg, Label* target) {
    EMIT_STUB_MARKER(201);
    ASSERT(!patch_site_.is_bound() && !info_emitted_);
    Assembler::BlockConstPoolScope block_const_pool(masm_);
    __ bind(&patch_site_);
    __ cmp(reg, reg, cr0);
    __ bne(target, cr0);  // Never taken before patched.
  }

  void EmitPatchInfo() {
    // Block literal pool emission whilst recording patch site information.
    Assembler::BlockConstPoolScope block_const_pool(masm_);
    if (patch_site_.is_bound()) {
      int delta_to_patch_site = masm_->InstructionsGeneratedSince(&patch_site_);
      Register reg;
      // I believe this is using reg as the high bits of of the offset
      reg.set_code(delta_to_patch_site / kOff16Mask);
      __ cmpi(reg, Operand(delta_to_patch_site % kOff16Mask));
#ifdef DEBUG
      info_emitted_ = true;
#endif
    } else {
      __ nop();  // Signals no inlined code.
    }
  }

 private:
  MacroAssembler* masm_;
  Label patch_site_;
#ifdef DEBUG
  bool info_emitted_;
#endif
};


// Generate code for a JS function.  On entry to the function the receiver
// and arguments have been pushed on the stack left to right.  The actual
// argument count matches the formal parameter count expected by the
// function.
//
// The live registers are:
//   o r4: the JS function object being called (i.e., ourselves)
//   o cp: our context
//   o fp: our caller's frame pointer r11 (ARM specific) PPC is fp (aka r31)
//   o sp: stack pointer
//   o lr: return address  (bogus.. PPC has no lr reg)
//
// The function builds a JS frame.  Please see JavaScriptFrameConstants in
// frames-ppc.h for its layout.
void FullCodeGenerator::Generate() {
  EMIT_STUB_MARKER(202);
  CompilationInfo* info = info_;
  handler_table_ =
      isolate()->factory()->NewFixedArray(function()->handler_count(), TENURED);
  profiling_counter_ = isolate()->factory()->NewJSGlobalPropertyCell(
      Handle<Smi>(Smi::FromInt(FLAG_interrupt_budget)));
  SetFunctionPosition(function());
  Comment cmnt(masm_, "[ function compiled by full code generator");

  ProfileEntryHookStub::MaybeCallEntryHook(masm_);

#ifdef DEBUG
  if (strlen(FLAG_stop_at) > 0 &&
      info->function()->name()->IsEqualTo(CStrVector(FLAG_stop_at))) {
    __ stop("stop-at");
  }
#endif

  // Strict mode functions and builtins need to replace the receiver
  // with undefined when called as functions (without an explicit
  // receiver object). r8 is zero for method calls and non-zero for
  // function calls.
  if (!info->is_classic_mode() || info->is_native()) {
    Label ok;
    __ cmpi(r8, Operand(0));
    __ beq(&ok);
    int receiver_offset = info->scope()->num_parameters() * kPointerSize;
    __ LoadRoot(r5, Heap::kUndefinedValueRootIndex);
    __ StoreWord(r5, MemOperand(sp, receiver_offset), r0);
    __ bind(&ok);
  }

  // Open a frame scope to indicate that there is a frame on the stack.  The
  // MANUAL indicates that the scope shouldn't actually generate code to set up
  // the frame (that is done below).
  FrameScope frame_scope(masm_, StackFrame::MANUAL);

  int locals_count = info->scope()->num_stack_slots();

  __ mflr(r0);
  __ Push(r0, fp, cp, r4);

  if (locals_count > 0) {
    // Load undefined value here, so the value is ready for the loop
    // below.
    __ LoadRoot(ip, Heap::kUndefinedValueRootIndex);
  }
  // Adjust fp to point to caller's fp.
  __ addi(fp, sp, Operand(2 * kPointerSize));

  { Comment cmnt(masm_, "[ Allocate locals");
    for (int i = 0; i < locals_count; i++) {
      __ push(ip);
    }
  }

  bool function_in_register = true;

  // Possibly allocate a local context.
  int heap_slots = info->scope()->num_heap_slots() - Context::MIN_CONTEXT_SLOTS;
  if (heap_slots > 0) {
    // Argument to NewContext is the function, which is still in r4.
    Comment cmnt(masm_, "[ Allocate context");
    __ push(r4);
    if (FLAG_harmony_scoping && info->scope()->is_global_scope()) {
      __ Push(info->scope()->GetScopeInfo());
      __ CallRuntime(Runtime::kNewGlobalContext, 2);
    } else if (heap_slots <= FastNewContextStub::kMaximumSlots) {
      FastNewContextStub stub(heap_slots);
      __ CallStub(&stub);
    } else {
      __ CallRuntime(Runtime::kNewFunctionContext, 1);
    }
    function_in_register = false;
    // Context is returned in both r3 and cp.  It replaces the context
    // passed to us.  It's saved in the stack and kept live in cp.
    __ stw(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
    // Copy any necessary parameters into the context.
    int num_parameters = info->scope()->num_parameters();
    for (int i = 0; i < num_parameters; i++) {
      Variable* var = scope()->parameter(i);
      if (var->IsContextSlot()) {
        int parameter_offset = StandardFrameConstants::kCallerSPOffset +
            (num_parameters - 1 - i) * kPointerSize;
        // Load parameter from stack.
        __ LoadWord(r3, MemOperand(fp, parameter_offset), r0);
        // Store it in the context.
        MemOperand target = ContextOperand(cp, var->index());
        __ StoreWord(r3, target, r0);

        // Update the write barrier.
        __ RecordWriteContextSlot(
            cp, target.offset(), r3, r6, kLRHasBeenSaved, kDontSaveFPRegs);
      }
    }
  }

  Variable* arguments = scope()->arguments();
  if (arguments != NULL) {
    // Function uses arguments object.
    Comment cmnt(masm_, "[ Allocate arguments object");
    if (!function_in_register) {
      // Load this again, if it's used by the local context below.
      __ lwz(r6, MemOperand(fp, JavaScriptFrameConstants::kFunctionOffset));
    } else {
      __ mr(r6, r4);
    }
    // Receiver is just before the parameters on the caller's stack.
    int num_parameters = info->scope()->num_parameters();
    int offset = num_parameters * kPointerSize;
    __ addi(r5, fp,
            Operand(StandardFrameConstants::kCallerSPOffset + offset));
    __ li(r4, Operand(Smi::FromInt(num_parameters)));
    __ Push(r6, r5, r4);

    // Arguments to ArgumentsAccessStub:
    //   function, receiver address, parameter count.
    // The stub will rewrite receiever and parameter count if the previous
    // stack frame was an arguments adapter frame.
    ArgumentsAccessStub::Type type;
    if (!is_classic_mode()) {
      type = ArgumentsAccessStub::NEW_STRICT;
    } else if (function()->has_duplicate_parameters()) {
      type = ArgumentsAccessStub::NEW_NON_STRICT_SLOW;
    } else {
      type = ArgumentsAccessStub::NEW_NON_STRICT_FAST;
    }
    ArgumentsAccessStub stub(type);
    __ CallStub(&stub);

    SetVar(arguments, r3, r4, r5);
  }

  if (FLAG_trace) {
    __ CallRuntime(Runtime::kTraceEnter, 0);
  }

  // Visit the declarations and body unless there is an illegal
  // redeclaration.
  if (scope()->HasIllegalRedeclaration()) {
    Comment cmnt(masm_, "[ Declarations");
    scope()->VisitIllegalRedeclaration(this);

  } else {
    PrepareForBailoutForId(BailoutId::FunctionEntry(), NO_REGISTERS);
    { Comment cmnt(masm_, "[ Declarations");
      // For named function expressions, declare the function name as a
      // constant.
      if (scope()->is_function_scope() && scope()->function() != NULL) {
        VariableDeclaration* function = scope()->function();
        ASSERT(function->proxy()->var()->mode() == CONST ||
               function->proxy()->var()->mode() == CONST_HARMONY);
        ASSERT(function->proxy()->var()->location() != Variable::UNALLOCATED);
        VisitVariableDeclaration(function);
      }
      VisitDeclarations(scope()->declarations());
    }

    { Comment cmnt(masm_, "[ Stack check");
      PrepareForBailoutForId(BailoutId::Declarations(), NO_REGISTERS);
      Label ok;
      __ LoadRoot(ip, Heap::kStackLimitRootIndex);
      __ cmpl(sp, ip);
      __ bge(&ok);
      StackCheckStub stub;
      __ CallStub(&stub);
      __ bind(&ok);
    }

    { Comment cmnt(masm_, "[ Body");
      ASSERT(loop_depth() == 0);
      VisitStatements(function()->body());
      ASSERT(loop_depth() == 0);
    }
  }

  // Always emit a 'return undefined' in case control fell off the end of
  // the body.
  { Comment cmnt(masm_, "[ return <undefined>;");
    __ LoadRoot(r3, Heap::kUndefinedValueRootIndex);
  }
  EmitReturnSequence();

  // Force emit the constant pool, so it doesn't get emitted in the middle
  // of the stack check table.
  masm()->CheckConstPool(true, false);
}


void FullCodeGenerator::ClearAccumulator() {
  __ li(r3, Operand(Smi::FromInt(0)));
}


void FullCodeGenerator::EmitProfilingCounterDecrement(int delta) {
  EMIT_STUB_MARKER(203);
  __ mov(r5, Operand(profiling_counter_));
  __ lwz(r6, FieldMemOperand(r5, JSGlobalPropertyCell::kValueOffset));
  __ sub(r6, r6, Operand(Smi::FromInt(delta)));
  __ stw(r6, FieldMemOperand(r5, JSGlobalPropertyCell::kValueOffset));
  __ cmpi(r6, Operand(0));
}


void FullCodeGenerator::EmitProfilingCounterReset() {
  EMIT_STUB_MARKER(204);
  int reset_value = FLAG_interrupt_budget;
  if (info_->ShouldSelfOptimize() && !FLAG_retry_self_opt) {
    // Self-optimization is a one-off thing: if it fails, don't try again.
    reset_value = Smi::kMaxValue;
  }
  if (isolate()->IsDebuggerActive()) {
    // Detect debug break requests as soon as possible.
    reset_value = FLAG_interrupt_budget >> 4;
  }
  __ mov(r5, Operand(profiling_counter_));
  __ mov(r6, Operand(Smi::FromInt(reset_value)));
  __ stw(r6, FieldMemOperand(r5, JSGlobalPropertyCell::kValueOffset));
}


void FullCodeGenerator::EmitStackCheck(IterationStatement* stmt,
                                       Label* back_edge_target) {
  EMIT_STUB_MARKER(205);
  Comment cmnt(masm_, "[ Stack check");
  // Block literal pools whilst emitting stack check code.
  Assembler::BlockConstPoolScope block_const_pool(masm_);
  Label ok;

  if (FLAG_count_based_interrupts) {
    int weight = 1;
    if (FLAG_weighted_back_edges) {
      ASSERT(back_edge_target->is_bound());
      int distance = masm_->SizeOfCodeGeneratedSince(back_edge_target);
      weight = Min(kMaxBackEdgeWeight,
                   Max(1, distance / kBackEdgeDistanceUnit));
    }
    EmitProfilingCounterDecrement(weight);
    __ bge(&ok);
    InterruptStub stub;
    __ CallStub(&stub);
  } else {
    __ LoadRoot(ip, Heap::kStackLimitRootIndex);
    __ cmpl(sp, ip);
    __ bge(&ok);
    StackCheckStub stub;
    __ CallStub(&stub);
  }

  // Record a mapping of this PC offset to the OSR id.  This is used to find
  // the AST id from the unoptimized code in order to use it as a key into
  // the deoptimization input data found in the optimized code.
  RecordStackCheck(stmt->OsrEntryId());

  if (FLAG_count_based_interrupts) {
    EmitProfilingCounterReset();
  }

  __ bind(&ok);
  PrepareForBailoutForId(stmt->EntryId(), NO_REGISTERS);
  // Record a mapping of the OSR id to this PC.  This is used if the OSR
  // entry becomes the target of a bailout.  We don't expect it to be, but
  // we want it to work if it is.
  PrepareForBailoutForId(stmt->OsrEntryId(), NO_REGISTERS);
}


void FullCodeGenerator::EmitReturnSequence() {
  EMIT_STUB_MARKER(206);
  Comment cmnt(masm_, "[ Return sequence");
  if (return_label_.is_bound()) {
    __ b(&return_label_);
  } else {
    __ bind(&return_label_);
    if (FLAG_trace) {
      // Push the return value on the stack as the parameter.
      // Runtime::TraceExit returns its parameter in r3
      __ push(r3);
      __ CallRuntime(Runtime::kTraceExit, 1);
    }
    if (FLAG_interrupt_at_exit || FLAG_self_optimization) {
      // Pretend that the exit is a backwards jump to the entry.
      int weight = 1;
      if (info_->ShouldSelfOptimize()) {
        weight = FLAG_interrupt_budget / FLAG_self_opt_count;
      } else if (FLAG_weighted_back_edges) {
        int distance = masm_->pc_offset();
        weight = Min(kMaxBackEdgeWeight,
                     Max(1, distance / kBackEdgeDistanceUnit));
      }
      EmitProfilingCounterDecrement(weight);
      Label ok;
      __ bge(&ok);
      __ push(r3);
      if (info_->ShouldSelfOptimize() && FLAG_direct_self_opt) {
        __ lwz(r5, MemOperand(fp, JavaScriptFrameConstants::kFunctionOffset));
        __ push(r5);
        __ CallRuntime(Runtime::kOptimizeFunctionOnNextCall, 1);
      } else {
        InterruptStub stub;
        __ CallStub(&stub);
      }
      __ pop(r3);
      EmitProfilingCounterReset();
      __ bind(&ok);
    }

#ifdef DEBUG
    // Add a label for checking the size of the code used for returning.
    Label check_exit_codesize;
    masm_->bind(&check_exit_codesize);
#endif
    // Make sure that the constant pool is not emitted inside of the return
    // sequence.
    { Assembler::BlockConstPoolScope block_const_pool(masm_);
      // Here we use masm_-> instead of the __ macro to avoid the code coverage
      // tool from instrumenting as we rely on the code size here.
      int32_t sp_delta = (info_->scope()->num_parameters() + 1) * kPointerSize;
      CodeGenerator::RecordPositions(masm_, function()->end_position() - 1);
      __ RecordJSReturn();
      masm_->mr(sp, fp);
      masm_->lwz(fp, MemOperand(sp));
      masm_->lwz(r0, MemOperand(sp, kPointerSize));
      masm_->mtlr(r0);
      masm_->Add(sp, sp, (uint32_t)(sp_delta + (2 * kPointerSize)), r0);
      masm_->blr();
    }

#ifdef DEBUG
    // Check that the size of the code used for returning is large enough
    // for the debugger's requirements.
    ASSERT(Assembler::kJSReturnSequenceInstructions <=
           masm_->InstructionsGeneratedSince(&check_exit_codesize));
#endif
  }
}


void FullCodeGenerator::EffectContext::Plug(Variable* var) const {
  ASSERT(var->IsStackAllocated() || var->IsContextSlot());
}


void FullCodeGenerator::AccumulatorValueContext::Plug(Variable* var) const {
  ASSERT(var->IsStackAllocated() || var->IsContextSlot());
  codegen()->GetVar(result_register(), var);
}


void FullCodeGenerator::StackValueContext::Plug(Variable* var) const {
  ASSERT(var->IsStackAllocated() || var->IsContextSlot());
  codegen()->GetVar(result_register(), var);
  __ push(result_register());
}


void FullCodeGenerator::TestContext::Plug(Variable* var) const {
  ASSERT(var->IsStackAllocated() || var->IsContextSlot());
  // For simplicity we always test the accumulator register.
  codegen()->GetVar(result_register(), var);
  codegen()->PrepareForBailoutBeforeSplit(condition(), false, NULL, NULL);
  codegen()->DoTest(this);
}


void FullCodeGenerator::EffectContext::Plug(Heap::RootListIndex index) const {
}


void FullCodeGenerator::AccumulatorValueContext::Plug(
    Heap::RootListIndex index) const {
  __ LoadRoot(result_register(), index);
}


void FullCodeGenerator::StackValueContext::Plug(
    Heap::RootListIndex index) const {
  __ LoadRoot(result_register(), index);
  __ push(result_register());
}


void FullCodeGenerator::TestContext::Plug(Heap::RootListIndex index) const {
  codegen()->PrepareForBailoutBeforeSplit(condition(),
                                          true,
                                          true_label_,
                                          false_label_);
  if (index == Heap::kUndefinedValueRootIndex ||
      index == Heap::kNullValueRootIndex ||
      index == Heap::kFalseValueRootIndex) {
    if (false_label_ != fall_through_) __ b(false_label_);
  } else if (index == Heap::kTrueValueRootIndex) {
    if (true_label_ != fall_through_) __ b(true_label_);
  } else {
    __ LoadRoot(result_register(), index);
    codegen()->DoTest(this);
  }
}


void FullCodeGenerator::EffectContext::Plug(Handle<Object> lit) const {
}


void FullCodeGenerator::AccumulatorValueContext::Plug(
    Handle<Object> lit) const {
  __ mov(result_register(), Operand(lit));
}


void FullCodeGenerator::StackValueContext::Plug(Handle<Object> lit) const {
  // Immediates cannot be pushed directly.
  __ mov(result_register(), Operand(lit));
  __ push(result_register());
}


void FullCodeGenerator::TestContext::Plug(Handle<Object> lit) const {
  codegen()->PrepareForBailoutBeforeSplit(condition(),
                                          true,
                                          true_label_,
                                          false_label_);
  ASSERT(!lit->IsUndetectableObject());  // There are no undetectable literals.
  if (lit->IsUndefined() || lit->IsNull() || lit->IsFalse()) {
    if (false_label_ != fall_through_) __ b(false_label_);
  } else if (lit->IsTrue() || lit->IsJSObject()) {
    if (true_label_ != fall_through_) __ b(true_label_);
  } else if (lit->IsString()) {
    if (String::cast(*lit)->length() == 0) {
      if (false_label_ != fall_through_) __ b(false_label_);
    } else {
      if (true_label_ != fall_through_) __ b(true_label_);
    }
  } else if (lit->IsSmi()) {
    if (Smi::cast(*lit)->value() == 0) {
      if (false_label_ != fall_through_) __ b(false_label_);
    } else {
      if (true_label_ != fall_through_) __ b(true_label_);
    }
  } else {
    // For simplicity we always test the accumulator register.
    __ mov(result_register(), Operand(lit));
    codegen()->DoTest(this);
  }
}


void FullCodeGenerator::EffectContext::DropAndPlug(int count,
                                                   Register reg) const {
  ASSERT(count > 0);
  __ Drop(count);
}


void FullCodeGenerator::AccumulatorValueContext::DropAndPlug(
    int count,
    Register reg) const {
  ASSERT(count > 0);
  __ Drop(count);
  __ Move(result_register(), reg);
}


void FullCodeGenerator::StackValueContext::DropAndPlug(int count,
                                                       Register reg) const {
  ASSERT(count > 0);
  if (count > 1) __ Drop(count - 1);
  __ stw(reg, MemOperand(sp, 0));
}


void FullCodeGenerator::TestContext::DropAndPlug(int count,
                                                 Register reg) const {
  ASSERT(count > 0);
  // For simplicity we always test the accumulator register.
  __ Drop(count);
  __ Move(result_register(), reg);
  codegen()->PrepareForBailoutBeforeSplit(condition(), false, NULL, NULL);
  codegen()->DoTest(this);
}


void FullCodeGenerator::EffectContext::Plug(Label* materialize_true,
                                            Label* materialize_false) const {
  ASSERT(materialize_true == materialize_false);
  __ bind(materialize_true);
}


void FullCodeGenerator::AccumulatorValueContext::Plug(
    Label* materialize_true,
    Label* materialize_false) const {
  EMIT_STUB_MARKER(207);
  Label done;
  __ bind(materialize_true);
  __ LoadRoot(result_register(), Heap::kTrueValueRootIndex);
  __ jmp(&done);
  __ bind(materialize_false);
  __ LoadRoot(result_register(), Heap::kFalseValueRootIndex);
  __ bind(&done);
}


void FullCodeGenerator::StackValueContext::Plug(
    Label* materialize_true,
    Label* materialize_false) const {
  EMIT_STUB_MARKER(208);
  Label done;
  __ bind(materialize_true);
  __ LoadRoot(ip, Heap::kTrueValueRootIndex);
  __ push(ip);
  __ jmp(&done);
  __ bind(materialize_false);
  __ LoadRoot(ip, Heap::kFalseValueRootIndex);
  __ push(ip);
  __ bind(&done);
}


void FullCodeGenerator::TestContext::Plug(Label* materialize_true,
                                          Label* materialize_false) const {
  ASSERT(materialize_true == true_label_);
  ASSERT(materialize_false == false_label_);
}


void FullCodeGenerator::EffectContext::Plug(bool flag) const {
}


void FullCodeGenerator::AccumulatorValueContext::Plug(bool flag) const {
  Heap::RootListIndex value_root_index =
      flag ? Heap::kTrueValueRootIndex : Heap::kFalseValueRootIndex;
  __ LoadRoot(result_register(), value_root_index);
}


void FullCodeGenerator::StackValueContext::Plug(bool flag) const {
  Heap::RootListIndex value_root_index =
      flag ? Heap::kTrueValueRootIndex : Heap::kFalseValueRootIndex;
  __ LoadRoot(ip, value_root_index);
  __ push(ip);
}


void FullCodeGenerator::TestContext::Plug(bool flag) const {
  codegen()->PrepareForBailoutBeforeSplit(condition(),
                                          true,
                                          true_label_,
                                          false_label_);
  if (flag) {
    if (true_label_ != fall_through_) __ b(true_label_);
  } else {
    if (false_label_ != fall_through_) __ b(false_label_);
  }
}


void FullCodeGenerator::DoTest(Expression* condition,
                               Label* if_true,
                               Label* if_false,
                               Label* fall_through) {
  EMIT_STUB_MARKER(209);
  ToBooleanStub stub(result_register());
  __ CallStub(&stub);
  __ cmpi(result_register(), Operand(0));
  Split(ne, if_true, if_false, fall_through);
}


void FullCodeGenerator::Split(Condition cond,
                              Label* if_true,
                              Label* if_false,
                              Label* fall_through) {
  EMIT_STUB_MARKER(210);
  if (if_false == fall_through) {
    __ b(cond, if_true);
  } else if (if_true == fall_through) {
    __ b(NegateCondition(cond), if_false);
  } else {
    __ b(cond, if_true);
    __ b(if_false);
  }
}


MemOperand FullCodeGenerator::StackOperand(Variable* var) {
  ASSERT(var->IsStackAllocated());
  // Offset is negative because higher indexes are at lower addresses.
  int offset = -var->index() * kPointerSize;
  // Adjust by a (parameter or local) base offset.
  if (var->IsParameter()) {
    offset += (info_->scope()->num_parameters() + 1) * kPointerSize;
  } else {
    offset += JavaScriptFrameConstants::kLocal0Offset;
  }
  return MemOperand(fp, offset);
}


MemOperand FullCodeGenerator::VarOperand(Variable* var, Register scratch) {
  ASSERT(var->IsContextSlot() || var->IsStackAllocated());
  if (var->IsContextSlot()) {
    int context_chain_length = scope()->ContextChainLength(var->scope());
    __ LoadContext(scratch, context_chain_length);
    return ContextOperand(scratch, var->index());
  } else {
    return StackOperand(var);
  }
}


void FullCodeGenerator::GetVar(Register dest, Variable* var) {
  // Use destination as scratch.
  MemOperand location = VarOperand(var, dest);
  __ LoadWord(dest, location, r0);
}


void FullCodeGenerator::SetVar(Variable* var,
                               Register src,
                               Register scratch0,
                               Register scratch1) {
  ASSERT(var->IsContextSlot() || var->IsStackAllocated());
  ASSERT(!scratch0.is(src));
  ASSERT(!scratch0.is(scratch1));
  ASSERT(!scratch1.is(src));
  MemOperand location = VarOperand(var, scratch0);
  __ StoreWord(src, location, r0);

  // Emit the write barrier code if the location is in the heap.
  if (var->IsContextSlot()) {
    __ RecordWriteContextSlot(scratch0,
                              location.offset(),
                              src,
                              scratch1,
                              kLRHasBeenSaved,
                              kDontSaveFPRegs);
  }
}


void FullCodeGenerator::PrepareForBailoutBeforeSplit(Expression* expr,
                                                     bool should_normalize,
                                                     Label* if_true,
                                                     Label* if_false) {
  EMIT_STUB_MARKER(211);
  // Only prepare for bailouts before splits if we're in a test
  // context. Otherwise, we let the Visit function deal with the
  // preparation to avoid preparing with the same AST id twice.
  if (!context()->IsTest() || !info_->IsOptimizable()) return;

  Label skip;
  if (should_normalize) __ b(&skip);
  PrepareForBailout(expr, TOS_REG);
  if (should_normalize) {
    __ LoadRoot(ip, Heap::kTrueValueRootIndex);
    __ cmp(r3, ip);
    Split(eq, if_true, if_false, NULL);
    __ bind(&skip);
  }
}


void FullCodeGenerator::EmitDebugCheckDeclarationContext(Variable* variable) {
  EMIT_STUB_MARKER(212);
  // The variable in the declaration always resides in the current function
  // context.
  ASSERT_EQ(0, scope()->ContextChainLength(variable->scope()));
  if (generate_debug_code_) {
    // Check that we're not inside a with or catch context.
    __ lwz(r4, FieldMemOperand(cp, HeapObject::kMapOffset));
    __ CompareRoot(r4, Heap::kWithContextMapRootIndex);
    __ Check(ne, "Declaration in with context.");
    __ CompareRoot(r4, Heap::kCatchContextMapRootIndex);
    __ Check(ne, "Declaration in catch context.");
  }
}


void FullCodeGenerator::VisitVariableDeclaration(
    VariableDeclaration* declaration) {
  EMIT_STUB_MARKER(213);
  // If it was not possible to allocate the variable at compile time, we
  // need to "declare" it at runtime to make sure it actually exists in the
  // local context.
  VariableProxy* proxy = declaration->proxy();
  VariableMode mode = declaration->mode();
  Variable* variable = proxy->var();
  bool hole_init = mode == CONST || mode == CONST_HARMONY || mode == LET;
  switch (variable->location()) {
    case Variable::UNALLOCATED:
      globals_->Add(variable->name(), zone());
      globals_->Add(variable->binding_needs_init()
                        ? isolate()->factory()->the_hole_value()
                        : isolate()->factory()->undefined_value(),
                    zone());
      break;

    case Variable::PARAMETER:
    case Variable::LOCAL:
      if (hole_init) {
        Comment cmnt(masm_, "[ VariableDeclaration");
        __ LoadRoot(ip, Heap::kTheHoleValueRootIndex);
        __ stw(ip, StackOperand(variable));
      }
      break;

    case Variable::CONTEXT:
      if (hole_init) {
        Comment cmnt(masm_, "[ VariableDeclaration");
        EmitDebugCheckDeclarationContext(variable);
        __ LoadRoot(ip, Heap::kTheHoleValueRootIndex);
        __ StoreWord(ip, ContextOperand(cp, variable->index()), r0);
        // No write barrier since the_hole_value is in old space.
        PrepareForBailoutForId(proxy->id(), NO_REGISTERS);
      }
      break;

    case Variable::LOOKUP: {
      Comment cmnt(masm_, "[ VariableDeclaration");
      __ mov(r5, Operand(variable->name()));
      // Declaration nodes are always introduced in one of four modes.
      ASSERT(IsDeclaredVariableMode(mode));
      PropertyAttributes attr =
          IsImmutableVariableMode(mode) ? READ_ONLY : NONE;
      __ mov(r4, Operand(Smi::FromInt(attr)));
      // Push initial value, if any.
      // Note: For variables we must not push an initial value (such as
      // 'undefined') because we may have a (legal) redeclaration and we
      // must not destroy the current value.
      if (hole_init) {
        __ LoadRoot(r3, Heap::kTheHoleValueRootIndex);
        __ Push(cp, r5, r4, r3);
      } else {
        __ li(r3, Operand(Smi::FromInt(0)));  // Indicates no initial value.
        __ Push(cp, r5, r4, r3);
      }
      __ CallRuntime(Runtime::kDeclareContextSlot, 4);
      break;
    }
  }
}


void FullCodeGenerator::VisitFunctionDeclaration(
    FunctionDeclaration* declaration) {
  EMIT_STUB_MARKER(214);
  VariableProxy* proxy = declaration->proxy();
  Variable* variable = proxy->var();
  switch (variable->location()) {
    case Variable::UNALLOCATED: {
      globals_->Add(variable->name(), zone());
      Handle<SharedFunctionInfo> function =
          Compiler::BuildFunctionInfo(declaration->fun(), script());
      // Check for stack-overflow exception.
      if (function.is_null()) return SetStackOverflow();
      globals_->Add(function, zone());
      break;
    }

    case Variable::PARAMETER:
    case Variable::LOCAL: {
      Comment cmnt(masm_, "[ FunctionDeclaration");
      VisitForAccumulatorValue(declaration->fun());
      __ stw(result_register(), StackOperand(variable));
      break;
    }

    case Variable::CONTEXT: {
      Comment cmnt(masm_, "[ FunctionDeclaration");
      EmitDebugCheckDeclarationContext(variable);
      VisitForAccumulatorValue(declaration->fun());
      __ StoreWord(result_register(),
                   ContextOperand(cp, variable->index()), r0);
      int offset = Context::SlotOffset(variable->index());
      // We know that we have written a function, which is not a smi.
      __ RecordWriteContextSlot(cp,
                                offset,
                                result_register(),
                                r5,
                                kLRHasBeenSaved,
                                kDontSaveFPRegs,
                                EMIT_REMEMBERED_SET,
                                OMIT_SMI_CHECK);
      PrepareForBailoutForId(proxy->id(), NO_REGISTERS);
      break;
    }

    case Variable::LOOKUP: {
      Comment cmnt(masm_, "[ FunctionDeclaration");
      __ mov(r5, Operand(variable->name()));
      __ li(r4, Operand(Smi::FromInt(NONE)));
      __ Push(cp, r5, r4);
      // Push initial value for function declaration.
      VisitForStackValue(declaration->fun());
      __ CallRuntime(Runtime::kDeclareContextSlot, 4);
      break;
    }
  }
}


void FullCodeGenerator::VisitModuleDeclaration(ModuleDeclaration* declaration) {
  EMIT_STUB_MARKER(215);
  VariableProxy* proxy = declaration->proxy();
  Variable* variable = proxy->var();
  Handle<JSModule> instance = declaration->module()->interface()->Instance();
  ASSERT(!instance.is_null());

  switch (variable->location()) {
    case Variable::UNALLOCATED: {
      Comment cmnt(masm_, "[ ModuleDeclaration");
      globals_->Add(variable->name(), zone());
      globals_->Add(instance, zone());
      Visit(declaration->module());
      break;
    }

    case Variable::CONTEXT: {
      Comment cmnt(masm_, "[ ModuleDeclaration");
      EmitDebugCheckDeclarationContext(variable);
      __ mov(r4, Operand(instance));
      __ StoreWord(r4, ContextOperand(cp, variable->index()), r0);
      Visit(declaration->module());
      break;
    }

    case Variable::PARAMETER:
    case Variable::LOCAL:
    case Variable::LOOKUP:
      UNREACHABLE();
  }
}


void FullCodeGenerator::VisitImportDeclaration(ImportDeclaration* declaration) {
  EMIT_STUB_MARKER(216);
  VariableProxy* proxy = declaration->proxy();
  Variable* variable = proxy->var();
  switch (variable->location()) {
    case Variable::UNALLOCATED:
      // TODO(rossberg)
      break;

    case Variable::CONTEXT: {
      Comment cmnt(masm_, "[ ImportDeclaration");
      EmitDebugCheckDeclarationContext(variable);
      // TODO(rossberg)
      break;
    }

    case Variable::PARAMETER:
    case Variable::LOCAL:
    case Variable::LOOKUP:
      UNREACHABLE();
  }
}


void FullCodeGenerator::VisitExportDeclaration(ExportDeclaration* declaration) {
  // TODO(rossberg)
}


void FullCodeGenerator::DeclareGlobals(Handle<FixedArray> pairs) {
  EMIT_STUB_MARKER(217);
  // Call the runtime to declare the globals.
  // The context is the first argument.
  __ mov(r4, Operand(pairs));
  __ li(r3, Operand(Smi::FromInt(DeclareGlobalsFlags())));
  __ Push(cp, r4, r3);
  __ CallRuntime(Runtime::kDeclareGlobals, 3);
  // Return value is ignored.
}


void FullCodeGenerator::VisitSwitchStatement(SwitchStatement* stmt) {
  EMIT_STUB_MARKER(218);
  Comment cmnt(masm_, "[ SwitchStatement");
  Breakable nested_statement(this, stmt);
  SetStatementPosition(stmt);

  // Keep the switch value on the stack until a case matches.
  VisitForStackValue(stmt->tag());
  PrepareForBailoutForId(stmt->EntryId(), NO_REGISTERS);

  ZoneList<CaseClause*>* clauses = stmt->cases();
  CaseClause* default_clause = NULL;  // Can occur anywhere in the list.

  Label next_test;  // Recycled for each test.
  // Compile all the tests with branches to their bodies.
  for (int i = 0; i < clauses->length(); i++) {
    CaseClause* clause = clauses->at(i);
    clause->body_target()->Unuse();

    // The default is not a test, but remember it as final fall through.
    if (clause->is_default()) {
      default_clause = clause;
      continue;
    }

    Comment cmnt(masm_, "[ Case comparison");
    __ bind(&next_test);
    next_test.Unuse();

    // Compile the label expression.
    VisitForAccumulatorValue(clause->label());

    // Perform the comparison as if via '==='.
    __ lwz(r4, MemOperand(sp, 0));  // Switch value.
    bool inline_smi_code = ShouldInlineSmiCase(Token::EQ_STRICT);
    JumpPatchSite patch_site(masm_);
    if (inline_smi_code) {
      Label slow_case;
      __ orx(r5, r4, r3);
      patch_site.EmitJumpIfNotSmi(r5, &slow_case);

      __ cmp(r4, r3);
      __ bne(&next_test);
      __ Drop(1);  // Switch value is no longer needed.
      __ b(clause->body_target());
      __ bind(&slow_case);
    }

    // Record position before stub call for type feedback.
    SetSourcePosition(clause->position());
    Handle<Code> ic = CompareIC::GetUninitialized(Token::EQ_STRICT);
    CallIC(ic, RelocInfo::CODE_TARGET, clause->CompareId());
    patch_site.EmitPatchInfo();

    __ cmpi(r3, Operand(0));
    __ bne(&next_test);
    __ Drop(1);  // Switch value is no longer needed.
    __ b(clause->body_target());
  }

  // Discard the test value and jump to the default if present, otherwise to
  // the end of the statement.
  __ bind(&next_test);
  __ Drop(1);  // Switch value is no longer needed.
  if (default_clause == NULL) {
    __ b(nested_statement.break_label());
  } else {
    __ b(default_clause->body_target());
  }

  // Compile all the case bodies.
  for (int i = 0; i < clauses->length(); i++) {
    Comment cmnt(masm_, "[ Case body");
    CaseClause* clause = clauses->at(i);
    __ bind(clause->body_target());
    PrepareForBailoutForId(clause->EntryId(), NO_REGISTERS);
    VisitStatements(clause->statements());
  }

  __ bind(nested_statement.break_label());
  PrepareForBailoutForId(stmt->ExitId(), NO_REGISTERS);
}


void FullCodeGenerator::VisitForInStatement(ForInStatement* stmt) {
  EMIT_STUB_MARKER(219);
  Comment cmnt(masm_, "[ ForInStatement");
  SetStatementPosition(stmt);

  Label loop, exit;
  ForIn loop_statement(this, stmt);
  increment_loop_depth();

  // Get the object to enumerate over. Both SpiderMonkey and JSC
  // ignore null and undefined in contrast to the specification; see
  // ECMA-262 section 12.6.4.
  VisitForAccumulatorValue(stmt->enumerable());
  __ LoadRoot(ip, Heap::kUndefinedValueRootIndex);
  __ cmp(r3, ip);
  __ beq(&exit);
  Register null_value = r7;
  __ LoadRoot(null_value, Heap::kNullValueRootIndex);
  __ cmp(r3, null_value);
  __ beq(&exit);

  PrepareForBailoutForId(stmt->PrepareId(), TOS_REG);

  // Convert the object to a JS object.
  Label convert, done_convert;
  __ JumpIfSmi(r3, &convert);
  __ CompareObjectType(r3, r4, r4, FIRST_SPEC_OBJECT_TYPE);
  __ bge(&done_convert);
  __ bind(&convert);
  __ push(r3);
  __ InvokeBuiltin(Builtins::TO_OBJECT, CALL_FUNCTION);
  __ bind(&done_convert);
  __ push(r3);

  // Check for proxies.
  Label call_runtime;
  STATIC_ASSERT(FIRST_JS_PROXY_TYPE == FIRST_SPEC_OBJECT_TYPE);
  __ CompareObjectType(r3, r4, r4, LAST_JS_PROXY_TYPE);
  __ ble(&call_runtime);

  // Check cache validity in generated code. This is a fast case for
  // the JSObject::IsSimpleEnum cache validity checks. If we cannot
  // guarantee cache validity, call the runtime system to check cache
  // validity or get the property names in a fixed array.
  __ CheckEnumCache(null_value, &call_runtime);

  // The enum cache is valid.  Load the map of the object being
  // iterated over and use the cache for the iteration.
  Label use_cache;
  __ lwz(r3, FieldMemOperand(r3, HeapObject::kMapOffset));
  __ b(&use_cache);

  // Get the set of properties to enumerate.
  __ bind(&call_runtime);
  __ push(r3);  // Duplicate the enumerable object on the stack.
  __ CallRuntime(Runtime::kGetPropertyNamesFast, 1);

  // If we got a map from the runtime call, we can do a fast
  // modification check. Otherwise, we got a fixed array, and we have
  // to do a slow check.
  Label fixed_array;
  __ lwz(r5, FieldMemOperand(r3, HeapObject::kMapOffset));
  __ LoadRoot(ip, Heap::kMetaMapRootIndex);
  __ cmp(r5, ip);
  __ bne(&fixed_array);

  // We got a map in register r3. Get the enumeration cache from it.
  Label no_descriptors;
  __ bind(&use_cache);

  __ EnumLength(r4, r3);
  __ cmpi(r4, Operand(Smi::FromInt(0)));
  __ beq(&no_descriptors);

  __ LoadInstanceDescriptors(r3, r5, r7);
  __ lwz(r5, FieldMemOperand(r5, DescriptorArray::kEnumCacheOffset));
  __ lwz(r5, FieldMemOperand(r5, DescriptorArray::kEnumCacheBridgeCacheOffset));

  // Set up the four remaining stack slots.
  __ push(r3);  // Map.
  __ li(r3, Operand(Smi::FromInt(0)));
  // Push enumeration cache, enumeration cache length (as smi) and zero.
  __ Push(r5, r4, r3);
  __ jmp(&loop);

  __ bind(&no_descriptors);
  __ Drop(1);
  __ jmp(&exit);

  // We got a fixed array in register r3. Iterate through that.
  Label non_proxy;
  __ bind(&fixed_array);

  Handle<JSGlobalPropertyCell> cell =
      isolate()->factory()->NewJSGlobalPropertyCell(
          Handle<Object>(
              Smi::FromInt(TypeFeedbackCells::kForInFastCaseMarker)));
  RecordTypeFeedbackCell(stmt->ForInFeedbackId(), cell);
  __ LoadHeapObject(r4, cell);
  __ mov(r5, Operand(Smi::FromInt(TypeFeedbackCells::kForInSlowCaseMarker)));
  __ stw(r5, FieldMemOperand(r4, JSGlobalPropertyCell::kValueOffset));

  __ li(r4, Operand(Smi::FromInt(1)));  // Smi indicates slow check
  __ lwz(r5, MemOperand(sp, 0 * kPointerSize));  // Get enumerated object
  STATIC_ASSERT(FIRST_JS_PROXY_TYPE == FIRST_SPEC_OBJECT_TYPE);
  __ CompareObjectType(r5, r6, r6, LAST_JS_PROXY_TYPE);
  __ bgt(&non_proxy);
  __ li(r4, Operand(Smi::FromInt(0)));  // Zero indicates proxy
  __ bind(&non_proxy);
  __ Push(r4, r3);  // Smi and array
  __ lwz(r4, FieldMemOperand(r3, FixedArray::kLengthOffset));
  __ li(r3, Operand(Smi::FromInt(0)));
  __ Push(r4, r3);  // Fixed array length (as smi) and initial index.

  // Generate code for doing the condition check.
  PrepareForBailoutForId(stmt->BodyId(), NO_REGISTERS);
  __ bind(&loop);
  // Load the current count to r3, load the length to r4.
  __ lwz(r3, MemOperand(sp, 0 * kPointerSize));
  __ lwz(r4, MemOperand(sp, 1 * kPointerSize));
  __ cmpl(r3, r4);  // Compare to the array length.
  __ bge(loop_statement.break_label());

  // Get the current entry of the array into register r6.
  __ lwz(r5, MemOperand(sp, 2 * kPointerSize));
  __ addi(r5, r5, Operand(FixedArray::kHeaderSize - kHeapObjectTag));
  __ slwi(r6, r3, Operand(kPointerSizeLog2 - kSmiTagSize));
  __ add(r6, r5, r6);
  __ lwz(r6, MemOperand(r6));

  // Get the expected map from the stack or a smi in the
  // permanent slow case into register r2.
  __ lwz(r5, MemOperand(sp, 3 * kPointerSize));

  // Check if the expected map still matches that of the enumerable.
  // If not, we may have to filter the key.
  Label update_each;
  __ lwz(r4, MemOperand(sp, 4 * kPointerSize));
  __ lwz(r7, FieldMemOperand(r4, HeapObject::kMapOffset));
  __ cmp(r7, r5);
  __ beq(&update_each);

  // For proxies, no filtering is done.
  // TODO(rossberg): What if only a prototype is a proxy? Not specified yet.
  __ cmpi(r5, Operand(Smi::FromInt(0)));
  __ beq(&update_each);

  // Convert the entry to a string or (smi) 0 if it isn't a property
  // any more. If the property has been removed while iterating, we
  // just skip it.
  __ push(r4);  // Enumerable.
  __ push(r6);  // Current entry.
  __ InvokeBuiltin(Builtins::FILTER_KEY, CALL_FUNCTION);
  __ mr(r6, r3);
  __ cmpi(r6, Operand(0));
  __ beq(loop_statement.continue_label());

  // Update the 'each' property or variable from the possibly filtered
  // entry in register r6.
  __ bind(&update_each);
  __ mr(result_register(), r6);
  // Perform the assignment as if via '='.
  { EffectContext context(this);
    EmitAssignment(stmt->each());
  }

  // Generate code for the body of the loop.
  Visit(stmt->body());

  // Generate code for the going to the next element by incrementing
  // the index (smi) stored on top of the stack.
  __ bind(loop_statement.continue_label());
  __ pop(r3);
  __ addi(r3, r3, Operand(Smi::FromInt(1)));
  __ push(r3);

  EmitStackCheck(stmt, &loop);
  __ b(&loop);

  // Remove the pointers stored on the stack.
  __ bind(loop_statement.break_label());
  __ Drop(5);

  // Exit and decrement the loop depth.
  PrepareForBailoutForId(stmt->ExitId(), NO_REGISTERS);
  __ bind(&exit);
  decrement_loop_depth();
}


void FullCodeGenerator::EmitNewClosure(Handle<SharedFunctionInfo> info,
                                       bool pretenure) {
  EMIT_STUB_MARKER(220);
  // Use the fast case closure allocation code that allocates in new
  // space for nested functions that don't need literals cloning. If
  // we're running with the --always-opt or the --prepare-always-opt
  // flag, we need to use the runtime function so that the new function
  // we are creating here gets a chance to have its code optimized and
  // doesn't just get a copy of the existing unoptimized code.
  if (!FLAG_always_opt &&
      !FLAG_prepare_always_opt &&
      !pretenure &&
      scope()->is_function_scope() &&
      info->num_literals() == 0) {
    FastNewClosureStub stub(info->language_mode());
    __ mov(r3, Operand(info));
    __ push(r3);
    __ CallStub(&stub);
  } else {
    __ mov(r3, Operand(info));
    __ LoadRoot(r4, pretenure ? Heap::kTrueValueRootIndex
                              : Heap::kFalseValueRootIndex);
    __ Push(cp, r3, r4);
    __ CallRuntime(Runtime::kNewClosure, 3);
  }
  context()->Plug(r3);
}


void FullCodeGenerator::VisitVariableProxy(VariableProxy* expr) {
  Comment cmnt(masm_, "[ VariableProxy");
  EmitVariableLoad(expr);
}


void FullCodeGenerator::EmitLoadGlobalCheckExtensions(Variable* var,
                                                      TypeofState typeof_state,
                                                      Label* slow) {
  EMIT_STUB_MARKER(221);
  Register current = cp;
  Register next = r4;
  Register temp = r5;

  Scope* s = scope();
  while (s != NULL) {
    if (s->num_heap_slots() > 0) {
      if (s->calls_non_strict_eval()) {
        // Check that extension is NULL.
        __ lwz(temp, ContextOperand(current, Context::EXTENSION_INDEX));
        __ cmpi(temp, Operand(0));
        __ bne(slow);
      }
      // Load next context in chain.
      __ lwz(next, ContextOperand(current, Context::PREVIOUS_INDEX));
      // Walk the rest of the chain without clobbering cp.
      current = next;
    }
    // If no outer scope calls eval, we do not need to check more
    // context extensions.
    if (!s->outer_scope_calls_non_strict_eval() || s->is_eval_scope()) break;
    s = s->outer_scope();
  }

  if (s->is_eval_scope()) {
    Label loop, fast;
    if (!current.is(next)) {
      __ Move(next, current);
    }
    __ bind(&loop);
    // Terminate at native context.
    __ lwz(temp, FieldMemOperand(next, HeapObject::kMapOffset));
    __ LoadRoot(ip, Heap::kNativeContextMapRootIndex);
    __ cmp(temp, ip);
    __ beq(&fast);
    // Check that extension is NULL.
    __ lwz(temp, ContextOperand(next, Context::EXTENSION_INDEX));
    __ cmpi(temp, Operand(0));
    __ bne(slow);
    // Load next context in chain.
    __ lwz(next, ContextOperand(next, Context::PREVIOUS_INDEX));
    __ b(&loop);
    __ bind(&fast);
  }

  __ lwz(r3, GlobalObjectOperand());
  __ mov(r5, Operand(var->name()));
  RelocInfo::Mode mode = (typeof_state == INSIDE_TYPEOF)
      ? RelocInfo::CODE_TARGET
      : RelocInfo::CODE_TARGET_CONTEXT;
  Handle<Code> ic = isolate()->builtins()->LoadIC_Initialize();
  CallIC(ic, mode);
}


MemOperand FullCodeGenerator::ContextSlotOperandCheckExtensions(Variable* var,
                                                                Label* slow) {
  EMIT_STUB_MARKER(222);

  ASSERT(var->IsContextSlot());
  Register context = cp;
  Register next = r6;
  Register temp = r7;

  for (Scope* s = scope(); s != var->scope(); s = s->outer_scope()) {
    if (s->num_heap_slots() > 0) {
      if (s->calls_non_strict_eval()) {
        // Check that extension is NULL.
        __ lwz(temp, ContextOperand(context, Context::EXTENSION_INDEX));
        __ cmpi(temp, Operand(0));
        __ bne(slow);
      }
      __ lwz(next, ContextOperand(context, Context::PREVIOUS_INDEX));
      // Walk the rest of the chain without clobbering cp.
      context = next;
    }
  }
  // Check that last extension is NULL.
  __ lwz(temp, ContextOperand(context, Context::EXTENSION_INDEX));
  __ cmpi(temp, Operand(0));
  __ bne(slow);

  // This function is used only for loads, not stores, so it's safe to
  // return an cp-based operand (the write barrier cannot be allowed to
  // destroy the cp register).
  return ContextOperand(context, var->index());
}


void FullCodeGenerator::EmitDynamicLookupFastCase(Variable* var,
                                                  TypeofState typeof_state,
                                                  Label* slow,
                                                  Label* done) {
  EMIT_STUB_MARKER(223);
  // Generate fast-case code for variables that might be shadowed by
  // eval-introduced variables.  Eval is used a lot without
  // introducing variables.  In those cases, we do not want to
  // perform a runtime call for all variables in the scope
  // containing the eval.
  if (var->mode() == DYNAMIC_GLOBAL) {
    EmitLoadGlobalCheckExtensions(var, typeof_state, slow);
    __ jmp(done);
  } else if (var->mode() == DYNAMIC_LOCAL) {
    Variable* local = var->local_if_not_shadowed();
    __ lwz(r3, ContextSlotOperandCheckExtensions(local, slow));
    if (local->mode() == CONST ||
        local->mode() == CONST_HARMONY ||
        local->mode() == LET) {
      __ CompareRoot(r3, Heap::kTheHoleValueRootIndex);
      if (local->mode() == CONST) {
        __ LoadRoot(r3, Heap::kUndefinedValueRootIndex, eq);
      } else {  // LET || CONST_HARMONY
        __ bne(done);
        __ mov(r3, Operand(var->name()));
        __ push(r3);
        __ CallRuntime(Runtime::kThrowReferenceError, 1);
      }
    }
    __ jmp(done);
  }
}


void FullCodeGenerator::EmitVariableLoad(VariableProxy* proxy) {
  EMIT_STUB_MARKER(224);
  // Record position before possible IC call.
  SetSourcePosition(proxy->position());
  Variable* var = proxy->var();

  // Three cases: global variables, lookup variables, and all other types of
  // variables.
  switch (var->location()) {
    case Variable::UNALLOCATED: {
      Comment cmnt(masm_, "Global variable");
      // Use inline caching. Variable name is passed in r5 and the global
      // object (receiver) in r3.
      __ lwz(r3, GlobalObjectOperand());
      __ mov(r5, Operand(var->name()));
      Handle<Code> ic = isolate()->builtins()->LoadIC_Initialize();
      CallIC(ic, RelocInfo::CODE_TARGET_CONTEXT);
      context()->Plug(r3);
      break;
    }

    case Variable::PARAMETER:
    case Variable::LOCAL:
    case Variable::CONTEXT: {
      Comment cmnt(masm_, var->IsContextSlot()
                              ? "Context variable"
                              : "Stack variable");
      if (var->binding_needs_init()) {
        // var->scope() may be NULL when the proxy is located in eval code and
        // refers to a potential outside binding. Currently those bindings are
        // always looked up dynamically, i.e. in that case
        //     var->location() == LOOKUP.
        // always holds.
        ASSERT(var->scope() != NULL);

        // Check if the binding really needs an initialization check. The check
        // can be skipped in the following situation: we have a LET or CONST
        // binding in harmony mode, both the Variable and the VariableProxy have
        // the same declaration scope (i.e. they are both in global code, in the
        // same function or in the same eval code) and the VariableProxy is in
        // the source physically located after the initializer of the variable.
        //
        // We cannot skip any initialization checks for CONST in non-harmony
        // mode because const variables may be declared but never initialized:
        //   if (false) { const x; }; var y = x;
        //
        // The condition on the declaration scopes is a conservative check for
        // nested functions that access a binding and are called before the
        // binding is initialized:
        //   function() { f(); let x = 1; function f() { x = 2; } }
        //
        bool skip_init_check;
        if (var->scope()->DeclarationScope() != scope()->DeclarationScope()) {
          skip_init_check = false;
        } else {
          // Check that we always have valid source position.
          ASSERT(var->initializer_position() != RelocInfo::kNoPosition);
          ASSERT(proxy->position() != RelocInfo::kNoPosition);
          skip_init_check = var->mode() != CONST &&
              var->initializer_position() < proxy->position();
        }

        if (!skip_init_check) {
          // Let and const need a read barrier.
          GetVar(r3, var);
          __ CompareRoot(r3, Heap::kTheHoleValueRootIndex);
          if (var->mode() == LET || var->mode() == CONST_HARMONY) {
            // Throw a reference error when using an uninitialized let/const
            // binding in harmony mode.
            Label done;
            __ bne(&done);
            __ mov(r3, Operand(var->name()));
            __ push(r3);
            __ CallRuntime(Runtime::kThrowReferenceError, 1);
            __ bind(&done);
          } else {
            // Uninitalized const bindings outside of harmony mode are unholed.
            ASSERT(var->mode() == CONST);
            __ LoadRoot(r3, Heap::kUndefinedValueRootIndex, eq);
          }
          context()->Plug(r3);
          break;
        }
      }
      context()->Plug(var);
      break;
    }

    case Variable::LOOKUP: {
      Label done, slow;
      // Generate code for loading from variables potentially shadowed
      // by eval-introduced variables.
      EmitDynamicLookupFastCase(var, NOT_INSIDE_TYPEOF, &slow, &done);
      __ bind(&slow);
      Comment cmnt(masm_, "Lookup variable");
      __ mov(r4, Operand(var->name()));
      __ Push(cp, r4);  // Context and name.
      __ CallRuntime(Runtime::kLoadContextSlot, 2);
      __ bind(&done);
      context()->Plug(r3);
    }
  }
}


void FullCodeGenerator::VisitRegExpLiteral(RegExpLiteral* expr) {
  EMIT_STUB_MARKER(225);
  Comment cmnt(masm_, "[ RegExpLiteral");
  Label materialized;
  // Registers will be used as follows:
  // r8 = materialized value (RegExp literal)
  // r7 = JS function, literals array
  // r6 = literal index
  // r5 = RegExp pattern
  // r4 = RegExp flags
  // r3 = RegExp literal clone
  __ lwz(r3, MemOperand(fp, JavaScriptFrameConstants::kFunctionOffset));
  __ lwz(r7, FieldMemOperand(r3, JSFunction::kLiteralsOffset));
  int literal_offset =
      FixedArray::kHeaderSize + expr->literal_index() * kPointerSize;
  __ LoadWord(r8, FieldMemOperand(r7, literal_offset), r0);
  __ LoadRoot(ip, Heap::kUndefinedValueRootIndex);
  __ cmp(r8, ip);
  __ bne(&materialized);

  // Create regexp literal using runtime function.
  // Result will be in r3.
  __ mov(r6, Operand(Smi::FromInt(expr->literal_index())));
  __ mov(r5, Operand(expr->pattern()));
  __ mov(r4, Operand(expr->flags()));
  __ Push(r7, r6, r5, r4);
  __ CallRuntime(Runtime::kMaterializeRegExpLiteral, 4);
  __ mr(r8, r3);

  __ bind(&materialized);
  int size = JSRegExp::kSize + JSRegExp::kInObjectFieldCount * kPointerSize;
  Label allocated, runtime_allocate;
  __ AllocateInNewSpace(size, r3, r5, r6, &runtime_allocate, TAG_OBJECT);
  __ jmp(&allocated);

  __ bind(&runtime_allocate);
  __ push(r8);
  __ li(r3, Operand(Smi::FromInt(size)));
  __ push(r3);
  __ CallRuntime(Runtime::kAllocateInNewSpace, 1);
  __ pop(r8);

  __ bind(&allocated);
  // After this, registers are used as follows:
  // r3: Newly allocated regexp.
  // r8: Materialized regexp.
  // r5: temp.
  __ CopyFields(r3, r8, r5.bit(), size / kPointerSize);
  context()->Plug(r3);
}


void FullCodeGenerator::EmitAccessor(Expression* expression) {
  if (expression == NULL) {
    __ LoadRoot(r4, Heap::kNullValueRootIndex);
    __ push(r4);
  } else {
    VisitForStackValue(expression);
  }
}


void FullCodeGenerator::VisitObjectLiteral(ObjectLiteral* expr) {
  EMIT_STUB_MARKER(226);
  Comment cmnt(masm_, "[ ObjectLiteral");
  Handle<FixedArray> constant_properties = expr->constant_properties();
  __ lwz(r6, MemOperand(fp,  JavaScriptFrameConstants::kFunctionOffset));
  __ lwz(r6, FieldMemOperand(r6, JSFunction::kLiteralsOffset));
  __ mov(r5, Operand(Smi::FromInt(expr->literal_index())));
  __ mov(r4, Operand(constant_properties));
  int flags = expr->fast_elements()
      ? ObjectLiteral::kFastElements
      : ObjectLiteral::kNoFlags;
  flags |= expr->has_function()
      ? ObjectLiteral::kHasFunction
      : ObjectLiteral::kNoFlags;
  __ li(r3, Operand(Smi::FromInt(flags)));
  __ Push(r6, r5, r4, r3);
  int properties_count = constant_properties->length() / 2;
  if (expr->depth() > 1) {
    __ CallRuntime(Runtime::kCreateObjectLiteral, 4);
  } else if (flags != ObjectLiteral::kFastElements ||
      properties_count > FastCloneShallowObjectStub::kMaximumClonedProperties) {
    __ CallRuntime(Runtime::kCreateObjectLiteralShallow, 4);
  } else {
    FastCloneShallowObjectStub stub(properties_count);
    __ CallStub(&stub);
  }

  // If result_saved is true the result is on top of the stack.  If
  // result_saved is false the result is in r3.
  bool result_saved = false;

  // Mark all computed expressions that are bound to a key that
  // is shadowed by a later occurrence of the same key. For the
  // marked expressions, no store code is emitted.
  expr->CalculateEmitStore(zone());

  AccessorTable accessor_table(zone());
  for (int i = 0; i < expr->properties()->length(); i++) {
    ObjectLiteral::Property* property = expr->properties()->at(i);
    if (property->IsCompileTimeValue()) continue;

    Literal* key = property->key();
    Expression* value = property->value();
    if (!result_saved) {
      __ push(r3);  // Save result on stack
      result_saved = true;
    }
    switch (property->kind()) {
      case ObjectLiteral::Property::CONSTANT:
        UNREACHABLE();
      case ObjectLiteral::Property::MATERIALIZED_LITERAL:
        ASSERT(!CompileTimeValue::IsCompileTimeValue(property->value()));
        // Fall through.
      case ObjectLiteral::Property::COMPUTED:
        if (key->handle()->IsSymbol()) {
          if (property->emit_store()) {
            VisitForAccumulatorValue(value);
            __ mov(r5, Operand(key->handle()));
            __ lwz(r4, MemOperand(sp));
            Handle<Code> ic = is_classic_mode()
                ? isolate()->builtins()->StoreIC_Initialize()
                : isolate()->builtins()->StoreIC_Initialize_Strict();
            CallIC(ic, RelocInfo::CODE_TARGET, key->LiteralFeedbackId());
            PrepareForBailoutForId(key->id(), NO_REGISTERS);
          } else {
            VisitForEffect(value);
          }
          break;
        }
        // Fall through.
      case ObjectLiteral::Property::PROTOTYPE:
        // Duplicate receiver on stack.
        __ lwz(r3, MemOperand(sp));
        __ push(r3);
        VisitForStackValue(key);
        VisitForStackValue(value);
        if (property->emit_store()) {
          __ li(r3, Operand(Smi::FromInt(NONE)));  // PropertyAttributes
          __ push(r3);
          __ CallRuntime(Runtime::kSetProperty, 4);
        } else {
          __ Drop(3);
        }
        break;
      case ObjectLiteral::Property::GETTER:
        accessor_table.lookup(key)->second->getter = value;
        break;
      case ObjectLiteral::Property::SETTER:
        accessor_table.lookup(key)->second->setter = value;
        break;
    }
  }

  // Emit code to define accessors, using only a single call to the runtime for
  // each pair of corresponding getters and setters.
  for (AccessorTable::Iterator it = accessor_table.begin();
       it != accessor_table.end();
       ++it) {
    __ lwz(r3, MemOperand(sp));  // Duplicate receiver.
    __ push(r3);
    VisitForStackValue(it->first);
    EmitAccessor(it->second->getter);
    EmitAccessor(it->second->setter);
    __ li(r3, Operand(Smi::FromInt(NONE)));
    __ push(r3);
    __ CallRuntime(Runtime::kDefineOrRedefineAccessorProperty, 5);
  }

  if (expr->has_function()) {
    ASSERT(result_saved);
    __ lwz(r3, MemOperand(sp));
    __ push(r3);
    __ CallRuntime(Runtime::kToFastProperties, 1);
  }

  if (result_saved) {
    context()->PlugTOS();
  } else {
    context()->Plug(r3);
  }
}


void FullCodeGenerator::VisitArrayLiteral(ArrayLiteral* expr) {
  EMIT_STUB_MARKER(227);
  Comment cmnt(masm_, "[ ArrayLiteral");

  ZoneList<Expression*>* subexprs = expr->values();
  int length = subexprs->length();
  Handle<FixedArray> constant_elements = expr->constant_elements();
  ASSERT_EQ(2, constant_elements->length());
  ElementsKind constant_elements_kind =
      static_cast<ElementsKind>(Smi::cast(constant_elements->get(0))->value());
  bool has_fast_elements = IsFastObjectElementsKind(constant_elements_kind);
  Handle<FixedArrayBase> constant_elements_values(
      FixedArrayBase::cast(constant_elements->get(1)));

  __ lwz(r6, MemOperand(fp, JavaScriptFrameConstants::kFunctionOffset));
  __ lwz(r6, FieldMemOperand(r6, JSFunction::kLiteralsOffset));
  __ mov(r5, Operand(Smi::FromInt(expr->literal_index())));
  __ mov(r4, Operand(constant_elements));
  __ Push(r6, r5, r4);
  if (has_fast_elements && constant_elements_values->map() ==
      isolate()->heap()->fixed_cow_array_map()) {
    FastCloneShallowArrayStub stub(
        FastCloneShallowArrayStub::COPY_ON_WRITE_ELEMENTS, length);
    __ CallStub(&stub);
    __ IncrementCounter(
        isolate()->counters()->cow_arrays_created_stub(), 1, r4, r5);
  } else if (expr->depth() > 1) {
    __ CallRuntime(Runtime::kCreateArrayLiteral, 3);
  } else if (length > FastCloneShallowArrayStub::kMaximumClonedLength) {
    __ CallRuntime(Runtime::kCreateArrayLiteralShallow, 3);
  } else {
    ASSERT(IsFastSmiOrObjectElementsKind(constant_elements_kind) ||
           FLAG_smi_only_arrays);
    FastCloneShallowArrayStub::Mode mode = has_fast_elements
      ? FastCloneShallowArrayStub::CLONE_ELEMENTS
      : FastCloneShallowArrayStub::CLONE_ANY_ELEMENTS;
    FastCloneShallowArrayStub stub(mode, length);
    __ CallStub(&stub);
  }

  bool result_saved = false;  // Is the result saved to the stack?

  // Emit code to evaluate all the non-constant subexpressions and to store
  // them into the newly cloned array.
  for (int i = 0; i < length; i++) {
    Expression* subexpr = subexprs->at(i);
    // If the subexpression is a literal or a simple materialized literal it
    // is already set in the cloned array.
    if (subexpr->AsLiteral() != NULL ||
        CompileTimeValue::IsCompileTimeValue(subexpr)) {
      continue;
    }

    if (!result_saved) {
      __ push(r3);
      result_saved = true;
    }
    VisitForAccumulatorValue(subexpr);

    if (IsFastObjectElementsKind(constant_elements_kind)) {
      int offset = FixedArray::kHeaderSize + (i * kPointerSize);
      __ lwz(r8, MemOperand(sp));  // Copy of array literal.
      __ lwz(r4, FieldMemOperand(r8, JSObject::kElementsOffset));
      __ StoreWord(result_register(), FieldMemOperand(r4, offset), r0);
      // Update the write barrier for the array store.
      __ RecordWriteField(r4, offset, result_register(), r5,
                          kLRHasBeenSaved, kDontSaveFPRegs,
                          EMIT_REMEMBERED_SET, INLINE_SMI_CHECK);
    } else {
      __ lwz(r4, MemOperand(sp));  // Copy of array literal.
      __ lwz(r5, FieldMemOperand(r4, JSObject::kMapOffset));
      __ li(r6, Operand(Smi::FromInt(i)));
      __ li(r7, Operand(Smi::FromInt(expr->literal_index())));
      StoreArrayLiteralElementStub stub;
      __ CallStub(&stub);
    }

    PrepareForBailoutForId(expr->GetIdForElement(i), NO_REGISTERS);
  }

  if (result_saved) {
    context()->PlugTOS();
  } else {
    context()->Plug(r3);
  }
}


void FullCodeGenerator::VisitAssignment(Assignment* expr) {
  EMIT_STUB_MARKER(228);
  Comment cmnt(masm_, "[ Assignment");
  // Invalid left-hand sides are rewritten to have a 'throw ReferenceError'
  // on the left-hand side.
  if (!expr->target()->IsValidLeftHandSide()) {
    VisitForEffect(expr->target());
    return;
  }

  // Left-hand side can only be a property, a global or a (parameter or local)
  // slot.
  enum LhsKind { VARIABLE, NAMED_PROPERTY, KEYED_PROPERTY };
  LhsKind assign_type = VARIABLE;
  Property* property = expr->target()->AsProperty();
  if (property != NULL) {
    assign_type = (property->key()->IsPropertyName())
        ? NAMED_PROPERTY
        : KEYED_PROPERTY;
  }

  // Evaluate LHS expression.
  switch (assign_type) {
    case VARIABLE:
      // Nothing to do here.
      break;
    case NAMED_PROPERTY:
      if (expr->is_compound()) {
        // We need the receiver both on the stack and in the accumulator.
        VisitForAccumulatorValue(property->obj());
        __ push(result_register());
      } else {
        VisitForStackValue(property->obj());
      }
      break;
    case KEYED_PROPERTY:
      if (expr->is_compound()) {
        VisitForStackValue(property->obj());
        VisitForAccumulatorValue(property->key());
        __ lwz(r4, MemOperand(sp, 0));
        __ push(r3);
      } else {
        VisitForStackValue(property->obj());
        VisitForStackValue(property->key());
      }
      break;
  }

  // For compound assignments we need another deoptimization point after the
  // variable/property load.
  if (expr->is_compound()) {
    { AccumulatorValueContext context(this);
      switch (assign_type) {
        case VARIABLE:
          EmitVariableLoad(expr->target()->AsVariableProxy());
          PrepareForBailout(expr->target(), TOS_REG);
          break;
        case NAMED_PROPERTY:
          EmitNamedPropertyLoad(property);
          PrepareForBailoutForId(property->LoadId(), TOS_REG);
          break;
        case KEYED_PROPERTY:
          EmitKeyedPropertyLoad(property);
          PrepareForBailoutForId(property->LoadId(), TOS_REG);
          break;
      }
    }

    Token::Value op = expr->binary_op();
    __ push(r3);  // Left operand goes on the stack.
    VisitForAccumulatorValue(expr->value());

    OverwriteMode mode = expr->value()->ResultOverwriteAllowed()
        ? OVERWRITE_RIGHT
        : NO_OVERWRITE;
    SetSourcePosition(expr->position() + 1);
    AccumulatorValueContext context(this);
    if (ShouldInlineSmiCase(op)) {
      EmitInlineSmiBinaryOp(expr->binary_operation(),
                            op,
                            mode,
                            expr->target(),
                            expr->value());
    } else {
      EmitBinaryOp(expr->binary_operation(), op, mode);
    }

    // Deoptimization point in case the binary operation may have side effects.
    PrepareForBailout(expr->binary_operation(), TOS_REG);
  } else {
    VisitForAccumulatorValue(expr->value());
  }

  // Record source position before possible IC call.
  SetSourcePosition(expr->position());

  // Store the value.
  switch (assign_type) {
    case VARIABLE:
      EmitVariableAssignment(expr->target()->AsVariableProxy()->var(),
                             expr->op());
      PrepareForBailoutForId(expr->AssignmentId(), TOS_REG);
      context()->Plug(r3);
      break;
    case NAMED_PROPERTY:
      EmitNamedPropertyAssignment(expr);
      break;
    case KEYED_PROPERTY:
      EmitKeyedPropertyAssignment(expr);
      break;
  }
}


void FullCodeGenerator::EmitNamedPropertyLoad(Property* prop) {
  EMIT_STUB_MARKER(229);
  SetSourcePosition(prop->position());
  Literal* key = prop->key()->AsLiteral();
  __ mov(r5, Operand(key->handle()));
  // Call load IC. It has arguments receiver and property name r3 and r5.
  Handle<Code> ic = isolate()->builtins()->LoadIC_Initialize();
  CallIC(ic, RelocInfo::CODE_TARGET, prop->PropertyFeedbackId());
}


void FullCodeGenerator::EmitKeyedPropertyLoad(Property* prop) {
  SetSourcePosition(prop->position());
  // Call keyed load IC. It has arguments key and receiver in r3 and r4.
  Handle<Code> ic = isolate()->builtins()->KeyedLoadIC_Initialize();
  CallIC(ic, RelocInfo::CODE_TARGET, prop->PropertyFeedbackId());
}


void FullCodeGenerator::EmitInlineSmiBinaryOp(BinaryOperation* expr,
                                              Token::Value op,
                                              OverwriteMode mode,
                                              Expression* left_expr,
                                              Expression* right_expr) {
  EMIT_STUB_MARKER(230);
  Label done, smi_case, stub_call;

  Register scratch1 = r5;
  Register scratch2 = r6;

  // Get the arguments.
  Register left = r4;
  Register right = r3;
  __ pop(left);

  // Perform combined smi check on both operands.
  __ orx(scratch1, left, right);
  STATIC_ASSERT(kSmiTag == 0);
  JumpPatchSite patch_site(masm_);
  patch_site.EmitJumpIfSmi(scratch1, &smi_case);

  __ bind(&stub_call);
  BinaryOpStub stub(op, mode);
  CallIC(stub.GetCode(), RelocInfo::CODE_TARGET,
         expr->BinaryOperationFeedbackId());
  patch_site.EmitPatchInfo();
  __ jmp(&done);

  __ bind(&smi_case);
  // Smi case. This code works the same way as the smi-smi case in the type
  // recording binary operation stub, see
  // BinaryOpStub::GenerateSmiSmiOperation for comments.
  switch (op) {
    case Token::SAR:
      __ b(&stub_call);
      __ GetLeastBitsFromSmi(scratch1, right, 5);
      __ sraw(right, left, scratch1);
      __ clrrwi(right, right, Operand(1));
      break;
    case Token::SHL: {
      __ b(&stub_call);
      __ SmiUntag(scratch1, left);
      __ GetLeastBitsFromSmi(scratch2, right, 5);
      __ slw(scratch1, scratch1, scratch2);
      // Check that the *signed* result fits in a smi
      __ addis(scratch2, scratch1, Operand(0x4000));
      __ cmpi(scratch2, Operand(0));
      __ blt(&stub_call);
      __ SmiTag(right, scratch1);
      break;
    }
    case Token::SHR: {
      __ b(&stub_call);
      __ SmiUntag(scratch1, left);
      __ GetLeastBitsFromSmi(scratch2, right, 5);
      __ srw(scratch1, scratch1, scratch2);
      __ andis(scratch2, scratch1, Operand(0xc000));
      __ bne(&stub_call, cr0);
      __ SmiTag(right, scratch1);
      break;
    }
    case Token::ADD: {
      Label add_no_overflow;
      // C = A+B; C overflows if A/B have same sign and C has diff sign than A
      __ xor_(r0, left, right);
      __ addc(scratch1, left, right);
      __ TestBit(r0, 0, r0);  // test sign bit
      __ bne(&add_no_overflow, cr0);
      __ xor_(r0, right, scratch1);
      __ TestBit(r0, 0, r0);  // test sign bit
      __ bne(&stub_call, cr0);
      __ bind(&add_no_overflow);
      __ mr(right, scratch1);
      break;
    }
    case Token::SUB: {
      Label sub_no_overflow;
      // C = A-B; C overflows if A/B have diff signs and C has diff sign than A
      __ xor_(r0, left, right);
      __ subfc(scratch1, left, right);
      __ TestBit(r0, 0, r0);  // test sign bit
      __ beq(&sub_no_overflow, cr0);
      __ xor_(r0, right, scratch1);
      __ TestBit(r0, 0, r0);  // test sign bit
      __ bne(&stub_call, cr0);
      __ bind(&sub_no_overflow);
      __ mr(right, scratch1);
      break;
    }
    case Token::MUL: {
      __ SmiUntag(ip, right);
      __ mullw(scratch1, left, ip);
      __ mulhw(scratch2, left, ip);
      __ cmpi(scratch2, Operand(0));
      __ bne(&stub_call);
      __ mr(right, scratch1);  // not sure logic is 100% correct
      break;
    }
    case Token::BIT_OR:
      __ orx(right, left, right);
      break;
    case Token::BIT_AND:
      __ and_(right, left, right);
      break;
    case Token::BIT_XOR:
      __ xor_(right, left, right);
      break;
    default:
      UNREACHABLE();
  }

  __ bind(&done);
  context()->Plug(r3);
}


void FullCodeGenerator::EmitBinaryOp(BinaryOperation* expr,
                                     Token::Value op,
                                     OverwriteMode mode) {
  EMIT_STUB_MARKER(231);
  __ pop(r4);
  BinaryOpStub stub(op, mode);
  JumpPatchSite patch_site(masm_);    // unbound, signals no inlined smi code.
  CallIC(stub.GetCode(), RelocInfo::CODE_TARGET,
         expr->BinaryOperationFeedbackId());
  patch_site.EmitPatchInfo();
  context()->Plug(r3);
}


void FullCodeGenerator::EmitAssignment(Expression* expr) {
  EMIT_STUB_MARKER(232);
  // Invalid left-hand sides are rewritten to have a 'throw
  // ReferenceError' on the left-hand side.
  if (!expr->IsValidLeftHandSide()) {
    VisitForEffect(expr);
    return;
  }

  // Left-hand side can only be a property, a global or a (parameter or local)
  // slot.
  enum LhsKind { VARIABLE, NAMED_PROPERTY, KEYED_PROPERTY };
  LhsKind assign_type = VARIABLE;
  Property* prop = expr->AsProperty();
  if (prop != NULL) {
    assign_type = (prop->key()->IsPropertyName())
        ? NAMED_PROPERTY
        : KEYED_PROPERTY;
  }

  switch (assign_type) {
    case VARIABLE: {
      Variable* var = expr->AsVariableProxy()->var();
      EffectContext context(this);
      EmitVariableAssignment(var, Token::ASSIGN);
      break;
    }
    case NAMED_PROPERTY: {
      __ push(r3);  // Preserve value.
      VisitForAccumulatorValue(prop->obj());
      __ mr(r4, r3);
      __ pop(r3);  // Restore value.
      __ mov(r5, Operand(prop->key()->AsLiteral()->handle()));
      Handle<Code> ic = is_classic_mode()
          ? isolate()->builtins()->StoreIC_Initialize()
          : isolate()->builtins()->StoreIC_Initialize_Strict();
      CallIC(ic);
      break;
    }
    case KEYED_PROPERTY: {
      __ push(r3);  // Preserve value.
      VisitForStackValue(prop->obj());
      VisitForAccumulatorValue(prop->key());
      __ mr(r4, r3);
      __ pop(r5);
      __ pop(r3);  // Restore value.
      Handle<Code> ic = is_classic_mode()
          ? isolate()->builtins()->KeyedStoreIC_Initialize()
          : isolate()->builtins()->KeyedStoreIC_Initialize_Strict();
      CallIC(ic);
      break;
    }
  }
  context()->Plug(r3);
}


void FullCodeGenerator::EmitVariableAssignment(Variable* var,
                                               Token::Value op) {
  EMIT_STUB_MARKER(233);
  if (var->IsUnallocated()) {
    // Global var, const, or let.
    __ mov(r5, Operand(var->name()));
    __ lwz(r4, GlobalObjectOperand());
    Handle<Code> ic = is_classic_mode()
        ? isolate()->builtins()->StoreIC_Initialize()
        : isolate()->builtins()->StoreIC_Initialize_Strict();
    CallIC(ic, RelocInfo::CODE_TARGET_CONTEXT);

  } else if (op == Token::INIT_CONST) {
    // Const initializers need a write barrier.
    ASSERT(!var->IsParameter());  // No const parameters.
    if (var->IsStackLocal()) {
      Label skip;
      __ lwz(r4, StackOperand(var));
      __ CompareRoot(r4, Heap::kTheHoleValueRootIndex);
      __ bne(&skip);
      __ stw(result_register(), StackOperand(var));
      __ bind(&skip);
    } else {
      ASSERT(var->IsContextSlot() || var->IsLookupSlot());
      // Like var declarations, const declarations are hoisted to function
      // scope.  However, unlike var initializers, const initializers are
      // able to drill a hole to that function context, even from inside a
      // 'with' context.  We thus bypass the normal static scope lookup for
      // var->IsContextSlot().
      __ push(r3);
      __ mov(r3, Operand(var->name()));
      __ Push(cp, r3);  // Context and name.
      __ CallRuntime(Runtime::kInitializeConstContextSlot, 3);
    }

  } else if (var->mode() == LET && op != Token::INIT_LET) {
    // Non-initializing assignment to let variable needs a write barrier.
    if (var->IsLookupSlot()) {
      __ push(r3);  // Value.
      __ mov(r4, Operand(var->name()));
      __ mov(r3, Operand(Smi::FromInt(language_mode())));
      __ Push(cp, r4, r3);  // Context, name, strict mode.
      __ CallRuntime(Runtime::kStoreContextSlot, 4);
    } else {
      ASSERT(var->IsStackAllocated() || var->IsContextSlot());
      Label assign;
      MemOperand location = VarOperand(var, r4);
      __ lwz(r6, location);
      __ CompareRoot(r6, Heap::kTheHoleValueRootIndex);
      __ b(ne, &assign);
      __ mov(r6, Operand(var->name()));
      __ push(r6);
      __ CallRuntime(Runtime::kThrowReferenceError, 1);
      // Perform the assignment.
      __ bind(&assign);
      __ stw(result_register(), location);
      if (var->IsContextSlot()) {
        // RecordWrite may destroy all its register arguments.
        __ mr(r6, result_register());
        int offset = Context::SlotOffset(var->index());
        __ RecordWriteContextSlot(
            r4, offset, r6, r5, kLRHasBeenSaved, kDontSaveFPRegs);
      }
    }

  } else if (!var->is_const_mode() || op == Token::INIT_CONST_HARMONY) {
    // Assignment to var or initializing assignment to let/const
    // in harmony mode.
    if (var->IsStackAllocated() || var->IsContextSlot()) {
      MemOperand location = VarOperand(var, r4);
      if (generate_debug_code_ && op == Token::INIT_LET) {
        // Check for an uninitialized let binding.
        __ lwz(r5, location);
        __ CompareRoot(r5, Heap::kTheHoleValueRootIndex);
        __ Check(eq, "Let binding re-initialization.");
      }
      // Perform the assignment.
      __ stw(r3, location);
      if (var->IsContextSlot()) {
        __ mr(r6, r3);
        int offset = Context::SlotOffset(var->index());
        __ RecordWriteContextSlot(
            r4, offset, r6, r5, kLRHasBeenSaved, kDontSaveFPRegs);
      }
    } else {
      ASSERT(var->IsLookupSlot());
      __ push(r3);  // Value.
      __ mov(r4, Operand(var->name()));
      __ mov(r3, Operand(Smi::FromInt(language_mode())));
      __ Push(cp, r4, r3);  // Context, name, strict mode.
      __ CallRuntime(Runtime::kStoreContextSlot, 4);
    }
  }
  // Non-initializing assignments to consts are ignored.
}


void FullCodeGenerator::EmitNamedPropertyAssignment(Assignment* expr) {
  EMIT_STUB_MARKER(234);
  // Assignment to a property, using a named store IC.
  Property* prop = expr->target()->AsProperty();
  ASSERT(prop != NULL);
  ASSERT(prop->key()->AsLiteral() != NULL);

  // Record source code position before IC call.
  SetSourcePosition(expr->position());
  __ mov(r5, Operand(prop->key()->AsLiteral()->handle()));
  __ pop(r4);

  Handle<Code> ic = is_classic_mode()
      ? isolate()->builtins()->StoreIC_Initialize()
      : isolate()->builtins()->StoreIC_Initialize_Strict();
  CallIC(ic, RelocInfo::CODE_TARGET, expr->AssignmentFeedbackId());

  PrepareForBailoutForId(expr->AssignmentId(), TOS_REG);
  context()->Plug(r3);
}


void FullCodeGenerator::EmitKeyedPropertyAssignment(Assignment* expr) {
  EMIT_STUB_MARKER(235);
  // Assignment to a property, using a keyed store IC.

  // Record source code position before IC call.
  SetSourcePosition(expr->position());
  __ pop(r4);  // Key.
  __ pop(r5);

  Handle<Code> ic = is_classic_mode()
      ? isolate()->builtins()->KeyedStoreIC_Initialize()
      : isolate()->builtins()->KeyedStoreIC_Initialize_Strict();
  CallIC(ic, RelocInfo::CODE_TARGET, expr->AssignmentFeedbackId());

  PrepareForBailoutForId(expr->AssignmentId(), TOS_REG);
  context()->Plug(r3);
}


void FullCodeGenerator::VisitProperty(Property* expr) {
  EMIT_STUB_MARKER(236);
  Comment cmnt(masm_, "[ Property");
  Expression* key = expr->key();

  if (key->IsPropertyName()) {
    VisitForAccumulatorValue(expr->obj());
    EmitNamedPropertyLoad(expr);
    PrepareForBailoutForId(expr->LoadId(), TOS_REG);
    context()->Plug(r3);
  } else {
    VisitForStackValue(expr->obj());
    VisitForAccumulatorValue(expr->key());
    __ pop(r4);
    EmitKeyedPropertyLoad(expr);
    context()->Plug(r3);
  }
}


void FullCodeGenerator::CallIC(Handle<Code> code,
                               RelocInfo::Mode rmode,
                               TypeFeedbackId ast_id) {
  ic_total_count_++;
  __ Call(code, rmode, ast_id);
}

void FullCodeGenerator::EmitCallWithIC(Call* expr,
                                       Handle<Object> name,
                                       RelocInfo::Mode mode) {
  EMIT_STUB_MARKER(237);
  // Code common for calls using the IC.
  ZoneList<Expression*>* args = expr->arguments();
  int arg_count = args->length();
  { PreservePositionScope scope(masm()->positions_recorder());
    for (int i = 0; i < arg_count; i++) {
      VisitForStackValue(args->at(i));
    }
    __ mov(r5, Operand(name));
  }
  // Record source position for debugger.
  SetSourcePosition(expr->position());
  // Call the IC initialization code.
  Handle<Code> ic =
      isolate()->stub_cache()->ComputeCallInitialize(arg_count, mode);
  CallIC(ic, mode, expr->CallFeedbackId());
  RecordJSReturnSite(expr);
  // Restore context register.
  __ lwz(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
  context()->Plug(r3);
}


void FullCodeGenerator::EmitKeyedCallWithIC(Call* expr,
                                            Expression* key) {
  EMIT_STUB_MARKER(238);
  // Load the key.
  VisitForAccumulatorValue(key);

  // Swap the name of the function and the receiver on the stack to follow
  // the calling convention for call ICs.
  __ pop(r4);
  __ push(r3);
  __ push(r4);

  // Code common for calls using the IC.
  ZoneList<Expression*>* args = expr->arguments();
  int arg_count = args->length();
  { PreservePositionScope scope(masm()->positions_recorder());
    for (int i = 0; i < arg_count; i++) {
      VisitForStackValue(args->at(i));
    }
  }
  // Record source position for debugger.
  SetSourcePosition(expr->position());
  // Call the IC initialization code.
  Handle<Code> ic =
      isolate()->stub_cache()->ComputeKeyedCallInitialize(arg_count);
  __ LoadWord(r5, MemOperand(sp, (arg_count + 1) * kPointerSize), r0);  // Key.
  CallIC(ic, RelocInfo::CODE_TARGET, expr->CallFeedbackId());
  RecordJSReturnSite(expr);
  // Restore context register.
  __ lwz(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
  context()->DropAndPlug(1, r3);  // Drop the key still on the stack.
}


void FullCodeGenerator::EmitCallWithStub(Call* expr, CallFunctionFlags flags) {
  EMIT_STUB_MARKER(239);
  // Code common for calls using the call stub.
  ZoneList<Expression*>* args = expr->arguments();
  int arg_count = args->length();
  { PreservePositionScope scope(masm()->positions_recorder());
    for (int i = 0; i < arg_count; i++) {
      VisitForStackValue(args->at(i));
    }
  }
  // Record source position for debugger.
  SetSourcePosition(expr->position());

  // Record call targets in unoptimized code.
  flags = static_cast<CallFunctionFlags>(flags | RECORD_CALL_TARGET);
  Handle<Object> uninitialized =
      TypeFeedbackCells::UninitializedSentinel(isolate());
  Handle<JSGlobalPropertyCell> cell =
      isolate()->factory()->NewJSGlobalPropertyCell(uninitialized);
  RecordTypeFeedbackCell(expr->CallFeedbackId(), cell);
  __ mov(r5, Operand(cell));

  CallFunctionStub stub(arg_count, flags);
  __ LoadWord(r4, MemOperand(sp, (arg_count + 1) * kPointerSize), r0);
  __ CallStub(&stub);
  RecordJSReturnSite(expr);
  // Restore context register.
  __ lwz(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
  context()->DropAndPlug(1, r3);
}


void FullCodeGenerator::EmitResolvePossiblyDirectEval(int arg_count) {
  EMIT_STUB_MARKER(240);
  // Push copy of the first argument or undefined if it doesn't exist.
  if (arg_count > 0) {
    __ LoadWord(r4, MemOperand(sp, arg_count * kPointerSize), r0);
  } else {
    __ LoadRoot(r4, Heap::kUndefinedValueRootIndex);
  }
  __ push(r4);

  // Push the receiver of the enclosing function.
  int receiver_offset = 2 + info_->scope()->num_parameters();
  __ LoadWord(r4, MemOperand(fp, receiver_offset * kPointerSize), r0);
  __ push(r4);
  // Push the language mode.
  __ li(r4, Operand(Smi::FromInt(language_mode())));
  __ push(r4);

  // Push the start position of the scope the calls resides in.
  __ mov(r4, Operand(Smi::FromInt(scope()->start_position())));
  __ push(r4);

  // Do the runtime call.
  __ CallRuntime(Runtime::kResolvePossiblyDirectEval, 5);
}


void FullCodeGenerator::VisitCall(Call* expr) {
  EMIT_STUB_MARKER(241);
#ifdef DEBUG
  // We want to verify that RecordJSReturnSite gets called on all paths
  // through this function.  Avoid early returns.
  expr->return_is_recorded_ = false;
#endif

  Comment cmnt(masm_, "[ Call");
  Expression* callee = expr->expression();
  VariableProxy* proxy = callee->AsVariableProxy();
  Property* property = callee->AsProperty();

  if (proxy != NULL && proxy->var()->is_possibly_eval()) {
    // In a call to eval, we first call %ResolvePossiblyDirectEval to
    // resolve the function we need to call and the receiver of the
    // call.  Then we call the resolved function using the given
    // arguments.
    ZoneList<Expression*>* args = expr->arguments();
    int arg_count = args->length();

    { PreservePositionScope pos_scope(masm()->positions_recorder());
      VisitForStackValue(callee);
      __ LoadRoot(r5, Heap::kUndefinedValueRootIndex);
      __ push(r5);  // Reserved receiver slot.

      // Push the arguments.
      for (int i = 0; i < arg_count; i++) {
        VisitForStackValue(args->at(i));
      }

      // Push a copy of the function (found below the arguments) and
      // resolve eval.
      __ LoadWord(r4, MemOperand(sp, (arg_count + 1) * kPointerSize), r0);
      __ push(r4);
      EmitResolvePossiblyDirectEval(arg_count);

      // The runtime call returns a pair of values in r3 (function) and
      // r4 (receiver). Touch up the stack with the right values.
      __ StoreWord(r3, MemOperand(sp, (arg_count + 1) * kPointerSize), r0);
      __ StoreWord(r4, MemOperand(sp, arg_count * kPointerSize), r0);
    }

    // Record source position for debugger.
    SetSourcePosition(expr->position());
    CallFunctionStub stub(arg_count, RECEIVER_MIGHT_BE_IMPLICIT);
    __ LoadWord(r4, MemOperand(sp, (arg_count + 1) * kPointerSize), r0);
    __ CallStub(&stub);
    RecordJSReturnSite(expr);
    // Restore context register.
    __ lwz(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
    context()->DropAndPlug(1, r3);
  } else if (proxy != NULL && proxy->var()->IsUnallocated()) {
    // Push global object as receiver for the call IC.
    __ lwz(r3, GlobalObjectOperand());
    __ push(r3);
    EmitCallWithIC(expr, proxy->name(), RelocInfo::CODE_TARGET_CONTEXT);
  } else if (proxy != NULL && proxy->var()->IsLookupSlot()) {
    // Call to a lookup slot (dynamically introduced variable).
    Label slow, done;

    { PreservePositionScope scope(masm()->positions_recorder());
      // Generate code for loading from variables potentially shadowed
      // by eval-introduced variables.
      EmitDynamicLookupFastCase(proxy->var(), NOT_INSIDE_TYPEOF, &slow, &done);
    }

    __ bind(&slow);
    // Call the runtime to find the function to call (returned in r3)
    // and the object holding it (returned in edx).
    __ push(context_register());
    __ mov(r5, Operand(proxy->name()));
    __ push(r5);
    __ CallRuntime(Runtime::kLoadContextSlot, 2);
    __ Push(r3, r4);  // Function, receiver.

    // If fast case code has been generated, emit code to push the
    // function and receiver and have the slow path jump around this
    // code.
    if (done.is_linked()) {
      Label call;
      __ b(&call);
      __ bind(&done);
      // Push function.
      __ push(r3);
      // The receiver is implicitly the global receiver. Indicate this
      // by passing the hole to the call function stub.
      __ LoadRoot(r4, Heap::kTheHoleValueRootIndex);
      __ push(r4);
      __ bind(&call);
    }

    // The receiver is either the global receiver or an object found
    // by LoadContextSlot. That object could be the hole if the
    // receiver is implicitly the global object.
    EmitCallWithStub(expr, RECEIVER_MIGHT_BE_IMPLICIT);
  } else if (property != NULL) {
    { PreservePositionScope scope(masm()->positions_recorder());
      VisitForStackValue(property->obj());
    }
    if (property->key()->IsPropertyName()) {
      EmitCallWithIC(expr,
                     property->key()->AsLiteral()->handle(),
                     RelocInfo::CODE_TARGET);
    } else {
      EmitKeyedCallWithIC(expr, property->key());
    }
  } else {
    // Call to an arbitrary expression not handled specially above.
    { PreservePositionScope scope(masm()->positions_recorder());
      VisitForStackValue(callee);
    }
    // Load global receiver object.
    __ lwz(r4, GlobalObjectOperand());
    __ lwz(r4, FieldMemOperand(r4, GlobalObject::kGlobalReceiverOffset));
    __ push(r4);
    // Emit function call.
    EmitCallWithStub(expr, NO_CALL_FUNCTION_FLAGS);
  }

#ifdef DEBUG
  // RecordJSReturnSite should have been called.
  ASSERT(expr->return_is_recorded_);
#endif
}


void FullCodeGenerator::VisitCallNew(CallNew* expr) {
  EMIT_STUB_MARKER(242);
  Comment cmnt(masm_, "[ CallNew");
  // According to ECMA-262, section 11.2.2, page 44, the function
  // expression in new calls must be evaluated before the
  // arguments.

  // Push constructor on the stack.  If it's not a function it's used as
  // receiver for CALL_NON_FUNCTION, otherwise the value on the stack is
  // ignored.
  VisitForStackValue(expr->expression());

  // Push the arguments ("left-to-right") on the stack.
  ZoneList<Expression*>* args = expr->arguments();
  int arg_count = args->length();
  for (int i = 0; i < arg_count; i++) {
    VisitForStackValue(args->at(i));
  }

  // Call the construct call builtin that handles allocation and
  // constructor invocation.
  SetSourcePosition(expr->position());

  // Load function and argument count into r4 and r3.
  __ mov(r3, Operand(arg_count));
  __ LoadWord(r4, MemOperand(sp, arg_count * kPointerSize), r0);

  // Record call targets in unoptimized code.
  Handle<Object> uninitialized =
      TypeFeedbackCells::UninitializedSentinel(isolate());
  Handle<JSGlobalPropertyCell> cell =
      isolate()->factory()->NewJSGlobalPropertyCell(uninitialized);
  RecordTypeFeedbackCell(expr->CallNewFeedbackId(), cell);
  __ mov(r5, Operand(cell));

  CallConstructStub stub(RECORD_CALL_TARGET);
  __ Call(stub.GetCode(), RelocInfo::CONSTRUCT_CALL);
  PrepareForBailoutForId(expr->ReturnId(), TOS_REG);
  context()->Plug(r3);
}


void FullCodeGenerator::EmitIsSmi(CallRuntime* expr) {
  EMIT_STUB_MARKER(243);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 1);

  VisitForAccumulatorValue(args->at(0));

  Label materialize_true, materialize_false;
  Label* if_true = NULL;
  Label* if_false = NULL;
  Label* fall_through = NULL;
  context()->PrepareTest(&materialize_true, &materialize_false,
                         &if_true, &if_false, &fall_through);

  PrepareForBailoutBeforeSplit(expr, true, if_true, if_false);
  __ andi(r0, r3, Operand(kSmiTagMask));
  __ cmpi(r0, Operand(0));
  Split(eq, if_true, if_false, fall_through);

  context()->Plug(if_true, if_false);
}


void FullCodeGenerator::EmitIsNonNegativeSmi(CallRuntime* expr) {
  EMIT_STUB_MARKER(244);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 1);

  VisitForAccumulatorValue(args->at(0));

  Label materialize_true, materialize_false;
  Label* if_true = NULL;
  Label* if_false = NULL;
  Label* fall_through = NULL;
  context()->PrepareTest(&materialize_true, &materialize_false,
                         &if_true, &if_false, &fall_through);

  PrepareForBailoutBeforeSplit(expr, true, if_true, if_false);
  ASSERT((kSmiTagMask | 0x80000000) == 0x80000001);
  __ rlwinm(r0, r3, 1, 30, 31);
  __ cmpi(r0, Operand(0));
  // was .. __ tst(r3, Operand(kSmiTagMask | 0x80000000));
  Split(eq, if_true, if_false, fall_through);

  context()->Plug(if_true, if_false);
}


void FullCodeGenerator::EmitIsObject(CallRuntime* expr) {
  EMIT_STUB_MARKER(245);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 1);

  VisitForAccumulatorValue(args->at(0));

  Label materialize_true, materialize_false;
  Label* if_true = NULL;
  Label* if_false = NULL;
  Label* fall_through = NULL;
  context()->PrepareTest(&materialize_true, &materialize_false,
                         &if_true, &if_false, &fall_through);

  __ JumpIfSmi(r3, if_false);
  __ LoadRoot(ip, Heap::kNullValueRootIndex);
  __ cmp(r3, ip);
  __ beq(if_true);
  __ lwz(r5, FieldMemOperand(r3, HeapObject::kMapOffset));
  // Undetectable objects behave like undefined when tested with typeof.
  __ lbz(r4, FieldMemOperand(r5, Map::kBitFieldOffset));
  __ andi(r0, r4, Operand(1 << Map::kIsUndetectable));
  __ bne(if_false, cr0);
  __ lbz(r4, FieldMemOperand(r5, Map::kInstanceTypeOffset));
  __ cmpi(r4, Operand(FIRST_NONCALLABLE_SPEC_OBJECT_TYPE));
  __ blt(if_false);
  __ cmpi(r4, Operand(LAST_NONCALLABLE_SPEC_OBJECT_TYPE));
  PrepareForBailoutBeforeSplit(expr, true, if_true, if_false);
  Split(le, if_true, if_false, fall_through);

  context()->Plug(if_true, if_false);
}


void FullCodeGenerator::EmitIsSpecObject(CallRuntime* expr) {
  EMIT_STUB_MARKER(246);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 1);

  VisitForAccumulatorValue(args->at(0));

  Label materialize_true, materialize_false;
  Label* if_true = NULL;
  Label* if_false = NULL;
  Label* fall_through = NULL;
  context()->PrepareTest(&materialize_true, &materialize_false,
                         &if_true, &if_false, &fall_through);

  __ JumpIfSmi(r3, if_false);
  __ CompareObjectType(r3, r4, r4, FIRST_SPEC_OBJECT_TYPE);
  PrepareForBailoutBeforeSplit(expr, true, if_true, if_false);
  Split(ge, if_true, if_false, fall_through);

  context()->Plug(if_true, if_false);
}


void FullCodeGenerator::EmitIsUndetectableObject(CallRuntime* expr) {
  EMIT_STUB_MARKER(247);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 1);

  VisitForAccumulatorValue(args->at(0));

  Label materialize_true, materialize_false;
  Label* if_true = NULL;
  Label* if_false = NULL;
  Label* fall_through = NULL;
  context()->PrepareTest(&materialize_true, &materialize_false,
                         &if_true, &if_false, &fall_through);

  __ JumpIfSmi(r3, if_false);
  __ lwz(r4, FieldMemOperand(r3, HeapObject::kMapOffset));
  __ lbz(r4, FieldMemOperand(r4, Map::kBitFieldOffset));
  __ andi(r0, r4, Operand(1 << Map::kIsUndetectable));
  __ cmpi(r0, Operand(0));
  PrepareForBailoutBeforeSplit(expr, true, if_true, if_false);
  Split(ne, if_true, if_false, fall_through);

  context()->Plug(if_true, if_false);
}


void FullCodeGenerator::EmitIsStringWrapperSafeForDefaultValueOf(
    CallRuntime* expr) {
  EMIT_STUB_MARKER(248);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 1);

  VisitForAccumulatorValue(args->at(0));

  Label materialize_true, materialize_false;
  Label* if_true = NULL;
  Label* if_false = NULL;
  Label* fall_through = NULL;
  context()->PrepareTest(&materialize_true, &materialize_false,
                         &if_true, &if_false, &fall_through);

  if (generate_debug_code_) __ AbortIfSmi(r3);

  __ lwz(r4, FieldMemOperand(r3, HeapObject::kMapOffset));
  __ lbz(ip, FieldMemOperand(r4, Map::kBitField2Offset));
  __ andi(r0, ip, Operand(1 << Map::kStringWrapperSafeForDefaultValueOf));
  __ bne(if_true, cr0);

  // Check for fast case object. Generate false result for slow case object.
  __ lwz(r5, FieldMemOperand(r3, JSObject::kPropertiesOffset));
  __ lwz(r5, FieldMemOperand(r5, HeapObject::kMapOffset));
  __ LoadRoot(ip, Heap::kHashTableMapRootIndex);
  __ cmp(r5, ip);
  __ beq(if_false);

  // Look for valueOf symbol in the descriptor array, and indicate false if
  // found. Since we omit an enumeration index check, if it is added via a
  // transition that shares its descriptor array, this is a false positive.
  Label entry, loop, done;

  // Skip loop if no descriptors are valid.
  __ NumberOfOwnDescriptors(r6, r4);
  __ cmpi(r6, Operand(0));
  __ beq(&done);

  __ LoadInstanceDescriptors(r4, r7, r5);
  // r7: descriptor array.
  // r6: valid entries in the descriptor array.
  STATIC_ASSERT(kSmiTag == 0);
  STATIC_ASSERT(kSmiTagSize == 1);
  STATIC_ASSERT(kPointerSize == 4);
  __ mov(ip, Operand(DescriptorArray::kDescriptorSize));
  __ mul(r6, r6, ip);
  // Calculate location of the first key name.
  __ addi(r7, r7, Operand(DescriptorArray::kFirstOffset - kHeapObjectTag));
  // Calculate the end of the descriptor array.
  __ mr(r5, r7);
  __ slwi(ip, r6, Operand(kPointerSizeLog2 - kSmiTagSize));
  __ add(r5, r5, ip);

  // Loop through all the keys in the descriptor array. If one of these is the
  // symbol valueOf the result is false.
  // The use of ip to store the valueOf symbol asumes that it is not otherwise
  // used in the loop below.
  __ mov(ip, Operand(FACTORY->value_of_symbol()));
  __ jmp(&entry);
  __ bind(&loop);
  __ lwz(r6, MemOperand(r7, 0));
  __ cmp(r6, ip);
  __ beq(if_false);
  __ addi(r7, r7, Operand(DescriptorArray::kDescriptorSize * kPointerSize));
  __ bind(&entry);
  __ cmp(r7, r5);
  __ bne(&loop);

  __ bind(&done);
  // If a valueOf property is not found on the object check that its
  // prototype is the un-modified String prototype. If not result is false.
  __ lwz(r5, FieldMemOperand(r4, Map::kPrototypeOffset));
  __ JumpIfSmi(r5, if_false);
  __ lwz(r5, FieldMemOperand(r5, HeapObject::kMapOffset));
  __ lwz(r6, ContextOperand(cp, Context::GLOBAL_OBJECT_INDEX));
  __ lwz(r6, FieldMemOperand(r6, GlobalObject::kNativeContextOffset));
  __ lwz(r6, ContextOperand(r6, Context::STRING_FUNCTION_PROTOTYPE_MAP_INDEX));
  __ cmp(r5, r6);
  __ bne(if_false);

  // Set the bit in the map to indicate that it has been checked safe for
  // default valueOf and set true result.
  __ lbz(r5, FieldMemOperand(r4, Map::kBitField2Offset));
  __ ori(r5, r5, Operand(1 << Map::kStringWrapperSafeForDefaultValueOf));
  __ stb(r5, FieldMemOperand(r4, Map::kBitField2Offset));
  __ jmp(if_true);

  PrepareForBailoutBeforeSplit(expr, true, if_true, if_false);
  context()->Plug(if_true, if_false);
}


void FullCodeGenerator::EmitIsFunction(CallRuntime* expr) {
  EMIT_STUB_MARKER(249);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 1);

  VisitForAccumulatorValue(args->at(0));

  Label materialize_true, materialize_false;
  Label* if_true = NULL;
  Label* if_false = NULL;
  Label* fall_through = NULL;
  context()->PrepareTest(&materialize_true, &materialize_false,
                         &if_true, &if_false, &fall_through);

  __ JumpIfSmi(r3, if_false);
  __ CompareObjectType(r3, r4, r5, JS_FUNCTION_TYPE);
  PrepareForBailoutBeforeSplit(expr, true, if_true, if_false);
  Split(eq, if_true, if_false, fall_through);

  context()->Plug(if_true, if_false);
}


void FullCodeGenerator::EmitIsArray(CallRuntime* expr) {
  EMIT_STUB_MARKER(250);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 1);

  VisitForAccumulatorValue(args->at(0));

  Label materialize_true, materialize_false;
  Label* if_true = NULL;
  Label* if_false = NULL;
  Label* fall_through = NULL;
  context()->PrepareTest(&materialize_true, &materialize_false,
                         &if_true, &if_false, &fall_through);

  __ JumpIfSmi(r3, if_false);
  __ CompareObjectType(r3, r4, r4, JS_ARRAY_TYPE);
  PrepareForBailoutBeforeSplit(expr, true, if_true, if_false);
  Split(eq, if_true, if_false, fall_through);

  context()->Plug(if_true, if_false);
}


void FullCodeGenerator::EmitIsRegExp(CallRuntime* expr) {
  EMIT_STUB_MARKER(251);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 1);

  VisitForAccumulatorValue(args->at(0));

  Label materialize_true, materialize_false;
  Label* if_true = NULL;
  Label* if_false = NULL;
  Label* fall_through = NULL;
  context()->PrepareTest(&materialize_true, &materialize_false,
                         &if_true, &if_false, &fall_through);

  __ JumpIfSmi(r3, if_false);
  __ CompareObjectType(r3, r4, r4, JS_REGEXP_TYPE);
  PrepareForBailoutBeforeSplit(expr, true, if_true, if_false);
  Split(eq, if_true, if_false, fall_through);

  context()->Plug(if_true, if_false);
}



void FullCodeGenerator::EmitIsConstructCall(CallRuntime* expr) {
  EMIT_STUB_MARKER(252);
  ASSERT(expr->arguments()->length() == 0);

  Label materialize_true, materialize_false;
  Label* if_true = NULL;
  Label* if_false = NULL;
  Label* fall_through = NULL;
  context()->PrepareTest(&materialize_true, &materialize_false,
                         &if_true, &if_false, &fall_through);

  // Get the frame pointer for the calling frame.
  __ lwz(r5, MemOperand(fp, StandardFrameConstants::kCallerFPOffset));

  // Skip the arguments adaptor frame if it exists.
  Label check_frame_marker;
  __ lwz(r4, MemOperand(r5, StandardFrameConstants::kContextOffset));
  __ cmpi(r4, Operand(Smi::FromInt(StackFrame::ARGUMENTS_ADAPTOR)));
  __ bne(&check_frame_marker);
  __ lwz(r5, MemOperand(r5, StandardFrameConstants::kCallerFPOffset));

  // Check the marker in the calling frame.
  __ bind(&check_frame_marker);
  __ lwz(r4, MemOperand(r5, StandardFrameConstants::kMarkerOffset));
  STATIC_ASSERT(StackFrame::CONSTRUCT < 0x4000);
  __ cmpi(r4, Operand(Smi::FromInt(StackFrame::CONSTRUCT)));
  PrepareForBailoutBeforeSplit(expr, true, if_true, if_false);
  Split(eq, if_true, if_false, fall_through);

  context()->Plug(if_true, if_false);
}


void FullCodeGenerator::EmitObjectEquals(CallRuntime* expr) {
  EMIT_STUB_MARKER(253);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 2);

  // Load the two objects into registers and perform the comparison.
  VisitForStackValue(args->at(0));
  VisitForAccumulatorValue(args->at(1));

  Label materialize_true, materialize_false;
  Label* if_true = NULL;
  Label* if_false = NULL;
  Label* fall_through = NULL;
  context()->PrepareTest(&materialize_true, &materialize_false,
                         &if_true, &if_false, &fall_through);

  __ pop(r4);
  __ cmp(r3, r4);
  PrepareForBailoutBeforeSplit(expr, true, if_true, if_false);
  Split(eq, if_true, if_false, fall_through);

  context()->Plug(if_true, if_false);
}


void FullCodeGenerator::EmitArguments(CallRuntime* expr) {
  EMIT_STUB_MARKER(254);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 1);

  // ArgumentsAccessStub expects the key in edx and the formal
  // parameter count in r3.
  VisitForAccumulatorValue(args->at(0));
  __ mr(r4, r3);
  __ li(r3, Operand(Smi::FromInt(info_->scope()->num_parameters())));
  ArgumentsAccessStub stub(ArgumentsAccessStub::READ_ELEMENT);
  __ CallStub(&stub);
  context()->Plug(r3);
}


void FullCodeGenerator::EmitArgumentsLength(CallRuntime* expr) {
  EMIT_STUB_MARKER(255);
  ASSERT(expr->arguments()->length() == 0);
  Label exit;
  // Get the number of formal parameters.
  __ li(r3, Operand(Smi::FromInt(info_->scope()->num_parameters())));

  // Check if the calling frame is an arguments adaptor frame.
  __ lwz(r5, MemOperand(fp, StandardFrameConstants::kCallerFPOffset));
  __ lwz(r6, MemOperand(r5, StandardFrameConstants::kContextOffset));
  __ cmpi(r6, Operand(Smi::FromInt(StackFrame::ARGUMENTS_ADAPTOR)));
  __ bne(&exit);

  // Arguments adaptor case: Read the arguments length from the
  // adaptor frame.
  __ lwz(r3, MemOperand(r5, ArgumentsAdaptorFrameConstants::kLengthOffset));

  __ bind(&exit);
  context()->Plug(r3);
}


void FullCodeGenerator::EmitClassOf(CallRuntime* expr) {
  EMIT_STUB_MARKER(256);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 1);
  Label done, null, function, non_function_constructor;

  VisitForAccumulatorValue(args->at(0));

  // If the object is a smi, we return null.
  __ JumpIfSmi(r3, &null);

  // Check that the object is a JS object but take special care of JS
  // functions to make sure they have 'Function' as their class.
  // Assume that there are only two callable types, and one of them is at
  // either end of the type range for JS object types. Saves extra comparisons.
  STATIC_ASSERT(NUM_OF_CALLABLE_SPEC_OBJECT_TYPES == 2);
  __ CompareObjectType(r3, r3, r4, FIRST_SPEC_OBJECT_TYPE);
  // Map is now in r3.
  __ blt(&null);
  STATIC_ASSERT(FIRST_NONCALLABLE_SPEC_OBJECT_TYPE ==
                FIRST_SPEC_OBJECT_TYPE + 1);
  __ beq(&function);

  __ cmpi(r4, Operand(LAST_SPEC_OBJECT_TYPE));
  STATIC_ASSERT(LAST_NONCALLABLE_SPEC_OBJECT_TYPE ==
                LAST_SPEC_OBJECT_TYPE - 1);
  __ beq(&function);
  // Assume that there is no larger type.
  STATIC_ASSERT(LAST_NONCALLABLE_SPEC_OBJECT_TYPE == LAST_TYPE - 1);

  // Check if the constructor in the map is a JS function.
  __ lwz(r3, FieldMemOperand(r3, Map::kConstructorOffset));
  __ CompareObjectType(r3, r4, r4, JS_FUNCTION_TYPE);
  __ bne(&non_function_constructor);

  // r3 now contains the constructor function. Grab the
  // instance class name from there.
  __ lwz(r3, FieldMemOperand(r3, JSFunction::kSharedFunctionInfoOffset));
  __ lwz(r3, FieldMemOperand(r3, SharedFunctionInfo::kInstanceClassNameOffset));
  __ b(&done);

  // Functions have class 'Function'.
  __ bind(&function);
  __ LoadRoot(r3, Heap::kfunction_class_symbolRootIndex);
  __ jmp(&done);

  // Objects with a non-function constructor have class 'Object'.
  __ bind(&non_function_constructor);
  __ LoadRoot(r3, Heap::kObject_symbolRootIndex);
  __ jmp(&done);

  // Non-JS objects have class null.
  __ bind(&null);
  __ LoadRoot(r3, Heap::kNullValueRootIndex);

  // All done.
  __ bind(&done);

  context()->Plug(r3);
}


void FullCodeGenerator::EmitLog(CallRuntime* expr) {
  EMIT_STUB_MARKER(257);
  // Conditionally generate a log call.
  // Args:
  //   0 (literal string): The type of logging (corresponds to the flags).
  //     This is used to determine whether or not to generate the log call.
  //   1 (string): Format string.  Access the string at argument index 2
  //     with '%2s' (see Logger::LogRuntime for all the formats).
  //   2 (array): Arguments to the format string.
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT_EQ(args->length(), 3);
  if (CodeGenerator::ShouldGenerateLog(args->at(0))) {
    VisitForStackValue(args->at(1));
    VisitForStackValue(args->at(2));
    __ CallRuntime(Runtime::kLog, 2);
  }

  // Finally, we're expected to leave a value on the top of the stack.
  __ LoadRoot(r3, Heap::kUndefinedValueRootIndex);
  context()->Plug(r3);
}


void FullCodeGenerator::EmitRandomHeapNumber(CallRuntime* expr) {
  EMIT_STUB_MARKER(258);
  ASSERT(expr->arguments()->length() == 0);
  Label slow_allocate_heapnumber;
  Label heapnumber_allocated;

  __ LoadRoot(r9, Heap::kHeapNumberMapRootIndex);
  __ AllocateHeapNumber(r7, r4, r5, r9, &slow_allocate_heapnumber);
  __ jmp(&heapnumber_allocated);

  __ bind(&slow_allocate_heapnumber);
  // Allocate a heap number.
  __ CallRuntime(Runtime::kNumberAlloc, 0);
  __ mr(r7, r3);

  __ bind(&heapnumber_allocated);

#if 0  // optimal ARM code - need to reimplement on PowerPC
  // Convert 32 random bits in r3 to 0.(32 random bits) in a double
  // by computing:
  // ( 1.(20 0s)(32 random bits) x 2^20 ) - (1.0 x 2^20)).
  if (CpuFeatures::IsSupported(VFP2)) {
    __ PrepareCallCFunction(1, r0);
    __ lwz(r0,
           ContextOperand(context_register(), Context::GLOBAL_OBJECT_INDEX));
    __ lwz(r0, FieldMemOperand(r0, GlobalObject::kNativeContextOffset));
    __ CallCFunction(ExternalReference::random_uint32_function(isolate()), 1);

    CpuFeatures::Scope scope(VFP2);
    // 0x41300000 is the top half of 1.0 x 2^20 as a double.
    // Create this constant using mov/orr to avoid PC relative load.
    __ mov(r1, Operand(0x41000000));
    __ orr(r1, r1, Operand(0x300000));
    // Move 0x41300000xxxxxxxx (x = random bits) to VFP.
    __ vmov(d7, r0, r1);
    // Move 0x4130000000000000 to VFP.
    __ mov(r0, Operand(0, RelocInfo::NONE));
    __ vmov(d8, r0, r1);
    // Subtract and store the result in the heap number.
    __ vsub(d7, d7, d8);
    __ sub(r0, r4, Operand(kHeapObjectTag));
    __ vstw(d7, r0, HeapNumber::kValueOffset);
    __ mr(r0, r4);
  } else {
#endif
    __ PrepareCallCFunction(2, r3);
    __ lwz(r4,
           ContextOperand(context_register(), Context::GLOBAL_OBJECT_INDEX));
    __ mr(r3, r7);
    __ lwz(r4, FieldMemOperand(r4, GlobalObject::kNativeContextOffset));
    __ CallCFunction(
        ExternalReference::fill_heap_number_with_random_function(isolate()), 2);
//  }

  context()->Plug(r3);
}


void FullCodeGenerator::EmitSubString(CallRuntime* expr) {
  EMIT_STUB_MARKER(259);
  // Load the arguments on the stack and call the stub.
  SubStringStub stub;
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 3);
  VisitForStackValue(args->at(0));
  VisitForStackValue(args->at(1));
  VisitForStackValue(args->at(2));
  __ CallStub(&stub);
  context()->Plug(r3);
}


void FullCodeGenerator::EmitRegExpExec(CallRuntime* expr) {
  EMIT_STUB_MARKER(260);
  // Load the arguments on the stack and call the stub.
  RegExpExecStub stub;
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 4);
  VisitForStackValue(args->at(0));
  VisitForStackValue(args->at(1));
  VisitForStackValue(args->at(2));
  VisitForStackValue(args->at(3));
  __ CallStub(&stub);
  context()->Plug(r3);
}


void FullCodeGenerator::EmitValueOf(CallRuntime* expr) {
  EMIT_STUB_MARKER(261);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 1);
  VisitForAccumulatorValue(args->at(0));  // Load the object.

  Label done;
  // If the object is a smi return the object.
  __ JumpIfSmi(r3, &done);
  // If the object is not a value type, return the object.
  __ CompareObjectType(r3, r4, r4, JS_VALUE_TYPE);
  __ b(ne, &done);
  __ lwz(r3, FieldMemOperand(r3, JSValue::kValueOffset));

  __ bind(&done);
  context()->Plug(r3);
}


void FullCodeGenerator::EmitDateField(CallRuntime* expr) {
  EMIT_STUB_MARKER(262);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 2);
  ASSERT_NE(NULL, args->at(1)->AsLiteral());
  Smi* index = Smi::cast(*(args->at(1)->AsLiteral()->handle()));

  VisitForAccumulatorValue(args->at(0));  // Load the object.

  Label runtime, done, not_date_object;
  Register object = r3;
  Register result = r3;
  Register scratch0 = r22;
  Register scratch1 = r4;

  __ JumpIfSmi(object, &not_date_object);
  __ CompareObjectType(object, scratch1, scratch1, JS_DATE_TYPE);
  __ bne(&not_date_object);

  if (index->value() == 0) {
    __ lwz(result, FieldMemOperand(object, JSDate::kValueOffset));
    __ jmp(&done);
  } else {
    if (index->value() < JSDate::kFirstUncachedField) {
      ExternalReference stamp = ExternalReference::date_cache_stamp(isolate());
      __ mov(scratch1, Operand(stamp));
      __ lwz(scratch1, MemOperand(scratch1));
      __ lwz(scratch0, FieldMemOperand(object, JSDate::kCacheStampOffset));
      __ cmp(scratch1, scratch0);
      __ bne(&runtime);
      __ LoadWord(result, FieldMemOperand(object, JSDate::kValueOffset +
                  kPointerSize * index->value()), scratch0);
      __ jmp(&done);
    }
    __ bind(&runtime);
    __ PrepareCallCFunction(2, scratch1);
    __ li(r4, Operand(index));
    __ CallCFunction(ExternalReference::get_date_field_function(isolate()), 2);
    __ jmp(&done);
  }

  __ bind(&not_date_object);
  __ CallRuntime(Runtime::kThrowNotDateError, 0);
  __ bind(&done);
  context()->Plug(r3);
}


void FullCodeGenerator::EmitMathPow(CallRuntime* expr) {
  EMIT_STUB_MARKER(263);
  // Load the arguments on the stack and call the runtime function.
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 2);
  VisitForStackValue(args->at(0));
  VisitForStackValue(args->at(1));
#if 0
  if (CpuFeatures::IsSupported(VFP2)) {
    MathPowStub stub(MathPowStub::ON_STACK);
    __ CallStub(&stub);
  } else {
#endif
    __ CallRuntime(Runtime::kMath_pow, 2);
  // }
  context()->Plug(r3);
}


void FullCodeGenerator::EmitSetValueOf(CallRuntime* expr) {
  EMIT_STUB_MARKER(264);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 2);
  VisitForStackValue(args->at(0));  // Load the object.
  VisitForAccumulatorValue(args->at(1));  // Load the value.
  __ pop(r4);  // r3 = value. r4 = object.

  Label done;
  // If the object is a smi, return the value.
  __ JumpIfSmi(r4, &done);

  // If the object is not a value type, return the value.
  __ CompareObjectType(r4, r5, r5, JS_VALUE_TYPE);
  __ b(ne, &done);

  // Store the value.
  __ stw(r3, FieldMemOperand(r4, JSValue::kValueOffset));
  // Update the write barrier.  Save the value as it will be
  // overwritten by the write barrier code and is needed afterward.
  __ mr(r5, r3);
  __ RecordWriteField(
      r4, JSValue::kValueOffset, r5, r6, kLRHasBeenSaved, kDontSaveFPRegs);

  __ bind(&done);
  context()->Plug(r3);
}


void FullCodeGenerator::EmitNumberToString(CallRuntime* expr) {
  EMIT_STUB_MARKER(265);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT_EQ(args->length(), 1);
  // Load the argument on the stack and call the stub.
  VisitForStackValue(args->at(0));

  NumberToStringStub stub;
  __ CallStub(&stub);
  context()->Plug(r3);
}


void FullCodeGenerator::EmitStringCharFromCode(CallRuntime* expr) {
  EMIT_STUB_MARKER(266);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 1);
  VisitForAccumulatorValue(args->at(0));

  Label done;
  StringCharFromCodeGenerator generator(r3, r4);
  generator.GenerateFast(masm_);
  __ jmp(&done);

  NopRuntimeCallHelper call_helper;
  generator.GenerateSlow(masm_, call_helper);

  __ bind(&done);
  context()->Plug(r4);
}


void FullCodeGenerator::EmitStringCharCodeAt(CallRuntime* expr) {
  EMIT_STUB_MARKER(267);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 2);
  VisitForStackValue(args->at(0));
  VisitForAccumulatorValue(args->at(1));

  Register object = r4;
  Register index = r3;
  Register result = r6;

  __ pop(object);

  Label need_conversion;
  Label index_out_of_range;
  Label done;
  StringCharCodeAtGenerator generator(object,
                                      index,
                                      result,
                                      &need_conversion,
                                      &need_conversion,
                                      &index_out_of_range,
                                      STRING_INDEX_IS_NUMBER);
  generator.GenerateFast(masm_);
  __ jmp(&done);

  __ bind(&index_out_of_range);
  // When the index is out of range, the spec requires us to return
  // NaN.
  __ LoadRoot(result, Heap::kNanValueRootIndex);
  __ jmp(&done);

  __ bind(&need_conversion);
  // Load the undefined value into the result register, which will
  // trigger conversion.
  __ LoadRoot(result, Heap::kUndefinedValueRootIndex);
  __ jmp(&done);

  NopRuntimeCallHelper call_helper;
  generator.GenerateSlow(masm_, call_helper);

  __ bind(&done);
  context()->Plug(result);
}


void FullCodeGenerator::EmitStringCharAt(CallRuntime* expr) {
  EMIT_STUB_MARKER(268);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 2);
  VisitForStackValue(args->at(0));
  VisitForAccumulatorValue(args->at(1));

  Register object = r4;
  Register index = r3;
  Register scratch = r6;
  Register result = r3;

  __ pop(object);

  Label need_conversion;
  Label index_out_of_range;
  Label done;
  StringCharAtGenerator generator(object,
                                  index,
                                  scratch,
                                  result,
                                  &need_conversion,
                                  &need_conversion,
                                  &index_out_of_range,
                                  STRING_INDEX_IS_NUMBER);
  generator.GenerateFast(masm_);
  __ jmp(&done);

  __ bind(&index_out_of_range);
  // When the index is out of range, the spec requires us to return
  // the empty string.
  __ LoadRoot(result, Heap::kEmptyStringRootIndex);
  __ jmp(&done);

  __ bind(&need_conversion);
  // Move smi zero into the result register, which will trigger
  // conversion.
  __ li(result, Operand(Smi::FromInt(0)));
  __ jmp(&done);

  NopRuntimeCallHelper call_helper;
  generator.GenerateSlow(masm_, call_helper);

  __ bind(&done);
  context()->Plug(result);
}


void FullCodeGenerator::EmitStringAdd(CallRuntime* expr) {
  EMIT_STUB_MARKER(269);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT_EQ(2, args->length());
  VisitForStackValue(args->at(0));
  VisitForStackValue(args->at(1));

  StringAddStub stub(NO_STRING_ADD_FLAGS);
  __ CallStub(&stub);
  context()->Plug(r3);
}


void FullCodeGenerator::EmitStringCompare(CallRuntime* expr) {
  EMIT_STUB_MARKER(270);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT_EQ(2, args->length());
  VisitForStackValue(args->at(0));
  VisitForStackValue(args->at(1));

  StringCompareStub stub;
  __ CallStub(&stub);
  context()->Plug(r3);
}


void FullCodeGenerator::EmitMathSin(CallRuntime* expr) {
  EMIT_STUB_MARKER(271);
  // Load the argument on the stack and call the stub.
  TranscendentalCacheStub stub(TranscendentalCache::SIN,
                               TranscendentalCacheStub::TAGGED);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 1);
  VisitForStackValue(args->at(0));
  __ CallStub(&stub);
  context()->Plug(r3);
}


void FullCodeGenerator::EmitMathCos(CallRuntime* expr) {
  EMIT_STUB_MARKER(272);
  // Load the argument on the stack and call the stub.
  TranscendentalCacheStub stub(TranscendentalCache::COS,
                               TranscendentalCacheStub::TAGGED);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 1);
  VisitForStackValue(args->at(0));
  __ CallStub(&stub);
  context()->Plug(r3);
}


void FullCodeGenerator::EmitMathTan(CallRuntime* expr) {
  EMIT_STUB_MARKER(273);
  // Load the argument on the stack and call the stub.
  TranscendentalCacheStub stub(TranscendentalCache::TAN,
                               TranscendentalCacheStub::TAGGED);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 1);
  VisitForStackValue(args->at(0));
  __ CallStub(&stub);
  context()->Plug(r3);
}


void FullCodeGenerator::EmitMathLog(CallRuntime* expr) {
  EMIT_STUB_MARKER(274);
  // Load the argument on the stack and call the stub.
  TranscendentalCacheStub stub(TranscendentalCache::LOG,
                               TranscendentalCacheStub::TAGGED);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 1);
  VisitForStackValue(args->at(0));
  __ CallStub(&stub);
  context()->Plug(r3);
}


void FullCodeGenerator::EmitMathSqrt(CallRuntime* expr) {
  EMIT_STUB_MARKER(275);
  // Load the argument on the stack and call the runtime function.
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 1);
  VisitForStackValue(args->at(0));
  __ CallRuntime(Runtime::kMath_sqrt, 1);
  context()->Plug(r3);
}


void FullCodeGenerator::EmitCallFunction(CallRuntime* expr) {
  EMIT_STUB_MARKER(276);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() >= 2);

  int arg_count = args->length() - 2;  // 2 ~ receiver and function.
  for (int i = 0; i < arg_count + 1; i++) {
    VisitForStackValue(args->at(i));
  }
  VisitForAccumulatorValue(args->last());  // Function.

  Label runtime, done;
  // Check for non-function argument (including proxy).
  __ JumpIfSmi(r3, &runtime);
  __ CompareObjectType(r3, r4, r4, JS_FUNCTION_TYPE);
  __ bne(&runtime);

  // InvokeFunction requires the function in r4. Move it in there.
  __ mr(r4, result_register());
  ParameterCount count(arg_count);
  __ InvokeFunction(r4, count, CALL_FUNCTION,
                    NullCallWrapper(), CALL_AS_METHOD);
  __ lwz(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
  __ jmp(&done);

  __ bind(&runtime);
  __ push(r3);
  __ CallRuntime(Runtime::kCall, args->length());
  __ bind(&done);

  context()->Plug(r3);
}


void FullCodeGenerator::EmitRegExpConstructResult(CallRuntime* expr) {
  EMIT_STUB_MARKER(277);
  RegExpConstructResultStub stub;
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 3);
  VisitForStackValue(args->at(0));
  VisitForStackValue(args->at(1));
  VisitForStackValue(args->at(2));
  __ CallStub(&stub);
  context()->Plug(r3);
}


void FullCodeGenerator::EmitGetFromCache(CallRuntime* expr) {
  EMIT_STUB_MARKER(278);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT_EQ(2, args->length());
  ASSERT_NE(NULL, args->at(0)->AsLiteral());
  int cache_id = Smi::cast(*(args->at(0)->AsLiteral()->handle()))->value();

  Handle<FixedArray> jsfunction_result_caches(
      isolate()->native_context()->jsfunction_result_caches());
  if (jsfunction_result_caches->length() <= cache_id) {
    __ Abort("Attempt to use undefined cache.");
    __ LoadRoot(r3, Heap::kUndefinedValueRootIndex);
    context()->Plug(r3);
    return;
  }

  VisitForAccumulatorValue(args->at(1));

  Register key = r3;
  Register cache = r4;
  __ lwz(cache, ContextOperand(cp, Context::GLOBAL_OBJECT_INDEX));
  __ lwz(cache, FieldMemOperand(cache, GlobalObject::kNativeContextOffset));
  __ lwz(cache, ContextOperand(cache, Context::JSFUNCTION_RESULT_CACHES_INDEX));
  __ LoadWord(cache,
         FieldMemOperand(cache, FixedArray::OffsetOfElementAt(cache_id)), r0);

  Label done, not_found;
  // tmp now holds finger offset as a smi.
  STATIC_ASSERT(kSmiTag == 0 && kSmiTagSize == 1);
  __ lwz(r5, FieldMemOperand(cache, JSFunctionResultCache::kFingerOffset));
  // r5 now holds finger offset as a smi.
  __ addi(r6, cache, Operand(FixedArray::kHeaderSize - kHeapObjectTag));
  // r6 now points to the start of fixed array elements.
  __ slwi(r5, r5, Operand(kPointerSizeLog2 - kSmiTagSize));
  __ add(r6, r6, r5);
  __ lwz(r5, MemOperand(r6));
  // r6 now points to the key of the pair.
  __ cmp(key, r5);
  __ bne(&not_found);

  __ lwz(r3, MemOperand(r6, kPointerSize));
  __ b(&done);

  __ bind(&not_found);
  // Call runtime to perform the lookup.
  __ Push(cache, key);
  __ CallRuntime(Runtime::kGetFromCache, 2);

  __ bind(&done);
  context()->Plug(r3);
}


void FullCodeGenerator::EmitIsRegExpEquivalent(CallRuntime* expr) {
  EMIT_STUB_MARKER(279);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT_EQ(2, args->length());

  Register right = r3;
  Register left = r4;
  Register tmp = r5;
  Register tmp2 = r6;

  VisitForStackValue(args->at(0));
  VisitForAccumulatorValue(args->at(1));
  __ pop(left);

  Label done, fail, ok;
  __ cmp(left, right);
  __ beq(&ok);
  // Fail if either is a non-HeapObject.
  __ and_(tmp, left, right);
  __ JumpIfSmi(tmp, &fail);
  __ lwz(tmp, FieldMemOperand(left, HeapObject::kMapOffset));
  __ lbz(tmp2, FieldMemOperand(tmp, Map::kInstanceTypeOffset));
  __ cmpi(tmp2, Operand(JS_REGEXP_TYPE));
  __ bne(&fail);
  __ lwz(tmp2, FieldMemOperand(right, HeapObject::kMapOffset));
  __ cmp(tmp, tmp2);
  __ bne(&fail);
  __ lwz(tmp, FieldMemOperand(left, JSRegExp::kDataOffset));
  __ lwz(tmp2, FieldMemOperand(right, JSRegExp::kDataOffset));
  __ cmp(tmp, tmp2);
  __ beq(&ok);
  __ bind(&fail);
  __ LoadRoot(r3, Heap::kFalseValueRootIndex);
  __ jmp(&done);
  __ bind(&ok);
  __ LoadRoot(r3, Heap::kTrueValueRootIndex);
  __ bind(&done);

  context()->Plug(r3);
}


void FullCodeGenerator::EmitHasCachedArrayIndex(CallRuntime* expr) {
  EMIT_STUB_MARKER(280);
  ZoneList<Expression*>* args = expr->arguments();
  VisitForAccumulatorValue(args->at(0));

  Label materialize_true, materialize_false;
  Label* if_true = NULL;
  Label* if_false = NULL;
  Label* fall_through = NULL;
  context()->PrepareTest(&materialize_true, &materialize_false,
                         &if_true, &if_false, &fall_through);

  __ lwz(r3, FieldMemOperand(r3, String::kHashFieldOffset));
  // PPC - assume ip is free
  __ mov(ip, Operand(String::kContainsCachedArrayIndexMask));
  __ and_(r0, r3, ip);
  __ cmpi(r0, Operand(0));
  PrepareForBailoutBeforeSplit(expr, true, if_true, if_false);
  Split(eq, if_true, if_false, fall_through);

  context()->Plug(if_true, if_false);
}


void FullCodeGenerator::EmitGetCachedArrayIndex(CallRuntime* expr) {
  EMIT_STUB_MARKER(281);
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 1);
  VisitForAccumulatorValue(args->at(0));

  __ AbortIfNotString(r3);


  __ lwz(r3, FieldMemOperand(r3, String::kHashFieldOffset));
  __ IndexFromHash(r3, r3);

  context()->Plug(r3);
}


void FullCodeGenerator::EmitFastAsciiArrayJoin(CallRuntime* expr) {
  EMIT_STUB_MARKER(282);
  Label bailout, done, one_char_separator, long_separator,
      non_trivial_array, not_size_one_array, loop,
      empty_separator_loop, one_char_separator_loop,
      one_char_separator_loop_entry, long_separator_loop;
  ZoneList<Expression*>* args = expr->arguments();
  ASSERT(args->length() == 2);
  VisitForStackValue(args->at(1));
  VisitForAccumulatorValue(args->at(0));

  // All aliases of the same register have disjoint lifetimes.
  Register array = r3;
  Register elements = no_reg;  // Will be r3.
  Register result = no_reg;  // Will be r3.
  Register separator = r4;
  Register array_length = r5;
  Register result_pos = no_reg;  // Will be r5
  Register string_length = r6;
  Register string = r7;
  Register element = r8;
  Register elements_end = r9;
  Register scratch1 = r10;
  Register scratch2 = r22;

  // Separator operand is on the stack.
  __ pop(separator);

  // Check that the array is a JSArray.
  __ JumpIfSmi(array, &bailout);
  __ CompareObjectType(array, scratch1, scratch2, JS_ARRAY_TYPE);
  __ bne(&bailout);

  // Check that the array has fast elements.
  __ CheckFastElements(scratch1, scratch2, &bailout);

  // If the array has length zero, return the empty string.
  __ lwz(array_length, FieldMemOperand(array, JSArray::kLengthOffset));
  __ SmiUntag(array_length);
  __ cmpi(array_length, Operand(0));
  __ bne(&non_trivial_array);
  __ LoadRoot(r3, Heap::kEmptyStringRootIndex);
  __ b(&done);

  __ bind(&non_trivial_array);

  // Get the FixedArray containing array's elements.
  elements = array;
  __ lwz(elements, FieldMemOperand(array, JSArray::kElementsOffset));
  array = no_reg;  // End of array's live range.

  // Check that all array elements are sequential ASCII strings, and
  // accumulate the sum of their lengths, as a smi-encoded value.
  __ li(string_length, Operand(0));
  __ addi(element,
          elements, Operand(FixedArray::kHeaderSize - kHeapObjectTag));
  __ slwi(elements_end, array_length, Operand(kPointerSizeLog2));
  __ add(elements_end, element, elements_end);
  // Loop condition: while (element < elements_end).
  // Live values in registers:
  //   elements: Fixed array of strings.
  //   array_length: Length of the fixed array of strings (not smi)
  //   separator: Separator string
  //   string_length: Accumulated sum of string lengths (smi).
  //   element: Current array element.
  //   elements_end: Array end.
  if (generate_debug_code_) {
    __ cmpi(array_length, Operand(0));
    __ Assert(gt, "No empty arrays here in EmitFastAsciiArrayJoin");
  }
  __ bind(&loop);
  __ lwz(string, MemOperand(element));
  __ addi(element, element, Operand(kPointerSize));
  __ JumpIfSmi(string, &bailout);
  __ lwz(scratch1, FieldMemOperand(string, HeapObject::kMapOffset));
  __ lbz(scratch1, FieldMemOperand(scratch1, Map::kInstanceTypeOffset));
  __ JumpIfInstanceTypeIsNotSequentialAscii(scratch1, scratch2, &bailout);
  __ lwz(scratch1, FieldMemOperand(string, SeqAsciiString::kLengthOffset));

  __ AddAndCheckForOverflow(string_length, string_length, scratch1,
                            scratch2, r0);
  __ BranchOnOverflow(&bailout);

  __ cmp(element, elements_end);
  __ blt(&loop);

  // If array_length is 1, return elements[0], a string.
  __ cmpi(array_length, Operand(1));
  __ bne(&not_size_one_array);
  __ lwz(r3, FieldMemOperand(elements, FixedArray::kHeaderSize));
  __ b(&done);

  __ bind(&not_size_one_array);

  // Live values in registers:
  //   separator: Separator string
  //   array_length: Length of the array.
  //   string_length: Sum of string lengths (smi).
  //   elements: FixedArray of strings.

  // Check that the separator is a flat ASCII string.
  __ JumpIfSmi(separator, &bailout);
  __ lwz(scratch1, FieldMemOperand(separator, HeapObject::kMapOffset));
  __ lbz(scratch1, FieldMemOperand(scratch1, Map::kInstanceTypeOffset));
  __ JumpIfInstanceTypeIsNotSequentialAscii(scratch1, scratch2, &bailout);

  // Add (separator length times array_length) - separator length to the
  // string_length to get the length of the result string. array_length is not
  // smi but the other values are, so the result is a smi
  __ lwz(scratch1, FieldMemOperand(separator, SeqAsciiString::kLengthOffset));
  __ sub(string_length, string_length, scratch1);
  __ mullw(scratch2, array_length, scratch1);
  __ mulhw(ip, array_length, scratch1);
  // Check for smi overflow. No overflow if higher 33 bits of 64-bit result are
  // zero.
  __ cmpi(ip, Operand(0));
  __ bne(&bailout);
  __ TestBit(scratch2, 0, r0);  // test sign bit
  __ bne(&bailout, cr0);

  __ AddAndCheckForOverflow(string_length, string_length, scratch2,
                            scratch1, r0);
  __ BranchOnOverflow(&bailout);
  __ SmiUntag(string_length);

  // Get first element in the array to free up the elements register to be used
  // for the result.
  __ addi(element,
          elements, Operand(FixedArray::kHeaderSize - kHeapObjectTag));
  result = elements;  // End of live range for elements.
  elements = no_reg;
  // Live values in registers:
  //   element: First array element
  //   separator: Separator string
  //   string_length: Length of result string (not smi)
  //   array_length: Length of the array.
  __ AllocateAsciiString(result,
                         string_length,
                         scratch1,
                         scratch2,
                         elements_end,
                         &bailout);
  // Prepare for looping. Set up elements_end to end of the array. Set
  // result_pos to the position of the result where to write the first
  // character.
  __ slwi(elements_end, array_length, Operand(kPointerSizeLog2));
  __ add(elements_end, element, elements_end);
  result_pos = array_length;  // End of live range for array_length.
  array_length = no_reg;
  __ addi(result_pos,
          result,
          Operand(SeqAsciiString::kHeaderSize - kHeapObjectTag));

  // Check the length of the separator.
  __ lwz(scratch1, FieldMemOperand(separator, SeqAsciiString::kLengthOffset));
  __ cmpi(scratch1, Operand(Smi::FromInt(1)));
  __ beq(&one_char_separator);
  __ bgt(&long_separator);

  // Empty separator case
  __ bind(&empty_separator_loop);
  // Live values in registers:
  //   result_pos: the position to which we are currently copying characters.
  //   element: Current array element.
  //   elements_end: Array end.

  // Copy next array element to the result.
  __ lwz(string, MemOperand(element));
  __ addi(element, element, Operand(kPointerSize));
  __ lwz(string_length, FieldMemOperand(string, String::kLengthOffset));
  __ SmiUntag(string_length);
  __ addi(string, string,
          Operand(SeqAsciiString::kHeaderSize - kHeapObjectTag));
  __ CopyBytes(string, result_pos, string_length, scratch1);
  __ cmp(element, elements_end);
  __ blt(&empty_separator_loop);  // End while (element < elements_end).
  ASSERT(result.is(r3));
  __ b(&done);

  // One-character separator case
  __ bind(&one_char_separator);
  // Replace separator with its ASCII character value.
  __ lbz(separator, FieldMemOperand(separator, SeqAsciiString::kHeaderSize));
  // Jump into the loop after the code that copies the separator, so the first
  // element is not preceded by a separator
  __ jmp(&one_char_separator_loop_entry);

  __ bind(&one_char_separator_loop);
  // Live values in registers:
  //   result_pos: the position to which we are currently copying characters.
  //   element: Current array element.
  //   elements_end: Array end.
  //   separator: Single separator ASCII char (in lower byte).

  // Copy the separator character to the result.
  __ stb(separator, MemOperand(result_pos));
  __ addi(result_pos, result_pos, Operand(1));

  // Copy next array element to the result.
  __ bind(&one_char_separator_loop_entry);
  __ lwz(string, MemOperand(element));
  __ addi(element, element, Operand(kPointerSize));
  __ lwz(string_length, FieldMemOperand(string, String::kLengthOffset));
  __ SmiUntag(string_length);
  __ addi(string, string,
          Operand(SeqAsciiString::kHeaderSize - kHeapObjectTag));
  __ CopyBytes(string, result_pos, string_length, scratch1);
  __ cmpl(element, elements_end);
  __ blt(&one_char_separator_loop);  // End while (element < elements_end).
  ASSERT(result.is(r3));
  __ b(&done);

  // Long separator case (separator is more than one character). Entry is at the
  // label long_separator below.
  __ bind(&long_separator_loop);
  // Live values in registers:
  //   result_pos: the position to which we are currently copying characters.
  //   element: Current array element.
  //   elements_end: Array end.
  //   separator: Separator string.

  // Copy the separator to the result.
  __ lwz(string_length, FieldMemOperand(separator, String::kLengthOffset));
  __ SmiUntag(string_length);
  __ addi(string,
          separator,
          Operand(SeqAsciiString::kHeaderSize - kHeapObjectTag));
  __ CopyBytes(string, result_pos, string_length, scratch1);

  __ bind(&long_separator);
  __ lwz(string, MemOperand(element));
  __ addi(element, element, Operand(kPointerSize));
  __ lwz(string_length, FieldMemOperand(string, String::kLengthOffset));
  __ SmiUntag(string_length);
  __ addi(string, string,
          Operand(SeqAsciiString::kHeaderSize - kHeapObjectTag));
  __ CopyBytes(string, result_pos, string_length, scratch1);
  __ cmpl(element, elements_end);
  __ blt(&long_separator_loop);  // End while (element < elements_end).
  ASSERT(result.is(r3));
  __ b(&done);

  __ bind(&bailout);
  __ LoadRoot(r3, Heap::kUndefinedValueRootIndex);
  __ bind(&done);
  context()->Plug(r3);
}


void FullCodeGenerator::VisitCallRuntime(CallRuntime* expr) {
  EMIT_STUB_MARKER(283);
  Handle<String> name = expr->name();
  if (name->length() > 0 && name->Get(0) == '_') {
    Comment cmnt(masm_, "[ InlineRuntimeCall");
    EmitInlineRuntimeCall(expr);
    return;
  }

  Comment cmnt(masm_, "[ CallRuntime");
  ZoneList<Expression*>* args = expr->arguments();

  if (expr->is_jsruntime()) {
    // Prepare for calling JS runtime function.
    __ lwz(r3, GlobalObjectOperand());
    __ lwz(r3, FieldMemOperand(r3, GlobalObject::kBuiltinsOffset));
    __ push(r3);
  }

  // Push the arguments ("left-to-right").
  int arg_count = args->length();
  for (int i = 0; i < arg_count; i++) {
    VisitForStackValue(args->at(i));
  }

  if (expr->is_jsruntime()) {
    // Call the JS runtime function.
    __ mov(r5, Operand(expr->name()));
    RelocInfo::Mode mode = RelocInfo::CODE_TARGET;
    Handle<Code> ic =
        isolate()->stub_cache()->ComputeCallInitialize(arg_count, mode);
    CallIC(ic, mode, expr->CallRuntimeFeedbackId());
    // Restore context register.
    __ lwz(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
  } else {
    // Call the C runtime function.
    __ CallRuntime(expr->function(), arg_count);
  }
  context()->Plug(r3);
}


void FullCodeGenerator::VisitUnaryOperation(UnaryOperation* expr) {
  EMIT_STUB_MARKER(284);
  switch (expr->op()) {
    case Token::DELETE: {
      Comment cmnt(masm_, "[ UnaryOperation (DELETE)");
      Property* property = expr->expression()->AsProperty();
      VariableProxy* proxy = expr->expression()->AsVariableProxy();

      if (property != NULL) {
        VisitForStackValue(property->obj());
        VisitForStackValue(property->key());
        StrictModeFlag strict_mode_flag = (language_mode() == CLASSIC_MODE)
            ? kNonStrictMode : kStrictMode;
        __ li(r4, Operand(Smi::FromInt(strict_mode_flag)));
        __ push(r4);
        __ InvokeBuiltin(Builtins::DELETE, CALL_FUNCTION);
        context()->Plug(r3);
      } else if (proxy != NULL) {
        Variable* var = proxy->var();
        // Delete of an unqualified identifier is disallowed in strict mode
        // but "delete this" is allowed.
        ASSERT(language_mode() == CLASSIC_MODE || var->is_this());
        if (var->IsUnallocated()) {
          __ lwz(r5, GlobalObjectOperand());
          __ mov(r4, Operand(var->name()));
          __ li(r3, Operand(Smi::FromInt(kNonStrictMode)));
          __ Push(r5, r4, r3);
          __ InvokeBuiltin(Builtins::DELETE, CALL_FUNCTION);
          context()->Plug(r3);
        } else if (var->IsStackAllocated() || var->IsContextSlot()) {
          // Result of deleting non-global, non-dynamic variables is false.
          // The subexpression does not have side effects.
          context()->Plug(var->is_this());
        } else {
          // Non-global variable.  Call the runtime to try to delete from the
          // context where the variable was introduced.
          __ push(context_register());
          __ mov(r5, Operand(var->name()));
          __ push(r5);
          __ CallRuntime(Runtime::kDeleteContextSlot, 2);
          context()->Plug(r3);
        }
      } else {
        // Result of deleting non-property, non-variable reference is true.
        // The subexpression may have side effects.
        VisitForEffect(expr->expression());
        context()->Plug(true);
      }
      break;
    }

    case Token::VOID: {
      Comment cmnt(masm_, "[ UnaryOperation (VOID)");
      VisitForEffect(expr->expression());
      context()->Plug(Heap::kUndefinedValueRootIndex);
      break;
    }

    case Token::NOT: {
      Comment cmnt(masm_, "[ UnaryOperation (NOT)");
      if (context()->IsEffect()) {
        // Unary NOT has no side effects so it's only necessary to visit the
        // subexpression.  Match the optimizing compiler by not branching.
        VisitForEffect(expr->expression());
      } else if (context()->IsTest()) {
        const TestContext* test = TestContext::cast(context());
        // The labels are swapped for the recursive call.
        VisitForControl(expr->expression(),
                        test->false_label(),
                        test->true_label(),
                        test->fall_through());
        context()->Plug(test->true_label(), test->false_label());
      } else {
        // We handle value contexts explicitly rather than simply visiting
        // for control and plugging the control flow into the context,
        // because we need to prepare a pair of extra administrative AST ids
        // for the optimizing compiler.
        ASSERT(context()->IsAccumulatorValue() || context()->IsStackValue());
        Label materialize_true, materialize_false, done;
        VisitForControl(expr->expression(),
                        &materialize_false,
                        &materialize_true,
                        &materialize_true);
        __ bind(&materialize_true);
        PrepareForBailoutForId(expr->MaterializeTrueId(), NO_REGISTERS);
        __ LoadRoot(r3, Heap::kTrueValueRootIndex);
        if (context()->IsStackValue()) __ push(r3);
        __ jmp(&done);
        __ bind(&materialize_false);
        PrepareForBailoutForId(expr->MaterializeFalseId(), NO_REGISTERS);
        __ LoadRoot(r3, Heap::kFalseValueRootIndex);
        if (context()->IsStackValue()) __ push(r3);
        __ bind(&done);
      }
      break;
    }

    case Token::TYPEOF: {
      Comment cmnt(masm_, "[ UnaryOperation (TYPEOF)");
      { StackValueContext context(this);
        VisitForTypeofValue(expr->expression());
      }
      __ CallRuntime(Runtime::kTypeof, 1);
      context()->Plug(r3);
      break;
    }

    case Token::ADD: {
      Comment cmt(masm_, "[ UnaryOperation (ADD)");
      VisitForAccumulatorValue(expr->expression());
      Label no_conversion;
      __ JumpIfSmi(result_register(), &no_conversion);
      ToNumberStub convert_stub;
      __ CallStub(&convert_stub);
      __ bind(&no_conversion);
      context()->Plug(result_register());
      break;
    }

    case Token::SUB:
      EmitUnaryOperation(expr, "[ UnaryOperation (SUB)");
      break;

    case Token::BIT_NOT:
      EmitUnaryOperation(expr, "[ UnaryOperation (BIT_NOT)");
      break;

    default:
      UNREACHABLE();
  }
}


void FullCodeGenerator::EmitUnaryOperation(UnaryOperation* expr,
                                           const char* comment) {
  EMIT_STUB_MARKER(285);
  // TODO(svenpanne): Allowing format strings in Comment would be nice here...
  Comment cmt(masm_, comment);
  bool can_overwrite = expr->expression()->ResultOverwriteAllowed();
  UnaryOverwriteMode overwrite =
      can_overwrite ? UNARY_OVERWRITE : UNARY_NO_OVERWRITE;
  UnaryOpStub stub(expr->op(), overwrite);
  // UnaryOpStub expects the argument to be in the
  // accumulator register r3.
  VisitForAccumulatorValue(expr->expression());
  SetSourcePosition(expr->position());
  CallIC(stub.GetCode(), RelocInfo::CODE_TARGET,
         expr->UnaryOperationFeedbackId());
  context()->Plug(r3);
}


void FullCodeGenerator::VisitCountOperation(CountOperation* expr) {
  EMIT_STUB_MARKER(286);
  Comment cmnt(masm_, "[ CountOperation");
  SetSourcePosition(expr->position());

  // Invalid left-hand sides are rewritten to have a 'throw ReferenceError'
  // as the left-hand side.
  if (!expr->expression()->IsValidLeftHandSide()) {
    VisitForEffect(expr->expression());
    return;
  }

  // Expression can only be a property, a global or a (parameter or local)
  // slot.
  enum LhsKind { VARIABLE, NAMED_PROPERTY, KEYED_PROPERTY };
  LhsKind assign_type = VARIABLE;
  Property* prop = expr->expression()->AsProperty();
  // In case of a property we use the uninitialized expression context
  // of the key to detect a named property.
  if (prop != NULL) {
    assign_type =
        (prop->key()->IsPropertyName()) ? NAMED_PROPERTY : KEYED_PROPERTY;
  }

  // Evaluate expression and get value.
  if (assign_type == VARIABLE) {
    ASSERT(expr->expression()->AsVariableProxy()->var() != NULL);
    AccumulatorValueContext context(this);
    EmitVariableLoad(expr->expression()->AsVariableProxy());
  } else {
    // Reserve space for result of postfix operation.
    if (expr->is_postfix() && !context()->IsEffect()) {
      __ li(ip, Operand(Smi::FromInt(0)));
      __ push(ip);
    }
    if (assign_type == NAMED_PROPERTY) {
      // Put the object both on the stack and in the accumulator.
      VisitForAccumulatorValue(prop->obj());
      __ push(r3);
      EmitNamedPropertyLoad(prop);
    } else {
      VisitForStackValue(prop->obj());
      VisitForAccumulatorValue(prop->key());
      __ lwz(r4, MemOperand(sp, 0));
      __ push(r3);
      EmitKeyedPropertyLoad(prop);
    }
  }

  // We need a second deoptimization point after loading the value
  // in case evaluating the property load my have a side effect.
  if (assign_type == VARIABLE) {
    PrepareForBailout(expr->expression(), TOS_REG);
  } else {
    PrepareForBailoutForId(prop->LoadId(), TOS_REG);
  }

  // Call ToNumber only if operand is not a smi.
  Label no_conversion;
  __ JumpIfSmi(r3, &no_conversion);
  ToNumberStub convert_stub;
  __ CallStub(&convert_stub);
  __ bind(&no_conversion);

  // Save result for postfix expressions.
  if (expr->is_postfix()) {
    if (!context()->IsEffect()) {
      // Save the result on the stack. If we have a named or keyed property
      // we store the result under the receiver that is currently on top
      // of the stack.
      switch (assign_type) {
        case VARIABLE:
          __ push(r3);
          break;
        case NAMED_PROPERTY:
          __ stw(r3, MemOperand(sp, kPointerSize));
          break;
        case KEYED_PROPERTY:
          __ stw(r3, MemOperand(sp, 2 * kPointerSize));
          break;
      }
    }
  }

  // Inline smi case if we are in a loop.
  Label stub_call, done;
  JumpPatchSite patch_site(masm_);

  int count_value = expr->op() == Token::INC ? 1 : -1;
  __ li(r4, Operand(Smi::FromInt(count_value)));

  if (ShouldInlineSmiCase(expr->op())) {
    __ AddAndCheckForOverflow(r3, r3, r4, r5, r0);
    __ BranchOnOverflow(&stub_call);

    // We could eliminate this smi check if we split the code at
    // the first smi check before calling ToNumber.
    patch_site.EmitJumpIfSmi(r3, &done);

    __ bind(&stub_call);
    // Call stub. Undo operation first.
    __ sub(r3, r3, Operand(Smi::FromInt(count_value)));
  }

  // Record position before stub call.
  SetSourcePosition(expr->position());

  BinaryOpStub stub(Token::ADD, NO_OVERWRITE);
  CallIC(stub.GetCode(), RelocInfo::CODE_TARGET, expr->CountBinOpFeedbackId());
  patch_site.EmitPatchInfo();
  __ bind(&done);

  // Store the value returned in r3.
  switch (assign_type) {
    case VARIABLE:
      if (expr->is_postfix()) {
        { EffectContext context(this);
          EmitVariableAssignment(expr->expression()->AsVariableProxy()->var(),
                                 Token::ASSIGN);
          PrepareForBailoutForId(expr->AssignmentId(), TOS_REG);
          context.Plug(r3);
        }
        // For all contexts except EffectConstant We have the result on
        // top of the stack.
        if (!context()->IsEffect()) {
          context()->PlugTOS();
        }
      } else {
        EmitVariableAssignment(expr->expression()->AsVariableProxy()->var(),
                               Token::ASSIGN);
        PrepareForBailoutForId(expr->AssignmentId(), TOS_REG);
        context()->Plug(r3);
      }
      break;
    case NAMED_PROPERTY: {
      __ mov(r5, Operand(prop->key()->AsLiteral()->handle()));
      __ pop(r4);
      Handle<Code> ic = is_classic_mode()
          ? isolate()->builtins()->StoreIC_Initialize()
          : isolate()->builtins()->StoreIC_Initialize_Strict();
      CallIC(ic, RelocInfo::CODE_TARGET, expr->CountStoreFeedbackId());
      PrepareForBailoutForId(expr->AssignmentId(), TOS_REG);
      if (expr->is_postfix()) {
        if (!context()->IsEffect()) {
          context()->PlugTOS();
        }
      } else {
        context()->Plug(r3);
      }
      break;
    }
    case KEYED_PROPERTY: {
      __ pop(r4);  // Key.
      __ pop(r5);  // Receiver.
      Handle<Code> ic = is_classic_mode()
          ? isolate()->builtins()->KeyedStoreIC_Initialize()
          : isolate()->builtins()->KeyedStoreIC_Initialize_Strict();
      CallIC(ic, RelocInfo::CODE_TARGET, expr->CountStoreFeedbackId());
      PrepareForBailoutForId(expr->AssignmentId(), TOS_REG);
      if (expr->is_postfix()) {
        if (!context()->IsEffect()) {
          context()->PlugTOS();
        }
      } else {
        context()->Plug(r3);
      }
      break;
    }
  }
}


void FullCodeGenerator::VisitForTypeofValue(Expression* expr) {
  EMIT_STUB_MARKER(287);
  ASSERT(!context()->IsEffect());
  ASSERT(!context()->IsTest());
  VariableProxy* proxy = expr->AsVariableProxy();
  if (proxy != NULL && proxy->var()->IsUnallocated()) {
    Comment cmnt(masm_, "Global variable");
    __ lwz(r3, GlobalObjectOperand());
    __ mov(r5, Operand(proxy->name()));
    Handle<Code> ic = isolate()->builtins()->LoadIC_Initialize();
    // Use a regular load, not a contextual load, to avoid a reference
    // error.
    CallIC(ic);
    PrepareForBailout(expr, TOS_REG);
    context()->Plug(r3);
  } else if (proxy != NULL && proxy->var()->IsLookupSlot()) {
    Label done, slow;

    // Generate code for loading from variables potentially shadowed
    // by eval-introduced variables.
    EmitDynamicLookupFastCase(proxy->var(), INSIDE_TYPEOF, &slow, &done);

    __ bind(&slow);
    __ mov(r3, Operand(proxy->name()));
    __ Push(cp, r3);
    __ CallRuntime(Runtime::kLoadContextSlotNoReferenceError, 2);
    PrepareForBailout(expr, TOS_REG);
    __ bind(&done);

    context()->Plug(r3);
  } else {
    // This expression cannot throw a reference error at the top level.
    VisitInDuplicateContext(expr);
  }
}


void FullCodeGenerator::EmitLiteralCompareTypeof(Expression* expr,
                                                 Expression* sub_expr,
                                                 Handle<String> check) {
  EMIT_STUB_MARKER(288);
  Label materialize_true, materialize_false;
  Label* if_true = NULL;
  Label* if_false = NULL;
  Label* fall_through = NULL;
  context()->PrepareTest(&materialize_true, &materialize_false,
                         &if_true, &if_false, &fall_through);

  { AccumulatorValueContext context(this);
    VisitForTypeofValue(sub_expr);
  }
  PrepareForBailoutBeforeSplit(expr, true, if_true, if_false);

  if (check->Equals(isolate()->heap()->number_symbol())) {
    __ JumpIfSmi(r3, if_true);
    __ lwz(r3, FieldMemOperand(r3, HeapObject::kMapOffset));
    __ LoadRoot(ip, Heap::kHeapNumberMapRootIndex);
    __ cmp(r3, ip);
    Split(eq, if_true, if_false, fall_through);
  } else if (check->Equals(isolate()->heap()->string_symbol())) {
    __ JumpIfSmi(r3, if_false);
    // Check for undetectable objects => false.
    __ CompareObjectType(r3, r3, r4, FIRST_NONSTRING_TYPE);
    __ bge(if_false);
    __ lbz(r4, FieldMemOperand(r3, Map::kBitFieldOffset));
    STATIC_ASSERT((1 << Map::kIsUndetectable) < 0x8000);
    __ andi(r0, r4, Operand(1 << Map::kIsUndetectable));
    __ cmpi(r0, Operand(0));
    Split(eq, if_true, if_false, fall_through);
  } else if (check->Equals(isolate()->heap()->boolean_symbol())) {
    __ CompareRoot(r3, Heap::kTrueValueRootIndex);
    __ beq(if_true);
    __ CompareRoot(r3, Heap::kFalseValueRootIndex);
    Split(eq, if_true, if_false, fall_through);
  } else if (FLAG_harmony_typeof &&
             check->Equals(isolate()->heap()->null_symbol())) {
    __ CompareRoot(r3, Heap::kNullValueRootIndex);
    Split(eq, if_true, if_false, fall_through);
  } else if (check->Equals(isolate()->heap()->undefined_symbol())) {
    __ CompareRoot(r3, Heap::kUndefinedValueRootIndex);
    __ beq(if_true);
    __ JumpIfSmi(r3, if_false);
    // Check for undetectable objects => true.
    __ lwz(r3, FieldMemOperand(r3, HeapObject::kMapOffset));
    __ lbz(r4, FieldMemOperand(r3, Map::kBitFieldOffset));
    __ andi(r0, r4, Operand(1 << Map::kIsUndetectable));
    __ cmpi(r0, Operand(0));
    Split(ne, if_true, if_false, fall_through);

  } else if (check->Equals(isolate()->heap()->function_symbol())) {
    __ JumpIfSmi(r3, if_false);
    STATIC_ASSERT(NUM_OF_CALLABLE_SPEC_OBJECT_TYPES == 2);
    __ CompareObjectType(r3, r3, r4, JS_FUNCTION_TYPE);
    __ beq(if_true);
    __ cmpi(r4, Operand(JS_FUNCTION_PROXY_TYPE));
    Split(eq, if_true, if_false, fall_through);
  } else if (check->Equals(isolate()->heap()->object_symbol())) {
    __ JumpIfSmi(r3, if_false);
    if (!FLAG_harmony_typeof) {
      __ CompareRoot(r3, Heap::kNullValueRootIndex);
      __ beq(if_true);
    }
    // Check for JS objects => true.
    __ CompareObjectType(r3, r3, r4, FIRST_NONCALLABLE_SPEC_OBJECT_TYPE);
    __ blt(if_false);
    __ CompareInstanceType(r3, r4, LAST_NONCALLABLE_SPEC_OBJECT_TYPE);
    __ bgt(if_false);
    // Check for undetectable objects => false.
    __ lbz(r4, FieldMemOperand(r3, Map::kBitFieldOffset));
    __ andi(r0, r4, Operand(1 << Map::kIsUndetectable));
    __ cmpi(r0, Operand(0));
    Split(eq, if_true, if_false, fall_through);
  } else {
    if (if_false != fall_through) __ jmp(if_false);
  }
  context()->Plug(if_true, if_false);
}


void FullCodeGenerator::VisitCompareOperation(CompareOperation* expr) {
  EMIT_STUB_MARKER(289);
  Comment cmnt(masm_, "[ CompareOperation");
  SetSourcePosition(expr->position());

  // First we try a fast inlined version of the compare when one of
  // the operands is a literal.
  if (TryLiteralCompare(expr)) return;

  // Always perform the comparison for its control flow.  Pack the result
  // into the expression's context after the comparison is performed.
  Label materialize_true, materialize_false;
  Label* if_true = NULL;
  Label* if_false = NULL;
  Label* fall_through = NULL;
  context()->PrepareTest(&materialize_true, &materialize_false,
                         &if_true, &if_false, &fall_through);

  Token::Value op = expr->op();
  VisitForStackValue(expr->left());
  switch (op) {
    case Token::IN:
      VisitForStackValue(expr->right());
      __ InvokeBuiltin(Builtins::IN, CALL_FUNCTION);
      PrepareForBailoutBeforeSplit(expr, false, NULL, NULL);
      __ LoadRoot(ip, Heap::kTrueValueRootIndex);
      __ cmp(r3, ip);
      Split(eq, if_true, if_false, fall_through);
      break;

    case Token::INSTANCEOF: {
      VisitForStackValue(expr->right());
      InstanceofStub stub(InstanceofStub::kNoFlags);
      __ CallStub(&stub);
      PrepareForBailoutBeforeSplit(expr, true, if_true, if_false);
      // The stub returns 0 for true.
      __ cmpi(r3, Operand(0));
      Split(eq, if_true, if_false, fall_through);
      break;
    }

    default: {
      VisitForAccumulatorValue(expr->right());
      Condition cond = eq;
      switch (op) {
        case Token::EQ_STRICT:
        case Token::EQ:
          cond = eq;
          break;
        case Token::LT:
          cond = lt;
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
      __ pop(r4);

      bool inline_smi_code = ShouldInlineSmiCase(op);
      JumpPatchSite patch_site(masm_);
      if (inline_smi_code) {
        Label slow_case;
        __ orx(r5, r3, r4);
        patch_site.EmitJumpIfNotSmi(r5, &slow_case);
        __ cmp(r4, r3);
        Split(cond, if_true, if_false, NULL);
        __ bind(&slow_case);
      }

      // Record position and call the compare IC.
      SetSourcePosition(expr->position());
      Handle<Code> ic = CompareIC::GetUninitialized(op);
      CallIC(ic, RelocInfo::CODE_TARGET, expr->CompareOperationFeedbackId());
      patch_site.EmitPatchInfo();
      PrepareForBailoutBeforeSplit(expr, true, if_true, if_false);
      __ cmpi(r3, Operand(0));
      Split(cond, if_true, if_false, fall_through);
    }
  }

  // Convert the result of the comparison into one expected for this
  // expression's context.
  context()->Plug(if_true, if_false);
}


void FullCodeGenerator::EmitLiteralCompareNil(CompareOperation* expr,
                                              Expression* sub_expr,
                                              NilValue nil) {
  EMIT_STUB_MARKER(290);
  Label materialize_true, materialize_false;
  Label* if_true = NULL;
  Label* if_false = NULL;
  Label* fall_through = NULL;
  context()->PrepareTest(&materialize_true, &materialize_false,
                         &if_true, &if_false, &fall_through);

  VisitForAccumulatorValue(sub_expr);
  PrepareForBailoutBeforeSplit(expr, true, if_true, if_false);
  Heap::RootListIndex nil_value = nil == kNullValue ?
      Heap::kNullValueRootIndex :
      Heap::kUndefinedValueRootIndex;
  __ LoadRoot(r4, nil_value);
  __ cmp(r3, r4);
  if (expr->op() == Token::EQ_STRICT) {
    Split(eq, if_true, if_false, fall_through);
  } else {
    Heap::RootListIndex other_nil_value = nil == kNullValue ?
        Heap::kUndefinedValueRootIndex :
        Heap::kNullValueRootIndex;
    __ beq(if_true);
    __ LoadRoot(r4, other_nil_value);
    __ cmp(r3, r4);
    __ beq(if_true);
    __ JumpIfSmi(r3, if_false);
    // It can be an undetectable object.
    __ lwz(r4, FieldMemOperand(r3, HeapObject::kMapOffset));
    __ lbz(r4, FieldMemOperand(r4, Map::kBitFieldOffset));
    __ andi(r4, r4, Operand(1 << Map::kIsUndetectable));
    __ cmpi(r4, Operand(1 << Map::kIsUndetectable));
    Split(eq, if_true, if_false, fall_through);
  }
  context()->Plug(if_true, if_false);
}


void FullCodeGenerator::VisitThisFunction(ThisFunction* expr) {
  __ lwz(r3, MemOperand(fp, JavaScriptFrameConstants::kFunctionOffset));
  context()->Plug(r3);
}


Register FullCodeGenerator::result_register() {
  return r3;
}


Register FullCodeGenerator::context_register() {
  return cp;
}


void FullCodeGenerator::StoreToFrameField(int frame_offset, Register value) {
  ASSERT_EQ(POINTER_SIZE_ALIGN(frame_offset), frame_offset);
  __ StoreWord(value, MemOperand(fp, frame_offset), r0);
}


void FullCodeGenerator::LoadContextField(Register dst, int context_index) {
  __ LoadWord(dst, ContextOperand(cp, context_index), r0);
}


void FullCodeGenerator::PushFunctionArgumentForContextAllocation() {
  EMIT_STUB_MARKER(291);
  Scope* declaration_scope = scope()->DeclarationScope();
  if (declaration_scope->is_global_scope() ||
      declaration_scope->is_module_scope()) {
    // Contexts nested in the native context have a canonical empty function
    // as their closure, not the anonymous closure containing the global
    // code.  Pass a smi sentinel and let the runtime look up the empty
    // function.
    __ mov(ip, Operand(Smi::FromInt(0)));
  } else if (declaration_scope->is_eval_scope()) {
    // Contexts created by a call to eval have the same closure as the
    // context calling eval, not the anonymous closure containing the eval
    // code.  Fetch it from the context.
    __ lwz(ip, ContextOperand(cp, Context::CLOSURE_INDEX));
  } else {
    ASSERT(declaration_scope->is_function_scope());
    __ lwz(ip, MemOperand(fp, JavaScriptFrameConstants::kFunctionOffset));
  }
  __ push(ip);
}


// ----------------------------------------------------------------------------
// Non-local control flow support.

void FullCodeGenerator::EnterFinallyBlock() {
  EMIT_STUB_MARKER(292);
  ASSERT(!result_register().is(r4));
  // Store result register while executing finally block.
  __ push(result_register());
  // Cook return address in link register to stack (smi encoded Code* delta)
  __ mflr(r4);
  __ mov(ip, Operand(masm_->CodeObject()));
  __ sub(r4, r4, ip);
  ASSERT_EQ(1, kSmiTagSize + kSmiShiftSize);
  STATIC_ASSERT(kSmiTag == 0);
  __ add(r4, r4, r4);  // Convert to smi.

  // Store result register while executing finally block.
  __ push(r4);

  // Store pending message while executing finally block.
  ExternalReference pending_message_obj =
      ExternalReference::address_of_pending_message_obj(isolate());
  __ mov(ip, Operand(pending_message_obj));
  __ lwz(r4, MemOperand(ip));
  __ push(r4);

  ExternalReference has_pending_message =
      ExternalReference::address_of_has_pending_message(isolate());
  __ mov(ip, Operand(has_pending_message));
  __ lwz(r4, MemOperand(ip));
  __ SmiTag(r4);
  __ push(r4);

  ExternalReference pending_message_script =
      ExternalReference::address_of_pending_message_script(isolate());
  __ mov(ip, Operand(pending_message_script));
  __ lwz(r4, MemOperand(ip));
  __ push(r4);
}


void FullCodeGenerator::ExitFinallyBlock() {
  EMIT_STUB_MARKER(293);
  ASSERT(!result_register().is(r4));
  // Restore pending message from stack.
  __ pop(r4);
  ExternalReference pending_message_script =
      ExternalReference::address_of_pending_message_script(isolate());
  __ mov(ip, Operand(pending_message_script));
  __ stw(r4, MemOperand(ip));

  __ pop(r4);
  __ SmiUntag(r4);
  ExternalReference has_pending_message =
      ExternalReference::address_of_has_pending_message(isolate());
  __ mov(ip, Operand(has_pending_message));
  __ stw(r4, MemOperand(ip));

  __ pop(r4);
  ExternalReference pending_message_obj =
      ExternalReference::address_of_pending_message_obj(isolate());
  __ mov(ip, Operand(pending_message_obj));
  __ stw(r4, MemOperand(ip));

  // Restore result register from stack.
  __ pop(r4);

  // Uncook return address and return.
  __ pop(result_register());
  ASSERT_EQ(1, kSmiTagSize + kSmiShiftSize);
  __ srawi(r4, r4, 1);  // Un-smi-tag value.
  __ li(r0, Operand(SIGN_EXT_IMM16(0xdead)));
  __ mov(ip, Operand(masm_->CodeObject()));
  __ add(ip, ip, r4);
  __ mtctr(ip);
  __ bcr();
}


#undef __

#define __ ACCESS_MASM(masm())

FullCodeGenerator::NestedStatement* FullCodeGenerator::TryFinally::Exit(
    int* stack_depth,
    int* context_length) {
  EMIT_STUB_MARKER(294);
  // The macros used here must preserve the result register.

  // Because the handler block contains the context of the finally
  // code, we can restore it directly from there for the finally code
  // rather than iteratively unwinding contexts via their previous
  // links.
  __ Drop(*stack_depth);  // Down to the handler block.
  if (*context_length > 0) {
    // Restore the context to its dedicated register and the stack.
    __ lwz(cp, MemOperand(sp, StackHandlerConstants::kContextOffset));
    __ stw(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
  }
  __ PopTryHandler();
  __ bl(finally_entry_);

  *stack_depth = 0;
  *context_length = 0;
  return previous_;
}

#undef __
} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_PPC
