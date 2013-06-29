// Copyright 2011 the V8 project authors. All rights reserved.
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

#ifndef V8_PPC_CONSTANTS_PPC_H_
#define V8_PPC_CONSTANTS_PPC_H_

#define INCLUDE_ARM 1

namespace v8 {
namespace internal {

// Number of registers
const int kNumRegisters = 32;

// FP support.
const int kNumFPSingleRegisters = 32;
const int kNumFPDoubleRegisters = 16;
const int kNumFPRegisters = kNumFPSingleRegisters + kNumFPDoubleRegisters;

#if defined(INCLUDE_ARM)
// Constant pool marker.
const int kConstantPoolMarkerMask = 0xffe00000;
const int kConstantPoolMarker = 0x0c000000;
const int kConstantPoolLengthMask = 0x001ffff;
#endif  // INCLUDE_ARM

// PPC doesn't really have a PC register - assign a fake number for simulation
const int kPCRegister = -2;
const int kNoRegister = -1;

// sign-extend the least significant 16-bit of value <imm>
#define SIGN_EXT_IMM16(imm) ((static_cast<int>(imm) << 16) >> 16)

// -----------------------------------------------------------------------------
// Conditions.

// Defines constants and accessor classes to assemble, disassemble and
// simulate ARM instructions.
//
// Section references in the code refer to the "PowerPC Microprocessor
// Family: The Programmer.s Reference Guide" from 10/95
// https://www-01.ibm.com/chips/techlib/techlib.nsf/techdocs/852569B20050FF778525699600741775/$file/prg.pdf
//
#if defined(INCLUDE_ARM)
// Constants for specific fields are defined in their respective named enums.
// General constants are in an anonymous enum in class Instr.

// Values for the condition field as defined in section A3.2
enum Condition {
  kNoCondition = -1,

  eq =  0 << 28,                 // Z set            Equal.
  ne =  1 << 28,                 // Z clear          Not equal.
  cs =  2 << 28,                 // C set            Unsigned higher or same.
  cc =  3 << 28,                 // C clear          Unsigned lower.
  mi =  4 << 28,                 // N set            Negative.
  pl =  5 << 28,                 // N clear          Positive or zero.
  vs =  6 << 28,                 // V set            Overflow.
  vc =  7 << 28,                 // V clear          No overflow.
  hi =  8 << 28,                 // C set, Z clear   Unsigned higher.
  ls =  9 << 28,                 // C clear or Z set Unsigned lower or same.
  ge = 10 << 28,                 // N == V           Greater or equal.
  lt = 11 << 28,                 // N != V           Less than.
  gt = 12 << 28,                 // Z clear, N == V  Greater than.
  le = 13 << 28,                 // Z set or N != V  Less then or equal
  al = 14 << 28,                 //                  Always.

  kSpecialCondition = 15 << 28,  // Special condition (refer to section A3.2.1).
  kNumberOfConditions = 16,

