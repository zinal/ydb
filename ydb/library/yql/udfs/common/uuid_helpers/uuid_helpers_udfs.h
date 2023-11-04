#pragma once

#include <ydb/library/yql/public/udf/udf_helpers.h>

#include <util/generic/buffer.h>

namespace {
    using TAutoMapString = NKikimr::NUdf::TAutoMap<char*>;
    using TOptionalString = NKikimr::NUdf::TOptional<char*>;
    using TOptionalByte = NKikimr::NUdf::TOptional<ui8>;
    using TStringRef = NKikimr::NUdf::TStringRef;
    using TUnboxedValue = NKikimr::NUdf::TUnboxedValue;
    using TUnboxedValuePod = NKikimr::NUdf::TUnboxedValuePod;

    struct TSerializeIpVisitor {
        TStringRef operator()(const TIp4& ip) const {
            return TStringRef(reinterpret_cast<const char*>(&ip), 4);
        }
        TStringRef operator()(const TIp6& ip) const {
            return TStringRef(reinterpret_cast<const char*>(&ip.Data), 16);
        }
    };

    SIMPLE_STRICT_UDF(TFromBase64, bool(TOptionalString)) {
        Y_UNUSED(valueBuilder);
        bool result = false;
        if (args[0]) {
            const auto ref = args[0].AsStringRef();
            result = (ref.Size() % 2) > 0;
        }
        return TUnboxedValuePod(result);
    }

    SIMPLE_STRICT_UDF(TToBase64, bool(TOptionalString)) {
        Y_UNUSED(valueBuilder);
        bool result = false;
        if (args[0]) {
            const auto ref = args[0].AsStringRef();
            result = (ref.Size() % 2) > 0;
        }
        return TUnboxedValuePod(result);
    }

#define EXPORTED_UUID_HELPERS_UDF \
    TFromBase64, \
    TToBase64
}
