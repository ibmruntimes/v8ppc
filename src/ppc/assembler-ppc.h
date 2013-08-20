// Copyright (c) 1994-2006 Sun Microsystems Inc.
// All Rights Reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
// - Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// - Redistribution in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the
// distribution.
//
// - Neither the name of Sun Microsystems or the names of contributors may
// be used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
// OF THE POSSIBILITY OF SUCH DAMAGE.

// The original source code covered by the above license above has been
// modified significantly by Google Inc.
// Copyright 2012 the V8 project authors. All rights reserved.

//
// Copyright IBM Corp. 2012, 2013. All rights reserved.
//

// A light-weight ARM Assembler
// Generates user mode instructions for the ARM architecture up to version 5

#ifndef V8_PPC_ASSEMBLER_PPC_H_
#define V8_PPC_ASSEMBLER_PPC_H_
#include <stdio.h>
#include "assembler.h"
#include "constants-ppc.h"
#include "serialize.h"

namespace v8 {
namespace internal {

#define INCLUDE_ARM 1

// CPU Registers.
//
// 1) We would prefer to use an enum, but enum values are assignment-
// compatible with int, which has caused code-generation bugs.
//
// 2) We would prefer to use a class instead of a struct but we don't like
// the register initialization to depend on the particular initialization
// order (which appears to be different on OS X, Linux, and Windows for the
// installed versions of C++ we tried). Using a struct permits C-style
// "initialization". Also, the Register objects cannot be const as this
// forces initialization stubs in MSVC, making us dependent on initialization
// order.
//
// 3) By not using an enum, we are possibly preventing the compiler from
// doing certain constant folds, which may significantly reduce the
// code generated for some assembly instructions (because they boil down
// to a few constants). If this is a problem, we could change the code
// such that we use an enum in optimized mode, and the struct in debug
// mode. This way we get the compile-time error checking in debug mode
// and best performance in optimized code.

// Core register
struct Register {
  static const int kNumRegisters = 32;
  static const int kNumAllocatableRegisters = 8;  // r3-r10
  static const int kSizeInBytes = 4;

  static int ToAllocationIndex(Register reg) {
    int index = reg.code() - 3;  // r0-r2 are skipped
    ASSERT(index < kNumAllocatableRegisters);
    return index;
  }

  static Register FromAllocationIndex(int index) {
    ASSERT(index >= 0 && index < kNumAllocatableRegisters);
    return from_code(index + 3);  // r0-r2 are skipped
  }

  static const char* AllocationIndexToString(int index) {
    ASSERT(index >= 0 && index < kNumAllocatableRegisters);
    const char* const names[] = {
      "r3",
      "r4",
      "r5",
      "r6",
      "r7",
      "r8",
      "r9",
      "r10",  // currently last allocated register
      "r11",  // lithium scratch
      "r12",  // ip
      "r13",
      "r14",
      "r15",
      "r16",
      "r17",
      "r18",
      "r19",
      "r20",
      "r21",
      "r22",
      "r23",
      "r24",
      "r25",
      "r26",
      "r27",
      "r28",
      "r29",
      "r30",
    };
    return names[index];
  }

  static Register from_code(int code) {
    Register r = { code };
    return r;
  }

  bool is_valid() const { return 0 <= code_ && code_ < kNumRegisters; }
  bool is(Register reg) const { return code_ == reg.code_; }
  int code() const {
    ASSERT(is_valid());
    return code_;
  }
  int bit() const {
    ASSERT(is_valid());
    return 1 << code_;
  }

  void set_code(int code) {
    code_ = code;
    ASSERT(is_valid());
  }

  // Unfortunately we can't make this private in a struct.
  int code_;
};

// These constants are used in several locations, including static initializers
const int kRegister_no_reg_Code = -1;
const int kRegister_r0_Code = 0;
const int kRegister_sp_Code = 1;  // todo - rename to SP
const int kRegister_r2_Code = 2;  // special on PowerPC
const int kRegister_r3_Code = 3;
const int kRegister_r4_Code = 4;
const int kRegister_r5_Code = 5;
const int kRegister_r6_Code = 6;
const int kRegister_r7_Code = 7;
const int kRegister_r8_Code = 8;
const int kRegister_r9_Code = 9;
const int kRegister_r10_Code = 10;
const int kRegister_r11_Code = 11;
const int kRegister_ip_Code = 12;  // todo - fix
const int kRegister_r13_Code = 13;
const int kRegister_r14_Code = 14;
const int kRegister_r15_Code = 15;

const int kRegister_r16_Code = 16;
const int kRegister_r17_Code = 17;
const int kRegister_r18_Code = 18;
const int kRegister_r19_Code = 19;
const int kRegister_r20_Code = 20;
const int kRegister_r21_Code = 21;
const int kRegister_r22_Code = 22;
const int kRegister_r23_Code = 23;
const int kRegister_r24_Code = 24;
const int kRegister_r25_Code = 25;
const int kRegister_r26_Code = 26;
const int kRegister_r27_Code = 27;
const int kRegister_r28_Code = 28;
const int kRegister_r29_Code = 29;
const int kRegister_r30_Code = 30;
const int kRegister_fp_Code = 31;

const Register no_reg = { kRegister_no_reg_Code };

const Register r0  = { kRegister_r0_Code };
const Register sp  = { kRegister_sp_Code };
const Register r2  = { kRegister_r2_Code };
const Register r3  = { kRegister_r3_Code };
const Register r4  = { kRegister_r4_Code };
const Register r5  = { kRegister_r5_Code };
const Register r6  = { kRegister_r6_Code };
const Register r7  = { kRegister_r7_Code };
const Register r8  = { kRegister_r8_Code };
const Register r9  = { kRegister_r9_Code };
const Register r10 = { kRegister_r10_Code };
// Used as lithium codegen scratch register.
const Register r11 = { kRegister_r11_Code };
const Register ip  = { kRegister_ip_Code };
// Used as roots register.
const Register r13  = { kRegister_r13_Code };
const Register r14  = { kRegister_r14_Code };
const Register r15  = { kRegister_r15_Code };

const Register r16  = { kRegister_r16_Code };
const Register r17  = { kRegister_r17_Code };
const Register r18  = { kRegister_r18_Code };
const Register r19  = { kRegister_r19_Code };
// Used as context register.
const Register r20  = { kRegister_r20_Code };
const Register r21  = { kRegister_r21_Code };
const Register r22  = { kRegister_r22_Code };
const Register r23  = { kRegister_r23_Code };
const Register r24  = { kRegister_r24_Code };
const Register r25  = { kRegister_r25_Code };
const Register r26  = { kRegister_r26_Code };
const Register r27  = { kRegister_r27_Code };
const Register r28  = { kRegister_r28_Code };
const Register r29  = { kRegister_r29_Code };
const Register r30  = { kRegister_r30_Code };
const Register fp = { kRegister_fp_Code };

// Double word VFP register.
struct DwVfpRegister {
  static const int kNumRegisters = 32;
  static const int kNumAllocatableRegisters = 12;  // d1-d12

