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

#ifndef V8_PPC_FRAMES_PPC_H_
#define V8_PPC_FRAMES_PPC_H_

namespace v8 {
namespace internal {


// Register list in load/store instructions
// Note that the bit values must match those used in actual instruction encoding
const int kNumRegs = 32;


// Caller-saved/arguments registers
const RegList kJSCallerSaved =
  1 << 3  |  // r3  a1
  1 << 4  |  // r4  a2
  1 << 5  |  // r5  a3
  1 << 6  |  // r6  a4
  1 << 7  |  // r7  a5
  1 << 8  |  // r8  a6
  1 << 9  |  // r9  a7
  1 << 10 |  // r10 a8
  1 << 11;

const int kNumJSCallerSaved = 9;

typedef Object* JSCallerSavedBuffer[kNumJSCallerSaved];

// Return the code of the n-th caller-saved register available to JavaScript
// e.g. JSCallerSavedReg(0) returns r0.code() == 0
int JSCallerSavedCode(int n);


// Callee-saved registers preserved when switching from C to JavaScript
// N.B.  Do not bother saving all non-volatiles -- only those that v8
//       modifies without saving/restoring inline.
const RegList kCalleeSaved =
  1 <<  14 |  // r14 (argument passing in CEntryStub)
  1 <<  15 |  // r15 (argument passing in CEntryStub)
  1 <<  16 |  // r16 (argument passing in CEntryStub)
              // r17-r19 unused
  1 <<  20 |  // r20 (cp in Javascript code)
  1 <<  21 |  // r21 (roots array in Javascript code)
  1 <<  22 |  // r22 (r9 hack in Javascript code)
              // r23-r25 unused
  1 <<  26 |  // r26 (HandleScope logic in MacroAssembler)
  1 <<  27 |  // r27 (HandleScope logic in MacroAssembler)
  1 <<  28 |  // r28 (HandleScope logic in MacroAssembler)
  1 <<  29 |  // r29 (HandleScope logic in MacroAssembler)
              // r30 used but saved/restored inline
  1 <<  31;   // r31 (fp in Javascript code)


const int kNumCalleeSaved = 11;

// Number of registers for which space is reserved in safepoints. Must be a
// multiple of 8.
// TODO(regis): Only 8 registers may actually be sufficient. Revisit.
const int kNumSafepointRegisters = 32;

// Define the list of registers actually saved at safepoints.
// Note that the number of saved registers may be smaller than the reserved
// space, i.e. kNumSafepointSavedRegisters <= kNumSafepointRegisters.
const RegList kSafepointSavedRegisters = kJSCallerSaved | kCalleeSaved;
const int kNumSafepointSavedRegisters = kNumJSCallerSaved + kNumCalleeSaved;

// The following constants describe the stack frame linkage area as
// defined by the ABI.
#if defined(V8_TARGET_ARCH_PPC64) && __BYTE_ORDER == __LITTLE_ENDIAN
// [0] back chain
// [1] condition register save area
// [2] link register save area
// [3] TOC save area
// [4] Parameter1 save area
// ...
// [11] Parameter8 save area
// [12] Parameter9 slot (if necessary)
// ...
const int kNumRequiredStackFrameSlots = 12;
const int kStackFrameLRSlot = 2;
const int kStackFrameExtraParamSlot = 12;
#elif defined(_AIX) || defined(V8_TARGET_ARCH_PPC64)
// [0] back chain
// [1] condition register save area
// [2] link register save area
// [3] reserved for compiler
// [4] reserved by binder
// [5] TOC save area
// [6] Parameter1 save area
// ...
// [13] Parameter8 save area
// [14] Parameter9 slot (if necessary)
// ...
const int kNumRequiredStackFrameSlots = 14;
const int kStackFrameLRSlot = 2;
const int kStackFrameExtraParamSlot = 14;
#else
// [0] back chain
// [1] link register save area
// [2] Parameter9 slot (if necessary)
// ...
const int kNumRequiredStackFrameSlots = 2;
const int kStackFrameLRSlot = 1;
const int kStackFrameExtraParamSlot = 2;
#endif

// ----------------------------------------------------


class StackHandlerConstants : public AllStatic {
 public:
  static const int kNextOffset     = 0 * kPointerSize;
  static const int kCodeOffset     = 1 * kPointerSize;
#if defined(V8_TARGET_ARCH_PPC64) && (__BYTE_ORDER == __BIG_ENDIAN)
  static const int kStateOffset    = 2 * kPointerSize + kIntSize;
#else
  static const int kStateOffset    = 2 * kPointerSize;
#endif
  static const int kStateSlot      = 2 * kPointerSize;
  static const int kContextOffset  = 3 * kPointerSize;
  static const int kFPOffset       = 4 * kPointerSize;

  static const int kSize = kFPOffset + kPointerSize;
};


class EntryFrameConstants : public AllStatic {
 public:
  static const int kCallerFPOffset      = -3 * kPointerSize;
};


class ExitFrameConstants : public AllStatic {
 public:
  static const int kCodeOffset = -2 * kPointerSize;
  static const int kSPOffset = -1 * kPointerSize;

  // The caller fields are below the frame pointer on the stack.
  static const int kCallerFPOffset = 0 * kPointerSize;
  // The calling JS function is below FP.
  static const int kCallerPCOffset = 1 * kPointerSize;

  // FP-relative displacement of the caller's SP.  It points just
  // below the saved PC.
  static const int kCallerSPDisplacement = 2 * kPointerSize;
};


class StandardFrameConstants : public AllStatic {
 public:
  // Fixed part of the frame consists of return address, caller fp,
  // context and function.
  static const int kFixedFrameSize    =  4 * kPointerSize;
  static const int kExpressionsOffset = -3 * kPointerSize;
  static const int kMarkerOffset      = -2 * kPointerSize;
  static const int kContextOffset     = -1 * kPointerSize;
  static const int kCallerFPOffset    =  0 * kPointerSize;
  static const int kCallerPCOffset    =  1 * kPointerSize;
  static const int kCallerSPOffset    =  2 * kPointerSize;
};


class JavaScriptFrameConstants : public AllStatic {
 public:
  // FP-relative.
  static const int kLocal0Offset = StandardFrameConstants::kExpressionsOffset;
  static const int kLastParameterOffset = +2 * kPointerSize;
  static const int kFunctionOffset = StandardFrameConstants::kMarkerOffset;

  // Caller SP-relative.
  static const int kParam0Offset   = -2 * kPointerSize;
  static const int kReceiverOffset = -1 * kPointerSize;
};


class ArgumentsAdaptorFrameConstants : public AllStatic {
 public:
  static const int kLengthOffset = StandardFrameConstants::kExpressionsOffset;
  static const int kFrameSize =
      StandardFrameConstants::kFixedFrameSize + kPointerSize;
};


class InternalFrameConstants : public AllStatic {
 public:
  static const int kCodeOffset = StandardFrameConstants::kExpressionsOffset;
};


inline Object* JavaScriptFrame::function_slot_object() const {
  const int offset = JavaScriptFrameConstants::kFunctionOffset;
  return Memory::Object_at(fp() + offset);
}


} }  // namespace v8::internal

#endif  // V8_PPC_FRAMES_PPC_H_
