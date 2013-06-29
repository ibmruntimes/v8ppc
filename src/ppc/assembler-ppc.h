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
  static const int kNumAllocatableRegisters = 10;  // r3-r12
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
      "r10",
      "r11",
      "r12",  // currently last allocated register
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
const Register r1  = { kRegister_r18_Code };  // hack for ARM
const Register r2  = { kRegister_r2_Code };
const Register r3  = { kRegister_r3_Code };
const Register r4  = { kRegister_r4_Code };
const Register r5  = { kRegister_r5_Code };
const Register r6  = { kRegister_r6_Code };
const Register r7  = { kRegister_r7_Code };
const Register r8  = { kRegister_r8_Code };
const Register r9  = { kRegister_r9_Code };
// Used as context register.
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

// Single word VFP register.
struct SwVfpRegister {
  bool is_valid() const { return 0 <= code_ && code_ < 32; }
  bool is(SwVfpRegister reg) const { return code_ == reg.code_; }
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
    *m = code_ & 0x1;
    *vm = code_ >> 1;
  }

  int code_;
};


// Double word VFP register.
struct DwVfpRegister {
  static const int kNumRegisters = 16;
  // A few double registers are reserved: one as a scratch register and one to
  // hold 0.0, that does not fit in the immediate field of vmov instructions.
  //  d14: 0.0
  //  d15: scratch register.
  static const int kNumReservedRegisters = 2;
  static const int kNumAllocatableRegisters = kNumRegisters -
      kNumReservedRegisters;

  inline static int ToAllocationIndex(DwVfpRegister reg);

  static DwVfpRegister FromAllocationIndex(int index) {
    ASSERT(index >= 0 && index < kNumAllocatableRegisters);
    return from_code(index);
  }

  static const char* AllocationIndexToString(int index) {
    ASSERT(index >= 0 && index < kNumAllocatableRegisters);
    const char* const names[] = {
      "d0",
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
      "d13"
    };
    return names[index];
  }

  static DwVfpRegister from_code(int code) {
    DwVfpRegister r = { code };
    return r;
  }

  // Supporting d0 to d15, can be later extended to d31.
  bool is_valid() const { return 0 <= code_ && code_ < 16; }
  bool is(DwVfpRegister reg) const { return code_ == reg.code_; }
  SwVfpRegister low() const {
    SwVfpRegister reg;
    reg.code_ = code_ * 2;

    ASSERT(reg.is_valid());
    return reg;
  }
  SwVfpRegister high() const {
    SwVfpRegister reg;
    reg.code_ = (code_ * 2) + 1;

    ASSERT(reg.is_valid());
    return reg;
  }
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


// Support for the VFP registers s0 to s31 (d0 to d15).
// Note that "s(N):s(N+1)" is the same as "d(N/2)".
const SwVfpRegister s0  = {  0 };
const SwVfpRegister s1  = {  1 };
const SwVfpRegister s2  = {  2 };
const SwVfpRegister s3  = {  3 };
const SwVfpRegister s4  = {  4 };
const SwVfpRegister s5  = {  5 };
const SwVfpRegister s6  = {  6 };
const SwVfpRegister s7  = {  7 };
const SwVfpRegister s8  = {  8 };
const SwVfpRegister s9  = {  9 };
const SwVfpRegister s10 = { 10 };
const SwVfpRegister s11 = { 11 };
const SwVfpRegister s12 = { 12 };
const SwVfpRegister s13 = { 13 };
const SwVfpRegister s14 = { 14 };
const SwVfpRegister s15 = { 15 };
const SwVfpRegister s16 = { 16 };
const SwVfpRegister s17 = { 17 };
const SwVfpRegister s18 = { 18 };
const SwVfpRegister s19 = { 19 };
const SwVfpRegister s20 = { 20 };
const SwVfpRegister s21 = { 21 };
const SwVfpRegister s22 = { 22 };
const SwVfpRegister s23 = { 23 };
const SwVfpRegister s24 = { 24 };
const SwVfpRegister s25 = { 25 };
const SwVfpRegister s26 = { 26 };
const SwVfpRegister s27 = { 27 };
const SwVfpRegister s28 = { 28 };
const SwVfpRegister s29 = { 29 };
const SwVfpRegister s30 = { 30 };
const SwVfpRegister s31 = { 31 };

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

// Aliases for double registers.  Defined using #define instead of
// "static const DwVfpRegister&" because Clang complains otherwise when a
// compilation unit that includes this header doesn't use the variables.
#define kFirstCalleeSavedDoubleReg d8
#define kLastCalleeSavedDoubleReg d15
#define kDoubleRegZero d14
#define kScratchDoubleReg d15

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


// Coprocessor number
enum Coprocessor {
  p0  = 0,
  p1  = 1,
  p2  = 2,
  p3  = 3,
  p4  = 4,
  p5  = 5,
  p6  = 6,
  p7  = 7,
  p8  = 8,
  p9  = 9,
  p10 = 10,
  p11 = 11,
  p12 = 12,
  p13 = 13,
  p14 = 14,
  p15 = 15
};


// -----------------------------------------------------------------------------
// Machine instruction Operands

// Class Operand represents a shifter operand in data processing instructions
class Operand BASE_EMBEDDED {
 public:
  // immediate
  INLINE(explicit Operand(int32_t immediate,
         RelocInfo::Mode rmode = RelocInfo::NONE));
  INLINE(static Operand Zero()) {
    return Operand(static_cast<int32_t>(0));
  }
  INLINE(explicit Operand(const ExternalReference& f));
  explicit Operand(Handle<Object> handle);
  INLINE(explicit Operand(Smi* value));