  inline static int ToAllocationIndex(DwVfpRegister reg);

  static DwVfpRegister FromAllocationIndex(int index) {
    ASSERT(index >= 0 && index < kNumAllocatableRegisters);
    return from_code(index + 1);  // d0 is skipped
  }

  static const char* AllocationIndexToString(int index) {
    ASSERT(index >= 0 && index < kNumAllocatableRegisters);
    const char* const names[] = {
      "d1",
      "d2",
      "d3",
      "d4",
      "d5",
      "d6",
      "d7",
      "d8",
      "d9",
      "d10",
      "d11",
      "d12",
    };
    return names[index];
  }

  static DwVfpRegister from_code(int code) {
    DwVfpRegister r = { code };
    return r;
  }

  // Supporting d0 to d15, can be later extended to d31.
  bool is_valid() const { return 0 <= code_ && code_ < kNumRegisters; }
  bool is(DwVfpRegister reg) const { return code_ == reg.code_; }

  int code() const {
    ASSERT(is_valid());
    return code_;
  }
  int bit() const {
    ASSERT(is_valid());
    return 1 << code_;
  }
  void split_code(int* vm, int* m) const {
    ASSERT(is_valid());
    *m = (code_ & 0x10) >> 4;
    *vm = code_ & 0x0F;
  }

  int code_;
};


typedef DwVfpRegister DoubleRegister;

const DwVfpRegister no_dreg = { -1 };
const DwVfpRegister d0  = {  0 };
const DwVfpRegister d1  = {  1 };
const DwVfpRegister d2  = {  2 };
const DwVfpRegister d3  = {  3 };
const DwVfpRegister d4  = {  4 };
const DwVfpRegister d5  = {  5 };
const DwVfpRegister d6  = {  6 };
const DwVfpRegister d7  = {  7 };
const DwVfpRegister d8  = {  8 };
const DwVfpRegister d9  = {  9 };
const DwVfpRegister d10 = { 10 };
const DwVfpRegister d11 = { 11 };
const DwVfpRegister d12 = { 12 };
const DwVfpRegister d13 = { 13 };
const DwVfpRegister d14 = { 14 };
const DwVfpRegister d15 = { 15 };
const DwVfpRegister d16 = { 16 };
const DwVfpRegister d17 = { 17 };
const DwVfpRegister d18 = { 18 };
const DwVfpRegister d19 = { 19 };
const DwVfpRegister d20 = { 20 };
const DwVfpRegister d21 = { 21 };
const DwVfpRegister d22 = { 22 };
const DwVfpRegister d23 = { 23 };
const DwVfpRegister d24 = { 24 };
const DwVfpRegister d25 = { 25 };
const DwVfpRegister d26 = { 26 };
const DwVfpRegister d27 = { 27 };
const DwVfpRegister d28 = { 28 };
const DwVfpRegister d29 = { 29 };
const DwVfpRegister d30 = { 30 };
const DwVfpRegister d31 = { 31 };

// Aliases for double registers.  Defined using #define instead of
// "static const DwVfpRegister&" because Clang complains otherwise when a
// compilation unit that includes this header doesn't use the variables.
#define kFirstCalleeSavedDoubleReg d14
#define kLastCalleeSavedDoubleReg d31
#define kDoubleRegZero d14
#define kScratchDoubleReg d13

Register ToRegister(int num);

// Coprocessor register
struct CRegister {
  bool is_valid() const { return 0 <= code_ && code_ < 16; }
  bool is(CRegister creg) const { return code_ == creg.code_; }
  int code() const {
    ASSERT(is_valid());
    return code_;
  }
  int bit() const {
    ASSERT(is_valid());
    return 1 << code_;
  }

  // Unfortunately we can't make this private in a struct.
  int code_;
};


const CRegister no_creg = { -1 };

const CRegister cr0  = {  0 };
const CRegister cr1  = {  1 };
const CRegister cr2  = {  2 };
const CRegister cr3  = {  3 };
const CRegister cr4  = {  4 };
const CRegister cr5  = {  5 };
const CRegister cr6  = {  6 };
const CRegister cr7  = {  7 };
const CRegister cr8  = {  8 };
const CRegister cr9  = {  9 };
const CRegister cr10 = { 10 };
const CRegister cr11 = { 11 };
const CRegister cr12 = { 12 };
const CRegister cr13 = { 13 };
const CRegister cr14 = { 14 };
const CRegister cr15 = { 15 };

// -----------------------------------------------------------------------------
// Machine instruction Operands

// Class Operand represents a shifter operand in data processing instructions
class Operand BASE_EMBEDDED {
 public:
  // immediate
#if V8_TARGET_ARCH_PPC64
  INLINE(explicit Operand(int64_t immediate,
         RelocInfo::Mode rmode = RelocInfo::NONE));
  INLINE(static Operand Zero()) {
    return Operand(static_cast<int64_t>(0));
  }
#else
  INLINE(explicit Operand(int32_t immediate,
         RelocInfo::Mode rmode = RelocInfo::NONE));
  INLINE(static Operand Zero()) {
    return Operand(static_cast<int32_t>(0));
  }
#endif
  INLINE(explicit Operand(const ExternalReference& f));
  explicit Operand(Handle<Object> handle);
  INLINE(explicit Operand(Smi* value));

  // rm
  INLINE(explicit Operand(Register rm));

  // Return true if this is a register operand.
  INLINE(bool is_reg() const);

