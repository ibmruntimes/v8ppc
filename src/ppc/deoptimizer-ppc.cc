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

#include "codegen.h"
#include "deoptimizer.h"
#include "full-codegen.h"
#include "safepoint-table.h"

namespace v8 {
namespace internal {

const int Deoptimizer::table_entry_size_ = 20;


int Deoptimizer::patch_size() {
#if V8_TARGET_ARCH_PPC64
  const int kCallInstructionSizeInWords = 7;
#else
  const int kCallInstructionSizeInWords = 4;
#endif
  return kCallInstructionSizeInWords * Assembler::kInstrSize;
}


void Deoptimizer::DeoptimizeFunction(JSFunction* function) {
  HandleScope scope;
  AssertNoAllocation no_allocation;

  if (!function->IsOptimized()) return;

  // The optimized code is going to be patched, so we cannot use it
  // any more.  Play safe and reset the whole cache.
  function->shared()->ClearOptimizedCodeMap();

  // Get the optimized code.
  Code* code = function->code();
  Address code_start_address = code->instruction_start();

  // Invalidate the relocation information, as it will become invalid by the
  // code patching below, and is not needed any more.
  code->InvalidateRelocation();

  // For each LLazyBailout instruction insert a call to the corresponding
  // deoptimization entry.
  DeoptimizationInputData* deopt_data =
      DeoptimizationInputData::cast(code->deoptimization_data());
#ifdef DEBUG
  Address prev_call_address = NULL;
#endif
  for (int i = 0; i < deopt_data->DeoptCount(); i++) {
    if (deopt_data->Pc(i)->value() == -1) continue;
    Address call_address = code_start_address + deopt_data->Pc(i)->value();
    Address deopt_entry = GetDeoptimizationEntry(i, LAZY);
    // We need calls to have a predictable size in the unoptimized code, but
    // this is optimized code, so we don't have to have a predictable size.
    int call_size_in_bytes =
        MacroAssembler::CallSizeNotPredictableCodeSize(deopt_entry,
                                                       RelocInfo::NONE);
    int call_size_in_words = call_size_in_bytes / Assembler::kInstrSize;
    ASSERT(call_size_in_bytes % Assembler::kInstrSize == 0);
    ASSERT(call_size_in_bytes <= patch_size());
    CodePatcher patcher(call_address, call_size_in_words);
    patcher.masm()->Call(deopt_entry, RelocInfo::NONE);
    ASSERT(prev_call_address == NULL ||
           call_address >= prev_call_address + patch_size());
    ASSERT(call_address + patch_size() <= code->instruction_end());
#ifdef DEBUG
    prev_call_address = call_address;
#endif
  }

  Isolate* isolate = code->GetIsolate();

  // Add the deoptimizing code to the list.
  DeoptimizingCodeListNode* node = new DeoptimizingCodeListNode(code);
  DeoptimizerData* data = isolate->deoptimizer_data();
  node->set_next(data->deoptimizing_code_list_);
  data->deoptimizing_code_list_ = node;

  // We might be in the middle of incremental marking with compaction.
  // Tell collector to treat this code object in a special way and
  // ignore all slots that might have been recorded on it.
  isolate->heap()->mark_compact_collector()->InvalidateCode(code);

  ReplaceCodeForRelatedFunctions(function, code);

  if (FLAG_trace_deopt) {
    PrintF("[forced deoptimization: ");
    function->PrintName();
    PrintF(" / %" V8PRIxPTR "]\n", reinterpret_cast<uintptr_t>(function));
  }
}


#if V8_TARGET_ARCH_PPC64
static const int32_t kBranchBeforeStackCheck = 0x409c0020;
static const int32_t kBranchBeforeInterrupt =  0x409c0044;
#else
static const int32_t kBranchBeforeStackCheck = 0x409c0014;
static const int32_t kBranchBeforeInterrupt =  0x409c0024;
#endif


// This code has some dependency on the FIXED_SEQUENCE lis/ori
void Deoptimizer::PatchStackCheckCodeAt(Code* unoptimized_code,
                                        Address pc_after,
                                        Code* check_code,
                                        Code* replacement_code) {
  const int kInstrSize = Assembler::kInstrSize;
  // There are two 'Stack check' sequences from full-codegen-ppc.cc
  // both have similar code - and FLAG_count_based_interrupts will
  // control which we expect to find
  //
  // The call of the stack guard check has the following form:
  // 409c0014       bge +40 -> 876  (0x25535c4c)  ;; (ok)
  // 3d802553       lis     r12, 9555        ;; two part load
  // 618c5000       ori     r12, r12, 20480  ;; of stack guard address
  // 7d8803a6       mtlr    r12
  // 4e800021       blrl
  //    <-- pc_after
  //
  // 64bit will have an expanded mov() [lis/ori] sequence

  // Check we have a branch & link through r12 (ip)
  ASSERT(Memory::int32_at(pc_after - 2 * kInstrSize) == 0x7d8803a6);
  ASSERT(Memory::int32_at(pc_after - kInstrSize) == 0x4e800021);

#if V8_TARGET_ARCH_PPC64
  ASSERT(Assembler::Is64BitLoadIntoR12(
      Assembler::instr_at(pc_after - 7 * kInstrSize),
      Assembler::instr_at(pc_after - 6 * kInstrSize),
      Assembler::instr_at(pc_after - 5 * kInstrSize),
      Assembler::instr_at(pc_after - 4 * kInstrSize),
      Assembler::instr_at(pc_after - 3 * kInstrSize)));
  if (FLAG_count_based_interrupts) {
    ASSERT_EQ(kBranchBeforeInterrupt,
              Memory::int32_at(pc_after - 8 * kInstrSize));
  } else {
    ASSERT_EQ(kBranchBeforeStackCheck,
              Memory::int32_at(pc_after - 8 * kInstrSize));
  }
#else
  ASSERT(Assembler::Is32BitLoadIntoR12(
      Assembler::instr_at(pc_after - 4 * kInstrSize),
      Assembler::instr_at(pc_after - 3 * kInstrSize)));
  if (FLAG_count_based_interrupts) {
    ASSERT_EQ(kBranchBeforeInterrupt,
              Memory::int32_at(pc_after - 5 * kInstrSize));
  } else {
    ASSERT_EQ(kBranchBeforeStackCheck,
              Memory::int32_at(pc_after - 5 * kInstrSize));
  }
#endif

  // We patch the code to the following form:
  // 60000000       ori     r0, r0, 0        ;; NOP
  // 3d80NNNN       lis     r12, NNNN        ;; two part load
  // 618cNNNN       ori     r12, r12, NNNN   ;; of on stack replace address
  // 7d8803a6       mtlr    r12
  // 4e800021       blrl

#if V8_TARGET_ARCH_PPC64
  CodePatcher patcher(pc_after - 8 * kInstrSize, 6);

  // Assemble the 64 bit value from the five part load and verify
  // that it is the stack guard code
  uint64_t stack_check_address =
    (Memory::uint32_at(pc_after - 7 * kInstrSize) & 0xFFFF) << 16;
  stack_check_address |=
    (Memory::uint32_at(pc_after - 6 * kInstrSize) & 0xFFFF);
  stack_check_address <<= 32;
  stack_check_address |=
    (Memory::uint32_at(pc_after - 4 * kInstrSize) & 0xFFFF) << 16;
  stack_check_address |=
    (Memory::uint32_at(pc_after - 3 * kInstrSize) & 0xFFFF);
#else
  CodePatcher patcher(pc_after - 5 * kInstrSize, 3);

  // Assemble the 32 bit value from the two part load and verify
  // that it is the stack guard code
  uint32_t stack_check_address =
    (Memory::int32_at(pc_after - 4 * kInstrSize) & 0xFFFF) << 16;
  stack_check_address |=
    (Memory::int32_at(pc_after - 3 * kInstrSize) & 0xFFFF);
#endif
  ASSERT(stack_check_address ==
    reinterpret_cast<uintptr_t>(check_code->entry()));

  // Replace conditional jump with NOP.
  patcher.masm()->nop();

  // Now modify the two part load (or 5 part on 64bit)
  patcher.masm()->mov(ip,
    Operand(reinterpret_cast<uintptr_t>(replacement_code->entry())));

#if V8_TARGET_ARCH_PPC64
  unoptimized_code->GetHeap()->incremental_marking()->RecordCodeTargetPatch(
      unoptimized_code, pc_after - 7 * kInstrSize, replacement_code);
#else
  unoptimized_code->GetHeap()->incremental_marking()->RecordCodeTargetPatch(
      unoptimized_code, pc_after - 4 * kInstrSize, replacement_code);
#endif
}


void Deoptimizer::RevertStackCheckCodeAt(Code* unoptimized_code,
                                         Address pc_after,
                                         Code* check_code,
                                         Code* replacement_code) {
  const int kInstrSize = Assembler::kInstrSize;

  // Check we have a branch & link through r12 (ip)
  ASSERT(Memory::int32_at(pc_after - 2 * kInstrSize) == 0x7d8803a6);
  ASSERT(Memory::int32_at(pc_after - kInstrSize) == 0x4e800021);

#if V8_TARGET_ARCH_PPC64
  ASSERT(Assembler::Is64BitLoadIntoR12(
      Assembler::instr_at(pc_after - 7 * kInstrSize),
      Assembler::instr_at(pc_after - 6 * kInstrSize),
      Assembler::instr_at(pc_after - 5 * kInstrSize),
      Assembler::instr_at(pc_after - 4 * kInstrSize),
      Assembler::instr_at(pc_after - 3 * kInstrSize)));
#else
  ASSERT(Assembler::Is32BitLoadIntoR12(
      Assembler::instr_at(pc_after - 4 * kInstrSize),
      Assembler::instr_at(pc_after - 3 * kInstrSize)));
#endif

#if V8_TARGET_ARCH_PPC64
  // Replace NOP with conditional jump.
  CodePatcher patcher(pc_after - 8 * kInstrSize, 6);
  if (FLAG_count_based_interrupts) {
      patcher.masm()->bc(+68, BF,
                v8::internal::Assembler::encode_crbit(cr7, CR_LT));  // bge
    ASSERT_EQ(kBranchBeforeInterrupt,
              Memory::int32_at(pc_after - 8 * kInstrSize));
  } else {
    patcher.masm()->bc(+32, BF,
                v8::internal::Assembler::encode_crbit(cr7, CR_LT));  // bge
    ASSERT_EQ(kBranchBeforeStackCheck,
              Memory::int32_at(pc_after - 8 * kInstrSize));
  }
#else
  // Replace NOP with conditional jump.
  CodePatcher patcher(pc_after - 5 * kInstrSize, 3);
  if (FLAG_count_based_interrupts) {
      patcher.masm()->bc(+36, BF,
                v8::internal::Assembler::encode_crbit(cr7, CR_LT));  // bge
    ASSERT_EQ(kBranchBeforeInterrupt,
              Memory::int32_at(pc_after - 5 * kInstrSize));
  } else {
    patcher.masm()->bc(+20, BF,
                v8::internal::Assembler::encode_crbit(cr7, CR_LT));  // bge
    ASSERT_EQ(kBranchBeforeStackCheck,
              Memory::int32_at(pc_after - 5 * kInstrSize));
  }
#endif

#if V8_TARGET_ARCH_PPC64
  // Assemble the 64 bit value from the five part load and verify
  // that it is the stack guard code
  uint64_t stack_check_address =
    (Memory::uint32_at(pc_after - 7 * kInstrSize) & 0xFFFF) << 16;
  stack_check_address |=
    (Memory::uint32_at(pc_after - 6 * kInstrSize) & 0xFFFF);
  stack_check_address <<= 32;
  stack_check_address |=
    (Memory::uint32_at(pc_after - 4 * kInstrSize) & 0xFFFF) << 16;
  stack_check_address |=
    (Memory::uint32_at(pc_after - 3 * kInstrSize) & 0xFFFF);
#else
  // Assemble the 32 bit value from the two part load and verify
  // that it is the replacement code address
  // This assumes a FIXED_SEQUENCE for lis/ori
  uint32_t stack_check_address =
    (Memory::int32_at(pc_after - 4 * kInstrSize) & 0xFFFF) << 16;
  stack_check_address |=
    (Memory::int32_at(pc_after - 3 * kInstrSize) & 0xFFFF);
#endif
  ASSERT(stack_check_address ==
    reinterpret_cast<uintptr_t>(replacement_code->entry()));

  // Now modify the two part load (or 5 part on 64bit)
  patcher.masm()->mov(ip,
    Operand(reinterpret_cast<uintptr_t>(check_code->entry())));

#if V8_TARGET_ARCH_PPC64
  check_code->GetHeap()->incremental_marking()->RecordCodeTargetPatch(
      unoptimized_code, pc_after - 7 * kInstrSize, check_code);
#else
  check_code->GetHeap()->incremental_marking()->RecordCodeTargetPatch(
      unoptimized_code, pc_after - 4 * kInstrSize, check_code);
#endif
}


static int LookupBailoutId(DeoptimizationInputData* data, BailoutId ast_id) {
  ByteArray* translations = data->TranslationByteArray();
  int length = data->DeoptCount();
  for (int i = 0; i < length; i++) {
    if (data->AstId(i) == ast_id) {
      TranslationIterator it(translations,  data->TranslationIndex(i)->value());
      int value = it.Next();
      ASSERT(Translation::BEGIN == static_cast<Translation::Opcode>(value));
      // Read the number of frames.
      value = it.Next();
      if (value == 1) return i;
    }
  }
  UNREACHABLE();
  return -1;
}


void Deoptimizer::DoComputeOsrOutputFrame() {
  DeoptimizationInputData* data = DeoptimizationInputData::cast(
      optimized_code_->deoptimization_data());
  unsigned ast_id = data->OsrAstId()->value();

  int bailout_id = LookupBailoutId(data, BailoutId(ast_id));
  unsigned translation_index = data->TranslationIndex(bailout_id)->value();
  ByteArray* translations = data->TranslationByteArray();

  TranslationIterator iterator(translations, translation_index);
  Translation::Opcode opcode =
      static_cast<Translation::Opcode>(iterator.Next());
  ASSERT(Translation::BEGIN == opcode);
  USE(opcode);
  int count = iterator.Next();
  iterator.Skip(1);  // Drop JS frame count.
  ASSERT(count == 1);
  USE(count);

  opcode = static_cast<Translation::Opcode>(iterator.Next());
  USE(opcode);
  ASSERT(Translation::JS_FRAME == opcode);
  unsigned node_id = iterator.Next();
  USE(node_id);
  ASSERT(node_id == ast_id);
  int closure_id = iterator.Next();
  USE(closure_id);
  ASSERT_EQ(Translation::kSelfLiteralId, closure_id);
  unsigned height = iterator.Next();
  unsigned height_in_bytes = height * kPointerSize;
  USE(height_in_bytes);

  unsigned fixed_size = ComputeFixedSize(function_);
  unsigned input_frame_size = input_->GetFrameSize();
  ASSERT(fixed_size + height_in_bytes == input_frame_size);

  unsigned stack_slot_size = optimized_code_->stack_slots() * kPointerSize;
  unsigned outgoing_height = data->ArgumentsStackHeight(bailout_id)->value();
  unsigned outgoing_size = outgoing_height * kPointerSize;
  unsigned output_frame_size = fixed_size + stack_slot_size + outgoing_size;
  ASSERT(outgoing_size == 0);  // OSR does not happen in the middle of a call.

  if (FLAG_trace_osr) {
    PrintF("[on-stack replacement: begin 0x%08" V8PRIxPTR " ",
           reinterpret_cast<intptr_t>(function_));
    function_->PrintName();
    PrintF(" => node=%u, frame=%d->%d]\n",
           ast_id,
           input_frame_size,
           output_frame_size);
  }

  // There's only one output frame in the OSR case.
  output_count_ = 1;
  output_ = new FrameDescription*[1];
  output_[0] = new(output_frame_size) FrameDescription(
      output_frame_size, function_);
  output_[0]->SetFrameType(StackFrame::JAVA_SCRIPT);

  // Clear the incoming parameters in the optimized frame to avoid
  // confusing the garbage collector.
  unsigned output_offset = output_frame_size - kPointerSize;
  int parameter_count = function_->shared()->formal_parameter_count() + 1;
  for (int i = 0; i < parameter_count; ++i) {
    output_[0]->SetFrameSlot(output_offset, 0);
    output_offset -= kPointerSize;
  }

  // Translate the incoming parameters. This may overwrite some of the
  // incoming argument slots we've just cleared.
  int input_offset = input_frame_size - kPointerSize;
  bool ok = true;
  int limit = input_offset - (parameter_count * kPointerSize);
  while (ok && input_offset > limit) {
    ok = DoOsrTranslateCommand(&iterator, &input_offset);
  }

  // There are no translation commands for the caller's pc and fp, the
  // context, and the function.  Set them up explicitly.
  for (int i =  StandardFrameConstants::kCallerPCOffset;
       ok && i >=  StandardFrameConstants::kMarkerOffset;
       i -= kPointerSize) {
    uintptr_t input_value = input_->GetFrameSlot(input_offset);
    if (FLAG_trace_osr) {
      const char* name = "UNKNOWN";
      switch (i) {
        case StandardFrameConstants::kCallerPCOffset:
          name = "caller's pc";
          break;
        case StandardFrameConstants::kCallerFPOffset:
          name = "fp";
          break;
        case StandardFrameConstants::kContextOffset:
          name = "context";
          break;
        case StandardFrameConstants::kMarkerOffset:
          name = "function";
          break;
      }
      PrintF("    [sp + %d] <- 0x%08" V8PRIxPTR " ;"
             " [sp + %d] (fixed part - %s)\n",
             output_offset,
             input_value,
             input_offset,
             name);
    }

    output_[0]->SetFrameSlot(output_offset, input_->GetFrameSlot(input_offset));
    input_offset -= kPointerSize;
    output_offset -= kPointerSize;
  }

  // Translate the rest of the frame.
  while (ok && input_offset >= 0) {
    ok = DoOsrTranslateCommand(&iterator, &input_offset);
  }

  // If translation of any command failed, continue using the input frame.
  if (!ok) {
    delete output_[0];
    output_[0] = input_;
    output_[0]->SetPc(reinterpret_cast<uintptr_t>(from_));
  } else {
    // Set up the frame pointer and the context pointer.
    output_[0]->SetRegister(fp.code(), input_->GetRegister(fp.code()));
    output_[0]->SetRegister(cp.code(), input_->GetRegister(cp.code()));

    unsigned pc_offset = data->OsrPcOffset()->value();
    uintptr_t pc = reinterpret_cast<uintptr_t>(
        optimized_code_->entry() + pc_offset);
    output_[0]->SetPc(pc);
  }
  Code* continuation = isolate_->builtins()->builtin(Builtins::kNotifyOSR);
  output_[0]->SetContinuation(
      reinterpret_cast<uintptr_t>(continuation->entry()));

  if (FLAG_trace_osr) {
    PrintF("[on-stack replacement translation %s: 0x%08" V8PRIxPTR " ",
           ok ? "finished" : "aborted",
           reinterpret_cast<intptr_t>(function_));
    function_->PrintName();
    PrintF(" => pc=0x%0" V8PRIxPTR "]\n", output_[0]->GetPc());
  }
}


void Deoptimizer::DoComputeArgumentsAdaptorFrame(TranslationIterator* iterator,
                                                 int frame_index) {
  JSFunction* function = JSFunction::cast(ComputeLiteral(iterator->Next()));
  unsigned height = iterator->Next();
  unsigned height_in_bytes = height * kPointerSize;
  if (FLAG_trace_deopt) {
    PrintF("  translating arguments adaptor => height=%d\n", height_in_bytes);
  }

  unsigned fixed_frame_size = ArgumentsAdaptorFrameConstants::kFrameSize;
  unsigned output_frame_size = height_in_bytes + fixed_frame_size;

  // Allocate and store the output frame description.
  FrameDescription* output_frame =
      new(output_frame_size) FrameDescription(output_frame_size, function);
  output_frame->SetFrameType(StackFrame::ARGUMENTS_ADAPTOR);

  // Arguments adaptor can not be topmost or bottommost.
  ASSERT(frame_index > 0 && frame_index < output_count_ - 1);
  ASSERT(output_[frame_index] == NULL);
  output_[frame_index] = output_frame;

  // The top address of the frame is computed from the previous
  // frame's top and this frame's size.
  uintptr_t top_address;
  top_address = output_[frame_index - 1]->GetTop() - output_frame_size;
  output_frame->SetTop(top_address);

  // Compute the incoming parameter translation.
  int parameter_count = height;
  unsigned output_offset = output_frame_size;
  for (int i = 0; i < parameter_count; ++i) {
    output_offset -= kPointerSize;
    DoTranslateCommand(iterator, frame_index, output_offset);
  }

  // Read caller's PC from the previous frame.
  output_offset -= kPointerSize;
  intptr_t callers_pc = output_[frame_index - 1]->GetPc();
  output_frame->SetFrameSlot(output_offset, callers_pc);
  if (FLAG_trace_deopt) {
    PrintF("    0x%08" V8PRIxPTR ": [top + %d] <- 0x%08" V8PRIxPTR
           " ; caller's pc\n",
           top_address + output_offset, output_offset, callers_pc);
  }

  // Read caller's FP from the previous frame, and set this frame's FP.
  output_offset -= kPointerSize;
  intptr_t value = output_[frame_index - 1]->GetFp();
  output_frame->SetFrameSlot(output_offset, value);
  intptr_t fp_value = top_address + output_offset;
  output_frame->SetFp(fp_value);
  if (FLAG_trace_deopt) {
    PrintF("    0x%08" V8PRIxPTR ": [top + %d] <- 0x%08" V8PRIxPTR
           " ; caller's fp\n", fp_value, output_offset, value);
  }

  // A marker value is used in place of the context.
  output_offset -= kPointerSize;
  intptr_t context = reinterpret_cast<intptr_t>(
      Smi::FromInt(StackFrame::ARGUMENTS_ADAPTOR));
  output_frame->SetFrameSlot(output_offset, context);
  if (FLAG_trace_deopt) {
    PrintF("    0x%08" V8PRIxPTR ": [top + %d] <- 0x%08" V8PRIxPTR
           " ; context (adaptor sentinel)\n",
           top_address + output_offset, output_offset, context);
  }

  // The function was mentioned explicitly in the ARGUMENTS_ADAPTOR_FRAME.
  output_offset -= kPointerSize;
  value = reinterpret_cast<intptr_t>(function);
  output_frame->SetFrameSlot(output_offset, value);
  if (FLAG_trace_deopt) {
    PrintF("    0x%08" V8PRIxPTR ": [top + %d] <- 0x%08" V8PRIxPTR
           " ; function\n",
           top_address + output_offset, output_offset, value);
  }

  // Number of incoming arguments.
  output_offset -= kPointerSize;
  value = reinterpret_cast<uintptr_t>(Smi::FromInt(height - 1));
  output_frame->SetFrameSlot(output_offset, value);
  if (FLAG_trace_deopt) {
    PrintF("    0x%08" V8PRIxPTR ": [top + %d] <- 0x%08" V8PRIxPTR
           " ; argc (%d)\n",
           top_address + output_offset, output_offset, value, height - 1);
  }

  ASSERT(0 == output_offset);

  Builtins* builtins = isolate_->builtins();
  Code* adaptor_trampoline =
      builtins->builtin(Builtins::kArgumentsAdaptorTrampoline);
  uintptr_t pc = reinterpret_cast<uintptr_t>(
      adaptor_trampoline->instruction_start() +
      isolate_->heap()->arguments_adaptor_deopt_pc_offset()->value());
  output_frame->SetPc(pc);
}


void Deoptimizer::DoComputeConstructStubFrame(TranslationIterator* iterator,
                                              int frame_index) {
  Builtins* builtins = isolate_->builtins();
  Code* construct_stub = builtins->builtin(Builtins::kJSConstructStubGeneric);
  JSFunction* function = JSFunction::cast(ComputeLiteral(iterator->Next()));
  unsigned height = iterator->Next();
  unsigned height_in_bytes = height * kPointerSize;
  if (FLAG_trace_deopt) {
    PrintF("  translating construct stub => height=%d\n", height_in_bytes);
  }

  unsigned fixed_frame_size = 8 * kPointerSize;
  unsigned output_frame_size = height_in_bytes + fixed_frame_size;

  // Allocate and store the output frame description.
  FrameDescription* output_frame =
      new(output_frame_size) FrameDescription(output_frame_size, function);
  output_frame->SetFrameType(StackFrame::CONSTRUCT);

  // Construct stub can not be topmost or bottommost.
  ASSERT(frame_index > 0 && frame_index < output_count_ - 1);
  ASSERT(output_[frame_index] == NULL);
  output_[frame_index] = output_frame;

  // The top address of the frame is computed from the previous
  // frame's top and this frame's size.
  uintptr_t top_address;
  top_address = output_[frame_index - 1]->GetTop() - output_frame_size;
  output_frame->SetTop(top_address);

  // Compute the incoming parameter translation.
  int parameter_count = height;
  unsigned output_offset = output_frame_size;
  for (int i = 0; i < parameter_count; ++i) {
    output_offset -= kPointerSize;
    DoTranslateCommand(iterator, frame_index, output_offset);
  }

  // Read caller's PC from the previous frame.
  output_offset -= kPointerSize;
  intptr_t callers_pc = output_[frame_index - 1]->GetPc();
  output_frame->SetFrameSlot(output_offset, callers_pc);
  if (FLAG_trace_deopt) {
    PrintF("    0x%08" V8PRIxPTR ": [top + %d] <- 0x%08" V8PRIxPTR
           " ; caller's pc\n",
           top_address + output_offset, output_offset, callers_pc);
  }

  // Read caller's FP from the previous frame, and set this frame's FP.
  output_offset -= kPointerSize;
  intptr_t value = output_[frame_index - 1]->GetFp();
  output_frame->SetFrameSlot(output_offset, value);
  intptr_t fp_value = top_address + output_offset;
  output_frame->SetFp(fp_value);
  if (FLAG_trace_deopt) {
    PrintF("    0x%08" V8PRIxPTR ": [top + %d] <- 0x%08" V8PRIxPTR
           " ; caller's fp\n", fp_value, output_offset, value);
  }

  // The context can be gotten from the previous frame.
  output_offset -= kPointerSize;
  value = output_[frame_index - 1]->GetContext();
  output_frame->SetFrameSlot(output_offset, value);
  if (FLAG_trace_deopt) {
    PrintF("    0x%08" V8PRIxPTR ": [top + %d] <- 0x%08" V8PRIxPTR
           " ; context\n",
           top_address + output_offset, output_offset, value);
  }

  // A marker value is used in place of the function.
  output_offset -= kPointerSize;
  value = reinterpret_cast<intptr_t>(Smi::FromInt(StackFrame::CONSTRUCT));
  output_frame->SetFrameSlot(output_offset, value);
  if (FLAG_trace_deopt) {
    PrintF("    0x%08" V8PRIxPTR ": [top + %d] <- 0x%08" V8PRIxPTR
           " ; function (construct sentinel)\n",
           top_address + output_offset, output_offset, value);
  }

  // The output frame reflects a JSConstructStubGeneric frame.
  output_offset -= kPointerSize;
  value = reinterpret_cast<intptr_t>(construct_stub);
  output_frame->SetFrameSlot(output_offset, value);
  if (FLAG_trace_deopt) {
    PrintF("    0x%08" V8PRIxPTR ": [top + %d] <- 0x%08" V8PRIxPTR
           " ; code object\n",
           top_address + output_offset, output_offset, value);
  }

  // Number of incoming arguments.
  output_offset -= kPointerSize;
  value = reinterpret_cast<uintptr_t>(Smi::FromInt(height - 1));
  output_frame->SetFrameSlot(output_offset, value);
  if (FLAG_trace_deopt) {
    PrintF("    0x%08" V8PRIxPTR ": [top + %d] <- 0x%08" V8PRIxPTR
           " ; argc (%d)\n",
           top_address + output_offset, output_offset, value, height - 1);
  }

  // Constructor function being invoked by the stub.
  output_offset -= kPointerSize;
  value = reinterpret_cast<intptr_t>(function);
  output_frame->SetFrameSlot(output_offset, value);
  if (FLAG_trace_deopt) {
    PrintF("    0x%08" V8PRIxPTR ": [top + %d] <- 0x%08" V8PRIxPTR
           " ; constructor function\n",
           top_address + output_offset, output_offset, value);
  }

  // The newly allocated object was passed as receiver in the artificial
  // constructor stub environment created by HEnvironment::CopyForInlining().
  output_offset -= kPointerSize;
  value = output_frame->GetFrameSlot(output_frame_size - kPointerSize);
  output_frame->SetFrameSlot(output_offset, value);
  if (FLAG_trace_deopt) {
    PrintF("    0x%08" V8PRIxPTR ": [top + %d] <- 0x%08" V8PRIxPTR
           " ; allocated receiver\n",
           top_address + output_offset, output_offset, value);
  }

  ASSERT(0 == output_offset);

  uintptr_t pc = reinterpret_cast<uintptr_t>(
      construct_stub->instruction_start() +
      isolate_->heap()->construct_stub_deopt_pc_offset()->value());
  output_frame->SetPc(pc);
}


void Deoptimizer::DoComputeAccessorStubFrame(TranslationIterator* iterator,
                                             int frame_index,
                                             bool is_setter_stub_frame) {
  JSFunction* accessor = JSFunction::cast(ComputeLiteral(iterator->Next()));
  // The receiver (and the implicit return value, if any) are expected in
  // registers by the LoadIC/StoreIC, so they don't belong to the output stack
  // frame. This means that we have to use a height of 0.
  unsigned height = 0;
  unsigned height_in_bytes = height * kPointerSize;
  const char* kind = is_setter_stub_frame ? "setter" : "getter";
  if (FLAG_trace_deopt) {
    PrintF("  translating %s stub => height=%u\n", kind, height_in_bytes);
  }

  // We need 5 stack entries from StackFrame::INTERNAL (lr, fp, cp, frame type,
  // code object, see MacroAssembler::EnterFrame). For a setter stub frames we
  // need one additional entry for the implicit return value, see
  // StoreStubCompiler::CompileStoreViaSetter.
  unsigned fixed_frame_entries = 5 + (is_setter_stub_frame ? 1 : 0);
  unsigned fixed_frame_size = fixed_frame_entries * kPointerSize;
  unsigned output_frame_size = height_in_bytes + fixed_frame_size;

  // Allocate and store the output frame description.
  FrameDescription* output_frame =
      new(output_frame_size) FrameDescription(output_frame_size, accessor);
  output_frame->SetFrameType(StackFrame::INTERNAL);

  // A frame for an accessor stub can not be the topmost or bottommost one.
  ASSERT(frame_index > 0 && frame_index < output_count_ - 1);
  ASSERT(output_[frame_index] == NULL);
  output_[frame_index] = output_frame;

  // The top address of the frame is computed from the previous frame's top and
  // this frame's size.
  uintptr_t top_address = (output_[frame_index - 1]->GetTop() -
                           output_frame_size);
  output_frame->SetTop(top_address);

  unsigned output_offset = output_frame_size;

  // Read caller's PC from the previous frame.
  output_offset -= kPointerSize;
  intptr_t callers_pc = output_[frame_index - 1]->GetPc();
  output_frame->SetFrameSlot(output_offset, callers_pc);
  if (FLAG_trace_deopt) {
    PrintF("    0x%08" V8PRIxPTR ": [top + %u] <- 0x%08" V8PRIxPTR
           " ; caller's pc\n",
           top_address + output_offset, output_offset, callers_pc);
  }

  // Read caller's FP from the previous frame, and set this frame's FP.
  output_offset -= kPointerSize;
  intptr_t value = output_[frame_index - 1]->GetFp();
  output_frame->SetFrameSlot(output_offset, value);
  intptr_t fp_value = top_address + output_offset;
  output_frame->SetFp(fp_value);
  if (FLAG_trace_deopt) {
    PrintF("    0x%08" V8PRIxPTR ": [top + %u] <- 0x%08" V8PRIxPTR
           " ; caller's fp\n",
           fp_value, output_offset, value);
  }

  // The context can be gotten from the previous frame.
  output_offset -= kPointerSize;
  value = output_[frame_index - 1]->GetContext();
  output_frame->SetFrameSlot(output_offset, value);
  if (FLAG_trace_deopt) {
    PrintF("    0x%08" V8PRIxPTR ": [top + %u] <- 0x%08" V8PRIxPTR
           " ; context\n",
           top_address + output_offset, output_offset, value);
  }

  // A marker value is used in place of the function.
  output_offset -= kPointerSize;
  value = reinterpret_cast<intptr_t>(Smi::FromInt(StackFrame::INTERNAL));
  output_frame->SetFrameSlot(output_offset, value);
  if (FLAG_trace_deopt) {
    PrintF("    0x%08" V8PRIxPTR ": [top + %u] <- 0x%08" V8PRIxPTR
           " ; function (%s sentinel)\n",
           top_address + output_offset, output_offset, value, kind);
  }

  // Get Code object from accessor stub.
  output_offset -= kPointerSize;
  Builtins::Name name = is_setter_stub_frame ?
      Builtins::kStoreIC_Setter_ForDeopt :
      Builtins::kLoadIC_Getter_ForDeopt;
  Code* accessor_stub = isolate_->builtins()->builtin(name);
  value = reinterpret_cast<intptr_t>(accessor_stub);
  output_frame->SetFrameSlot(output_offset, value);
  if (FLAG_trace_deopt) {
    PrintF("    0x%08" V8PRIxPTR ": [top + %u] <- 0x%08" V8PRIxPTR
           " ; code object\n",
           top_address + output_offset, output_offset, value);
  }

  // Skip receiver.
  Translation::Opcode opcode =
      static_cast<Translation::Opcode>(iterator->Next());
  iterator->Skip(Translation::NumberOfOperandsFor(opcode));

  if (is_setter_stub_frame) {
    // The implicit return value was part of the artificial setter stub
    // environment.
    output_offset -= kPointerSize;
    DoTranslateCommand(iterator, frame_index, output_offset);
  }

  ASSERT(0 == output_offset);

  Smi* offset = is_setter_stub_frame ?
      isolate_->heap()->setter_stub_deopt_pc_offset() :
      isolate_->heap()->getter_stub_deopt_pc_offset();
  intptr_t pc = reinterpret_cast<intptr_t>(
      accessor_stub->instruction_start() + offset->value());
  output_frame->SetPc(pc);
}


// This code is very similar to ia32 code, but relies on register names (fp, sp)
// and how the frame is laid out.
void Deoptimizer::DoComputeJSFrame(TranslationIterator* iterator,
                                   int frame_index) {
  // Read the ast node id, function, and frame height for this output frame.
  BailoutId node_id = BailoutId(iterator->Next());
  JSFunction* function;
  if (frame_index != 0) {
    function = JSFunction::cast(ComputeLiteral(iterator->Next()));
  } else {
    int closure_id = iterator->Next();
    USE(closure_id);
    ASSERT_EQ(Translation::kSelfLiteralId, closure_id);
    function = function_;
  }
  unsigned height = iterator->Next();
  unsigned height_in_bytes = height * kPointerSize;
  if (FLAG_trace_deopt) {
    PrintF("  translating ");
    function->PrintName();
    PrintF(" => node=%d, height=%d\n", node_id.ToInt(), height_in_bytes);
  }

  // The 'fixed' part of the frame consists of the incoming parameters and
  // the part described by JavaScriptFrameConstants.
  unsigned fixed_frame_size = ComputeFixedSize(function);
  unsigned input_frame_size = input_->GetFrameSize();
  unsigned output_frame_size = height_in_bytes + fixed_frame_size;

  // Allocate and store the output frame description.
  FrameDescription* output_frame =
      new(output_frame_size) FrameDescription(output_frame_size, function);
  output_frame->SetFrameType(StackFrame::JAVA_SCRIPT);

  bool is_bottommost = (0 == frame_index);
  bool is_topmost = (output_count_ - 1 == frame_index);
  ASSERT(frame_index >= 0 && frame_index < output_count_);
  ASSERT(output_[frame_index] == NULL);
  output_[frame_index] = output_frame;

  // The top address for the bottommost output frame can be computed from
  // the input frame pointer and the output frame's height.  For all
  // subsequent output frames, it can be computed from the previous one's
  // top address and the current frame's size.
  uintptr_t top_address;
  if (is_bottommost) {
    // 2 = context and function in the frame.
    top_address =
        input_->GetRegister(fp.code()) - (2 * kPointerSize) - height_in_bytes;
  } else {
    top_address = output_[frame_index - 1]->GetTop() - output_frame_size;
  }
  output_frame->SetTop(top_address);

  // Compute the incoming parameter translation.
  int parameter_count = function->shared()->formal_parameter_count() + 1;
  unsigned output_offset = output_frame_size;
  unsigned input_offset = input_frame_size;
  for (int i = 0; i < parameter_count; ++i) {
    output_offset -= kPointerSize;
    DoTranslateCommand(iterator, frame_index, output_offset);
  }
  input_offset -= (parameter_count * kPointerSize);

  // There are no translation commands for the caller's pc and fp, the
  // context, and the function.  Synthesize their values and set them up
  // explicitly.
  //
  // The caller's pc for the bottommost output frame is the same as in the
  // input frame.  For all subsequent output frames, it can be read from the
  // previous one.  This frame's pc can be computed from the non-optimized
  // function code and AST id of the bailout.
  output_offset -= kPointerSize;
  input_offset -= kPointerSize;
  intptr_t value;
  if (is_bottommost) {
    value = input_->GetFrameSlot(input_offset);
  } else {
    value = output_[frame_index - 1]->GetPc();
  }
  output_frame->SetFrameSlot(output_offset, value);
  if (FLAG_trace_deopt) {
    PrintF("    0x%08" V8PRIxPTR ": [top + %d] <- 0x%08" V8PRIxPTR
           " ; caller's pc\n",
           top_address + output_offset, output_offset, value);
  }

  // The caller's frame pointer for the bottommost output frame is the same
  // as in the input frame.  For all subsequent output frames, it can be
  // read from the previous one.  Also compute and set this frame's frame
  // pointer.
  output_offset -= kPointerSize;
  input_offset -= kPointerSize;
  if (is_bottommost) {
    value = input_->GetFrameSlot(input_offset);
  } else {
    value = output_[frame_index - 1]->GetFp();
  }
  output_frame->SetFrameSlot(output_offset, value);
  intptr_t fp_value = top_address + output_offset;
  ASSERT(!is_bottommost || input_->GetRegister(fp.code()) == fp_value);
  output_frame->SetFp(fp_value);
  if (is_topmost) {
    output_frame->SetRegister(fp.code(), fp_value);
  }
  if (FLAG_trace_deopt) {
    PrintF("    0x%08" V8PRIxPTR ": [top + %d] <- 0x%08" V8PRIxPTR
           " ; caller's fp\n", fp_value, output_offset, value);
  }

  // For the bottommost output frame the context can be gotten from the input
  // frame. For all subsequent output frames it can be gotten from the function
  // so long as we don't inline functions that need local contexts.
  output_offset -= kPointerSize;
  input_offset -= kPointerSize;
  if (is_bottommost) {
    value = input_->GetFrameSlot(input_offset);
  } else {
    value = reinterpret_cast<intptr_t>(function->context());
  }
  output_frame->SetFrameSlot(output_offset, value);
  output_frame->SetContext(value);
  if (is_topmost) output_frame->SetRegister(cp.code(), value);
  if (FLAG_trace_deopt) {
    PrintF("    0x%08" V8PRIxPTR ": [top + %d] <- 0x%08" V8PRIxPTR
           " ; context\n",
           top_address + output_offset, output_offset, value);
  }

  // The function was mentioned explicitly in the BEGIN_FRAME.
  output_offset -= kPointerSize;
  input_offset -= kPointerSize;
  value = reinterpret_cast<uintptr_t>(function);
  // The function for the bottommost output frame should also agree with the
  // input frame.
  ASSERT(!is_bottommost || input_->GetFrameSlot(input_offset) == value);
  output_frame->SetFrameSlot(output_offset, value);
  if (FLAG_trace_deopt) {
    PrintF("    0x%08" V8PRIxPTR ": [top + %d] <- 0x%08" V8PRIxPTR
           " ; function\n",
           top_address + output_offset, output_offset, value);
  }

  // Translate the rest of the frame.
  for (unsigned i = 0; i < height; ++i) {
    output_offset -= kPointerSize;
    DoTranslateCommand(iterator, frame_index, output_offset);
  }
  ASSERT(0 == output_offset);

  // Compute this frame's PC, state, and continuation.
  Code* non_optimized_code = function->shared()->code();
  FixedArray* raw_data = non_optimized_code->deoptimization_data();
  DeoptimizationOutputData* data = DeoptimizationOutputData::cast(raw_data);
  Address start = non_optimized_code->instruction_start();
  unsigned pc_and_state = GetOutputInfo(data, node_id, function->shared());
  unsigned pc_offset = FullCodeGenerator::PcField::decode(pc_and_state);
  uintptr_t pc_value = reinterpret_cast<uintptr_t>(start + pc_offset);
  output_frame->SetPc(pc_value);
#if 0  // applicable on PPC?
  if (is_topmost) {
    output_frame->SetRegister(pc.code(), pc_value);
  }
#endif

  FullCodeGenerator::State state =
      FullCodeGenerator::StateField::decode(pc_and_state);
  output_frame->SetState(Smi::FromInt(state));


  // Set the continuation for the topmost frame.
  if (is_topmost && bailout_type_ != DEBUGGER) {
    Builtins* builtins = isolate_->builtins();
    Code* continuation = (bailout_type_ == EAGER)
        ? builtins->builtin(Builtins::kNotifyDeoptimized)
        : builtins->builtin(Builtins::kNotifyLazyDeoptimized);
    output_frame->SetContinuation(
        reinterpret_cast<uintptr_t>(continuation->entry()));
  }
}


void Deoptimizer::FillInputFrame(Address tos, JavaScriptFrame* frame) {
  // Set the register values. The values are not important as there are no
  // callee saved registers in JavaScript frames, so all registers are
  // spilled. Registers fp and sp are set to the correct values though.

  for (int i = 0; i < Register::kNumRegisters; i++) {
    input_->SetRegister(i, i * 4);
  }
  input_->SetRegister(sp.code(), reinterpret_cast<intptr_t>(frame->sp()));
  input_->SetRegister(fp.code(), reinterpret_cast<intptr_t>(frame->fp()));
  for (int i = 0; i < DoubleRegister::kNumAllocatableRegisters; i++) {
    input_->SetDoubleRegister(i, 0.0);
  }

  // Fill the frame content from the actual data on the frame.
  for (unsigned i = 0; i < input_->GetFrameSize(); i += kPointerSize) {
    input_->SetFrameSlot(i, reinterpret_cast<intptr_t>(
                           Memory::Address_at(tos + i)));
  }
}


#define __ masm()->

// This code tries to be close to ia32 code so that any changes can be
// easily ported.
void Deoptimizer::EntryGenerator::Generate() {
  GeneratePrologue();

  Isolate* isolate = masm()->isolate();

  // Unlike on ARM we don't save all the registers, just the useful ones.
  // For the rest, there are gaps on the stack, so the offsets remain the same.
  const int kNumberOfRegisters = Register::kNumRegisters;

  RegList restored_regs = kJSCallerSaved | kCalleeSaved;
  RegList saved_regs = restored_regs | sp.bit();

  const int kDoubleRegsSize =
      kDoubleSize * DwVfpRegister::kNumAllocatableRegisters;

  // Save all FPU registers before messing with them.
  __ subi(sp, sp, Operand(kDoubleRegsSize));
  for (int i = 0; i < DwVfpRegister::kNumAllocatableRegisters; ++i) {
    DwVfpRegister fpu_reg = DwVfpRegister::FromAllocationIndex(i);
    int offset = i * kDoubleSize;
    __ stfd(fpu_reg, MemOperand(sp, offset));
  }

  // Push saved_regs (needed to populate FrameDescription::registers_).
  // Leave gaps for other registers.
  __ subi(sp, sp, Operand(kNumberOfRegisters * kPointerSize));
  for (int16_t i = kNumberOfRegisters - 1; i >= 0; i--) {
    if ((saved_regs & (1 << i)) != 0) {
      __ StoreP(ToRegister(i), MemOperand(sp, kPointerSize * i));
    }
  }

  const int kSavedRegistersAreaSize =
      (kNumberOfRegisters * kPointerSize) + kDoubleRegsSize;

  // Get the bailout id from the stack.
  __ LoadP(r5, MemOperand(sp, kSavedRegistersAreaSize));

  // Get the address of the location in the code object if possible (r6) (return
  // address for lazy deoptimization) and compute the fp-to-sp delta in
  // register r7.
  if (type() == EAGER) {
    __ li(r6, Operand::Zero());
    // Correct one word for bailout id.
    __ addi(r7, sp, Operand(kSavedRegistersAreaSize + (1 * kPointerSize)));
  } else if (type() == OSR) {
    __ mflr(r6);
    // Correct one word for bailout id.
    __ addi(r7, sp, Operand(kSavedRegistersAreaSize + (1 * kPointerSize)));
  } else {
    __ mflr(r6);
    // Correct two words for bailout id and return address.
    __ addi(r7, sp, Operand(kSavedRegistersAreaSize + (2 * kPointerSize)));
  }
  __ sub(r7, fp, r7);

  // Allocate a new deoptimizer object.
  // Pass six arguments in r3 to r8.
  __ PrepareCallCFunction(6, r8);
  __ LoadP(r3, MemOperand(fp, JavaScriptFrameConstants::kFunctionOffset));
  __ li(r4, Operand(type()));  // bailout type,
  // r5: bailout id already loaded.
  // r6: code address or 0 already loaded.
  // r7: Fp-to-sp delta.
  __ mov(r8, Operand(ExternalReference::isolate_address()));
  // Call Deoptimizer::New().
  {
    AllowExternalCallThatCantCauseGC scope(masm());
    __ CallCFunction(ExternalReference::new_deoptimizer_function(isolate), 6);
  }

  // Preserve "deoptimizer" object in register r3 and get the input
  // frame descriptor pointer to r4 (deoptimizer->input_);
  __ LoadP(r4, MemOperand(r3, Deoptimizer::input_offset()));

  // Copy core registers into FrameDescription::registers_[kNumRegisters].
  ASSERT(Register::kNumRegisters == kNumberOfRegisters);
  for (int i = 0; i < kNumberOfRegisters; i++) {
    int offset = (i * kPointerSize) + FrameDescription::registers_offset();
    __ LoadP(r5, MemOperand(sp, i * kPointerSize));
    __ StoreP(r5, MemOperand(r4, offset));
  }

  // Copy VFP registers to
  // double_registers_[DoubleRegister::kNumAllocatableRegisters]
  int double_regs_offset = FrameDescription::double_registers_offset();
  for (int i = 0; i < DwVfpRegister::kNumAllocatableRegisters; ++i) {
    int dst_offset = i * kDoubleSize + double_regs_offset;
    int src_offset = i * kDoubleSize + kNumberOfRegisters * kPointerSize;
    __ lfd(d0, MemOperand(sp, src_offset));
    __ stfd(d0, MemOperand(r4, dst_offset));
  }

  // Remove the bailout id, eventually return address, and the saved registers
  // from the stack.
  if (type() == EAGER || type() == OSR) {
    __ addi(sp, sp, Operand(kSavedRegistersAreaSize + (1 * kPointerSize)));
  } else {
    __ addi(sp, sp, Operand(kSavedRegistersAreaSize + (2 * kPointerSize)));
  }

  // Compute a pointer to the unwinding limit in register r5; that is
  // the first stack slot not part of the input frame.
  __ LoadP(r5, MemOperand(r4, FrameDescription::frame_size_offset()));
  __ add(r5, r5, sp);

  // Unwind the stack down to - but not including - the unwinding
  // limit and copy the contents of the activation frame to the input
  // frame description.
  __ addi(r6,  r4, Operand(FrameDescription::frame_content_offset()));
  Label pop_loop;
  __ bind(&pop_loop);
  __ pop(r7);
  __ StoreP(r7, MemOperand(r6, 0));
  __ addi(r6, r6, Operand(sizeof(intptr_t)));
  __ cmp(r5, sp);
  __ bne(&pop_loop);

  // Compute the output frame in the deoptimizer.
  __ push(r3);  // Preserve deoptimizer object across call.
  // r3: deoptimizer object; r4: scratch.
  __ PrepareCallCFunction(1, r4);
  // Call Deoptimizer::ComputeOutputFrames().
  {
    AllowExternalCallThatCantCauseGC scope(masm());
    __ CallCFunction(
        ExternalReference::compute_output_frames_function(isolate), 1);
  }
  __ pop(r3);  // Restore deoptimizer object (class Deoptimizer).

  // Replace the current (input) frame with the output frames.
  Label outer_push_loop, inner_push_loop;
  // Outer loop state: r3 = current "FrameDescription** output_",
  // r4 = one past the last FrameDescription**.
  __ lwz(r4, MemOperand(r3, Deoptimizer::output_count_offset()));
  __ LoadP(r3, MemOperand(r3, Deoptimizer::output_offset()));  // r3 is output_.
  __ ShiftLeftImm(r4, r4, Operand(kPointerSizeLog2));
  __ add(r4, r3, r4);
  __ bind(&outer_push_loop);
  // Inner loop state: r5 = current FrameDescription*, r6 = loop index.
  __ LoadP(r5, MemOperand(r3, 0));  // output_[ix]
  __ LoadP(r6, MemOperand(r5, FrameDescription::frame_size_offset()));

  __ bind(&inner_push_loop);
  __ addi(r6, r6, Operand(-sizeof(intptr_t)));
  __ add(r9, r5, r6);
  __ LoadP(r10, MemOperand(r9, FrameDescription::frame_content_offset()));
  __ push(r10);
  __ cmpi(r6, Operand::Zero());
  __ bne(&inner_push_loop);  // test for gt?

  __ addi(r3, r3, Operand(kPointerSize));
  __ cmp(r3, r4);
  __ blt(&outer_push_loop);

  // Push state, pc, and continuation from the last output frame.
  if (type() != OSR) {
    __ LoadP(r9, MemOperand(r5, FrameDescription::state_offset()));
    __ push(r9);
  }

  __ LoadP(r9, MemOperand(r5, FrameDescription::pc_offset()));
  __ push(r9);
  __ LoadP(r9, MemOperand(r5, FrameDescription::continuation_offset()));
  __ push(r9);

  // Restore the registers from the last output frame.
  ASSERT(!(ip.bit() & restored_regs));
  __ mr(ip, r5);
  for (int i = kNumberOfRegisters - 1; i >= 0; i--) {
    int offset = (i * kPointerSize) + FrameDescription::registers_offset();
    if ((restored_regs & (1 << i)) != 0) {
      __ LoadP(ToRegister(i), MemOperand(ip, offset));
    }
  }

  __ InitializeRootRegister();

  __ pop(r10);  // get continuation, leave pc on stack
  __ pop(r0);
  __ mtlr(r0);
  __ Jump(r10);
  __ stop("Unreachable.");
}


void Deoptimizer::TableEntryGenerator::GeneratePrologue() {
  Assembler::BlockTrampolinePoolScope block_trampoline_pool(masm());

  // Create a sequence of deoptimization entries. Note that any
  // registers may be still live.
  Label done;
  for (int i = 0; i < count(); i++) {
    int start = masm()->pc_offset();
    USE(start);
    if (type() == EAGER) {
      __ nop();
      __ nop();
    } else {
      // Emulate ia32 like call by pushing return address to stack.
      __ mflr(r0);
      __ push(r0);
    }
    __ li(ip, Operand(i));
    __ push(ip);
    __ b(&done);
    ASSERT(masm()->pc_offset() - start == table_entry_size_);
  }
  __ bind(&done);
}

#undef __

} }  // namespace v8::internal
