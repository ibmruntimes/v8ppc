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

// A Disassembler object is used to disassemble a block of code instruction by
// instruction. The default implementation of the NameConverter object can be
// overriden to modify register names or to do symbol lookup on addresses.
//
// The example below will disassemble a block of code and print it to stdout.
//
//   NameConverter converter;
//   Disassembler d(converter);
//   for (byte* pc = begin; pc < end;) {
//     v8::internal::EmbeddedVector<char, 256> buffer;
//     byte* prev_pc = pc;
//     pc += d.InstructionDecode(buffer, pc);
//     printf("%p    %08x      %s\n",
//            prev_pc, *reinterpret_cast<int32_t*>(prev_pc), buffer);
//   }
//
// The Disassembler class also has a convenience method to disassemble a block
// of code into a FILE*, meaning that the above functionality could also be
// achieved by just calling Disassembler::Disassemble(stdout, begin, end);


#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#ifndef WIN32
#include <stdint.h>
#endif

#include "v8.h"

#if defined(V8_TARGET_ARCH_PPC)

#include "constants-ppc.h"
#include "disasm.h"
#include "macro-assembler.h"
#include "platform.h"


namespace v8 {
namespace internal {


//------------------------------------------------------------------------------

// Decoder decodes and disassembles instructions into an output buffer.
// It uses the converter to convert register names and call destinations into
// more informative description.
class Decoder {
 public:
  Decoder(const disasm::NameConverter& converter,
          Vector<char> out_buffer)
    : converter_(converter),
      out_buffer_(out_buffer),
      out_buffer_pos_(0) {
    out_buffer_[out_buffer_pos_] = '\0';
  }

  ~Decoder() {}

  // Writes one disassembled instruction into 'buffer' (0-terminated).
  // Returns the length of the disassembled machine instruction in bytes.
  int InstructionDecode(byte* instruction);

  static bool IsConstantPoolAt(byte* instr_ptr);
  static int ConstantPoolSizeAt(byte* instr_ptr);

 private:
  // Bottleneck functions to print into the out_buffer.
  void PrintChar(const char ch);
  void Print(const char* str);

  // Printing of common values.
  void PrintRegister(int reg);
  void PrintSRegister(int reg);
  void PrintDRegister(int reg);
  int FormatFPRegister(Instruction* instr, const char* format);
  void PrintShiftSat(Instruction* instr);
  void PrintPU(Instruction* instr);
  void PrintSoftwareInterrupt(SoftwareInterruptCodes svc);

  // Handle formatting of instructions and their options.
  int FormatRegister(Instruction* instr, const char* option);
  int FormatOption(Instruction* instr, const char* option);
  void Format(Instruction* instr, const char* format);
  void Unknown(Instruction* instr);
  void UnknownFormat(Instruction* instr, const char* opcname);
  void MarkerFormat(Instruction* instr, const char* opcname, int id);

  // PowerPC decoding
  void DecodeExt1(Instruction* instr);
  void DecodeExt2(Instruction* instr);
  void DecodeExt4(Instruction* instr);

  const disasm::NameConverter& converter_;
  Vector<char> out_buffer_;
  int out_buffer_pos_;