  // Aliases.
  hs = cs,                       // C set            Unsigned higher or same.
  lo = cc                        // C clear          Unsigned lower.
};


inline Condition NegateCondition(Condition cond) {
  ASSERT(cond != al);
  return static_cast<Condition>(cond ^ ne);
}


// Corresponds to transposing the operands of a comparison.
inline Condition ReverseCondition(Condition cond) {
  switch (cond) {
    case lo:
      return hi;
    case hi:
      return lo;
    case hs:
      return ls;
    case ls:
      return hs;
    case lt:
      return gt;
    case gt:
      return lt;
    case ge:
      return le;
    case le:
      return ge;
    default:
      return cond;
  };
}
#endif  // INCLUDE_ARM

// -----------------------------------------------------------------------------
// Instructions encoding.

// Instr is merely used by the Assembler to distinguish 32bit integers
// representing instructions from usual 32 bit values.
// Instruction objects are pointers to 32bit values, and provide methods to
// access the various ISA fields.
typedef int32_t Instr;

// Opcodes as defined in section 4.2 table 34 (32bit PowerPC)
enum Opcode {
  TWI     =  3 << 26,  // Trap Word Immediate
  MULLI   =  7 << 26,  // Multiply Low Immediate
  SUBFIC  =  8 << 26,  // Subtract from Immediate Carrying
  CMPLI   = 10 << 26,  // Compare Logical Immediate
  CMPI    = 11 << 26,  // Compare Immediate
  ADDIC   = 12 << 26,  // Add Immediate Carrying
  ADDICx  = 13 << 26,  // Add Immediate Carrying and Record
  ADDI    = 14 << 26,  // Add Immediate
  ADDIS   = 15 << 26,  // Add Immediate Shifted
  BCX     = 16 << 26,  // Branch Conditional
  SC      = 17 << 26,  // System Call
  BX      = 18 << 26,  // Branch
  EXT1    = 19 << 26,  // Extended code set 1
  RLWIMIX = 20 << 26,  // Rotate Left Word Immediate then Mask Insert
  RLWINMX = 21 << 26,  // Rotate Left Word Immediate then AND with Mask
  RLWNMX  = 23 << 26,  // Rotate Left then AND with Mask
  ORI     = 24 << 26,  // OR Immediate
  ORIS    = 25 << 26,  // OR Immediate Shifted
  XORI    = 26 << 26,  // XOR Immediate
  XORIS   = 27 << 26,  // XOR Immediate Shifted
  ANDIx   = 28 << 26,  // AND Immediate
  ANDISx  = 29 << 26,  // AND Immediate Shifted
  EXT2    = 31 << 26,  // Extended code set 2
  LWZ     = 32 << 26,  // Load Word and Zero
  LWZU    = 33 << 26,  // Load Word with Zero Update
  LBZ     = 34 << 26,  // Load Byte and Zero
  LBZU    = 35 << 26,  // Load Byte and Zero with Update
  STW     = 36 << 26,  // Store
  STWU    = 37 << 26,  // Store Word with Update
  STB     = 38 << 26,  // Store Byte
  STBU    = 39 << 26,  // Store Byte with Update
  LHZ     = 40 << 26,  // Load Half and Zero
  LHZU    = 41 << 26,  // Load Half and Zero with Update
  LHA     = 42 << 26,  // Load Half Algebraic
  LHAU    = 43 << 26,  // Load Half Algebraic with Update
  STH     = 44 << 26,  // Store Half
  STHU    = 45 << 26,  // Store Half with Update
  LMW     = 46 << 26,  // Load Multiple Word
  STMW    = 47 << 26,  // Store Multiple Word
  LFS     = 48 << 26,  // Load Floating-Point Single
  LFSU    = 49 << 26,  // Load Floating-Point Single with Update
  LFD     = 50 << 26,  // Load Floating-Point Double
  LFDU    = 51 << 26,  // Load Floating-Point Double with Update
  STFS    = 52 << 26,  // Store Floating-Point Single
  STFSU   = 53 << 26,  // Store Floating-Point Single with Update
  STFD    = 54 << 26,  // Store Floating-Point Double
  STFDU   = 55 << 26,  // Store Floating-Point Double with Update
  EXT3    = 59 << 26,  // Extended code set 3
  EXT4    = 63 << 26   // Extended code set 4
};

// Bits 10-1
enum OpcodeExt1 {
  MCRF   = 0 << 1,    // Move Condition Register Field
  BCLRX  = 16 << 1,   // Branch Conditional Link Register
  CRNOR  = 33 << 1,   // Condition Register NOR)
  RFI    = 50 << 1,   // Return from Interrupt
  CRANDC = 129 << 1,  // Condition Register AND with Complement
  ISYNC  = 150 << 1,  // Instruction Synchronize
  CRXOR  = 193 << 1,  // Condition Register XOR
  CRNAND = 225 << 1,  // Condition Register NAND
  CRAND  = 257 << 1,  // Condition Register AND
  CREQV  = 289 << 1,  // Condition Register Equivalent
  CRORC  = 417 << 1,  // Condition Register OR with Complement
  CROR   = 449 << 1,  // Condition Register OR
  BCCTRX = 528 << 1   // Branch Conditional to Count Register
};

// Bits 9-1 or 10-1
enum OpcodeExt2 {
  CMP = 0 << 1,
  TW = 4 << 1,
  SUBFCX = 8 << 1,
  ADDCX = 10 << 1,
  MULHWUX = 11 << 1,
  MFCR = 19 << 1,
  LWARX = 20 << 1,
  LDX = 21 << 1,
  LWZX = 23 << 1,
  SLWX = 24 << 1,
  CNTLZWX = 26 << 1,
  ANDX = 28 << 1,
  CMPL = 32 << 1,
  SUBFX = 40 << 1,
  DCBST = 54 << 1,
  LWZUX = 55 << 1,
  ANDCX = 60 << 1,
  MULHWX = 75 << 1,
  DCBF = 86 << 1,
  LBZX = 87 << 1,
  NEGX = 104 << 1,
  LBZUX = 119 << 1,
  NORX = 124 << 1,
  SUBFEX = 136 << 1,
  ADDEX = 138 << 1,
  STWX = 151 << 1,
  STWUX = 183 << 1,
/*
  MTCRF
  MTMSR
  STDX
  STWCXx
  STDUX
  SUBFZEX
*/
  ADDZEX = 202 << 1,  // Add to Zero Extended
/*
  MTSR
*/
  MULLW  = 235 << 1,  // Multiply Low Word
  ADDX = 266 << 1,  // Add
  XORX = 316 << 1,  // Exclusive OR
  MFSPR = 339 <<1,  // Move from Special-Purpose-Register
  ORX = 444 << 1,  // Or
  MTSPR = 467 <<1,  // Move to Special-Purpose-Register