  // rm
  INLINE(explicit Operand(Register rm));

  // rm <shift_op> shift_imm
  explicit Operand(Register rm, ShiftOp shift_op, int shift_imm);

  // rm <shift_op> rs
  explicit Operand(Register rm, ShiftOp shift_op, Register rs);

  // Return true if this is a register operand.
  INLINE(bool is_reg() const);

  // Return true if this operand fits in one instruction so that no
  // 2-instruction solution with a load into the ip register is necessary. If
  // the instruction this operand is used for is a MOV or MVN instruction the
  // actual instruction to use is required for this calculation. For other
  // instructions instr is ignored.
  bool is_single_instruction(const Assembler* assembler, Instr instr = 0) const;
  bool must_use_constant_pool(const Assembler* assembler) const;

  inline int32_t immediate() const {
    ASSERT(!rm_.is_valid());
    return imm32_;
  }

  Register rm() const { return rm_; }
  Register rs() const { return rs_; }
  ShiftOp shift_op() const { return shift_op_; }

 private:
  Register rm_;
  Register rs_;
  ShiftOp shift_op_;
  int shift_imm_;  // valid if rm_ != no_reg && rs_ == no_reg
  int32_t imm32_;  // valid if rm_ == no_reg
  RelocInfo::Mode rmode_;

  friend class Assembler;
};


// Class MemOperand represents a memory operand in load and store instructions
// On PowerPC we have base register + 16bit signed value
// Alternatively we can have a 16bit signed value immediate
class MemOperand BASE_EMBEDDED {
 public:
  // Contains cruft left to allow ARM to continue to work

  // PowerPC (remove AddrMode later)
  explicit MemOperand(Register rn, int32_t offset = 0, AddrMode am = Offset);

  // ARM only
  explicit MemOperand(Register rn, Register rm, AddrMode am = Offset);

  // ARM only
  explicit MemOperand(Register rn, Register rm,
                      ShiftOp shift_op, int shift_imm, AddrMode am = Offset);

  void set_offset(int32_t offset) {
      ASSERT(rm_.is(no_reg));
      offset_ = offset;
  }

  uint32_t offset() const {
      ASSERT(rm_.is(no_reg));
      return offset_;
  }

  // PowerPC - base register
  Register ra() const { return ra_; }

  // ARM stuff
  Register rn() const { return ra_; }
  Register rm() const { return rm_; }
  AddrMode am() const { return am_; }

  bool OffsetIsUint12Encodable() const {
    return offset_ >= 0 ? is_uint12(offset_) : is_uint12(-offset_);
  }

