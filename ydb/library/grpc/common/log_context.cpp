#include "log_context.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include <util/datetime/base.h>
#include <util/generic/deque.h>
#include <util/generic/string.h>
#include <util/system/mutex.h>

#include <atomic>
#include <cstring>

#if !defined(_win_)
#include <pthread.h>
#endif

namespace NYdbGrpc::NGrpcLog {
namespace {

constexpr size_t MaxRecentConnectEvents = 64;
constexpr size_t MaxPendingOutgoingTargets = 64;

struct TConnectEvent {
    TInstant Time;
    uint64_t ThreadId = 0;
    bool Incoming = false;
    TString Peer;
    TString Target;
};

class TGrpcLogContext {
public:
    void RegisterOutgoingTarget(TString target) {
        with_lock (Lock_) {
            if (PendingOutgoingTargets_.size() >= MaxPendingOutgoingTargets) {
                PendingOutgoingTargets_.pop_front();
            }
            PendingOutgoingTargets_.push_back(std::move(target));
        }
    }

    void RecordIncomingConnect(TString peer) {
        TConnectEvent event;
        event.Time = TInstant::Now();
        event.ThreadId = CurrentThreadId();
        event.Incoming = true;
        event.Peer = std::move(peer);
        PushRecentEvent(std::move(event));
    }

    void RecordOutgoingConnect(TString peer) {
        TConnectEvent event;
        event.Time = TInstant::Now();
        event.ThreadId = CurrentThreadId();
        event.Incoming = false;
        event.Peer = std::move(peer);
        with_lock (Lock_) {
            if (!PendingOutgoingTargets_.empty()) {
                event.Target = std::move(PendingOutgoingTargets_.front());
                PendingOutgoingTargets_.pop_front();
            }
        }
        PushRecentEvent(std::move(event));
    }

    TString FormatHandshakeContext() const {
        const TInstant now = TInstant::Now();
        const uint64_t threadId = CurrentThreadId();
        const TDuration window = TDuration::Seconds(2);

        with_lock (Lock_) {
            const TConnectEvent* best = nullptr;
            for (const auto& event : RecentEvents_) {
                if (now - event.Time > window) {
                    continue;
                }
                if (!best || event.ThreadId == threadId) {
                    best = &event;
                    if (event.ThreadId == threadId) {
                        break;
                    }
                }
            }
            if (!best) {
                return {};
            }

            TStringBuilder context;
            context << " | connection: direction="
                    << (best->Incoming ? "incoming" : "outgoing")
                    << " peer=" << best->Peer;
            if (!best->Target.empty()) {
                context << " target=" << best->Target;
            }
            return context;
        }
    }

    void ObserveGrpcLogMessage(gpr_log_func_args* args) {
        if (!args || !args->message) {
            return;
        }
        const char* message = args->message;
        TString peer;
        if (ExtractSuffix(message, "SERVER_CONNECT: incoming connection: ", &peer) ||
            ExtractSuffix(message, "SERVER_CONNECT: incoming external connection: ", &peer)) {
            RecordIncomingConnect(std::move(peer));
            return;
        }
        if (ExtractBetween(message, "CLIENT_CONNECT: ", ": asynchronously connecting", &peer)) {
            RecordOutgoingConnect(std::move(peer));
        }
    }

    bool IsHandshakeFailureMessage(const char* message) const {
        return message && std::strstr(message, "Handshake failed") != nullptr;
    }

    bool ShouldPrint(gpr_log_severity severity) const {
        const auto minPriority = static_cast<NActors::NLog::EPriority>(
            PrintVerbosity_.load(std::memory_order_relaxed));
        switch (severity) {
            case GPR_LOG_SEVERITY_DEBUG:
                return minPriority >= NActors::NLog::EPriority::PRI_DEBUG;
            case GPR_LOG_SEVERITY_INFO:
                return minPriority >= NActors::NLog::EPriority::PRI_INFO;
            default:
                return minPriority >= NActors::NLog::EPriority::PRI_ERROR;
        }
    }

    void SetPrintVerbosity(NActors::NLog::EPriority prio) {
        PrintVerbosity_.store(static_cast<int>(prio), std::memory_order_relaxed);
    }

private:
    static uint64_t CurrentThreadId() {
#if defined(_win_)
        return static_cast<uint64_t>(GetCurrentThreadId());
#else
        return static_cast<uint64_t>(pthread_self());
#endif
    }

    static const char* FindSuffixPrefix(const char* message, const char* prefix) {
        const size_t prefixLen = std::strlen(prefix);
        if (std::strncmp(message, prefix, prefixLen) != 0) {
            return nullptr;
        }
        return message + prefixLen;
    }

    static bool ExtractSuffix(const char* message, const char* prefix, TString* out) {
        const char* begin = FindSuffixPrefix(message, prefix);
        if (!begin) {
            return false;
        }
        *out = TString(begin);
        return true;
    }

    static bool ExtractBetween(const char* message, const char* prefix, const char* suffix, TString* out) {
        const char* begin = FindSuffixPrefix(message, prefix);
        if (!begin) {
            return false;
        }
        const char* end = std::strstr(begin, suffix);
        if (!end || end <= begin) {
            return false;
        }
        *out = TString(begin, end - begin);
        return true;
    }

    void PushRecentEvent(TConnectEvent event) {
        with_lock (Lock_) {
            if (RecentEvents_.size() >= MaxRecentConnectEvents) {
                RecentEvents_.pop_front();
            }
            RecentEvents_.push_back(std::move(event));
        }
    }

    mutable TMutex Lock_;
    TDeque<TConnectEvent> RecentEvents_;
    TDeque<TString> PendingOutgoingTargets_;
    std::atomic<int> PrintVerbosity_{static_cast<int>(NActors::NLog::EPriority::PRI_ERROR)};
};

TGrpcLogContext& Context() {
    static TGrpcLogContext context;
    return context;
}

void EnableTcpTracer() {
    grpc_tracer_set_enabled("tcp", true);
}

void GrpcLibraryLogFunction(gpr_log_func_args* args) {
    Context().ObserveGrpcLogMessage(args);

    const char* message = args->message ? args->message : "";
    const bool handshakeFailure = Context().IsHandshakeFailureMessage(message);
    if (!handshakeFailure && !Context().ShouldPrint(args->severity)) {
        return;
    }

    if (handshakeFailure) {
        const TString context = Context().FormatHandshakeContext();
        fprintf(stderr, ":GRPC_LIBRARY ERROR: %s%s\n", message, context.c_str());
        return;
    }

    if (args->severity == GPR_LOG_SEVERITY_DEBUG) {
        fprintf(stderr, ":GRPC_LIBRARY DEBUG: %s\n", message);
    } else if (args->severity == GPR_LOG_SEVERITY_INFO) {
        fprintf(stderr, ":GRPC_LIBRARY INFO: %s\n", message);
    } else {
        fprintf(stderr, ":GRPC_LIBRARY ERROR: %s\n", message);
    }
}

} // namespace

void InstallGrpcLibraryLogHook() {
    EnableTcpTracer();
    gpr_set_log_verbosity(GPR_LOG_SEVERITY_DEBUG);
    gpr_set_log_function(GrpcLibraryLogFunction);
}

void SetPrintVerbosity(NActors::NLog::EPriority prio) {
    Context().SetPrintVerbosity(prio);
    EnableTcpTracer();
    gpr_set_log_verbosity(GPR_LOG_SEVERITY_DEBUG);
}

void RegisterOutgoingClientTarget(const TString& target) {
    Context().RegisterOutgoingTarget(target);
}

} // namespace NYdbGrpc::NGrpcLog