  inline int32_t immediate() const {
    ASSERT(!rm_.is_valid());
    return imm32_;
  }

  Register rm() const { return rm_; }

 private:
  Register rm_;
  int32_t imm32_;  // valid if rm_ == no_reg
  RelocInfo::Mode rmode_;

  friend class Assembler;
  friend class MacroAssembler;
};


// Class MemOperand represents a memory operand in load and store instructions
// On PowerPC we have base register + 16bit signed value
// Alternatively we can have a 16bit signed value immediate
class MemOperand BASE_EMBEDDED {
 public:
  // Contains cruft left to allow ARM to continue to work

  // PowerPC (remove AddrMode later)
  explicit MemOperand(Register rn, int32_t offset = 0, AddrMode am = Offset);

  explicit MemOperand(Register ra, Register rb);

  uint32_t offset() const {
    ASSERT(validPPCAddressing_ && rb_.is(no_reg));
    return offset_;
  }

  // PowerPC - base register
  Register ra() const {
    ASSERT(validPPCAddressing_ && !ra_.is(no_reg));
    return ra_;
  }

  Register rb() const {
    ASSERT(validPPCAddressing_ && offset_ == 0 && !rb_.is(no_reg));
    return rb_;
  }

  bool isPPCAddressing() const { return validPPCAddressing_;}

 private:
  Register ra_;  // base
  int32_t offset_;  // offset
  Register rb_;  // index
  bool validPPCAddressing_;

  friend class Assembler;
};

// CpuFeatures keeps track of which features are supported by the target CPU.
// Supported features must be enabled by a Scope before use.
class CpuFeatures : public AllStatic {
 public:
  // Detect features of the target CPU. Set safe defaults if the serializer
  // is enabled (snapshots must be portable).
  static void Probe();

  // Check whether a feature is supported by the target CPU.
  static bool IsSupported(CpuFeature f) {
    ASSERT(initialized_);
    return (supported_ & (1u << f)) != 0;
  }

#ifdef DEBUG
  // Check whether a feature is currently enabled.
  static bool IsEnabled(CpuFeature f) {
    ASSERT(initialized_);
    Isolate* isolate = Isolate::UncheckedCurrent();
    if (isolate == NULL) {
      // When no isolate is available, work as if we're running in
      // release mode.
      return IsSupported(f);
    }
    unsigned enabled = static_cast<unsigned>(isolate->enabled_cpu_features());
    return (enabled & (1u << f)) != 0;
  }
#endif

  // Enable a specified feature within a scope.
  class Scope BASE_EMBEDDED {
#ifdef DEBUG

   public:
    explicit Scope(CpuFeature f) {
      unsigned mask = 1u << f;
      ASSERT(CpuFeatures::IsSupported(f));
      ASSERT(!Serializer::enabled() ||
             (CpuFeatures::found_by_runtime_probing_ & mask) == 0);
      isolate_ = Isolate::UncheckedCurrent();
      old_enabled_ = 0;
      if (isolate_ != NULL) {
        old_enabled_ = static_cast<unsigned>(isolate_->enabled_cpu_features());
        isolate_->set_enabled_cpu_features(old_enabled_ | mask);
      }
    }
    ~Scope() {
      ASSERT_EQ(Isolate::UncheckedCurrent(), isolate_);
      if (isolate_ != NULL) {
        isolate_->set_enabled_cpu_features(old_enabled_);
      }
    }

   private:
    Isolate* isolate_;
    unsigned old_enabled_;
#else

   public:
    explicit Scope(CpuFeature f) {}
#endif
  };

  class TryForceFeatureScope BASE_EMBEDDED {
   public:
    explicit TryForceFeatureScope(CpuFeature f)
        : old_supported_(CpuFeatures::supported_) {
      if (CanForce()) {
        CpuFeatures::supported_ |= (1u << f);
      }
    }

    ~TryForceFeatureScope() {
      if (CanForce()) {
        CpuFeatures::supported_ = old_supported_;
      }
    }

   private:
    static bool CanForce() {
      // It's only safe to temporarily force support of CPU features
      // when there's only a single isolate, which is guaranteed when
      // the serializer is enabled.
      return Serializer::enabled();
    }

    const unsigned old_supported_;
  };

 private:
#ifdef DEBUG
  static bool initialized_;
#endif
  static unsigned supported_;
  static unsigned found_by_runtime_probing_;

  DISALLOW_COPY_AND_ASSIGN(CpuFeatures);
};


extern const Instr kMovMvnMask;
extern const Instr kMovMvnPattern;
extern const Instr kMovMvnFlip;

extern const Instr kMovLeaveCCMask;
extern const Instr kMovLeaveCCPattern;
extern const Instr kMovwMask;
extern const Instr kMovwPattern;
extern const Instr kMovwLeaveCCFlip;

extern const Instr kCmpCmnMask;
extern const Instr kCmpCmnPattern;
extern const Instr kCmpCmnFlip;
extern const Instr kAddSubFlip;
extern const Instr kAndBicFlip;



class Assembler : public AssemblerBase {
 public:
  // Create an assembler. Instructions and relocation information are emitted
  // into a buffer, with the instructions starting from the beginning and the
  // relocation information starting from the end of the buffer. See CodeDesc
  // for a detailed comment on the layout (globals.h).
  //
  // If the provided buffer is NULL, the assembler allocates and grows its own
  // buffer, and buffer_size determines the initial buffer size. The buffer is
  // owned by the assembler and deallocated upon destruction of the assembler.
  //
  // If the provided buffer is not NULL, the assembler uses the provided buffer
  // for code generation and assumes its size to be buffer_size. If the buffer
  // is too small, a fatal error occurs. No deallocation of the buffer is done
  // upon destruction of the assembler.
  Assembler(Isolate* isolate, void* buffer, int buffer_size);
  ~Assembler();

  // Overrides the default provided by FLAG_debug_code.
  void set_emit_debug_code(bool value) { emit_debug_code_ = value; }

  // Avoids using instructions that vary in size in unpredictable ways between
  // the snapshot and the running VM.  This is needed by the full compiler so
  // that it can recompile code with debug support and fix the PC.
  void set_predictable_code_size(bool value) { predictable_code_size_ = value; }