  DISALLOW_COPY_AND_ASSIGN(Decoder);
};


// Support for assertions in the Decoder formatting functions.
#define STRING_STARTS_WITH(string, compare_string) \
  (strncmp(string, compare_string, strlen(compare_string)) == 0)


// Append the ch to the output buffer.
void Decoder::PrintChar(const char ch) {
  out_buffer_[out_buffer_pos_++] = ch;
}


// Append the str to the output buffer.
void Decoder::Print(const char* str) {
  char cur = *str++;
  while (cur != '\0' && (out_buffer_pos_ < (out_buffer_.length() - 1))) {
    PrintChar(cur);
    cur = *str++;
  }
  out_buffer_[out_buffer_pos_] = 0;
}

// Print the register name according to the active name converter.
void Decoder::PrintRegister(int reg) {
  Print(converter_.NameOfCPURegister(reg));
}

// Print the VFP S register name according to the active name converter.
void Decoder::PrintSRegister(int reg) {
  Print(FPRegisters::Name(reg, false));
}

// Print the  VFP D register name according to the active name converter.
void Decoder::PrintDRegister(int reg) {
  Print(FPRegisters::Name(reg, true));
}

// Print SoftwareInterrupt codes. Factoring this out reduces the complexity of
// the FormatOption method.
void Decoder::PrintSoftwareInterrupt(SoftwareInterruptCodes svc) {
  switch (svc) {
    case kCallRtRedirected:
      Print("call rt redirected");
      return;
    case kBreakpoint:
      Print("breakpoint");
      return;
    default:
      if (svc >= kStopCode) {
        out_buffer_pos_ += OS::SNPrintF(out_buffer_ + out_buffer_pos_,
                                        "%d - 0x%x",
                                        svc & kStopCodeMask,
                                        svc & kStopCodeMask);
      } else {
        out_buffer_pos_ += OS::SNPrintF(out_buffer_ + out_buffer_pos_,
                                        "%d",
                                        svc);
      }
      return;
  }
}

// Handle all register based formatting in this function to reduce the
// complexity of FormatOption.
int Decoder::FormatRegister(Instruction* instr, const char* format) {
  ASSERT(format[0] == 'r');

  if ((format[1] == 't') || (format[1] == 's')) {  // 'rt & 'rs register
    int reg = instr->RTValue();
    PrintRegister(reg);
    return 2;
  } else if (format[1] == 'a') {  // 'ra: RA register
    int reg = instr->RAValue();
    PrintRegister(reg);
    return 2;
  } else if (format[1] == 'b') {  // 'rb: RB register
    int reg = instr->RBValue();
    PrintRegister(reg);
    return 2;
  }

#if 0
  if (format[1] == 'n') {  // 'rn: Rn register
    int reg = instr->RnValue();
    PrintRegister(reg);
    return 2;
  } else if (format[1] == 'd') {  // 'rd: Rd register
    int reg = instr->RdValue();
    PrintRegister(reg);
    return 2;
  } else if (format[1] == 's') {  // 'rs: Rs register
    int reg = instr->RsValue();
    PrintRegister(reg);
    return 2;
  } else if (format[1] == 'm') {  // 'rm: Rm register
    int reg = instr->RmValue();
    PrintRegister(reg);
    return 2;
  } else if (format[1] == 't') {  // 'rt: Rt register
    int reg = instr->RtValue();
    PrintRegister(reg);
    return 2;
  } else if (format[1] == 'l') {
    // 'rlist: register list for load and store multiple instructions
    ASSERT(STRING_STARTS_WITH(format, "rlist"));
    int rlist = instr->RlistValue();
    int reg = 0;
    Print("{");
    // Print register list in ascending order, by scanning the bit mask.
    while (rlist != 0) {
      if ((rlist & 1) != 0) {
        PrintRegister(reg);
        if ((rlist >> 1) != 0) {
          Print(", ");
        }
      }
      reg++;
      rlist >>= 1;
    }
    Print("}");
    return 5;
  }
#endif
  UNREACHABLE();
  return -1;
}


// Handle all FP register based formatting in this function to reduce the
// complexity of FormatOption.
int Decoder::FormatFPRegister(Instruction* instr, const char* format) {
  ASSERT(format[0] == 'D');

  int retval = 2;
  int reg = -1;
  if (format[1] == 't') {
    reg = instr->RTValue();
  } else if (format[1] == 'a') {
    reg = instr->RAValue();
  } else if (format[1] == 'b') {
    reg = instr->RBValue();
  } else if (format[1] == 'c') {
    reg = instr->RCValue();
  } else {
    UNREACHABLE();
  }

  PrintDRegister(reg);

  return retval;
}

// FormatOption takes a formatting string and interprets it based on
// the current instructions. The format string points to the first
// character of the option string (the option escape has already been
// consumed by the caller.)  FormatOption returns the number of
// characters that were consumed from the formatting string.
int Decoder::FormatOption(Instruction* instr, const char* format) {
  switch (format[0]) {
    case 'o': {
      if (instr->Bit(10) == 1) {
        Print("o");
      }
      return 1;
    }
    case '.': {
      if (instr->Bit(0) == 1) {
        Print(".");
      } else {
        Print(" ");  // ensure consistent spacing
      }
      return 1;
    }
    case 'r': {
      return FormatRegister(instr, format);
    }
    case 'D': {
      return FormatFPRegister(instr, format);
    }
    case 'i': {  // int16
      int32_t value = (instr->Bits(15, 0) << 16) >> 16;
      out_buffer_pos_ += OS::SNPrintF(out_buffer_ + out_buffer_pos_,
                                      "%d", value);
      return 5;
    }
    case 'u': {  // uint16
      int32_t value = instr->Bits(15, 0);
      out_buffer_pos_ += OS::SNPrintF(out_buffer_ + out_buffer_pos_,
                                      "%d", value);
      return 6;
    }
    case 'l': {
      // Link (LK) Bit 0
      if (instr->Bit(0) == 1) {
        Print("l");
      }
      return 1;
    }
    case 'a': {
      // Absolute Address Bit 1
      if (instr->Bit(1) == 1) {
        Print("a");
      }
      return 1;
    }
    case 't': {  // 'target: target of branch instructions
      // target26 or target16
      ASSERT(STRING_STARTS_WITH(format, "target"));
      if ((format[6] == '2') && (format[7] == '6')) {
        int off = ((instr->Bits(25, 2)) << 8) >> 6;
        out_buffer_pos_ += OS::SNPrintF(out_buffer_ + out_buffer_pos_,
                                        "%+d -> %s",
                                        off,
                                        converter_.NameOfAddress(
                                        reinterpret_cast<byte*>(instr) + off));
        return 8;
      } else if ((format[6] == '1') && (format[7] == '6')) {
        int off = ((instr->Bits(15, 2)) << 18) >> 16;
        out_buffer_pos_ += OS::SNPrintF(out_buffer_ + out_buffer_pos_,
                                        "%+d -> %s",
                                        off,
                                        converter_.NameOfAddress(
                                        reinterpret_cast<byte*>(instr) + off));
        return 8;
      }
     case 's': {  // SH Bits 15-11
       ASSERT(format[1] == 'h');
       int32_t value = (instr->Bits(15, 11) << 26) >> 26;
       out_buffer_pos_ += OS::SNPrintF(out_buffer_ + out_buffer_pos_,
                                     "%d", value);
       return 2;
     }
     case 'm': {
       int32_t value = 0;
       if (format[1] == 'e') {  // ME Bits 10-6
         value = (instr->Bits(10, 6) << 26) >> 26;
       } else if (format[1] == 'b') {  // MB Bits 5-1
         value = (instr->Bits(5, 1) << 26) >> 26;
       } else {
         UNREACHABLE();  // bad format
       }
       out_buffer_pos_ += OS::SNPrintF(out_buffer_ + out_buffer_pos_,
                                     "%d", value);
       return 2;
     }
    }
    default: {
      UNREACHABLE();
      break;
    }
  }
#if 0
  switch (format[0]) {
    case 'a': {  // 'a: accumulate multiplies
      if (instr->Bit(21) == 0) {
        Print("ul");
      } else {
        Print("la");
      }
      return 1;
    }
    case 'b': {  // 'b: byte loads or stores
      if (instr->HasB()) {
        Print("b");
      }
      return 1;
    }
    case 'c': {  // 'cond: conditional execution
      ASSERT(STRING_STARTS_WITH(format, "cond"));
      PrintCondition(instr);
      return 4;
    }
    case 'd': {  // 'd: vmov double immediate.
      double d = instr->DoubleImmedVmov();
      out_buffer_pos_ += OS::SNPrintF(out_buffer_ + out_buffer_pos_,
                                      "#%g", d);
      return 1;
    }
    case 'f': {  // 'f: bitfield instructions - v7 and above.
      uint32_t lsbit = instr->Bits(11, 7);
      uint32_t width = instr->Bits(20, 16) + 1;
      if (instr->Bit(21) == 0) {
        // BFC/BFI:
        // Bits 20-16 represent most-significant bit. Covert to width.
        width -= lsbit;
        ASSERT(width > 0);
      }
      ASSERT((width + lsbit) <= 32);
      out_buffer_pos_ += OS::SNPrintF(out_buffer_ + out_buffer_pos_,
                                      "#%d, #%d", lsbit, width);
      return 1;
    }
    case 'h': {  // 'h: halfword operation for extra loads and stores
      if (instr->HasH()) {
        Print("h");
      } else {
        Print("b");
      }
      return 1;
    }
    case 'i': {  // 'i: immediate value from adjacent bits.
      // Expects tokens in the form imm%02d@%02d, i.e. imm05@07, imm10@16
      int width = (format[3] - '0') * 10 + (format[4] - '0');
      int lsb   = (format[6] - '0') * 10 + (format[7] - '0');

      ASSERT((width >= 1) && (width <= 32));
      ASSERT((lsb >= 0) && (lsb <= 31));
      ASSERT((width + lsb) <= 32);

      out_buffer_pos_ += OS::SNPrintF(out_buffer_ + out_buffer_pos_,
                                      "%d",
                                      instr->Bits(width + lsb - 1, lsb));
      return 8;
    }
    case 'l': {  // 'l: branch and link
      if (instr->HasLink()) {
        Print("l");
      }
      return 1;
    }
    case 'm': {
      if (format[1] == 'w') {
        // 'mw: movt/movw instructions.
        PrintMovwMovt(instr);
        return 2;
      }
      if (format[1] == 'e') {  // 'memop: load/store instructions.
        ASSERT(STRING_STARTS_WITH(format, "memop"));
        if (instr->HasL()) {
          Print("ldr");
        } else {
          if ((instr->Bits(27, 25) == 0) && (instr->Bit(20) == 0) &&
              (instr->Bits(7, 6) == 3) && (instr->Bit(4) == 1)) {
            if (instr->Bit(5) == 1) {
              Print("strd");
            } else {
              Print("ldrd");
            }
            return 5;
          }
          Print("str");
        }
        return 5;
      }
      // 'msg: for simulator break instructions
      ASSERT(STRING_STARTS_WITH(format, "msg"));
      byte* str =
          reinterpret_cast<byte*>(instr->InstructionBits() & 0x0fffffff);
      out_buffer_pos_ += OS::SNPrintF(out_buffer_ + out_buffer_pos_,
                                      "%s", converter_.NameInCode(str));
      return 3;
    }
    case 'o': {
      if ((format[3] == '1') && (format[4] == '2')) {
        // 'off12: 12-bit offset for load and store instructions
        ASSERT(STRING_STARTS_WITH(format, "off12"));
        out_buffer_pos_ += OS::SNPrintF(out_buffer_ + out_buffer_pos_,
                                        "%d", instr->Offset12Value());
        return 5;
      } else if (format[3] == '0') {
        // 'off0to3and8to19 16-bit immediate encoded in bits 19-8 and 3-0.
        ASSERT(STRING_STARTS_WITH(format, "off0to3and8to19"));
        out_buffer_pos_ += OS::SNPrintF(out_buffer_ + out_buffer_pos_,
                                        "%d",
                                        (instr->Bits(19, 8) << 4) +
                                        instr->Bits(3, 0));
        return 15;
      }
      // 'off8: 8-bit offset for extra load and store instructions
      ASSERT(STRING_STARTS_WITH(format, "off8"));
      int offs8 = (instr->ImmedHValue() << 4) | instr->ImmedLValue();
      out_buffer_pos_ += OS::SNPrintF(out_buffer_ + out_buffer_pos_,
                                      "%d", offs8);
      return 4;
    }
    case 'p': {  // 'pu: P and U bits for load and store instructions
      ASSERT(STRING_STARTS_WITH(format, "pu"));
      PrintPU(instr);
      return 2;
    }
    case 'r': {
      return FormatRegister(instr, format);
    }
    case 's': {
      if (format[1] == 'h') {  // 'shift_op or 'shift_rm or 'shift_sat.
        if (format[6] == 'o') {  // 'shift_op
          ASSERT(STRING_STARTS_WITH(format, "shift_op"));
          if (instr->TypeValue() == 0) {
            PrintShiftRm(instr);
          } else {
            ASSERT(instr->TypeValue() == 1);
            PrintShiftImm(instr);
          }
          return 8;
        } else if (format[6] == 's') {  // 'shift_sat.
          ASSERT(STRING_STARTS_WITH(format, "shift_sat"));
          PrintShiftSat(instr);
          return 9;
        } else {  // 'shift_rm
          ASSERT(STRING_STARTS_WITH(format, "shift_rm"));
          PrintShiftRm(instr);
          return 8;
        }
      } else if (format[1] == 'v') {  // 'svc
        ASSERT(STRING_STARTS_WITH(format, "svc"));
        PrintSoftwareInterrupt(instr->SvcValue());
        return 3;
      } else if (format[1] == 'i') {  // 'sign: signed extra loads and stores
        ASSERT(STRING_STARTS_WITH(format, "sign"));
        if (instr->HasSign()) {
          Print("s");
        }
        return 4;
      }
      // 's: S field of data processing instructions
      if (instr->HasS()) {
        Print("s");
      }
      return 1;
    }
    case 't': {  // 'target: target of branch instructions
      ASSERT(STRING_STARTS_WITH(format, "target"));
      int off = (instr->SImmed24Value() << 2) + 8;
      out_buffer_pos_ += OS::SNPrintF(out_buffer_ + out_buffer_pos_,
                                      "%+d -> %s",
                                      off,
                                      converter_.NameOfAddress(
                                        reinterpret_cast<byte*>(instr) + off));
      return 6;
    }
    case 'u': {  // 'u: signed or unsigned multiplies
      // The manual gets the meaning of bit 22 backwards in the multiply
      // instruction overview on page A3.16.2.  The instructions that
      // exist in u and s variants are the following:
      // smull A4.1.87
      // umull A4.1.129
      // umlal A4.1.128
      // smlal A4.1.76
      // For these 0 means u and 1 means s.  As can be seen on their individual
      // pages.  The other 18 mul instructions have the bit set or unset in
      // arbitrary ways that are unrelated to the signedness of the instruction.
      // None of these 18 instructions exist in both a 'u' and an 's' variant.

      if (instr->Bit(22) == 0) {
        Print("u");
      } else {
        Print("s");
      }
      return 1;
    }
    case 'v': {
      return FormatVFPinstruction(instr, format);
    }
    case 'S':
    case 'D': {
      return FormatFPRegister(instr, format);
    }
    case 'w': {  // 'w: W field of load and store instructions
      if (instr->HasW()) {
        Print("!");
      }
      return 1;
    }
    default: {
      UNREACHABLE();
      break;
    }
  }
#endif
  UNREACHABLE();
  return -1;
}


// Format takes a formatting string for a whole instruction and prints it into
// the output buffer. All escaped options are handed to FormatOption to be
// parsed further.
void Decoder::Format(Instruction* instr, const char* format) {
  char cur = *format++;
  while ((cur != 0) && (out_buffer_pos_ < (out_buffer_.length() - 1))) {
    if (cur == '\'') {  // Single quote is used as the formatting escape.
      format += FormatOption(instr, format);
    } else {
      out_buffer_[out_buffer_pos_++] = cur;
    }
    cur = *format++;
  }
  out_buffer_[out_buffer_pos_]  = '\0';
}


// The disassembler may end up decoding data inlined in the code. We do not want
// it to crash if the data does not ressemble any known instruction.
#define VERIFY(condition) \
  if (!(condition)) {     \
    Unknown(instr);       \
    return;               \
  }


// For currently unimplemented decodings the disassembler calls Unknown(instr)
// which will just print "unknown" of the instruction bits.
void Decoder::Unknown(Instruction* instr) {
  Format(instr, "unknown");
}

// For currently unimplemented decodings the disassembler calls
// UnknownFormat(instr) which will just print opcode name of the
// instruction bits.
void Decoder::UnknownFormat(Instruction* instr, const char* name) {
  char buffer[100];
  snprintf(buffer, sizeof(buffer), "%s (unknown-format)", name);
  Format(instr, buffer);
}

void Decoder::MarkerFormat(Instruction* instr, const char* name, int id) {
  char buffer[100];
  snprintf(buffer, sizeof(buffer), "%s %d", name, id);
  Format(instr, buffer);
}

// PowerPC
void Decoder::DecodeExt1(Instruction* instr) {
  switch (instr->Bits(10, 1) << 1) {
    case MCRF: {
      UnknownFormat(instr, "mcrf");  // not used by V8
      break;
    }
    case BCLRX: {
      switch (instr->Bits(25, 21) << 21) {
        case DCBNZF: {
          UnknownFormat(instr, "bclrx-dcbnzf");
          break;
        }
        case DCBEZF: {
          UnknownFormat(instr, "bclrx-dcbezf");
          break;
        }
        case BF: {
          UnknownFormat(instr, "bclrx-bf");
          break;
        }
        case DCBNZT: {
          UnknownFormat(instr, "bclrx-dcbbzt");
          break;
        }
        case DCBEZT: {
          UnknownFormat(instr, "bclrx-dcbnezt");
          break;
        }
        case BT: {
          UnknownFormat(instr, "bclrx-bt");
          break;
        }
        case DCBNZ: {
          UnknownFormat(instr, "bclrx-dcbnz");
          break;
        }
        case DCBEZ: {
          UnknownFormat(instr, "bclrx-dcbez");  // not used by V8
          break;
        }
        case BA: {
          if (instr->Bit(0) == 1) {
            Format(instr, "blrl");
          } else {
            Format(instr, "blr");
          }
          break;
        }
      }
      break;
    }
    case BCCTRX: {
      switch (instr->Bits(25, 21) << 21) {
        case DCBNZF: {
          UnknownFormat(instr, "bcctrx-dcbnzf");
          break;
        }
        case DCBEZF: {
          UnknownFormat(instr, "bcctrx-dcbezf");
          break;
        }
        case BF: {
          UnknownFormat(instr, "bcctrx-bf");
          break;
        }
        case DCBNZT: {
          UnknownFormat(instr, "bcctrx-dcbnzt");
          break;
        }
        case DCBEZT: {
          UnknownFormat(instr, "bcctrx-dcbezf");
          break;
        }
        case BT: {
          UnknownFormat(instr, "bcctrx-bt");
          break;
        }
        case DCBNZ: {
          UnknownFormat(instr, "bcctrx-dcbnz");
          break;
        }
        case DCBEZ: {
          UnknownFormat(instr, "bcctrx-dcbez");
          break;
        }
        case BA: {
          if (instr->Bit(0) == 1) {
            Format(instr, "bctrl");
          } else {
            Format(instr, "bctr");
          }
          break;
        }
        default: {
          UNREACHABLE();
        }
      }
      break;
    }
    case CRNOR: {
      Format(instr, "crnor (stuff)");
      break;
    }
    case RFI: {
      Format(instr, "rfi (stuff)");
      break;
    }
    case CRANDC: {
      Format(instr, "crandc (stuff)");
      break;
    }
    case ISYNC: {
      Format(instr, "isync (stuff)");
      break;
    }
    case CRXOR: {
      Format(instr, "crxor (stuff)");
      break;
    }
    case CRNAND: {
      UnknownFormat(instr, "crnand");
      break;
    }
    case CRAND: {
      UnknownFormat(instr, "crand");
      break;
    }
    case CREQV: {
      UnknownFormat(instr, "creqv");
      break;
    }
    case CRORC: {
      UnknownFormat(instr, "crorc");
      break;
    }
    case CROR: {
      UnknownFormat(instr, "cror");
      break;
    }
    default: {
      Unknown(instr);  // not used by V8
    }
  }
}

void Decoder::DecodeExt2(Instruction* instr) {
  // Some encodings are 10-1 bits, handle those first
  switch (instr->Bits(10, 1) << 1) {
    case SRWX: {
      Format(instr, "srw'.    'ra,'rs,'rb");
      return;
    }
    case SRAW: {
      Format(instr, "sraw'.   'ra,'rs,'rb");
      return;
    }
    case SRAWIX: {
      Format(instr, "srawi'.  'ra,'rs,'sh");
      return;
    }
    case EXTSH: {
      Format(instr, "extsh'.  'ra,'rs");
      return;
    }
    case EXTSB: {
      Format(instr, "extsb'.  'ra,'rs");
      return;
    }
  }

  // ?? are all of these xo_form?
  switch (instr->Bits(9, 1) << 1) {
    case CMP: {
      Format(instr, "cmp     'ra,'rb");
      break;
    }
    case SLWX: {
      Format(instr, "slw'.   'ra,'rs,'rb");
      break;
    }
    case SUBFCX: {
      Format(instr, "subfc'. 'rt,'ra,'rb");
      break;
    }
    case ADDCX: {
      Format(instr, "addc'.   'rt,'ra,'rb");
      break;
    }
    case CNTLZWX: {
      Format(instr, "cntlzw'. 'ra,'rs");
      break;
    }
    case ANDX: {
      Format(instr, "and'.    'ra,'rs,'rb");
      break;
    }
    case ANDCX: {
      Format(instr, "andc'.   'ra,'rs,'rb");
      break;
    }
    case CMPL: {
      Format(instr, "cmpl    'ra,'rb");
      break;
    }
    case NEGX: {
      Format(instr, "neg'.    'rt,'ra");
      break;
    }
    case NORX: {
      Format(instr, "nor'.    'rt,'ra,'rb");
      break;
    }
    case SUBFX: {
      Format(instr, "subf'.   'rt,'ra,'rb");
      break;
    }
    case MULHWX: {
      Format(instr, "mulhw'o'. 'rt,'ra,'rb");
      break;
    }
    case ADDZEX: {
      Format(instr, "addze'.   'rt,'ra");
      break;
    }
    case MULLW: {
      Format(instr, "mullw'o'. 'rt,'ra,'rb");
      break;
    }
    case ADDX: {
      Format(instr, "add'o     'rt,'ra,'rb");
      break;
    }
    case XORX: {
      Format(instr, "xor'.    'ra,'rs,'rb");
      break;
    }
    case ORX: {
      if ( instr->RTValue() == instr->RBValue() ) {
        Format(instr, "mr      'ra,'rb");
      } else {
        Format(instr, "or      'ra,'rs,'rb");
      }
      break;
    }
    case MFSPR: {
      int spr = instr->Bits(20, 11);
      if (256 == spr) {
        Format(instr, "mflr    'rt");
      } else {
        Format(instr, "mfspr   'rt ??");
      }
      break;
    }
    case MTSPR: {
      int spr = instr->Bits(20, 11);
      if (256 == spr) {
        Format(instr, "mtlr    'rt");
      } else if (288 == spr) {
        Format(instr, "mtctr   'rt");
      } else {
        Format(instr, "mtspr   'rt ??");
      }
      break;
    }
    case STWX: {
      Format(instr, "stwx    'rs, 'ra, 'rb");
      break;
    }
    case STWUX: {
      Format(instr, "stwux   'rs, 'ra, 'rb");
      break;
    }
    case LWZX: {
      Format(instr, "lwzx    'rt, 'ra, 'rb");
      break;
    }
    case LWZUX: {
      Format(instr, "lwzux   'rt, 'ra, 'rb");
      break;
    }
    default: {
      Unknown(instr);  // not used by V8
    }
  }
}

void Decoder::DecodeExt4(Instruction* instr) {
  switch (instr->Bits(5, 1) << 1) {
    case FDIV: {
      Format(instr, "fdiv    'Dt, 'Da, 'Db");
      return;
    }
    case FSUB: {
      Format(instr, "fsub    'Dt, 'Da, 'Db");
      return;
    }
    case FADD: {
      Format(instr, "fadd    'Dt, 'Da, 'Db");
      return;
    }
    case FMUL: {
      Format(instr, "fmul    'Dt, 'Da, 'Dc");
      return;
    }
  }

  switch (instr->Bits(10, 1) << 1) {
    case FCMPU: {
      Format(instr, "fcmpu   'Da, 'Db");
      break;
    }
    case FRSP: {
      Format(instr, "frsp    'Dt, 'Db");
      break;
    }
    case FCFID: {
      Format(instr, "fcfid   'Dt, 'Db");
      break;
    }
    case FCTIWZ: {
      Format(instr, "fctiwz  'Dt, 'Db");
      break;
    }
    case FRIM: {
      Format(instr, "frim    'Dt, 'Db");
      break;
    }
    default: {
      Unknown(instr);  // not used by V8
    }
  }
}

#undef VERIFIY

bool Decoder::IsConstantPoolAt(byte* instr_ptr) {
  int instruction_bits = *(reinterpret_cast<int*>(instr_ptr));
  return (instruction_bits & kConstantPoolMarkerMask) == kConstantPoolMarker;
}


int Decoder::ConstantPoolSizeAt(byte* instr_ptr) {
  if (IsConstantPoolAt(instr_ptr)) {
    int instruction_bits = *(reinterpret_cast<int*>(instr_ptr));
    return instruction_bits & kConstantPoolLengthMask;
  } else {
    return -1;
  }
}


// Disassemble the instruction at *instr_ptr into the output buffer.
int Decoder::InstructionDecode(byte* instr_ptr) {
  Instruction* instr = Instruction::At(instr_ptr);
  // Print raw instruction bytes.
  out_buffer_pos_ += OS::SNPrintF(out_buffer_ + out_buffer_pos_,
                                  "%08x       ",
                                  instr->InstructionBits());

    switch (instr->OpcodeValue() << 26) {
    case TWI: {
      PrintSoftwareInterrupt(instr->SvcValue());
      break;
    }
    case MULLI: {
      UnknownFormat(instr, "mulli");
      break;
    }
    case SUBFIC: {
      Format(instr, "subfic  'rt, 'ra, 'int16");
      break;
    }
    case CMPLI: {
      Format(instr, "cmpli   'ra,'uint16");
      break;
    }
    case CMPI: {
      Format(instr, "cmpwi   'ra,'int16");
      break;
    }
    case ADDIC: {
      Format(instr, "addic   'rt, 'ra, 'int16");
      break;
    }
    case ADDICx: {
      UnknownFormat(instr, "addicx");
      break;
    }
    case ADDI: {
      if ( instr->RAValue() == 0 ) {
        // this is load immediate
        Format(instr, "li      'rt, 'int16");
      } else {
        Format(instr, "addi    'rt, 'ra, 'int16");
      }
      break;
    }
    case ADDIS: {
      if ( instr->RAValue() == 0 ) {
        Format(instr, "lis     'rt, 'int16");
      } else {
        Format(instr, "addis   'rt, 'ra, 'int16");
      }
      break;
    }
    case BCX: {
      int bo = instr->Bits(25, 21) << 21;
      int bi = instr->Bits(20, 16);
      switch (bi) {
        case 2:
        case 30:
          if (BT == bo) {
            Format(instr, "beq'l'a 'target16");
            break;
          }
          if (BF == bo) {
            Format(instr, "bne'l'a 'target16");
            break;
          }
          Format(instr, "bc'l'a 'target16");
          break;
        case 29:
          if (BT == bo) {
            Format(instr, "bgt'l'a 'target16");
            break;
          }
          if (BF == bo) {
            Format(instr, "ble'l'a 'target16");
            break;
          }
          Format(instr, "bc'l'a 'target16");
          break;
        case 28:
          if (BT == bo) {
            Format(instr, "blt'l'a 'target16");
            break;
          }
          if (BF == bo) {
            Format(instr, "bge'l'a 'target16");
            break;
          }
          Format(instr, "bc'l'a 'target16");
          break;
        default:
          Format(instr, "bc'l'a 'target16");
          break;
      }
      break;
    }
    case SC: {
      UnknownFormat(instr, "sc");
      break;
    }
    case BX: {
      Format(instr, "b'l'a 'target26");
      break;
    }
    case EXT1: {
      DecodeExt1(instr);
      break;
    }
    case RLWIMIX: {
      Format(instr, "rlwimi'. 'ra,'rs,'sh,'me,'mb");
      break;
    }
    case RLWINMX: {
      Format(instr, "rlwinm'. 'ra,'rs,'sh,'me,'mb");
      break;
    }
    case RLWNMX: {
      UnknownFormat(instr, "rlwnmx");
      break;
    }
    case ORI: {
      Format(instr, "ori.    'ra, 'rs, 'uint16");
      break;
    }
    case ORIS: {
      Format(instr, "oris    'ra, 'rs, 'uint16");
      break;
    }
    case XORI: {
      Format(instr, "xori.   'ra, 'rs, 'uint16");
      break;
    }
    case XORIS: {
      Format(instr, "xoris   'ra, 'rs, 'uint16");
      break;
    }
    case ANDIx: {
      Format(instr, "andi.   'ra, 'rs, 'uint16");
      break;
    }
    case ANDISx: {
      Format(instr, "andis.  'ra, 'rs, 'uint16");
      break;
    }
    case EXT2: {
      DecodeExt2(instr);
      break;
    }
    case LWZ: {
      Format(instr, "lwz     'rt, 'int16('ra)");
      break;
    }
    case LWZU: {
      Format(instr, "lwzu    'rt, 'int16('ra)");
      break;
    }
    case LBZ: {
      Format(instr, "lbz     'rt, 'int16('ra)");
      break;
    }
    case LBZU: {
      Format(instr, "lbzu    'rt, 'int16('ra)");
      break;
    }
    case STW: {
      Format(instr, "stw     'rs, 'int16('ra)");
      break;
    }
    case STWU: {
      Format(instr, "stwu    'rs, 'int16('ra)");
      break;
    }
    case STB: {
      Format(instr, "stb     'rs, 'int16('ra)");
      break;
    }
    case STBU: {
      Format(instr, "stbu    'rs, 'int16('ra)");
      break;
    }
    case LHZ: {
      Format(instr, "lhz     'rt, 'int16('ra)");
      break;
    }
    case LHZU: {
      Format(instr, "lhzu    'rt, 'int16('ra)");
      break;
    }
    case LHA: {
      Format(instr, "lha     'rt, 'int16('ra)");
      break;
    }
    case LHAU: {
      Format(instr, "lhau    'rt, 'int16('ra)");
      break;
    }
    case STH: {
      Format(instr, "sth 'rs, 'int16('ra)");
      break;
    }
    case STHU: {
      Format(instr, "sthu 'rs, 'int16('ra)");
      break;
    }
    case LMW: {
      UnknownFormat(instr, "lmw");
      break;
    }
    case STMW: {
      UnknownFormat(instr, "stmw");
      break;
    }
    case LFS: {
      Format(instr, "lfs     'Dt, 'int16('ra)");
      break;
    }
    case LFSU: {
      Format(instr, "lfsu    'Dt, 'int16('ra)");
      break;
    }
    case LFD: {
      Format(instr, "lfd     'Dt, 'int16('ra)");
      break;
    }
    case LFDU: {
      Format(instr, "lfdu    'Dt, 'int16('ra)");
      break;
    }
    case STFS: {
      Format(instr, "stfs    'Dt, 'int16('ra)");
      break;
    }
    case STFSU: {
      Format(instr, "stfsu   'Dt, 'int16('ra)");
      break;
    }
    case STFD: {
      Format(instr, "stfd    'Dt, 'int16('ra)");
      break;
    }
    case STFDU: {
      Format(instr, "stfdu   'Dt, 'int16('ra)");
      break;
    }
    case EXT3:
    case EXT4: {
      DecodeExt4(instr);
      break;
    }

    case FAKE_OPCODE: {
      if (instr->Bits(MARKER_SUBOPCODE_BIT, MARKER_SUBOPCODE_BIT) == 1) {
        int marker_code = instr->Bits(STUB_MARKER_HIGH_BIT, 0);
        ASSERT(marker_code < F_NEXT_AVAILABLE_STUB_MARKER);
        MarkerFormat(instr, "stub-marker ", marker_code);
      } else {
        int fake_opcode = instr->Bits(FAKE_OPCODE_HIGH_BIT, 0);
        MarkerFormat(instr, "faker-opcode ", fake_opcode);
      }
      break;
    }
    default: {
      Unknown(instr);
      break;
    }
  }


#if 0
  if (instr->ConditionField() == kSpecialCondition) {
    Unknown(instr);
    return Instruction::kInstrSize;
  }
  int instruction_bits = *(reinterpret_cast<int*>(instr_ptr));
  if ((instruction_bits & kConstantPoolMarkerMask) == kConstantPoolMarker) {
    out_buffer_pos_ += OS::SNPrintF(out_buffer_ + out_buffer_pos_,
                                    "constant pool begin (length %d)",
                                    instruction_bits &
                                    kConstantPoolLengthMask);
    return Instruction::kInstrSize;
  }
  switch (instr->TypeValue()) {
    case 0:
    case 1: {
      DecodeType01(instr);
      break;
    }
    case 2: {
      DecodeType2(instr);
      break;
    }
    case 3: {
      DecodeType3(instr);
      break;
    }
    case 4: {
      DecodeType4(instr);
      break;
    }
    case 5: {
      DecodeType5(instr);
      break;
    }
    case 6: {
      DecodeType6(instr);
      break;
    }
    case 7: {
      return DecodeType7(instr);
    }
    default: {
      // The type field is 3-bits in the ARM encoding.
      UNREACHABLE();
      break;
    }
  }
#endif
  return Instruction::kInstrSize;
}


} }  // namespace v8::internal



