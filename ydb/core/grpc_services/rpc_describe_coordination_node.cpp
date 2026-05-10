#include "service_coordination.h"
#include <ydb/core/grpc_services/base/base.h>

#include <ydb/public/api/protos/ydb_coordination.pb.h>

#include "rpc_scheme_base.h"
#include "rpc_common/rpc_common.h"

#include <ydb/core/base/tablet_pipe.h>
#include <ydb/core/kesus/tablet/events.h>
#include <ydb/core/protos/flat_tx_scheme.pb.h>
#include <ydb/core/tx/schemeshard/schemeshard.h>
#include <ydb/core/ydb_convert/kesus_description.h>

namespace NKikimr {
namespace NGRpcService {

using namespace NActors;
using namespace NKesus;
using namespace Ydb;

using TEvDescribeCoordinationNode = TGrpcRequestOperationCall<Ydb::Coordination::DescribeNodeRequest,
    Ydb::Coordination::DescribeNodeResponse>;

class TDescribeCoordinationNode : public TRpcSchemeRequestActor<TDescribeCoordinationNode, TEvDescribeCoordinationNode> {
    using TBase = TRpcSchemeRequestActor<TDescribeCoordinationNode, TEvDescribeCoordinationNode>;

public:
    TDescribeCoordinationNode(IRequestOpCtx* msg)
        : TBase(msg)
    {}

    void Bootstrap(const TActorContext &ctx) {
        TBase::Bootstrap(ctx);

        SendProposeRequest(ctx);
        Become(&TDescribeCoordinationNode::StateWork);
    }

    void PassAway() override {
        if (KesusPipeClient_) {
            NTabletPipe::CloseClient(SelfId(), KesusPipeClient_);
            KesusPipeClient_ = {};
        }
        TBase::PassAway();
    }

private:
    static NTabletPipe::TClientConfig GetKesusPipeConfig() {
        NTabletPipe::TClientConfig cfg;
        cfg.RetryPolicy = {
            .RetryLimitCount = 3u
        };
        return cfg;
    }

    void StartFetchSemaphoreNames() {
        WaitingForSemaphoreNames_ = true;
        KesusPipeClient_ = Register(NTabletPipe::CreateClient(SelfId(), KesusTabletId_, GetKesusPipeConfig()));
    }

    void StateWork(TAutoPtr<IEventHandle>& ev) {
        if (WaitingForSemaphoreNames_) {
            switch (ev->GetTypeRewrite()) {
                HFunc(TEvTabletPipe::TEvClientConnected, HandleSemaphorePipeConnected);
                HFunc(TEvTabletPipe::TEvClientDestroyed, HandleSemaphorePipeDestroyed);
                HFunc(TEvKesus::TEvGetConfigResult, HandleGetConfigResult);
                default:
                    TBase::StateWork(ev);
            }
            return;
        }

        switch (ev->GetTypeRewrite()) {
            HFunc(NSchemeShard::TEvSchemeShard::TEvDescribeSchemeResult, Handle);
            default:
                TBase::StateWork(ev);
        }
    }

    void HandleSemaphorePipeConnected(TEvTabletPipe::TEvClientConnected::TPtr& ev, const TActorContext& ctx) {
        if (ev->Get()->Status != NKikimrProto::OK) {
            NYql::TIssues issues;
            issues.AddIssue(MakeIssue(NKikimrIssues::TIssuesIds::DEFAULT_ERROR,
                TStringBuilder() << "Tablet not available, status: " << (ui32)ev->Get()->Status));
            return Reply(Ydb::StatusIds::UNAVAILABLE, issues, ctx);
        }

        auto req = MakeHolder<TEvKesus::TEvGetConfig>();
        req->Record.SetIncludeSemaphoreNames(true);
        NTabletPipe::SendData(SelfId(), KesusPipeClient_, req.Release(), 0);
    }

    void HandleSemaphorePipeDestroyed(TEvTabletPipe::TEvClientDestroyed::TPtr& ev, const TActorContext& ctx) {
        TBase::Handle(ev, ctx);
    }

    void HandleGetConfigResult(TEvKesus::TEvGetConfigResult::TPtr& ev, const TActorContext& ctx) {
        const auto& record = ev->Get()->Record;
        for (const auto& name : record.GetSemaphoreNames()) {
            PendingResult_.add_semaphore_names(name);
        }
        return ReplyWithResult(Ydb::StatusIds::SUCCESS, PendingResult_, ctx);
    }

    void Handle(NSchemeShard::TEvSchemeShard::TEvDescribeSchemeResult::TPtr& ev, const TActorContext& ctx) {
        const auto& record = ev->Get()->GetRecord();
        const auto status = record.GetStatus();
        switch (status) {
            case NKikimrScheme::StatusSuccess: {
                const auto& pathDescription = record.GetPathDescription();
                const auto pathType = pathDescription.GetSelf().GetPathType();

                if (pathType != NKikimrSchemeOp::EPathTypeKesus) {
                    return Reply(Ydb::StatusIds::SCHEME_ERROR, ctx);
                }

                Ydb::Coordination::DescribeNodeResult result;

                if (pathDescription.HasKesus()) {
                    const auto& kesusDescription = pathDescription.GetKesus();
                    FillKesusDescription(result, kesusDescription, pathDescription.GetSelf());
                }

                const auto* req = GetProtoRequest();
                if (req->include_semaphore_names()) {
                    if (!pathDescription.HasKesus()) {
                        return Reply(Ydb::StatusIds::BAD_REQUEST, ctx);
                    }
                    const ui64 tabletId = pathDescription.GetKesus().GetKesusTabletId();
                    if (!tabletId) {
                        return Reply(Ydb::StatusIds::BAD_REQUEST, ctx);
                    }
                    PendingResult_ = std::move(result);
                    KesusTabletId_ = tabletId;
                    StartFetchSemaphoreNames();
                    return;
                }

                return ReplyWithResult(Ydb::StatusIds::SUCCESS, result, ctx);
            }
            case NKikimrScheme::StatusPathDoesNotExist:
            case NKikimrScheme::StatusSchemeError: {
                return Reply(Ydb::StatusIds::SCHEME_ERROR, ctx);
            }
            case NKikimrScheme::StatusAccessDenied: {
                return Reply(Ydb::StatusIds::UNAUTHORIZED, ctx);
            }
            case NKikimrScheme::StatusNotAvailable: {
                return Reply(Ydb::StatusIds::UNAVAILABLE, ctx);
            }
            default: {
                return Reply(Ydb::StatusIds::GENERIC_ERROR, ctx);
            }
        }
    }

    void SendProposeRequest(const TActorContext &ctx) {
        const auto req = GetProtoRequest();

        std::unique_ptr<TEvTxUserProxy::TEvNavigate> navigateRequest(new TEvTxUserProxy::TEvNavigate());
        SetAuthToken(navigateRequest, *Request_);
        SetDatabase(navigateRequest.get(), *Request_);
        NKikimrSchemeOp::TDescribePath* record = navigateRequest->Record.MutableDescribePath();
        record->SetPath(req->path());

        ctx.Send(MakeTxProxyID(), navigateRequest.release());
    }

    Ydb::Coordination::DescribeNodeResult PendingResult_;
    ui64 KesusTabletId_ = 0;
    TActorId KesusPipeClient_;
    bool WaitingForSemaphoreNames_ = false;
};

void DoDescribeCoordinationNode(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider& f) {
    f.RegisterActor(new TDescribeCoordinationNode(p.release()));
}

}
}
