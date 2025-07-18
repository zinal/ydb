#pragma once

#include "public.h"

#include <optional>

#include <yt/yt/client/tablet_client/public.h>

#include <yt/yt/core/ytree/yson_struct.h>

namespace NYT::NBundleControllerClient {

////////////////////////////////////////////////////////////////////////////////

struct TCpuLimits
    : public NYTree::TYsonStruct
{
    std::optional<int> LookupThreadPoolSize;
    std::optional<int> QueryThreadPoolSize;
    std::optional<int> WriteThreadPoolSize;

    REGISTER_YSON_STRUCT(TCpuLimits);

    static void Register(TRegistrar registrar);
};

DEFINE_REFCOUNTED_TYPE(TCpuLimits)

////////////////////////////////////////////////////////////////////////////////

struct TMemoryLimits
    : public NYTree::TYsonStruct
{
    std::optional<i64> CompressedBlockCache;
    std::optional<i64> KeyFilterBlockCache;
    std::optional<i64> LookupRowCache;
    std::optional<i64> TabletDynamic;
    std::optional<i64> TabletStatic;
    std::optional<i64> UncompressedBlockCache;
    std::optional<i64> VersionedChunkMeta;
    std::optional<i64> Reserved;
    std::optional<i64> Query;

    REGISTER_YSON_STRUCT(TMemoryLimits);

    static void Register(TRegistrar registrar);
};

DEFINE_REFCOUNTED_TYPE(TMemoryLimits)

////////////////////////////////////////////////////////////////////////////////

struct TInstanceResources
    : public NYTree::TYsonStruct
{
    i64 Memory;
    // Bits per second.
    // TODO(grachevkirill): Remove this field.
    std::optional<i64> NetBits;
    // Bytes per second.
    std::optional<i64> NetBytes;

    TString Type;
    int Vcpu;

    bool operator==(const TInstanceResources& resources) const;

    void Clear();

    void CanonizeNet();
    void ResetNet();

    REGISTER_YSON_STRUCT(TInstanceResources);

    static void Register(TRegistrar registrar);
};

DEFINE_REFCOUNTED_TYPE(TInstanceResources)

////////////////////////////////////////////////////////////////////////////////

struct TDefaultInstanceConfig
    : public NYTree::TYsonStruct
{
    TCpuLimitsPtr CpuLimits;
    TMemoryLimitsPtr MemoryLimits;

    REGISTER_YSON_STRUCT(TDefaultInstanceConfig);

    static void Register(TRegistrar registrar);
};

DEFINE_REFCOUNTED_TYPE(TDefaultInstanceConfig)

////////////////////////////////////////////////////////////////////////////////

struct TInstanceSize
    : public NYTree::TYsonStruct
{
    TInstanceResourcesPtr ResourceGuarantee;
    TDefaultInstanceConfigPtr DefaultConfig;

    std::optional<TString> HostTagFilter;

    REGISTER_YSON_STRUCT(TInstanceSize);

    static void Register(TRegistrar registrar);
};

DEFINE_REFCOUNTED_TYPE(TInstanceSize)

////////////////////////////////////////////////////////////////////////////////

struct TBundleTargetConfig
    : public NYTree::TYsonStruct
{
    TCpuLimitsPtr CpuLimits;
    TMemoryLimitsPtr MemoryLimits;
    std::optional<int> RpcProxyCount;
    TInstanceResourcesPtr RpcProxyResourceGuarantee;
    std::optional<int> TabletNodeCount;
    TInstanceResourcesPtr TabletNodeResourceGuarantee;

    REGISTER_YSON_STRUCT(TBundleTargetConfig);

    static void Register(TRegistrar registrar);
};

DEFINE_REFCOUNTED_TYPE(TBundleTargetConfig)

////////////////////////////////////////////////////////////////////////////////

struct TBundleConfigConstraints
    : public NYTree::TYsonStruct
{
    std::vector<TInstanceSizePtr> RpcProxySizes;
    std::vector<TInstanceSizePtr> TabletNodeSizes;

    REGISTER_YSON_STRUCT(TBundleConfigConstraints);

    static void Register(TRegistrar registrar);
};

DEFINE_REFCOUNTED_TYPE(TBundleConfigConstraints)

////////////////////////////////////////////////////////////////////////////////

struct TBundleResourceQuota
    : public NYTree::TYsonStruct
{
    int Vcpu;
    i64 Memory;
    // TODO(grachevkirill): Remove it later.
    i64 NetworkBits;
    i64 NetworkBytes;

    REGISTER_YSON_STRUCT(TBundleResourceQuota);

    static void Register(TRegistrar registrar);
};

DEFINE_REFCOUNTED_TYPE(TBundleResourceQuota)

////////////////////////////////////////////////////////////////////////////////

namespace NProto {

////////////////////////////////////////////////////////////////////////////////

void ToProto(NBundleController::NProto::TCpuLimits* protoCpuLimits, const TCpuLimitsPtr cpuLimits);
void FromProto(TCpuLimitsPtr cpuLimits, const NBundleController::NProto::TCpuLimits* protoCpuLimits);

void ToProto(NBundleController::NProto::TMemoryLimits* protoMemoryLimits, const TMemoryLimitsPtr memoryLimits);
void FromProto(TMemoryLimitsPtr memoryLimits, const NBundleController::NProto::TMemoryLimits* protoMemoryLimits);

void ToProto(NBundleController::NProto::TInstanceResources* protoInstanceResources, const TInstanceResourcesPtr instanceResources);
void FromProto(TInstanceResourcesPtr instanceResources, const NBundleController::NProto::TInstanceResources* protoInstanceResources);

void ToProto(NBundleController::NProto::TDefaultInstanceConfig* protoDefaultInstanceConfig, const TDefaultInstanceConfigPtr defaultInstanceConfig);
void FromProto(TDefaultInstanceConfigPtr defaultInstanceConfig, const NBundleController::NProto::TDefaultInstanceConfig* protoDefaultInstanceConfig);

void ToProto(NBundleController::NProto::TInstanceSize* protoInstanceSize, const TInstanceSizePtr instanceSize);
void FromProto(TInstanceSizePtr instanceSize, const NBundleController::NProto::TInstanceSize* protoInstanceSize);

void ToProto(NBundleController::NProto::TBundleConfig* protoBundleConfig, const TBundleTargetConfigPtr bundleConfig);
void FromProto(TBundleTargetConfigPtr bundleConfig, const NBundleController::NProto::TBundleConfig* protoBundleConfig);

void ToProto(NBundleController::NProto::TBundleConfigConstraints* protoBundleConfigConstraints, const TBundleConfigConstraintsPtr bundleConfigConstraints);
void FromProto(TBundleConfigConstraintsPtr bundleConfigConstraints, const NBundleController::NProto::TBundleConfigConstraints* protoBundleConfigConstraints);

void ToProto(NBundleController::NProto::TResourceQuota* protoResourceQuota, const TBundleResourceQuotaPtr resourceQuota);
void FromProto(TBundleResourceQuotaPtr resourceQuota, const NBundleController::NProto::TResourceQuota* protoResourceQuota);

////////////////////////////////////////////////////////////////////////////////

} // namespace NProto

} // namespace NYT::NBundleControllerClient
