#include "grpc_library_helper.h"

#include <ydb/library/grpc/common/log_context.h>

namespace NKikimr::NConsole {

void SetGRpcLibraryFunction() {
    NYdbGrpc::NGrpcLog::InstallGrpcLibraryLogHook();
}

void EnableGRpcTracersEnable() {
    grpc_tracer_set_enabled("cares_resolver", true);
    grpc_tracer_set_enabled("channel", true);
    grpc_tracer_set_enabled("connectivity_state", true);
    grpc_tracer_set_enabled("sdk_authz", true);
    grpc_tracer_set_enabled("http", true);
    grpc_tracer_set_enabled("http1", true);
    grpc_tracer_set_enabled("tcp", true);
}

void SetGRpcLibraryLogVerbosity(NActors::NLog::EPriority prio) {
    NYdbGrpc::NGrpcLog::SetPrintVerbosity(prio);
    if (prio >= NActors::NLog::EPriority::PRI_DEBUG) {
        EnableGRpcTracersEnable();
    } else if (prio >= NActors::NLog::EPriority::PRI_INFO) {
        EnableGRpcTracersEnable();
    } else if (prio >= NActors::NLog::EPriority::PRI_ERROR) {
        EnableGRpcTracersEnable();
    } else {
        grpc_tracer_set_enabled("all", false);
        grpc_tracer_set_enabled("tcp", true);
    }
}

} // namespace NKikimr::NConsole