  // GetCode emits any pending (non-emitted) code and fills the descriptor
  // desc. GetCode() is idempotent; it returns the same result if no other
  // Assembler functions are invoked in between GetCode() calls.
  void GetCode(CodeDesc* desc);

  // Label operations & relative jumps (PPUM Appendix D)
  //
  // Takes a branch opcode (cc) and a label (L) and generates
  // either a backward branch or a forward branch and links it
  // to the label fixup chain. Usage:
  //
  // Label L;    // unbound label
  // j(cc, &L);  // forward branch to unbound label
  // bind(&L);   // bind label to the current pc
  // j(cc, &L);  // backward branch to bound label
  // bind(&L);   // illegal: a label may be bound only once
  //
  // Note: The same Label can be used for forward and backward branches
  // but it may be bound only once.

  void bind(Label* L);  // binds an unbound label L to the current code position

  // Returns the branch offset to the given label from the current code position
  // Links the label to the current position if it is still unbound
  // Manages the jump elimination optimization if the second parameter is true.
  int branch_offset(Label* L, bool jump_elimination_allowed);

  // Puts a labels target address at the given position.
  // The high 8 bits are set to zero.
  void label_at_put(Label* L, int at_offset);

  // Read/Modify the code target address in the branch/call instruction at pc.
  INLINE(static Address target_address_at(Address pc));
  INLINE(static void set_target_address_at(Address pc, Address target));

  // Return the code target address at a call site from the return address
  // of that call in the instruction stream.
  inline static Address target_address_from_return_address(Address pc);

  // This sets the branch destination.
  // This is for calls and branches within generated code.
  inline static void deserialization_set_special_target_at(
      Address instruction_payload, Address target);

  // Size of an instruction.
  static const int kInstrSize = sizeof(Instr);

  // Here we are patching the address in the LUI/ORI instruction pair.
  // These values are used in the serialization process and must be zero for
  // PPC platform, as Code, Embedded Object or External-reference pointers
  // are split across two consecutive instructions and don't exist separately
  // in the code, so the serializer should not step forwards in memory after
  // a target is resolved and written.
  static const int kSpecialTargetSize = 0;

  // Number of consecutive instructions used to store 32bit constant.
  static const int kInstructionsFor32BitConstant = 2;

  // Distance between the instruction referring to the address of the call
  // target and the return address.

  // Call sequence is:
  // lis     r8, 2148      @ call address hi
  // addic   r8, r8, 5728  @ call address lo
  // mtlr    r8
  // blrl
  //                      @ return address
  static const int kCallTargetAddressOffset = 4 * kInstrSize;

  // Distance between start of patched return sequence and the emitted address
  // to jump to.
  // Patched return sequence is:
  //   lis r0, <address hi>
  //   addic r0, r0, <address lo>
  //   mtlr r0
  //   blrl
  static const int kPatchReturnSequenceAddressOffset =  0 * kInstrSize;

  // Distance between start of patched debug break slot and the emitted address
  // to jump to.
  // Patched debug break slot code is:
  //   lis r0, <address hi>
  //   addic r0, r0, <address lo>
  //   mtlr r0
  //   blrl
  static const int kPatchDebugBreakSlotAddressOffset =  0 * kInstrSize;

  // Difference between address of current opcode and value read from pc
  // register.
  static const int kPcLoadDelta = 0;  // Todo: remove

  static const int kPatchDebugBreakSlotReturnOffset = 4 * kInstrSize;

  static const int kJSReturnSequenceInstructions = 5;
  static const int kDebugBreakSlotInstructions = 4;
  static const int kDebugBreakSlotLength =
      kDebugBreakSlotInstructions * kInstrSize;

  static inline int encode_crbit(const CRegister& cr, enum CRBit crbit) {
    return ((cr.code() * CRWIDTH) + crbit);
  }

  // ---------------------------------------------------------------------------
  // Code generation

  // Insert the smallest number of nop instructions
  // possible to align the pc offset to a multiple
  // of m. m must be a power of 2 (>= 4).
  void Align(int m);
  // Aligns code to something that's optimal for a jump target for the platform.
  void CodeTargetAlign();

  // Branch instructions
  // PowerPC
  void bclr(BOfield bo, LKBit lk);
  void blr();
  void bc(int branch_offset, BOfield bo, int condition_bit, LKBit lk = LeaveLK);
  void b(int branch_offset, LKBit lk);
  void b(int branch_offset, Condition cond = al);

  void bcctr(BOfield bo, LKBit lk);
  void bcr();

  // end PowerPC
  void bl(int branch_offset, Condition cond = al);