//------------------------------------------------------------------------------

namespace disasm {


const char* NameConverter::NameOfAddress(byte* addr) const {
  v8::internal::OS::SNPrintF(tmp_buffer_, "%p", addr);
  return tmp_buffer_.start();
}


const char* NameConverter::NameOfConstant(byte* addr) const {
  return NameOfAddress(addr);
}


const char* NameConverter::NameOfCPURegister(int reg) const {
  return v8::internal::Registers::Name(reg);
}


const char* NameConverter::NameOfByteCPURegister(int reg) const {
  UNREACHABLE();  // ARM does not have the concept of a byte register
  return "nobytereg";
}


const char* NameConverter::NameOfXMMRegister(int reg) const {
  UNREACHABLE();  // ARM does not have any XMM registers
  return "noxmmreg";
}


const char* NameConverter::NameInCode(byte* addr) const {
  // The default name converter is called for unknown code. So we will not try
  // to access any memory.
  return "";
}


//------------------------------------------------------------------------------

Disassembler::Disassembler(const NameConverter& converter)
    : converter_(converter) {}


Disassembler::~Disassembler() {}


int Disassembler::InstructionDecode(v8::internal::Vector<char> buffer,
                                    byte* instruction) {
  v8::internal::Decoder d(converter_, buffer);
  return d.InstructionDecode(instruction);
}


int Disassembler::ConstantPoolSizeAt(byte* instruction) {
  return v8::internal::Decoder::ConstantPoolSizeAt(instruction);
}


void Disassembler::Disassemble(FILE* f, byte* begin, byte* end) {
  NameConverter converter;
  Disassembler d(converter);
  for (byte* pc = begin; pc < end;) {
    v8::internal::EmbeddedVector<char, 128> buffer;
    buffer[0] = '\0';
    byte* prev_pc = pc;
    pc += d.InstructionDecode(buffer, pc);
    fprintf(f, "%p    %08x      %s\n",
            prev_pc, *reinterpret_cast<int32_t*>(prev_pc), buffer.start());
  }
}


}  // namespace disasm

#endif  // V8_TARGET_ARCH_PPC
