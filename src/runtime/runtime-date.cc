// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/runtime/runtime-utils.h"

#include "src/arguments.h"
#include "src/conversions-inl.h"
#include "src/date.h"
#include "src/factory.h"
#include "src/isolate-inl.h"
#include "src/messages.h"

namespace v8 {
namespace internal {

RUNTIME_FUNCTION(Runtime_DateMakeDay) {
  SealHandleScope shs(isolate);
  DCHECK(args.length() == 2);

  CONVERT_SMI_ARG_CHECKED(year, 0);
  CONVERT_SMI_ARG_CHECKED(month, 1);

  int days = isolate->date_cache()->DaysFromYearMonth(year, month);
  RUNTIME_ASSERT(Smi::IsValid(days));
  return Smi::FromInt(days);
}


RUNTIME_FUNCTION(Runtime_DateSetValue) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 3);

  CONVERT_ARG_HANDLE_CHECKED(JSDate, date, 0);
  CONVERT_DOUBLE_ARG_CHECKED(time, 1);
  CONVERT_SMI_ARG_CHECKED(is_utc, 2);

  DateCache* date_cache = isolate->date_cache();

  Handle<Object> value;
  bool is_value_nan = false;
  if (std::isnan(time)) {
    value = isolate->factory()->nan_value();
    is_value_nan = true;
  } else if (!is_utc && (time < -DateCache::kMaxTimeBeforeUTCInMs ||
                         time > DateCache::kMaxTimeBeforeUTCInMs)) {
    value = isolate->factory()->nan_value();
    is_value_nan = true;
  } else {
    time = is_utc ? time : date_cache->ToUTC(static_cast<int64_t>(time));
    if (time < -DateCache::kMaxTimeInMs || time > DateCache::kMaxTimeInMs) {
      value = isolate->factory()->nan_value();
      is_value_nan = true;
    } else {
      value = isolate->factory()->NewNumber(DoubleToInteger(time));
    }
  }
  date->SetValue(*value, is_value_nan);
  return *value;
}


RUNTIME_FUNCTION(Runtime_IsDate) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_ARG_CHECKED(Object, obj, 0);
  return isolate->heap()->ToBoolean(obj->IsJSDate());
}


RUNTIME_FUNCTION(Runtime_ThrowNotDateError) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 0);
  THROW_NEW_ERROR_RETURN_FAILURE(isolate,
                                 NewTypeError(MessageTemplate::kNotDateObject));
}


RUNTIME_FUNCTION(Runtime_DateCurrentTime) {
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());
  return *isolate->factory()->NewNumber(JSDate::CurrentTimeValue(isolate));
}


RUNTIME_FUNCTION(Runtime_DateLocalTimezone) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 1);

  CONVERT_DOUBLE_ARG_CHECKED(x, 0);
  RUNTIME_ASSERT(x >= -DateCache::kMaxTimeBeforeUTCInMs &&
                 x <= DateCache::kMaxTimeBeforeUTCInMs);
  const char* zone =
      isolate->date_cache()->LocalTimezone(static_cast<int64_t>(x));
  Handle<String> result =
      isolate->factory()->NewStringFromUtf8(CStrVector(zone)).ToHandleChecked();
  return *result;
}


RUNTIME_FUNCTION(Runtime_DateCacheVersion) {
  HandleScope hs(isolate);
  DCHECK(args.length() == 0);
  if (isolate->serializer_enabled()) return isolate->heap()->undefined_value();
  if (!isolate->eternal_handles()->Exists(EternalHandles::DATE_CACHE_VERSION)) {
    Handle<FixedArray> date_cache_version =
        isolate->factory()->NewFixedArray(1, TENURED);
    date_cache_version->set(0, Smi::FromInt(0));
    isolate->eternal_handles()->CreateSingleton(
        isolate, *date_cache_version, EternalHandles::DATE_CACHE_VERSION);
  }
  Handle<FixedArray> date_cache_version =
      Handle<FixedArray>::cast(isolate->eternal_handles()->GetSingleton(
          EternalHandles::DATE_CACHE_VERSION));
  // Return result as a JS array.
  Handle<JSObject> result =
      isolate->factory()->NewJSObject(isolate->array_function());
  JSArray::SetContent(Handle<JSArray>::cast(result), date_cache_version);
  return *result;
}


RUNTIME_FUNCTION(Runtime_DateField) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(2, args.length());
  CONVERT_ARG_CHECKED(JSDate, date, 0);
  CONVERT_SMI_ARG_CHECKED(index, 1);
  DCHECK_LE(0, index);
  if (index == 0) return date->value();
  return JSDate::GetField(date, Smi::FromInt(index));
}

}  // namespace internal
}  // namespace v8
