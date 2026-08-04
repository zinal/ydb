#include "rowid_keygen.h"

#include <yql/essentials/public/udf/udf_helpers.h>
#include <yql/essentials/public/udf/udf_value_builder.h>

#include <util/system/datetime.h>

#include <vector>

// Rowid UDF: key-friendly Rowid generators.
//
// Returned values are raw 14-byte Rowid representation.
// Primary-key helpers (layouts from the pk_generation RFC):
//   - newRowKey: shard spread via 12-bit prefix + time locality within a prefix;
//   - newColumnKey: chronological clustering by creation time (seconds);
//   - newRowGroup: batch of row keys sharing a common prefix (Uint64 or Rowid).
//
// Optional dependency arguments [T1, ...] work like RandomUuid(): they control
// when the function is evaluated per row, not the value contents.

using namespace NYql;
using namespace NYql::NUdf;

namespace {

constexpr ui32 MaxDepArgs = 32;

enum class EPrefixArgType {
    None,
    Uint64,
    Rowid,
};

TString BuildDepArgKindsPredicate(TStringBuf argName) {
    return TStringBuilder() << R"(
{cmd=or;value=[
    {cmd=kind;arg=)" << argName << R"(;value=Data};
    {cmd=kind;arg=)" << argName << R"(;value=Optional};
    {cmd=kind;arg=)" << argName << R"(;value=Tuple};
    {cmd=kind;arg=)" << argName << R"(;value=Struct};
    {cmd=kind;arg=)" << argName << R"(;value=List};
    {cmd=kind;arg=)" << argName << R"(;value=Dict};
    {cmd=kind;arg=)" << argName << R"(;value=Stream};
    {cmd=kind;arg=)" << argName << R"(;value=Null};
    {cmd=kind;arg=)" << argName << R"(;value=Void}
]}
)";
}

TString BuildAndDepArgKindsPredicate(ui32 depCount, ui32 firstArgIndex = 0) {
    Y_ENSURE(depCount > 0);
    TStringBuilder sb;
    sb << "{cmd=and;value=[";
    for (ui32 i = 0; i < depCount; ++i) {
        if (i > 0) {
            sb << ";";
        }
        sb << BuildDepArgKindsPredicate(TStringBuilder() << "T" << (firstArgIndex + i));
    }
    sb << "]}";
    return sb;
}

TString BuildCallableTypeWithUniversalDeps(ui32 depCount, EPrefixArgType prefixArg) {
    TStringBuilder sb;
    sb << "[CallableType;[];[];[";
    if (prefixArg != EPrefixArgType::None) {
        const TStringBuf prefixTypeName = prefixArg == EPrefixArgType::Rowid ? "Rowid" : "Uint64";
        sb << "[[DataType;" << prefixTypeName << "]";
        for (ui32 i = 0; i < depCount; ++i) {
            sb << ";[UniversalType]";
        }
        sb << ";[[DataType;Rowid]]]";
    } else {
        for (ui32 i = 0; i < depCount; ++i) {
            sb << "[UniversalType]";
            if (i + 1 < depCount) {
                sb << ";";
            }
        }
        if (depCount > 0) {
            sb << ";";
        }
        sb << "[[DataType;Rowid]]]";
    }
    sb << "]]";
    return sb;
}

TString BuildCallableTypeRowGroup(ui32 depCount, EPrefixArgType prefixArg) {
    Y_ENSURE(prefixArg != EPrefixArgType::None);
    const TStringBuf prefixTypeName = prefixArg == EPrefixArgType::Rowid ? "Rowid" : "Uint64";
    TStringBuilder sb;
    sb << "[CallableType;[];[];[[[DataType;" << prefixTypeName << "];[DataType;Uint64]";
    for (ui32 i = 0; i < depCount; ++i) {
        sb << ";[UniversalType]";
    }
    sb << ";[[ListType;[DataType;Rowid]]]]]";
    return sb;
}

void AppendNoPrefixPolyArgRule(TStringBuilder& sb, ui32 depCount) {
    sb << "[";
    if (depCount == 0) {
        sb << "[]";
    } else {
        sb << BuildAndDepArgKindsPredicate(depCount);
    }
    sb << "; {type=" << BuildCallableTypeWithUniversalDeps(depCount, EPrefixArgType::None) << "}]";
}