  // Below represent bits 10-1  (any value >= 512)
  SRWX = 536 <<1,  // Shift Right Word
  SRAW = 792 << 1,  // Shift Right Algebraic Word
  SRAWIX = 824 << 1,  // Shift Right Algebraic Word Immediate
  EXTSH = 922 << 1,  // Extend Sign Halfword
  EXTSB = 954 << 1  // Extend Sign Byte
};

// Some use Bits 10-1 and other only 5-1 for the opcode
enum OpcodeExt4 {
  // Bits 5-1
  FDIV = 18 << 1,    // Floating Divide
  FSUB = 20 << 1,    // Floating Subtract
  FADD = 21 << 1,    // Floating Add
  FMUL = 25 << 1,    // Floating Multiply

  // Bits 10-1
  FCMPU = 0 << 1,    // Floating Compare Unordered
  FRSP = 12 << 1,    // Floating-Point Rounding
  FCTIWZ = 15 << 1,  // Floating Convert to Integer Word with Round to Zero
  MCRFS = 64 << 1,   // Move to Condition Register from FPSCR
  FMR = 72 << 1,     // Floating Move Register
  FRIM = 488 << 1,   // Floating Round to Integer Minus
  FCFID = 846 << 1   // Floating convert from integer doubleword
};

// Instruction encoding bits and masks.
enum {
  B10 = 1 << 10,
  B11 = 1 << 11,
  B16 = 1 << 16,
  B21 = 1 << 21,

  kOpcodeMask = 0x3f << 26,
  kExt2OpcodeMask = 0x1f << 1,
  kBOMask = 0x1f << 21,
  kBIMask = 0x1F << 16,
  kBDMask = 0x14 << 2,
  kAAMask = 0x01 << 1,
  kLKMask = 0x01,
  kRCMask = 0x01,
  kTOMask = 0x1f << 21,


#if defined(INCLUDE_ARM)
// Instruction encoding bits and masks.
  H   = 1 << 5,   // Halfword (or byte).
  S6  = 1 << 6,   // Signed (or unsigned).
  L   = 1 << 20,  // Load (or store).
  S   = 1 << 20,  // Set condition code (or leave unchanged).
  W   = 1 << 21,  // Writeback base register (or leave unchanged).
  A   = 1 << 21,  // Accumulate in multiply instruction (or not).
  B   = 1 << 22,  // Unsigned byte (or word).
  N   = 1 << 22,  // Long (or short).
  U   = 1 << 23,  // Positive (or negative) offset/index.
  P   = 1 << 24,  // Offset/pre-indexed addressing (or post-indexed addressing).
  I   = 1 << 25,  // Immediate shifter operand (or not).

