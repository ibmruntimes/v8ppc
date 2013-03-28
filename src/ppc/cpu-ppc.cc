// Copyright 2006-2009 the V8 project authors. All rights reserved.
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

// CPU specific code for arm independent of OS goes here.
#include "v8.h"

#if defined(V8_TARGET_ARCH_PPC)

#include "cpu.h"
#include "macro-assembler.h"
#include "simulator.h"  // for cache flushing.

namespace v8 {
namespace internal {

void CPU::SetUp() {
  CpuFeatures::Probe();
}


bool CPU::SupportsCrankshaft() {
  return CpuFeatures::IsSupported(VFP3);
}


void CPU::FlushICache(void* start, size_t size) {
  // Nothing to do flushing no instructions.
  if (size == 0) {
    return;
  }

#if defined (USE_SIMULATOR)
  // Not generating PPC instructions for C-code. This means that we are
  // building an PPC emulator based target.  We should notify the simulator
  // that the Icache was flushed.
  // None of this code ends up in the snapshot so there are no issues
  // around whether or not to generate the code when building snapshots.
  Simulator::FlushICache(Isolate::Current()->simulator_i_cache(), start, size);
#else

// This constant will be different for other versions of PowerPC
#define CACHELINESIZE 128

  int *end = (int*)start+(size/4);
  for( int *pointer=(int*)start; pointer<end; pointer+=CACHELINESIZE ) {
    __asm__( 
      "dcbf 0, %0  \n"  \
      "sync        \n"  \
      "icbi 0, %0  \n"  \
      "isync       \n"
      : /* no output */
      : "r" (pointer)); 
  }

#endif  // USE_SIMULATOR
}


void CPU::DebugBreak() {
#if !defined (__arm__) || !defined(CAN_USE_ARMV5_INSTRUCTIONS)
  UNIMPLEMENTED();  // when building ARM emulator target
#else
  asm volatile("bkpt 0");
#endif
}

} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_PPC