 private:
  Register ra_;  // base
  Register rm_;  // register offset
  int32_t offset_;  // valid if rm_ == no_reg
  ShiftOp shift_op_;
  int shift_imm_;  // valid if rm_ != no_reg && rs_ == no_reg
  AddrMode am_;  // bits P, U, and W

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
    if (f == VFP3 && !FLAG_enable_vfp3) return false;
    if (f == VFP2 && !FLAG_enable_vfp2) return false;
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
      // VFP2 and ARMv7 are implied by VFP3.
      if (f == VFP3) mask |= 1u << VFP2 | 1u << ARMv7;
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

  // This sets the branch destination (which is in the constant pool on ARM).
  // This is for calls and branches within generated code.
  inline static void deserialization_set_special_target_at(
      Address constant_pool_entry, Address target);

  // This sets the branch destination (which is in the constant pool on ARM).
  // This is for calls and branches to runtime code.
  inline static void set_external_target_at(Address constant_pool_entry,
                                            Address target);

  // Here we are patching the address in the constant pool, not the actual call
  // instruction.  The address in the constant pool is the same size as a
  // pointer.
  static const int kSpecialTargetSize = kPointerSize;

  // Size of an instruction.
  static const int kInstrSize = sizeof(Instr);

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

  static const int kJSReturnSequenceInstructions = 6;
  static const int kDebugBreakSlotInstructions = 5;
  static const int kDebugBreakSlotLength =
      kDebugBreakSlotInstructions * kInstrSize;

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
  void bc(int branch_offset, BOfield bo, int condition_bit);
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
  void bc(Label* L, BOfield bo, int bit)  {
    bc(branch_offset(L, false), bo, bit);
  }
  void b(Condition cond, Label* L, CRegister cr = cr7)  {
    ASSERT(cr.code() >= 0 && cr.code() <= 7);
    switch (cond) {
      case al:
        b(L);
        break;
      case eq:
        bc(branch_offset(L, false), BT, 2 + (cr.code() * 4));
        break;
      case ne:
        bc(branch_offset(L, false), BF, 2 + (cr.code() * 4));
        break;
      case gt:
        bc(branch_offset(L, false), BT, 1 + (cr.code() * 4));
        break;
      case le:
        bc(branch_offset(L, false), BF, 1 + (cr.code() * 4));
        break;
      case lt:
        bc(branch_offset(L, false), BT, 0 + (cr.code() * 4));
        break;
      case ge:
        bc(branch_offset(L, false), BF, 0 + (cr.code() * 4));
        break;
      default:
        fake_asm(fBranch);
        // UNIMPLEMENTED();
    }
  }
  void bne(Label* L, CRegister cr = cr7) {
    b(ne, L, cr); }
  void beq(Label* L, CRegister cr = cr7) {
    b(eq, L, cr); }
  void blt(Label* L, CRegister cr = cr7) {
    b(lt, L, cr); }
  void bge(Label* L, CRegister cr = cr7) {
    b(ge, L, cr); }
  void ble(Label* L, CRegister cr = cr7) {
    b(le, L, cr); }
  void bgt(Label* L, CRegister cr = cr7) {
    b(gt, L, cr); }

  void bunordered(Label* L, CRegister cr = cr7) {
    ASSERT(cr.code() >= 0 && cr.code() <= 7);
    bc(branch_offset(L, false), BT, 3 + (cr.code() * 4));
  }
  void bordered(Label* L, CRegister cr = cr7) {
    ASSERT(cr.code() >= 0 && cr.code() <= 7);
    bc(branch_offset(L, false), BF, 3 + (cr.code() * 4));
  }
  void boverflow(Label* L, CRegister cr = cr1) {
    ASSERT(cr.code() >= 0 && cr.code() <= 7);
    bc(branch_offset(L, false), BT, 3 + (cr.code() * 4));
  }
  void bnotoverflow(Label* L, CRegister cr = cr1) {
    ASSERT(cr.code() >= 0 && cr.code() <= 7);
    bc(branch_offset(L, false), BF, 3 + (cr.code() * 4));
  }