  B4  = 1 << 4,
  B5  = 1 << 5,
  B6  = 1 << 6,
  B7  = 1 << 7,
  B8  = 1 << 8,
  B9  = 1 << 9,
  B12 = 1 << 12,
  B18 = 1 << 18,
  B19 = 1 << 19,
  B20 = 1 << 20,
  B22 = 1 << 22,
  B23 = 1 << 23,
  B24 = 1 << 24,
  B25 = 1 << 25,
  B26 = 1 << 26,
  B27 = 1 << 27,
  B28 = 1 << 28,

  // Instruction bit masks.
  kCondMask   = 0x1F << 21,  // changed for PowerPC
  kALUMask    = 0x6f << 21,
  kRdMask     = 15 << 12,  // In str instruction.
  kCoprocessorMask = 15 << 8,
  kOpCodeMask = 15 << 21,  // In data-processing instructions.
  kOff12Mask  = (1 << 12) - 1,
  kImm24Mask  = (1 << 24) - 1,
#endif  // INCLUDE_ARM
  kOff16Mask  = (1 << 16) - 1,
  kImm16Mask  = (1 << 16) - 1,
  kImm26Mask  = (1 << 26) - 1,
  kBOfieldMask = 0x1f << 20
};

// the following is to differentiate different faked ARM opcodes for
// the BOGUS PPC instruction we invented (when bit 25 is 0) or to mark
// different stub code (when bit 25 is 1)
//   - use primary opcode 1 for undefined instruction
//   - use bit 25 to indicate whether the opcode is for fake-arm
//     instr or stub-marker
//   - use the least significant 6-bit to indicate FAKE_OPCODE_T or
//     MARKER_T
#define FAKE_OPCODE 1 << 26
#define MARKER_SUBOPCODE_BIT 25
#define MARKER_SUBOPCODE 1 << MARKER_SUBOPCODE_BIT
#define FAKER_SUBOPCODE 0 << MARKER_SUBOPCODE_BIT

enum FAKE_OPCODE_T {
  fMRS = 0,
  fMSR = 1,
  fLDR = 2,
  fSTR = 3,
  fLDRB = 4,
  fSTRB = 5,
  fLDRH = 6,
  fSTRH = 7,
  fLDRSH = 8,
  fLDRD = 9,
  fSTRD = 10,
  fLDM = 11,
  fSTM = 12,
  // fSTOP = 13,
  fBKPT = 14,
  fSVC = 15,
  fVLDR = 16,
  fVSTR = 17,
  fVMOV = 18,
  fVNEG = 19,
  fVABS = 20,
  fVADD = 21,
  fVSUB = 22,
  fVMUL = 23,
  fVDIV = 24,
  fVCMP = 25,
  fVMSR = 26,
  fVMRS = 27,
  fVSQRT = 28,
  fAND = 29,
  fEOR = 30,
  fRSB = 31,
  fADC = 32,
  fSBC = 33,
  fRSC = 34,
  fTST = 35,
  fTEQ = 36,
  fCMP = 37,
  fCMN = 38,
  fORR = 39,
  fBIC = 40,
  fMVN = 41,
  fLDRSB = 42,
  fADD = 43,
  fBranch = 44,
  // the following is the marker for ARM instruction sequences outside
  // assembler.cc that we have removed (marked by PPCPORT_UNIMPLEMENTED)
  fMASM1 = 60,
  fMASM3 = 61,
  fMASM4 = 62,
  fMASM5 = 63,
  fMASM6 = 64,
  fMASM7 = 65,
  fMASM8 = 66,
  fMASM12 = 67,
  fMASM13 = 68,
  fMASM16 = 69,
  fMASM17 = 70,
  fMASM18 = 71,
  fMASM19 = 72,
  fMASM20 = 73,
  fMASM21 = 74,
  fMASM22 = 75,
  fMASM23 = 76,
  fMASM24 = 77,
  fMASM25 = 78,
  fMASM26 = 79,
  fMASM27 = 80,
  fMASM28 = 81,
  fMASM29 = 82,
  fLastFaker  // can't be more than 128 (2^^7)
};
#define FAKE_OPCODE_HIGH_BIT 7  // fake opcode has to fall into bit 0~7
#define F_NEXT_AVAILABLE_STUB_MARKER 369  // must be less than 2^^9 (512)
#define STUB_MARKER_HIGH_BIT 9  // stub marker has to fall into bit 0~9
// -----------------------------------------------------------------------------
// Addressing modes and instruction variants.

// Overflow Exception
enum OEBit {
  SetOE   = 1 << 10,  // Set overflow exception
  LeaveOE = 0 << 10   // No overflow exception
};

// Record bit
enum RCBit {  // Bit 0
  SetRC   = 1,  // LT,GT,EQ,SO
  LeaveRC = 0   // None
};

// Link bit
enum LKBit {  // Bit 0
  SetLK   = 1,  // Load effective address of next instruction
  LeaveLK = 0   // No action
};

enum BOfield {  // Bits 25-21
  DCBNZF =  0 << 21,  // Decrement CTR; branch if CTR != 0 and condition false
  DCBEZF =  2 << 21,  // Decrement CTR; branch if CTR == 0 and condition false
  BF     =  4 << 21,  // Branch if condition false
  DCBNZT =  8 << 21,  // Decrement CTR; branch if CTR != 0 and condition true
  DCBEZT = 10 << 21,  // Decrement CTR; branch if CTR == 0 and condition true
  BT     = 12 << 21,  // Branch if condition true
  DCBNZ  = 16 << 21,  // Decrement CTR; branch if CTR != 0
  DCBEZ  = 18 << 21,  // Decrement CTR; branch if CTR == 0
  BA     = 20 << 21   // Branch always
};

#if defined(INCLUDE_ARM)
// Condition code updating mode.
enum SBit {
  SetCC   = 1 << 20,  // Set condition code.
  LeaveCC = 0 << 20   // Leave condition code unchanged.
};


// Status register selection.
enum SRegister {
  CPSR = 0 << 22,
  SPSR = 1 << 22
};


// Shifter types for Data-processing operands as defined in section A5.1.2.
enum ShiftOp {
  LSL = 0 << 5,   // Logical shift left.
  LSR = 1 << 5,   // Logical shift right.
  ASR = 2 << 5,   // Arithmetic shift right.
  ROR = 3 << 5,   // Rotate right.