void AppendRowGroupPolyArgRule(TStringBuilder& sb, ui32 depCount, EPrefixArgType prefixArg) {
    Y_ENSURE(prefixArg != EPrefixArgType::None);
    const TStringBuf prefixTypeName = prefixArg == EPrefixArgType::Rowid ? "Rowid" : "Uint64";

    sb << "[";
    if (depCount == 0) {
        sb << "{cmd=and;value=["
           << "{cmd=type;arg=T0;value=[DataType;" << prefixTypeName << "]};"
           << "{cmd=type;arg=T1;value=[DataType;Uint64]}"
           << "]}";
    } else {
        sb << "{cmd=and;value=["
           << "{cmd=type;arg=T0;value=[DataType;" << prefixTypeName << "]};"
           << "{cmd=type;arg=T1;value=[DataType;Uint64]}";
        for (ui32 i = 0; i < depCount; ++i) {
            sb << ";" << BuildDepArgKindsPredicate(TStringBuilder() << "T" << (i + 2));
        }
        sb << "]}";
    }
    sb << "; {type=" << BuildCallableTypeRowGroup(depCount, prefixArg) << "}]";
}

TString BuildNoPrefixPolyArgs(TStringBuf errorMessage) {
    TStringBuilder sb;
    sb << "[[";
    bool first = true;
    for (ui32 depCount = MaxDepArgs; depCount > 0; --depCount) {
        if (!first) {
            sb << ";";
        }
        first = false;
        AppendNoPrefixPolyArgRule(sb, depCount);
    }
    if (!first) {
        sb << ";";
    }
    AppendNoPrefixPolyArgRule(sb, 0);
    sb << "; [{cmd=error;message=\"" << errorMessage << "\"}; {}]]";
    return sb;
}

TString BuildRowGroupPolyArgs(TStringBuf errorMessage) {
    TStringBuilder sb;
    sb << "[[";
    bool first = true;
    for (ui32 depCount = MaxDepArgs; depCount > 0; --depCount) {
        if (!first) {
            sb << ";";
        }
        first = false;
        AppendRowGroupPolyArgRule(sb, depCount, EPrefixArgType::Rowid);
        sb << ";";
        AppendRowGroupPolyArgRule(sb, depCount, EPrefixArgType::Uint64);
    }
    if (!first) {
        sb << ";";
    }
    AppendRowGroupPolyArgRule(sb, 0, EPrefixArgType::Rowid);
    sb << ";";
    AppendRowGroupPolyArgRule(sb, 0, EPrefixArgType::Uint64);
    sb << "; [{cmd=error;message=\"" << errorMessage << "\"}; {}]]";
    return sb;
}

ui64 ReadPrefixArg(const TUnboxedValuePod& arg, bool prefixFromRowid) {
    if (prefixFromRowid) {
        const auto ref = arg.AsStringRef();
        if (ref.Size() != NRowidKeyGen::RowidLen) {
            throw std::runtime_error("Expected Rowid value of 14 bytes");
        }
        return NRowidKeyGen::ExtractPrefixFromRowidBytes(
            reinterpret_cast<const ui8*>(ref.Data()));
    }
    return arg.Get<ui64>();
}

bool IsRowidArgType(const ITypeInfoHelper1& typeHelper, const TType* argType) {
    TDataTypeInspector argInspector(typeHelper, argType);
    return argInspector && argInspector.GetTypeId() == NUdf::TDataType<NUdf::TRowid>::Id;
}

TUnboxedValue MakeRowidFromBytes(
    const IValueBuilder* valueBuilder,
    const std::array<ui8, NRowidKeyGen::RowidLen>& bytes)
{
    return valueBuilder->NewString(TStringRef(
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()));
}

TUnboxedValue MakeRowKeyRowidValue(
    const IValueBuilder* valueBuilder, ui64 prefix, bool hasPrefix)
{
    return MakeRowidFromBytes(
        valueBuilder,
        NRowidKeyGen::MakeRowKeyRowidBytes(prefix, Seconds(), hasPrefix));
}

TUnboxedValue MakeColumnKeyRowidValue(const IValueBuilder* valueBuilder) {
    return MakeRowidFromBytes(
        valueBuilder,
        NRowidKeyGen::MakeColumnKeyRowidBytes(Seconds()));
}