  // Convenience branch instructions using labels
  void b(Label* L, Condition cond = al)  {
    b(branch_offset(L, cond == al), cond);
  }
  void b(Label* L, LKBit lk)  {
    b(branch_offset(L, false), lk);
  }
  // PowerPC
  void bc(Label* L, BOfield bo, int bit, LKBit lk = LeaveLK)  {
    bc(branch_offset(L, false), bo, bit, lk);
  }
  void b(Condition cond, Label* L, CRegister cr = cr7, LKBit lk = LeaveLK)  {
    if (cond == al) {
        b(L, lk);
        return;
    }

    ASSERT(cr.code() >= 0 && cr.code() <= 7);
    int b_offset = branch_offset(L, (cond == al));

    // if the offset fits in the 16-bit immediate
    // field (b-form) we use that, otherwise use a
    // branch conditional to the i-form which gives
    // us 26 bits
    if (is_int16(b_offset)) {
      switch (cond) {
        case eq:
          bc(b_offset, BT, encode_crbit(cr, CR_EQ), lk);
          break;
        case ne:
          bc(b_offset, BF, encode_crbit(cr, CR_EQ), lk);
          break;
        case gt:
          bc(b_offset, BT, encode_crbit(cr, CR_GT), lk);
          break;
        case le:
          bc(b_offset, BF, encode_crbit(cr, CR_GT), lk);
          break;
        case lt:
          bc(b_offset, BT, encode_crbit(cr, CR_LT), lk);
          break;
        case ge:
          bc(b_offset, BF, encode_crbit(cr, CR_LT), lk);
          break;
        default:
          fake_asm(fBranch);
          // UNIMPLEMENTED();
      }
    } else {
      // as mentioned above, somewhat of a hack here: use the bc of
      // the negated condition to branch around the i-form instruction
      switch (NegateCondition(cond)) {
        case eq:
          bc(kInstrSize*2, BT, encode_crbit(cr, CR_EQ), LeaveLK);
          break;
        case ne:
          bc(kInstrSize*2, BF, encode_crbit(cr, CR_EQ), LeaveLK);
          break;
        case gt:
          bc(kInstrSize*2, BT, encode_crbit(cr, CR_GT), LeaveLK);
          break;
        case le:
          bc(kInstrSize*2, BF, encode_crbit(cr, CR_GT), LeaveLK);
          break;
        case lt:
          bc(kInstrSize*2, BT, encode_crbit(cr, CR_LT), LeaveLK);
          break;
        case ge:
          bc(kInstrSize*2, BF, encode_crbit(cr, CR_LT), LeaveLK);
          break;
        default:
          fake_asm(fBranch);
          // UNIMPLEMENTED();
      }
      b(b_offset - kInstrSize, lk);
    }
  }
  void bne(Label* L, CRegister cr = cr7, LKBit lk = LeaveLK) {
    b(ne, L, cr, lk); }
  void beq(Label* L, CRegister cr = cr7, LKBit lk = LeaveLK) {
    b(eq, L, cr, lk); }
  void blt(Label* L, CRegister cr = cr7, LKBit lk = LeaveLK) {
    b(lt, L, cr, lk); }
  void bge(Label* L, CRegister cr = cr7, LKBit lk = LeaveLK) {
    b(ge, L, cr, lk); }
  void ble(Label* L, CRegister cr = cr7, LKBit lk = LeaveLK) {
    b(le, L, cr, lk); }
  void bgt(Label* L, CRegister cr = cr7, LKBit lk = LeaveLK) {
    b(gt, L, cr, lk); }

  void bunordered(Label* L, CRegister cr = cr7, LKBit lk = LeaveLK) {
    ASSERT(cr.code() >= 0 && cr.code() <= 7);
    bc(branch_offset(L, false), BT, encode_crbit(cr, CR_FU), lk);
  }
  void bordered(Label* L, CRegister cr = cr7, LKBit lk = LeaveLK) {
    ASSERT(cr.code() >= 0 && cr.code() <= 7);
    bc(branch_offset(L, false), BF, encode_crbit(cr, CR_FU), lk);
  }
  void boverflow(Label* L, CRegister cr = cr1, LKBit lk = LeaveLK) {
    ASSERT(cr.code() >= 0 && cr.code() <= 7);
    bc(branch_offset(L, false), BT, encode_crbit(cr, CR_SO), lk);
  }
  void bnotoverflow(Label* L, CRegister cr = cr1, LKBit lk = LeaveLK) {
    ASSERT(cr.code() >= 0 && cr.code() <= 7);
    bc(branch_offset(L, false), BF, encode_crbit(cr, CR_SO), lk);
  }

  // Decrement CTR; branch if CTR != 0
  void bdnz(Label* L, LKBit lk = LeaveLK) {
    bc(branch_offset(L, false), DCBNZ, 0, lk);
  }

  // end PowerPC
  void bl(Label* L, Condition cond = al)  { bl(branch_offset(L, false), cond); }
  void bl(Condition cond, Label* L)  { bl(branch_offset(L, false), cond); }

  // Data-processing instructions

  // PowerPC
  void sub(Register dst, Register src1, Register src2,
           OEBit s = LeaveOE, RCBit r = LeaveRC);

  void subfic(Register dst, Register src, const Operand& imm);

  void subfc(Register dst, Register src1, Register src2,
           OEBit s = LeaveOE, RCBit r = LeaveRC);

  void add(Register dst, Register src1, Register src2,
           OEBit s = LeaveOE, RCBit r = LeaveRC);

  void addc(Register dst, Register src1, Register src2,
                    OEBit o = LeaveOE, RCBit r = LeaveRC);

  void addze(Register dst, Register src1, OEBit o, RCBit r);

  void mullw(Register dst, Register src1, Register src2,
               OEBit o = LeaveOE, RCBit r = LeaveRC);

  void mulhw(Register dst, Register src1, Register src2,
               OEBit o = LeaveOE, RCBit r = LeaveRC);

  void divw(Register dst, Register src1, Register src2,
            OEBit o = LeaveOE, RCBit r = LeaveRC);

  void addi(Register dst, Register src, const Operand& imm);
  void addis(Register dst, Register src, const Operand& imm);
  void addic(Register dst, Register src, const Operand& imm);

  void and_(Register dst, Register src1, Register src2, RCBit rc = LeaveRC);
  void andc(Register dst, Register src1, Register src2, RCBit rc = LeaveRC);
  void andi(Register ra, Register rs, const Operand& imm);
  void andis(Register ra, Register rs, const Operand& imm);
  void nor(Register dst, Register src1, Register src2, RCBit r = LeaveRC);
  void notx(Register dst, Register src, RCBit r = LeaveRC);
  void ori(Register dst, Register src, const Operand& imm);
  void oris(Register dst, Register src, const Operand& imm);
  void orx(Register dst, Register src1, Register src2, RCBit rc = LeaveRC);
  void xori(Register dst, Register src, const Operand& imm);
  void xoris(Register ra, Register rs, const Operand& imm);
  void xor_(Register dst, Register src1, Register src2, RCBit rc = LeaveRC);
  void cmpi(Register src1, const Operand& src2, CRegister cr = cr7);
  void cmpli(Register src1, const Operand& src2, CRegister cr = cr7);
  void li(Register dst, const Operand& src);
  void lis(Register dst, const Operand& imm);
  void mr(Register dst, Register src);

