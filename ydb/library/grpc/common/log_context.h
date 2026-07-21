#pragma once

#include <ydb/library/actors/core/log_iface.h>

#include <util/generic/string.h>

namespace NYdbGrpc::NGrpcLog {

// Installs a global gRPC log hook used by ydbd.
void InstallGrpcLibraryLogHook();

// Controls which gRPC log messages are printed to stderr.
void SetPrintVerbosity(NActors::NLog::EPriority prio);

// Registers a logical target for the next outgoing TLS client connection.
void RegisterOutgoingClientTarget(const TString& target);

} // namespace NYdbGrpc::NGrpcLog
