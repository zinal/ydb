#include "grpc_list_semaphores.h"

#include <ydb/core/kesus/proxy/events.h>
#include <ydb/core/kesus/proxy/proxy.h>
#include <ydb/core/kesus/tablet/events.h>

#include <ydb/library/aclib/aclib.h>
#include <ydb/library/actors/core/actor_bootstrapped.h>
#include <ydb/library/actors/core/actorsystem.h>
#include <ydb/library/actors/core/hfunc.h>
#include <ydb/library/grpc/server/grpc_request.h>

#include <ydb/public/api/protos/ydb_coordination.pb.h>

#include <memory>

namespace NKikimr {
namespace NKesus {

namespace {

grpc::StatusCode YdbStatusToGrpcStatus(Ydb::StatusIds::StatusCode status) {
    switch (status) {
        case Ydb::StatusIds::NOT_FOUND:
            return grpc::NOT_FOUND;
        case Ydb::StatusIds::BAD_REQUEST:
            return grpc::INVALID_ARGUMENT;
        case Ydb::StatusIds::UNAUTHORIZED:
            return grpc::PERMISSION_DENIED;
        case Ydb::StatusIds::UNAVAILABLE:
            return grpc::UNAVAILABLE;
        default:
            return grpc::INTERNAL;
    }
}

class TGRpcListSemaphoresActor : public TActorBootstrapped<TGRpcListSemaphoresActor> {
public:
    explicit TGRpcListSemaphoresActor(TIntrusivePtr<NYdbGrpc::IRequestContextBase> grpcRequest)
        : GrpcRequest_(std::move(grpcRequest))
    {}

    void Bootstrap(const TActorContext& ctx) {
        Y_UNUSED(ctx);

        const auto* protoReq = dynamic_cast<const Ydb::Coordination::ListSemaphoresRequest*>(GrpcRequest_->GetRequest());
        if (!protoReq || protoReq->path().empty()) {
            GrpcRequest_->ReplyError(grpc::INVALID_ARGUMENT, "Invalid ListSemaphores request");
            return PassAway();
        }

        KesusPath_ = protoReq->path();
        IncludeDetails_ = protoReq->include_details();

        auto dbVals = GrpcRequest_->GetPeerMetaValues(TStringBuf("x-ydb-database"));
        if (!dbVals.empty()) {
            Database_ = TString{dbVals[0]};
        }

        auto tokenVals = GrpcRequest_->GetPeerMetaValues(TStringBuf("x-ydb-auth-ticket"));
        if (!tokenVals.empty() && !tokenVals[0].empty()) {
            UserToken_ = std::make_unique<NACLib::TUserToken>(TString{tokenVals[0]});
        }

        if (!Send(MakeKesusProxyServiceId(), new TEvKesusProxy::TEvResolveKesusProxy(Database_, KesusPath_))) {
            GrpcRequest_->ReplyError(grpc::UNIMPLEMENTED, "Coordination service not implemented on this server");
            return PassAway();
        }

        Become(&TThis::StateResolve);
    }

private:
    bool CheckAccess(ui32 access) const {
        if (UserToken_) {
            if (!SecurityObject_) {
                return true;
            }
            return SecurityObject_->CheckAccess(access, *UserToken_);
        }
        return true;
    }

    void FailWithKesusError(const NKikimrKesus::TKesusError& err) {
        TString msg = err.IssuesSize() ? TString{err.GetIssues(0).Getmessage()} : TString{"Coordination error"};
        GrpcRequest_->ReplyError(YdbStatusToGrpcStatus(err.GetStatus()), msg);
        PassAway();
    }

    void HandleResolve(const TEvKesusProxy::TEvAttachProxyActor::TPtr& ev) {
        const TActorContext& ctx = ActorContext();
        ProxyActor_ = ev->Get()->ProxyActor;
        SecurityObject_ = ev->Get()->SecurityObject;

        const bool readAllowed = CheckAccess(NACLib::EAccessRights::SelectRow);
        if (!readAllowed) {
            GrpcRequest_->ReplyError(grpc::PERMISSION_DENIED, "Read permission denied");
            return PassAway();
        }

        ctx.Send(ProxyActor_,
            new TEvKesus::TEvListSemaphores(KesusPath_, IncludeDetails_),
            0,
            RequestCookie_);

        Become(&TThis::StateWaitTablet);
    }

    void HandleProxyError(const TEvKesusProxy::TEvProxyError::TPtr& ev) {
        FailWithKesusError(ev->Get()->Error);
    }

    void HandleListResult(const TEvKesus::TEvListSemaphoresResult::TPtr& ev) {
        const auto& record = ev->Get()->Record;
        if (record.GetError().GetStatus() != Ydb::StatusIds::SUCCESS) {
            FailWithKesusError(record.GetError());
            return;
        }

        const auto& items = record.GetSemaphoreDescriptions();
        for (const auto& src : items) {
            auto* msg = google::protobuf::Arena::CreateMessage<Ydb::Coordination::SemaphoreDescription>(GrpcRequest_->GetArena());
            msg->CopyFrom(src);
            GrpcRequest_->Reply(msg, 0);
        }

        GrpcRequest_->FinishStreamingOk();
        PassAway();
    }

    STFUNC(StateResolve) {
        switch (ev->GetTypeRewrite()) {
            hFunc(TEvKesusProxy::TEvProxyError, HandleProxyError);
            hFunc(TEvKesusProxy::TEvAttachProxyActor, HandleResolve);
            default:
                Y_ABORT("Unexpected event 0x%x for TGRpcListSemaphoresActor::StateResolve", ev->GetTypeRewrite());
        }
    }

    STFUNC(StateWaitTablet) {
        switch (ev->GetTypeRewrite()) {
            hFunc(TEvKesusProxy::TEvProxyError, HandleProxyError);
            hFunc(TEvKesus::TEvListSemaphoresResult, HandleListResult);
            default:
                Y_ABORT("Unexpected event 0x%x for TGRpcListSemaphoresActor::StateWaitTablet", ev->GetTypeRewrite());
        }
    }

private:
    TIntrusivePtr<NYdbGrpc::IRequestContextBase> GrpcRequest_;
    TString Database_;
    TString KesusPath_;
    bool IncludeDetails_ = false;
    std::unique_ptr<NACLib::TUserToken> UserToken_;
    TActorId ProxyActor_;
    TIntrusivePtr<TSecurityObject> SecurityObject_;
    static constexpr ui64 RequestCookie_ = 1;
};

} // namespace

void StartGRpcListSemaphores(NActors::TActorSystem* actorSystem, NYdbGrpc::IRequestContextBase* grpcRequest) {
    Y_ABORT_UNLESS(actorSystem);
    Y_ABORT_UNLESS(grpcRequest);
    actorSystem->Register(new TGRpcListSemaphoresActor(TIntrusivePtr<NYdbGrpc::IRequestContextBase>(grpcRequest)));
}

}
}