  void lbz(Register dst, const MemOperand& src);
  void lbzx(Register dst, const MemOperand& src);
  void lbzux(Register dst, const MemOperand& src);
  void lhz(Register dst, const MemOperand& src);
  void lhzx(Register dst, const MemOperand& src);
  void lhzux(Register dst, const MemOperand& src);
  void lwz(Register dst, const MemOperand& src);
  void lwzu(Register dst, const MemOperand& src);
  void lwzx(Register dst, const MemOperand& src);
  void lwzux(Register dst, const MemOperand& src);
  void stb(Register dst, const MemOperand& src);
  void stbx(Register dst, const MemOperand& src);
  void stbux(Register dst, const MemOperand& src);
  void sth(Register dst, const MemOperand& src);
  void sthx(Register dst, const MemOperand& src);
  void sthux(Register dst, const MemOperand& src);
  void stw(Register dst, const MemOperand& src);
  void stwu(Register dst, const MemOperand& src);
  void stwx(Register rs, const MemOperand& src);
  void stwux(Register rs, const MemOperand& src);

  void extsb(Register rs, Register ra, RCBit r = LeaveRC);
  void extsh(Register rs, Register ra, RCBit r = LeaveRC);

  void neg(Register rt, Register ra, OEBit o = LeaveOE, RCBit c = LeaveRC);

#if V8_TARGET_ARCH_PPC64
  void ld(Register rd, const MemOperand &src);
  void std(Register rs, const MemOperand &src);
  void stdu(Register rs, const MemOperand &src);
#endif

  void rlwinm(Register ra, Register rs, int sh, int mb, int me,
              RCBit rc = LeaveRC);
  void rlwimi(Register ra, Register rs, int sh, int mb, int me,
              RCBit rc = LeaveRC);
  void slwi(Register dst, Register src, const Operand& val, RCBit rc = LeaveRC);
  void srwi(Register dst, Register src, const Operand& val, RCBit rc = LeaveRC);
  void clrrwi(Register dst, Register src, const Operand& val,
              RCBit rc = LeaveRC);
  void clrlwi(Register dst, Register src, const Operand& val,
              RCBit rc = LeaveRC);
  void srawi(Register ra, Register rs, int sh, RCBit r = LeaveRC);
  void srw(Register dst, Register src1, Register src2, RCBit r = LeaveRC);
  void slw(Register dst, Register src1, Register src2, RCBit r = LeaveRC);
  void sraw(Register dst, Register src1, Register src2, RCBit r = LeaveRC);
  // 64bit PowerPC
  void rldicl(Register dst, Register src, int sh, int mb, RCBit r = LeaveRC);

  void cntlzw_(Register dst, Register src, RCBit rc = LeaveRC);
  // end PowerPC

  void eor(Register dst, Register src1, const Operand& src2,
           SBit s = LeaveCC, Condition cond = al);
  void sub(Register dst, Register src1, const Operand& src2);

  void rsb(Register dst, Register src1, const Operand& src2,
           SBit s = LeaveCC, Condition cond = al);

  void add(Register dst, Register src1, const Operand& src2,
           SBit s = LeaveCC, Condition cond = al);

  void adc(Register dst, Register src1, const Operand& src2,
           SBit s = LeaveCC, Condition cond = al);

  void sbc(Register dst, Register src1, const Operand& src2,
           SBit s = LeaveCC, Condition cond = al);

  void rsc(Register dst, Register src1, const Operand& src2,
           SBit s = LeaveCC, Condition cond = al);

  void teq(Register src1, const Operand& src2, Condition cond = al);

  void cmp(Register src1, const Operand& src2, Condition cond = al);
  void cmp(Register src1, Register src2, CRegister cr = cr7);
  void cmpl(Register src1, Register src2, CRegister cr = cr7);

  void cmn(Register src1, const Operand& src2, Condition cond = al);

  void mov(Register dst, const Operand& src,
           SBit s = LeaveCC, Condition cond = al);
  void bic(Register dst, Register src1, const Operand& src2,
           SBit s = LeaveCC, Condition cond = al);

  void mvn(Register dst, const Operand& src,
           SBit s = LeaveCC, Condition cond = al);

  // Multiply instructions

  // PowerPC
  void mul(Register dst, Register src1, Register src2,
           OEBit s = LeaveOE, RCBit r = LeaveRC);

  // Miscellaneous arithmetic instructions

  // Bitfield manipulation instructions. v7 and above.

  // Special register access
  // PowerPC
  void crxor(int bt, int ba, int bb);
  void mflr(Register dst);
  void mtlr(Register src);
  void mtctr(Register src);
  void mtxer(Register src);
  void mcrfs(int bf, int bfa);
  void mfcr(Register dst);

  void fake_asm(enum FAKE_OPCODE_T fopcode);
  void marker_asm(int mcode);
  void function_descriptor();
  // end PowerPC

  // Load/Store instructions
  void ldr(Register dst, const MemOperand& src, Condition cond = al);
  void str(Register src, const MemOperand& dst, Condition cond = al);
  void ldrb(Register dst, const MemOperand& src, Condition cond = al);
  void strb(Register src, const MemOperand& dst, Condition cond = al);
  void ldrh(Register dst, const MemOperand& src, Condition cond = al);
  void strh(Register src, const MemOperand& dst, Condition cond = al);
  void ldrsb(Register dst, const MemOperand& src, Condition cond = al);
  void ldrsh(Register dst, const MemOperand& src, Condition cond = al);
  void ldrd(Register dst1,
            Register dst2,
            const MemOperand& src, Condition cond = al);
  void strd(Register src1,
            Register src2,
            const MemOperand& dst, Condition cond = al);

  // Exception-generating instructions and debugging support
  void stop(const char* msg,
            Condition cond = al,
            int32_t code = kDefaultStopCode,
            CRegister cr = cr7);

  void bkpt(uint32_t imm16);  // v5 and above

  // Informational messages when simulating
  void info(const char* msg,
            Condition cond = al,
            int32_t code = kDefaultStopCode,
            CRegister cr = cr7);

  void svc(uint32_t imm24, Condition cond = al);

  void dcbf(Register ra, Register rb);
  void sync();
  void icbi(Register ra, Register rb);
  void isync();

