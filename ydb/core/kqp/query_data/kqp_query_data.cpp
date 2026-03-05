#include "kqp_query_data.h"

#include <ydb/core/protos/kqp_physical.pb.h>
#include <ydb/public/api/protos/ydb_query.pb.h>
#include <ydb/public/api/protos/ydb_formats.pb.h>
#include <ydb/core/formats/arrow/arrow_batch_builder.h>
#include <ydb/core/kqp/common/result_set_format/kqp_result_set_builders.h>
#include <ydb/core/kqp/common/kqp_row_builder.h>
#include <ydb/core/kqp/common/kqp_types.h>

#include <ydb/library/mkql_proto/mkql_proto.h>
#include <ydb/library/yql/dq/runtime/dq_transport.h>
#include <yql/essentials/core/yql_type_annotation.h>
#include <yql/essentials/minikql/mkql_string_util.h>
#include <yql/essentials/public/udf/udf_data_type.h>
#include <yql/essentials/utils/yql_panic.h>
#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/params/params.h>
#include <ydb/library/yverify_stream/yverify_stream.h>

#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/result/result.h>

namespace NKikimr::NKqp {

using namespace NKikimr::NMiniKQL;
using namespace NYql;
using namespace NYql::NUdf;

namespace {

TString BuildPath(TStringBuf parentPath, TStringBuf childSuffix) {
    return TStringBuilder() << parentPath << childSuffix;
}

TString BuildIndexPath(TStringBuf parentPath, ui32 index) {
    return TStringBuilder() << parentPath << "[" << index << "]";
}

TString DescribeType(const TType* type) {
    switch (type->GetKind()) {
        case TType::EKind::Data: {
            const auto* dataType = static_cast<const TDataType*>(type);
            if (dataType->GetSchemeType() == NUdf::TDataType<NUdf::TDecimal>::Id) {
                const auto [precision, scale] = static_cast<const TDataDecimalType*>(dataType)->GetParams();
                return TStringBuilder() << "Decimal(" << ui32(precision) << "," << ui32(scale) << ")";
            }

            if (const auto slot = dataType->GetDataSlot()) {
                return TStringBuilder() << *slot;
            }

            return TStringBuilder() << "Data(" << dataType->GetSchemeType() << ")";
        }
        case TType::EKind::Pg:
            return TStringBuilder() << "Pg(" << static_cast<const TPgType*>(type)->GetName() << ")";
        case TType::EKind::Resource:
            return TStringBuilder() << "Resource(" << static_cast<const TResourceType*>(type)->GetTag() << ")";
        case TType::EKind::Tagged:
            return TStringBuilder() << "Tagged(" << static_cast<const TTaggedType*>(type)->GetTag() << ")";
        case TType::EKind::Block: {
            const auto shape = static_cast<const TBlockType*>(type)->GetShape();
            return shape == TBlockType::EShape::Scalar ? "Scalar" : "Block";
        }
        case TType::EKind::Linear:
            return static_cast<const TLinearType*>(type)->IsDynamic() ? "Linear(dynamic)" : "Linear(static)";
        default:
            return TString(type->GetKindAsStr());
    }
}

bool GetFirstTypeIncompatibility(const TType* expected, const TType* actual, TStringBuf path, TString& incompatibility) {
    if (expected->GetKind() != actual->GetKind()) {
        incompatibility = TStringBuilder()
            << "first incompatibility at " << path
            << ": expected " << DescribeType(expected)
            << ", actual " << DescribeType(actual);
        return true;
    }

    switch (expected->GetKind()) {
        case TType::EKind::Data: {
            const auto* expectedDataType = static_cast<const TDataType*>(expected);
            const auto* actualDataType = static_cast<const TDataType*>(actual);
            if (expectedDataType->GetSchemeType() != actualDataType->GetSchemeType()) {
                incompatibility = TStringBuilder()
                    << "first incompatibility at " << path
                    << ": expected " << DescribeType(expected)
                    << ", actual " << DescribeType(actual);
                return true;
            }

            if (expectedDataType->GetSchemeType() == NUdf::TDataType<NUdf::TDecimal>::Id) {
                const auto [expectedPrecision, expectedScale] = static_cast<const TDataDecimalType*>(expectedDataType)->GetParams();
                const auto [actualPrecision, actualScale] = static_cast<const TDataDecimalType*>(actualDataType)->GetParams();
                if (expectedPrecision != actualPrecision || expectedScale != actualScale) {
                    incompatibility = TStringBuilder()
                        << "first incompatibility at " << path
                        << ": expected " << DescribeType(expected)
                        << ", actual " << DescribeType(actual);
                    return true;
                }
            }

            return false;
        }

        case TType::EKind::Pg: {
            const auto* expectedPgType = static_cast<const TPgType*>(expected);
            const auto* actualPgType = static_cast<const TPgType*>(actual);
            if (expectedPgType->GetTypeId() != actualPgType->GetTypeId()) {
                incompatibility = TStringBuilder()
                    << "first incompatibility at " << path
                    << ": expected " << DescribeType(expected)
                    << ", actual " << DescribeType(actual);
                return true;
            }
            return false;
        }

        case TType::EKind::Struct: {
            const auto* expectedStruct = static_cast<const TStructType*>(expected);
            const auto* actualStruct = static_cast<const TStructType*>(actual);

            for (ui32 i = 0; i < expectedStruct->GetMembersCount(); ++i) {
                const auto memberName = expectedStruct->GetMemberName(i);
                const auto memberIndex = actualStruct->FindMemberIndex(memberName);
                if (!memberIndex) {
                    incompatibility = TStringBuilder()
                        << "first incompatibility at " << path
                        << ": missing member '" << memberName << "' in actual Struct";
                    return true;
                }

                const TString memberPath = TStringBuilder() << path << "." << memberName;
                if (GetFirstTypeIncompatibility(
                    expectedStruct->GetMemberType(i),
                    actualStruct->GetMemberType(*memberIndex),
                    memberPath,
                    incompatibility))
                {
                    return true;
                }
            }

            for (ui32 i = 0; i < actualStruct->GetMembersCount(); ++i) {
                const auto memberName = actualStruct->GetMemberName(i);
                if (!expectedStruct->FindMemberIndex(memberName)) {
                    incompatibility = TStringBuilder()
                        << "first incompatibility at " << path
                        << ": unexpected member '" << memberName << "' in actual Struct";
                    return true;
                }
            }

            return false;
        }

        case TType::EKind::List:
            return GetFirstTypeIncompatibility(
                static_cast<const TListType*>(expected)->GetItemType(),
                static_cast<const TListType*>(actual)->GetItemType(),
                BuildPath(path, "[]"),
                incompatibility);

        case TType::EKind::Stream:
            return GetFirstTypeIncompatibility(
                static_cast<const TStreamType*>(expected)->GetItemType(),
                static_cast<const TStreamType*>(actual)->GetItemType(),
                BuildPath(path, "[]"),
                incompatibility);

        case TType::EKind::Flow:
            return GetFirstTypeIncompatibility(
                static_cast<const TFlowType*>(expected)->GetItemType(),
                static_cast<const TFlowType*>(actual)->GetItemType(),
                BuildPath(path, "[]"),
                incompatibility);

        case TType::EKind::Optional:
            return GetFirstTypeIncompatibility(
                static_cast<const TOptionalType*>(expected)->GetItemType(),
                static_cast<const TOptionalType*>(actual)->GetItemType(),
                BuildPath(path, "?"),
                incompatibility);

        case TType::EKind::Linear: {
            const auto* expectedLinear = static_cast<const TLinearType*>(expected);
            const auto* actualLinear = static_cast<const TLinearType*>(actual);
            if (expectedLinear->IsDynamic() != actualLinear->IsDynamic()) {
                incompatibility = TStringBuilder()
                    << "first incompatibility at " << path
                    << ": expected " << DescribeType(expected)
                    << ", actual " << DescribeType(actual);
                return true;
            }

            return GetFirstTypeIncompatibility(
                expectedLinear->GetItemType(),
                actualLinear->GetItemType(),
                BuildPath(path, "[]"),
                incompatibility);
        }

        case TType::EKind::Dict: {
            const auto* expectedDict = static_cast<const TDictType*>(expected);
            const auto* actualDict = static_cast<const TDictType*>(actual);
            return GetFirstTypeIncompatibility(
                expectedDict->GetKeyType(),
                actualDict->GetKeyType(),
                BuildPath(path, "{key}"),
                incompatibility)
                || GetFirstTypeIncompatibility(
                    expectedDict->GetPayloadType(),
                    actualDict->GetPayloadType(),
                    BuildPath(path, "{value}"),
                    incompatibility);
        }

        case TType::EKind::Tuple: {
            const auto* expectedTuple = static_cast<const TTupleType*>(expected);
            const auto* actualTuple = static_cast<const TTupleType*>(actual);

            const auto expectedSize = expectedTuple->GetElementsCount();
            const auto actualSize = actualTuple->GetElementsCount();
            const auto commonSize = Min(expectedSize, actualSize);
            for (ui32 i = 0; i < commonSize; ++i) {
                if (GetFirstTypeIncompatibility(
                    expectedTuple->GetElementType(i),
                    actualTuple->GetElementType(i),
                    BuildIndexPath(path, i),
                    incompatibility))
                {
                    return true;
                }
            }

            if (expectedSize != actualSize) {
                incompatibility = TStringBuilder()
                    << "first incompatibility at " << path
                    << ": expected Tuple size " << expectedSize
                    << ", actual size " << actualSize;
                return true;
            }

            return false;
        }

        case TType::EKind::Multi: {
            const auto* expectedMulti = static_cast<const TMultiType*>(expected);
            const auto* actualMulti = static_cast<const TMultiType*>(actual);

            const auto expectedSize = expectedMulti->GetElementsCount();
            const auto actualSize = actualMulti->GetElementsCount();
            const auto commonSize = Min(expectedSize, actualSize);
            for (ui32 i = 0; i < commonSize; ++i) {
                if (GetFirstTypeIncompatibility(
                    expectedMulti->GetElementType(i),
                    actualMulti->GetElementType(i),
                    BuildIndexPath(path, i),
                    incompatibility))
                {
                    return true;
                }
            }

            if (expectedSize != actualSize) {
                incompatibility = TStringBuilder()
                    << "first incompatibility at " << path
                    << ": expected Multi size " << expectedSize
                    << ", actual size " << actualSize;
                return true;
            }

            return false;
        }

        case TType::EKind::Resource: {
            const auto* expectedResource = static_cast<const TResourceType*>(expected);
            const auto* actualResource = static_cast<const TResourceType*>(actual);
            if (expectedResource->GetTagStr() != actualResource->GetTagStr()) {
                incompatibility = TStringBuilder()
                    << "first incompatibility at " << path
                    << ": expected " << DescribeType(expected)
                    << ", actual " << DescribeType(actual);
                return true;
            }
            return false;
        }

        case TType::EKind::Tagged: {
            const auto* expectedTagged = static_cast<const TTaggedType*>(expected);
            const auto* actualTagged = static_cast<const TTaggedType*>(actual);
            if (expectedTagged->GetTagStr() != actualTagged->GetTagStr()) {
                incompatibility = TStringBuilder()
                    << "first incompatibility at " << path
                    << ": expected " << DescribeType(expected)
                    << ", actual " << DescribeType(actual);
                return true;
            }

            return GetFirstTypeIncompatibility(
                expectedTagged->GetBaseType(),
                actualTagged->GetBaseType(),
                path,
                incompatibility);
        }

        case TType::EKind::Variant:
            return GetFirstTypeIncompatibility(
                static_cast<const TVariantType*>(expected)->GetUnderlyingType(),
                static_cast<const TVariantType*>(actual)->GetUnderlyingType(),
                BuildPath(path, "<variant>"),
                incompatibility);

        case TType::EKind::Block: {
            const auto* expectedBlock = static_cast<const TBlockType*>(expected);
            const auto* actualBlock = static_cast<const TBlockType*>(actual);
            if (expectedBlock->GetShape() != actualBlock->GetShape()) {
                incompatibility = TStringBuilder()
                    << "first incompatibility at " << path
                    << ": expected " << DescribeType(expected)
                    << ", actual " << DescribeType(actual);
                return true;
            }

            return GetFirstTypeIncompatibility(
                expectedBlock->GetItemType(),
                actualBlock->GetItemType(),
                BuildPath(path, "[]"),
                incompatibility);
        }

        case TType::EKind::Callable: {
            const auto* expectedCallable = static_cast<const TCallableType*>(expected);
            const auto* actualCallable = static_cast<const TCallableType*>(actual);

            if (expectedCallable->GetNameStr() != actualCallable->GetNameStr()) {
                incompatibility = TStringBuilder()
                    << "first incompatibility at " << path
                    << ": expected callable '" << expectedCallable->GetName()
                    << "', actual callable '" << actualCallable->GetName() << "'";
                return true;
            }

            if (expectedCallable->GetArgumentsCount() != actualCallable->GetArgumentsCount()) {
                incompatibility = TStringBuilder()
                    << "first incompatibility at " << path
                    << ": expected callable arguments count " << expectedCallable->GetArgumentsCount()
                    << ", actual " << actualCallable->GetArgumentsCount();
                return true;
            }

            if (expectedCallable->GetOptionalArgumentsCount() != actualCallable->GetOptionalArgumentsCount()) {
                incompatibility = TStringBuilder()
                    << "first incompatibility at " << path
                    << ": expected optional arguments count " << expectedCallable->GetOptionalArgumentsCount()
                    << ", actual " << actualCallable->GetOptionalArgumentsCount();
                return true;
            }

            if (GetFirstTypeIncompatibility(
                expectedCallable->GetReturnType(),
                actualCallable->GetReturnType(),
                BuildPath(path, "->"),
                incompatibility))
            {
                return true;
            }

            for (ui32 i = 0; i < expectedCallable->GetArgumentsCount(); ++i) {
                if (GetFirstTypeIncompatibility(
                    expectedCallable->GetArgumentType(i),
                    actualCallable->GetArgumentType(i),
                    BuildIndexPath(path, i),
                    incompatibility))
                {
                    return true;
                }
            }

            return false;
        }

        default:
            break;
    }

    if (!expected->IsSameType(*actual)) {
        incompatibility = TStringBuilder()
            << "first incompatibility at " << path
            << ": expected " << DescribeType(expected)
            << ", actual " << DescribeType(actual);
        return true;
    }

    return false;
}

} // namespace

TTypedUnboxedValue TKqpExecuterTxResult::GetUV(
    const NKikimr::NMiniKQL::TTypeEnvironment& typeEnv,
    const NKikimr::NMiniKQL::THolderFactory& factory)
{
    YQL_ENSURE(!Rows.IsWide());
    if (IsStream) {
        auto* listOfItemType = NKikimr::NMiniKQL::TListType::Create(MkqlItemType, typeEnv);

        NUdf::TUnboxedValue* itemsPtr = nullptr;
        auto value = factory.CreateDirectArrayHolder(Rows.RowCount(), itemsPtr);
        Rows.ForEachRow([&](NUdf::TUnboxedValue& value) {
            *itemsPtr++ = std::move(value);
        });
        return {listOfItemType, value};
    } else {
        YQL_ENSURE(Rows.RowCount() == 1, "Actual buffer size: " << Rows.RowCount());
        return {MkqlItemType, *Rows.Head()};
    }
}

NKikimrMiniKQL::TResult* TKqpExecuterTxResult::GetMkql(google::protobuf::Arena* arena) {
    NKikimrMiniKQL::TResult* mkqlResult = google::protobuf::Arena::CreateMessage<NKikimrMiniKQL::TResult>(arena);
    FillMkql(mkqlResult);
    return mkqlResult;
}

NKikimrMiniKQL::TResult TKqpExecuterTxResult::GetMkql() {
    NKikimrMiniKQL::TResult mkqlResult;
    FillMkql(&mkqlResult);
    return mkqlResult;
}

void TKqpExecuterTxResult::FillMkql(NKikimrMiniKQL::TResult* mkqlResult) {
    YQL_ENSURE(!Rows.IsWide());
    if (IsStream) {
        mkqlResult->MutableType()->SetKind(NKikimrMiniKQL::List);
        ExportTypeToProto(
            MkqlItemType,
            *mkqlResult->MutableType()->MutableList()->MutableItem(),
            ColumnOrder);

        Rows.ForEachRow([&](NUdf::TUnboxedValue& value) {
            ExportValueToProto(
                MkqlItemType, value, *mkqlResult->MutableValue()->AddList(),
                ColumnOrder);
        });
    } else {
        YQL_ENSURE(Rows.RowCount() == 1, "Actual buffer size: " << Rows.RowCount());
        ExportTypeToProto(MkqlItemType, *mkqlResult->MutableType(), ColumnOrder);
        ExportValueToProto(MkqlItemType, *Rows.Head(), *mkqlResult->MutableValue());
    }
}

Ydb::ResultSet* TKqpExecuterTxResult::GetYdb(google::protobuf::Arena* arena, const NFormats::TFormatsSettings& settings, bool fillSchema, TMaybe<ui64> rowsLimitPerWrite) {
    Ydb::ResultSet* ydbResult = google::protobuf::Arena::CreateMessage<Ydb::ResultSet>(arena);
    FillYdb(ydbResult, settings, fillSchema, rowsLimitPerWrite);
    return ydbResult;
}

bool TKqpExecuterTxResult::HasTrailingResults() {
    return HasTrailingResult;
}

void TKqpExecuterTxResult::FillYdb(Ydb::ResultSet* ydbResult, const NFormats::TFormatsSettings& settings, bool fillSchema, TMaybe<ui64> rowsLimitPerWrite) {
    NFormats::BuildResultSetFromRows(ydbResult, settings, fillSchema, MkqlItemType,
        Rows, ColumnOrder, ColumnHints, rowsLimitPerWrite);
}

TTxAllocatorState::TTxAllocatorState(const IFunctionRegistry* functionRegistry,
    TIntrusivePtr<ITimeProvider> timeProvider, TIntrusivePtr<IRandomProvider> randomProvider)
    : Alloc(std::make_shared<NKikimr::NMiniKQL::TScopedAlloc>(__LOCATION__, NKikimr::TAlignedPagePoolCounters(), functionRegistry->SupportsSizedAllocators()))
    , TypeEnv(*Alloc)
    , MemInfo("TQueryData")
    , HolderFactory(Alloc->Ref(), MemInfo, functionRegistry)
{
    Alloc->Release();
    TimeProvider = timeProvider;
    RandomProvider = randomProvider;
}

TTxAllocatorState::~TTxAllocatorState()
{
    Alloc->Acquire();
}

std::pair<NKikimr::NMiniKQL::TType*, NUdf::TUnboxedValue> TTxAllocatorState::GetInternalBindingValue(
    const NKqpProto::TKqpPhyParamBinding& paramBinding)
{
    auto& internalBinding = paramBinding.GetInternalBinding();
    switch (internalBinding.GetType()) {
        case NKqpProto::TKqpPhyInternalBinding::PARAM_NOW:
            return {TypeEnv.GetUi64Lazy(), TUnboxedValuePod(ui64(GetCachedNow()))};
        case NKqpProto::TKqpPhyInternalBinding::PARAM_CURRENT_DATE: {
            ui32 date = GetCachedDate();
            YQL_ENSURE(date <= Max<ui32>());
            return {TypeEnv.GetUi32Lazy(), TUnboxedValuePod(ui32(date))};
        }
        case NKqpProto::TKqpPhyInternalBinding::PARAM_CURRENT_DATETIME: {
            ui64 datetime = GetCachedDatetime();
            YQL_ENSURE(datetime <= Max<ui32>());
            return {TypeEnv.GetUi32Lazy(), TUnboxedValuePod(ui32(datetime))};
        }
        case NKqpProto::TKqpPhyInternalBinding::PARAM_CURRENT_TIMESTAMP:
            return {TypeEnv.GetUi64Lazy(), TUnboxedValuePod(ui64(GetCachedTimestamp()))};
        case NKqpProto::TKqpPhyInternalBinding::PARAM_RANDOM_NUMBER:
            return {TypeEnv.GetUi64Lazy(), TUnboxedValuePod(ui64(GetCachedRandom<ui64>()))};
        case NKqpProto::TKqpPhyInternalBinding::PARAM_RANDOM:
            return {NKikimr::NMiniKQL::TDataType::Create(NUdf::TDataType<double>::Id, TypeEnv),
                TUnboxedValuePod(double(GetCachedRandom<double>()))};
        case NKqpProto::TKqpPhyInternalBinding::PARAM_RANDOM_UUID: {
            auto uuid = GetCachedRandom<TGUID>();
            const auto ptr = reinterpret_cast<ui8*>(uuid.dw);
            union {
                ui64 half[2];
                char bytes[16];
            } buf;
            buf.half[0] = *reinterpret_cast<ui64*>(ptr);
            buf.half[1] = *reinterpret_cast<ui64*>(ptr + 8);
            return {NKikimr::NMiniKQL::TDataType::Create(NUdf::TDataType<NUdf::TUuid>::Id, TypeEnv), MakeString(TStringRef(buf.bytes, 16))};
        }
        default:
            YQL_ENSURE(false, "Unexpected internal parameter type: " << (ui32)internalBinding.GetType());
    }
}

TQueryData::TQueryData(const NKikimr::NMiniKQL::IFunctionRegistry* functionRegistry,
    TIntrusivePtr<ITimeProvider> timeProvider, TIntrusivePtr<IRandomProvider> randomProvider)
    : TQueryData(std::make_shared<TTxAllocatorState>(functionRegistry, timeProvider, randomProvider))
{
}

TQueryData::TQueryData(TTxAllocatorState::TPtr allocatorState)
    : AllocState(std::move(allocatorState))
{
}

TQueryData::~TQueryData() {
    {
        auto g = TypeEnv().BindAllocator();
        THashMap<ui32, TVector<TKqpExecuterTxResult>> emptyResultMap;
        TxResults.swap(emptyResultMap);
        TUnboxedParamsMap emptyMap;
        UnboxedData.swap(emptyMap);

        TPartitionedParamMap empty;
        empty.swap(PartitionedParams);
    }
}


const TQueryData::TParamProtobufMap& TQueryData::GetParamsProtobuf() {
    for(auto& [name, _] : UnboxedData) {
        GetParameterTypedValue(name);
    }

    return ParamsProtobuf;
}

NKikimr::NMiniKQL::TType* TQueryData::GetParameterType(const TString& name) {
    auto it = UnboxedData.find(name);
    if (it == UnboxedData.end()) {
        return nullptr;
    }

    return it->second.first;
}

std::pair<NKikimr::NMiniKQL::TType*, NUdf::TUnboxedValue> TQueryData::GetTxResult(ui32 txIndex, ui32 resultIndex) {
    return TxResults[txIndex][resultIndex].GetUV(
        TypeEnv(), AllocState->HolderFactory);
}

NKikimrMiniKQL::TResult* TQueryData::GetMkqlTxResult(const NKqpProto::TKqpPhyResultBinding& rb, google::protobuf::Arena* arena) {
    auto txIndex = rb.GetTxResultBinding().GetTxIndex();
    auto resultIndex = rb.GetTxResultBinding().GetResultIndex();

    YQL_ENSURE(HasResult(txIndex, resultIndex));
    auto g = TypeEnv().BindAllocator();
    return TxResults[txIndex][resultIndex].GetMkql(arena);
}

bool TQueryData::HasTrailingTxResult(const NKqpProto::TKqpPhyResultBinding& rb) {
    auto txIndex = rb.GetTxResultBinding().GetTxIndex();
    auto resultIndex = rb.GetTxResultBinding().GetResultIndex();

    YQL_ENSURE(HasResult(txIndex, resultIndex));
    return TxResults[txIndex][resultIndex].HasTrailingResults();
}

Ydb::ResultSet* TQueryData::GetYdbTxResult(const NKqpProto::TKqpPhyResultBinding& rb, google::protobuf::Arena* arena, const NFormats::TFormatsSettings& formatsSettings, TMaybe<ui64> rowsLimitPerWrite)
{
    auto txIndex = rb.GetTxResultBinding().GetTxIndex();
    auto resultIndex = rb.GetTxResultBinding().GetResultIndex();

    YQL_ENSURE(HasResult(txIndex, resultIndex));

    bool fillSchema = false;
    if (formatsSettings.IsSchemaInclusionAlways()) {
        fillSchema = true;
    } else if (formatsSettings.IsSchemaInclusionFirstOnly()) {
        fillSchema = (BuiltResultIndexes.find(resultIndex) == BuiltResultIndexes.end());
    } else {
        YQL_ENSURE(false, "Unexpected schema inclusion mode");
    }

    AddBuiltResultIndex(resultIndex);

    auto g = TypeEnv().BindAllocator();
    return TxResults[txIndex][resultIndex].GetYdb(arena, formatsSettings, fillSchema, rowsLimitPerWrite);
}

void TQueryData::AddTxResults(ui32 txIndex, TVector<TKqpExecuterTxResult>&& results) {
    auto g = TypeEnv().BindAllocator();
    TxResults.emplace(std::make_pair(txIndex, std::move(results)));
}

void TQueryData::AddTxHolders(TVector<TKqpPhyTxHolder::TConstPtr>&& holders) {
    TxHolders.emplace_back(std::move(holders));
}

void TQueryData::ValidateParameter(const TString& name, const NKikimrMiniKQL::TType& type, NMiniKQL::TTypeEnvironment& txTypeEnv) {
    auto parameterType = GetParameterType(name);
    if (!parameterType) {
        if (type.GetKind() == NKikimrMiniKQL::ETypeKind::Optional) {
            NKikimrMiniKQL::TValue value;
            AddMkqlParam(name, type, value);
            return;
        }
        ythrow yexception() << "Missing value for parameter: " << name;
    }

    auto pType = ImportTypeFromProto(type, txTypeEnv);
    if (pType == nullptr) {
        ythrow yexception() << "Parameter " << name << " type is empty";
    }

    if (!parameterType->IsSameType(*pType)) {
        TString incompatibility;
        GetFirstTypeIncompatibility(pType, parameterType, "$", incompatibility);
        const TString fallback = TStringBuilder() << *pType << ", actual: " << *parameterType;
        ythrow yexception() << "Parameter " << name
            << " type mismatch"
            << (incompatibility ? ": " : ", expected: ")
            << (incompatibility ? incompatibility : fallback);
    }
}

void TQueryData::PrepareParameters(const TKqpPhyTxHolder::TConstPtr& tx, const TPreparedQueryHolder::TConstPtr& preparedQuery,
    NMiniKQL::TTypeEnvironment& txTypeEnv)
{
    if (preparedQuery) {
        for (const auto& paramDesc : preparedQuery->GetParameters()) {
            ValidateParameter(paramDesc.GetName(), paramDesc.GetType(), txTypeEnv);
        }
    }

    if (tx) {
        for(const auto& paramBinding: tx->GetParamBindings()) {
            MaterializeParamValue(true, paramBinding);
        }
    }
}

void TQueryData::CreateKqpValueMap(const TKqpPhyTxHolder::TConstPtr& tx) {
    for (const auto& paramBinding : tx->GetParamBindings()) {
        MaterializeParamValue(true, paramBinding);
    }
}

void TQueryData::ParseParameters(const google::protobuf::Map<TBasicString<char>, Ydb::TypedValue>& params) {
    for(const auto& [name, param] : params) {
        auto success = AddTypedValueParam(name, param);
        YQL_ENSURE(success, "Duplicate parameter: " << name);
    }
}


bool TQueryData::AddUVParam(const TString& name, NKikimr::NMiniKQL::TType* type, const NUdf::TUnboxedValue& value) {
    auto g = TypeEnv().BindAllocator();
    auto [_, success] = UnboxedData.emplace(name, std::make_pair(type, value));
    return success;
}

bool TQueryData::AddTypedValueParam(const TString& name, const Ydb::TypedValue& param) {
    auto guard = TypeEnv().BindAllocator();
    const TBindTerminator bind(this);
    auto [typeFromProto, value] = ImportValueFromProto(
        param.type(), param.value(), TypeEnv(), AllocState->HolderFactory);
    return AddUVParam(name, typeFromProto, value);
}

bool TQueryData::AddMkqlParam(const TString& name, const NKikimrMiniKQL::TType& t, const NKikimrMiniKQL::TValue& v) {
    auto guard = TypeEnv().BindAllocator();
    auto [typeFromProto, value] = ImportValueFromProto(t, v, TypeEnv(), AllocState->HolderFactory);
    return AddUVParam(name, typeFromProto, value);
}

std::pair<NKikimr::NMiniKQL::TType*, NUdf::TUnboxedValue> TQueryData::GetInternalBindingValue(
    const NKqpProto::TKqpPhyParamBinding& paramBinding)
{
    return AllocState->GetInternalBindingValue(paramBinding);
}

TQueryData::TTypedUnboxedValue& TQueryData::GetParameterUnboxedValue(const TString& name) {
    auto it = UnboxedData.find(name);
    YQL_ENSURE(it != UnboxedData.end(), "Param " << name << " not found");
    return it->second;
}

TQueryData::TTypedUnboxedValue* TQueryData::GetParameterUnboxedValuePtr(const TString& name) {
    auto it = UnboxedData.find(name);
    if (it == UnboxedData.end()) {
        return nullptr;
    }

    return &it->second;
}


const Ydb::TypedValue* TQueryData::GetParameterTypedValue(const TString& name) {
    if (UnboxedData.find(name) == UnboxedData.end())
        return nullptr;

    auto it = ParamsProtobuf.find(name);
    if (it == ParamsProtobuf.end()) {
        with_lock(*AllocState->Alloc) {
            const auto& [type, uv] = GetParameterUnboxedValue(name);

            auto& tv = ParamsProtobuf[name];

            ExportTypeToProto(type, *tv.mutable_type());
            ExportValueToProto(type, uv, *tv.mutable_value());

            return &tv;
        }
    }

    return &(it->second);
}

const NKikimr::NMiniKQL::TTypeEnvironment& TQueryData::TypeEnv() {
    return AllocState->TypeEnv;
}


const NKikimr::NMiniKQL::THolderFactory& TQueryData::HolderFactory() {
    return AllocState->HolderFactory;
}

bool TQueryData::MaterializeParamValue(bool ensure, const NKqpProto::TKqpPhyParamBinding& paramBinding) {
    switch (paramBinding.GetTypeCase()) {
        case NKqpProto::TKqpPhyParamBinding::kExternalBinding: {
            const auto* clientParam = GetParameterType(paramBinding.GetName());
            if (clientParam) {
                return true;
            }
            Y_ENSURE(!ensure || clientParam, "Parameter not found: " << paramBinding.GetName());
            return false;
        }
        case NKqpProto::TKqpPhyParamBinding::kTxResultBinding: {
            auto& txResultBinding = paramBinding.GetTxResultBinding();
            auto txIndex = txResultBinding.GetTxIndex();
            auto resultIndex = txResultBinding.GetResultIndex();

            if (HasResult(txIndex, resultIndex)) {
                auto guard = TypeEnv().BindAllocator();
                auto [type, value] = GetTxResult(txIndex, resultIndex);
                AddUVParam(paramBinding.GetName(), type, value);
                return true;
            }

            if (ensure) {
                YQL_ENSURE(HasResult(txIndex, resultIndex));
            }
            return false;
        }
        case NKqpProto::TKqpPhyParamBinding::kInternalBinding: {
            auto guard = TypeEnv().BindAllocator();
            auto [type, value] = GetInternalBindingValue(paramBinding);
            AddUVParam(paramBinding.GetName(), type, value);
            return true;
        }
        default:
            YQL_ENSURE(false, "Unexpected parameter binding type: " << (ui32)paramBinding.GetTypeCase());
    }

    return false;
}

bool TQueryData::AddShardParam(ui64 shardId, const TString& name, NKikimr::NMiniKQL::TType* type, NKikimr::NMiniKQL::TUnboxedValueVector&& value) {
    auto guard = TypeEnv().BindAllocator();
    auto [it, inserted] = PartitionedParams.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(shardId, name),
        std::forward_as_tuple(type, std::move(value)));
    return inserted;
}