enum class EKeyKind {
    RowKey,
    ColumnKey,
};

template <EKeyKind Kind>
class TNewRowid: public TBoxedValue {
public:
    using TTypeAwareMarker = bool;

    explicit TNewRowid(TSourcePosition pos)
        : Pos_(pos)
    {
    }

    static const TStringRef& Name() {
        if constexpr (Kind == EKeyKind::RowKey) {
            static auto name = TStringRef::Of("newRowKey");
            return name;
        } else {
            static auto name = TStringRef::Of("newColumnKey");
            return name;
        }
    }

    static bool DeclareSignature(
        const TStringRef& name,
        TType* userType,
        IFunctionTypeInfoBuilder& builder,
        bool typesOnly)
    {
        if (Name() != name) {
            return false;
        }

        if (!userType) {
            builder.SetError("Missing user type.");
            return true;
        }

        builder.UserType(userType);
        const auto typeHelper = builder.TypeInfoHelper();
        const auto userTypeInspector = TTupleTypeInspector(*typeHelper, userType);
        if (!userTypeInspector || userTypeInspector.GetElementsCount() < 1) {
            builder.SetError("Invalid user type.");
            return true;
        }

        const auto argsTypeTuple = userTypeInspector.GetElementType(0);
        const auto argsTypeInspector = TTupleTypeInspector(*typeHelper, argsTypeTuple);
        if (!argsTypeInspector) {
            builder.SetError("Invalid user type - expected tuple.");
            return true;
        }

        const ui32 argsCount = argsTypeInspector.GetElementsCount();
        if (argsCount > MaxDepArgs) {
            builder.SetError(TStringBuilder() << "Too many dependency arguments: " << argsCount);
            return true;
        }

        auto args = builder.Args(argsCount);
        for (ui32 i = 0; i < argsCount; ++i) {
            args->Add(argsTypeInspector.GetElementType(i));
        }
        args.Done();
        builder.Returns<TDataType<TRowid>>();
        builder.SupportsNullArguments();

        if (!typesOnly) {
            builder.Implementation(new TNewRowid(builder.GetSourcePosition()));
        }
        return true;
    }

private:
    TUnboxedValue Run(
        const IValueBuilder* valueBuilder,
        const TUnboxedValuePod* args) const final try
    {
        Y_UNUSED(args);
        if constexpr (Kind == EKeyKind::RowKey) {
            return MakeRowKeyRowidValue(valueBuilder, 0, false);
        } else {
            return MakeColumnKeyRowidValue(valueBuilder);
        }
    } catch (const std::exception& e) {
        UdfTerminate((TStringBuilder() << Pos_ << " " << e.what()).c_str());
    }

    TSourcePosition Pos_;
};

class TNewRowGroup: public TBoxedValue {
public:
    using TTypeAwareMarker = bool;

    TNewRowGroup(TSourcePosition pos, bool prefixFromRowid)
        : Pos_(pos)
        , PrefixFromRowid_(prefixFromRowid)
    {
    }

    static const TStringRef& Name() {
        static auto name = TStringRef::Of("newRowGroup");
        return name;
    }