  // Support for floating point
  void lfd(const DwVfpRegister frt, const MemOperand& src);
  void lfdu(const DwVfpRegister frt, const MemOperand& src);
  void lfdx(const DwVfpRegister frt, const MemOperand& src);
  void lfdux(const DwVfpRegister frt, const MemOperand& src);
  void lfs(const DwVfpRegister frt, const MemOperand& src);
  void lfsu(const DwVfpRegister frt, const MemOperand& src);
  void lfsx(const DwVfpRegister frt, const MemOperand& src);
  void lfsux(const DwVfpRegister frt, const MemOperand& src);
  void stfd(const DwVfpRegister frs, const MemOperand& src);
  void stfdu(const DwVfpRegister frs, const MemOperand& src);
  void stfdx(const DwVfpRegister frs, const MemOperand& src);
  void stfdux(const DwVfpRegister frs, const MemOperand& src);
  void stfs(const DwVfpRegister frs, const MemOperand& src);
  void stfsu(const DwVfpRegister frs, const MemOperand& src);
  void stfsx(const DwVfpRegister frs, const MemOperand& src);
  void stfsux(const DwVfpRegister frs, const MemOperand& src);

  void fadd(const DwVfpRegister frt, const DwVfpRegister fra,
            const DwVfpRegister frb, RCBit rc = LeaveRC);
  void fsub(const DwVfpRegister frt, const DwVfpRegister fra,
            const DwVfpRegister frb, RCBit rc = LeaveRC);
  void fdiv(const DwVfpRegister frt, const DwVfpRegister fra,
            const DwVfpRegister frb, RCBit rc = LeaveRC);
  void fmul(const DwVfpRegister frt, const DwVfpRegister fra,
            const DwVfpRegister frc, RCBit rc = LeaveRC);
  void fcmpu(const DwVfpRegister fra, const DwVfpRegister frb,
             CRegister cr = cr7);
  void fmr(const DwVfpRegister frt, const DwVfpRegister frb,
           RCBit rc = LeaveRC);
  void fctiwz(const DwVfpRegister frt, const DwVfpRegister frb);
  void fctiw(const DwVfpRegister frt, const DwVfpRegister frb);
  void frim(const DwVfpRegister frt, const DwVfpRegister frb);
  void frsp(const DwVfpRegister frt, const DwVfpRegister frb,
            RCBit rc = LeaveRC);
  void fcfid(const DwVfpRegister frt, const DwVfpRegister frb,
            RCBit rc = LeaveRC);
  void fctid(const DwVfpRegister frt, const DwVfpRegister frb,
            RCBit rc = LeaveRC);
  void fctidz(const DwVfpRegister frt, const DwVfpRegister frb,
            RCBit rc = LeaveRC);
  void fsel(const DwVfpRegister frt, const DwVfpRegister fra,
            const DwVfpRegister frc, const DwVfpRegister frb,
            RCBit rc = LeaveRC);
  void fneg(const DwVfpRegister frt, const DwVfpRegister frb,
            RCBit rc = LeaveRC);
  void mtfsfi(int bf, int immediate, RCBit rc = LeaveRC);
  void mffs(const DwVfpRegister frt, RCBit rc = LeaveRC);
  void mtfsf(const DwVfpRegister frb, bool L = 1, int FLM = 0, bool W = 0,
             RCBit rc = LeaveRC);
  void fsqrt(const DwVfpRegister frt, const DwVfpRegister frb,
             RCBit rc = LeaveRC);
  void fabs(const DwVfpRegister frt, const DwVfpRegister frb,
            RCBit rc = LeaveRC);

  // Support for VFP.
  // All these APIs support S0 to S31 and D0 to D15.
  // Currently these APIs do not support extended D registers, i.e, D16 to D31.
  // However, some simple modifications can allow
  // these APIs to support D16 to D31.

  void vldr(const DwVfpRegister dst,
            const Register base,
            int offset,
            const Condition cond = al);
  void vldr(const DwVfpRegister dst,
            const MemOperand& src,
            const Condition cond = al);
  void vstr(const DwVfpRegister src,
            const Register base,
            int offset,
            const Condition cond = al);
  void vstr(const DwVfpRegister src,
            const MemOperand& dst,
            const Condition cond = al);
  void vmov(const DwVfpRegister dst,
            double imm,
            const Register scratch = no_reg,
            const Condition cond = al);
  void vmov(const DwVfpRegister dst,
            const DwVfpRegister src,
            const Condition cond = al);
  void vmov(const DwVfpRegister dst,
            const Register src1,
            const Register src2,
            const Condition cond = al);
  void vmov(const Register dst1,
            const Register dst2,
            const DwVfpRegister src,
            const Condition cond = al);
  void vneg(const DwVfpRegister dst,
            const DwVfpRegister src,
            const Condition cond = al);
  void vabs(const DwVfpRegister dst,
            const DwVfpRegister src,
            const Condition cond = al);
  void vadd(const DwVfpRegister dst,
            const DwVfpRegister src1,
            const DwVfpRegister src2,
            const Condition cond = al);
  void vsub(const DwVfpRegister dst,
            const DwVfpRegister src1,
            const DwVfpRegister src2,
            const Condition cond = al);
  void vmul(const DwVfpRegister dst,
            const DwVfpRegister src1,
            const DwVfpRegister src2,
            const Condition cond = al);
  void vdiv(const DwVfpRegister dst,
            const DwVfpRegister src1,
            const DwVfpRegister src2,
            const Condition cond = al);
  void vcmp(const DwVfpRegister src1,
            const DwVfpRegister src2,
            const Condition cond = al);
  void vcmp(const DwVfpRegister src1,
            const double src2,
            const Condition cond = al);
  void vmrs(const Register dst,
            const Condition cond = al);
  void vmsr(const Register dst,
            const Condition cond = al);
  void vsqrt(const DwVfpRegister dst,
             const DwVfpRegister src,
             const Condition cond = al);

  // Pseudo instructions

  // Different nop operations are used by the code generator to detect certain
  // states of the generated code.
  enum NopMarkerTypes {
    NON_MARKING_NOP = 0,
    DEBUG_BREAK_NOP,
    // IC markers.
    PROPERTY_ACCESS_INLINED,
    PROPERTY_ACCESS_INLINED_CONTEXT,
    PROPERTY_ACCESS_INLINED_CONTEXT_DONT_DELETE,
    // Helper values.
    LAST_CODE_MARKER,
    FIRST_IC_MARKER = PROPERTY_ACCESS_INLINED
  };

  void nop(int type = 0);   // 0 is the default non-marking type.

