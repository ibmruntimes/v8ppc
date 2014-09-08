// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Copyright IBM Corp. 2012, 2013. All rights reserved.

#include "src/v8.h"

#if V8_TARGET_ARCH_PPC

#include "src/interface-descriptors.h"

namespace v8 {
namespace internal {

const Register CallInterfaceDescriptor::ContextRegister() { return cp; }


void CallDescriptors::InitializeForIsolate(Isolate* isolate) {
  InitializeForIsolateAllPlatforms(isolate);

  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::FastNewClosureCall);
    Register registers[] = {cp, r5};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::FastNewContextCall);
    Register registers[] = {cp, r4};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::ToNumberCall);
    Register registers[] = {cp, r3};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::NumberToStringCall);
    Register registers[] = {cp, r3};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::FastCloneShallowArrayCall);
    Register registers[] = {cp, r6, r5, r4};
    Representation representations[] = {
        Representation::Tagged(), Representation::Tagged(),
        Representation::Smi(), Representation::Tagged()};
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::FastCloneShallowObjectCall);
    Register registers[] = {cp, r6, r5, r4, r3};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::CreateAllocationSiteCall);
    Register registers[] = {cp, r5, r6};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::CallFunctionCall);
    Register registers[] = {cp, r4};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::CallConstructCall);
    // r3 : number of arguments
    // r4 : the function to call
    // r5 : feedback vector
    // r6 : (only if r5 is not the megamorphic symbol) slot in feedback
    //      vector (Smi)
    // TODO(turbofan): So far we don't gather type feedback and hence skip the
    // slot parameter, but ArrayConstructStub needs the vector to be undefined.
    Register registers[] = {cp, r3, r4, r5};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::RegExpConstructResultCall);
    Register registers[] = {cp, r5, r4, r3};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::TransitionElementsKindCall);
    Register registers[] = {cp, r3, r4};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor = isolate->call_descriptor(
        CallDescriptorKey::ArrayConstructorConstantArgCountCall);
    // register state
    // cp -- context
    // r3 -- number of arguments
    // r4 -- function
    // r5 -- allocation site with elements kind
    Register registers[] = {cp, r4, r5};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::ArrayConstructorCall);
    // stack param count needs (constructor pointer, and single argument)
    Register registers[] = {cp, r4, r5, r3};
    Representation representations[] = {
        Representation::Tagged(), Representation::Tagged(),
        Representation::Tagged(), Representation::Integer32()};
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
  {
    CallInterfaceDescriptor* descriptor = isolate->call_descriptor(
        CallDescriptorKey::InternalArrayConstructorConstantArgCountCall);
    // register state
    // cp -- context
    // r3 -- number of arguments
    // r4 -- constructor function
    Register registers[] = {cp, r4};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor = isolate->call_descriptor(
        CallDescriptorKey::InternalArrayConstructorCall);
    // stack param count needs (constructor pointer, and single argument)
    Register registers[] = {cp, r4, r3};
    Representation representations[] = {Representation::Tagged(),
                                        Representation::Tagged(),
                                        Representation::Integer32()};
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::CompareNilCall);
    Register registers[] = {cp, r3};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::ToBooleanCall);
    Register registers[] = {cp, r3};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::BinaryOpCall);
    Register registers[] = {cp, r4, r3};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor = isolate->call_descriptor(
        CallDescriptorKey::BinaryOpWithAllocationSiteCall);
    Register registers[] = {cp, r5, r4, r3};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::StringAddCall);
    Register registers[] = {cp, r4, r3};
    descriptor->Initialize(arraysize(registers), registers, NULL);
  }

  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::ArgumentAdaptorCall);
    Register registers[] = {
        cp,  // context
        r4,  // JSFunction
        r3,  // actual number of arguments
        r5,  // expected number of arguments
    };
    Representation representations[] = {
        Representation::Tagged(),     // context
        Representation::Tagged(),     // JSFunction
        Representation::Integer32(),  // actual number of arguments
        Representation::Integer32(),  // expected number of arguments
    };
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::KeyedCall);
    Register registers[] = {
        cp,  // context
        r5,  // key
    };
    Representation representations[] = {
        Representation::Tagged(),  // context
        Representation::Tagged(),  // key
    };
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::NamedCall);
    Register registers[] = {
        cp,  // context
        r5,  // name
    };
    Representation representations[] = {
        Representation::Tagged(),  // context
        Representation::Tagged(),  // name
    };
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::CallHandler);
    Register registers[] = {
        cp,  // context
        r3,  // receiver
    };
    Representation representations[] = {
        Representation::Tagged(),  // context
        Representation::Tagged(),  // receiver
    };
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
  {
    CallInterfaceDescriptor* descriptor =
        isolate->call_descriptor(CallDescriptorKey::ApiFunctionCall);
    Register registers[] = {
        cp,  // context
        r3,  // callee
        r7,  // call_data
        r5,  // holder
        r4,  // api_function_address
    };
    Representation representations[] = {
        Representation::Tagged(),    // context
        Representation::Tagged(),    // callee
        Representation::Tagged(),    // call_data
        Representation::Tagged(),    // holder
        Representation::External(),  // api_function_address
    };
    descriptor->Initialize(arraysize(registers), registers, representations);
  }
}
}
}  // namespace v8::internal

#endif  // V8_TARGET_ARCH_PPC
