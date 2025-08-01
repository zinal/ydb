#pragma once

#define INCLUDE_READ_SESSION_IMPL_H
#include "read_session_impl.h"
#undef INCLUDE_READ_SESSION_IMPL_H

#include "direct_reader.h"

// #include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/topic/control_plane.h>

#include <ydb/public/sdk/cpp/src/client/topic/common/log_lazy.h>

#define INCLUDE_YDB_INTERNAL_H
#include <ydb/public/sdk/cpp/src/client/impl/ydb_internal/logger/log.h>
#undef INCLUDE_YDB_INTERNAL_H

#include <google/protobuf/util/time_util.h>

#include <library/cpp/containers/disjoint_interval_tree/disjoint_interval_tree.h>

#include <util/generic/guid.h>
#include <util/generic/size_literals.h>
#include <util/generic/utility.h>
#include <util/generic/yexception.h>
#include <util/stream/mem.h>

#include <utility>
#include <variant>


namespace NYdb::inline Dev::NTopic {

static const bool RangesMode = !std::string{std::getenv("PQ_OFFSET_RANGES_MODE") ? std::getenv("PQ_OFFSET_RANGES_MODE") : ""}.empty();
static const bool ExperimentalDirectRead = !std::string{std::getenv("PQ_EXPERIMENTAL_DIRECT_READ") ? std::getenv("PQ_EXPERIMENTAL_DIRECT_READ") : ""}.empty();


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TPartitionStreamImpl

template<bool UseMigrationProtocol>
TLog TPartitionStreamImpl<UseMigrationProtocol>::GetLog() const {
    if (auto session = CbContext->LockShared()) {
        return session->GetLog();
    }
    return {};
}

template<bool UseMigrationProtocol>
void TPartitionStreamImpl<UseMigrationProtocol>::Commit(ui64 startOffset, ui64 endOffset) {
    std::vector<std::pair<ui64, ui64>> toCommit;
    if (auto sessionShared = CbContext->LockShared()) {
        Y_ABORT_UNLESS(endOffset > startOffset);
        {
            std::lock_guard guard(sessionShared->Lock);
            if (!AddToCommitRanges(startOffset, endOffset, true)) // Add range for real commit always.
                return;

            Y_ABORT_UNLESS(!Commits.Empty());
            for (auto c : Commits) {
                if (c.first >= endOffset) break; // Commit only gaps before client range.
                toCommit.emplace_back(c);
            }
            Commits.EraseInterval(0, endOffset); // Drop only committed ranges;
        }
        for (auto range: toCommit) {
            sessionShared->Commit(this, range.first, Min(range.second, endOffset));
        }
    }
}

template<bool UseMigrationProtocol>
void TPartitionStreamImpl<UseMigrationProtocol>::RequestStatus() {
    if (auto sessionShared = CbContext->LockShared()) {
        sessionShared->RequestPartitionStreamStatus(this);
    }
}

template<bool UseMigrationProtocol>
void TPartitionStreamImpl<UseMigrationProtocol>::ConfirmCreate(std::optional<ui64> readOffset, std::optional<ui64> commitOffset) {
    if (auto sessionShared = CbContext->LockShared()) {
        if (commitOffset.has_value()) {
            SetFirstNotReadOffset(commitOffset.value());
        }
        sessionShared->ConfirmPartitionStreamCreate(this, readOffset, commitOffset);
    }
}

template<bool UseMigrationProtocol>
void TPartitionStreamImpl<UseMigrationProtocol>::ConfirmDestroy() {
    if (auto sessionShared = CbContext->LockShared()) {
        sessionShared->ConfirmPartitionStreamDestroy(this);
    }
}

template<bool UseMigrationProtocol>
void TPartitionStreamImpl<UseMigrationProtocol>::ConfirmEnd(const std::vector<ui32>& childIds) {
    if (auto sessionShared = CbContext->LockShared()) {
        sessionShared->ConfirmPartitionStreamEnd(this, childIds);
    }
}

template<bool UseMigrationProtocol>
void TPartitionStreamImpl<UseMigrationProtocol>::StopReading() {
    Y_ABORT("Not implemented"); // TODO
}

template<bool UseMigrationProtocol>
void TPartitionStreamImpl<UseMigrationProtocol>::ResumeReading() {
    Y_ABORT("Not implemented"); // TODO
}

template<bool UseMigrationProtocol>
void TPartitionStreamImpl<UseMigrationProtocol>::SignalReadyEvents(TIntrusivePtr<TPartitionStreamImpl<UseMigrationProtocol>> stream,
                                                                   TReadSessionEventsQueue<UseMigrationProtocol>* queue,
                                                                   TDeferredActions<UseMigrationProtocol>& deferred)
{
    Y_ABORT_UNLESS(queue);

    stream->EventsQueue.SignalReadyEvents(stream, *queue, deferred);
}

template<bool UseMigrationProtocol>
void TPartitionStreamImpl<UseMigrationProtocol>::DeleteNotReadyTail(TDeferredActions<UseMigrationProtocol>& deferred)
{
    EventsQueue.DeleteNotReadyTail(deferred);
}

template <bool UseMigrationProtocol>
void TPartitionStreamImpl<UseMigrationProtocol>::GetDataEventImpl(TIntrusivePtr<TPartitionStreamImpl<UseMigrationProtocol>> partitionStream,
                                                                  size_t& maxEventsCount,
                                                                  size_t& maxByteSize,
                                                                  std::vector<typename TADataReceivedEvent<UseMigrationProtocol>::TMessage>& messages,
                                                                  std::vector<typename TADataReceivedEvent<UseMigrationProtocol>::TCompressedMessage>& compressedMessages,
                                                                  TUserRetrievedEventsInfoAccumulator<UseMigrationProtocol>& accumulator)
{
    partitionStream->EventsQueue.GetDataEventImpl(partitionStream,
                                                  maxEventsCount,
                                                  maxByteSize,
                                                  messages,
                                                  compressedMessages,
                                                  accumulator);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TRawPartitionStreamEventQueue

template<bool UseMigrationProtocol>
void TRawPartitionStreamEventQueue<UseMigrationProtocol>::SignalReadyEvents(TIntrusivePtr<TPartitionStreamImpl<UseMigrationProtocol>> stream,
                                                                            TReadSessionEventsQueue<UseMigrationProtocol>& queue,
                                                                            TDeferredActions<UseMigrationProtocol>& deferred)
{
    if constexpr (!UseMigrationProtocol) {
        if (auto session = CbContext->LockShared()) {
            if (!session->AllParentSessionsHasBeenRead(stream->GetPartitionId(), stream->GetPartitionSessionId())) {
                return;
            }
        }
    }

    auto moveToReadyQueue = [&](TRawPartitionStreamEvent<UseMigrationProtocol> &&event) {
        queue.SignalEventImpl(stream, deferred, event.IsDataEvent());

        Ready.push_back(std::move(event));
        NotReady.pop_front();
    };

    while (!NotReady.empty() && NotReady.front().IsReady()) {
        auto& front = NotReady.front();

        if (front.IsDataEvent()) {
            if (queue.HasDataEventCallback()) {
                std::vector<typename TADataReceivedEvent<UseMigrationProtocol>::TMessage> messages;
                std::vector<typename TADataReceivedEvent<UseMigrationProtocol>::TCompressedMessage> compressedMessages;
                TUserRetrievedEventsInfoAccumulator<UseMigrationProtocol> accumulator;
                auto maxEventsCount = Max<size_t>();
                auto maxByteSize = Max<size_t>();

                queue.GetDataEventCallbackSettings(maxByteSize);

                TRawPartitionStreamEventQueue<UseMigrationProtocol>::GetDataEventImpl(stream,
                                                                                      maxEventsCount,
                                                                                      maxByteSize,
                                                                                      messages,
                                                                                      compressedMessages,
                                                                                      accumulator,
                                                                                      NotReady);

                TADataReceivedEvent<UseMigrationProtocol> data(std::move(messages),
                                                              std::move(compressedMessages),
                                                              stream);

                queue.ApplyCallbackToEventImpl(data, std::move(accumulator), deferred);
            } else {
                moveToReadyQueue(std::move(front));
            }
        } else {
            if (queue.TryApplyCallbackToEventImpl(front.GetEvent(), deferred, CbContext)) {
                NotReady.pop_front();
            } else {
                moveToReadyQueue(std::move(front));
            }
        }
    }
}

template<bool UseMigrationProtocol>
void TRawPartitionStreamEventQueue<UseMigrationProtocol>::DeleteNotReadyTail(TDeferredActions<UseMigrationProtocol>& deferred)
{
    std::deque<TRawPartitionStreamEvent<UseMigrationProtocol>> ready;

    auto i = NotReady.begin();
    for (; (i != NotReady.end()) && i->IsReady(); ++i) {
        ready.push_back(std::move(*i));
    }

    std::vector<TDataDecompressionInfoPtr<UseMigrationProtocol>> infos;

    for (; i != NotReady.end(); ++i) {
        if (i->IsDataEvent()) {
            infos.push_back(i->GetDataEvent().GetParent());
        }
    }

    deferred.DeferDestroyDecompressionInfos(std::move(infos));

    swap(ready, NotReady);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TDecompressionQueueItem

template <bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::TDecompressionQueueItem::OnDestroyReadSession()
{
    BatchInfo->OnDestroyReadSession();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TSingleClusterReadSessionImpl

template<bool UseMigrationProtocol>
TSingleClusterReadSessionImpl<UseMigrationProtocol>::~TSingleClusterReadSessionImpl() {
    for (auto&& [_, partitionStream] : PartitionStreams) {
        partitionStream->ClearQueue();
    }

    for (auto& e : DecompressionQueue) {
        e.OnDestroyReadSession();
    }
}

template<bool UseMigrationProtocol>
TStringBuilder TSingleClusterReadSessionImpl<UseMigrationProtocol>::GetLogPrefix() const {
    return TStringBuilder() << GetDatabaseLogPrefix(Database) << "[" << SessionId << "] [" << ClusterName << "] ";
}

template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::Start() {
    Y_ABORT_UNLESS(this->SelfContext);
    Settings.DecompressionExecutor_->Start();
    Settings.EventHandlers_.HandlersExecutor_->Start();
    if (!Reconnect(TPlainStatus())) {
        AbortSession(EStatus::ABORTED, "Driver is stopping");
    }
}

template<bool UseMigrationProtocol>
bool TSingleClusterReadSessionImpl<UseMigrationProtocol>::Reconnect(const TPlainStatus& status) {
    TDuration delay = TDuration::Zero();

    // Previous operations contexts.
    NYdbGrpc::IQueueClientContextPtr prevConnectContext;
    NYdbGrpc::IQueueClientContextPtr prevConnectTimeoutContext;
    NYdbGrpc::IQueueClientContextPtr prevConnectDelayContext;

    // Callbacks
    std::function<void(TPlainStatus&&, typename IProcessor::TPtr&&)> connectCallback;
    std::function<void(bool)> connectTimeoutCallback;

    if (!status.Ok()) {
        LOG_LAZY(Log, TLOG_ERR, GetLogPrefix() << "Got error. Status: " << status.Status
                                               << ". Description: " << IssuesSingleLineString(status.Issues));
    }

    NYdbGrpc::IQueueClientContextPtr delayContext = nullptr;
    NYdbGrpc::IQueueClientContextPtr connectContext = nullptr;
    NYdbGrpc::IQueueClientContextPtr connectTimeoutContext = nullptr;

    TDeferredActions<UseMigrationProtocol> deferred;
    {
        std::lock_guard guard(Lock);
        connectContext = ClientContext->CreateContext();
        connectTimeoutContext = ClientContext->CreateContext();
        if (!connectContext || !connectTimeoutContext) {
            return false;
        }

        if (Aborting) {
            Cancel(connectContext);
            Cancel(connectTimeoutContext);
            return false;
        }
        if (Processor) {
            Processor->Cancel();
        }
        Processor = nullptr;
        WaitingReadResponse = false;
        ServerMessage = std::make_shared<TServerMessage<UseMigrationProtocol>>();
        ++ConnectionGeneration;

        LOG_LAZY(Log, TLOG_DEBUG,
                 GetLogPrefix() << "In Reconnect, ReadSizeBudget = " << ReadSizeBudget
                                << ", ReadSizeServerDelta = " << ReadSizeServerDelta);

        ReadSizeBudget += ReadSizeServerDelta;
        ReadSizeServerDelta = 0;

        LOG_LAZY(Log, TLOG_DEBUG,
                 GetLogPrefix() << "New values: ReadSizeBudget = " << ReadSizeBudget
                                << ", ReadSizeServerDelta = " << ReadSizeServerDelta);

        if (!RetryState) {
            RetryState = Settings.RetryPolicy_->CreateRetryState();
        }
        if (!status.Ok()) {
            auto nextDelay = RetryState->GetNextRetryDelay(status.Status);

            if (!nextDelay) {
                return false;
            }
            delay = *nextDelay;
            delayContext = ClientContext->CreateContext();
            if (!delayContext) {
                return false;
            }
        }

        LOG_LAZY(Log, TLOG_DEBUG, GetLogPrefix() << "Reconnecting session to cluster " << ClusterName << " in " << delay);

        ++ConnectionAttemptsDone;

        if constexpr (!UseMigrationProtocol) {
            if (DirectReadSessionManager) {
                DirectReadSessionManager->Close();
                DirectReadSessionManager.reset();
            }
        }

        // Set new context
        prevConnectContext = std::exchange(ConnectContext, connectContext);
        prevConnectTimeoutContext = std::exchange(ConnectTimeoutContext, connectTimeoutContext);
        prevConnectDelayContext = std::exchange(ConnectDelayContext, delayContext);

        Y_ASSERT(ConnectContext);
        Y_ASSERT(ConnectTimeoutContext);
        Y_ASSERT((delay == TDuration::Zero()) == !ConnectDelayContext);

        // Destroy all partition streams before connecting.
        DestroyAllPartitionStreamsImpl(deferred);

        Y_ABORT_UNLESS(this->SelfContext);

        connectCallback = [cbContext = this->SelfContext,
                           connectContext = connectContext](TPlainStatus&& st, typename IProcessor::TPtr&& processor) {
            if (auto borrowedSelf = cbContext->LockShared()) {
                borrowedSelf->OnConnect(std::move(st), std::move(processor), connectContext); // OnConnect could be called inplace!
            }
        };

        connectTimeoutCallback = [cbContext = this->SelfContext,
                                  connectTimeoutContext = connectTimeoutContext](bool ok) {
            if (ok) {
                if (auto borrowedSelf = cbContext->LockShared()) {
                    borrowedSelf->OnConnectTimeout(connectTimeoutContext);
                }
            }
        };
    }

    // Cancel previous operations.
    Cancel(prevConnectContext);
    Cancel(prevConnectTimeoutContext);
    Cancel(prevConnectDelayContext);

    Y_ASSERT(connectContext);
    Y_ASSERT(connectTimeoutContext);
    Y_ASSERT((delay == TDuration::Zero()) == !delayContext);
    ConnectionFactory->CreateProcessor(
        std::move(connectCallback),
        TRpcRequestSettings::Make(Settings),
        std::move(connectContext),
        TDuration::Seconds(30) /* connect timeout */, // TODO: make connect timeout setting.
        std::move(connectTimeoutContext),
        std::move(connectTimeoutCallback),
        delay,
        std::move(delayContext));
    return true;
}

template <bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::BreakConnectionAndReconnectImpl(
    TPlainStatus&& status, TDeferredActions<UseMigrationProtocol>& deferred) {
    Y_ABORT_UNLESS(Lock.IsLocked());
    LOG_LAZY(Log, TLOG_INFO,
              GetLogPrefix() << "Break connection due to unexpected message from server. Status: " << status.Status
                             << ", Issues: \"" << IssuesSingleLineString(status.Issues) << "\"");

    Processor->Cancel();
    Processor = nullptr;
    RetryState = Settings.RetryPolicy_->CreateRetryState(); // Explicitly create retry state to determine whether we should connect to server again.

    deferred.DeferReconnection(this->SelfContext, std::move(status));
}

template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::OnConnectTimeout(const NYdbGrpc::IQueueClientContextPtr& connectTimeoutContext) {
    {
        std::lock_guard guard(Lock);
        if (ConnectTimeoutContext == connectTimeoutContext) {
            Cancel(ConnectContext);
            ConnectContext = nullptr;
            ConnectTimeoutContext = nullptr;
            ConnectDelayContext = nullptr;

            if (Closing || Aborting) {
                CallCloseCallbackImpl();
                return;
            }
        } else {
            return;
        }
    }

    ++*Settings.Counters_->Errors;
    TStringBuilder description;
    description << "Failed to establish connection to server. Attempts done: " << ConnectionAttemptsDone;
    if (!Reconnect(TPlainStatus(EStatus::TIMEOUT, description))) {
        AbortSession(EStatus::TIMEOUT, description);
    }
}

template <bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::OnConnect(
    TPlainStatus&& st, typename IProcessor::TPtr&& processor, const NYdbGrpc::IQueueClientContextPtr& connectContext) {
    TDeferredActions<UseMigrationProtocol> deferred;
    {
        std::lock_guard guard(Lock);
        if (ConnectContext == connectContext) {
            Cancel(ConnectTimeoutContext);
            ConnectContext = nullptr;
            ConnectTimeoutContext = nullptr;
            ConnectDelayContext = nullptr;

            if (Closing || Aborting) {
                CallCloseCallbackImpl();
                return;
            }

            if (st.Ok()) {
                Processor = std::move(processor);
                ConnectionAttemptsDone = 0;
                InitImpl(deferred);
                return;
            }
        } else {
            return;
        }
    }

    if (!st.Ok()) {
        ++*Settings.Counters_->Errors;
        if (!Reconnect(st)) {
            AbortSession(
                st.Status, MakeIssueWithSubIssues(TStringBuilder() << "Failed to establish connection to server \""
                                                                   << st.Endpoint << "\" ( cluster " << ClusterName
                                                                   << "). Attempts done: " << ConnectionAttemptsDone,
                                                  st.Issues));
        }
    }
}

template<>
inline void TSingleClusterReadSessionImpl<true>::InitImpl(TDeferredActions<true>& deferred) {
    Y_ABORT_UNLESS(Lock.IsLocked());
    LOG_LAZY(Log, TLOG_DEBUG, GetLogPrefix() << "Successfully connected. Initializing session");
    TClientMessage<true> req;
    auto& init = *req.mutable_init_request();
    init.set_ranges_mode(GetRangesMode());
    for (const NPersQueue::TTopicReadSettings& topic : Settings.Topics_) {
        auto* topicSettings = init.add_topics_read_settings();
        topicSettings->set_topic(TStringType{topic.Path_});
        if (topic.StartingMessageTimestamp_) {
            topicSettings->set_start_from_written_at_ms(topic.StartingMessageTimestamp_->MilliSeconds());
        }
        for (ui64 groupId : topic.PartitionGroupIds_) {
            topicSettings->add_partition_group_ids(groupId);
        }
    }
    init.set_consumer(TStringType{Settings.ConsumerName_});
    init.set_read_only_original(Settings.ReadOnlyOriginal_);
    init.mutable_read_params()->set_max_read_size(Settings.MaxMemoryUsageBytes_);
    if (Settings.MaxTimeLag_) {
        init.set_max_lag_duration_ms(Settings.MaxTimeLag_->MilliSeconds());
    }
    if (Settings.StartingMessageTimestamp_) {
        init.set_start_from_written_at_ms(Settings.StartingMessageTimestamp_->MilliSeconds());
    }

    WriteToProcessorImpl(std::move(req));
    ReadFromProcessorImpl(deferred);
}

template<>
inline bool TSingleClusterReadSessionImpl<false>::IsDirectRead() {
    // TODO(qyryq) Replace this return with the next one when direct read is ready for production.
    return ExperimentalDirectRead;
    // return Settings.DirectRead_;
}

template<>
inline void TSingleClusterReadSessionImpl<false>::InitImpl(TDeferredActions<false>& deferred) {
    Y_ABORT_UNLESS(Lock.IsLocked());
    LOG_LAZY(Log, TLOG_DEBUG, GetLogPrefix() << "Successfully connected. Initializing session");

    TClientMessage<false> req;
    auto& init = *req.mutable_init_request();

    init.set_consumer(TStringType{Settings.ConsumerName_});
    init.set_auto_partitioning_support(Settings.AutoPartitioningSupport_);
    init.set_direct_read(IsDirectRead());

    for (const TTopicReadSettings& topic : Settings.Topics_) {
        auto* topicSettings = init.add_topics_read_settings();
        topicSettings->set_path(TStringType{topic.Path_});
        for (ui64 partitionId : topic.PartitionIds_) {
            topicSettings->add_partition_ids(partitionId);
        }

        if (topic.ReadFromTimestamp_) {
            *topicSettings->mutable_read_from() =
                ::google::protobuf::util::TimeUtil::MillisecondsToTimestamp(topic.ReadFromTimestamp_->MilliSeconds());
        } else if (Settings.ReadFromTimestamp_) {
            *topicSettings->mutable_read_from() =
                ::google::protobuf::util::TimeUtil::MillisecondsToTimestamp(Settings.ReadFromTimestamp_->MilliSeconds());
        }

        if (topic.MaxLag_) {
            *topicSettings->mutable_max_lag() =
                ::google::protobuf::util::TimeUtil::MillisecondsToDuration(topic.MaxLag_->MilliSeconds());
        } else if (Settings.MaxLag_) {
            *topicSettings->mutable_max_lag() =
                ::google::protobuf::util::TimeUtil::MillisecondsToDuration(Settings.MaxLag_->MilliSeconds());
        }
    }

    WriteToProcessorImpl(std::move(req));
    ReadFromProcessorImpl(deferred);
}

template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::ContinueReadingDataImpl() {
    Y_ABORT_UNLESS(Lock.IsLocked());

    if (!Closing
        && !Aborting
        && !WaitingReadResponse
        && !DataReadingSuspended
        && Processor
        && CompressedDataSize < GetCompressedDataSizeLimit()
        && static_cast<size_t>(CompressedDataSize + DecompressedDataSize) < Settings.MaxMemoryUsageBytes_)
    {
        TClientMessage<UseMigrationProtocol> req;
        if constexpr (UseMigrationProtocol) {
            req.mutable_read();
        } else {
            LOG_LAZY(Log, TLOG_DEBUG,
                     GetLogPrefix() << "In ContinueReadingDataImpl, ReadSizeBudget = " << ReadSizeBudget
                                    << ", ReadSizeServerDelta = " << ReadSizeServerDelta);

            if (ReadSizeBudget <= 0 || ReadSizeServerDelta + ReadSizeBudget <= 0) {
                return;
            }
            req.mutable_read_request()->set_bytes_size(ReadSizeBudget);
            ReadSizeServerDelta += ReadSizeBudget;
            ReadSizeBudget = 0;
        }

        WriteToProcessorImpl(std::move(req));
        LOG_LAZY(Log, TLOG_DEBUG,
                 GetLogPrefix() << "After sending read request: ReadSizeBudget = " << ReadSizeBudget
                                << ", ReadSizeServerDelta = " << ReadSizeServerDelta);
        WaitingReadResponse = true;
    }
}

template<bool UseMigrationProtocol>
ui64 GetPartitionStreamId(const TPartitionStreamImpl<UseMigrationProtocol>* partitionStream) {
    if constexpr (UseMigrationProtocol) {
        return partitionStream->GetPartitionStreamId();
    } else {
        return partitionStream->GetPartitionSessionId();
    }
}

template<bool UseMigrationProtocol>
std::string GetCluster(const TPartitionStreamImpl<UseMigrationProtocol>* partitionStream) {
    if constexpr (UseMigrationProtocol) {
        return partitionStream->GetCluster();
    } else {
        return "-";
    }
}

template<bool UseMigrationProtocol>
bool TSingleClusterReadSessionImpl<UseMigrationProtocol>::IsActualPartitionStreamImpl(const TPartitionStreamImpl<UseMigrationProtocol>* partitionStream) {
    Y_ABORT_UNLESS(Lock.IsLocked());
    auto it = PartitionStreams.find(partitionStream->GetAssignId());
    return it != PartitionStreams.end() && GetPartitionStreamId(it->second.Get()) == GetPartitionStreamId(partitionStream);
}

template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::ConfirmPartitionStreamCreate(const TPartitionStreamImpl<UseMigrationProtocol>* partitionStream, std::optional<ui64> readOffset, std::optional<ui64> commitOffset) {
    TStringBuilder commitOffsetLogStr;
    if (commitOffset) {
        commitOffsetLogStr << ". Commit offset: " << *commitOffset;
    }
    LOG_LAZY(Log,
        TLOG_INFO,
        GetLogPrefix() << "Confirm partition stream create. Partition stream id: " << GetPartitionStreamId(partitionStream)
            << ". Cluster: \"" << GetCluster(partitionStream) << "\". Topic: \"" << partitionStream->GetTopicPath()
            << "\". Partition: " << partitionStream->GetPartitionId()
            << ". Read offset: " << readOffset << commitOffsetLogStr
    );

    std::lock_guard guard(Lock);
    if (Aborting || Closing || !IsActualPartitionStreamImpl(partitionStream)) { // Got previous incarnation.
        LOG_LAZY(Log,
            TLOG_DEBUG,
            GetLogPrefix() << "Skip partition stream create confirm. Partition stream id: "
                << GetPartitionStreamId(partitionStream)
        );
        return;
    }

    TClientMessage<UseMigrationProtocol> req;

    if constexpr (UseMigrationProtocol) {
        auto& startRead = *req.mutable_start_read();
        startRead.mutable_topic()->set_path(TStringType{partitionStream->GetTopicPath()});
        startRead.set_cluster(TStringType{partitionStream->GetCluster()});
        startRead.set_partition(partitionStream->GetPartitionId());
        startRead.set_assign_id(partitionStream->GetAssignId());
        if (readOffset) {
            startRead.set_read_offset(*readOffset);
        }
        if (commitOffset) {
            startRead.set_commit_offset(*commitOffset);
        }
        WriteToProcessorImpl(std::move(req));
    } else {
        auto partitionSessionId = partitionStream->GetAssignId();
        auto& startRead = *req.mutable_start_partition_session_response();
        startRead.set_partition_session_id(partitionSessionId);
        if (readOffset) {
            startRead.set_read_offset(*readOffset);
        }
        if (commitOffset) {
            startRead.set_commit_offset(*commitOffset);
        }

        WriteToProcessorImpl(std::move(req));

        if (IsDirectRead()) {
            Y_ABORT_UNLESS(DirectReadSessionManager);

            auto location = partitionStream->GetLocation();
            Y_ABORT_UNLESS(location);

            DirectReadSessionManager->StartPartitionSession({
                .PartitionSessionId = static_cast<TPartitionSessionId>(partitionSessionId),
                .PartitionId = static_cast<TPartitionId>(partitionStream->GetPartitionId()),
                .Location = *location,
            });
        }
    }
}

template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::ConfirmPartitionStreamDestroy(TPartitionStreamImpl<UseMigrationProtocol>* partitionStream) {
    LOG_LAZY(Log,
        TLOG_INFO,
        GetLogPrefix() << "Confirm partition stream destroy. Partition stream id: "
            << GetPartitionStreamId(partitionStream)
            << ". Cluster: \"" << GetCluster(partitionStream) << "\". Topic: \"" << partitionStream->GetTopicPath()
            << "\". Partition: " << partitionStream->GetPartitionId()
    );

    TDeferredActions<UseMigrationProtocol> deferred;
    std::lock_guard guard(Lock);
    if (Aborting || Closing || !IsActualPartitionStreamImpl(partitionStream)) { // Got previous incarnation.
        LOG_LAZY(Log,
            TLOG_DEBUG,
            GetLogPrefix() << "Skip partition stream destroy confirm. Partition stream id: "
                << GetPartitionStreamId(partitionStream)
        );
        return;
    }

    using TClosedEvent = std::conditional_t<
        UseMigrationProtocol,
            NPersQueue::TReadSessionEvent::TPartitionStreamClosedEvent,
            NTopic::TReadSessionEvent::TPartitionSessionClosedEvent
    >;

    CookieMapping.RemoveMapping(GetPartitionStreamId(partitionStream));
    PartitionStreams.erase(partitionStream->GetAssignId());

    bool pushRes = true;
    if constexpr (UseMigrationProtocol) {
        pushRes = EventsQueue->PushEvent(partitionStream,
                                TClosedEvent(partitionStream, TClosedEvent::EReason::DestroyConfirmedByUser),
                                deferred);
    } else {
        pushRes = EventsQueue->PushEvent(partitionStream,
                                TClosedEvent(partitionStream, TClosedEvent::EReason::StopConfirmedByUser),
                                deferred);
    }
    if (!pushRes) {
        AbortImpl();
        return;
    }
    TClientMessage<UseMigrationProtocol> req;

    if constexpr (UseMigrationProtocol) {
        auto& released = *req.mutable_released();
        released.mutable_topic()->set_path(TStringType{partitionStream->GetTopicPath()});
        released.set_cluster(TStringType{partitionStream->GetCluster()});
        released.set_partition(partitionStream->GetPartitionId());
        released.set_assign_id(partitionStream->GetAssignId());
    } else {
        auto& released = *req.mutable_stop_partition_session_response();
        released.set_partition_session_id(partitionStream->GetAssignId());

        // TODO(qyryq) Client must pass graceful value unchanged from the StopPartitionSessionRequest.
        released.set_graceful(true);
    }

    WriteToProcessorImpl(std::move(req));
}

template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::Commit(const TPartitionStreamImpl<UseMigrationProtocol>* partitionStream, ui64 startOffset, ui64 endOffset) {
    LOG_LAZY(Log,
        TLOG_DEBUG,
        GetLogPrefix() << "Commit offsets [" << startOffset << ", " << endOffset
            << "). Partition stream id: " << GetPartitionStreamId(partitionStream)
    );
    std::lock_guard guard(Lock);
    if (Aborting || Closing || !IsActualPartitionStreamImpl(partitionStream)) { // Got previous incarnation.
        return;
    }
    TClientMessage<UseMigrationProtocol> req;
    bool hasSomethingToCommit = false;

    if constexpr (UseMigrationProtocol) {
        if (GetRangesMode()) {
            hasSomethingToCommit = true;
            auto* range = req.mutable_commit()->add_offset_ranges();
            range->set_assign_id(partitionStream->GetAssignId());
            range->set_start_offset(startOffset);
            range->set_end_offset(endOffset);
        } else {
            for (ui64 offset = startOffset; offset < endOffset; ++offset) {
                typename TPartitionCookieMapping::TCookie::TPtr cookie = CookieMapping.CommitOffset(GetPartitionStreamId(partitionStream), offset);
                if (cookie) {
                    hasSomethingToCommit = true;
                    auto* cookieInfo = req.mutable_commit()->add_cookies();
                    cookieInfo->set_assign_id(partitionStream->GetAssignId());
                    cookieInfo->set_partition_cookie(cookie->Cookie);
                }
            }
        }
    } else {
        hasSomethingToCommit = true;
        auto* part_commit = req.mutable_commit_offset_request()->add_commit_offsets();
        part_commit->set_partition_session_id(partitionStream->GetAssignId());
        auto* range = part_commit->add_offsets();
        range->set_start(startOffset);
        range->set_end(endOffset);
    }

    if (hasSomethingToCommit) {
        WriteToProcessorImpl(std::move(req));
    }
}

template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::RequestPartitionStreamStatus(const TPartitionStreamImpl<UseMigrationProtocol>* partitionStream) {
    LOG_LAZY(Log,
        TLOG_DEBUG,
        GetLogPrefix() << "Requesting status for partition stream id: " << GetPartitionStreamId(partitionStream)
    );
    std::lock_guard guard(Lock);
    if (Aborting || Closing || !IsActualPartitionStreamImpl(partitionStream)) { // Got previous incarnation.
        return;
    }

    TClientMessage<UseMigrationProtocol> req;

    if constexpr (UseMigrationProtocol) {
        auto& status = *req.mutable_status();
        status.mutable_topic()->set_path(TStringType{partitionStream->GetTopicPath()});
        status.set_cluster(TStringType{partitionStream->GetCluster()});
        status.set_partition(partitionStream->GetPartitionId());
        status.set_assign_id(partitionStream->GetAssignId());
    } else {
        auto& status = *req.mutable_partition_session_status_request();
        status.set_partition_session_id(partitionStream->GetAssignId());
    }

    WriteToProcessorImpl(std::move(req));
}

template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::OnUserRetrievedEvent(i64 decompressedSize, size_t messagesCount)
{
    LOG_LAZY(Log, TLOG_DEBUG, GetLogPrefix()
                          << "The application data is transferred to the client. Number of messages "
                          << messagesCount
                          << ", size "
                          << decompressedSize
                          << " bytes");

    *Settings.Counters_->MessagesInflight -= messagesCount;
    *Settings.Counters_->BytesInflightTotal -= decompressedSize;
    *Settings.Counters_->BytesInflightUncompressed -= decompressedSize;

    TDeferredActions<UseMigrationProtocol> deferred;
    std::lock_guard guard(Lock);
    UpdateMemoryUsageStatisticsImpl();

    Y_ABORT_UNLESS(decompressedSize <= DecompressedDataSize);
    DecompressedDataSize -= decompressedSize;

    ContinueReadingDataImpl();
    StartDecompressionTasksImpl(deferred);
}

template <bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::WriteToProcessorImpl(
    TClientMessage<UseMigrationProtocol>&& req) {
    Y_ABORT_UNLESS(Lock.IsLocked());

    if (Processor) {
        Processor->Write(std::move(req));
    }
}

template<bool UseMigrationProtocol>
bool TSingleClusterReadSessionImpl<UseMigrationProtocol>::HasCommitsInflightImpl() const {
    Y_ABORT_UNLESS(Lock.IsLocked());
    for (const auto& [id, partitionStream] : PartitionStreams) {
        if (partitionStream->HasCommitsInflight())
            return true;
    }
    return false;
}

template <bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::ReadFromProcessorImpl(
    TDeferredActions<UseMigrationProtocol>& deferred) {
    Y_ABORT_UNLESS(Lock.IsLocked());
    if (Aborting) {
        return;
    }
    if (Closing && !HasCommitsInflightImpl()) {
        Processor->Cancel();
        CallCloseCallbackImpl();
        return;
    }

    if (Processor && !Closing) {
        ServerMessage->Clear();

        Y_ABORT_UNLESS(this->SelfContext);

        auto callback = [cbContext = this->SelfContext,
                         connectionGeneration = ConnectionGeneration,
                         // Capture message & processor not to read in freed memory.
                         serverMessage = ServerMessage,
                         processor = Processor](NYdbGrpc::TGrpcStatus&& grpcStatus) {
            if (auto borrowedSelf = cbContext->LockShared()) {
                borrowedSelf->OnReadDone(std::move(grpcStatus), connectionGeneration);
            }
        };

        deferred.DeferReadFromProcessor(Processor, ServerMessage.get(), std::move(callback));
    }
}

template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::OnReadDone(NYdbGrpc::TGrpcStatus&& grpcStatus, size_t connectionGeneration) {
    TPlainStatus errorStatus;
    if (!grpcStatus.Ok()) {
        errorStatus = TPlainStatus(std::move(grpcStatus));
    }

    TDeferredActions<UseMigrationProtocol> deferred;
    {
        std::lock_guard guard(Lock);
        if (Aborting) {
            return;
        }

        if (connectionGeneration != ConnectionGeneration) {
            return; // Message from previous connection. Ignore.
        }

        if (errorStatus.Ok()) {
            if (IsErrorMessage(*ServerMessage)) {
                errorStatus = MakeErrorFromProto(*ServerMessage);
            } else {

                if constexpr (UseMigrationProtocol) {
                    switch (ServerMessage->response_case()) {
                    case Ydb::PersQueue::V1::MigrationStreamingReadServerMessage::kInitResponse:
                        OnReadDoneImpl(std::move(*ServerMessage->mutable_init_response()), deferred);
                        break;
                    case Ydb::PersQueue::V1::MigrationStreamingReadServerMessage::kDataBatch:
                        OnReadDoneImpl(std::move(*ServerMessage->mutable_data_batch()), deferred);
                        break;
                    case Ydb::PersQueue::V1::MigrationStreamingReadServerMessage::kAssigned:
                        OnReadDoneImpl(std::move(*ServerMessage->mutable_assigned()), deferred);
                        break;
                    case Ydb::PersQueue::V1::MigrationStreamingReadServerMessage::kRelease:
                        OnReadDoneImpl(std::move(*ServerMessage->mutable_release()), deferred);
                        break;
                    case Ydb::PersQueue::V1::MigrationStreamingReadServerMessage::kCommitted:
                        OnReadDoneImpl(std::move(*ServerMessage->mutable_committed()), deferred);
                        break;
                    case Ydb::PersQueue::V1::MigrationStreamingReadServerMessage::kPartitionStatus:
                        OnReadDoneImpl(std::move(*ServerMessage->mutable_partition_status()), deferred);
                        break;
                    case Ydb::PersQueue::V1::MigrationStreamingReadServerMessage::RESPONSE_NOT_SET:
                        errorStatus = TPlainStatus::Internal("Unexpected response from server");
                        break;
                    }
                } else {
                    switch (ServerMessage->server_message_case()) {
                    case TServerMessage<false>::kInitResponse:
                        OnReadDoneImpl(std::move(*ServerMessage->mutable_init_response()), deferred);
                        break;
                    case TServerMessage<false>::kReadResponse:
                        OnReadDoneImpl(std::move(*ServerMessage->mutable_read_response()), deferred);
                        break;
                    case TServerMessage<false>::kStartPartitionSessionRequest:
                        OnReadDoneImpl(std::move(*ServerMessage->mutable_start_partition_session_request()), deferred);
                        break;
                    case TServerMessage<false>::kUpdatePartitionSession:
                        OnReadDoneImpl(std::move(*ServerMessage->mutable_update_partition_session()), deferred);
                        break;

                    case TServerMessage<false>::kStopPartitionSessionRequest:
                        OnReadDoneImpl(std::move(*ServerMessage->mutable_stop_partition_session_request()), deferred);
                        break;
                    case TServerMessage<false>::kEndPartitionSession:
                        OnReadDoneImpl(std::move(*ServerMessage->mutable_end_partition_session()), deferred);
                        break;
                    case TServerMessage<false>::kCommitOffsetResponse:
                        OnReadDoneImpl(std::move(*ServerMessage->mutable_commit_offset_response()), deferred);
                        break;
                    case TServerMessage<false>::kPartitionSessionStatusResponse:
                        OnReadDoneImpl(std::move(*ServerMessage->mutable_partition_session_status_response()), deferred);
                        break;
                    case TServerMessage<false>::kUpdateTokenResponse:
                        OnReadDoneImpl(std::move(*ServerMessage->mutable_update_token_response()), deferred);
                        break;
                    case TServerMessage<false>::SERVER_MESSAGE_NOT_SET:
                        errorStatus = TPlainStatus::Internal("Server message is not set");
                        break;
                    default:
                        errorStatus = TPlainStatus::Internal("Unexpected response from server");
                        break;
                    }
                }

                if (errorStatus.Ok()) {
                    ReadFromProcessorImpl(deferred); // Read next.
                }
            }
        }
    }
    if (!errorStatus.Ok()) {
        ++*Settings.Counters_->Errors;

        if (!Reconnect(errorStatus)) {
            AbortSession(std::move(errorStatus));
        }
    }
}

template <>
template <>
inline void TSingleClusterReadSessionImpl<true>::OnReadDoneImpl(
    Ydb::PersQueue::V1::MigrationStreamingReadServerMessage::InitResponse&& msg,
    TDeferredActions<true>& deferred) {
    Y_ABORT_UNLESS(Lock.IsLocked());
    Y_UNUSED(deferred);

    LOG_LAZY(Log, TLOG_INFO, GetLogPrefix() << "Server session id: " << msg.session_id());

    RetryState = nullptr;
    ReadSessionId = msg.session_id();

    // Successful init. Do nothing.
    ContinueReadingDataImpl();
}

template <>
template <>
inline void TSingleClusterReadSessionImpl<true>::OnReadDoneImpl(
    Ydb::PersQueue::V1::MigrationStreamingReadServerMessage::DataBatch&& msg,
    TDeferredActions<true>& deferred) {
    Y_ABORT_UNLESS(Lock.IsLocked());
    if (Closing || Aborting) {
        return; // Don't process new data.
    }
    UpdateMemoryUsageStatisticsImpl();
    for (TPartitionData<true>& partitionData : *msg.mutable_partition_data()) {
        auto partitionStreamIt = PartitionStreams.find(partitionData.cookie().assign_id());
        if (partitionStreamIt == PartitionStreams.end()) {
            ++*Settings.Counters_->Errors;
            BreakConnectionAndReconnectImpl(EStatus::INTERNAL_ERROR,
                                            TStringBuilder()
                                                << "Got unexpected partition stream data message. Topic: "
                                                << partitionData.topic().ShortDebugString() << ". Partition: " << partitionData.partition()
                                                << " AssignId: " << partitionData.cookie().assign_id(),
                                            deferred);
            return;
        }
        const TIntrusivePtr<TPartitionStreamImpl<true>>& partitionStream = partitionStreamIt->second;

        typename TPartitionCookieMapping::TCookie::TPtr cookie = MakeIntrusive<typename TPartitionCookieMapping::TCookie>(partitionData.cookie().partition_cookie(), partitionStream);

        ui64 firstOffset = std::numeric_limits<ui64>::max();
        ui64 currentOffset = std::numeric_limits<ui64>::max();
        ui64 desiredOffset = partitionStream->GetFirstNotReadOffset();
        for (const Ydb::PersQueue::V1::MigrationStreamingReadServerMessage::DataBatch::Batch& batch : partitionData.batches()) {
            // Validate messages.
            for (const Ydb::PersQueue::V1::MigrationStreamingReadServerMessage::DataBatch::MessageData& messageData : batch.message_data()) {
                // Check offsets continuity.
                if (messageData.offset() != desiredOffset) {
                    bool res = partitionStream->AddToCommitRanges(desiredOffset, messageData.offset(), GetRangesMode());
                    Y_ABORT_UNLESS(res);
                }

                if (firstOffset == std::numeric_limits<ui64>::max()) {
                    firstOffset = messageData.offset();
                }
                currentOffset = messageData.offset();
                desiredOffset = currentOffset + 1;
                partitionStream->UpdateMaxReadOffset(currentOffset);
                const i64 messageSize = static_cast<i64>(messageData.data().size());
                CompressedDataSize += messageSize;
                *Settings.Counters_->BytesInflightTotal += messageSize;
                *Settings.Counters_->BytesInflightCompressed += messageSize;
                ++*Settings.Counters_->MessagesInflight;
            }
        }
        if (firstOffset == std::numeric_limits<ui64>::max()) {
            BreakConnectionAndReconnectImpl(EStatus::INTERNAL_ERROR,
                                            TStringBuilder() << "Got empty data message. Topic: "
                                                << partitionData.topic().ShortDebugString()
                                                << ". Partition: " << partitionData.partition()
                                                << " message: " << msg.ShortDebugString(),
                                            deferred);
            return;
        }
        cookie->SetOffsetRange(std::make_pair(firstOffset, desiredOffset));
        partitionStream->SetFirstNotReadOffset(desiredOffset);
        if (!CookieMapping.AddMapping(cookie)) {
            BreakConnectionAndReconnectImpl(EStatus::INTERNAL_ERROR,
                                            TStringBuilder() << "Got unexpected data message. Topic: "
                                                << partitionData.topic().ShortDebugString()
                                                << ". Partition: " << partitionData.partition()
                                                << ". Cookie mapping already has such cookie",
                                            deferred);
            return;
        }

        auto decompressionInfo = std::make_shared<TDataDecompressionInfo<true>>(std::move(partitionData),
                                                                                SelfContext,
                                                                                Settings.Decompress_);
        Y_ABORT_UNLESS(decompressionInfo);

        decompressionInfo->PlanDecompressionTasks(AverageCompressionRatio,
                                                  partitionStream);

        DecompressionQueue.emplace_back(decompressionInfo, partitionStream);
        StartDecompressionTasksImpl(deferred);
    }

    WaitingReadResponse = false;
    ContinueReadingDataImpl();
}

template <>
template <>
inline void TSingleClusterReadSessionImpl<true>::OnReadDoneImpl(
    Ydb::PersQueue::V1::MigrationStreamingReadServerMessage::Assigned&& msg,
    TDeferredActions<true>& deferred) {
    Y_ABORT_UNLESS(Lock.IsLocked());

    auto partitionStream = MakeIntrusive<TPartitionStreamImpl<true>>(
        NextPartitionStreamId, msg.topic().path(), msg.cluster(),
        msg.partition() + 1, // Group.
        msg.partition(),     // Partition.
        msg.assign_id(), msg.read_offset(), SelfContext);
    NextPartitionStreamId += PartitionStreamIdStep;

    // Renew partition stream.
    TIntrusivePtr<TPartitionStreamImpl<true>>& currentPartitionStream =
        PartitionStreams[partitionStream->GetAssignId()];
    if (currentPartitionStream) {
        CookieMapping.RemoveMapping(currentPartitionStream->GetPartitionStreamId());

        bool pushRes = EventsQueue->PushEvent(
            currentPartitionStream,
            NPersQueue::TReadSessionEvent::TPartitionStreamClosedEvent(
                 currentPartitionStream, NPersQueue::TReadSessionEvent::TPartitionStreamClosedEvent::EReason::Lost),
            deferred);
        if (!pushRes) {
            AbortImpl();
            return;
        }
    }
    currentPartitionStream = partitionStream;

    // Send event to user.
    bool pushRes = EventsQueue->PushEvent(
        partitionStream,
        NPersQueue::TReadSessionEvent::TCreatePartitionStreamEvent(partitionStream, msg.read_offset(), msg.end_offset()),
        deferred);
    if (!pushRes) {
        AbortImpl();
        return;
    }
}

template <>
template <>
inline void TSingleClusterReadSessionImpl<true>::OnReadDoneImpl(
    Ydb::PersQueue::V1::MigrationStreamingReadServerMessage::Release&& msg,
    TDeferredActions<true>& deferred) {
    Y_ABORT_UNLESS(Lock.IsLocked());

    auto partitionStreamIt = PartitionStreams.find(msg.assign_id());
    if (partitionStreamIt == PartitionStreams.end()) {
        return;
    }
    TIntrusivePtr<TPartitionStreamImpl<true>> partitionStream = partitionStreamIt->second;
    bool pushRes = true;
    if (msg.forceful_release()) {
        PartitionStreams.erase(msg.assign_id());
        CookieMapping.RemoveMapping(partitionStream->GetPartitionStreamId());
        pushRes = EventsQueue->PushEvent(
            partitionStream,
            NPersQueue::TReadSessionEvent::TPartitionStreamClosedEvent(
                partitionStream, NPersQueue::TReadSessionEvent::TPartitionStreamClosedEvent::EReason::Lost),
            deferred);
    } else {
        pushRes = EventsQueue->PushEvent(
            partitionStream,
            NPersQueue::TReadSessionEvent::TDestroyPartitionStreamEvent(std::move(partitionStream), msg.commit_offset()),
            deferred);
    }

    if (!pushRes) {
        AbortImpl();
        return;
    }

}

template <>
template <>
inline void TSingleClusterReadSessionImpl<true>::OnReadDoneImpl(
    Ydb::PersQueue::V1::MigrationStreamingReadServerMessage::Committed&& msg,
    TDeferredActions<true>& deferred) {
    Y_ABORT_UNLESS(Lock.IsLocked());

    LOG_LAZY(Log, TLOG_DEBUG, GetLogPrefix() << "Committed response: " << msg.ShortDebugString());

    std::map<ui64, TIntrusivePtr<TPartitionStreamImpl<true>>> partitionStreams;
    for (const Ydb::PersQueue::V1::CommitCookie& cookieProto : msg.cookies()) {
        typename TPartitionCookieMapping::TCookie::TPtr cookie = CookieMapping.RetrieveCommittedCookie(cookieProto);
        if (cookie) {
            cookie->PartitionStream->UpdateMaxCommittedOffset(cookie->OffsetRange.second);
            partitionStreams[cookie->PartitionStream->GetPartitionStreamId()] = cookie->PartitionStream;
        }
    }
    for (auto& [id, partitionStream] : partitionStreams) {
        bool pushRes = EventsQueue->PushEvent(partitionStream,
                                              NPersQueue::TReadSessionEvent::TCommitAcknowledgementEvent(
                                                  partitionStream, partitionStream->GetMaxCommittedOffset()),
                                              deferred);
        if (!pushRes) {
            AbortImpl();
            return;
        }
    }

    for (const auto& rangeProto : msg.offset_ranges()) {
        auto partitionStreamIt = PartitionStreams.find(rangeProto.assign_id());
        if (partitionStreamIt != PartitionStreams.end()) {
            auto partitionStream = partitionStreamIt->second;
            partitionStream->UpdateMaxCommittedOffset(rangeProto.end_offset());
            bool pushRes = EventsQueue->PushEvent(
                partitionStream,
                NPersQueue::TReadSessionEvent::TCommitAcknowledgementEvent(partitionStream, rangeProto.end_offset()),
                deferred);
            if (!pushRes) {
                AbortImpl();
                return;
            }
        }
    }
}

template <>
template <>
inline void TSingleClusterReadSessionImpl<true>::OnReadDoneImpl(
    Ydb::PersQueue::V1::MigrationStreamingReadServerMessage::PartitionStatus&& msg,
    TDeferredActions<true>& deferred) {
    Y_ABORT_UNLESS(Lock.IsLocked());

    auto partitionStreamIt = PartitionStreams.find(msg.assign_id());
    if (partitionStreamIt == PartitionStreams.end()) {
        return;
    }
    bool pushRes = EventsQueue->PushEvent(partitionStreamIt->second,
                                          NPersQueue::TReadSessionEvent::TPartitionStreamStatusEvent(
                                              partitionStreamIt->second, msg.committed_offset(),
                                              0, // TODO: support read offset in status
                                              msg.end_offset(), TInstant::MilliSeconds(msg.write_watermark_ms())),
                                          deferred);
    if (!pushRes) {
        AbortImpl();
        return;
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <>
template <>
inline void TSingleClusterReadSessionImpl<false>::OnReadDoneImpl(
    Ydb::Topic::StreamReadMessage::ReadResponse&& msg,
    TDeferredActions<false>& deferred
) {
    Y_ABORT_UNLESS(Lock.IsLocked());

    if (Closing || Aborting) {
        return; // Don't process new data.
    }

    i64 serverBytesSize = msg.bytes_size();
    ReadSizeServerDelta -= serverBytesSize;
    LOG_LAZY(Log, TLOG_DEBUG,
             GetLogPrefix() << "Got ReadResponse, serverBytesSize = " << serverBytesSize << ", now ReadSizeBudget = "
                            << ReadSizeBudget << ", ReadSizeServerDelta = " << ReadSizeServerDelta);

    UpdateMemoryUsageStatisticsImpl();
    for (TPartitionData<false>& partitionData : *msg.mutable_partition_data()) {
        auto partitionStreamIt = PartitionStreams.find(partitionData.partition_session_id());
        if (partitionStreamIt == PartitionStreams.end()) {
            if (IsDirectRead()) {
                // If we have a timeline like the next one, ignore the partition stream data message:
                //   1. A subsession receives DirectReadResponse.
                //   2. The control session receives StopPartitionSessionRequest and deletes the partition session from PartitionStreams.
                //   3. The subsession calls OnDirectReadDone which calls this method.
                LOG_LAZY(Log, TLOG_DEBUG, GetLogPrefix() << "Got unexpected partition stream data message. PartitionSessionId: " << partitionData.partition_session_id());
                continue;
            } else {
                ++*Settings.Counters_->Errors;
                BreakConnectionAndReconnectImpl(EStatus::INTERNAL_ERROR,
                                                TStringBuilder() << "Got unexpected partition stream data message. "
                                                << "PartitionSessionId: " << partitionData.partition_session_id(),
                                                deferred);
                return;
            }
        }
        const TIntrusivePtr<TPartitionStreamImpl<false>>& partitionStream = partitionStreamIt->second;

        i64 firstOffset = std::numeric_limits<i64>::max();
        i64 currentOffset = std::numeric_limits<i64>::max();
        i64 desiredOffset = partitionStream->GetFirstNotReadOffset();
        for (const auto& batch : partitionData.batches()) {
            // Validate messages.
            for (const auto& messageData : batch.message_data()) {
                // Check offsets continuity.
                if (messageData.offset() != desiredOffset) {
                    bool res = partitionStream->AddToCommitRanges(desiredOffset, messageData.offset(), GetRangesMode());
                    Y_ABORT_UNLESS(res);
                }

                if (firstOffset == std::numeric_limits<i64>::max()) {
                    firstOffset = messageData.offset();
                }
                currentOffset = messageData.offset();
                desiredOffset = currentOffset + 1;
                partitionStream->UpdateMaxReadOffset(currentOffset);
                const i64 messageSize = static_cast<i64>(messageData.data().size());
                CompressedDataSize += messageSize;
                *Settings.Counters_->BytesInflightTotal += messageSize;
                *Settings.Counters_->BytesInflightCompressed += messageSize;
                ++*Settings.Counters_->MessagesInflight;
            }
        }
        if (firstOffset == std::numeric_limits<i64>::max()) {
            BreakConnectionAndReconnectImpl(EStatus::INTERNAL_ERROR,
                                            TStringBuilder() << "Got empty data message. "
                                                << "PartitionSessionId: " << partitionData.partition_session_id()
                                                << " message: " << msg,
                                            deferred);
            return;
        }
        partitionStream->SetFirstNotReadOffset(desiredOffset);

        auto decompressionInfo = std::make_shared<TDataDecompressionInfo<false>>(std::move(partitionData),
                                                                                 SelfContext,
                                                                                 Settings.Decompress_,
                                                                                 serverBytesSize);
        // TODO (ildar-khisam@): share serverBytesSize between partitions data according to their actual sizes;
        //                       for now whole serverBytesSize goes with first (and only) partition data.
        serverBytesSize = 0;
        Y_ABORT_UNLESS(decompressionInfo);

        decompressionInfo->PlanDecompressionTasks(AverageCompressionRatio,
                                                  partitionStream);
        DecompressionQueue.emplace_back(decompressionInfo, partitionStream);
        StartDecompressionTasksImpl(deferred);
    }

    WaitingReadResponse = false;
    ContinueReadingDataImpl();
}

template <>
inline void TSingleClusterReadSessionImpl<false>::StopPartitionSessionImpl(
    TIntrusivePtr<TPartitionStreamImpl<false>> partitionStream, bool graceful, TDeferredActions<false>& deferred
) {
    auto partitionSessionId = partitionStream->GetAssignId();

    if (IsDirectRead()) {
        Y_ABORT_UNLESS(DirectReadSessionManager);
        DirectReadSessionManager->StopPartitionSession(partitionSessionId);
    }

    bool pushRes = true;

    if (graceful) {
        auto committedOffset = partitionStream->GetMaxCommittedOffset();
        LOG_LAZY(Log, TLOG_DEBUG, GetLogPrefix() << "XXXXX PushEvent 1422 TStopPartitionSessionEvent");
        pushRes = EventsQueue->PushEvent(
            partitionStream,
            // TODO(qyryq) Is it safe to use GetMaxCommittedOffset here instead of StopPartitionSessionRequest.commmitted_offset?
            TReadSessionEvent::TStopPartitionSessionEvent(std::move(partitionStream), committedOffset),
            deferred);
    } else {
        // partitionStream->ConfirmDestroy();
        TClientMessage<false> req;
        auto& released = *req.mutable_stop_partition_session_response();
        released.set_partition_session_id(partitionStream->GetAssignId());
        WriteToProcessorImpl(std::move(req));
        PartitionStreams.erase(partitionSessionId);
        LOG_LAZY(Log, TLOG_DEBUG, GetLogPrefix() << "XXXXX PushEvent 1435 TPartitionSessionClosedEvent");
        pushRes = EventsQueue->PushEvent(
            partitionStream,
            TReadSessionEvent::TPartitionSessionClosedEvent(partitionStream, TReadSessionEvent::TPartitionSessionClosedEvent::EReason::Lost),
            deferred);
    }

    if (!pushRes) {
        AbortImpl();
    }
}

template <>
inline void TSingleClusterReadSessionImpl<false>::OnDirectReadDone(
    std::shared_ptr<TLockFreeQueue<Ydb::Topic::StreamDirectReadMessage::DirectReadResponse>> responses
    // Ydb::Topic::StreamDirectReadMessage::DirectReadResponse&& response,
    // TDeferredActions<false>& deferred
) {
    TDeferredActions<false> deferred;
    with_lock (Lock) {
        Ydb::Topic::StreamDirectReadMessage::DirectReadResponse response;
        if (!responses->Dequeue(&response)) {
            return;
        }

        {
            // Send DirectReadAck.
            TClientMessage<false> req;
            auto& ack = *req.mutable_direct_read_ack();
            ack.set_direct_read_id(response.direct_read_id());
            ack.set_partition_session_id(response.partition_session_id());
            WriteToProcessorImpl(std::move(req));
        }

        auto partitionStreamIt = PartitionStreams.find(response.partition_session_id());
        if (partitionStreamIt == PartitionStreams.end()) {
            LOG_LAZY(Log, TLOG_INFO, GetLogPrefix() << "Got DirectReadResponse for unknown partition session id: " << response.partition_session_id() << ".");
            return;
        }

        auto& partitionStream = partitionStreamIt->second;
        partitionStream->SetNextDirectReadId(response.direct_read_id() + 1);

        auto stopPartitionSession = [&](){
            // After we get a StopPartitionSessionRequest(graceful=true), LastDirectReadId is defined.
            // In this case we're waiting for the DirectReadResponse(direct_read_id=LastDirectReadId) and then close the subsession.

            auto lastId = partitionStream->GetLastDirectReadId();
            if (lastId && lastId <= response.direct_read_id() + 1) {
                this->StopPartitionSessionImpl(partitionStream, true, deferred);
            }
        };

        if (!response.has_partition_data() || response.partition_data().batches_size() == 0) {
            // Sometimes the server might send an empty DirectReadResponse with a non-zero bytes_size, that we should take into account.
            stopPartitionSession();
            ReadSizeBudget += response.bytes_size();
            ReadSizeServerDelta -= response.bytes_size();
            WaitingReadResponse = false;
            ContinueReadingDataImpl();
            return;
        }

        Ydb::Topic::StreamReadMessage::ReadResponse r;
        r.set_bytes_size(response.bytes_size());
        auto* data = r.add_partition_data();
        data->Swap(response.mutable_partition_data());
        OnReadDoneImpl(std::move(r), deferred);
        stopPartitionSession();
    }
}

template <>
inline void TSingleClusterReadSessionImpl<false>::ScheduleCallback(TDuration timeout, std::function<void(bool)> callback) {
    // TODO(qyryq) Pass context ptr?
    ScheduleCallbackFunc(timeout, callback, nullptr);
}

template <>
inline void TSingleClusterReadSessionImpl<false>::StopPartitionSession(TPartitionSessionId partitionSessionId) {
    TDeferredActions<false> deferred;
    with_lock (Lock) {
        auto partitionStreamIt = PartitionStreams.find(partitionSessionId);
        if (partitionStreamIt == PartitionStreams.end()) {
            LOG_LAZY(Log, TLOG_DEBUG, GetLogPrefix() << "Wanted to stop partition stream id=" << partitionSessionId
                                                     << ", but no such id was found");
            return;
        }
        StopPartitionSessionImpl(partitionStreamIt->second, /* graceful= */ true, deferred);
    }
}

template <>
template <>
inline void TSingleClusterReadSessionImpl<false>::OnReadDoneImpl(
    Ydb::Topic::StreamReadMessage::InitResponse&& msg,
    TDeferredActions<false>&
) {
    Y_ABORT_UNLESS(Lock.IsLocked());

    RetryState = nullptr;
    ReadSessionId = msg.session_id();

    LOG_LAZY(Log, TLOG_INFO, GetLogPrefix() << "Got InitResponse. ReadSessionId: " << ReadSessionId);

    if (IsDirectRead()) {
        Y_ABORT_UNLESS(!DirectReadSessionManager);
        DirectReadSessionManager.emplace(
            ReadSessionId,
            Settings,
            std::make_shared<TDirectReadSessionControlCallbacks>(this->SelfContext),
            ClientContext->CreateContext(),
            DirectReadProcessorFactory,
            Log
        );
    }

    ContinueReadingDataImpl();
}

template <>
template <>
inline void TSingleClusterReadSessionImpl<false>::OnReadDoneImpl(
    Ydb::Topic::StreamReadMessage::StartPartitionSessionRequest&& msg,
    TDeferredActions<false>& deferred
) {
    Y_ABORT_UNLESS(Lock.IsLocked());

    // For DirectRead the message MUST have partition location.
    Y_ABORT_UNLESS(!IsDirectRead() || msg.has_partition_location());

    auto partitionSessionId = msg.partition_session().partition_session_id();

    // Renew partition stream.
    TIntrusivePtr<TPartitionStreamImpl<false>>& partitionStream = PartitionStreams[partitionSessionId];
    if (partitionStream) {
        bool pushRes = EventsQueue->PushEvent(
            partitionStream,
            TReadSessionEvent::TPartitionSessionClosedEvent(
                partitionStream, TReadSessionEvent::TPartitionSessionClosedEvent::EReason::Lost),
            deferred);

        if (!pushRes) {
            AbortImpl();
            return;
        }
    }

    partitionStream = MakeIntrusive<TPartitionStreamImpl<false>>(
        NextPartitionStreamId,
        msg.partition_session().path(),
        ReadSessionId,
        msg.partition_session().partition_id(),
        partitionSessionId,
        msg.committed_offset(),
        msg.has_partition_location() ? std::optional<TPartitionLocation>(msg.partition_location()) : std::nullopt,
        SelfContext);

    NextPartitionStreamId += PartitionStreamIdStep;

    // Send event to user.
    bool pushRes = EventsQueue->PushEvent(
        partitionStream,
        TReadSessionEvent::TStartPartitionSessionEvent(
            partitionStream, msg.committed_offset(), msg.partition_offsets().end()),
        deferred);

    if (!pushRes) {
        AbortImpl();
        return;
    }
}

template <>
template <>
inline void TSingleClusterReadSessionImpl<false>::OnReadDoneImpl(
    Ydb::Topic::StreamReadMessage::UpdatePartitionSession&& msg,
    TDeferredActions<false>&
) {
    Y_ABORT_UNLESS(Lock.IsLocked());
    LOG_LAZY(Log, TLOG_DEBUG, GetLogPrefix() << "UpdatePartitionSession " << msg.DebugString());

    auto partitionSessionId = msg.partition_session_id();
    auto it = PartitionStreams.find(partitionSessionId);
    if (it == PartitionStreams.end()) {
        LOG_LAZY(Log, TLOG_DEBUG, GetLogPrefix() << "Wanted to update partition_session_id: " << partitionSessionId
                                                 << ", but no such id was found");
        return;
    }

    Y_ABORT_UNLESS(it->second->GetAssignId() == static_cast<unsigned long>(partitionSessionId));

    // TODO(qyryq) Do we need to store generation/nodeid info in TSingleClusterReadSessionImpl?
    if (IsDirectRead()) {
        Y_ABORT_UNLESS(DirectReadSessionManager);
        it->second->SetLocation(msg.partition_location());
        DirectReadSessionManager->UpdatePartitionSession(
            partitionSessionId,
            static_cast<TPartitionId>(it->second->GetPartitionId()),
            msg.partition_location()
        );
    }
}

template <>
template <>
inline void TSingleClusterReadSessionImpl<false>::OnReadDoneImpl(
    Ydb::Topic::StreamReadMessage::StopPartitionSessionRequest&& msg,
    TDeferredActions<false>& deferred
) {
    Y_ABORT_UNLESS(Lock.IsLocked());

    LOG_LAZY(Log, TLOG_DEBUG, GetLogPrefix() << "StopPartitionSessionRequest " << msg.DebugString());

    auto partitionSessionId = msg.partition_session_id();

    auto partitionStreamIt = PartitionStreams.find(partitionSessionId);
    if (partitionStreamIt == PartitionStreams.end()) {
        LOG_LAZY(Log, TLOG_DEBUG, GetLogPrefix() << "Server wants us to stop partition session id=" << partitionSessionId
                                                 << ", but it's not found");
        return;
    }

    TIntrusivePtr<TPartitionStreamImpl<false>> partitionStream = partitionStreamIt->second;

    if (IsDirectRead() && msg.graceful()) {
        // Keep reading DirectReadResponses until we get the one with direct_read_id == last_direct_read_id.
        // Only then we send the TStopPartitionSessionEvent to the user.

        partitionStream->SetLastDirectReadId(msg.last_direct_read_id());

        if (msg.last_direct_read_id() <= partitionStream->GetNextDirectReadId()) {
            // There are two cases when we need to call StopPartitionSessionImpl:
            //   1. We have received the last DirectReadResponse.
            //   2. We have received the StopPartitionSessionRequest(graceful=true) after we received a corresponding DirectReadResponse.
            // This is the second case.
            StopPartitionSessionImpl(partitionStreamIt->second, true, deferred);
        }

        return;
    }

    StopPartitionSessionImpl(partitionStreamIt->second, msg.graceful(), deferred);
}

template <>
template <>
inline void TSingleClusterReadSessionImpl<false>::OnReadDoneImpl(
    Ydb::Topic::StreamReadMessage::EndPartitionSession&& msg,
    TDeferredActions<false>& deferred
) {
    Y_ABORT_UNLESS(Lock.IsLocked());

    auto partitionStreamIt = PartitionStreams.find(msg.partition_session_id());
    if (partitionStreamIt == PartitionStreams.end()) {
        return;
    }
    TIntrusivePtr<TPartitionStreamImpl<false>> partitionStream = partitionStreamIt->second;

    std::vector<ui32> adjacentPartitionIds;
    adjacentPartitionIds.reserve(msg.adjacent_partition_ids_size());
    adjacentPartitionIds.insert(adjacentPartitionIds.end(), msg.adjacent_partition_ids().begin(), msg.adjacent_partition_ids().end());

    std::vector<ui32> childPartitionIds;
    childPartitionIds.reserve(msg.child_partition_ids_size());
    childPartitionIds.insert(childPartitionIds.end(), msg.child_partition_ids().begin(), msg.child_partition_ids().end());

    for (auto child : childPartitionIds) {
        RegisterParentPartition(child,
                                partitionStream->GetPartitionId(),
                                partitionStream->GetPartitionSessionId());
    }

    if (IsDirectRead()) {
        Y_ABORT_UNLESS(DirectReadSessionManager);
        DirectReadSessionManager->StopPartitionSession(msg.partition_session_id());
    }

    bool pushRes = EventsQueue->PushEvent(
            partitionStream,
            TReadSessionEvent::TEndPartitionSessionEvent(std::move(partitionStream), std::move(adjacentPartitionIds), std::move(childPartitionIds)),
            deferred);
    if (!pushRes) {
        AbortImpl();
        return;
    }
}

template <>
template <>
inline void TSingleClusterReadSessionImpl<false>::OnReadDoneImpl(
    Ydb::Topic::StreamReadMessage::CommitOffsetResponse&& msg,
    TDeferredActions<false>& deferred) {
    Y_ABORT_UNLESS(Lock.IsLocked());

    LOG_LAZY(Log, TLOG_DEBUG, GetLogPrefix() << "Committed response: " << msg);

    for (const auto& rangeProto : msg.partitions_committed_offsets()) {
        auto partitionStreamIt = PartitionStreams.find(rangeProto.partition_session_id());
        if (partitionStreamIt != PartitionStreams.end()) {
            auto partitionStream = partitionStreamIt->second;
            partitionStream->UpdateMaxCommittedOffset(rangeProto.committed_offset());
            bool pushRes = EventsQueue->PushEvent(partitionStream,
                                    TReadSessionEvent::TCommitOffsetAcknowledgementEvent(
                                        partitionStream, rangeProto.committed_offset()),
                                   deferred);
            if (!pushRes) {
                AbortImpl();
                return;
            }
        }
    }
}

template <>
template <>
inline void TSingleClusterReadSessionImpl<false>::OnReadDoneImpl(
    Ydb::Topic::StreamReadMessage::PartitionSessionStatusResponse&& msg,
    TDeferredActions<false>& deferred) {
    Y_ABORT_UNLESS(Lock.IsLocked());

    auto partitionStreamIt = PartitionStreams.find(msg.partition_session_id());
    if (partitionStreamIt == PartitionStreams.end()) {
        return;
    }
    bool pushRes = EventsQueue->PushEvent(partitionStreamIt->second,
                            TReadSessionEvent::TPartitionSessionStatusEvent(
                                partitionStreamIt->second, msg.committed_offset(),
                                0, // TODO: support read offset in status
                                msg.partition_offsets().end(),
                                TInstant::MilliSeconds(::google::protobuf::util::TimeUtil::TimestampToMilliseconds(
                                    msg.write_time_high_watermark()))),
                           deferred);
    if (!pushRes) {
        AbortImpl();
        return;
    }
}

template <>
template <>
inline void TSingleClusterReadSessionImpl<false>::OnReadDoneImpl(
    Ydb::Topic::UpdateTokenResponse&& msg,
    TDeferredActions<false>& deferred) {
    Y_ABORT_UNLESS(Lock.IsLocked());
    // TODO
    Y_UNUSED(msg, deferred);
}

//////////////

template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::StartDecompressionTasksImpl(TDeferredActions<UseMigrationProtocol>& deferred) {
    Y_ABORT_UNLESS(Lock.IsLocked());

    if (Aborting) {
        return;
    }
    UpdateMemoryUsageStatisticsImpl();
    const i64 limit = GetDecompressedDataSizeLimit();
    Y_ABORT_UNLESS(limit > 0);
    while (DecompressedDataSize < limit
           && (static_cast<size_t>(CompressedDataSize + DecompressedDataSize) < Settings.MaxMemoryUsageBytes_
               || DecompressedDataSize == 0 /* Allow decompression of at least one message even if memory is full. */)
           && !DecompressionQueue.empty())
    {
        TDecompressionQueueItem& current = DecompressionQueue.front();
        auto sentToDecompress = current.BatchInfo->StartDecompressionTasks(Settings.DecompressionExecutor_,
                                                                           Max(limit - DecompressedDataSize, static_cast<i64>(1)),
                                                                           deferred);
        DecompressedDataSize += sentToDecompress;
        if (current.BatchInfo->AllDecompressionTasksStarted()) {
            DecompressionQueue.pop_front();
        } else {
            break;
        }
    }
}

template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::DestroyAllPartitionStreamsImpl(TDeferredActions<UseMigrationProtocol>& deferred) {
    Y_ABORT_UNLESS(Lock.IsLocked());

    using TClosedEvent = std::conditional_t<
        UseMigrationProtocol,
            NPersQueue::TReadSessionEvent::TPartitionStreamClosedEvent,
            NTopic::TReadSessionEvent::TPartitionSessionClosedEvent
    >;

    for (auto&& [key, partitionStream] : PartitionStreams) {
        bool pushRes = EventsQueue->PushEvent(partitionStream,
                                TClosedEvent(std::move(partitionStream), TClosedEvent::EReason::ConnectionLost),
                               deferred);
        if (!pushRes) {
            AbortImpl();
            return;
        }
    }
    PartitionStreams.clear();
    CookieMapping.ClearMapping();
}

template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::OnCreateNewDecompressionTask() {
    ++DecompressionTasksInflight;
}

template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::OnDecompressionInfoDestroy(i64 compressedSize, i64 decompressedSize, i64 messagesCount, i64 serverBytesSize)
{

    *Settings.Counters_->MessagesInflight -= messagesCount;
    *Settings.Counters_->BytesInflightUncompressed -= decompressedSize;
    *Settings.Counters_->BytesInflightCompressed -= compressedSize;
    *Settings.Counters_->BytesInflightTotal -= (compressedSize + decompressedSize);

    TDeferredActions<UseMigrationProtocol> deferred;
    std::lock_guard guard(Lock);
    UpdateMemoryUsageStatisticsImpl();

    CompressedDataSize -= compressedSize;
    DecompressedDataSize -= decompressedSize;

    if constexpr (!UseMigrationProtocol) {
        LOG_LAZY(Log, TLOG_DEBUG, GetLogPrefix() << "Returning serverBytesSize = " << serverBytesSize << " to budget");
        ReadSizeBudget += serverBytesSize;
    }

    ContinueReadingDataImpl();
    StartDecompressionTasksImpl(deferred);
}

template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::OnDataDecompressed(i64 sourceSize, i64 estimatedDecompressedSize, i64 decompressedSize, size_t messagesCount, i64 serverBytesSize) {

    TDeferredActions<UseMigrationProtocol> deferred;

    Y_ABORT_UNLESS(DecompressionTasksInflight > 0);
    --DecompressionTasksInflight;

    *Settings.Counters_->BytesRead += decompressedSize;
    *Settings.Counters_->BytesReadCompressed += sourceSize;
    *Settings.Counters_->MessagesRead += messagesCount;
    *Settings.Counters_->BytesInflightUncompressed += decompressedSize;
    *Settings.Counters_->BytesInflightCompressed -= sourceSize;
    *Settings.Counters_->BytesInflightTotal += (decompressedSize - sourceSize);

    std::lock_guard guard(Lock);
    UpdateMemoryUsageStatisticsImpl();
    CompressedDataSize -= sourceSize;
    DecompressedDataSize += decompressedSize - estimatedDecompressedSize;
    constexpr double weight = 0.6;
    if (sourceSize > 0) {
        AverageCompressionRatio = weight * static_cast<double>(decompressedSize) / static_cast<double>(sourceSize) + (1 - weight) * AverageCompressionRatio;
    }
    if (Aborting) {
        return;
    }
    if constexpr (!UseMigrationProtocol) {
        LOG_LAZY(Log, TLOG_DEBUG, GetLogPrefix() << "Returning serverBytesSize = " << serverBytesSize << " to budget");
        ReadSizeBudget += serverBytesSize;
    }
    ContinueReadingDataImpl();
    StartDecompressionTasksImpl(deferred);
}

template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::Abort() {
    LOG_LAZY(Log, TLOG_DEBUG, GetLogPrefix() << "Abort session to cluster");

    std::lock_guard guard(Lock);
    AbortImpl();
}

template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::AbortSession(TASessionClosedEvent<UseMigrationProtocol>&& closeEvent) {
    TDeferredActions<UseMigrationProtocol> deferred;
    LOG_LAZY(Log, TLOG_INFO, GetLogPrefix() << "Closing session to cluster: " << closeEvent.DebugString());

    EventsQueue->Close(closeEvent, deferred);
}


template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::AbortImpl() {
    Y_ABORT_UNLESS(Lock.IsLocked());

    if (!Aborting) {
        Aborting = true;
        CallCloseCallbackImpl();

        // Cancel(ClientContext); // Don't cancel, because this is used only as factory for other contexts.
        Cancel(ConnectContext);
        Cancel(ConnectTimeoutContext);
        Cancel(ConnectDelayContext);

        if (Processor) {
            Processor->Cancel();
        }

        if constexpr (!UseMigrationProtocol) {
            if (DirectReadSessionManager) {
                DirectReadSessionManager->Close();
                DirectReadSessionManager.reset();
            }
        }
    }
}

template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::Close(std::function<void()> callback) {
    std::lock_guard guard(Lock);
    if (Aborting) {
        callback();
    }

    if (!Closing) {
        Closing = true;

        CloseCallback = std::move(callback);

        Cancel(ConnectContext);
        Cancel(ConnectTimeoutContext);
        Cancel(ConnectDelayContext);

        if (!Processor) {
            CallCloseCallbackImpl();
        } else {
            if (!HasCommitsInflightImpl()) {
                Processor->Cancel();
                CallCloseCallbackImpl();
            }
        }
    }

    AbortImpl();
}

template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::CallCloseCallbackImpl() {
    Y_ABORT_UNLESS(Lock.IsLocked());

    if (CloseCallback) {
        CloseCallback();
        CloseCallback = {};
    }
    AbortImpl();
}

template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::StopReadingData() {
    std::lock_guard guard(Lock);
    DataReadingSuspended = true;
}

template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::ResumeReadingData() {
    std::lock_guard guard(Lock);
    if (DataReadingSuspended) {
        DataReadingSuspended = false;
        ContinueReadingDataImpl();
    }
}

template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::DumpStatisticsToLog(TLogElement& log) {
    std::lock_guard guard(Lock);
    // cluster:topic:partition:stream-id:read-offset:committed-offset
    for (auto&& [key, partitionStream] : PartitionStreams) {
        if constexpr (UseMigrationProtocol) {
            log << " "
                << ClusterName
                << ':' << partitionStream->GetTopicPath()
                << ':' << partitionStream->GetPartitionId()
                << ':' << partitionStream->GetPartitionStreamId()
                << ':' << partitionStream->GetMaxReadOffset()
                << ':' << partitionStream->GetMaxCommittedOffset();
        } else {
            log << " "
                << "-"
                << ':' << partitionStream->GetTopicPath()
                << ':' << partitionStream->GetPartitionId()
                << ':' << partitionStream->GetPartitionSessionId()
                << ':' << partitionStream->GetMaxReadOffset()
                << ':' << partitionStream->GetMaxCommittedOffset();
        }
    }
}

template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::UpdateMemoryUsageStatisticsImpl() {
    Y_ABORT_UNLESS(Lock.IsLocked());

    const TInstant now = TInstant::Now();
    const ui64 delta = (now - UsageStatisticsLastUpdateTime).MilliSeconds();
    UsageStatisticsLastUpdateTime = now;
    const double percent = 100.0 / static_cast<double>(Settings.MaxMemoryUsageBytes_);

    Settings.Counters_->TotalBytesInflightUsageByTime->Collect((DecompressedDataSize + CompressedDataSize) * percent, delta);
    Settings.Counters_->UncompressedBytesInflightUsageByTime->Collect(DecompressedDataSize * percent, delta);
    Settings.Counters_->CompressedBytesInflightUsageByTime->Collect(CompressedDataSize * percent, delta);
}

template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::UpdateMemoryUsageStatistics() {
    std::lock_guard guard(Lock);
    UpdateMemoryUsageStatisticsImpl();
}

template<bool UseMigrationProtocol>
bool TSingleClusterReadSessionImpl<UseMigrationProtocol>::GetRangesMode() const {
    if constexpr (UseMigrationProtocol) {
        return Settings.RangesMode_.value_or(RangesMode);
    } else {
        return true;
    }
}

template<bool UseMigrationProtocol>
bool TSingleClusterReadSessionImpl<UseMigrationProtocol>::TPartitionCookieMapping::AddMapping(const typename TCookie::TPtr& cookie) {
    if (!Cookies.emplace(cookie->GetKey(), cookie).second) {
        return false;
    }
    for (ui64 offset = cookie->OffsetRange.first; offset < cookie->OffsetRange.second; ++offset) {
        if (!UncommittedOffsetToCookie.emplace(std::make_pair(GetPartitionStreamId(cookie->PartitionStream.Get()), offset), cookie).second) {
            return false;
        }
    }
    PartitionStreamIdToCookie.emplace(GetPartitionStreamId(cookie->PartitionStream.Get()), cookie);
    return true;
}

template<bool UseMigrationProtocol>
typename TSingleClusterReadSessionImpl<UseMigrationProtocol>::TPartitionCookieMapping::TCookie::TPtr TSingleClusterReadSessionImpl<UseMigrationProtocol>::TPartitionCookieMapping::CommitOffset(ui64 partitionStreamId, ui64 offset) {
    auto cookieIt = UncommittedOffsetToCookie.find(std::make_pair(partitionStreamId, offset));
    if (cookieIt != UncommittedOffsetToCookie.end()) {
        typename TCookie::TPtr cookie;
        if (!--cookieIt->second->UncommittedMessagesLeft) {
            ++CommitInflight;
            cookie = cookieIt->second;
        }
        UncommittedOffsetToCookie.erase(cookieIt);
        return cookie;
    } else {
        ThrowFatalError(TStringBuilder() << "Invalid offset " << offset << ". Partition stream id: " << partitionStreamId << Endl);
    }
    // If offset wasn't found, there might be already hard released partition.
    // This situation is OK.
    return nullptr;
}

template<bool UseMigrationProtocol>
typename TSingleClusterReadSessionImpl<UseMigrationProtocol>::TPartitionCookieMapping::TCookie::TPtr TSingleClusterReadSessionImpl<UseMigrationProtocol>::TPartitionCookieMapping::RetrieveCommittedCookie(const Ydb::PersQueue::V1::CommitCookie& cookieProto) {
    typename TCookie::TPtr cookieInfo;
    auto cookieIt = Cookies.find(typename TCookie::TKey(cookieProto.assign_id(), cookieProto.partition_cookie()));
    if (cookieIt != Cookies.end()) {
        --CommitInflight;
        cookieInfo = cookieIt->second;
        Cookies.erase(cookieIt);

        auto [rangeBegin, rangeEnd] = PartitionStreamIdToCookie.equal_range(GetPartitionStreamId(cookieInfo->PartitionStream.Get()));
        for (auto i = rangeBegin; i != rangeEnd; ++i) {
            if (i->second == cookieInfo) {
                PartitionStreamIdToCookie.erase(i);
                break;
            }
        }
    }
    return cookieInfo;
}

template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::TPartitionCookieMapping::RemoveMapping(ui64 partitionStreamId) {
    auto [rangeBegin, rangeEnd] = PartitionStreamIdToCookie.equal_range(partitionStreamId);
    for (auto i = rangeBegin; i != rangeEnd; ++i) {
        typename TCookie::TPtr cookie = i->second;
        Cookies.erase(cookie->GetKey());
        for (ui64 offset = cookie->OffsetRange.first; offset < cookie->OffsetRange.second; ++offset) {
            UncommittedOffsetToCookie.erase(std::make_pair(partitionStreamId, offset));
        }
    }
    PartitionStreamIdToCookie.erase(rangeBegin, rangeEnd);
}

template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::TPartitionCookieMapping::ClearMapping() {
    Cookies.clear();
    UncommittedOffsetToCookie.clear();
    PartitionStreamIdToCookie.clear();
    CommitInflight = 0;
}

template<bool UseMigrationProtocol>
bool TSingleClusterReadSessionImpl<UseMigrationProtocol>::TPartitionCookieMapping::HasUnacknowledgedCookies() const {
    return CommitInflight != 0;
}

template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::RegisterParentPartition(ui32 partitionId, ui32 parentPartitionId, ui64 parentPartitionSessionId) {
    auto& values = HierarchyData[partitionId];
    values.push_back({parentPartitionId, parentPartitionSessionId});
}

template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::UnregisterPartition(ui32 partitionId, ui64 partitionSessionId) {
    for (auto it = HierarchyData.begin(); it != HierarchyData.end();) {
        auto& values = it->second;
        for (auto v = values.begin(); v != values.end();) {
            if (v->PartitionId == partitionId && v->PartitionSessionId < partitionSessionId) {
                v = values.erase(v);
            } else {
                ++v;
            }
        }
        if (values.empty()) {
            it = HierarchyData.erase(it);
        } else {
            ++it;
        }
    }
}

template<bool UseMigrationProtocol>
std::vector<ui64> TSingleClusterReadSessionImpl<UseMigrationProtocol>::GetParentPartitionSessions(ui32 partitionId, ui64 partitionSessionId) {
    auto it = HierarchyData.find(partitionId);
    if (it == HierarchyData.end()) {
        return {};
    }

    auto& parents = it->second;

    std::unordered_map<ui32, ui64> index;
    for (auto& v : parents) {
        if (v.PartitionSessionId > partitionSessionId) {
            break;
        }

        index[v.PartitionId] = v.PartitionSessionId;
    }

    std::vector<ui64> result;
    for (auto [_, v] : index) {
        result.push_back(v);
    }

    return result;
}

template<bool UseMigrationProtocol>
bool TSingleClusterReadSessionImpl<UseMigrationProtocol>::AllParentSessionsHasBeenRead(ui32 partitionId, ui64 partitionSessionId) {
    for (auto id : GetParentPartitionSessions(partitionId, partitionSessionId)) {
        if (!ReadingFinishedData.contains(id)) {
            return false;
        }
    }

    return true;
}

template<bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::ConfirmPartitionStreamEnd(TPartitionStreamImpl<UseMigrationProtocol>* partitionStream, const std::vector<ui32>& childIds) {
    ReadingFinishedData.insert(partitionStream->GetPartitionSessionId());
    for (auto& [_, s] : PartitionStreams) {
        for (auto partitionId : childIds) {
            if (s->GetPartitionId() == partitionId) {
                EventsQueue->SignalReadyEvents(s);
                break;
            }
        }
    }
}

template <bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::CollectOffsets(TTransactionBase& tx,
                                                                         const std::vector<TReadSessionEvent::TEvent>& events,
                                                                         std::shared_ptr<TTopicClient::TImpl> client)
{
    auto txInfo = GetOrCreateTxInfo(MakeTransactionId(tx));
    TrySubscribeOnTransactionCommit(tx, std::move(client));
    with_lock (txInfo->Lock) {
        txInfo->OffsetsCollector.CollectOffsets(events);
    }
}

template <bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::CollectOffsets(TTransactionBase& tx,
                                                                         const TReadSessionEvent::TEvent& event,
                                                                         std::shared_ptr<TTopicClient::TImpl> client)
{
    auto txInfo = GetOrCreateTxInfo(MakeTransactionId(tx));
    TrySubscribeOnTransactionCommit(tx, std::move(client));
    with_lock (txInfo->Lock) {
        txInfo->OffsetsCollector.CollectOffsets(event);
    }
}

template <bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::TrySubscribeOnTransactionCommit(TTransactionBase& tx,
                                                                                          std::shared_ptr<TTopicClient::TImpl> client)
{
    const TTransactionId txId = MakeTransactionId(tx);
    auto txInfo = GetOrCreateTxInfo(txId);
    Y_ABORT_UNLESS(txInfo);

    with_lock (txInfo->Lock) {
        if (txInfo->Subscribed) {
            return;
        }

        txInfo->IsActive = true;
        txInfo->Subscribed = true;

        auto callback = [cbContext = this->SelfContext, txId, txInfo, consumer = Settings.ConsumerName_, client]() {
            std::vector<TTopicOffsets> offsets;

            with_lock (txInfo->Lock) {
                Y_ABORT_UNLESS(!txInfo->CommitCalled);

                txInfo->CommitCalled = true;

                offsets = txInfo->OffsetsCollector.GetOffsets();
            }

            if (auto self = cbContext->LockShared()) {
                self->DeleteTx(txId);
            }

            return client->UpdateOffsetsInTransaction(txId,
                                                      offsets,
                                                      consumer,
                                                      {});
        };

        tx.AddPrecommitCallback(std::move(callback));
    }
}

template <bool UseMigrationProtocol>
auto TSingleClusterReadSessionImpl<UseMigrationProtocol>::GetOrCreateTxInfo(const TTransactionId& txId) -> TTransactionInfoPtr
{
    with_lock (Lock) {
        auto p = Txs.find(txId);
        if (p == Txs.end()) {
            TTransactionInfoPtr& txInfo = Txs[txId];
            txInfo = std::make_shared<TTransactionInfo>();
            txInfo->Subscribed = false;
            txInfo->CommitCalled = false;
            p = Txs.find(txId);
        }
        return p->second;
    }
}

template <bool UseMigrationProtocol>
void TSingleClusterReadSessionImpl<UseMigrationProtocol>::DeleteTx(const TTransactionId& txId)
{
    with_lock (Lock) {
        Txs.erase(txId);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TReadSessionEventInfo

template<bool UseMigrationProtocol>
TReadSessionEventInfo<UseMigrationProtocol>::TReadSessionEventInfo(TIntrusivePtr<TPartitionStreamImpl<UseMigrationProtocol>> partitionStream,
                                                                   TCallbackContextPtr<UseMigrationProtocol> cbContext,
                                                                   TEvent event)
    : PartitionStream(std::move(partitionStream))
    , Event(std::move(event))
    , CbContext(std::move(cbContext))
{
}

template<bool UseMigrationProtocol>
TReadSessionEventInfo<UseMigrationProtocol>::TReadSessionEventInfo(TIntrusivePtr<TPartitionStreamImpl<UseMigrationProtocol>> partitionStream,
                                                                   TCallbackContextPtr<UseMigrationProtocol> cbContext,
                                                                   bool hasDataEvents)
    : PartitionStream(std::move(partitionStream))
    , HasDataEvents(hasDataEvents)
    , EventsCount(1)
    , CbContext(std::move(cbContext))
{
}

template<bool UseMigrationProtocol>
bool TReadSessionEventInfo<UseMigrationProtocol>::IsEmpty() const {
    return !PartitionStream || !PartitionStream->HasEvents();
}

template<bool UseMigrationProtocol>
bool TReadSessionEventInfo<UseMigrationProtocol>::IsDataEvent() const {
    return !IsEmpty() && PartitionStream->TopEvent().IsDataEvent();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TReadSessionEventsQueue

template <bool UseMigrationProtocol>
TReadSessionEventsQueue<UseMigrationProtocol>::TReadSessionEventsQueue(
    const TAReadSessionSettings<UseMigrationProtocol>& settings)
    : TParent(settings) {
    const auto& h = TParent::Settings.EventHandlers_;

    if constexpr (UseMigrationProtocol) {
        HasEventCallbacks = (h.CommonHandler_
                             || h.DataReceivedHandler_
                             || h.CommitAcknowledgementHandler_
                             || h.CreatePartitionStreamHandler_
                             || h.DestroyPartitionStreamHandler_
                             || h.PartitionStreamStatusHandler_
                             || h.PartitionStreamClosedHandler_
                             || h.SessionClosedHandler_);
    } else {
        HasEventCallbacks = (h.CommonHandler_
                             || h.DataReceivedHandler_
                             || h.CommitOffsetAcknowledgementHandler_
                             || h.StartPartitionSessionHandler_
                             || h.StopPartitionSessionHandler_
                             || h.EndPartitionSessionHandler_
                             || h.PartitionSessionStatusHandler_
                             || h.PartitionSessionClosedHandler_
                             || h.SessionClosedHandler_);
    }
}

template <bool UseMigrationProtocol>
bool TReadSessionEventsQueue<UseMigrationProtocol>::PushEvent(TIntrusivePtr<TPartitionStreamImpl<UseMigrationProtocol>> stream,
                                                              typename TAReadSessionEvent<UseMigrationProtocol>::TEvent event,
                                                              TDeferredActions<UseMigrationProtocol>& deferred)
{
    std::lock_guard<std::mutex> guard(TParent::Mutex);
    if (TParent::Closed) {
        return false;
    }
    //TODO: check session closed event and return false
    using TClosedEvent = std::conditional_t<
        UseMigrationProtocol,
            NPersQueue::TReadSessionEvent::TPartitionStreamClosedEvent,
            NTopic::TReadSessionEvent::TPartitionSessionClosedEvent
    >;

    if (std::holds_alternative<TClosedEvent>(event)) {
        stream->DeleteNotReadyTail(deferred);
    }

    if (!HasDataEventCallback() && !std::holds_alternative<TADataReceivedEvent<UseMigrationProtocol>>(event)) {
        // Call non-dataEvent callbacks immediately.
        if (TryApplyCallbackToEventImpl(event, deferred, CbContext)) {
            return true;
        }
    }

    stream->InsertEvent(std::move(event));
    Y_ASSERT(stream->HasEvents());

    SignalReadyEventsImpl(stream, deferred);
    return true;
}

template <bool UseMigrationProtocol>
void TReadSessionEventsQueue<UseMigrationProtocol>::SignalEventImpl(
    TIntrusivePtr<TPartitionStreamImpl<UseMigrationProtocol>> partitionStream,
    TDeferredActions<UseMigrationProtocol>& deferred,
    bool isDataEvent)
{
    if (TParent::Closed) {
        return;
    }

    auto cbContext = partitionStream->GetCbContext();

    if (TParent::Events.empty()) {
        TParent::Events.emplace(std::move(partitionStream), std::move(cbContext), isDataEvent);
    } else {
        auto& event = TParent::Events.back();
        if (event.HasDataEvents
            && isDataEvent
            && (event.PartitionStream == partitionStream)) {
            ++event.EventsCount;
        } else {
            TParent::Events.emplace(std::move(partitionStream), std::move(cbContext), isDataEvent);
        }
    }

    SignalWaiterImpl(deferred);
}

template <bool UseMigrationProtocol>
bool TReadSessionEventsQueue<UseMigrationProtocol>::PushDataEvent(TIntrusivePtr<TPartitionStreamImpl<UseMigrationProtocol>> partitionStream,
                                                                  size_t batch,
                                                                  size_t message,
                                                                  TDataDecompressionInfoPtr<UseMigrationProtocol> parent,
                                                                  std::atomic<bool>& ready)
{

    std::lock_guard<std::mutex> guard(TParent::Mutex);
    if (this->Closed) {
        return false;
    }
    partitionStream->InsertDataEvent(batch, message, parent, ready);
    return true;
}

template <bool UseMigrationProtocol>
void TRawPartitionStreamEventQueue<UseMigrationProtocol>::GetDataEventImpl(TIntrusivePtr<TPartitionStreamImpl<UseMigrationProtocol>> partitionStream,
                                                                           size_t& maxEventsCount,
                                                                           size_t& maxByteSize,
                                                                           std::vector<typename TADataReceivedEvent<UseMigrationProtocol>::TMessage>& messages,
                                                                           std::vector<typename TADataReceivedEvent<UseMigrationProtocol>::TCompressedMessage>& compressedMessages,
                                                                           TUserRetrievedEventsInfoAccumulator<UseMigrationProtocol>& accumulator)
{
    GetDataEventImpl(partitionStream,
                     maxEventsCount,
                     maxByteSize,
                     messages,
                     compressedMessages,
                     accumulator,
                     Ready);
}

template <bool UseMigrationProtocol>
void TRawPartitionStreamEventQueue<UseMigrationProtocol>::GetDataEventImpl(TIntrusivePtr<TPartitionStreamImpl<UseMigrationProtocol>> partitionStream,
                                                                           size_t& maxEventsCount,
                                                                           size_t& maxByteSize,
                                                                           std::vector<typename TADataReceivedEvent<UseMigrationProtocol>::TMessage>& messages,
                                                                           std::vector<typename TADataReceivedEvent<UseMigrationProtocol>::TCompressedMessage>& compressedMessages,
                                                                           TUserRetrievedEventsInfoAccumulator<UseMigrationProtocol>& accumulator,
                                                                           std::deque<TRawPartitionStreamEvent<UseMigrationProtocol>>& queue)
{
    auto readyDataInTheHead = [&queue]() {
        if (queue.empty()) {
            return false;
        }

        auto& front = queue.front();

        return front.IsDataEvent() && front.IsReady();
    };

    Y_ABORT_UNLESS(readyDataInTheHead());

    for (; readyDataInTheHead() && (maxEventsCount > 0) && (maxByteSize > 0); --maxEventsCount) {
        auto& event = queue.front().GetDataEvent();

        TDataDecompressionInfoPtr<UseMigrationProtocol> parent = event.GetParent();
        size_t size = 0;

        event.TakeData(partitionStream, messages, compressedMessages, maxByteSize, size);
        queue.pop_front();

        accumulator.Add(parent, size);
    }
}

template <bool UseMigrationProtocol>
TADataReceivedEvent<UseMigrationProtocol>
TReadSessionEventsQueue<UseMigrationProtocol>::GetDataEventImpl(TIntrusivePtr<TPartitionStreamImpl<UseMigrationProtocol>> stream,
                                                                size_t& maxByteSize,
                                                                TUserRetrievedEventsInfoAccumulator<UseMigrationProtocol>& accumulator) // Assumes that we're under lock.
{
    std::vector<typename TADataReceivedEvent<UseMigrationProtocol>::TMessage> messages;
    std::vector<typename TADataReceivedEvent<UseMigrationProtocol>::TCompressedMessage> compressedMessages;

    Y_ABORT_UNLESS(!TParent::Events.empty());

    auto& event = TParent::Events.front();

    Y_ABORT_UNLESS(event.PartitionStream == stream);
    Y_ABORT_UNLESS(event.EventsCount > 0);

    TPartitionStreamImpl<UseMigrationProtocol>::GetDataEventImpl(stream,
                                                                 event.EventsCount,
                                                                 maxByteSize,
                                                                 messages,
                                                                 compressedMessages,
                                                                 accumulator);

    if (event.EventsCount == 0) {
        TParent::Events.pop();
    }

    Y_ABORT_UNLESS(!messages.empty() || !compressedMessages.empty());

    return {std::move(messages), std::move(compressedMessages), stream};
}

template <bool UseMigrationProtocol>
TReadSessionEventInfo<UseMigrationProtocol>
TReadSessionEventsQueue<UseMigrationProtocol>::GetEventImpl(size_t& maxByteSize,
                                                            TUserRetrievedEventsInfoAccumulator<UseMigrationProtocol>& accumulator) // Assumes that we're under lock.
{
    Y_ASSERT(TParent::HasEventsImpl());

    if (!TParent::Events.empty()) {
        TReadSessionEventInfo<UseMigrationProtocol>& front = TParent::Events.front();
        auto partitionStream = front.PartitionStream;

        if (!partitionStream->HasEvents()) {
            Y_ABORT("can't be here - got events in global queue, but nothing in partition queue");
        }

        std::optional<typename TAReadSessionEvent<UseMigrationProtocol>::TEvent> event;
        auto frontCbContext = front.CbContext;
        if (partitionStream->TopEvent().IsDataEvent()) {
            event = GetDataEventImpl(partitionStream, maxByteSize, accumulator);
        } else {
            event = std::move(partitionStream->TopEvent().GetEvent());
            partitionStream->PopEvent();

            TParent::Events.pop();

            if constexpr (!UseMigrationProtocol) {
                if (std::holds_alternative<TReadSessionEvent::TPartitionSessionClosedEvent>(*event)) {
                     auto& e = std::get<TReadSessionEvent::TPartitionSessionClosedEvent>(*event);
                     if (auto session = frontCbContext->LockShared()) {
                        session->UnregisterPartition(e.GetPartitionSession()->GetPartitionId(), e.GetPartitionSession()->GetPartitionSessionId());
                     }
                }
            }
        }

        TParent::RenewWaiterImpl();

        return {partitionStream, std::move(frontCbContext), std::move(*event)};
    }

    Y_ASSERT(TParent::CloseEvent);

    return {*TParent::CloseEvent};
}

template <bool UseMigrationProtocol>
std::vector<typename TAReadSessionEvent<UseMigrationProtocol>::TEvent>
TReadSessionEventsQueue<UseMigrationProtocol>::GetEvents(bool block, std::optional<size_t> maxEventsCount, size_t maxByteSize)
{
    if (!maxByteSize) {
        ThrowFatalError("the maxByteSize value must be greater than 0");
    }

    std::vector<TReadSessionEventInfo<UseMigrationProtocol>> eventInfos;
    const size_t maxCount = maxEventsCount ? *maxEventsCount : std::numeric_limits<size_t>::max();
    TUserRetrievedEventsInfoAccumulator<UseMigrationProtocol> accumulator;
    {
        std::lock_guard<std::mutex> guard(TParent::Mutex);
        eventInfos.reserve(Min(TParent::Events.size() + TParent::CloseEvent.has_value(), maxCount));
        do {
            if (block) {
                TParent::WaitEventsImpl();
            }

            while (TParent::HasEventsImpl() && eventInfos.size() < maxCount && maxByteSize > 0) {
                TReadSessionEventInfo<UseMigrationProtocol> event = GetEventImpl(maxByteSize, accumulator);
                eventInfos.emplace_back(std::move(event));
                if (eventInfos.back().IsSessionClosedEvent()) {
                    break;
                }
            }
        } while (block && eventInfos.empty());
    }

    accumulator.OnUserRetrievedEvent();

    std::vector<typename TAReadSessionEvent<UseMigrationProtocol>::TEvent> result;
    result.reserve(eventInfos.size());
    for (TReadSessionEventInfo<UseMigrationProtocol>& eventInfo : eventInfos) {
        result.emplace_back(std::move(eventInfo.GetEvent()));
    }

    return result;
}

template <bool UseMigrationProtocol>
std::optional<typename TAReadSessionEvent<UseMigrationProtocol>::TEvent>
TReadSessionEventsQueue<UseMigrationProtocol>::GetEvent(bool block, size_t maxByteSize)
{
    if (!maxByteSize) {
        ThrowFatalError("the maxByteSize value must be greater than 0");
    }

    std::optional<TReadSessionEventInfo<UseMigrationProtocol>> eventInfo;
    TUserRetrievedEventsInfoAccumulator<UseMigrationProtocol> accumulator;
    {
        std::lock_guard<std::mutex> guard(TParent::Mutex);
        do {
            if (block) {
                TParent::WaitEventsImpl();
            }

            if (TParent::HasEventsImpl()) {
                eventInfo = GetEventImpl(maxByteSize, accumulator);
            }

        } while (block && !eventInfo);
    }

    accumulator.OnUserRetrievedEvent();

    if (eventInfo) {
        return std::move(eventInfo->Event);
    }

    return std::nullopt;
}

template <bool UseMigrationProtocol>
void TReadSessionEventsQueue<UseMigrationProtocol>::SignalReadyEvents(
    TIntrusivePtr<TPartitionStreamImpl<UseMigrationProtocol>> partitionStream) {
    Y_ASSERT(partitionStream);

    std::lock_guard<std::mutex> guard1(partitionStream->GetLock());
    TDeferredActions<UseMigrationProtocol> deferred;
    {
        std::lock_guard<std::mutex> g(TParent::Mutex);
        SignalReadyEventsImpl(partitionStream, deferred);
    }
}

template <bool UseMigrationProtocol>
void TReadSessionEventsQueue<UseMigrationProtocol>::SignalReadyEventsImpl(
    TIntrusivePtr<TPartitionStreamImpl<UseMigrationProtocol>> partitionStream,
    TDeferredActions<UseMigrationProtocol>& deferred) {
    TPartitionStreamImpl<UseMigrationProtocol>::SignalReadyEvents(partitionStream, this, deferred);
}

template <bool UseMigrationProtocol>
bool TReadSessionEventsQueue<UseMigrationProtocol>::TryApplyCallbackToEventImpl(typename TParent::TEvent& event,
                                                                                TDeferredActions<UseMigrationProtocol>& deferred,
                                                                                TCallbackContextPtr<UseMigrationProtocol>& cbContext)
{
    THandlersVisitor visitor(TParent::Settings, event, deferred, cbContext);
    return visitor.Visit();
}

template <bool UseMigrationProtocol>
bool TReadSessionEventsQueue<UseMigrationProtocol>::HasDataEventCallback() const
{
    if (!HasEventCallbacks) {
        return false;
    }

    if (TParent::Settings.EventHandlers_.DataReceivedHandler_) {
        return true;
    }
    if (TParent::Settings.EventHandlers_.CommonHandler_) {
        return true;
    }

    return false;
}

template <bool UseMigrationProtocol>
void TReadSessionEventsQueue<UseMigrationProtocol>::ApplyCallbackToEventImpl(TADataReceivedEvent<UseMigrationProtocol>& data,
                                                                             TUserRetrievedEventsInfoAccumulator<UseMigrationProtocol>&& eventsInfo,
                                                                             TDeferredActions<UseMigrationProtocol>& deferred)
{
    Y_ABORT_UNLESS(HasEventCallbacks);

    if (TParent::Settings.EventHandlers_.DataReceivedHandler_) {
        auto action = [func = TParent::Settings.EventHandlers_.DataReceivedHandler_,
                       data = std::move(data),
                       eventsInfo = std::move(eventsInfo)]() mutable {
            func(data);
            eventsInfo.OnUserRetrievedEvent();
        };

        deferred.DeferStartExecutorTask(TParent::Settings.EventHandlers_.HandlersExecutor_, std::move(action));
    } else if (TParent::Settings.EventHandlers_.CommonHandler_) {
        auto action = [func = TParent::Settings.EventHandlers_.CommonHandler_,
                       data = std::move(data),
                       eventsInfo = std::move(eventsInfo)]() mutable {
            typename TParent::TEvent event(std::move(data));

            func(event);
            eventsInfo.OnUserRetrievedEvent();
        };

        deferred.DeferStartExecutorTask(TParent::Settings.EventHandlers_.HandlersExecutor_, std::move(action));
    } else {
        Y_ABORT_UNLESS(false);
    }
}

template <bool UseMigrationProtocol>
void TReadSessionEventsQueue<UseMigrationProtocol>::GetDataEventCallbackSettings(size_t& maxMessagesBytes)
{
    maxMessagesBytes = TParent::Settings.EventHandlers_.MaxMessagesBytes_;
}

template<bool UseMigrationProtocol>
void TReadSessionEventsQueue<UseMigrationProtocol>::ClearAllEvents() {
    std::lock_guard<std::mutex> guard(TParent::Mutex);
    while (!TParent::Events.empty()) {
        auto& event = TParent::Events.front();
        if (event.PartitionStream && event.PartitionStream->HasEvents()) {
            event.PartitionStream->PopEvent();
        }
        TParent::Events.pop();
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TDataDecompressionInfo

template<bool UseMigrationProtocol>
TDataDecompressionInfo<UseMigrationProtocol>::TDataDecompressionInfo(
    TPartitionData<UseMigrationProtocol>&& msg,
    TCallbackContextPtr<UseMigrationProtocol> cbContext,
    bool doDecompress,
    i64 serverBytesSize
)
    : ServerMessage(std::move(msg))
    , CbContext(std::move(cbContext))
    , DoDecompress(doDecompress)
    , ServerBytesSize(serverBytesSize)
{
    i64 compressedSize = 0;
    i64 messagesCount = 0;

    for (const auto& batch : ServerMessage.batches()) {
        for (const auto& messageData : batch.message_data()) {
            compressedSize += messageData.data().size();
            ++messagesCount;
        }
    }

    MessagesInflight = messagesCount;
    SourceDataNotProcessed = compressedSize;
    CompressedDataSize = compressedSize;

    BuildBatchesMeta();
}

template<bool UseMigrationProtocol>
TDataDecompressionInfo<UseMigrationProtocol>::~TDataDecompressionInfo()
{
    if (auto session = CbContext->LockShared()) {
        session->OnDecompressionInfoDestroy(CompressedDataSize, DecompressedDataSize, MessagesInflight, ServerBytesSize);
    }
}

template<bool UseMigrationProtocol>
void TDataDecompressionInfo<UseMigrationProtocol>::BuildBatchesMeta() {
    BatchesMeta.reserve(ServerMessage.batches_size());
    if constexpr (!UseMigrationProtocol) {
        MessagesMeta.reserve(ServerMessage.batches_size());
    }
    for (const auto& batch : ServerMessage.batches()) {
        // Extra fields.
        typename TAWriteSessionMeta<UseMigrationProtocol>::TPtr meta = MakeIntrusive<TAWriteSessionMeta<UseMigrationProtocol>>();

        if constexpr (UseMigrationProtocol) {
            meta->Fields.reserve(batch.extra_fields_size());
            for (const Ydb::PersQueue::V1::KeyValue& kv : batch.extra_fields()) {
                meta->Fields.emplace(kv.key(), kv.value());
            }
        } else {
            meta->Fields.reserve(batch.write_session_meta_size());
            for (const auto& [key, value] : batch.write_session_meta()) {
                meta->Fields.emplace(key, value);
            }
            MessagesMeta.emplace_back(TMessageMetaPtrVector{});
            auto& currBatchMessagesMeta = MessagesMeta.back();
            for (const auto& messageData: batch.message_data()) {
                typename TAMessageMeta<UseMigrationProtocol>::TPtr msgMeta = MakeIntrusive<TAMessageMeta<UseMigrationProtocol>>();
                msgMeta->Fields.reserve(messageData.metadata_items_size());
                for (const auto& metaPair: messageData.metadata_items()) {
                    msgMeta->Fields.emplace_back(std::make_pair(metaPair.key(), metaPair.value()));
                }
                currBatchMessagesMeta.emplace_back(std::move(msgMeta));
                }
        }

        BatchesMeta.emplace_back(std::move(meta));
    }
}

template<bool UseMigrationProtocol>
void TDataDecompressionInfo<UseMigrationProtocol>::PutDecompressionError(std::exception_ptr error, size_t batch, size_t message) {
    if (!DecompressionErrorsStructCreated) {
        std::lock_guard guard(DecompressionErrorsStructLock);
        DecompressionErrors.resize(ServerMessage.batches_size());
        for (size_t batch = 0; batch < static_cast<size_t>(ServerMessage.batches_size()); ++batch) {
            DecompressionErrors[batch].resize(static_cast<size_t>(ServerMessage.batches(batch).message_data_size()));
        }

        // Set barrier.
        DecompressionErrorsStructCreated = true;
    }
    Y_ASSERT(batch < DecompressionErrors.size());
    Y_ASSERT(message < DecompressionErrors[batch].size());
    DecompressionErrors[batch][message] = std::move(error);
}

template<bool UseMigrationProtocol>
std::exception_ptr TDataDecompressionInfo<UseMigrationProtocol>::GetDecompressionError(size_t batch, size_t message) {
    if (!DecompressionErrorsStructCreated) {
        return {};
    }
    Y_ASSERT(batch < DecompressionErrors.size());
    Y_ASSERT(message < DecompressionErrors[batch].size());
    return DecompressionErrors[batch][message];
}

template <bool UseMigrationProtocol>
i64 TDataDecompressionInfo<UseMigrationProtocol>::StartDecompressionTasks(
    const typename IAExecutor<UseMigrationProtocol>::TPtr& executor, i64 availableMemory,
    TDeferredActions<UseMigrationProtocol>& deferred)
{
    auto session = CbContext->LockShared();
    Y_ASSERT(session);

    i64 used = 0;

    while (availableMemory > 0 && !Tasks.empty()) {
        auto& task = Tasks.front();

        used += task.GetEstimatedDecompressedSize();
        availableMemory -= task.GetEstimatedDecompressedSize();

        session->OnCreateNewDecompressionTask();

        deferred.DeferStartExecutorTask(executor, std::move(task));
        Tasks.pop_front();
    }

    return used;
}

template<bool UseMigrationProtocol>
void TDataDecompressionInfo<UseMigrationProtocol>::PlanDecompressionTasks(double averageCompressionRatio,
                                                                          TIntrusivePtr<TPartitionStreamImpl<UseMigrationProtocol>> partitionStream) {
    constexpr size_t TASK_LIMIT = 512_KB;

    auto session = CbContext->LockShared();
    Y_ASSERT(session);

    ReadyThresholds.emplace_back();

    TDecompressionTask task(TDataDecompressionInfo::shared_from_this(), partitionStream, &ReadyThresholds.back());

    while (CurrentDecompressingMessage.first < static_cast<size_t>(ServerMessage.batches_size())) {
        const auto& batch = ServerMessage.batches(CurrentDecompressingMessage.first);

        if (CurrentDecompressingMessage.second < static_cast<size_t>(batch.message_data_size())) {
            const auto& messageData = batch.message_data(CurrentDecompressingMessage.second);
            const i64 size = static_cast<i64>(messageData.data().size());
            const i64 estimatedDecompressedSize = messageData.uncompressed_size()
                                                      ? static_cast<i64>(messageData.uncompressed_size())
                                                      : static_cast<i64>(size * averageCompressionRatio);
            Y_ABORT_UNLESS(estimatedDecompressedSize >= 0);

            task.Add(CurrentDecompressingMessage.first, CurrentDecompressingMessage.second, size, estimatedDecompressedSize);

            bool pushRes = session->GetEventsQueue()->PushDataEvent(partitionStream,
                                                     CurrentDecompressingMessage.first,
                                                     CurrentDecompressingMessage.second,
                                                     TDataDecompressionInfo::shared_from_this(),
                                                     ReadyThresholds.back().Ready);
            if (!pushRes) {
                session->AbortImpl();
                return;
            }
        }

        ++CurrentDecompressingMessage.second;

        if (CurrentDecompressingMessage.second >= static_cast<size_t>(batch.message_data_size())) { // next batch
            ++CurrentDecompressingMessage.first;
            CurrentDecompressingMessage.second = 0;
        }

        if (task.AddedDataSize() >= TASK_LIMIT) {
            Tasks.push_back(std::move(task));

            ReadyThresholds.emplace_back();
            task = TDecompressionTask(TDataDecompressionInfo::shared_from_this(), partitionStream, &ReadyThresholds.back());
        }
    }

    if (task.AddedMessagesCount() > 0) {
        Tasks.push_back(std::move(task));
    } else {
        ReadyThresholds.pop_back(); // Revert.
    }
}

template <bool UseMigrationProtocol>
void TDataDecompressionInfo<UseMigrationProtocol>::OnDestroyReadSession()
{
    for (auto& task : Tasks) {
        task.ClearParent();
    }
}

template<bool UseMigrationProtocol>
void TDataDecompressionEvent<UseMigrationProtocol>::TakeData(TIntrusivePtr<TPartitionStreamImpl<UseMigrationProtocol>> partitionStream,
                                                             std::vector<typename TADataReceivedEvent<UseMigrationProtocol>::TMessage>& messages,
                                                             std::vector<typename TADataReceivedEvent<UseMigrationProtocol>::TCompressedMessage>& compressedMessages,
                                                             size_t& maxByteSize,
                                                             size_t& dataSize) const
{
    auto& msg = Parent->GetServerMessage();
    i64 minOffset = Max<i64>();
    i64 maxOffset = 0;
    auto& batch = *msg.mutable_batches(Batch);
    const auto& meta = Parent->GetBatchMeta(Batch);
    const TInstant batchWriteTimestamp = [&batch](){
        if constexpr (UseMigrationProtocol) {
            return TInstant::MilliSeconds(batch.write_timestamp_ms());
        } else {
            return TInstant::MilliSeconds(::google::protobuf::util::TimeUtil::TimestampToMilliseconds(batch.written_at()));
        }
    }();
    auto& messageData = *batch.mutable_message_data(Message);

    minOffset = Min(minOffset, static_cast<i64>(messageData.offset()));
    maxOffset = Max(maxOffset, static_cast<i64>(messageData.offset()));

    if constexpr (UseMigrationProtocol) {
        using TMessageInformation = NPersQueue::TReadSessionEvent::TDataReceivedEvent::TMessageInformation;

        TMessageInformation messageInfo(messageData.offset(),
                                        batch.source_id(),
                                        messageData.seq_no(),
                                        TInstant::MilliSeconds(messageData.create_timestamp_ms()),
                                        batchWriteTimestamp,
                                        batch.ip(),
                                        meta,
                                        messageData.uncompressed_size());

        if (Parent->GetDoDecompress()) {
            messages.emplace_back(messageData.data(),
                                  Parent->GetDecompressionError(Batch, Message),
                                  messageInfo,
                                  partitionStream,
                                  messageData.partition_key(),
                                  messageData.explicit_hash());
        } else {
            compressedMessages.emplace_back(static_cast<NPersQueue::ECodec>(messageData.codec()),
                                            messageData.data(),
                                            std::vector<TMessageInformation>{messageInfo},
                                            partitionStream,
                                            messageData.partition_key(),
                                            messageData.explicit_hash());
        }
    } else {
        const auto& messageMeta = Parent->GetMessageMeta(Batch, Message);
        TReadSessionEvent::TDataReceivedEvent::TMessageInformation messageInfo(
                messageData.offset(),
                batch.producer_id(),
                messageData.seq_no(),
                TInstant::MilliSeconds(::google::protobuf::util::TimeUtil::TimestampToMilliseconds(messageData.created_at())),
                batchWriteTimestamp,
                meta,
                messageMeta,
                messageData.uncompressed_size(),
                messageData.message_group_id()
        );

        if (Parent->GetDoDecompress()) {
            messages.emplace_back(messageData.data(),
                                  Parent->GetDecompressionError(Batch, Message),
                                  messageInfo,
                                  partitionStream);
        } else {
            compressedMessages.emplace_back(static_cast<ECodec>(batch.codec()),
                                            messageData.data(),
                                            messageInfo,
                                            partitionStream);
        }
    }

    maxByteSize -= Min(maxByteSize, messageData.data().size());

    dataSize += messageData.data().size();

    // Clear data to free internal session's memory.
    messageData.clear_data();

    LOG_LAZY(partitionStream->GetLog(), TLOG_DEBUG, TStringBuilder()
                                        << "Take Data. Partition " << partitionStream->GetPartitionId()
                                        << ". Read: {" << Batch << ", " << Message << "} ("
                                        << minOffset << "-" << maxOffset << ")");
}

template<bool UseMigrationProtocol>
bool TDataDecompressionInfo<UseMigrationProtocol>::HasReadyUnreadData() const {
    std::optional<std::pair<size_t, size_t>> threshold = GetReadyThreshold();
    if (!threshold) {
        return false;
    }
    return CurrentReadingMessage <= *threshold;
}

template<bool UseMigrationProtocol>
void TDataDecompressionInfo<UseMigrationProtocol>::OnDataDecompressed(i64 sourceSize, i64 estimatedDecompressedSize, i64 decompressedSize, size_t messagesCount)
{
    CompressedDataSize -= sourceSize;
    DecompressedDataSize += decompressedSize;

    if (auto session = CbContext->LockShared()) {
        // TODO (ildar-khisam@): distribute total ServerBytesSize in proportion of source size
        // Use CompressedDataSize, sourceSize, ServerBytesSize
        session->OnDataDecompressed(sourceSize, estimatedDecompressedSize, decompressedSize, messagesCount, std::exchange(ServerBytesSize, 0));
    }
}

template<bool UseMigrationProtocol>
void TDataDecompressionInfo<UseMigrationProtocol>::OnUserRetrievedEvent(i64 decompressedSize, size_t messagesCount)
{
    MessagesInflight -= messagesCount;
    DecompressedDataSize -= decompressedSize;

    if (auto session = CbContext->LockShared()) {
        session->OnUserRetrievedEvent(decompressedSize, messagesCount);
    }
}

template <bool UseMigrationProtocol>
void TDataDecompressionInfo<UseMigrationProtocol>::TDecompressionTask::Add(size_t batch, size_t message,
                                                                           size_t sourceDataSize,
                                                                           size_t estimatedDecompressedSize) {
    if (Messages.empty() || Messages.back().Batch != batch) {
        Messages.push_back({ batch, { message, message + 1 } });
    }
    Messages.back().MessageRange.second = message + 1;
    SourceDataSize += sourceDataSize;
    EstimatedDecompressedSize += estimatedDecompressedSize;
    Ready->Batch = batch;
    Ready->Message = message;
}

template <bool UseMigrationProtocol>
TDataDecompressionInfo<UseMigrationProtocol>::TDecompressionTask::TDecompressionTask(
    TDataDecompressionInfo::TPtr parent, TIntrusivePtr<TPartitionStreamImpl<UseMigrationProtocol>> partitionStream,
    TReadyMessageThreshold* ready)
    : Parent(std::move(parent))
    , PartitionStream(std::move(partitionStream))
    , Ready(ready) {
}

template<bool UseMigrationProtocol>
void TDataDecompressionInfo<UseMigrationProtocol>::TDecompressionTask::operator()() {
    auto parent = Parent;
    if (!parent) {
        return;
    }
    i64 minOffset = Max<i64>();
    i64 maxOffset = 0;
    i64 dataProcessed = 0;
    size_t messagesProcessed = 0;
    for (const TMessageRange& messages : Messages) {
        auto& batch = *parent->ServerMessage.mutable_batches(messages.Batch);
        for (size_t i = messages.MessageRange.first; i < messages.MessageRange.second; ++i) {
            auto& data = *batch.mutable_message_data(i);

            ++messagesProcessed;
            dataProcessed += static_cast<i64>(data.data().size());
            minOffset = Min(minOffset, static_cast<i64>(data.offset()));
            maxOffset = Max(maxOffset, static_cast<i64>(data.offset()));

            try {
                if constexpr (UseMigrationProtocol) {
                    if (parent->DoDecompress
                        && data.codec() != Ydb::PersQueue::V1::CODEC_RAW
                        && data.codec() != Ydb::PersQueue::V1::CODEC_UNSPECIFIED
                    ) {
                        const ICodec* codecImpl = TCodecMap::GetTheCodecMap().GetOrThrow(static_cast<ui32>(data.codec()));
                        std::string decompressed = codecImpl->Decompress(data.data());
                        data.set_data(TStringType{decompressed});
                        data.set_codec(Ydb::PersQueue::V1::CODEC_RAW);
                    }
                } else {
                    if (parent->DoDecompress
                        && static_cast<Ydb::Topic::Codec>(batch.codec()) != Ydb::Topic::CODEC_RAW
                        && static_cast<Ydb::Topic::Codec>(batch.codec()) != Ydb::Topic::CODEC_UNSPECIFIED
                    ) {
                        const ICodec* codecImpl = TCodecMap::GetTheCodecMap().GetOrThrow(static_cast<ui32>(batch.codec()));
                        std::string decompressed = codecImpl->Decompress(data.data());
                        data.set_data(TStringType{decompressed});
                    }
                }

                DecompressedSize += data.data().size();
            } catch (...) {
                parent->PutDecompressionError(std::current_exception(), messages.Batch, i);
                data.clear_data(); // Free memory, because we don't count it.

                if (auto session = parent->CbContext->LockShared()) {
                    session->GetLog() << TLOG_INFO << "Error decompressing data: " << CurrentExceptionMessage();
                }
            }
        }
    }

    if (auto session = parent->CbContext->LockShared()) {
        const i64 partition_id = [parent](){
            if constexpr (UseMigrationProtocol) {
                return parent->ServerMessage.partition();
            } else {
                return parent->ServerMessage.partition_session_id();
            }
        }();
        LOG_LAZY(session->GetLog(), TLOG_DEBUG, TStringBuilder() << "Decompression task done. Partition/PartitionSessionId: "
                                                                 << partition_id << " (" << minOffset << "-"
                                                                 << maxOffset << ")");
    }

    Y_ASSERT(dataProcessed == SourceDataSize);

    parent->OnDataDecompressed(SourceDataSize, EstimatedDecompressedSize, DecompressedSize, messagesProcessed);

    parent->SourceDataNotProcessed -= dataProcessed;
    Ready->Ready = true;

    if (auto session = parent->CbContext->LockShared()) {
        session->GetEventsQueue()->SignalReadyEvents(PartitionStream);
    }
}

template<bool UseMigrationProtocol>
void TDataDecompressionInfo<UseMigrationProtocol>::TDecompressionTask::ClearParent()
{
    Parent = nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TUserRetrievedEventsInfoAccumulator

template<bool UseMigrationProtocol>
void TUserRetrievedEventsInfoAccumulator<UseMigrationProtocol>::Add(TDataDecompressionInfoPtr<UseMigrationProtocol> info, i64 decompressedSize)
{
    auto& counter = Counters[info];

    counter.DecompressedSize += decompressedSize;
    ++counter.MessagesCount;
}

template<bool UseMigrationProtocol>
void TUserRetrievedEventsInfoAccumulator<UseMigrationProtocol>::OnUserRetrievedEvent() const
{
    for (auto& [parent, counter] : Counters) {
        parent->OnUserRetrievedEvent(counter.DecompressedSize, counter.MessagesCount);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TDeferredActions

template<bool UseMigrationProtocol>
void TDeferredActions<UseMigrationProtocol>::DeferReadFromProcessor(
    const IDirectReadProcessor::TPtr& processor,
    TDirectReadServerMessage* dst,
    IDirectReadProcessor::TReadCallback callback
) {
    Y_ASSERT(!DirectReadActions.Read);
    DirectReadActions.Read = {
        .Processor = processor,
        .ServerMessage = dst,
        .ReadCallback = std::move(callback),
    };
}

template<bool UseMigrationProtocol>
void TDeferredActions<UseMigrationProtocol>::DeferScheduleCallback(TDuration delay, std::function<void(bool)> callback, TSingleClusterReadSessionContextPtr contextPtr) {
    Y_ASSERT(!DirectReadActions.ScheduledCallback);
    DirectReadActions.ScheduledCallback = {
        .Callback = std::move(callback),
        .Delay = delay,
        .ContextPtr = contextPtr,
    };
}

template<bool UseMigrationProtocol>
void TDeferredActions<UseMigrationProtocol>::DeferCallback(std::function<void()> callback) {
    Y_ASSERT(!DirectReadActions.Callback);
    DirectReadActions.Callback = std::move(callback);
}

template<bool UseMigrationProtocol>
void TDeferredActions<UseMigrationProtocol>::DeferReadFromProcessor(const typename IProcessor<UseMigrationProtocol>::TPtr& processor,
                                              TServerMessage<UseMigrationProtocol>* dst,
                                              typename IProcessor<UseMigrationProtocol>::TReadCallback callback)
{
    Y_ASSERT(!Processor);
    Y_ASSERT(!ReadDst);
    Y_ASSERT(!ReadCallback);
    Processor = processor;
    ReadDst = dst;
    ReadCallback = std::move(callback);
}

template<bool UseMigrationProtocol>
void TDeferredActions<UseMigrationProtocol>::DeferStartExecutorTask(const typename IAExecutor<UseMigrationProtocol>::TPtr& executor, typename IAExecutor<UseMigrationProtocol>::TFunction&& task) {
    ExecutorsTasks.emplace_back(executor, std::move(task));
}

template<bool UseMigrationProtocol>
void TDeferredActions<UseMigrationProtocol>::DeferAbortSession(TCallbackContextPtr<UseMigrationProtocol> cbContext, TASessionClosedEvent<UseMigrationProtocol>&& closeEvent) {
    CbContext = std::move(cbContext);
    SessionClosedEvent.ConstructInPlace(std::move(closeEvent));
}

template<bool UseMigrationProtocol>
void TDeferredActions<UseMigrationProtocol>::DeferAbortSession(TCallbackContextPtr<UseMigrationProtocol> cbContext, EStatus statusCode, NYdb::NIssue::TIssues&& issues) {
    DeferAbortSession(std::move(cbContext), TASessionClosedEvent<UseMigrationProtocol>(statusCode, std::move(issues)));
}

template<bool UseMigrationProtocol>
void TDeferredActions<UseMigrationProtocol>::DeferAbortSession(TCallbackContextPtr<UseMigrationProtocol> cbContext, EStatus statusCode, const std::string& message) {
    NYdb::NIssue::TIssues issues;
    issues.AddIssue(message);
    DeferAbortSession(std::move(cbContext), statusCode, std::move(issues));
}

template<bool UseMigrationProtocol>
void TDeferredActions<UseMigrationProtocol>::DeferAbortSession(TCallbackContextPtr<UseMigrationProtocol> cbContext, TPlainStatus&& status) {
    DeferAbortSession(std::move(cbContext), TASessionClosedEvent<UseMigrationProtocol>(std::move(status)));
}

template<bool UseMigrationProtocol>
void TDeferredActions<UseMigrationProtocol>::DeferReconnection(TCallbackContextPtr<UseMigrationProtocol> cbContext, TPlainStatus&& status) {
    CbContext = std::move(cbContext);
    ReconnectionStatus = std::move(status);
}

template<bool UseMigrationProtocol>
void TDeferredActions<UseMigrationProtocol>::DeferStartSession(TCallbackContextPtr<UseMigrationProtocol> cbContext) {
    CbContexts.push_back(std::move(cbContext));
}

template<bool UseMigrationProtocol>
void TDeferredActions<UseMigrationProtocol>::DeferSignalWaiter(TWaiter&& waiter) {
    Waiters.emplace_back(std::move(waiter));
}

template<bool UseMigrationProtocol>
void TDeferredActions<UseMigrationProtocol>::DeferDestroyDecompressionInfos(std::vector<TDataDecompressionInfoPtr<UseMigrationProtocol>>&& infos)
{
    DecompressionInfos = std::move(infos);
}

template<bool UseMigrationProtocol>
void TDeferredActions<UseMigrationProtocol>::DoActions() {
    Read();
    DirectRead();
    DirectReadScheduleCallback();
    DirectReadCallback();
    StartExecutorTasks();
    AbortSession();
    Reconnect();
    SignalWaiters();
    StartSessions();
}

template<bool UseMigrationProtocol>
void TDeferredActions<UseMigrationProtocol>::StartSessions() {
    for (auto& ctx : CbContexts) {
        if (auto session = ctx->LockShared()) {
            session->Start();
        }
    }
}

template<bool UseMigrationProtocol>
void TDeferredActions<UseMigrationProtocol>::Read() {
    if (ReadDst) {
        Y_ASSERT(Processor);
        Y_ASSERT(ReadCallback);
        Processor->Read(ReadDst, std::move(ReadCallback));
    }
}

template<bool UseMigrationProtocol>
void TDeferredActions<UseMigrationProtocol>::DirectRead() {
    auto& read = DirectReadActions.Read;
    if (read) {
        Y_ASSERT(read->Processor);
        Y_ASSERT(read->ReadCallback);
        read->Processor->Read(read->ServerMessage, std::move(read->ReadCallback));
    }
}

template<bool UseMigrationProtocol>
void TDeferredActions<UseMigrationProtocol>::DirectReadScheduleCallback() {
    auto& scheduled = DirectReadActions.ScheduledCallback;
    if (scheduled) {
        Y_ASSERT(scheduled->Callback);
        Y_ASSERT(scheduled->ContextPtr);
        if (auto s = scheduled->ContextPtr->LockShared()) {
            s->ScheduleCallback(scheduled->Delay, scheduled->Callback);
        }
    }
}

template<bool UseMigrationProtocol>
void TDeferredActions<UseMigrationProtocol>::DirectReadCallback() {
    auto& callback = DirectReadActions.Callback;
    if (callback) {
        (*callback)();
    }
}

template<bool UseMigrationProtocol>
void TDeferredActions<UseMigrationProtocol>::StartExecutorTasks() {
    for (auto&& [executor, task] : ExecutorsTasks) {
        executor->Post(std::move(task));
    }
}

template<bool UseMigrationProtocol>
void TDeferredActions<UseMigrationProtocol>::AbortSession() {
    if (SessionClosedEvent) {
        Y_ABORT_UNLESS(CbContext);
        if (auto session = CbContext->LockShared()) {
            session->AbortSession(std::move(*SessionClosedEvent));
        }
    }
}

template<bool UseMigrationProtocol>
void TDeferredActions<UseMigrationProtocol>::Reconnect() {
    if (CbContext) {
        if (auto session = CbContext->LockShared()) {
            if (!session->Reconnect(ReconnectionStatus)) {
                session->AbortSession(std::move(ReconnectionStatus));
            }
        }
    }
}

template<bool UseMigrationProtocol>
void TDeferredActions<UseMigrationProtocol>::SignalWaiters() {
    for (auto& w : Waiters) {
        w.Signal();
    }
}

} // namespace NYdb::NTopic
