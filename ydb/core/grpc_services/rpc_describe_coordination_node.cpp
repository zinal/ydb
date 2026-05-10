#include "service_coordination.h"
#include <ydb/core/grpc_services/base/base.h>

#include <ydb/core/base/tablet_pipe.h>
#include <ydb/core/kesus/tablet/events.h>
#include <ydb/public/api/protos/ydb_coordination.pb.h>

#include "rpc_scheme_base.h"
#include "rpc_common/rpc_common.h"

#include <ydb/core/base/path.h>
#include <ydb/core/protos/flat_tx_scheme.pb.h>
#include <ydb/core/scheme/scheme_tabledefs.h>
#include <ydb/core/tx/schemeshard/schemeshard.h>
#include <ydb/core/ydb_convert/kesus_description.h>
#include <ydb/library/actors/core/hfunc.h>

namespace NKikimr {
namespace NGRpcService {

using namespace NActors;
using namespace Ydb;
using namespace NKesus;

using TEvDescribeCoordinationNode = TGrpcRequestOperationCall<Ydb::Coordination::DescribeNodeRequest,
    Ydb::Coordination::DescribeNodeResponse>;

class TDescribeCoordinationNode : public TRpcSchemeRequestActor<TDescribeCoordinationNode, TEvDescribeCoordinationNode> {
    using TBase = TRpcSchemeRequestActor<TDescribeCoordinationNode, TEvDescribeCoordinationNode>;

public:
    TDescribeCoordinationNode(IRequestOpCtx* msg)
        : TBase(msg) {}

    void Bootstrap(const TActorContext& ctx) {
        TBase::Bootstrap(ctx);

        SendProposeRequest(ctx);
        Become(&TDescribeCoordinationNode::StateWork);
    }

    void PassAway() override {
        CloseKesusPipe();
        TBase::PassAway();
    }

private:
    STATEFN(StateWork) {
        switch (ev->GetTypeRewrite()) {
            HFunc(NSchemeShard::TEvSchemeShard::TEvDescribeSchemeResult, HandleSchemeDescribe);
            default:
                return TBase::StateWork(ev);
        }
    }

    STATEFN(StateWaitSemaphores) {
        switch (ev->GetTypeRewrite()) {
            hFunc(TEvKesus::TEvDescribeSemaphoresResult, HandleDescribeSemaphoresResult);
            HFunc(TEvTabletPipe::TEvClientConnected, HandleKesusPipeConnected);
            HFunc(TEvTabletPipe::TEvClientDestroyed, HandleKesusPipeDestroyed);
            default:
                return TBase::StateFuncBase(ev);
        }
    }