  void push(Register src, Condition cond = al) {
#if V8_TARGET_ARCH_PPC64
    stdu(src, MemOperand(sp, -8));
#else
    stwu(src, MemOperand(sp, -4));
#endif
  }

  void pop(Register dst, Condition cond = al) {
#if V8_TARGET_ARCH_PPC64
    ld(dst, MemOperand(sp));
    addi(sp, sp, Operand(8));
#else
    lwz(dst, MemOperand(sp));
    addi(sp, sp, Operand(4));
#endif
  }

  void pop() {
    addi(sp, sp, Operand(kPointerSize));
  }

  // Jump unconditionally to given label.
  void jmp(Label* L) { b(L, al); }

  bool predictable_code_size() const { return predictable_code_size_; }

  // Check the code size generated from label to here.
  int SizeOfCodeGeneratedSince(Label* label) {
    return pc_offset() - label->pos();
  }

  // Check the number of instructions generated from label to here.
  int InstructionsGeneratedSince(Label* label) {
    return SizeOfCodeGeneratedSince(label) / kInstrSize;
  }

  // Debugging

  // Mark address of the ExitJSFrame code.
  void RecordJSReturn();

  // Mark address of a debug break slot.
  void RecordDebugBreakSlot();

  // Record the AST id of the CallIC being compiled, so that it can be placed
  // in the relocation information.
  void SetRecordedAstId(TypeFeedbackId ast_id) {
// PPC - this shouldn't be failing roohack   ASSERT(recorded_ast_id_.IsNone());
    recorded_ast_id_ = ast_id;
  }

  TypeFeedbackId RecordedAstId() {
    // roohack - another issue??? ASSERT(!recorded_ast_id_.IsNone());
    return recorded_ast_id_;
  }

  void ClearRecordedAstId() { recorded_ast_id_ = TypeFeedbackId::None(); }

  // Record a comment relocation entry that can be used by a disassembler.
  // Use --code-comments to enable.
  void RecordComment(const char* msg);

  // Writes a single byte or word of data in the code stream.  Used
  // for inline tables, e.g., jump-tables.
  void db(uint8_t data);
  void dd(uint32_t data);

  int pc_offset() const { return pc_ - buffer_; }

  PositionsRecorder* positions_recorder() { return &positions_recorder_; }

  // Read/patch instructions
  Instr instr_at(int pos) { return *reinterpret_cast<Instr*>(buffer_ + pos); }
  void instr_at_put(int pos, Instr instr) {
    *reinterpret_cast<Instr*>(buffer_ + pos) = instr;
  }
  static Instr instr_at(byte* pc) { return *reinterpret_cast<Instr*>(pc); }
  static void instr_at_put(byte* pc, Instr instr) {
    *reinterpret_cast<Instr*>(pc) = instr;
  }
  static Condition GetCondition(Instr instr);

  static bool IsLis(Instr instr);
  static bool IsAddic(Instr instr);

  static bool IsBranch(Instr instr);
  static Register GetRA(Instr instr);
  static Register GetRB(Instr instr);
  static bool Is32BitLoadIntoR12(Instr instr1, Instr instr2);
  static bool IsCmpRegister(Instr instr);
  static bool IsCmpImmediate(Instr instr);
  static bool IsRlwinm(Instr instr);
  static Register GetCmpImmediateRegister(Instr instr);
  static int GetCmpImmediateRawImmediate(Instr instr);
  static bool IsNop(Instr instr, int type = NON_MARKING_NOP);

 protected:
  // Relocation for a type-recording IC has the AST id added to it.  This
  // member variable is a way to pass the information from the call site to
  // the relocation info.
  TypeFeedbackId recorded_ast_id_;

  bool emit_debug_code() const { return emit_debug_code_; }

  int buffer_space() const { return reloc_info_writer.pos() - pc_; }

  // Decode branch instruction at pos and return branch target pos
  int target_at(int pos);

  // Patch branch instruction at pos to branch to given branch target pos
  void target_at_put(int pos, int target_pos);

 private:
  // Code buffer:
  // The buffer into which code and relocation info are generated.
  byte* buffer_;
  int buffer_size_;
  // True if the assembler owns the buffer, false if buffer is external.
  bool own_buffer_;

  // Code generation
  // The relocation writer's position is at least kGap bytes below the end of
  // the generated instructions. This is so that multi-instruction sequences do
  // not have to check for overflow. The same is true for writes of large
  // relocation info entries.
  static const int kGap = 32;
  byte* pc_;  // the program counter; moves forward

  // Relocation info generation
  // Each relocation is encoded as a variable size value
  static const int kMaxRelocSize = RelocInfoWriter::kMaxSize;
  RelocInfoWriter reloc_info_writer;

  // The bound position, before this we cannot do instruction elimination.
  int last_bound_pos_;

  // Code emission
  inline void CheckBuffer();
  void GrowBuffer();
  inline void emit(Instr x);

  // Instruction generation
  void a_form(Instr instr, DwVfpRegister frt, DwVfpRegister fra,
              DwVfpRegister frb, RCBit r);
  void d_form(Instr instr, Register rt, Register ra, const int val,
              bool signed_disp);
  void x_form(Instr instr, Register ra, Register rs, Register rb, RCBit r);
  void xo_form(Instr instr, Register rt, Register ra, Register rb,
               OEBit o, RCBit r);

  // Labels
  void print(Label* L);
  void bind_to(Label* L, int pos);
  void next(Label* L);

  // Say if we need to relocate with this mode.
  bool MustUseReg(RelocInfo::Mode rmode);

  // Record reloc info for current pc_
  void RecordRelocInfo(RelocInfo::Mode rmode, intptr_t data = 0);

  friend class RegExpMacroAssemblerPPC;
  friend class RelocInfo;
  friend class CodePatcher;

  PositionsRecorder positions_recorder_;

  bool emit_debug_code_;
  bool predictable_code_size_;

  friend class PositionsRecorder;
  friend class EnsureSpace;
};


class EnsureSpace BASE_EMBEDDED {
 public:
  explicit EnsureSpace(Assembler* assembler) {
    assembler->CheckBuffer();
  }
};

#undef INCLUDE_ARM
} }  // namespace v8::internal

#endif  // V8_PPC_ASSEMBLER_PPC_H_