void TQueryData::ClearPrunedParams() {
    if (PartitionedParams.empty())
        return;

    auto guard = TypeEnv().BindAllocator();
    for(auto& [key, value]: PartitionedParams) {
        TUnboxedValueVector emptyVector;
        value.Values.swap(emptyVector);
    }

    TPartitionedParamMap emptyMap;
    emptyMap.swap(PartitionedParams);
}

NDqProto::TData TQueryData::GetShardParam(ui64 shardId, const TString& name) {
    auto kv = std::make_pair(shardId, name);
    auto it = PartitionedParams.find(kv);
    if (it == PartitionedParams.end()) {
        return SerializeParamValue(name);
    }

    auto guard = TypeEnv().BindAllocator();
    NDq::TDqDataSerializer dataSerializer{AllocState->TypeEnv, AllocState->HolderFactory, NDqProto::EDataTransportVersion::DATA_TRANSPORT_UV_PICKLE_1_0, EValuePackerVersion::V0};
    NDq::TDqSerializedBatch batch = dataSerializer.Serialize(it->second.Values.begin(), it->second.Values.end(), it->second.ItemType);
    YQL_ENSURE(!batch.IsOOB());
    return batch.Proto;
}

NDqProto::TData TQueryData::SerializeParamValue(const TString& name) {
    auto guard = TypeEnv().BindAllocator();
    const auto& [type, value] = GetParameterUnboxedValue(name);
    return NDq::TDqDataSerializer::SerializeParamValue(type, value);
}

void TQueryData::Clear() {
    {
        auto g = TypeEnv().BindAllocator();
        Params.clear();
        TUnboxedParamsMap emptyMap;
        UnboxedData.swap(emptyMap);

        THashMap<ui32, TVector<TKqpExecuterTxResult>> emptyResultMap;
        TxResults.swap(emptyResultMap);

        for(auto& [key, param]: PartitionedParams) {
            NKikimr::NMiniKQL::TUnboxedValueVector emptyValues;
            param.Values.swap(emptyValues);
        }

        PartitionedParams.clear();

        AllocState->Reset();
    }
}

void TQueryData::Terminate(const char* message) const {
    TStringBuf reason = (message ? TStringBuf(message) : TStringBuf("(unknown)"));
    TString fullMessage = TStringBuilder() <<
        "Terminate was called, reason(" << reason.size() << "): " << reason << Endl;
    AllocState->HolderFactory.CleanupModulesOnTerminate();
    if (std::current_exception()) {
        throw;
    }

    ythrow yexception() << fullMessage;
}

} // namespace NKikimr::NKqp
