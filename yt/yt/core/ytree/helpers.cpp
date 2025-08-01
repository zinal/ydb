#include "helpers.h"

#include "attributes.h"
#include "ypath_client.h"

#include <yt/yt/core/misc/error.h>

#include <yt/yt/core/yson/protobuf_helpers.h>

#include <library/cpp/yt/memory/leaky_ref_counted_singleton.h>

namespace NYT::NYTree {

using namespace NYson;

using NYT::FromProto;
using NYT::ToProto;

////////////////////////////////////////////////////////////////////////////////

bool operator == (const IAttributeDictionary& lhs, const IAttributeDictionary& rhs)
{
    auto lhsPairs = lhs.ListPairs();
    auto rhsPairs = rhs.ListPairs();
    if (lhsPairs.size() != rhsPairs.size()) {
        return false;
    }

    std::sort(lhsPairs.begin(), lhsPairs.end(), [] (const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });
    std::sort(rhsPairs.begin(), rhsPairs.end(), [] (const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });

    for (auto index = 0; index < std::ssize(lhsPairs); ++index) {
        if (lhsPairs[index].first != rhsPairs[index].first) {
            return false;
        }
    }

    for (auto index = 0; index < std::ssize(lhsPairs); ++index) {
        auto lhsNode = ConvertToNode(lhsPairs[index].second);
        auto rhsNode = ConvertToNode(rhsPairs[index].second);
        if (!AreNodesEqual(lhsNode, rhsNode)) {
            return false;
        }
    }

    return true;
}

////////////////////////////////////////////////////////////////////////////////

class TEphemeralAttributeDictionary
    : public IAttributeDictionary
{
public:
    explicit TEphemeralAttributeDictionary(std::optional<int> ysonNestingLevelLimit = std::nullopt)
        : NestingLevelLimit_(ysonNestingLevelLimit)
    { }

    std::vector<TKey> ListKeys() const override
    {
        std::vector<TKey> keys;
        keys.reserve(Map_.size());
        for (const auto& [key, value] : Map_) {
            keys.push_back(key);
        }
        return keys;
    }

    std::vector<TKeyValuePair> ListPairs() const override
    {
        std::vector<TKeyValuePair> pairs;
        pairs.reserve(Map_.size());
        for (const auto& pair : Map_) {
            pairs.push_back(pair);
        }
        return pairs;
    }

    TValue FindYson(TKeyView key) const override
    {
        auto it = Map_.find(key);
        return it == Map_.end() ? TYsonString() : it->second;
    }

    void SetYson(TKeyView key, const TValue& value) override
    {
        YT_ASSERT(value.GetType() == EYsonType::Node);
        if (NestingLevelLimit_) {
            ValidateYson(value, *NestingLevelLimit_);
        }
        Map_[key] = value;
    }

    bool Remove(TKeyView key) override
    {
        return Map_.erase(key) > 0;
    }

private:
    THashMap<TKey, TYsonString, THash<std::string_view>, TEqualTo<std::string_view>> Map_;
    std::optional<int> NestingLevelLimit_;
};

IAttributeDictionaryPtr CreateEphemeralAttributes(std::optional<int> ysonNestingLevelLimit)
{
    return New<TEphemeralAttributeDictionary>(ysonNestingLevelLimit);
}

////////////////////////////////////////////////////////////////////////////////

class TEmptyAttributeDictionary
    : public IAttributeDictionary
{
public:
    std::vector<TKey> ListKeys() const override
    {
        return {};
    }

    std::vector<TKeyValuePair> ListPairs() const override
    {
        return {};
    }

    TValue FindYson(TKeyView /*key*/) const override
    {
        return {};
    }

    void SetYson(TKeyView /*key*/, const TValue& /*value*/) override
    {
        YT_ABORT();
    }

    bool Remove(TKeyView /*key*/) override
    {
        return false;
    }

private:
    DECLARE_LEAKY_REF_COUNTED_SINGLETON_FRIEND()
    TEmptyAttributeDictionary() = default;
};

const IAttributeDictionary& EmptyAttributes()
{
    return *LeakyRefCountedSingleton<TEmptyAttributeDictionary>();
}

////////////////////////////////////////////////////////////////////////////////

class TThreadSafeAttributeDictionary
    : public NYTree::IAttributeDictionary
{
public:
    explicit TThreadSafeAttributeDictionary(IAttributeDictionary* underlying)
        : Underlying_(underlying)
    { }

    std::vector<TKey> ListKeys() const override
    {
        auto guard = ReaderGuard(Lock_);
        return Underlying_->ListKeys();
    }

    std::vector<TKeyValuePair> ListPairs() const override
    {
        auto guard = ReaderGuard(Lock_);
        return Underlying_->ListPairs();
    }

    TValue FindYson(TKeyView key) const override
    {
        auto guard = ReaderGuard(Lock_);
        return Underlying_->FindYson(key);
    }

    void SetYson(TKeyView key, const TValue& value) override
    {
        auto guard = WriterGuard(Lock_);
        Underlying_->SetYson(key, value);
    }

    bool Remove(TKeyView key) override
    {
        auto guard = WriterGuard(Lock_);
        return Underlying_->Remove(key);
    }

private:
    IAttributeDictionary* const Underlying_;

    YT_DECLARE_SPIN_LOCK(NThreading::TReaderWriterSpinLock, Lock_);
};

IAttributeDictionaryPtr CreateThreadSafeAttributes(IAttributeDictionary* underlying)
{
    return New<TThreadSafeAttributeDictionary>(underlying);
}

////////////////////////////////////////////////////////////////////////////////

void Serialize(const IAttributeDictionary& attributes, IYsonConsumer* consumer)
{
    auto pairs = attributes.ListPairs();
    std::sort(pairs.begin(), pairs.end(), [] (const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });
    consumer->OnBeginMap();
    for (const auto& [key, value] : pairs) {
        consumer->OnKeyedItem(key);
        consumer->OnRaw(value);
    }
    consumer->OnEndMap();
}

void ToProto(NProto::TAttributeDictionary* protoAttributes, const IAttributeDictionary& attributes)
{
    protoAttributes->Clear();
    auto pairs = attributes.ListPairs();
    std::sort(pairs.begin(), pairs.end(), [] (const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });
    protoAttributes->mutable_attributes()->Reserve(std::ssize(pairs));
    for (const auto& [key, value] : pairs) {
        auto* protoAttribute = protoAttributes->add_attributes();
        protoAttribute->set_key(key);
        protoAttribute->set_value(ToProto(value));
    }
}

IAttributeDictionaryPtr FromProto(const NProto::TAttributeDictionary& protoAttributes)
{
    auto attributes = CreateEphemeralAttributes();
    for (const auto& protoAttribute : protoAttributes.attributes()) {
        auto key = FromProto<TString>(protoAttribute.key());
        auto value = FromProto<TString>(protoAttribute.value());
        attributes->SetYson(key, TYsonString(value));
    }
    return attributes;
}

////////////////////////////////////////////////////////////////////////////////

void TAttributeDictionarySerializer::Save(TStreamSaveContext& context, const IAttributeDictionaryPtr& attributes)
{
    using NYT::Save;

    // Presence byte.
    if (!attributes) {
        Save(context, false);
        return;
    }

    Save(context, true);
    SaveNonNull(context, attributes);
}

void TAttributeDictionarySerializer::SaveNonNull(TStreamSaveContext& context, const IAttributeDictionaryPtr& attributes)
{
    using NYT::Save;
    auto pairs = attributes->ListPairs();
    std::sort(pairs.begin(), pairs.end(), [] (const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });
    TSizeSerializer::Save(context, pairs.size());
    for (const auto& [key, value] : pairs) {
        Save(context, key);
        Save(context, value);
    }
}

void TAttributeDictionarySerializer::Load(TStreamLoadContext& context, IAttributeDictionaryPtr& attributes)
{
    using NYT::Load;

    // We intentionally always recreate attributes from scratch as an ephemeral
    // attribute dictionary. Do not expect any phoenix-like behaviour here.
    attributes = CreateEphemeralAttributes();

    // Presence byte.
    if (!Load<bool>(context)) {
        return;
    }

    LoadNonNull(context, attributes);
}

void TAttributeDictionarySerializer::LoadNonNull(TStreamLoadContext& context, const IAttributeDictionaryPtr& attributes)
{
    using NYT::Load;
    attributes->Clear();
    size_t size = TSizeSerializer::Load(context);
    for (size_t index = 0; index < size; ++index) {
        auto key = Load<TString>(context);
        auto value = Load<TYsonString>(context);
        attributes->SetYson(key, value);
    }
}

////////////////////////////////////////////////////////////////////////////////

void ValidateYTreeKey(IAttributeDictionary::TKeyView key)
{
    Y_UNUSED(key);
    // XXX(vvvv): Disabled due to existing data with empty keys, see https://st.yandex-team.ru/YQL-2640
#if 0
    if (key.empty()) {
        THROW_ERROR_EXCEPTION("Empty keys are not allowed in map nodes");
    }
#endif
}

[[noreturn]] void ThrowYPathResolutionDepthExceeded(TYPathBuf path)
{
    THROW_ERROR_EXCEPTION(
        NYTree::EErrorCode::ResolveError,
        "Path %v exceeds resolve depth limit",
        path)
        << TErrorAttribute("limit", MaxYPathResolveIterations);
}

void ValidateYPathResolutionDepth(TYPathBuf path, int depth)
{
    if (depth > MaxYPathResolveIterations) {
        ThrowYPathResolutionDepthExceeded(path);
    }
}

std::vector<IAttributeDictionary::TKeyValuePair> ListAttributesPairs(const IAttributeDictionary& attributes)
{
    std::vector<IAttributeDictionary::TKeyValuePair> result;
    auto keys = attributes.ListKeys();
    result.reserve(keys.size());
    for (const auto& key : keys) {
        auto value = attributes.FindYson(key);
        if (value) {
            result.push_back(std::pair(key, value));
        }
    }
    return result;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NYTree
