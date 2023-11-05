#pragma once

#include <ydb/library/yql/public/udf/udf_data_type.h>
#include <ydb/library/yql/public/udf/udf_helpers.h>
#include <library/cpp/string_utils/base64/base64.h>

#include <util/generic/buffer.h>

namespace {

    using namespace NYql::NUdf;
    using TStringRef = NKikimr::NUdf::TStringRef;
    using TUnboxedValue = NKikimr::NUdf::TUnboxedValue;
    using TUnboxedValuePod = NKikimr::NUdf::TUnboxedValuePod;

    class TToBase64: public NYql::NUdf::TBoxedValue {
    public:
        TToBase64(const TSourcePosition& pos)
            : Pos_(pos)
        {}

        static const TStringRef& Name() {
            static auto name = TStringRef::Of("ToBase64");
            return name;
        }

        static bool DeclareSignature(
            const TStringRef& name,
            TType* userType,
            IFunctionTypeInfoBuilder& builder,
            bool typesOnly) {
            Y_UNUSED(userType);

            if (name != Name()) {
                return false;
            }

            builder.Args()
                ->Add<TAutoMap<TUuid>>()
                .Done()
                .Returns<char*>();

            if (!typesOnly) {
                builder.Implementation(new TToBase64(builder.GetSourcePosition()));
            }
            return true;
        }

    private:
        TUnboxedValue Run(
            const IValueBuilder* valueBuilder,
            const TUnboxedValuePod* args) const final {
            try {
                char output[32];
                const auto input = args[0].AsStringRef();
                if (input.Size() != 16) {
                    return args[0]; // Wrong type, error on call
                }
                memset(output, 0, sizeof(output));
                Base64EncodeUrlNoPadding(output, (unsigned char*) input.Data(), input.Size());
                return valueBuilder->NewString(TStringBuf(&output[0], 22));
            } catch (const std::exception& e) {
                UdfTerminate((TStringBuilder() << Pos_ << " " << e.what()).data());
            }
        }

        const TSourcePosition Pos_;
    };

    class TFromBase64: public NYql::NUdf::TBoxedValue {
    public:
        TFromBase64(const TSourcePosition& pos)
            : Pos_(pos)
        {}

        static const TStringRef& Name() {
            static auto name = TStringRef::Of("FromBase64");
            return name;
        }

        static bool DeclareSignature(
            const TStringRef& name,
            TType* userType,
            IFunctionTypeInfoBuilder& builder,
            bool typesOnly) {
            Y_UNUSED(userType);

            if (name != Name()) {
                return false;
            }

            builder.Args()
                ->Add<char*>()
                .Done()
                .Returns<TAutoMap<TUuid>>();

            if (!typesOnly) {
                builder.Implementation(new TFromBase64(builder.GetSourcePosition()));
            }
            return true;
        }

    private:
        TUnboxedValue Run(
            const IValueBuilder* valueBuilder,
            const TUnboxedValuePod* args) const final {
            try {
                char work[32], output[16];
                const auto input = args[0].AsStringRef();
                if (input.Size() != 22) {
                    return args[0]; // Wrong type, error on call
                }
                memset(work, '=', sizeof(work));
                memcpy(work, input.Data(), 22);
                Base64StrictDecode(output, work, &work[0] + 24);
                return valueBuilder->NewString(TStringBuf(&output[0], 16));
            } catch (const std::exception& e) {
                UdfTerminate((TStringBuilder() << Pos_ << " " << e.what()).data());
            }
        }

        const TSourcePosition Pos_;
    };

}