    void HandleSchemeDescribe(NSchemeShard::TEvSchemeShard::TEvDescribeSchemeResult::TPtr& ev, const TActorContext& ctx) {
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
                const auto mode = req->include_semaphore_info();
                const bool wantSemaphores =
                    mode == Ydb::Coordination::DESCRIBE_NODE_SEMAPHORE_INFO_MODE_BASIC
                    || mode == Ydb::Coordination::DESCRIBE_NODE_SEMAPHORE_INFO_MODE_FULL;

                if (!wantSemaphores || !pathDescription.HasKesus()) {
                    return ReplyWithResult(Ydb::StatusIds::SUCCESS, result, ctx);
                }

                // Same read gate as coordination session DescribeSemaphore (SelectRow on the node).
                const auto& self = pathDescription.GetSelf();
                TIntrusivePtr<TSecurityObject> securityObject;
                if (self.HasOwner()) {
                    securityObject = new TSecurityObject(self.GetOwner(), self.GetEffectiveACL(), false);
                }
                if (!this->CheckAccess(CanonizePath(req->path()), securityObject, NACLib::EAccessRights::SelectRow)) {
                    return;
                }

                const ui64 tabletId = pathDescription.GetKesus().GetKesusTabletId();
                if (!tabletId) {
                    return ReplyWithResult(Ydb::StatusIds::SUCCESS, result, ctx);
                }

                DescribeNodeResult_.Clear();
                DescribeNodeResult_.Swap(&result);
                const bool includeOwnersAndWaiters = mode == Ydb::Coordination::DESCRIBE_NODE_SEMAPHORE_INFO_MODE_FULL;

                NTabletPipe::TClientConfig pipeConfig;
                pipeConfig.RetryPolicy = {.RetryLimitCount = 3u};
                KesusPipeClient_ = ctx.Register(NTabletPipe::CreateClient(ctx.SelfID, tabletId, pipeConfig));

                auto kesReq = MakeHolder<TEvKesus::TEvDescribeSemaphores>(req->path(), includeOwnersAndWaiters);
                NTabletPipe::SendData(SelfId(), KesusPipeClient_, kesReq.Release(), 0);
                Become(&TDescribeCoordinationNode::StateWaitSemaphores);
                return;
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

    void HandleKesusPipeConnected(TEvTabletPipe::TEvClientConnected::TPtr& ev, const TActorContext& ctx) {
        if (ev->Get()->Status != NKikimrProto::OK) {
            NYql::TIssues issues;
            issues.AddIssue(MakeIssue(NKikimrIssues::TIssuesIds::DEFAULT_ERROR,
                TStringBuilder() << "Coordination tablet not available, status: " << static_cast<ui32>(ev->Get()->Status)));
            Reply(Ydb::StatusIds::UNAVAILABLE, issues, ctx);
        }
    }

    void HandleKesusPipeDestroyed(TEvTabletPipe::TEvClientDestroyed::TPtr&, const TActorContext& ctx) {
        NYql::TIssues issues;
        issues.AddIssue(MakeIssue(NKikimrIssues::TIssuesIds::DEFAULT_ERROR,
            TStringBuilder() << "Connection to coordination tablet was lost."));
        Reply(Ydb::StatusIds::UNAVAILABLE, issues, ctx);
    }

    void HandleDescribeSemaphoresResult(TEvKesus::TEvDescribeSemaphoresResult::TPtr& ev) {
        const TActorContext& ctx = TActivationContext::AsActorContext();
        const auto& kesRecord = ev->Get()->Record;
        const auto& kesError = kesRecord.GetError();
        if (kesError.GetStatus() != Ydb::StatusIds::SUCCESS) {
            NYql::TIssues issues;
            for (const auto& issue : kesError.GetIssues()) {
                issues.AddIssue(NYql::TIssue(issue.message()));
            }
            Reply(kesError.GetStatus(), issues, ctx);
            return;
        }

        DescribeNodeResult_.mutable_semaphore_descriptions()->CopyFrom(kesRecord.GetSemaphoreDescriptions());
        ReplyWithResult(Ydb::StatusIds::SUCCESS, DescribeNodeResult_, ctx);
    }

    void CloseKesusPipe() {
        if (KesusPipeClient_) {
            NTabletPipe::CloseClient(SelfId(), KesusPipeClient_);
            KesusPipeClient_ = {};
        }
    }

    void SendProposeRequest(const TActorContext& ctx) {
        const auto req = GetProtoRequest();

        std::unique_ptr<TEvTxUserProxy::TEvNavigate> navigateRequest(new TEvTxUserProxy::TEvNavigate());
        SetAuthToken(navigateRequest, *Request_);
        SetDatabase(navigateRequest.get(), *Request_);
        NKikimrSchemeOp::TDescribePath* record = navigateRequest->Record.MutableDescribePath();
        record->SetPath(req->path());

        ctx.Send(MakeTxProxyID(), navigateRequest.release());
    }

    Ydb::Coordination::DescribeNodeResult DescribeNodeResult_;
    TActorId KesusPipeClient_;
};

void DoDescribeCoordinationNode(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider& f) {
    f.RegisterActor(new TDescribeCoordinationNode(p.release()));
}

}
}
