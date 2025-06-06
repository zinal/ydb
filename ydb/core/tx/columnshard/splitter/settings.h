#pragma once

#include <ydb/library/accessor/accessor.h>

#include <ydb/library/actors/core/log.h>

#include <util/system/types.h>
#include <util/generic/hash.h>
#include <util/generic/string.h>
#include <util/generic/hash_set.h>
#include <set>

namespace NKikimr::NOlap::NSplitter {

class TSplitSettings {
private:
// DefaultMaxBlobSize - 2 * DefaultMinBlobSize have to been enought to "guarantee" records count > 1 through blobs splitting
    static const inline i64 DefaultMaxBlobSize = 8 * 1024 * 1024;
    static const inline i64 DefaultMinBlobSize = 3 * 1024 * 1024;
    static const inline i64 DefaultBlobSizeTolerance = 64 * 1024;

    static const inline i64 DefaultMinRecordsCount = 10000;
    static const inline i64 DefaultMaxPortionSize = 6 * DefaultMaxBlobSize;
    YDB_ACCESSOR(i64, MaxBlobSize, DefaultMaxBlobSize);
    YDB_ACCESSOR(i64, MinBlobSize, DefaultMinBlobSize);
    YDB_ACCESSOR(i64, BlobSizeTolerance, DefaultBlobSizeTolerance);
    YDB_ACCESSOR(i64, MinRecordsCount, DefaultMinRecordsCount);
    YDB_ACCESSOR(i64, MaxPortionSize, DefaultMaxPortionSize);

public:
    TString DebugString() const {
        TStringBuilder sb;
        sb << "{";
        sb << "max_bs=" << MaxBlobSize << ";";
        sb << "min_bs=" << MinBlobSize << ";";
        sb << "bs_tlrn=" << BlobSizeTolerance << ";";
        sb << "min_rc=" << MinRecordsCount << ";";
        sb << "max_ps=" << MaxPortionSize << ";";
        sb << "}";
        return sb;
    }

    ui64 GetExpectedBlobPage() const {
        return ((ui64)512) << 10;
    }

    static TSplitSettings BuildForTests(const double scaleKff = 1) {
        return TSplitSettings().SetMaxBlobSize(1024 * 10 * scaleKff).SetMinBlobSize(256 * 10 * scaleKff);
    }

    ui64 GetExpectedRecordsCountOnPage() const {
        return 1.5 * MinRecordsCount;
    }

    ui64 GetMinRecordsCountOnPage() const {
        return 1.5 * MinRecordsCount;
    }

    ui64 GetMaxRecordsCountOnPage() const {
        return 7.5 * MinRecordsCount;
    }

    ui64 GetExpectedUnpackColumnChunkRawSize() const {
        return (ui64)50 * 1024 * 1024;
    }

    ui64 GetExpectedPortionSize() const {
        return MaxPortionSize;
    }

    ui64 GetExpectedPortionRecordsCount() const {
        return 10 * GetExpectedRecordsCountOnPage();
    }
};

class TGroupFeatures {
private:
    YDB_READONLY_DEF(TString, Name);
    YDB_READONLY_DEF(TSplitSettings, SplitSettings);
    YDB_READONLY_DEF(std::set<ui32>, EntityIds);
public:
    TGroupFeatures(const TString& name, const TSplitSettings& settings, std::set<ui32>&& entities)
        : Name(name)
        , SplitSettings(settings)
        , EntityIds(std::move(entities)) {
        AFL_VERIFY(!!Name);
    }

    TGroupFeatures(const TString& name, const TSplitSettings& settings)
        : Name(name)
        , SplitSettings(settings) {
        AFL_VERIFY(!!Name);
    }

    void AddEntity(const ui32 entityId) {
        AFL_VERIFY(EntityIds.emplace(entityId).second);
    }

    bool IsEmpty() const {
        return EntityIds.empty();
    }

    bool Contains(const ui32 entityId) const {
        return EntityIds.empty() || EntityIds.contains(entityId);
    }
};

class TEntityGroups {
private:
    THashMap<TString, TGroupFeatures> GroupEntities;
    THashSet<ui32> UsedEntityIds;
    TGroupFeatures DefaultGroupFeatures;
public:
    TEntityGroups(const TGroupFeatures& defaultGroup)
        : DefaultGroupFeatures(defaultGroup) {
        AFL_VERIFY(DefaultGroupFeatures.IsEmpty())("problem", "default group cannot be not empty");
    }

    TEntityGroups(const TSplitSettings& splitSettings, const TString& name)
        : DefaultGroupFeatures(name, splitSettings) {

    }

    const TGroupFeatures& GetDefaultGroupFeatures() const {
        return DefaultGroupFeatures;
    }

    bool IsEmpty() const {
        return GroupEntities.empty();
    }

    TGroupFeatures& RegisterGroup(const TString& groupName, const TSplitSettings& settings) {
        auto it = GroupEntities.find(groupName);
        AFL_VERIFY(it == GroupEntities.end());
        return GroupEntities.emplace(groupName, TGroupFeatures(groupName, settings)).first->second;
    }

    TGroupFeatures& MutableGroupVerified(const TString& groupName) {
        auto it = GroupEntities.find(groupName);
        AFL_VERIFY(it != GroupEntities.end());
        return it->second;
    }

    TGroupFeatures* GetGroupOptional(const TString& groupName) {
        auto it = GroupEntities.find(groupName);
        if (it != GroupEntities.end()) {
            return &it->second;
        } else {
            return nullptr;
        }
    }

    void Add(TGroupFeatures&& features, const TString& groupName) {
        for (auto&& i : features.GetEntityIds()) {
            AFL_VERIFY(UsedEntityIds.emplace(i).second);
        }
        AFL_VERIFY(GroupEntities.emplace(groupName, std::move(features)).second);
    }

    THashMap<TString, TGroupFeatures>::const_iterator begin() const {
        return GroupEntities.begin();
    }

    THashMap<TString, TGroupFeatures>::const_iterator end() const {
        return GroupEntities.end();
    }
};
}
