#pragma once

#include <ydb/library/actors/core/actorsystem_fwd.h>

namespace NYdbGrpc {
class IRequestContextBase;
}

namespace NKikimr::NKesus {

void StartGRpcListSemaphores(NActors::TActorSystem* actorSystem, NYdbGrpc::IRequestContextBase* grpcRequest);

}