    static bool DeclareSignature(
        const TStringRef& name,
        TType* userType,
        IFunctionTypeInfoBuilder& builder,
        bool typesOnly)
    {
        if (Name() != name) {
            return false;
        }

        if (!userType) {
            builder.SetError("Missing user type.");
            return true;
        }

        builder.UserType(userType);
        const auto typeHelper = builder.TypeInfoHelper();
        const auto userTypeInspector = TTupleTypeInspector(*typeHelper, userType);
        if (!userTypeInspector || userTypeInspector.GetElementsCount() < 1) {
            builder.SetError("Invalid user type.");
            return true;
        }

        const auto argsTypeTuple = userTypeInspector.GetElementType(0);
        const auto argsTypeInspector = TTupleTypeInspector(*typeHelper, argsTypeTuple);
        if (!argsTypeInspector || argsTypeInspector.GetElementsCount() < 2) {
            builder.SetError("newRowGroup requires prefix and count arguments.");
            return true;
        }

        const ui32 argsCount = argsTypeInspector.GetElementsCount();
        if (argsCount > 2 + MaxDepArgs) {
            builder.SetError(TStringBuilder() << "Too many dependency arguments: " << (argsCount - 2));
            return true;
        }

        const bool prefixFromRowid = IsRowidArgType(*typeHelper, argsTypeInspector.GetElementType(0));
        auto args = builder.Args(argsCount);
        for (ui32 i = 0; i < argsCount; ++i) {
            args->Add(argsTypeInspector.GetElementType(i));
        }
        args.Done();
        builder.Returns(builder.List()->Item<TDataType<TRowid>>().Build());
        builder.SupportsNullArguments();

        if (!typesOnly) {
            builder.Implementation(new TNewRowGroup(builder.GetSourcePosition(), prefixFromRowid));
        }
        return true;
    }

private:
    TUnboxedValue Run(
        const IValueBuilder* valueBuilder,
        const TUnboxedValuePod* args) const final try
    {
        const ui64 prefix = ReadPrefixArg(args[0], PrefixFromRowid_);
        const ui64 count = args[1].Get<ui64>();
        if (count > NRowidKeyGen::MaxRowGroupCount) {
            throw std::runtime_error(TStringBuilder()
                << "newRowGroup count exceeds limit " << NRowidKeyGen::MaxRowGroupCount);
        }

        std::vector<TUnboxedValue> items;
        items.reserve(count);
        const ui64 epochSeconds = Seconds();
        for (ui64 i = 0; i < count; ++i) {
            items.push_back(MakeRowidFromBytes(
                valueBuilder,
                NRowidKeyGen::MakeRowKeyRowidBytes(prefix, epochSeconds, true)));
        }
        return valueBuilder->NewList(items.data(), items.size());
    } catch (const std::exception& e) {
        UdfTerminate((TStringBuilder() << Pos_ << " " << e.what()).c_str());
    }

    TSourcePosition Pos_;
    bool PrefixFromRowid_ = false;
};

class TRowidModule: public IUdfModule {
public:
    TStringRef Name() const {
        return TStringRef::Of("Rowid");
    }

    void CleanupOnTerminate() const final {
    }

    void GetAllFunctions(IFunctionsSink& sink) const final {
        static const TString newRowKeyPolyArgs = BuildNoPrefixPolyArgs("Unexpected arguments for Rowid::newRowKey");
        static const TString newColumnKeyPolyArgs = BuildNoPrefixPolyArgs("Unexpected arguments for Rowid::newColumnKey");
        static const TString newRowGroupPolyArgs = BuildRowGroupPolyArgs("Unexpected arguments for Rowid::newRowGroup");

        auto newRowKey = sink.Add(TNewRowid<EKeyKind::RowKey>::Name());
        newRowKey->SetTypeAwareness();
        newRowKey->SetPolyArgs(TStringRef(newRowKeyPolyArgs));

        auto newColumnKey = sink.Add(TNewRowid<EKeyKind::ColumnKey>::Name());
        newColumnKey->SetTypeAwareness();
        newColumnKey->SetPolyArgs(TStringRef(newColumnKeyPolyArgs));

        auto newRowGroup = sink.Add(TNewRowGroup::Name());
        newRowGroup->SetTypeAwareness();
        newRowGroup->SetPolyArgs(TStringRef(newRowGroupPolyArgs));
    }

    void BuildFunctionTypeInfo(
        const TStringRef& name,
        TType* userType,
        const TStringRef& typeConfig,
        ui32 flags,
        IFunctionTypeInfoBuilder& builder) const override
    {
        Y_UNUSED(typeConfig);
        try {
            const bool typesOnly = (flags & TFlags::TypesOnly);
            if (TNewRowid<EKeyKind::RowKey>::DeclareSignature(name, userType, builder, typesOnly)) {
                return;
            }
            if (TNewRowid<EKeyKind::ColumnKey>::DeclareSignature(name, userType, builder, typesOnly)) {
                return;
            }
            if (TNewRowGroup::DeclareSignature(name, userType, builder, typesOnly)) {
                return;
            }
            ythrow yexception() << "Unknown function name: " << TStringBuf(name);
        } catch (const std::exception& e) {
            builder.SetError(CurrentExceptionMessage());
        }
    }
};

} // namespace

REGISTER_MODULES(TRowidModule)