  // RRX is encoded as ROR with shift_imm == 0.
  // Use a special code to make the distinction. The RRX ShiftOp is only used
  // as an argument, and will never actually be encoded. The Assembler will
  // detect it and emit the correct ROR shift operand with shift_imm == 0.
  RRX = -1,
  kNumberOfShifts = 4
};


// Status register fields.
enum SRegisterField {
  CPSR_c = CPSR | 1 << 16,
  CPSR_x = CPSR | 1 << 17,
  CPSR_s = CPSR | 1 << 18,
  CPSR_f = CPSR | 1 << 19,
  SPSR_c = SPSR | 1 << 16,
  SPSR_x = SPSR | 1 << 17,
  SPSR_s = SPSR | 1 << 18,
  SPSR_f = SPSR | 1 << 19
};

// Status register field mask (or'ed SRegisterField enum values).
typedef uint32_t SRegisterFieldMask;


// Memory operand addressing mode.
enum AddrMode {
  // Bit encoding P U W.
  Offset       = (8|4|0) << 21,  // Offset (without writeback to base).
  PreIndex     = (8|4|1) << 21,  // Pre-indexed addressing with writeback.
  PostIndex    = (0|4|0) << 21,  // Post-indexed addressing with writeback.
  NegOffset    = (8|0|0) << 21,  // Negative offset (without writeback to base).
  NegPreIndex  = (8|0|1) << 21,  // Negative pre-indexed with writeback.
  NegPostIndex = (0|0|0) << 21   // Negative post-indexed with writeback.
};


// Load/store multiple addressing mode.
enum BlockAddrMode {
  // Bit encoding P U W .
  da           = (0|0|0) << 21,  // Decrement after.
  ia           = (0|4|0) << 21,  // Increment after.
  db           = (8|0|0) << 21,  // Decrement before.
  ib           = (8|4|0) << 21,  // Increment before.
  da_w         = (0|0|1) << 21,  // Decrement after with writeback to base.
  ia_w         = (0|4|1) << 21,  // Increment after with writeback to base.
  db_w         = (8|0|1) << 21,  // Decrement before with writeback to base.
  ib_w         = (8|4|1) << 21,  // Increment before with writeback to base.

