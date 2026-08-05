#include "service_coordination.h"
#include <ydb/core/grpc_services/base/base.h>

#include <ydb/public/api/protos/ydb_coordination.pb.h>

#include "rpc_common/rpc_common.h"

#include <ydb/core/base/path.h>
#include <ydb/core/base/tablet_pipe.h>
#include <ydb/core/kesus/tablet/events.h>
#include <ydb/core/tx/scheme_cache/scheme_cache.h>
#include <ydb/core/tx/schemeshard/schemeshard.h>
#include <ydb/core/tx/tx_proxy/proxy.h>

namespace NKikimr {
namespace NGRpcService {

using namespace NActors;
using namespace Ydb;

using TEvListSemaphoresCoordinationNode = TGrpcRequestOperationCall<
    Ydb::Coordination::ListSemaphoresRequest,
    Ydb::Coordination::ListSemaphoresResponse>;

class TListSemaphoresCoordinationNode
    : public TRpcOperationRequestActor<TListSemaphoresCoordinationNode, TEvListSemaphoresCoordinationNode>
{
    using TBase = TRpcOperationRequestActor<TListSemaphoresCoordinationNode, TEvListSemaphoresCoordinationNode>;

public:
    TListSemaphoresCoordinationNode(IRequestOpCtx* msg)
        : TBase(msg)
    {}

    void Bootstrap(const TActorContext& ctx) {
        TBase::Bootstrap(ctx);

        const auto req = GetProtoRequest();
        const TString path = req->path();

        if (path.empty()) {
            return Reply(Ydb::StatusIds::BAD_REQUEST, "Empty path.", NKikimrIssues::TIssuesIds::GENERIC_RESOLVE_ERROR, ctx);
        }

        TVector<TString> pathComponents = SplitPath(path);
        if (pathComponents.empty()) {
            return Reply(Ydb::StatusIds::BAD_REQUEST, "Invalid path.", NKikimrIssues::TIssuesIds::GENERIC_RESOLVE_ERROR, ctx);
        }

        auto navigateRequest = MakeHolder<NSchemeCache::TSchemeCacheNavigate>();
        navigateRequest->DatabaseName = Request_->GetDatabaseName().GetOrElse("");
        navigateRequest->ResultSet.emplace_back();
        navigateRequest->ResultSet.back().Path.swap(pathComponents);
        navigateRequest->ResultSet.back().Operation = NSchemeCache::TSchemeCacheNavigate::OpPath;

        Send(MakeSchemeCacheID(), new TEvTxProxySchemeCache::TEvNavigateKeySet(navigateRequest.Release()), 0, 0);
        Become(&TListSemaphoresCoordinationNode::StateResolve);
    }

private:
    STFUNC(StateResolve) {
        switch (ev->GetTypeRewrite()) {
            hFunc(TEvTxProxySchemeCache::TEvNavigateKeySetResult, HandleResolve);
            default:
                return TBase::StateFuncBase(ev);
        }
    }

    void HandleResolve(TEvTxProxySchemeCache::TEvNavigateKeySetResult::TPtr& ev) {
        THolder<NSchemeCache::TSchemeCacheNavigate> navigate = std::move(ev->Get()->Request);
        const auto& ctx = TActivationContext::AsActorContext();

        if (navigate->ResultSet.size() != 1 || navigate->ErrorCount > 0) {
            return Reply(Ydb::StatusIds::SCHEME_ERROR, ctx);
        }

        const auto& entry = navigate->ResultSet.front();
        if (entry.Status != NSchemeCache::TSchemeCacheNavigate::EStatus::Ok) {
            return Reply(Ydb::StatusIds::SCHEME_ERROR, ctx);
        }

        if (entry.Kind != NSchemeCache::TSchemeCacheNavigate::KindKesus) {
            return Reply(Ydb::StatusIds::BAD_REQUEST, "Path is not a coordination node path.",
                NKikimrIssues::TIssuesIds::GENERIC_RESOLVE_ERROR, ctx);
        }

        if (!entry.KesusInfo) {
            return Reply(Ydb::StatusIds::BAD_REQUEST, "No coordination node info found.",
                NKikimrIssues::TIssuesIds::GENERIC_RESOLVE_ERROR, ctx);
        }

        KesusTabletId = entry.KesusInfo->Description.GetKesusTabletId();
        if (!KesusTabletId) {
            return Reply(Ydb::StatusIds::BAD_REQUEST, "No coordination node id found.",
                NKikimrIssues::TIssuesIds::GENERIC_RESOLVE_ERROR, ctx);
        }

        KesusPath = CanonizePath(GetProtoRequest()->path());

        NTabletPipe::TClientConfig pipeConfig;
        pipeConfig.RetryPolicy = {.RetryLimitCount = 3u};
        KesusPipeClient = Register(NTabletPipe::CreateClient(SelfId(), KesusTabletId, pipeConfig));

        auto request = MakeHolder<TEvKesus::TEvListSemaphores>(KesusPath);
        NTabletPipe::SendData(SelfId(), KesusPipeClient, request.Release(), 0);

        Become(&TListSemaphoresCoordinationNode::StateWork);
    }

    STFUNC(StateWork) {
        switch (ev->GetTypeRewrite()) {
            hFunc(TEvTabletPipe::TEvClientConnected, HandlePipeConnected);
            hFunc(TEvTabletPipe::TEvClientDestroyed, HandlePipeDestroyed);
            hFunc(TEvKesus::TEvListSemaphoresResult, HandleListSemaphoresResult);
            default:
                return TBase::StateFuncBase(ev);
        }
    }

    void HandlePipeConnected(TEvTabletPipe::TEvClientConnected::TPtr& ev) {
        const auto& ctx = TActivationContext::AsActorContext();
        if (ev->Get()->Status != NKikimrProto::OK) {
            return Reply(Ydb::StatusIds::UNAVAILABLE, "Failed to connect to coordination node.",
                NKikimrIssues::TIssuesIds::SHARD_NOT_AVAILABLE, ctx);
        }
    }

    void HandlePipeDestroyed(TEvTabletPipe::TEvClientDestroyed::TPtr&) {
        const auto& ctx = TActivationContext::AsActorContext();
        return Reply(Ydb::StatusIds::UNAVAILABLE, "Connection to coordination node was lost.",
            NKikimrIssues::TIssuesIds::SHARD_NOT_AVAILABLE, ctx);
    }

    void HandleListSemaphoresResult(TEvKesus::TEvListSemaphoresResult::TPtr& ev) {
        const auto& ctx = TActivationContext::AsActorContext();
        const auto& record = ev->Get()->Record;

        if (record.GetError().GetStatus() != Ydb::StatusIds::SUCCESS) {
            return Reply(record.GetError().GetStatus(), record.GetError().GetIssues(), ctx);
        }

        Ydb::Coordination::ListSemaphoresResult result;
        for (const auto& name : record.GetSemaphoreNames()) {
            result.add_semaphore_names(name);
        }

        Request_->SendResult(result, Ydb::StatusIds::SUCCESS);
        PassAway();
    }

    void PassAway() override {
        if (KesusPipeClient) {
            NTabletPipe::CloseClient(SelfId(), KesusPipeClient);
            KesusPipeClient = {};
        }
        TBase::PassAway();
    }

private:
    ui64 KesusTabletId = 0;
    TActorId KesusPipeClient;
    TString KesusPath;
};

void DoListSemaphoresCoordinationNode(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider& f) {
    f.RegisterActor(new TListSemaphoresCoordinationNode(p.release()));
}

}
}
