// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#include "src/assembler.h"
#include "src/code-stubs.h"
#include "src/compiler/linkage.h"
#include "src/compiler/linkage-impl.h"
#include "src/zone.h"

namespace v8 {
namespace internal {
namespace compiler {

struct LinkageHelperTraits {
  static Register ReturnValueReg() { return r3; }
  static Register ReturnValue2Reg() { return r4; }
  static Register JSCallFunctionReg() { return r4; }
  static Register ContextReg() { return cp; }
  static Register RuntimeCallFunctionReg() { return r4; }
  static Register RuntimeCallArgCountReg() { return r3; }
  static RegList CCalleeSaveRegisters() {
    return r14.bit() | r15.bit() | r16.bit() | r17.bit() | r18.bit() |
           r19.bit() | r20.bit() | r21.bit() | r22.bit() | r23.bit() |
           r24.bit() | r25.bit() | r26.bit() | r27.bit() | r28.bit() |
           r29.bit() | r30.bit() | fp.bit();
  }
  static Register CRegisterParameter(int i) {
    static Register register_parameters[] = {r3, r4, r5, r6, r7, r8, r9, r10};
    return register_parameters[i];
  }
  static int CRegisterParametersLength() { return 8; }
};


CallDescriptor* Linkage::GetJSCallDescriptor(int parameter_count, Zone* zone) {
  return LinkageHelper::GetJSCallDescriptor<LinkageHelperTraits>(
      zone, parameter_count);
}


CallDescriptor* Linkage::GetRuntimeCallDescriptor(
    Runtime::FunctionId function, int parameter_count,
    Operator::Property properties,
    CallDescriptor::DeoptimizationSupport can_deoptimize, Zone* zone) {
  return LinkageHelper::GetRuntimeCallDescriptor<LinkageHelperTraits>(
      zone, function, parameter_count, properties, can_deoptimize);
}


CallDescriptor* Linkage::GetStubCallDescriptor(
    CodeStubInterfaceDescriptor* descriptor, int stack_parameter_count,
    CallDescriptor::DeoptimizationSupport can_deoptimize, Zone* zone) {
  return LinkageHelper::GetStubCallDescriptor<LinkageHelperTraits>(
      zone, descriptor, stack_parameter_count, can_deoptimize);
}


CallDescriptor* Linkage::GetSimplifiedCDescriptor(
    Zone* zone, int num_params, MachineType return_type,
    const MachineType* param_types) {
  return LinkageHelper::GetSimplifiedCDescriptor<LinkageHelperTraits>(
      zone, num_params, return_type, param_types);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