  // Alias modes for comparison when writeback does not matter.
  da_x         = (0|0|0) << 21,  // Decrement after.
  ia_x         = (0|4|0) << 21,  // Increment after.
  db_x         = (8|0|0) << 21,  // Decrement before.
  ib_x         = (8|4|0) << 21,  // Increment before.

  kBlockAddrModeMask = (8|4|1) << 21
};


// Coprocessor load/store operand size.
enum LFlag {
  Long  = 1 << 22,  // Long load/store coprocessor.
  Short = 0 << 22   // Short load/store coprocessor.
};
#endif  // INCLUDE_ARM


// -----------------------------------------------------------------------------
// Supervisor Call (svc) specific support.

// Special Software Interrupt codes when used in the presence of the ARM
// simulator.
// svc (formerly swi) provides a 24bit immediate value. Use bits 22:0 for
// standard SoftwareInterrupCode. Bit 23 is reserved for the stop feature.
enum SoftwareInterruptCodes {
  // transition to C code
  kCallRtRedirected= 0x10,
  // break point
  kBreakpoint= 0x20,
  // stop
  kStopCode = 1 << 23
};
const uint32_t kStopCodeMask = kStopCode - 1;
const uint32_t kMaxStopCode = kStopCode - 1;
const int32_t  kDefaultStopCode = -1;

// VFP rounding modes. See ARM DDI 0406B Page A2-29.
enum VFPRoundingMode {
  RN = 0 << 22,   // Round to Nearest.
  RP = 1 << 22,   // Round towards Plus Infinity.
  RM = 2 << 22,   // Round towards Minus Infinity.
  RZ = 3 << 22,   // Round towards zero.

  // Aliases.
  kRoundToNearest = RN,
  kRoundToPlusInf = RP,
  kRoundToMinusInf = RM,
  kRoundToZero = RZ
};

const uint32_t kVFPRoundingModeMask = 3 << 22;

enum CheckForInexactConversion {
  kCheckForInexactConversion,
  kDontCheckForInexactConversion
};

// -----------------------------------------------------------------------------
// Hints.

// Branch hints are not used on the ARM.  They are defined so that they can
// appear in shared function signatures, but will be ignored in ARM
// implementations.
enum Hint { no_hint };

// Hints are not used on the arm.  Negating is trivial.
inline Hint NegateHint(Hint ignored) { return no_hint; }


// -----------------------------------------------------------------------------
// Specific instructions, constants, and masks.
// These constants are declared in assembler-arm.cc, as they use named registers
// and other constants.


// add(sp, sp, 4) instruction (aka Pop())
extern const Instr kPopInstruction;

// str(r, MemOperand(sp, 4, NegPreIndex), al) instruction (aka push(r))
// register r is not encoded.
extern const Instr kPushRegPattern;

// ldr(r, MemOperand(sp, 4, PostIndex), al) instruction (aka pop(r))
// register r is not encoded.
extern const Instr kPopRegPattern;

// use TWI to indicate redirection call for simulation mode
const Instr rtCallRedirInstr = TWI;

// -----------------------------------------------------------------------------
// Instruction abstraction.

// The class Instruction enables access to individual fields defined in the ARM
// architecture instruction set encoding as described in figure A3-1.
// Note that the Assembler uses typedef int32_t Instr.
//
// Example: Test whether the instruction at ptr does set the condition code
// bits.
//
// bool InstructionSetsConditionCodes(byte* ptr) {
//   Instruction* instr = Instruction::At(ptr);
//   int type = instr->TypeValue();
//   return ((type == 0) || (type == 1)) && instr->HasS();
// }
//
class Instruction {
 public:
  enum {
    kInstrSize = 4,
    kInstrSizeLog2 = 2,
    kPCReadOffset = 8
  };

  // Helper macro to define static accessors.
  // We use the cast to char* trick to bypass the strict anti-aliasing rules.
  #define DECLARE_STATIC_TYPED_ACCESSOR(return_type, Name)                     \
    static inline return_type Name(Instr instr) {                              \
      char* temp = reinterpret_cast<char*>(&instr);                            \
      return reinterpret_cast<Instruction*>(temp)->Name();                     \
    }