  // Decrement CTR; branch if CTR != 0
  void bdnz(Label* L) {
    bc(branch_offset(L, false), DCBNZ, 0);
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

  void addi(Register dst, Register src, const Operand& imm);
  void addis(Register dst, Register src, const Operand& imm);
  void addic(Register dst, Register src, const Operand& imm);

  void andc(Register dst, Register src1, Register src2, RCBit rc = LeaveRC);
  void andi(Register ra, Register rs, const Operand& imm);
  void andis(Register ra, Register rs, const Operand& imm);
  void nor(Register dst, Register src1, Register src2, RCBit r = LeaveRC);
  void notx(Register dst, Register src, RCBit r = LeaveRC);
  void ori(Register dst, Register src, const Operand& imm);
  void oris(Register dst, Register src, const Operand& imm);
  void orx(Register dst, Register src1, Register src2, RCBit r = LeaveRC);
  void cmpi(Register src1, const Operand& src2, CRegister cr = cr7);
  void cmpli(Register src1, const Operand& src2, CRegister cr = cr7);
  void li(Register dst, const Operand& src);
  void lis(Register dst, const Operand& imm);
  void mr(Register dst, Register src);

  void lbz(Register dst, const MemOperand& src);
  void lhz(Register dst, const MemOperand& src);
  void lwz(Register dst, const MemOperand& src);
  void lwzu(Register dst, const MemOperand& src);
  void lwzx(Register dst, Register ra, Register rb);
  void lwzux(Register dst, Register ra, Register rb);
  void stb(Register dst, const MemOperand& src);
  void sth(Register dst, const MemOperand& src);
  void stw(Register dst, const MemOperand& src);
  void stwu(Register dst, const MemOperand& src);
  void stwx(Register rs, Register ra, Register rb);
  void stwux(Register rs, Register ra, Register rb);

  void extsb(Register rs, Register ra, RCBit r = LeaveRC);
  void extsh(Register rs, Register ra, RCBit r = LeaveRC);

  void neg(Register rt, Register ra, RCBit rc = LeaveRC);

  void rlwinm(Register ra, Register rs, int sh, int mb, int me,
              RCBit rc = LeaveRC);
  void rlwimi(Register ra, Register rs, int sh, int mb, int me,
              RCBit rc = LeaveRC);
  void slwi(Register dst, Register src, const Operand& val);
  void srwi(Register dst, Register src, const Operand& val);
  void srawi(Register ra, Register rs, int sh, RCBit r = LeaveRC);
  void srw(Register dst, Register src1, Register src2, RCBit r = LeaveRC);
  void slw(Register dst, Register src1, Register src2, RCBit r = LeaveRC);
  void sraw(Register dst, Register src1, Register src2, RCBit r = LeaveRC);
  void and_(Register dst, Register src1, Register src2, RCBit rc = LeaveRC);

  void xori(Register dst, Register src, const Operand& imm);
  void xoris(Register ra, Register rs, const Operand& imm);
  void xor_(Register dst, Register src1, Register src2, RCBit rc = LeaveRC);

  void cntlzw_(Register dst, Register src, RCBit rc = LeaveRC);
  // end PowerPC

  void and_(Register dst, Register src1, const Operand& src2,
            SBit s = LeaveCC, Condition cond = al);

  void eor(Register dst, Register src1, const Operand& src2,
           SBit s = LeaveCC, Condition cond = al);

  void sub(Register dst, Register src1, const Operand& src2,
           SBit s = LeaveCC, Condition cond = al);
  void sub(Register dst, Register src1, Register src2,
           SBit s, Condition cond = al) {
    sub(dst, src1, Operand(src2), s, cond);
  }

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

  void tst(Register src1, const Operand& src2, Condition cond = al);
  void tst(Register src1, Register src2, Condition cond = al) {
    tst(src1, Operand(src2), cond);
  }

  void teq(Register src1, const Operand& src2, Condition cond = al);

  void cmp(Register src1, const Operand& src2, Condition cond = al);
  void cmp(Register src1, Register src2, CRegister cr = cr7);
  void cmpl(Register src1, Register src2, CRegister cr = cr7);

  void cmn(Register src1, const Operand& src2, Condition cond = al);

  void orr(Register dst, Register src1, const Operand& src2,
           SBit s = LeaveCC, Condition cond = al);
  void orr(Register dst, Register src1, Register src2,
           SBit s = LeaveCC, Condition cond = al) {
    orr(dst, src1, Operand(src2), s, cond);
  }

  void mov(Register dst, const Operand& src,
           SBit s = LeaveCC, Condition cond = al);
  void mov(Register dst, Register src, SBit s = LeaveCC, Condition cond = al) {
    mov(dst, Operand(src), s, cond);
  }

  void bic(Register dst, Register src1, const Operand& src2,
           SBit s = LeaveCC, Condition cond = al);

  void mvn(Register dst, const Operand& src,
           SBit s = LeaveCC, Condition cond = al);

  // Multiply instructions

  void mla(Register dst, Register src1, Register src2, Register srcA,
           SBit s = LeaveCC, Condition cond = al);

  // PowerPC
  void mul(Register dst, Register src1, Register src2,
           OEBit s = LeaveOE, RCBit r = LeaveRC);

  void smlal(Register dstL, Register dstH, Register src1, Register src2,
             SBit s = LeaveCC, Condition cond = al);

  void smull(Register dstL, Register dstH, Register src1, Register src2,
             SBit s = LeaveCC, Condition cond = al);

  void umlal(Register dstL, Register dstH, Register src1, Register src2,
             SBit s = LeaveCC, Condition cond = al);

  void umull(Register dstL, Register dstH, Register src1, Register src2,
             SBit s = LeaveCC, Condition cond = al);

  // Miscellaneous arithmetic instructions

  // Saturating instructions. v6 and above.

  // Unsigned saturate.
  //
  // Saturate an optionally shifted signed value to an unsigned range.
  //
  //   usat dst, #satpos, src
  //   usat dst, #satpos, src, lsl #sh
  //   usat dst, #satpos, src, asr #sh
  //
  // Register dst will contain:
  //
  //   0,                 if s < 0
  //   (1 << satpos) - 1, if s > ((1 << satpos) - 1)
  //   s,                 otherwise
  //
  // where s is the contents of src after shifting (if used.)
  void usat(Register dst, int satpos, const Operand& src, Condition cond = al);

  // Bitfield manipulation instructions. v7 and above.

  void ubfx(Register dst, Register src, int lsb, int width,
            Condition cond = al);

  // Special register access
  // PowerPC
  void crxor(int bt, int ba, int bb);
  void mflr(Register dst);
  void mtlr(Register src);
  void mtctr(Register src);
  void mcrfs(int bf, int bfa);

  void fake_asm(enum FAKE_OPCODE_T fopcode);
  void marker_asm(int mcode);
  // end PowerPC
  // Status register access instructions

  void mrs(Register dst, SRegister s, Condition cond = al);
  void msr(SRegisterFieldMask fields, const Operand& src, Condition cond = al);

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

  // Load/Store multiple instructions
  void ldm(BlockAddrMode am, Register base, RegList dst, Condition cond = al);
  void stm(BlockAddrMode am, Register base, RegList src, Condition cond = al);

  // Exception-generating instructions and debugging support
  void stop(const char* msg,
            Condition cond = al,
            int32_t code = kDefaultStopCode);

  void bkpt(uint32_t imm16);  // v5 and above
  void svc(uint32_t imm24, Condition cond = al);

  // Support for floating point
  void lfd(const DwVfpRegister frt, const MemOperand& src);
  void lfs(const DwVfpRegister frt, const MemOperand& src);
  void stfd(const DwVfpRegister frs, const MemOperand& src);
  void stfs(const DwVfpRegister frs, const MemOperand& src);
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
  void fmr(const DwVfpRegister frt, const DwVfpRegister frb);
  void fctiwz(const DwVfpRegister frt, const DwVfpRegister frb);
  void frim(const DwVfpRegister frt, const DwVfpRegister frb);
  void frsp(const DwVfpRegister frt, const DwVfpRegister frb,
            RCBit rc = LeaveRC);
  void fcfid(const DwVfpRegister frt, const DwVfpRegister frb,
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

  void vldr(const SwVfpRegister dst,
            const Register base,
            int offset,
            const Condition cond = al);
  void vldr(const SwVfpRegister dst,
            const MemOperand& src,
            const Condition cond = al);

  void vstr(const DwVfpRegister src,
            const Register base,
            int offset,
            const Condition cond = al);
  void vstr(const DwVfpRegister src,
            const MemOperand& dst,
            const Condition cond = al);

  void vstr(const SwVfpRegister src,
            const Register base,
            int offset,
            const Condition cond = al);
  void vstr(const SwVfpRegister src,
            const MemOperand& dst,
            const Condition cond = al);

  void vmov(const DwVfpRegister dst,
            double imm,
            const Register scratch = no_reg,
            const Condition cond = al);
  void vmov(const SwVfpRegister dst,
            const SwVfpRegister src,
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
  void vmov(const SwVfpRegister dst,
            const Register src,
            const Condition cond = al);
  void vmov(const Register dst,
            const SwVfpRegister src,
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
    stwu(src, MemOperand(sp, -4));
  }

  void pop(Register dst, Condition cond = al) {
    lwz(dst, MemOperand(sp));
    addi(sp, sp, Operand(4));
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

  // Class for scoping postponing the constant pool generation.
  class BlockConstPoolScope {
   public:
    explicit BlockConstPoolScope(Assembler* assem) : assem_(assem) {
      assem_->StartBlockConstPool();
    }
    ~BlockConstPoolScope() {
      assem_->EndBlockConstPool();
    }

   private:
    Assembler* assem_;

    DISALLOW_IMPLICIT_CONSTRUCTORS(BlockConstPoolScope);
  };

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

  // Record the emission of a constant pool.
  //
  // The emission of constant pool depends on the size of the code generated and
  // the number of RelocInfo recorded.
  // The Debug mechanism needs to map code offsets between two versions of a
  // function, compiled with and without debugger support (see for example
  // Debug::PrepareForBreakPoints()).
  // Compiling functions with debugger support generates additional code
  // (Debug::GenerateSlot()). This may affect the emission of the constant
  // pools and cause the version of the code with debugger support to have
  // constant pools generated in different places.
  // Recording the position and size of emitted constant pools allows to
  // correctly compute the offset mappings between the different versions of a
  // function in all situations.
  //
  // The parameter indicates the size of the constant pool (in bytes), including
  // the marker and branch over the data.
  void RecordConstPool(int size);

  // Writes a single byte or word of data in the code stream.  Used
  // for inline tables, e.g., jump-tables. The constant pool should be
  // emitted before any use of db and dd to ensure that constant pools
  // are not emitted as part of the tables generated.
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
  static bool IsPush(Instr instr);
  static bool IsPop(Instr instr);
  static bool IsLdrPcImmediateOffset(Instr instr);
  static bool IsCmpRegister(Instr instr);
  static bool IsCmpImmediate(Instr instr);
  static bool IsRlwinm(Instr instr);
  static Register GetCmpImmediateRegister(Instr instr);
  static int GetCmpImmediateRawImmediate(Instr instr);
  static bool IsNop(Instr instr, int type = NON_MARKING_NOP);

  // Constants in pools are accessed via pc relative addressing, which can
  // reach +/-4KB thereby defining a maximum distance between the instruction
  // and the accessed constant.
  static const int kMaxDistToPool = 4*KB;
  static const int kMaxNumPendingRelocInfo = kMaxDistToPool/kInstrSize;

  // Postpone the generation of the constant pool for the specified number of
  // instructions.
  void BlockConstPoolFor(int instructions);

  // Check if is time to emit a constant pool.
  void CheckConstPool(bool force_emit, bool require_jump);

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

  // Prevent contant pool emission until EndBlockConstPool is called.
  // Call to this function can be nested but must be followed by an equal
  // number of call to EndBlockConstpool.
  void StartBlockConstPool() {
    if (const_pool_blocked_nesting_++ == 0) {
      // Prevent constant pool checks happening by setting the next check to
      // the biggest possible offset.
      next_buffer_check_ = kMaxInt;
    }
  }

  // Resume constant pool emission. Need to be called as many time as
  // StartBlockConstPool to have an effect.
  void EndBlockConstPool() {
    if (--const_pool_blocked_nesting_ == 0) {
      // Check the constant pool hasn't been blocked for too long.
      ASSERT((num_pending_reloc_info_ == 0) ||
             (pc_offset() < (first_const_pool_use_ + kMaxDistToPool)));
      // Two cases:
      //  * no_const_pool_before_ >= next_buffer_check_ and the emission is
      //    still blocked
      //  * no_const_pool_before_ < next_buffer_check_ and the next emit will
      //    trigger a check.
      next_buffer_check_ = no_const_pool_before_;
    }
  }

  bool is_const_pool_blocked() const {
    return (const_pool_blocked_nesting_ > 0) ||
           (pc_offset() < no_const_pool_before_);
  }

 private:
  // Code buffer:
  // The buffer into which code and relocation info are generated.
  byte* buffer_;
  int buffer_size_;
  // True if the assembler owns the buffer, false if buffer is external.
  bool own_buffer_;

  int next_buffer_check_;  // pc offset of next buffer check

  // Code generation
  // The relocation writer's position is at least kGap bytes below the end of
  // the generated instructions. This is so that multi-instruction sequences do
  // not have to check for overflow. The same is true for writes of large
  // relocation info entries.
  static const int kGap = 32;
  byte* pc_;  // the program counter; moves forward

  // Constant pool generation
  // Pools are emitted in the instruction stream, preferably after unconditional
  // jumps or after returns from functions (in dead code locations).
  // If a long code sequence does not contain unconditional jumps, it is
  // necessary to emit the constant pool before the pool gets too far from the
  // location it is accessed from. In this case, we emit a jump over the emitted
  // constant pool.
  // Constants in the pool may be addresses of functions that gets relocated;
  // if so, a relocation info entry is associated to the constant pool entry.

  // Repeated checking whether the constant pool should be emitted is rather
  // expensive. By default we only check again once a number of instructions
  // has been generated. That also means that the sizing of the buffers is not
  // an exact science, and that we rely on some slop to not overrun buffers.
  static const int kCheckPoolIntervalInst = 32;
  static const int kCheckPoolInterval = kCheckPoolIntervalInst * kInstrSize;


  // Average distance beetween a constant pool and the first instruction
  // accessing the constant pool. Longer distance should result in less I-cache
  // pollution.
  // In practice the distance will be smaller since constant pool emission is
  // forced after function return and sometimes after unconditional branches.
  static const int kAvgDistToPool = kMaxDistToPool - kCheckPoolInterval;

  // Emission of the constant pool may be blocked in some code sequences.
  int const_pool_blocked_nesting_;  // Block emission if this is not zero.
  int no_const_pool_before_;  // Block emission before this pc offset.

  // Keep track of the first instruction requiring a constant pool entry
  // since the previous constant pool was emitted.
  int first_const_pool_use_;

  // Relocation info generation
  // Each relocation is encoded as a variable size value
  static const int kMaxRelocSize = RelocInfoWriter::kMaxSize;
  RelocInfoWriter reloc_info_writer;

  // Relocation info records are also used during code generation as temporary
  // containers for constants and code target addresses until they are emitted
  // to the constant pool. These pending relocation info records are temporarily
  // stored in a separate buffer until a constant pool is emitted.
  // If every instruction in a long sequence is accessing the pool, we need one
  // pending relocation entry per instruction.

  // the buffer of pending relocation info
  RelocInfo pending_reloc_info_[kMaxNumPendingRelocInfo];
  // number of pending reloc info entries in the buffer
  int num_pending_reloc_info_;

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
  void link_to(Label* L, Label* appendix);
  void next(Label* L);

  // Record reloc info for current pc_
  void RecordRelocInfo(RelocInfo::Mode rmode, intptr_t data = 0);

  friend class RegExpMacroAssemblerPPC;
  friend class RelocInfo;
  friend class CodePatcher;
  friend class BlockConstPoolScope;

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
