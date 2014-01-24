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
  1 <<  14 |  // r14 (general use)
  1 <<  15 |  // r15 (general use)
  1 <<  16 |  // r16 (general use)
  1 <<  17 |  // r17 (general use)
  1 <<  18 |  // r18 (general use / cp in Javascript code)
  1 <<  19 |  // r19 (roots array in Javascript code)
              // r20-r30 unused
  1 <<  31;   // r31 (fp in Javascript code)


const int kNumCalleeSaved = 7;

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
// defined by the ABI.  Note that kNumRequiredStackFrameSlots must
// satisfy alignment requirements (rounding up if required).
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
#ifdef V8_TARGET_ARCH_PPC64
const int kNumRequiredStackFrameSlots = 14;
#else
const int kNumRequiredStackFrameSlots = 16;
#endif
const int kStackFrameLRSlot = 2;
const int kStackFrameExtraParamSlot = 14;
#else
// [0] back chain
// [1] link register save area
// [2] Parameter9 slot (if necessary)
// ...
const int kNumRequiredStackFrameSlots = 4;
const int kStackFrameLRSlot = 1;
const int kStackFrameExtraParamSlot = 2;
#endif

// ----------------------------------------------------


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
  // FP-relative.
  static const int kLengthOffset = StandardFrameConstants::kExpressionsOffset;

  static const int kFrameSize =
      StandardFrameConstants::kFixedFrameSize + kPointerSize;
};


class ConstructFrameConstants : public AllStatic {
 public:
  // FP-relative.
  static const int kImplicitReceiverOffset = -6 * kPointerSize;
  static const int kConstructorOffset      = -5 * kPointerSize;
  static const int kLengthOffset           = -4 * kPointerSize;
  static const int kCodeOffset = StandardFrameConstants::kExpressionsOffset;

  static const int kFrameSize =
      StandardFrameConstants::kFixedFrameSize + 4 * kPointerSize;
};


class InternalFrameConstants : public AllStatic {
 public:
  // FP-relative.
  static const int kCodeOffset = StandardFrameConstants::kExpressionsOffset;
};


inline Object* JavaScriptFrame::function_slot_object() const {
  const int offset = JavaScriptFrameConstants::kFunctionOffset;
  return Memory::Object_at(fp() + offset);
}


inline void StackHandler::SetFp(Address slot, Address fp) {
  Memory::Address_at(slot) = fp;
}


} }  // namespace v8::internal

#endif  // V8_PPC_FRAMES_PPC_H_