  #define DECLARE_STATIC_ACCESSOR(Name) DECLARE_STATIC_TYPED_ACCESSOR(int, Name)

  // Get the raw instruction bits.
  inline Instr InstructionBits() const {
    return *reinterpret_cast<const Instr*>(this);
  }

  // Set the raw instruction bits to value.
  inline void SetInstructionBits(Instr value) {
    *reinterpret_cast<Instr*>(this) = value;
  }

  // Read one particular bit out of the instruction bits.
  inline int Bit(int nr) const {
    return (InstructionBits() >> nr) & 1;
  }

  // Read a bit field's value out of the instruction bits.
  inline int Bits(int hi, int lo) const {
    return (InstructionBits() >> lo) & ((2 << (hi - lo)) - 1);
  }

  // Read a bit field out of the instruction bits.
  inline int BitField(int hi, int lo) const {
    return InstructionBits() & (((2 << (hi - lo)) - 1) << lo);
  }

  // Static support.

  // Read one particular bit out of the instruction bits.
  static inline int Bit(Instr instr, int nr) {
    return (instr >> nr) & 1;
  }

  // Read the value of a bit field out of the instruction bits.
  static inline int Bits(Instr instr, int hi, int lo) {
    return (instr >> lo) & ((2 << (hi - lo)) - 1);
  }


  // Read a bit field out of the instruction bits.
  static inline int BitField(Instr instr, int hi, int lo) {
    return instr & (((2 << (hi - lo)) - 1) << lo);
  }

  // PowerPC
  inline int RSValue() const { return Bits(25, 21); }
  inline int RTValue() const { return Bits(25, 21); }
  inline int RAValue() const { return Bits(20, 16); }
  DECLARE_STATIC_ACCESSOR(RAValue);
  inline int RBValue() const { return Bits(15, 11); }
  DECLARE_STATIC_ACCESSOR(RBValue);
  inline int RCValue() const { return Bits(10, 6); }
  DECLARE_STATIC_ACCESSOR(RCValue);
  // end PowerPC

  inline int OpcodeValue() const {
    return static_cast<Opcode>(Bits(31, 26));  // PowerPC
  }
  inline Opcode OpcodeField() const {
    return static_cast<Opcode>(BitField(24, 21));
  }

  // Fields used in Software interrupt instructions
  inline SoftwareInterruptCodes SvcValue() const {
    return static_cast<SoftwareInterruptCodes>(Bits(23, 0));
  }

  // Instructions are read of out a code stream. The only way to get a
  // reference to an instruction is to convert a pointer. There is no way
  // to allocate or create instances of class Instruction.
  // Use the At(pc) function to create references to Instruction.
  static Instruction* At(byte* pc) {
    return reinterpret_cast<Instruction*>(pc);
  }


 private:
  // We need to prevent the creation of instances of class Instruction.
  DISALLOW_IMPLICIT_CONSTRUCTORS(Instruction);
};


// Helper functions for converting between register numbers and names.
class Registers {
 public:
  // Return the name of the register.
  static const char* Name(int reg);

  // Lookup the register number for the name provided.
  static int Number(const char* name);

  struct RegisterAlias {
    int reg;
    const char* name;
  };

 private:
  static const char* names_[kNumRegisters];
  static const RegisterAlias aliases_[];
};

// Helper functions for converting between FP register numbers and names.
class FPRegisters {
 public:
  // Return the name of the register.
  static const char* Name(int reg, bool is_double);

  // Lookup the register number for the name provided.
  // Set flag pointed by is_double to true if register
  // is double-precision.
  static int Number(const char* name, bool* is_double);

 private:
  static const char* names_[kNumFPRegisters];
};

// Argument encoding for function calls
enum FunctionCallType {
  // First arg passed by value
  CallType_ScalarArg,
  // First arg passed by reference
  CallType_NonScalarArg
};


} }  // namespace v8::internal

#undef INCLUDE_ARM
#endif  // V8_PPC_CONSTANTS_PPC_H_
