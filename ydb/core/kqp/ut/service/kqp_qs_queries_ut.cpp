#include <ydb/core/kqp/counters/kqp_counters.h>
#include <ydb/core/kqp/ut/common/kqp_ut_common.h>
#include <ydb/core/kqp/ut/common/columnshard.h>
#include <ydb/core/testlib/common_helper.h>
#include <ydb/core/tx/columnshard/hooks/abstract/abstract.h>
#include <ydb/core/tx/columnshard/hooks/testing/controller.h>
#include <ydb/public/lib/ut_helpers/ut_helpers_query.h>
#include <ydb/core/base/tablet_pipecache.h>
#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/operation/operation.h>
#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/proto/accessor.h>
#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/types/exceptions/exceptions.h>
#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/types/operation/operation.h>

#include <ydb/core/kqp/counters/kqp_counters.h>
#include <ydb/core/base/counters.h>
#include <library/cpp/threading/local_executor/local_executor.h>

#include <fmt/format.h>

namespace NKikimr {
namespace NKqp {

using namespace NYdb;
using namespace NYdb::NQuery;
using namespace fmt::literals;

Y_UNIT_TEST_SUITE(KqpQueryService) {
    Y_UNIT_TEST(SessionFromPoolError) {
        auto kikimr = DefaultKikimrRunner();
        auto settings = NYdb::NQuery::TClientSettings().Database("WrongDB");
        auto db = kikimr.GetQueryClient(settings);

        auto result = db.GetSession().GetValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::NOT_FOUND, result.GetIssues().ToString());
    }

    Y_UNIT_TEST(SessionFromPoolSuccess) {
        auto kikimr = DefaultKikimrRunner();
        NKqp::TKqpCounters counters(kikimr.GetTestServer().GetRuntime()->GetAppData().Counters);

        {
            auto db = kikimr.GetQueryClient();

            TString id;
            {
                auto result = db.GetSession().GetValueSync();
                UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
                UNIT_ASSERT(!result.GetSession().GetId().empty());
                id = result.GetSession().GetId();
            }
            {
                auto result = db.GetSession().GetValueSync();
                UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
                UNIT_ASSERT_VALUES_EQUAL(result.GetSession().GetId(), id);
            }
        }
        WaitForZeroSessions(counters);
    }

    void DoClosedSessionRemovedWhileActiveTest(bool withQuery) {
        auto kikimr = DefaultKikimrRunner();
        auto clientConfig = NGRpcProxy::TGRpcClientConfig(kikimr.GetEndpoint());
        NKqp::TKqpCounters counters(kikimr.GetTestServer().GetRuntime()->GetAppData().Counters);

        {
            auto db = kikimr.GetQueryClient();

            TString id;
            {
                auto result = db.GetSession().GetValueSync();
                UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
                UNIT_ASSERT(!result.GetSession().GetId().empty());
                auto session = result.GetSession();
                id = session.GetId();

                bool allDoneOk = true;
                NTestHelpers::CheckDelete(clientConfig, id, Ydb::StatusIds::SUCCESS, allDoneOk);

                UNIT_ASSERT(allDoneOk);

                if (withQuery) {
                    auto execResult = session.ExecuteQuery("SELECT 1;",
                        NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();

                    UNIT_ASSERT_VALUES_EQUAL(execResult.GetStatus(), EStatus::BAD_SESSION);
                }
            }
            // closed session must be removed from session pool
            {
                auto result = db.GetSession().GetValueSync();
                UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
                UNIT_ASSERT(result.GetSession().GetId() != id);
            }
            UNIT_ASSERT_VALUES_EQUAL(db.GetActiveSessionCount(), 0);
        }
        WaitForZeroSessions(counters);
    }

    Y_UNIT_TEST(ClosedSessionRemovedWhileActiveWithQuery) {
        // - Session is active (user gfot it)
        // - server close it
        // - user executes query (got BAD SESSION)
        // - session should be removed from pool
        DoClosedSessionRemovedWhileActiveTest(true);
    }

/*  Not implemented in the sdk
    Y_UNIT_TEST(ClosedSessionRemovedWhileActiveWithoutQuery) {
        // - Session is active (user gfot it)
        // - server close it
        // - user do not executes any query
        // - session should be removed from pool
        DoClosedSessionRemovedWhileActiveTest(false);
    }
*/
    // Copy paste from table service but with some modifications for query service
    // Checks read iterators/session/sdk counters have expected values
    Y_UNIT_TEST(CloseSessionsWithLoad) {
        auto kikimr = std::make_shared<TKikimrRunner>();
        kikimr->GetTestServer().GetRuntime()->SetLogPriority(NKikimrServices::KQP_EXECUTER, NLog::PRI_DEBUG);
        kikimr->GetTestServer().GetRuntime()->SetLogPriority(NKikimrServices::KQP_SESSION, NLog::PRI_DEBUG);
        kikimr->GetTestServer().GetRuntime()->SetLogPriority(NKikimrServices::KQP_COMPILE_ACTOR, NLog::PRI_DEBUG);
        kikimr->GetTestServer().GetRuntime()->SetLogPriority(NKikimrServices::KQP_COMPILE_SERVICE, NLog::PRI_DEBUG);

        NYdb::NQuery::TQueryClient db = kikimr->GetQueryClient();

        const ui32 SessionsCount = 50;
        const TDuration WaitDuration = TDuration::Seconds(1);

        TVector<NYdb::NQuery::TQueryClient::TSession> sessions;
        for (ui32 i = 0; i < SessionsCount; ++i) {
            auto sessionResult = db.GetSession().GetValueSync();
            UNIT_ASSERT_C(sessionResult.IsSuccess(), sessionResult.GetIssues().ToString());

            sessions.push_back(sessionResult.GetSession());
        }

        NPar::LocalExecutor().RunAdditionalThreads(SessionsCount + 1);
        NPar::LocalExecutor().ExecRange([&kikimr, sessions, WaitDuration](int id) mutable {
            if (id == (i32)sessions.size()) {
                Sleep(WaitDuration);
                Cerr << "start sessions close....." << Endl;
                auto clientConfig = NGRpcProxy::TGRpcClientConfig(kikimr->GetEndpoint());
                for (ui32 i = 0; i < sessions.size(); ++i) {
                    bool allDoneOk = true;
                    NTestHelpers::CheckDelete(clientConfig, TString{sessions[i].GetId()}, Ydb::StatusIds::SUCCESS, allDoneOk);
                    UNIT_ASSERT(allDoneOk);
                }

                Cerr << "finished sessions close....." << Endl;
                auto counters = GetServiceCounters(kikimr->GetTestServer().GetRuntime()->GetAppData(0).Counters,  "ydb");

                ui64 pendingCompilations = 0;
                do {
                    Sleep(WaitDuration);
                    pendingCompilations = counters->GetNamedCounter("name", "table.query.compilation.active_count", false)->Val();
                    Cerr << "still compiling... " << pendingCompilations << Endl;
                } while (pendingCompilations != 0);

                ui64 pendingSessions = 0;
                do {
                    Sleep(WaitDuration);
                    pendingSessions = counters->GetNamedCounter("name", "table.session.active_count", false)->Val();
                    Cerr << "still active sessions ... " << pendingSessions << Endl;
                } while (pendingSessions != 0);

                return;
            }

            auto session = sessions[id];
            std::optional<TTransaction> tx;

            while (true) {
                if (tx) {
                    auto result = tx->Commit().GetValueSync();
                    if (!result.IsSuccess()) {
                        return;
                    }

                    tx = {};
                    continue;
                }

                auto query = Sprintf(R"(
                    SELECT Key, Text, Data FROM `/Root/EightShard` WHERE Key=%1$d + 0;
                    SELECT Key, Data, Text FROM `/Root/EightShard` WHERE Key=%1$d + 1;
                    SELECT Text, Key, Data FROM `/Root/EightShard` WHERE Key=%1$d + 2;
                    SELECT Text, Data, Key FROM `/Root/EightShard` WHERE Key=%1$d + 3;
                    SELECT Data, Key, Text FROM `/Root/EightShard` WHERE Key=%1$d + 4;
                    SELECT Data, Text, Key FROM `/Root/EightShard` WHERE Key=%1$d + 5;

                    UPSERT INTO `/Root/EightShard` (Key, Text) VALUES
                        (%2$dul, "New");
                )", RandomNumber<ui32>(), RandomNumber<ui32>());

                auto result = session.ExecuteQuery(query, TTxControl::BeginTx()).GetValueSync();
                if (!result.IsSuccess()) {
                    UNIT_ASSERT_VALUES_EQUAL(result.GetStatus(), EStatus::BAD_SESSION);
                    Cerr << "received non-success status for session " << id << Endl;
                    return;
                }

                tx = result.GetTransaction();
            }
        }, 0, SessionsCount + 1, NPar::TLocalExecutor::WAIT_COMPLETE | NPar::TLocalExecutor::MED_PRIORITY);
        WaitForZeroReadIterators(kikimr->GetTestServer(), "/Root/EightShard");
    }

    Y_UNIT_TEST(PeriodicTaskInSessionPool) {
        auto kikimr = DefaultKikimrRunner();
        auto clientConfig = NGRpcProxy::TGRpcClientConfig(kikimr.GetEndpoint());
        NKqp::TKqpCounters counters(kikimr.GetTestServer().GetRuntime()->GetAppData().Counters);

        {
            auto db = kikimr.GetQueryClient();

            TString id;
            {
                auto result = db.GetSession().GetValueSync();
                UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
                UNIT_ASSERT(!result.GetSession().GetId().empty());
                auto session = result.GetSession();
                id = session.GetId();

                auto execResult = session.ExecuteQuery("SELECT 1;",
                    NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
                UNIT_ASSERT_VALUES_EQUAL(execResult.GetStatus(), EStatus::SUCCESS);
            }
            // This time is more then internal sdk periodic timeout but less than close session
            // expect nothing happens with session in the pool
            Sleep(TDuration::Seconds(10));

            {
                auto result = db.GetSession().GetValueSync();
                UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
                UNIT_ASSERT(result.GetSession().GetId() == id);

                auto execResult = result.GetSession().ExecuteQuery("SELECT 1;",
                    NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();

                UNIT_ASSERT_VALUES_EQUAL(execResult.GetStatus(), EStatus::SUCCESS);
            }
        }
        WaitForZeroSessions(counters);
    }

    Y_UNIT_TEST(PeriodicTaskInSessionPoolSessionCloseByIdle) {
        auto kikimr = DefaultKikimrRunner();
        auto clientConfig = NGRpcProxy::TGRpcClientConfig(kikimr.GetEndpoint());
        NKqp::TKqpCounters counters(kikimr.GetTestServer().GetRuntime()->GetAppData().Counters);

        {
            auto settings = NYdb::NQuery::TClientSettings().SessionPoolSettings(
                NYdb::NQuery::TSessionPoolSettings()
                    .MinPoolSize(0)
                    .CloseIdleThreshold(TDuration::Seconds(1)));
            auto db = kikimr.GetQueryClient(settings);

            TString id;
            {
                auto result = db.GetSession().GetValueSync();
                UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
                UNIT_ASSERT(!result.GetSession().GetId().empty());
                auto session = result.GetSession();
                id = session.GetId();

                auto execResult = session.ExecuteQuery("SELECT 1;",
                    NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
                UNIT_ASSERT_VALUES_EQUAL(execResult.GetStatus(), EStatus::SUCCESS);
            }

            Sleep(TDuration::Seconds(11));
            UNIT_ASSERT_VALUES_EQUAL(db.GetCurrentPoolSize(), 0);

            {
                auto result = db.GetSession().GetValueSync();
                UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
                UNIT_ASSERT(result.GetSession().GetId() != id);

                auto execResult = result.GetSession().ExecuteQuery("SELECT 1;",
                    NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
                UNIT_ASSERT_VALUES_EQUAL(execResult.GetStatus(), EStatus::SUCCESS);
            }
        }
        WaitForZeroSessions(counters);
    }

    // Check closed session removed while its in the session pool
    Y_UNIT_TEST(ClosedSessionRemovedFromPool) {
        auto kikimr = DefaultKikimrRunner();
        auto clientConfig = NGRpcProxy::TGRpcClientConfig(kikimr.GetEndpoint());
        NKqp::TKqpCounters counters(kikimr.GetTestServer().GetRuntime()->GetAppData().Counters);

        {
            auto db = kikimr.GetQueryClient();

            TString id;
            {
                auto result = db.GetSession().GetValueSync();
                UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
                UNIT_ASSERT(!result.GetSession().GetId().empty());
                auto session = result.GetSession();
                id = session.GetId();
            }

            bool allDoneOk = true;
            NTestHelpers::CheckDelete(clientConfig, id, Ydb::StatusIds::SUCCESS, allDoneOk);

            Sleep(TDuration::Seconds(5));

            UNIT_ASSERT(allDoneOk);
            {
                auto result = db.GetSession().GetValueSync();
                UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
                auto newSession = result.GetSession();
                UNIT_ASSERT_C(newSession.GetId() != id, "closed id: " << id << " new id: " << newSession.GetId());

                auto execResult = newSession.ExecuteQuery("SELECT 1;",
                    NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();

                UNIT_ASSERT_VALUES_EQUAL(execResult.GetStatus(), EStatus::SUCCESS);
            }
        }
        WaitForZeroSessions(counters);
    }

    // Attempt to trigger simultanous server side close and return session
    // From sdk perspective check no dataraces
    Y_UNIT_TEST(ReturnAndCloseSameTime) {
        auto kikimr = DefaultKikimrRunner();
        auto clientConfig = NGRpcProxy::TGRpcClientConfig(kikimr.GetEndpoint());
        NKqp::TKqpCounters counters(kikimr.GetTestServer().GetRuntime()->GetAppData().Counters);

        size_t iterations = 999;
        auto db = kikimr.GetQueryClient();

        NPar::LocalExecutor().RunAdditionalThreads(2);
        while (iterations--) {
            auto lim = iterations % 33;
            TVector<NYdb::NQuery::TQueryClient::TSession> sessions;
            TVector<TString> sids;
            sessions.reserve(lim);
            sids.reserve(lim);
            for (size_t i = 0; i < lim; ++i) {
                auto sessionResult = db.GetSession().GetValueSync();
                UNIT_ASSERT_C(sessionResult.IsSuccess(), sessionResult.GetIssues().ToString());

                sessions.push_back(sessionResult.GetSession());
                sids.push_back(TString{sessions.back().GetId()});
            }

            if (iterations & 1) {
                auto rng = std::default_random_engine {};
                std::ranges::shuffle(sids, rng);
            }

            NPar::LocalExecutor().ExecRange([sessions{std::move(sessions)}, sids{std::move(sids)}, clientConfig](int id) mutable{
                if (id == 0) {
                    for (size_t i = 0; i < sessions.size(); i++) {
                        auto s = std::move(sessions[i]);
                        auto execResult = s.ExecuteQuery("SELECT 1;",
                            NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
                        switch (execResult.GetStatus()) {
                            case EStatus::SUCCESS:
                            case EStatus::BAD_SESSION:
                                break;
                            default:
                                UNIT_ASSERT_C(false, "unexpected status: " << execResult.GetStatus());
                        }
                    }
                } else if (id == 1) {
                    for (size_t i = 0; i < sids.size(); i++) {
                        bool allDoneOk = true;
                        NTestHelpers::CheckDelete(clientConfig, sids[i], Ydb::StatusIds::SUCCESS, allDoneOk);
                        UNIT_ASSERT(allDoneOk);
                    }
                } else {
                    Y_ABORT_UNLESS(false, "unexpected thread cxount");
                }
            }, 0, 2, NPar::TLocalExecutor::WAIT_COMPLETE | NPar::TLocalExecutor::MED_PRIORITY);
        }

        WaitForZeroSessions(counters);
    }

    Y_UNIT_TEST(StreamExecuteQueryPure) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();

        auto params = TParamsBuilder()
            .AddParam("$value").Int64(17).Build()
            .Build();

        auto it = db.StreamExecuteQuery(R"(
            DECLARE $value As Int64;
            SELECT $value;
        )", TTxControl::BeginTx().CommitTx(), params).ExtractValueSync();
        UNIT_ASSERT_C(it.IsSuccess(), it.GetIssues().ToString());

        ui64 count = 0;
        for (;;) {
            auto streamPart = it.ReadNext().GetValueSync();
            if (!streamPart.IsSuccess()) {
                UNIT_ASSERT_C(streamPart.EOS(), streamPart.GetIssues().ToString());
                break;
            }

            if (streamPart.HasResultSet()) {
                auto resultSet = streamPart.ExtractResultSet();
                count += resultSet.RowsCount();
            }
        }

        UNIT_ASSERT_VALUES_EQUAL(count, 1);
    }

    Y_UNIT_TEST(ExecuteQueryUpsertDoesntChangeIndexedValuesIfNotChanged) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();

        auto settings = TExecuteQuerySettings()
            .StatsMode(EStatsMode::Full);

        {
            auto result = db.ExecuteQuery(R"(
                create table test (
                    id1 Uint64,
                    id2 Uint64,
                    value Utf8,
                    PRIMARY KEY(id1, id2),
                    INDEX Id2Idx GLOBAL ON (id2),
                    INDEX ValueIdx GLOBAL ON (value)
                );

            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        }

        {
            auto result = db.ExecuteQuery(R"(
                UPSERT INTO test (id1, id2, value) VALUES (2, 2, "c"u), (3, 3, null), (4, 4, null);
            )", TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            auto stats = NYdb::TProtoAccessor::GetProto(*result.GetStats());
            AssertTableStats(stats, "/Root/test", {.ExpectedUpdates = 3});
            AssertTableStats(stats, "/Root/test/Id2Idx/indexImplTable", {.ExpectedUpdates = 3});
            AssertTableStats(stats, "/Root/test/ValueIdx/indexImplTable", {.ExpectedUpdates = 3});
        }

        // value not changed, no updates to indexes
        {
            auto result = db.ExecuteQuery(R"(
                UPSERT INTO test (id1, id2) VALUES (2, 2);
            )", TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            auto stats = NYdb::TProtoAccessor::GetProto(*result.GetStats());
            AssertTableStats(stats, "/Root/test", {.ExpectedUpdates = 1});
            AssertTableStats(stats, "/Root/test/Id2Idx/indexImplTable", {.ExpectedUpdates = 0});
            AssertTableStats(stats, "/Root/test/ValueIdx/indexImplTable", {.ExpectedUpdates = 0});
        }

        // value not changed, no updates to indexes
        {
            auto result = db.ExecuteQuery(R"(
                UPSERT INTO test (id1, id2, value) VALUES (2, 2, "c"u);
            )", TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            auto stats = NYdb::TProtoAccessor::GetProto(*result.GetStats());
            AssertTableStats(stats, "/Root/test", {.ExpectedUpdates = 1});
            AssertTableStats(stats, "/Root/test/Id2Idx/indexImplTable", {.ExpectedUpdates = 0});
            AssertTableStats(stats, "/Root/test/ValueIdx/indexImplTable", {.ExpectedUpdates = 0});
        }

        // value IS changed, updates to index by VALUE
        {
            auto result = db.ExecuteQuery(R"(
                UPSERT INTO test (id1, id2, value) VALUES (2, 2, "Q"u);
            )", TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            auto stats = NYdb::TProtoAccessor::GetProto(*result.GetStats());
            AssertTableStats(stats, "/Root/test", {.ExpectedUpdates = 1});
            AssertTableStats(stats, "/Root/test/Id2Idx/indexImplTable", {.ExpectedUpdates = 0});
            AssertTableStats(stats, "/Root/test/ValueIdx/indexImplTable", {.ExpectedUpdates = 1});
            const TString query = "SELECT id1, id2, value FROM test VIEW ValueIdx WHERE value = \"Q\"u";
            auto selectResult = db.ExecuteQuery(query, TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
            CompareYson(R"([[[2u];[2u];["Q"]]])", FormatResultSetYson(selectResult.GetResultSet(0)));
        }

        // value IS NOT changed
        {
            auto result = db.ExecuteQuery(R"(
                UPDATE test ON SELECT 2 as id1, 2 as id2, "Q"u as value;
            )", TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            auto stats = NYdb::TProtoAccessor::GetProto(*result.GetStats());
            AssertTableStats(stats, "/Root/test", {.ExpectedUpdates = 1});
            AssertTableStats(stats, "/Root/test/Id2Idx/indexImplTable", {.ExpectedUpdates = 0});
            AssertTableStats(stats, "/Root/test/ValueIdx/indexImplTable", {.ExpectedUpdates = 0});
            const TString query = "SELECT id1, id2, value FROM test VIEW ValueIdx WHERE value = \"Q\"u";
            auto selectResult = db.ExecuteQuery(query, TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
            CompareYson(R"([[[2u];[2u];["Q"]]])", FormatResultSetYson(selectResult.GetResultSet(0)));
        }


        // value IS NOT changed
        {
            auto result = db.ExecuteQuery(R"(
                UPDATE test ON SELECT 2 as id1, 2 as id2, "R"u as value;
            )", TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            auto stats = NYdb::TProtoAccessor::GetProto(*result.GetStats());
            AssertTableStats(stats, "/Root/test", {.ExpectedUpdates = 1});
            AssertTableStats(stats, "/Root/test/Id2Idx/indexImplTable", {.ExpectedUpdates = 0});
            AssertTableStats(stats, "/Root/test/ValueIdx/indexImplTable", {.ExpectedUpdates = 1});
            const TString query = "SELECT id1, id2, value FROM test VIEW ValueIdx WHERE value = \"R\"u";
            auto selectResult = db.ExecuteQuery(query, TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
            CompareYson(R"([[[2u];[2u];["R"]]])", FormatResultSetYson(selectResult.GetResultSet(0)));
        }

        // value (null) IS NOT changed
        {
            auto result = db.ExecuteQuery(R"(
                UPSERT INTO test (id1, id2, value) VALUES (3, 3, null);
            )", TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            auto stats = NYdb::TProtoAccessor::GetProto(*result.GetStats());
            AssertTableStats(stats, "/Root/test", {.ExpectedUpdates = 1});
            AssertTableStats(stats, "/Root/test/Id2Idx/indexImplTable", {.ExpectedUpdates = 0});
            AssertTableStats(stats, "/Root/test/ValueIdx/indexImplTable", {.ExpectedUpdates = 0});
            const TString query = "SELECT id1, id2, value FROM test VIEW ValueIdx WHERE value is null";
            auto selectResult = db.ExecuteQuery(query, TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
            CompareYson(R"([[[3u];[3u];#];[[4u];[4u];#]])", FormatResultSetYson(selectResult.GetResultSet(0)));
        }

        // value (null) IS NOT changed
        {
            auto result = db.ExecuteQuery(R"(
                UPDATE test ON SELECT 3 as id1, 3 as id2, null as value;
            )", TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            auto stats = NYdb::TProtoAccessor::GetProto(*result.GetStats());
            AssertTableStats(stats, "/Root/test", {.ExpectedUpdates = 1});
            AssertTableStats(stats, "/Root/test/Id2Idx/indexImplTable", {.ExpectedUpdates = 0});
            AssertTableStats(stats, "/Root/test/ValueIdx/indexImplTable", {.ExpectedUpdates = 0});
            const TString query = "SELECT id1, id2, value FROM test VIEW ValueIdx WHERE value is null";
            auto selectResult = db.ExecuteQuery(query, TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
            CompareYson(R"([[[3u];[3u];#];[[4u];[4u];#]])", FormatResultSetYson(selectResult.GetResultSet(0)));
        }

        // value (null) IS changed
        {
            auto result = db.ExecuteQuery(R"(
                UPSERT INTO test (id1, id2, value) VALUES (3, 3, "T"u);
            )", TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            auto stats = NYdb::TProtoAccessor::GetProto(*result.GetStats());
            AssertTableStats(stats, "/Root/test", {.ExpectedUpdates = 1});
            AssertTableStats(stats, "/Root/test/Id2Idx/indexImplTable", {.ExpectedUpdates = 0});
            AssertTableStats(stats, "/Root/test/ValueIdx/indexImplTable", {.ExpectedUpdates = 1});
            const TString query = "SELECT id1, id2, value FROM test VIEW ValueIdx WHERE value = \"T\"u";
            auto selectResult = db.ExecuteQuery(query, TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
            CompareYson(R"([[[3u];[3u];["T"]]])", FormatResultSetYson(selectResult.GetResultSet(0)));
        }

        // value (null) IS changed
        {
            auto result = db.ExecuteQuery(R"(
                UPDATE test ON SELECT 4 as id1, 4 as id2, "L"u as value;
            )", TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            auto stats = NYdb::TProtoAccessor::GetProto(*result.GetStats());
            AssertTableStats(stats, "/Root/test", {.ExpectedUpdates = 1});
            AssertTableStats(stats, "/Root/test/Id2Idx/indexImplTable", {.ExpectedUpdates = 0});
            AssertTableStats(stats, "/Root/test/ValueIdx/indexImplTable", {.ExpectedUpdates = 1});
            const TString query = "SELECT id1, id2, value FROM test VIEW ValueIdx WHERE value = \"L\"u";
            auto selectResult = db.ExecuteQuery(query, TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
            CompareYson(R"([[[4u];[4u];["L"]]])", FormatResultSetYson(selectResult.GetResultSet(0)));
        }
    }

    Y_UNIT_TEST(ExecuteQueryPure) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();

        auto params = TParamsBuilder()
            .AddParam("$value").Int64(17).Build()
            .Build();

        auto result = db.ExecuteQuery(R"(
            DECLARE $value As Int64;
            SELECT $value;
        )", TTxControl::BeginTx().CommitTx(), params).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

        CompareYson(R"([[17]])", FormatResultSetYson(result.GetResultSet(0)));
    }

    Y_UNIT_TEST(StreamExecuteQuery) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();

        {
            auto it = db.StreamExecuteQuery(R"(
                SELECT Key, Value2 FROM TwoShard WHERE Value2 > 0;
            )", TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(it.IsSuccess(), it.GetIssues().ToString());

            ui64 count = 0;
            for (;;) {
                auto streamPart = it.ReadNext().GetValueSync();
                if (!streamPart.IsSuccess()) {
                    UNIT_ASSERT_C(streamPart.EOS(), streamPart.GetIssues().ToString());
                    break;
                }

                if (streamPart.HasResultSet()) {
                    auto resultSet = streamPart.ExtractResultSet();
                    count += resultSet.RowsCount();
                }
            }

            UNIT_ASSERT_VALUES_EQUAL(count, 2);
        }

        {
            auto it = db.StreamExecuteQuery(R"(
                SELECT Key, Value2 FROM TwoShard WHERE false ORDER BY Key > 0;
            )", TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(it.IsSuccess(), it.GetIssues().ToString());

            ui32 rsCount = 0;
            ui32 columns = 0;
            for (;;) {
                auto streamPart = it.ReadNext().GetValueSync();
                if (!streamPart.IsSuccess()) {
                    UNIT_ASSERT_C(streamPart.EOS(), streamPart.GetIssues().ToString());
                    break;
                }

                if (streamPart.HasResultSet()) {
                    auto resultSet = streamPart.ExtractResultSet();
                    columns = resultSet.ColumnsCount();
                    CompareYson(R"([])", FormatResultSetYson(resultSet));
                    rsCount++;
                }
            }

            UNIT_ASSERT_VALUES_EQUAL(rsCount, 1);
            UNIT_ASSERT_VALUES_EQUAL(columns, 2);
        }
    }

    Y_UNIT_TEST(ExecuteCollectMeta) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();

        {
            TExecuteQuerySettings settings;
            settings.StatsMode(EStatsMode::Full);

            auto result = db.ExecuteQuery(R"(
                SELECT Key, Value2 FROM TwoShard WHERE Value2 > 0;
            )", TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();

            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            auto& stats = NYdb::TProtoAccessor::GetProto(*result.GetStats());
            UNIT_ASSERT_C(!stats.query_meta().empty(), "Query result meta is empty");

            TStringStream in;
            in << stats.query_meta();
            NJson::TJsonValue value;
            ReadJsonTree(&in, &value);

            UNIT_ASSERT_C(value.IsMap(), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("query_id"), "Incorrect Meta");
            UNIT_ASSERT_C(!value.Has("query_text"), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("version"), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("query_parameter_types"), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("table_metadata"), "Incorrect Meta");
            UNIT_ASSERT_C(value["table_metadata"].IsArray(), "Incorrect Meta: table_metadata type should be an array");
            UNIT_ASSERT_C(value.Has("created_at"), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("query_syntax"), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("query_database"), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("query_cluster"), "Incorrect Meta");
            UNIT_ASSERT_C(!value.Has("query_plan"), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("query_type"), "Incorrect Meta");
        }

        {
            TExecuteQuerySettings settings;
            settings.StatsMode(EStatsMode::Basic);

            auto result = db.ExecuteQuery(R"(
                SELECT Key, Value2 FROM TwoShard WHERE Value2 > 0;
            )", TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();

            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString().c_str());

            auto& stats = NYdb::TProtoAccessor::GetProto(*result.GetStats());
            UNIT_ASSERT_C(stats.query_meta().empty(), "Query result Meta should be empty, but it's not");
        }

        {
            TExecuteQuerySettings settings;
            settings.ExecMode(EExecMode::Explain);

            auto result = db.ExecuteQuery(R"(
                SELECT Key, Value2 FROM TwoShard WHERE Value2 > 0;
            )", TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            UNIT_ASSERT(result.GetResultSets().empty());

            UNIT_ASSERT(result.GetStats().has_value());

            auto& stats = NYdb::TProtoAccessor::GetProto(*result.GetStats());
            UNIT_ASSERT_C(!stats.query_ast().empty(), "Query result ast is empty");
            UNIT_ASSERT_C(!stats.query_meta().empty(), "Query result meta is empty");

            TStringStream in;
            in << stats.query_meta();
            NJson::TJsonValue value;
            ReadJsonTree(&in, &value);

            UNIT_ASSERT_C(value.IsMap(), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("query_id"), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("version"), "Incorrect Meta");
            UNIT_ASSERT_C(!value.Has("query_text"), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("query_parameter_types"), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("table_metadata"), "Incorrect Meta");
            UNIT_ASSERT_C(value["table_metadata"].IsArray(), "Incorrect Meta: table_metadata type should be an array");
            UNIT_ASSERT_C(value.Has("created_at"), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("query_syntax"), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("query_database"), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("query_cluster"), "Incorrect Meta");
            UNIT_ASSERT_C(!value.Has("query_plan"), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("query_type"), "Incorrect Meta");
        }
    }

    Y_UNIT_TEST(StreamExecuteCollectMeta) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();

        {
            TExecuteQuerySettings settings;
            settings.StatsMode(EStatsMode::Full);

            auto it = db.StreamExecuteQuery(R"(
                SELECT Key, Value2 FROM TwoShard WHERE Value2 > 0;
            )", TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());

            TString statsString;
            for (;;) {
                auto streamPart = it.ReadNext().GetValueSync();
                if (!streamPart.IsSuccess()) {
                    UNIT_ASSERT_C(streamPart.EOS(), streamPart.GetIssues().ToString());
                    break;
                }

                const auto& execStats = streamPart.GetStats();
                if (execStats) {
                    auto& stats = NYdb::TProtoAccessor::GetProto(*execStats);
                    statsString = stats.query_meta();
                }
            }

            UNIT_ASSERT_C(!statsString.empty(), "Query result meta is empty");

            TStringStream in;
            in << statsString;
            NJson::TJsonValue value;
            ReadJsonTree(&in, &value);

            UNIT_ASSERT_C(value.IsMap(), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("query_id"), "Incorrect Meta");
            UNIT_ASSERT_C(!value.Has("query_text"), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("version"), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("query_parameter_types"), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("table_metadata"), "Incorrect Meta");
            UNIT_ASSERT_C(value["table_metadata"].IsArray(), "Incorrect Meta: table_metadata type should be an array");
            UNIT_ASSERT_C(value.Has("created_at"), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("query_syntax"), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("query_database"), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("query_cluster"), "Incorrect Meta");
            UNIT_ASSERT_C(!value.Has("query_plan"), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("query_type"), "Incorrect Meta");
        }

        {
            auto settings = TExecuteQuerySettings().ExecMode(EExecMode::Explain);

            auto it = db.StreamExecuteQuery(R"(
                SELECT Key, Value2 FROM TwoShard WHERE Value2 > 0;
            )", TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());

            TString statsString;
            for (;;) {
                auto streamPart = it.ReadNext().GetValueSync();
                if (!streamPart.IsSuccess()) {
                    UNIT_ASSERT_C(streamPart.EOS(), streamPart.GetIssues().ToString());
                    break;
                }

                const auto& execStats = streamPart.GetStats();
                if (execStats) {
                    auto& stats = NYdb::TProtoAccessor::GetProto(*execStats);
                    statsString = stats.query_meta();
                }
            }

            UNIT_ASSERT_C(!statsString.empty(), "Query result meta is empty");

            TStringStream in;
            in << statsString;
            NJson::TJsonValue value;
            ReadJsonTree(&in, &value);

            UNIT_ASSERT_C(value.IsMap(), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("query_id"), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("version"), "Incorrect Meta");
            UNIT_ASSERT_C(!value.Has("query_text"), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("query_parameter_types"), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("table_metadata"), "Incorrect Meta");
            UNIT_ASSERT_C(value["table_metadata"].IsArray(), "Incorrect Meta: table_metadata type should be an array");
            UNIT_ASSERT_C(value.Has("created_at"), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("query_syntax"), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("query_database"), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("query_cluster"), "Incorrect Meta");
            UNIT_ASSERT_C(!value.Has("query_plan"), "Incorrect Meta");
            UNIT_ASSERT_C(value.Has("query_type"), "Incorrect Meta");
        }

        {
            TExecuteQuerySettings settings;
            settings.StatsMode(EStatsMode::Basic);

            auto it = db.StreamExecuteQuery(R"(
                SELECT Key, Value2 FROM TwoShard WHERE Value2 > 0;
            )", TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());

            TString statsString;
            for (;;) {
                auto streamPart = it.ReadNext().GetValueSync();
                if (!streamPart.IsSuccess()) {
                    UNIT_ASSERT_C(streamPart.EOS(), streamPart.GetIssues().ToString());
                    break;
                }

                const auto& execStats = streamPart.GetStats();
                if (execStats) {
                    auto& stats = NYdb::TProtoAccessor::GetProto(*execStats);
                    statsString = stats.query_meta();
                }
            }

            UNIT_ASSERT_C(statsString.empty(), "Query result meta should be empty, but it's not");
        }
    }

    void CheckQueryResult(TExecuteQueryResult result) {
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 1);
        CompareYson(R"([
            [[3u];[1]];
            [[4000000003u];[1]]
        ])", FormatResultSetYson(result.GetResultSet(0)));
    }

    Y_UNIT_TEST(ExecuteQuery) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();

        const TString query = "SELECT Key, Value2 FROM TwoShard WHERE Value2 > 0 ORDER BY Key";
        auto result = db.ExecuteQuery(query, TTxControl::BeginTx().CommitTx()).ExtractValueSync();
        CheckQueryResult(result);
    }

    Y_UNIT_TEST(ExecuteQueryExplicitBeginCommitRollback) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();
        auto sessionResult = db.GetSession().ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(sessionResult.GetStatus(), EStatus::SUCCESS, sessionResult.GetIssues().ToString());
        auto session = sessionResult.GetSession();

        {
            auto beginTxResult = session.BeginTransaction(TTxSettings::OnlineRO()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(beginTxResult.GetStatus(), EStatus::BAD_REQUEST, beginTxResult.GetIssues().ToString());
            UNIT_ASSERT(beginTxResult.GetIssues());
        }

        {
            auto beginTxResult = session.BeginTransaction(TTxSettings::SerializableRW()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(beginTxResult.GetStatus(), EStatus::SUCCESS, beginTxResult.GetIssues().ToString());

            auto transaction = beginTxResult.GetTransaction();
            auto commitTxResult = transaction.Commit().ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(commitTxResult.GetStatus(), EStatus::SUCCESS, commitTxResult.GetIssues().ToString());

            auto rollbackTxResult = transaction.Rollback().ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(rollbackTxResult.GetStatus(), EStatus::NOT_FOUND, rollbackTxResult.GetIssues().ToString());
        }
    }

    Y_UNIT_TEST(Followers) {
        NKikimrConfig::TAppConfig config;

        auto kikimr = TKikimrRunner(TKikimrSettings().SetAppConfig(config).SetUseRealThreads(false));
        auto db = kikimr.GetQueryClient();

        TExecuteQuerySettings settings;

        THashMap<TActorId, ui64> countResolveTablet;
        countResolveTablet[NKikimr::MakePipePerNodeCacheID(false)] = 0;
        countResolveTablet[NKikimr::MakePipePerNodeCacheID(true)] = 0;
        auto counterObserver = [&countResolveTablet](TAutoPtr<IEventHandle>& ev) -> auto {
            if (ev->GetTypeRewrite() == NKikimr::TEvPipeCache::TEvGetTabletNode::EventType) {
                countResolveTablet[ev->Recipient]++;
                return TTestActorRuntime::EEventAction::PROCESS;
            }

            return TTestActorRuntime::EEventAction::PROCESS;
        };

        kikimr.GetTestServer().GetRuntime()->SetObserverFunc(counterObserver);

        Y_DEFER {
            kikimr.GetTestServer().GetRuntime()->SetObserverFunc(TTestActorRuntime::DefaultObserverFunc);
        };

        {
            const TString query = "SELECT Key, Value2 FROM TwoShard WHERE Key = 1u";
            auto result = kikimr.RunCall([&] { return db.ExecuteQuery(query, NQuery::TTxControl::BeginTx(TTxSettings::StaleRO()).CommitTx()).ExtractValueSync(); });
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 1);
            CompareYson(R"([
                [[1u];[-1]]
            ])", FormatResultSetYson(result.GetResultSet(0)));
            UNIT_ASSERT(countResolveTablet[NKikimr::MakePipePerNodeCacheID(false)] == 0);

            // using followers resolver.
            UNIT_ASSERT(countResolveTablet[NKikimr::MakePipePerNodeCacheID(true)] > 0);
        }
    }

    Y_UNIT_TEST(ExecuteQueryWithWorkloadManager) {
        NKikimrConfig::TAppConfig config;
        config.MutableFeatureFlags()->SetEnableResourcePools(true);

        auto kikimr = TKikimrRunner(TKikimrSettings()
            .SetAppConfig(config)
            .SetEnableResourcePools(true));
        auto db = kikimr.GetQueryClient();

        TExecuteQuerySettings settings;

        {  // Existing pool
            settings.ResourcePool("default");

            const TString query = "SELECT Key, Value2 FROM TwoShard WHERE Value2 > 0 ORDER BY Key";
            auto result = db.ExecuteQuery(query, TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
            CheckQueryResult(result);
        }

        {  // Not existing pool (check workload manager enabled)
            settings.ResourcePool("another_pool_id");

            const TString query = "SELECT Key, Value2 FROM TwoShard WHERE Value2 > 0 ORDER BY Key";
            auto result = db.ExecuteQuery(query, TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::NOT_FOUND, result.GetIssues().ToOneLineString());
            UNIT_ASSERT_STRING_CONTAINS(result.GetIssues().ToString(), "Resource pool another_pool_id not found");
            UNIT_ASSERT_STRING_CONTAINS(result.GetIssues().ToString(), "Failed to resolve pool id another_pool");
            UNIT_ASSERT_STRING_CONTAINS(result.GetIssues().ToString(), "Query failed during adding/waiting in workload pool");
        }
    }

    Y_UNIT_TEST(ExecuteQueryWithResourcePoolClassifier) {
        NKikimrConfig::TAppConfig config;
        config.MutableFeatureFlags()->SetEnableResourcePools(true);

        auto kikimr = TKikimrRunner(TKikimrSettings()
            .SetAppConfig(config)
            .SetEnableResourcePools(true));
        auto db = kikimr.GetQueryClient();

        const TString userSID = TStringBuilder() << "test@" << BUILTIN_ACL_DOMAIN;
        const TString schemeSql = TStringBuilder() << R"(
            CREATE RESOURCE POOL MyPool WITH (
                CONCURRENT_QUERY_LIMIT=0
            );
            CREATE RESOURCE POOL CLASSIFIER MyPoolClassifier WITH (
                RESOURCE_POOL="MyPool",
                MEMBER_NAME=")" << userSID << R"("
            );
            GRANT ALL ON `/Root` TO `)" << userSID << R"(`;
        )";
        auto schemeResult = db.ExecuteQuery(schemeSql, TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(schemeResult.GetStatus(), EStatus::SUCCESS, schemeResult.GetIssues().ToString());

        auto testUserClient = kikimr.GetQueryClient(TClientSettings().AuthToken(userSID));
        const TDuration timeout = TDuration::Seconds(5);
        const TInstant start = TInstant::Now();
        while (TInstant::Now() - start <= timeout) {
            const TString query = "SELECT 42;";
            auto result = testUserClient.ExecuteQuery(query, TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            if (!result.IsSuccess()) {
                UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::PRECONDITION_FAILED, result.GetIssues().ToString());
                UNIT_ASSERT_STRING_CONTAINS(result.GetIssues().ToString(), "Resource pool MyPool was disabled due to zero concurrent query limit");
                return;
            }

            Cerr << "Wait resource pool classifier " << TInstant::Now() - start << ": status = " << result.GetStatus() << ", issues = " << result.GetIssues().ToOneLineString() << "\n";
            Sleep(TDuration::Seconds(1));
        }
        UNIT_ASSERT_C(false, "Waiting resource pool classifier timeout. Spent time " << TInstant::Now() - start << " exceeds limit " << timeout);
    }

    std::pair<ui32, ui32> CalcRowsAndBatches(TExecuteQueryIterator& it) {
        ui32 totalRows = 0;
        ui32 totalBatches = 0;
        for (;;) {
            auto streamPart = it.ReadNext().GetValueSync();
            if (!streamPart.IsSuccess()) {
                UNIT_ASSERT_C(streamPart.EOS(), streamPart.GetIssues().ToString());
                break;
            }

            if (streamPart.HasResultSet()) {
                auto result = streamPart.ExtractResultSet();
                UNIT_ASSERT(!result.Truncated());
                totalRows += result.RowsCount();
                totalBatches++;
            }
        }
        return {totalRows, totalBatches};
    }

    Y_UNIT_TEST(FlowControllOnHugeLiteralAsTable) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();

        const TString query = "SELECT * FROM AS_TABLE(ListReplicate(AsStruct(\"12345678\" AS Key), 100000))";

        {
            // Check range for chunk size settings
            auto settings = TExecuteQuerySettings().OutputChunkMaxSize(48_MB);
            auto it = db.StreamExecuteQuery(query, TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
            auto streamPart = it.ReadNext().GetValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(streamPart.GetStatus(), EStatus::BAD_REQUEST, streamPart.GetIssues().ToString());
        }

        auto settings = TExecuteQuerySettings().OutputChunkMaxSize(10000);
        auto it = db.StreamExecuteQuery(query, TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());

        auto [totalRows, totalBatches] = CalcRowsAndBatches(it);

        UNIT_ASSERT_VALUES_EQUAL(totalRows, 100000);
        // 100000 rows * 9 (?) byte per row / 10000 chunk size limit -> expect 90 batches
        UNIT_ASSERT(totalBatches >= 90); // but got 91 in our case
        UNIT_ASSERT(totalBatches < 100);
    }

    TString GetQueryToFillTable(bool longRow) {
        TString s = "12345678";
        int rows = 100000;
        if (longRow) {
            rows /= 1000;
            s.resize(1000, 'x');
        }
        return Sprintf("UPSERT INTO test SELECT * FROM AS_TABLE (ListMap(ListEnumerate(ListReplicate(\"%s\", %d)), "
                       "($x) -> {RETURN AsStruct($x.0 AS Key, $x.1 as Value)}))",
                       s.c_str(), rows);
    }

    void DoFlowControllOnHugeRealTable(bool longRow) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();

        {
            const TString q = "CREATE TABLE test (Key Uint64, Value String, PRIMARY KEY (Key))";
            auto r = db.ExecuteQuery(q, TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(r.GetStatus(), EStatus::SUCCESS, r.GetIssues().ToString());
        }

        {
            auto q = GetQueryToFillTable(longRow);
            auto r = db.ExecuteQuery(q, TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(r.GetStatus(), EStatus::SUCCESS, r.GetIssues().ToString());
        }

        const TString query = "SELECT * FROM test";
        if (longRow) {
            // Check the case of limit less than one row size - expect one batch for each row
            auto settings = TExecuteQuerySettings().OutputChunkMaxSize(100);
            auto it = db.StreamExecuteQuery(query, TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());

            auto [totalRows, totalBatches] = CalcRowsAndBatches(it);

            UNIT_ASSERT_VALUES_EQUAL(totalRows, 100);
            UNIT_ASSERT_VALUES_EQUAL(totalBatches, 100);
        }

        auto settings = TExecuteQuerySettings().OutputChunkMaxSize(10000);
        auto it = db.StreamExecuteQuery(query, TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());

        auto [totalRows, totalBatches] = CalcRowsAndBatches(it);

        Cerr << totalBatches << Endl;
        if (longRow) {
            UNIT_ASSERT_VALUES_EQUAL(totalRows, 100);
            // 100 rows * 1000 byte per row / 10000 chunk size limit -> expect 10 batches
            UNIT_ASSERT(9 <= totalBatches);
            UNIT_ASSERT_LT_C(totalBatches, 13, totalBatches);
        } else {
            UNIT_ASSERT_VALUES_EQUAL(totalRows, 100000);
            // 100000 rows * 12 byte per row / 10000 chunk size limit -> expect 120 batches
            UNIT_ASSERT(119 <= totalBatches);
            UNIT_ASSERT_LT_C(totalBatches, 123, totalBatches);
        }
    }

    Y_UNIT_TEST_TWIN(FlowControllOnHugeRealTable, LongRow) {
        DoFlowControllOnHugeRealTable(LongRow);
    }

    Y_UNIT_TEST(ExecuteQueryExplicitTxTLI) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();
        auto sessionResult = db.GetSession().ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(sessionResult.GetStatus(), EStatus::SUCCESS, sessionResult.GetIssues().ToString());
        auto session = sessionResult.GetSession();

        auto beginTxResult = session.BeginTransaction(TTxSettings::SerializableRW()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(beginTxResult.GetStatus(), EStatus::SUCCESS, beginTxResult.GetIssues().ToString());
        auto transaction = beginTxResult.GetTransaction();
        UNIT_ASSERT(transaction.IsActive());

        {
            const TString query = "UPDATE TwoShard SET Value2 = 0";
            auto result = transaction.GetSession().ExecuteQuery(query, TTxControl::Tx(transaction)).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        }

        {

            const TString query = "UPDATE TwoShard SET Value2 = 1";
            auto result = db.ExecuteQuery(query, TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        }

        auto commitTxResult = transaction.Commit().ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(commitTxResult.GetStatus(), EStatus::ABORTED, commitTxResult.GetIssues().ToString());
    }

    Y_UNIT_TEST(ExecuteQueryInteractiveTx) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();
        auto sessionResult = db.GetSession().ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(sessionResult.GetStatus(), EStatus::SUCCESS, sessionResult.GetIssues().ToString());
        auto session = sessionResult.GetSession();

        {
            const TString query = "UPDATE TwoShard SET Value2 = 0";
            auto result = session.ExecuteQuery(query, TTxControl::BeginTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            auto transaction = result.GetTransaction();
            UNIT_ASSERT(transaction->IsActive());

            auto checkResult = [&](TString expected) {
                auto selectRes = db.ExecuteQuery(
                    "SELECT * FROM TwoShard ORDER BY Key",
                    TTxControl::BeginTx().CommitTx()
                ).ExtractValueSync();

                UNIT_ASSERT_C(selectRes.IsSuccess(), selectRes.GetIssues().ToString());
                CompareYson(expected, FormatResultSetYson(selectRes.GetResultSet(0)));
            };
            checkResult(R"([[[1u];["One"];[-1]];[[2u];["Two"];[0]];[[3u];["Three"];[1]];[[4000000001u];["BigOne"];[-1]];[[4000000002u];["BigTwo"];[0]];[[4000000003u];["BigThree"];[1]]])");

            auto txRes = transaction->Commit().GetValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(txRes.GetStatus(), EStatus::SUCCESS, txRes.GetIssues().ToString());

            checkResult(R"([[[1u];["One"];[0]];[[2u];["Two"];[0]];[[3u];["Three"];[0]];[[4000000001u];["BigOne"];[0]];[[4000000002u];["BigTwo"];[0]];[[4000000003u];["BigThree"];[0]]])");
        }

        {
            const TString query = "UPDATE TwoShard SET Value2 = 1";
            auto result = session.ExecuteQuery(query, TTxControl::BeginTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            auto transaction = result.GetTransaction();
            UNIT_ASSERT(transaction->IsActive());

            const TString query2 = "UPDATE KeyValue SET Value = 'Vic'";
            auto result2 = session.ExecuteQuery(query2, TTxControl::Tx(*transaction)).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result2.GetStatus(), EStatus::SUCCESS, result2.GetIssues().ToString());
            auto transaction2 = result2.GetTransaction();
            UNIT_ASSERT(transaction2->IsActive());

            auto checkResult = [&](TString table, TString expected) {
                auto selectRes = db.ExecuteQuery(
                    Sprintf("SELECT * FROM %s ORDER BY Key", table.data()),
                    TTxControl::BeginTx().CommitTx()
                ).ExtractValueSync();

                UNIT_ASSERT_C(selectRes.IsSuccess(), selectRes.GetIssues().ToString());
                CompareYson(expected, FormatResultSetYson(selectRes.GetResultSet(0)));
            };
            checkResult("TwoShard", R"([[[1u];["One"];[0]];[[2u];["Two"];[0]];[[3u];["Three"];[0]];[[4000000001u];["BigOne"];[0]];[[4000000002u];["BigTwo"];[0]];[[4000000003u];["BigThree"];[0]]])");
            checkResult("KeyValue", R"([[[1u];["One"]];[[2u];["Two"]]])");
            auto txRes = transaction->Commit().GetValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(txRes.GetStatus(), EStatus::SUCCESS, txRes.GetIssues().ToString());

            checkResult("KeyValue", R"([[[1u];["Vic"]];[[2u];["Vic"]]])");
            checkResult("TwoShard", R"([[[1u];["One"];[1]];[[2u];["Two"];[1]];[[3u];["Three"];[1]];[[4000000001u];["BigOne"];[1]];[[4000000002u];["BigTwo"];[1]];[[4000000003u];["BigThree"];[1]]])");
        }
    }

    Y_UNIT_TEST(IssuesInCaseOfSuccess) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();
        auto session = kikimr.GetTableClient().CreateSession().GetValueSync().GetSession();
        CreateSampleTablesWithIndex(session, true);
        auto selectRes = db.ExecuteQuery(
            "SELECT Value FROM `/Root/SecondaryKeys` VIEW Index WHERE Key = 2",
            TTxControl::BeginTx().CommitTx()
        ).ExtractValueSync();

        UNIT_ASSERT_C(selectRes.IsSuccess(), selectRes.GetIssues().ToString());
        const TString expected = R"([[["Payload2"]]])";
        CompareYson(expected, FormatResultSetYson(selectRes.GetResultSet(0)));
        UNIT_ASSERT_C(HasIssue(selectRes.GetIssues(), NYql::TIssuesIds::KIKIMR_WRONG_INDEX_USAGE,
            [](const auto& issue) {
                return issue.GetMessage().contains("Given predicate is not suitable for used index: Index");
            }), selectRes.GetIssues().ToString());
    }

    Y_UNIT_TEST(ExecuteQueryInteractiveTxCommitWithQuery) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();
        auto sessionResult = db.GetSession().ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(sessionResult.GetStatus(), EStatus::SUCCESS, sessionResult.GetIssues().ToString());
        auto session = sessionResult.GetSession();

        const TString query = "UPDATE TwoShard SET Value2 = 0";
        auto result = session.ExecuteQuery(query, TTxControl::BeginTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        auto transaction = result.GetTransaction();
        UNIT_ASSERT(transaction->IsActive());

        auto checkResult = [&](TString expected) {
            auto selectRes = db.ExecuteQuery(
                "SELECT * FROM TwoShard ORDER BY Key",
                TTxControl::BeginTx().CommitTx()
            ).ExtractValueSync();

            UNIT_ASSERT_C(selectRes.IsSuccess(), selectRes.GetIssues().ToString());
            CompareYson(expected, FormatResultSetYson(selectRes.GetResultSet(0)));
        };
        checkResult(R"([[[1u];["One"];[-1]];[[2u];["Two"];[0]];[[3u];["Three"];[1]];[[4000000001u];["BigOne"];[-1]];[[4000000002u];["BigTwo"];[0]];[[4000000003u];["BigThree"];[1]]])");

        result = session.ExecuteQuery("UPDATE TwoShard SET Value2 = 1 WHERE Key = 1",
            TTxControl::Tx(*transaction).CommitTx()).ExtractValueSync();;
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        UNIT_ASSERT(!result.GetTransaction());

        checkResult(R"([[[1u];["One"];[1]];[[2u];["Two"];[0]];[[3u];["Three"];[0]];[[4000000001u];["BigOne"];[0]];[[4000000002u];["BigTwo"];[0]];[[4000000003u];["BigThree"];[0]]])");
    }


    Y_UNIT_TEST(ForbidInteractiveTxOnImplicitSession) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();

        const TString query = "SELECT 1";
        UNIT_ASSERT_EXCEPTION(db.ExecuteQuery(query, TTxControl::BeginTx()).ExtractValueSync(), NYdb::TContractViolation);
    }

    Y_UNIT_TEST(ExecuteRetryQuery) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();
        const TString query = "SELECT Key, Value2 FROM TwoShard WHERE Value2 > 0 ORDER BY Key";
        auto queryFunc = [&query](TSession session) -> TAsyncExecuteQueryResult {
            return session.ExecuteQuery(query, TTxControl::BeginTx().CommitTx());
        };

        auto resultRetryFunc = db.RetryQuery(std::move(queryFunc)).GetValueSync();
        int attempt = 10;
        while (attempt-- && db.GetActiveSessionCount() > 0) {
            Sleep(TDuration::MilliSeconds(100));
        }
        UNIT_ASSERT_VALUES_EQUAL(db.GetActiveSessionCount(), 0);
        CheckQueryResult(resultRetryFunc);
    }

    Y_UNIT_TEST(ExecuteQueryPg) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();

        auto settings = TExecuteQuerySettings()
            .Syntax(ESyntax::Pg);

        auto result = db.ExecuteQuery(R"(
            SELECT * FROM (VALUES
                (1::int8, 'one'),
                (2::int8, 'two'),
                (3::int8, 'three')
            ) AS t;
        )", TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

        CompareYson(R"([
            ["1";"one"];
            ["2";"two"];
            ["3";"three"]
        ])", FormatResultSetYson(result.GetResultSet(0)));
    }

    //KIKIMR-18492
    Y_UNIT_TEST(ExecuteQueryPgTableSelect) {
        TKikimrRunner kikimr(NKqp::TKikimrSettings().SetWithSampleTables(false));
        auto settings = TExecuteQuerySettings()
            .Syntax(ESyntax::Pg);
        {
            auto db = kikimr.GetTableClient();
            auto session = db.CreateSession().GetValueSync().GetSession();
            auto result = session.ExecuteSchemeQuery(R"(
                CREATE TABLE test (id int16,PRIMARY KEY (id)))"
            ).GetValueSync();

            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
        }
        {
            auto db = kikimr.GetQueryClient();
            auto result = db.ExecuteQuery(
                "SELECT * FROM test",
                TTxControl::BeginTx().CommitTx(), settings
            ).ExtractValueSync();

            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
            CompareYson(R"([])", FormatResultSetYson(result.GetResultSet(0)));
        }
    }

    Y_UNIT_TEST(ExecuteDDLStatusCodeSchemeError) {
        TKikimrRunner kikimr(NKqp::TKikimrSettings().SetWithSampleTables(false));
        {
            auto db = kikimr.GetQueryClient();
            auto result = db.ExecuteQuery(R"(
                CREATE TABLE unsupported_TzTimestamp (key Int32, payload TzTimestamp, primary key(key)))",
                TTxControl::NoTx()
            ).GetValueSync();

            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SCHEME_ERROR, result.GetIssues().ToString());
        }
    }

    Y_UNIT_TEST(ExecuteQueryScalar) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();

        auto result = db.ExecuteQuery(R"(
            SELECT COUNT(*) FROM EightShard;
        )", TTxControl::BeginTx().CommitTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

        CompareYson(R"([[24u]])", FormatResultSetYson(result.GetResultSet(0)));
    }

    Y_UNIT_TEST(ExecuteQueryMultiResult) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();

        auto settings = TExecuteQuerySettings()
            .StatsMode(EStatsMode::Basic);

        auto result = db.ExecuteQuery(R"(
            SELECT * FROM EightShard WHERE Text = "Value2" AND Data = 1 ORDER BY Key;
            SELECT * FROM TwoShard WHERE Key < 10 ORDER BY Key;
        )", TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

        auto& stats = NYdb::TProtoAccessor::GetProto(*result.GetStats());
        UNIT_ASSERT_VALUES_EQUAL(stats.query_phases().size(), 1);

        CompareYson(R"([
            [[1];[202u];["Value2"]];
            [[1];[502u];["Value2"]];
            [[1];[802u];["Value2"]]])", FormatResultSetYson(result.GetResultSet(0)));
        CompareYson(R"([
            [[1u];["One"];[-1]];
            [[2u];["Two"];[0]];
            [[3u];["Three"];[1]]])", FormatResultSetYson(result.GetResultSet(1)));
    }

    Y_UNIT_TEST(ExecuteQueryMultiScalar) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();

        auto result = db.ExecuteQuery(R"(
            SELECT COUNT(*) FROM EightShard;
            SELECT COUNT(*) FROM TwoShard;
        )", TTxControl::BeginTx().CommitTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

        CompareYson(R"([[24u]])", FormatResultSetYson(result.GetResultSet(0)));
        CompareYson(R"([[6u]])", FormatResultSetYson(result.GetResultSet(1)));
    }

    Y_UNIT_TEST(StreamExecuteQueryMultiResult) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();

        auto it = db.StreamExecuteQuery(R"(
            SELECT * FROM EightShard WHERE Text = "Value2" AND Data = 1 ORDER BY Key;
            SELECT 2;
            SELECT * FROM TwoShard WHERE Key < 10 ORDER BY Key;
        )", TTxControl::BeginTx().CommitTx()).ExtractValueSync();
        UNIT_ASSERT_C(it.IsSuccess(), it.GetIssues().ToString());

        ui64 lastResultSetIndex = 0;
        ui64 count = 0;
        for (;;) {
            auto streamPart = it.ReadNext().GetValueSync();
            if (!streamPart.IsSuccess()) {
                UNIT_ASSERT_C(streamPart.EOS(), streamPart.GetIssues().ToString());
                break;
            }

            if (streamPart.HasResultSet()) {
                if (streamPart.GetResultSetIndex() != lastResultSetIndex) {
                    UNIT_ASSERT_VALUES_EQUAL(streamPart.GetResultSetIndex(), lastResultSetIndex + 1);
                    ++lastResultSetIndex;
                }

                auto resultSet = streamPart.ExtractResultSet();
                count += resultSet.RowsCount();
            }
        }

        UNIT_ASSERT_VALUES_EQUAL(count, 7);
    }

    Y_UNIT_TEST(Write) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();

        auto result = db.ExecuteQuery(R"(
            UPSERT INTO TwoShard (Key, Value2) VALUES(0, 101);

            SELECT Value2 FROM TwoShard WHERE Key = 0;
        )", TTxControl::BeginTx().CommitTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

        CompareYson(R"([[[101]]])", FormatResultSetYson(result.GetResultSet(0)));
    }

    Y_UNIT_TEST(Explain) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();

        auto params = TParamsBuilder()
            .AddParam("$value").Int64(17).Build()
            .Build();

        auto settings = TExecuteQuerySettings()
            .ExecMode(EExecMode::Explain);

        auto result = db.ExecuteQuery(R"(
            DECLARE $value As Int64;
            SELECT $value;
        )", TTxControl::NoTx(), params, settings).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        UNIT_ASSERT(result.GetResultSets().empty());

        UNIT_ASSERT(result.GetStats().has_value());
        UNIT_ASSERT(result.GetStats()->GetPlan().has_value());

        NJson::TJsonValue plan;
        NJson::ReadJsonTree(*result.GetStats()->GetPlan(), &plan, true);
        UNIT_ASSERT(ValidatePlanNodeIds(plan));
    }

    Y_UNIT_TEST(ExecStats) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();

        auto params = TParamsBuilder()
            .AddParam("$value").Uint32(10).Build()
            .Build();

        auto settings = TExecuteQuerySettings()
            .StatsMode(EStatsMode::Basic);

        auto result = db.ExecuteQuery(R"(
            DECLARE $value As Uint32;
            SELECT * FROM TwoShard WHERE Key < $value;
        )", TTxControl::BeginTx().CommitTx(), params, settings).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

        UNIT_ASSERT_VALUES_EQUAL(result.GetResultSet(0).RowsCount(), 3);
        UNIT_ASSERT(result.GetStats().has_value());
        UNIT_ASSERT(!result.GetStats()->GetPlan().has_value());

        auto& stats = NYdb::TProtoAccessor::GetProto(*result.GetStats());
        UNIT_ASSERT_VALUES_EQUAL(stats.query_phases().size(), 1);
    }

    Y_UNIT_TEST(ExecStatsPlan) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();

        auto params = TParamsBuilder()
            .AddParam("$value").Uint32(10).Build()
            .Build();

        auto settings = TExecuteQuerySettings()
            .StatsMode(EStatsMode::Full);

        auto result = db.ExecuteQuery(R"(
            DECLARE $value As Uint32;
            SELECT * FROM TwoShard WHERE Key < $value;
        )", TTxControl::BeginTx().CommitTx(), params, settings).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

        UNIT_ASSERT_VALUES_EQUAL(result.GetResultSet(0).RowsCount(), 3);
        UNIT_ASSERT(result.GetStats().has_value());
        UNIT_ASSERT(result.GetStats()->GetPlan().has_value());

        NJson::TJsonValue plan;
        NJson::ReadJsonTree(*result.GetStats()->GetPlan(), &plan, true);

        auto stages = FindPlanStages(plan);

        i64 totalTasks = 0;
        for (const auto& stage : stages) {
            if (stage.GetMapSafe().contains("Stats")) {
                totalTasks += stage.GetMapSafe().at("Stats").GetMapSafe().at("Tasks").GetIntegerSafe();
            }
        }
        UNIT_ASSERT_VALUES_EQUAL(totalTasks, 1);
    }

    Y_UNIT_TEST(ExecStatsAst) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();

        auto settings = TExecuteQuerySettings()
            .StatsMode(EStatsMode::Full);

        std::vector<std::pair<TString, EStatus>> cases = {
            { "SELECT 42 AS test_ast_column", EStatus::SUCCESS },
            { "SELECT test_ast_column FROM TwoShard", EStatus::GENERIC_ERROR },
            { "SELECT UNWRAP(42 / 0) AS test_ast_column", EStatus::PRECONDITION_FAILED },
        };

        for (const auto& [sql, status] : cases) {
            auto result = db.ExecuteQuery(sql, TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), status, result.GetIssues().ToString());

            UNIT_ASSERT(result.GetStats().has_value());
            UNIT_ASSERT(result.GetStats()->GetAst().has_value());
            UNIT_ASSERT_STRING_CONTAINS(*result.GetStats()->GetAst(), "test_ast_column");
        }
    }

    Y_UNIT_TEST(Ddl) {
        NKikimrConfig::TAppConfig appConfig;
        auto setting = NKikimrKqp::TKqpSetting();
        auto serverSettings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetKqpSettings({setting});

        TKikimrRunner kikimr(serverSettings);
        auto db = kikimr.GetQueryClient();

        enum EEx {
            Empty,
            IfExists,
            IfNotExists,
        };

        auto checkCreate = [&](bool expectSuccess, EEx exMode, int nameSuffix) {
            UNIT_ASSERT_UNEQUAL(exMode, EEx::IfExists);
            const TString ifNotExistsStatement = exMode == EEx::IfNotExists ? "IF NOT EXISTS" : "";
            const TString sql = fmt::format(R"sql(
                CREATE TABLE {if_not_exists} TestDdl_{name_suffix} (
                    Key Uint64,
                    Value String,
                    PRIMARY KEY (Key)
                );
                )sql",
                "if_not_exists"_a = ifNotExistsStatement,
                "name_suffix"_a = nameSuffix
            );

            auto result = db.ExecuteQuery(sql, TTxControl::NoTx()).ExtractValueSync();
            if (expectSuccess) {
                UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            } else {
                UNIT_ASSERT_VALUES_UNEQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            }
            UNIT_ASSERT(result.GetResultSets().empty());
        };

        auto checkDrop = [&](bool expectSuccess, EEx exMode, int nameSuffix) {
            UNIT_ASSERT_UNEQUAL(exMode, EEx::IfNotExists);
            const TString ifExistsStatement = exMode == EEx::IfExists ? "IF EXISTS" : "";
            const TString sql = fmt::format(R"sql(
                DROP TABLE {if_exists} TestDdl_{name_suffix};
                )sql",
                "if_exists"_a = ifExistsStatement,
                "name_suffix"_a = nameSuffix
            );

            auto result = db.ExecuteQuery(sql, TTxControl::NoTx()).ExtractValueSync();
            if (expectSuccess) {
                UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            } else {
                UNIT_ASSERT_VALUES_UNEQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            }
            UNIT_ASSERT(result.GetResultSets().empty());
        };

        auto checkUpsert = [&](int nameSuffix) {
            const TString sql = fmt::format(R"sql(
                UPSERT INTO TestDdl_{name_suffix} (Key, Value) VALUES (1, "One");
                )sql",
                "name_suffix"_a = nameSuffix
            );

            auto result = db.ExecuteQuery(sql, TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        };

        auto checkExists = [&](bool expectSuccess, int nameSuffix) {
            const TString sql = fmt::format(R"sql(
                SELECT * FROM TestDdl_{name_suffix};
                )sql",
                "name_suffix"_a = nameSuffix
            );
            auto result = db.ExecuteQuery(sql, TTxControl::BeginTx().CommitTx()).ExtractValueSync();

            if (expectSuccess) {
                UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
                CompareYson(R"([[[1u];["One"]]])", FormatResultSetYson(result.GetResultSet(0)));
            } else {
                UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SCHEME_ERROR, result.GetIssues().ToString());
            }
        };

        auto checkRename = [&](bool expectSuccess, int nameSuffix, int nameSuffixTo) {
            const TString sql = fmt::format(R"sql(
                ALTER TABLE TestDdl_{name_suffix} RENAME TO TestDdl_{name_suffix_to}
                )sql",
                "name_suffix"_a = nameSuffix,
                "name_suffix_to"_a = nameSuffixTo
            );

            auto result = db.ExecuteQuery(sql, TTxControl::NoTx()).ExtractValueSync();
            if (expectSuccess) {
                UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            } else {
                UNIT_ASSERT_VALUES_UNEQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            }
            UNIT_ASSERT(result.GetResultSets().empty());
        };

        // usual create
        checkCreate(true, EEx::Empty, 0);
        checkUpsert(0);
        checkExists(true, 0);

        // create already existing table
        checkCreate(false, EEx::Empty, 0); // already exists
        checkCreate(true, EEx::IfNotExists, 0);
        checkExists(true, 0);

        // usual drop
        checkDrop(true, EEx::Empty, 0);
        checkExists(false, 0);
        checkDrop(false, EEx::Empty, 0); // no such table

        // drop if exists
        checkDrop(true, EEx::IfExists, 0);
        checkExists(false, 0);

        // failed attempt to drop nonexisting table
        checkDrop(false, EEx::Empty, 0);

        // create with if not exists
        checkCreate(true, EEx::IfNotExists, 1); // real creation
        checkUpsert(1);
        checkExists(true, 1);
        checkCreate(true, EEx::IfNotExists, 1);

        // drop if exists
        checkDrop(true, EEx::IfExists, 1); // real drop
        checkExists(false, 1);
        checkDrop(true, EEx::IfExists, 1);

        // rename
        checkCreate(true, EEx::Empty, 2);
        checkRename(true, 2, 3);
        checkRename(false, 2, 3); // already renamed, no such table
        checkDrop(false, EEx::Empty, 2); // no such table
        checkDrop(true, EEx::Empty, 3);
    }

    Y_UNIT_TEST(DdlColumnTable) {
        const TVector<TTestHelper::TColumnSchema> schema = {
            TTestHelper::TColumnSchema().SetName("Key").SetType(NScheme::NTypeIds::Uint64).SetNullable(false),
            TTestHelper::TColumnSchema().SetName("Value").SetType(NScheme::NTypeIds::String)
        };

        NKikimrConfig::TAppConfig appConfig;
        auto setting = NKikimrKqp::TKqpSetting();
        auto serverSettings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetKqpSettings({setting});

        TTestHelper testHelper(serverSettings);
        auto& kikimr = testHelper.GetKikimr();

        auto db = kikimr.GetQueryClient();

        enum EEx {
            Empty,
            IfExists,
            IfNotExists,
        };

        auto checkCreate = [&](bool expectSuccess, EEx exMode, const TString& objPath, bool isStore) {
            UNIT_ASSERT_UNEQUAL(exMode, EEx::IfExists);
            const TString ifNotExistsStatement = exMode == EEx::IfNotExists ? "IF NOT EXISTS" : "";
            const TString objType = isStore ? "TABLESTORE" : "TABLE";
            const TString hash = !isStore ? " PARTITION BY HASH(Key) " : "";
            auto sql = TStringBuilder() << R"(
                --!syntax_v1
                CREATE )" << objType << " " << ifNotExistsStatement << " `" << objPath << R"(` (
                    Key Uint64 NOT NULL,
                    Value String,
                    PRIMARY KEY (Key)
                ))" << hash << R"(
                WITH (
                    STORE = COLUMN,
                    AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 10
                );)";

            auto result = db.ExecuteQuery(sql, TTxControl::NoTx()).ExtractValueSync();
            if (expectSuccess) {
                UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            } else {
                UNIT_ASSERT_VALUES_UNEQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            }
            UNIT_ASSERT(result.GetResultSets().empty());
        };

        auto checkAlter = [&](const TString& objPath, bool isStore) {
            const TString objType = isStore ? "TABLESTORE" : "TABLE";
            {
                auto sql = TStringBuilder() << R"(
                    --!syntax_v1
                    ALTER )" << objType << " `" << objPath << R"(`
                        ADD COLUMN NewColumn Uint64;
                    ;)";

                auto result = db.ExecuteQuery(sql, TTxControl::NoTx()).ExtractValueSync();
                UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            }
            {
                auto sql = TStringBuilder() << R"(
                    --!syntax_v1
                    ALTER )" << objType << " `" << objPath << R"(`
                        DROP COLUMN NewColumn;
                    ;)";

                auto result = db.ExecuteQuery(sql, TTxControl::NoTx()).ExtractValueSync();
                UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            }
        };

        auto checkDrop = [&](bool expectSuccess, EEx exMode,
                const TString& objPath, bool isStore) {
            UNIT_ASSERT_UNEQUAL(exMode, EEx::IfNotExists);
            const TString ifExistsStatement = exMode == EEx::IfExists ? "IF EXISTS" : "";
            const TString objType = isStore ? "TABLESTORE" : "TABLE";
            auto sql = TStringBuilder() << R"(
                --!syntax_v1
                DROP )" << objType << " " << ifExistsStatement << " `" << objPath << R"(`;)";

            auto result = db.ExecuteQuery(sql, TTxControl::NoTx()).ExtractValueSync();
            if (expectSuccess) {
                UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            } else {
                UNIT_ASSERT_VALUES_UNEQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            }
            UNIT_ASSERT(result.GetResultSets().empty());
        };

        auto checkAddRow = [&](const TString& objPath) {
            const size_t inserted_rows = 5;
            TTestHelper::TColumnTable testTable;
            testTable.SetName(objPath)
                .SetPrimaryKey({"Key"})
                .SetSharding({"Key"})
                .SetSchema(schema);
            {
                TTestHelper::TUpdatesBuilder tableInserter(testTable.GetArrowSchema(schema));
                for (size_t i = 0; i < inserted_rows; i++) {
                    tableInserter.AddRow().Add(i).Add("test_res_" + std::to_string(i));
                }
                testHelper.BulkUpsert(testTable, tableInserter);
            }

            Sleep(TDuration::Seconds(100));

            auto sql = TStringBuilder() << R"(
                SELECT Value FROM `)" << objPath << R"(` WHERE Key=1)";

            testHelper.ReadData(sql, "[[[\"test_res_1\"]]]");
        };

        checkCreate(true, EEx::Empty, "/Root/TableStoreTest", true);
        checkCreate(false, EEx::Empty, "/Root/TableStoreTest", true);
        checkCreate(true, EEx::IfNotExists, "/Root/TableStoreTest", true);
        checkDrop(true, EEx::Empty, "/Root/TableStoreTest", true);
        checkDrop(true, EEx::IfExists, "/Root/TableStoreTest", true);
        checkDrop(false, EEx::Empty, "/Root/TableStoreTest", true);

        checkCreate(true, EEx::IfNotExists, "/Root/TableStoreTest", true);
        checkCreate(false, EEx::Empty, "/Root/TableStoreTest", true);
        checkDrop(true, EEx::IfExists, "/Root/TableStoreTest", true);
        checkDrop(false, EEx::Empty, "/Root/TableStoreTest", true);

        checkCreate(true, EEx::IfNotExists, "/Root/ColumnTable", false);
        checkAlter("/Root/ColumnTable", false);
        checkDrop(true, EEx::IfExists, "/Root/ColumnTable", false);

        checkCreate(true, EEx::Empty, "/Root/ColumnTable", false);
        checkCreate(false, EEx::Empty, "/Root/ColumnTable", false);
        checkCreate(true, EEx::IfNotExists, "/Root/ColumnTable", false);
        checkAddRow("/Root/ColumnTable");
        checkDrop(true, EEx::IfExists, "/Root/ColumnTable", false);
        checkDrop(false, EEx::Empty, "/Root/ColumnTable", false);
        checkDrop(true, EEx::IfExists, "/Root/ColumnTable", false);
    }

    Y_UNIT_TEST(DdlUser) {
        NKikimrConfig::TAppConfig appConfig;
        auto setting = NKikimrKqp::TKqpSetting();
        auto serverSettings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetKqpSettings({setting});

        TKikimrRunner kikimr(serverSettings);
        auto db = kikimr.GetQueryClient();

        auto result = db.ExecuteQuery(R"(
            CREATE USER user1 PASSWORD 'password1';
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

        result = db.ExecuteQuery(R"(
            CREATE USER user1 PASSWORD 'password1';
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::PRECONDITION_FAILED, result.GetIssues().ToString());

        result = db.ExecuteQuery(R"(
            ALTER USER user1 WITH ENCRYPTED PASSWORD 'password3';
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

        result = db.ExecuteQuery(R"(
            DROP USER user1;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

        result = db.ExecuteQuery(R"(
            ALTER USER user1 WITH ENCRYPTED PASSWORD 'password3';
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::PRECONDITION_FAILED, result.GetIssues().ToString());

        result = db.ExecuteQuery(R"(
            DROP USER user1;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::PRECONDITION_FAILED, result.GetIssues().ToString());

        result = db.ExecuteQuery(R"(
            DROP USER IF EXISTS user1;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
    }

    Y_UNIT_TEST(CreateTempTable) {
        NKikimrConfig::TAppConfig appConfig;
        auto setting = NKikimrKqp::TKqpSetting();
        auto serverSettings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetKqpSettings({setting})
            .SetAuthToken("user0@builtin");
        TKikimrRunner kikimr(
            serverSettings.SetWithSampleTables(false).SetEnableTempTables(true));
        auto clientConfig = NGRpcProxy::TGRpcClientConfig(kikimr.GetEndpoint());
        auto client = kikimr.GetQueryClient();

        TString SessionId;
        {
            auto session = client.GetSession().GetValueSync().GetSession();
            auto id = session.GetId();

            const auto queryCreate = Q_(R"(
                --!syntax_v1
                CREATE TEMP TABLE Temp (
                    Key Uint64 NOT NULL,
                    Value String,
                    PRIMARY KEY (Key)
                );)");

            auto resultCreate = session.ExecuteQuery(queryCreate, NYdb::NQuery::TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_C(resultCreate.IsSuccess(), resultCreate.GetIssues().ToString());

            const auto querySelect = Q_(R"(
                --!syntax_v1
                SELECT * FROM Temp;
            )");

            auto resultSelect = session.ExecuteQuery(
                querySelect, NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(resultSelect.IsSuccess(), resultSelect.GetIssues().ToString());

            const TVector<TString> sessionIdSplitted = StringSplitter(id).SplitByString("&id=");
            SessionId = sessionIdSplitted.back();

            const auto queryCreateRestricted = Q_(fmt::format(R"(
                --!syntax_v1
                CREATE TABLE `/Root/.tmp/sessions/{}/Test` (
                    Key Uint64 NOT NULL,
                    Value String,
                    PRIMARY KEY (Key)
                );)", SessionId));

            auto resultCreateRestricted = session.ExecuteQuery(queryCreateRestricted, NYdb::NQuery::TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT(!resultCreateRestricted.IsSuccess());
            UNIT_ASSERT_C(resultCreateRestricted.GetIssues().ToString().contains("error: path is temporary"), resultCreateRestricted.GetIssues().ToString());

            bool allDoneOk = true;
            NTestHelpers::CheckDelete(clientConfig, TString{id}, Ydb::StatusIds::SUCCESS, allDoneOk);

            UNIT_ASSERT(allDoneOk);
        }

        {
            const auto querySelect = Q_(R"(
                --!syntax_v1
                SELECT * FROM Temp;
            )");

            auto resultSelect = client.ExecuteQuery(
                querySelect, NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT(!resultSelect.IsSuccess());
        }

        Sleep(TDuration::Seconds(1));

        {
            auto schemeClient = kikimr.GetSchemeClient();
            auto listResult = schemeClient.ListDirectory(
                fmt::format("/Root/.tmp/sessions/{}", SessionId)
                ).GetValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(listResult.GetStatus(), NYdb::EStatus::SCHEME_ERROR, listResult.GetIssues().ToString());
            UNIT_ASSERT_STRING_CONTAINS_C(listResult.GetIssues().ToString(), "Path not found",listResult.GetIssues().ToString());
        }
    }

    Y_UNIT_TEST(AlterTempTable) {
        NKikimrConfig::TAppConfig appConfig;
        auto setting = NKikimrKqp::TKqpSetting();
        auto serverSettings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetKqpSettings({setting});
        TKikimrRunner kikimr(
            serverSettings.SetWithSampleTables(false).SetEnableTempTables(true));
        auto clientConfig = NGRpcProxy::TGRpcClientConfig(kikimr.GetEndpoint());
        auto client = kikimr.GetQueryClient();
        auto settings = NYdb::NQuery::TExecuteQuerySettings()
            .StatsMode(NYdb::NQuery::EStatsMode::Basic);
        {
            auto session = client.GetSession().GetValueSync().GetSession();
            auto id = session.GetId();

            {
                const auto query = Q_(R"(
                    --!syntax_v1
                    CREATE TEMP TABLE Temp (
                        Key Int32 NOT NULL,
                        Value Int32,
                        PRIMARY KEY (Key)
                    );)");

                auto result =
                    session.ExecuteQuery(query, NYdb::NQuery::TTxControl::NoTx(), settings).ExtractValueSync();
                UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
                auto stats = NYdb::TProtoAccessor::GetProto(*result.GetStats());
                UNIT_ASSERT_VALUES_EQUAL(stats.compilation().from_cache(), false);
            }

            {
                const auto query = Q_(R"(
                    --!syntax_v1
                    ALTER TABLE Temp DROP COLUMN Value;
                )");
                auto result =
                    session.ExecuteQuery(query, NYdb::NQuery::TTxControl::NoTx(), settings).ExtractValueSync();
                UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
                auto stats = NYdb::TProtoAccessor::GetProto(*result.GetStats());
                UNIT_ASSERT_VALUES_EQUAL(stats.compilation().from_cache(), false);
            }

            {
                const auto query = Q_(R"(
                    --!syntax_v1
                    DROP TABLE Temp;
                )");

                auto result =
                    session.ExecuteQuery(query, NYdb::NQuery::TTxControl::NoTx(), settings).ExtractValueSync();
                UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
                auto stats = NYdb::TProtoAccessor::GetProto(*result.GetStats());
                UNIT_ASSERT_VALUES_EQUAL(stats.compilation().from_cache(), false);
            }

            {
                const auto query = Q_(R"(
                    --!syntax_v1
                    CREATE TEMP TABLE Temp (
                        Key Int32 NOT NULL,
                        Value Int32,
                        PRIMARY KEY (Key)
                    );)");

                auto result =
                    session.ExecuteQuery(query, NYdb::NQuery::TTxControl::NoTx(), settings).ExtractValueSync();
                UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
                auto stats = NYdb::TProtoAccessor::GetProto(*result.GetStats());
                UNIT_ASSERT_VALUES_EQUAL(stats.compilation().from_cache(), false);

                auto resultInsert = session.ExecuteQuery(R"(
                    UPSERT INTO Temp (Key, Value) VALUES (1, 1);
                )", NYdb::NQuery::TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
                UNIT_ASSERT_VALUES_EQUAL_C(
                    resultInsert.GetStatus(), EStatus::SUCCESS, resultInsert.GetIssues().ToString());
            }

            {
                const auto query = Q_(R"(
                    --!syntax_v1
                    SELECT * FROM Temp;
                )");

                auto result = session.ExecuteQuery(
                    query, NYdb::NQuery::TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
                UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
                auto stats = NYdb::TProtoAccessor::GetProto(*result.GetStats());
                UNIT_ASSERT_VALUES_EQUAL(stats.compilation().from_cache(), false);

                UNIT_ASSERT_C(!result.GetResultSets().empty(), "results are empty");
                CompareYson(R"([[1;[1]]])", FormatResultSetYson(result.GetResultSet(0)));
            }

            {
                auto result = session.ExecuteQuery(R"(
                    ALTER TABLE Temp DROP COLUMN Value;
                )", NYdb::NQuery::TTxControl::NoTx(), settings).ExtractValueSync();
                UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
                auto stats = NYdb::TProtoAccessor::GetProto(*result.GetStats());
                UNIT_ASSERT_VALUES_EQUAL(stats.compilation().from_cache(), false);
            }

            {
                const auto query = Q_(R"(
                    --!syntax_v1
                    SELECT * FROM Temp;
                )");

                auto result = session.ExecuteQuery(
                    query, NYdb::NQuery::TTxControl::BeginTx().CommitTx(), settings).ExtractValueSync();
                UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
                auto stats = NYdb::TProtoAccessor::GetProto(*result.GetStats());
                UNIT_ASSERT_VALUES_EQUAL(stats.compilation().from_cache(), false);

                UNIT_ASSERT_C(!result.GetResultSets().empty(), "results are empty");
                CompareYson(R"([[1]])", FormatResultSetYson(result.GetResultSet(0)));
            }

            {
                const auto query = Q_(R"(
                    --!syntax_v1
                    DROP TABLE Temp;
                )");

                auto result =
                    session.ExecuteQuery(query, NYdb::NQuery::TTxControl::NoTx(), settings).ExtractValueSync();
                UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
                auto stats = NYdb::TProtoAccessor::GetProto(*result.GetStats());
                UNIT_ASSERT_VALUES_EQUAL(stats.compilation().from_cache(), false);
            }

            {
                const auto querySelect = Q_(R"(
                    --!syntax_v1
                    SELECT * FROM Temp;
                )");

                auto resultSelect = client.ExecuteQuery(
                    querySelect, NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
                UNIT_ASSERT(!resultSelect.IsSuccess());
            }

            bool allDoneOk = true;
            NTestHelpers::CheckDelete(clientConfig, TString{id}, Ydb::StatusIds::SUCCESS, allDoneOk);

            UNIT_ASSERT(allDoneOk);
        }

        {
            const auto querySelect = Q_(R"(
                --!syntax_v1
                SELECT * FROM Temp;
            )");

            auto resultSelect = client.ExecuteQuery(
                querySelect, NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT(!resultSelect.IsSuccess());
        }
    }

    Y_UNIT_TEST(TempTablesDrop) {
        NKikimrConfig::TAppConfig appConfig;
        auto setting = NKikimrKqp::TKqpSetting();
        auto serverSettings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetKqpSettings({setting});
        TKikimrRunner kikimr(
            serverSettings.SetWithSampleTables(false).SetEnableTempTables(true));
        auto clientConfig = NGRpcProxy::TGRpcClientConfig(kikimr.GetEndpoint());
        auto client = kikimr.GetQueryClient();

        auto session = client.GetSession().GetValueSync().GetSession();
        auto id = session.GetId();

        const auto queryCreate = Q_(R"(
            --!syntax_v1
            CREATE TEMPORARY TABLE `/Root/test/Temp` (
                Key Uint64 NOT NULL,
                Value String,
                PRIMARY KEY (Key)
            );)");

        auto resultCreate = session.ExecuteQuery(queryCreate, NYdb::NQuery::TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_C(resultCreate.IsSuccess(), resultCreate.GetIssues().ToString());

        {
            const auto querySelect = Q_(R"(
                --!syntax_v1
                SELECT * FROM `/Root/test/Temp`;
            )");

            auto resultSelect = session.ExecuteQuery(
                querySelect, NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(resultSelect.IsSuccess(), resultSelect.GetIssues().ToString());
        }

        const auto queryDrop = Q_(R"(
            --!syntax_v1
            DROP TABLE `/Root/test/Temp`;
        )");

        auto resultDrop = session.ExecuteQuery(
            queryDrop, NYdb::NQuery::TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_C(resultDrop.IsSuccess(), resultDrop.GetIssues().ToString());

        {
            const auto querySelect = Q_(R"(
                --!syntax_v1
                SELECT * FROM `/Root/test/Temp`;
            )");

            auto resultSelect = session.ExecuteQuery(
                querySelect, NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT(!resultSelect.IsSuccess());
        }

        bool allDoneOk = true;
        NTestHelpers::CheckDelete(clientConfig, TString{id}, Ydb::StatusIds::SUCCESS, allDoneOk);

        UNIT_ASSERT(allDoneOk);

        auto sessionAnother = client.GetSession().GetValueSync().GetSession();
        auto idAnother = sessionAnother.GetId();
        UNIT_ASSERT(id != idAnother);

        {
            const auto querySelect = Q_(R"(
                --!syntax_v1
                SELECT * FROM `/Root/test/Temp`;
            )");

            auto resultSelect = sessionAnother.ExecuteQuery(
                querySelect, NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT(!resultSelect.IsSuccess());
        }
    }

    Y_UNIT_TEST(DdlGroup) {
        NKikimrConfig::TAppConfig appConfig;
        auto setting = NKikimrKqp::TKqpSetting();
        auto serverSettings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetKqpSettings({setting});

        TKikimrRunner kikimr(serverSettings);
        auto db = kikimr.GetQueryClient();

        // Check create and drop group
        auto result = db.ExecuteQuery(R"(
            CREATE GROUP group1;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

        result = db.ExecuteQuery(R"(
            CREATE GROUP group1;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::PRECONDITION_FAILED, result.GetIssues().ToString());

        result = db.ExecuteQuery(R"(
            DROP GROUP group1;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

        result = db.ExecuteQuery(R"(
            CREATE GROUP group1;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

        result = db.ExecuteQuery(R"(
            DROP GROUP group2;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::PRECONDITION_FAILED, result.GetIssues().ToString());

        result = db.ExecuteQuery(R"(
            DROP GROUP IF EXISTS group2;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

        // Check rename group
        result = db.ExecuteQuery(R"(
            ALTER GROUP group1 RENAME TO group2;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

        result = db.ExecuteQuery(R"(
            CREATE GROUP group1;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

        result = db.ExecuteQuery(R"(
            CREATE GROUP group2;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::PRECONDITION_FAILED, result.GetIssues().ToString());

        // Check add and drop users
        result = db.ExecuteQuery(R"(
            CREATE USER user1;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

        result = db.ExecuteQuery(R"(
            CREATE USER user2;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

        result = db.ExecuteQuery(R"(
            ALTER GROUP group1 ADD USER user1;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

        result = db.ExecuteQuery(R"(
            ALTER GROUP group1 ADD USER user1;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        UNIT_ASSERT(result.GetIssues().Size() == 1);
        UNIT_ASSERT_STRING_CONTAINS(result.GetIssues().ToOneLineString(), "{ <main>: Info: Success, code: 4 subissue: { <main>: Info: Role \"user1\" is already a member of role \"group1\", code: 2 } }");

        result = db.ExecuteQuery(R"(
            ALTER GROUP group1 ADD USER user3;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::PRECONDITION_FAILED, result.GetIssues().ToString());

        result = db.ExecuteQuery(R"(
            ALTER GROUP group1 DROP USER user1;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

        result = db.ExecuteQuery(R"(
            ALTER GROUP group1 DROP USER user1;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        UNIT_ASSERT(result.GetIssues().Size() == 1);
        UNIT_ASSERT_STRING_CONTAINS(result.GetIssues().ToOneLineString(), "{ <main>: Warning: Success, code: 4 subissue: { <main>: Warning: Role \"user1\" is not a member of role \"group1\", code: 3 } }");

        result = db.ExecuteQuery(R"(
            ALTER GROUP group1 DROP USER user3;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        UNIT_ASSERT(result.GetIssues().Size() == 1);
        UNIT_ASSERT_STRING_CONTAINS(result.GetIssues().ToOneLineString(), "{ <main>: Warning: Success, code: 4 subissue: { <main>: Warning: Role \"user3\" is not a member of role \"group1\", code: 3 } }");

        result = db.ExecuteQuery(R"(
            ALTER GROUP group1 ADD USER user1, user3, user2;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::PRECONDITION_FAILED, result.GetIssues().ToString());

        result = db.ExecuteQuery(R"(
            ALTER GROUP group1 ADD USER user1;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        UNIT_ASSERT(result.GetIssues().Size() == 1);
        UNIT_ASSERT_STRING_CONTAINS(result.GetIssues().ToOneLineString(), "{ <main>: Info: Success, code: 4 subissue: { <main>: Info: Role \"user1\" is already a member of role \"group1\", code: 2 } }");

        result = db.ExecuteQuery(R"(
            ALTER GROUP group1 ADD USER user2;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        UNIT_ASSERT(result.GetIssues().Size() == 0);

        result = db.ExecuteQuery(R"(
            ALTER GROUP group1 DROP USER user1, user3, user2;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        UNIT_ASSERT(result.GetIssues().Size() == 1);
        UNIT_ASSERT_STRING_CONTAINS(result.GetIssues().ToOneLineString(), "{ <main>: Warning: Success, code: 4 subissue: { <main>: Warning: Role \"user3\" is not a member of role \"group1\", code: 3 } }");

        result = db.ExecuteQuery(R"(
            ALTER GROUP group1 DROP USER user1;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        UNIT_ASSERT(result.GetIssues().Size() == 1);
        UNIT_ASSERT_STRING_CONTAINS(result.GetIssues().ToOneLineString(), "{ <main>: Warning: Success, code: 4 subissue: { <main>: Warning: Role \"user1\" is not a member of role \"group1\", code: 3 } }");

        result = db.ExecuteQuery(R"(
            ALTER GROUP group1 DROP USER user2;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        UNIT_ASSERT(result.GetIssues().Size() == 1);
        UNIT_ASSERT_STRING_CONTAINS(result.GetIssues().ToOneLineString(), "{ <main>: Warning: Success, code: 4 subissue: { <main>: Warning: Role \"user2\" is not a member of role \"group1\", code: 3 } }");

        //Check create with users
        result = db.ExecuteQuery(R"(
            CREATE GROUP group3 WITH USER user1;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

        result = db.ExecuteQuery(R"(
            CREATE GROUP group3;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::PRECONDITION_FAILED, result.GetIssues().ToString());

        result = db.ExecuteQuery(R"(
            ALTER GROUP group3 ADD USER user1;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        UNIT_ASSERT(result.GetIssues().Size() == 1);
        UNIT_ASSERT_STRING_CONTAINS(result.GetIssues().ToOneLineString(), "{ <main>: Info: Success, code: 4 subissue: { <main>: Info: Role \"user1\" is already a member of role \"group3\", code: 2 } }");

        result = db.ExecuteQuery(R"(
            ALTER GROUP group3 ADD USER user2;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

        result = db.ExecuteQuery(R"(
            CREATE GROUP group4 WITH USER user1, user3, user2;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::PRECONDITION_FAILED, result.GetIssues().ToString());

        result = db.ExecuteQuery(R"(
            CREATE GROUP group4;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::PRECONDITION_FAILED, result.GetIssues().ToString());
        UNIT_ASSERT(result.GetIssues().Size() == 1);
        UNIT_ASSERT_STRING_CONTAINS(result.GetIssues().ToOneLineString(), "{ <main>: Error: Group already exists, code: 2029 }");

        result = db.ExecuteQuery(R"(
            ALTER GROUP group4 ADD USER user1;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        UNIT_ASSERT(result.GetIssues().Size() == 1);
        UNIT_ASSERT_STRING_CONTAINS(result.GetIssues().ToOneLineString(), "{ <main>: Info: Success, code: 4 subissue: { <main>: Info: Role \"user1\" is already a member of role \"group4\", code: 2 } }");

        result = db.ExecuteQuery(R"(
            ALTER GROUP group4 ADD USER user2;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        UNIT_ASSERT(result.GetIssues().Size() == 0);
    }

    struct ExpectedPermissions {
        TString Path;
        THashMap<TString, TVector<TString>> Permissions;
    };

    Y_UNIT_TEST(DdlPermission) {
        NKikimrConfig::TAppConfig appConfig;
        auto setting = NKikimrKqp::TKqpSetting();
        auto serverSettings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetKqpSettings({setting});

        TKikimrRunner kikimr(serverSettings);
        auto db = kikimr.GetQueryClient();
        auto session = kikimr.GetTableClient().CreateSession().GetValueSync().GetSession();

        const auto checkPermissions = [](NYdb::NTable::TSession& session, TVector<ExpectedPermissions>&& expectedPermissionsValues) {
            for (auto& value : expectedPermissionsValues) {
                NYdb::NTable::TDescribeTableResult describe = session.DescribeTable(value.Path).GetValueSync();
                UNIT_ASSERT_VALUES_EQUAL(describe.GetStatus(), EStatus::SUCCESS);
                auto tableDesc = describe.GetTableDescription();
                const auto& permissions = tableDesc.GetPermissions();

                THashMap<TString, TVector<TString>> describePermissions;
                for (const auto& permission : permissions) {
                    auto& permissionNames = describePermissions[permission.Subject];
                    permissionNames.insert(permissionNames.end(), permission.PermissionNames.begin(), permission.PermissionNames.end());
                }

                auto& expectedPermissions = value.Permissions;
                UNIT_ASSERT_VALUES_EQUAL_C(expectedPermissions.size(), describePermissions.size(), "Number of user names does not equal on path: " + value.Path);
                for (auto& item : expectedPermissions) {
                    auto& expectedPermissionNames = item.second;
                    auto& describedPermissionNames = describePermissions[item.first];
                    UNIT_ASSERT_VALUES_EQUAL_C(expectedPermissionNames.size(), describedPermissionNames.size(), "Number of permissions for " + item.first + " does not equal on path: " + value.Path);
                    sort(expectedPermissionNames.begin(), expectedPermissionNames.end());
                    sort(describedPermissionNames.begin(), describedPermissionNames.end());
                    UNIT_ASSERT_VALUES_EQUAL_C(expectedPermissionNames, describedPermissionNames, "Permissions are not equal on path: " + value.Path);
                }
            }
        };

        {
            auto query = TStringBuilder() << R"(
            --!syntax_v1
            CREATE USER user1 PASSWORD 'password1';
            CREATE USER user2 PASSWORD 'password2';
            CREATE USER user3 PASSWORD 'password3';
            CREATE USER user4 PASSWORD 'password4';
            CREATE USER user5 PASSWORD 'password5';
            CREATE USER user6 PASSWORD 'password6';
            CREATE USER user7 PASSWORD 'password7';
            CREATE USER user8 PASSWORD 'password8';
            CREATE USER user9 PASSWORD 'password9';
            )";
            auto result = session.ExecuteSchemeQuery(query).GetValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        }

        auto result = db.ExecuteQuery(R"(
            GRANT ROW SELECT ON `/Root` TO user1;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::GENERIC_ERROR, result.GetIssues().ToString());
        UNIT_ASSERT_STRING_CONTAINS(result.GetIssues().ToString(), "extraneous input 'ROW'");
        checkPermissions(session, {{.Path = "/Root", .Permissions = {}}});

        result = db.ExecuteQuery(R"(
            GRANT `ydb.database.connect` ON `/Root` TO user1;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::GENERIC_ERROR, result.GetIssues().ToString());
        UNIT_ASSERT_STRING_CONTAINS_C(result.GetIssues().ToString(), "mismatched input '`ydb.database.connect`'", result.GetIssues().ToString());
        checkPermissions(session, {{.Path = "/Root", .Permissions = {}}});

        result = db.ExecuteQuery(R"(
            GRANT CONNECT, READ ON `/Root` TO user1;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::GENERIC_ERROR, result.GetIssues().ToString());
        UNIT_ASSERT_STRING_CONTAINS(result.GetIssues().ToString(), "extraneous input 'READ'");
        checkPermissions(session, {{.Path = "/Root", .Permissions = {}}});

        result = db.ExecuteQuery(R"(
            GRANT "" ON `/Root` TO user1;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::GENERIC_ERROR, result.GetIssues().ToString());
        UNIT_ASSERT_STRING_CONTAINS(result.GetIssues().ToString(), "Unknown permission name: ");
        checkPermissions(session, {{.Path = "/Root", .Permissions = {}}});

        result = db.ExecuteQuery(R"(
            CREATE TABLE TestDdl1 (
                Key Uint64,
                PRIMARY KEY (Key)
            );
        )", TTxControl::NoTx()).ExtractValueSync();

        result = db.ExecuteQuery(R"(
            CREATE TABLE TestDdl2 (
                Key Uint64,
                PRIMARY KEY (Key)
            );
        )", TTxControl::NoTx()).ExtractValueSync();

        result = db.ExecuteQuery(R"(
            GRANT CONNECT ON `/Root` TO user1;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        checkPermissions(session, {
            {.Path = "/Root", .Permissions = {{"user1", {"ydb.database.connect"}}}},
            {.Path = "/Root/TestDdl1", .Permissions = {}},
            {.Path = "/Root/TestDdl2", .Permissions = {}}
        });

        result = db.ExecuteQuery(R"(
            REVOKE "ydb.database.connect" ON `/Root` FROM user1;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        checkPermissions(session, {
            {.Path = "/Root", .Permissions = {}},
            {.Path = "/Root/TestDdl1", .Permissions = {}},
            {.Path = "/Root/TestDdl2", .Permissions = {}}
        });

        result = db.ExecuteQuery(R"(
            GRANT MODIFY TABLES, 'ydb.tables.read' ON `/Root/TestDdl1`, `/Root/TestDdl2` TO user2;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        checkPermissions(session, {
            {.Path = "/Root", .Permissions = {}},
            {.Path = "/Root/TestDdl1", .Permissions = {{"user2", {"ydb.tables.read", "ydb.tables.modify"}}}},
            {.Path = "/Root/TestDdl2", .Permissions = {{"user2", {"ydb.tables.read", "ydb.tables.modify"}}}}
        });

        result = db.ExecuteQuery(R"(
            REVOKE SELECT TABLES, "ydb.tables.modify", ON `/Root/TestDdl2` FROM user2;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        checkPermissions(session, {
            {.Path = "/Root", .Permissions = {}},
            {.Path = "/Root/TestDdl1", .Permissions = {{"user2", {"ydb.tables.read", "ydb.tables.modify"}}}},
            {.Path = "/Root/TestDdl2", .Permissions = {}}
        });

        result = db.ExecuteQuery(R"(
            GRANT "ydb.generic.read", LIST, "ydb.generic.write", USE LEGACY ON `/Root` TO user3, user4, user5;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        checkPermissions(session, {
            {.Path = "/Root", .Permissions = {
                {"user3", {"ydb.generic.read", "ydb.generic.list", "ydb.generic.write", "ydb.generic.use_legacy"}},
                {"user4", {"ydb.generic.read", "ydb.generic.list", "ydb.generic.write", "ydb.generic.use_legacy"}},
                {"user5", {"ydb.generic.read", "ydb.generic.list", "ydb.generic.write", "ydb.generic.use_legacy"}}
            }},
            {.Path = "/Root/TestDdl1", .Permissions = {{"user2", {"ydb.tables.read", "ydb.tables.modify"}}}},
            {.Path = "/Root/TestDdl2", .Permissions = {}}
        });

        result = db.ExecuteQuery(R"(
            REVOKE "ydb.generic.use_legacy", SELECT, "ydb.generic.list", INSERT ON `/Root` FROM user4, user3;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        checkPermissions(session, {
            {.Path = "/Root", .Permissions = {{"user5", {"ydb.generic.read", "ydb.generic.list", "ydb.generic.write", "ydb.generic.use_legacy"}}}},
            {.Path = "/Root/TestDdl1", .Permissions = {{"user2", {"ydb.tables.read", "ydb.tables.modify"}}}},
            {.Path = "/Root/TestDdl2", .Permissions = {}}
        });

        result = db.ExecuteQuery(R"(
            GRANT ALL ON `/Root` TO user6;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        checkPermissions(session, {
            {.Path = "/Root", .Permissions = {
                {"user5", {"ydb.generic.read", "ydb.generic.list", "ydb.generic.write", "ydb.generic.use_legacy"}},
                {"user6", {"ydb.generic.full"}}
            }},
            {.Path = "/Root/TestDdl1", .Permissions = {{"user2", {"ydb.tables.read", "ydb.tables.modify"}}}},
            {.Path = "/Root/TestDdl2", .Permissions = {}}
        });

        result = db.ExecuteQuery(R"(
            REVOKE ALL PRIVILEGES ON `/Root` FROM user6;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        checkPermissions(session, {
            {.Path = "/Root", .Permissions = {{"user5", {"ydb.generic.read", "ydb.generic.list", "ydb.generic.write", "ydb.generic.use_legacy"}}}},
            {.Path = "/Root/TestDdl1", .Permissions = {{"user2", {"ydb.tables.read", "ydb.tables.modify"}}}},
            {.Path = "/Root/TestDdl2", .Permissions = {}}
        });

        result = db.ExecuteQuery(R"(
            GRANT "ydb.generic.use", "ydb.generic.manage" ON `/Root` TO user7 WITH GRANT OPTION;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        checkPermissions(session, {
            {.Path = "/Root", .Permissions = {
                {"user5", {"ydb.generic.read", "ydb.generic.list", "ydb.generic.write", "ydb.generic.use_legacy"}},
                {"user7", {"ydb.generic.use", "ydb.generic.manage", "ydb.access.grant"}}
            }},
            {.Path = "/Root/TestDdl1", .Permissions = {{"user2", {"ydb.tables.read", "ydb.tables.modify"}}}},
            {.Path = "/Root/TestDdl2", .Permissions = {}}
        });

        result = db.ExecuteQuery(R"(
            REVOKE GRANT OPTION FOR USE, MANAGE ON `/Root` FROM user7;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        checkPermissions(session, {
            {.Path = "/Root", .Permissions = {{"user5", {"ydb.generic.read", "ydb.generic.list", "ydb.generic.write", "ydb.generic.use_legacy"}}}},
            {.Path = "/Root/TestDdl1", .Permissions = {{"user2", {"ydb.tables.read", "ydb.tables.modify"}}}},
            {.Path = "/Root/TestDdl2", .Permissions = {}}
        });

        result = db.ExecuteQuery(R"(
            GRANT USE LEGACY, FULL LEGACY, FULL, CREATE, DROP, GRANT,
                  SELECT ROW, UPDATE ROW, ERASE ROW, SELECT ATTRIBUTES,
                  MODIFY ATTRIBUTES, CREATE DIRECTORY, CREATE TABLE, CREATE QUEUE,
                  REMOVE SCHEMA, DESCRIBE SCHEMA, ALTER SCHEMA ON `/Root` TO user8;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        checkPermissions(session, {
            {.Path = "/Root", .Permissions = {
                {"user5", {"ydb.generic.read", "ydb.generic.list", "ydb.generic.write", "ydb.generic.use_legacy"}},
                {"user8", {"ydb.generic.use_legacy", "ydb.generic.full_legacy", "ydb.generic.full", "ydb.database.create",
                    "ydb.database.drop", "ydb.access.grant", "ydb.granular.select_row", "ydb.granular.update_row",
                    "ydb.granular.erase_row", "ydb.granular.read_attributes", "ydb.granular.write_attributes",
                    "ydb.granular.create_directory", "ydb.granular.create_table", "ydb.granular.create_queue",
                    "ydb.granular.remove_schema", "ydb.granular.describe_schema", "ydb.granular.alter_schema"}}
            }},
            {.Path = "/Root/TestDdl1", .Permissions = {{"user2", {"ydb.tables.read", "ydb.tables.modify"}}}},
            {.Path = "/Root/TestDdl2", .Permissions = {}}
        });

        result = db.ExecuteQuery(R"(
            REVOKE "ydb.granular.write_attributes", "ydb.granular.create_directory", "ydb.granular.create_table", "ydb.granular.create_queue",
                   "ydb.granular.select_row", "ydb.granular.update_row", "ydb.granular.erase_row", "ydb.granular.read_attributes",
                   "ydb.generic.use_legacy", "ydb.generic.full_legacy", "ydb.generic.full", "ydb.database.create", "ydb.database.drop", "ydb.access.grant",
                   "ydb.granular.remove_schema", "ydb.granular.describe_schema", "ydb.granular.alter_schema" ON `/Root` FROM user8;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        checkPermissions(session, {
            {.Path = "/Root", .Permissions = {{"user5", {"ydb.generic.read", "ydb.generic.list", "ydb.generic.write", "ydb.generic.use_legacy"}}}},
            {.Path = "/Root/TestDdl1", .Permissions = {{"user2", {"ydb.tables.read", "ydb.tables.modify"}}}},
            {.Path = "/Root/TestDdl2", .Permissions = {}}
        });

        result = db.ExecuteQuery(R"(
            REVOKE LIST, INSERT ON `/Root` FROM user9, user4, user5;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        checkPermissions(session, {
            {.Path = "/Root", .Permissions = {{"user5", {"ydb.generic.read", "ydb.generic.use_legacy"}}}},
            {.Path = "/Root/TestDdl1", .Permissions = {{"user2", {"ydb.tables.read", "ydb.tables.modify"}}}},
            {.Path = "/Root/TestDdl2", .Permissions = {}}
        });

        result = db.ExecuteQuery(R"(
            REVOKE ALL ON `/Root`, `/Root/TestDdl1` FROM user9, user4, user5, user2;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        checkPermissions(session, {
            {.Path = "/Root", .Permissions = {}},
            {.Path = "/Root/TestDdl1", .Permissions = {}},
            {.Path = "/Root/TestDdl2", .Permissions = {}}
        });
    }

    Y_UNIT_TEST(DdlSecret) {
        NKikimrConfig::TAppConfig appConfig;
        auto setting = NKikimrKqp::TKqpSetting();
        auto serverSettings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetKqpSettings({setting});

        TKikimrRunner kikimr(serverSettings);
        auto db = kikimr.GetQueryClient();

        enum EEx {
            Empty,
            IfExists,
            IfNotExists,
        };

        auto executeSql = [&](const TString& sql, bool expectSuccess) {
            Cerr << "Execute SQL:\n" << sql << Endl;

            auto result = db.ExecuteQuery(sql, TTxControl::NoTx()).ExtractValueSync();
            if (expectSuccess) {
                UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            } else {
                UNIT_ASSERT_VALUES_UNEQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            }
            UNIT_ASSERT(result.GetResultSets().empty());
        };

        auto checkCreate = [&](bool expectSuccess, EEx exMode, int nameSuffix) {
            UNIT_ASSERT_UNEQUAL(exMode, EEx::IfExists);
            const TString ifNotExistsStatement = exMode == EEx::IfNotExists ? "IF NOT EXISTS" : "";
            const TString sql = fmt::format(R"sql(
                CREATE OBJECT {if_not_exists} my_secret_{name_suffix} (TYPE SECRET) WITH (value="qwerty");
                )sql",
                "if_not_exists"_a = ifNotExistsStatement,
                "name_suffix"_a = nameSuffix
            );

            executeSql(sql, expectSuccess);
        };

        auto checkAlter = [&](bool expectSuccess, int nameSuffix) {
            const TString sql = fmt::format(R"sql(
                ALTER OBJECT my_secret_{name_suffix} (TYPE SECRET) SET value = "abcde";
                )sql",
                "name_suffix"_a = nameSuffix
            );

            executeSql(sql, expectSuccess);
        };

        auto checkUpsert = [&](bool expectSuccess, int nameSuffix) {
            const TString sql = fmt::format(R"sql(
                UPSERT OBJECT my_secret_{name_suffix} (TYPE SECRET) WITH value = "edcba";
                )sql",
                "name_suffix"_a = nameSuffix
            );

            executeSql(sql, expectSuccess);
        };

        auto checkDrop = [&](bool expectSuccess, EEx exMode, int nameSuffix) {
            UNIT_ASSERT_UNEQUAL(exMode, EEx::IfNotExists);
            const TString ifExistsStatement = exMode == EEx::IfExists ? "IF EXISTS" : "";
            const TString sql = fmt::format(R"sql(
                DROP OBJECT {if_exists} my_secret_{name_suffix} (TYPE SECRET);
                )sql",
                "if_exists"_a = ifExistsStatement,
                "name_suffix"_a = nameSuffix
            );

            executeSql(sql, expectSuccess);
        };

        checkCreate(true, EEx::Empty, 0);
        checkCreate(false, EEx::Empty, 0);
        checkAlter(true, 0);
        checkAlter(false, 2); // not exists
        checkDrop(true, EEx::Empty, 0);
        checkDrop(true, EEx::Empty, 0); // we don't check object existence

        checkCreate(true, EEx::IfNotExists, 1);
        checkCreate(true, EEx::IfNotExists, 1);
        checkDrop(true, EEx::IfExists, 1);
        checkDrop(true, EEx::IfExists, 1);

        checkUpsert(true, 2);
        checkCreate(false, EEx::Empty, 2); // already exists
        checkUpsert(true, 2);
    }

    Y_UNIT_TEST(ShowCreateTable) {
        auto serverSettings = TKikimrSettings().SetEnableShowCreate(true);

        TKikimrRunner kikimr(serverSettings);
        auto db = kikimr.GetQueryClient();
        auto session = db.GetSession().GetValueSync().GetSession();

        {
            auto result = session.ExecuteQuery(R"(
                CREATE TABLE test_show_create (
                    Key Uint32,
                    Value Uint32,
                    PRIMARY KEY (Key)
                );
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        }

        {
            auto result = session.ExecuteQuery(R"(
                SHOW CREATE TABLE `/Root/test_show_create`;
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

            UNIT_ASSERT(!result.GetResultSets().empty());

            CompareYson(R"([
                [["CREATE TABLE `test_show_create` (\n    `Key` Uint32,\n    `Value` Uint32,\n    PRIMARY KEY (`Key`)\n);\n"]; ["test_show_create"];["Table"];];
            ])", FormatResultSetYson(result.GetResultSet(0)));
        }
    }

    Y_UNIT_TEST(ShowCreateTableDisable) {
        auto serverSettings = TKikimrSettings().SetEnableShowCreate(false);

        TKikimrRunner kikimr(serverSettings);
        auto db = kikimr.GetQueryClient();
        auto session = db.GetSession().GetValueSync().GetSession();

        {
            auto result = session.ExecuteQuery(R"(
                CREATE TABLE test_show_create (
                    Key Uint32,
                    Value Uint32,
                    PRIMARY KEY (Key)
                );
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        }

        {
            auto result = session.ExecuteQuery(R"(
                SHOW CREATE TABLE `/Root/test_show_create`;
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT(!result.IsSuccess());

            UNIT_ASSERT_VALUES_EQUAL("<main>: Error: Type annotation, code: 1030\n    <main>:2:35: Error: At function: KiReadTable!\n        <main>:2:35: Error: SHOW CREATE statement is not supported\n",
                result.GetIssues().ToString());
        }
    }

    Y_UNIT_TEST(ShowCreateSysView) {
        auto serverSettings = TKikimrSettings().SetEnableShowCreate(true

        );

        TKikimrRunner kikimr(serverSettings);
        auto db = kikimr.GetQueryClient();
        auto session = db.GetSession().GetValueSync().GetSession();

        {
            auto result = session.ExecuteQuery(R"(
                CREATE TABLE test_show_create (
                    Key Uint32,
                    Value Uint32,
                    PRIMARY KEY (Key)
                );
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        }

        {
            auto result = session.ExecuteQuery(R"(
                SELECT * FROM `.sys/show_create`;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT(!result.IsSuccess());

            UNIT_ASSERT_VALUES_EQUAL("<main>: Error: Type annotation, code: 1030\n    <main>:2:17: Error: At function: KiReadTable!\n        <main>:2:17: Error: Cannot find table 'db.[/Root/.sys/show_create]' because it does not exist or you do not have access permissions. Please check correctness of table path and user permissions., code: 2003\n",
                result.GetIssues().ToString());
        }

        {
            auto result = session.ExecuteQuery(R"(
                SELECT * FROM `.sys/show_create` WHERE Path = "/Root/test_show_create" and PathType = "Table";
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT(!result.IsSuccess());

            UNIT_ASSERT_VALUES_EQUAL("<main>: Error: Type annotation, code: 1030\n    <main>:2:17: Error: At function: KiReadTable!\n        <main>:2:17: Error: Cannot find table 'db.[/Root/.sys/show_create]' because it does not exist or you do not have access permissions. Please check correctness of table path and user permissions., code: 2003\n",
                result.GetIssues().ToString());
        }

        {
            auto tableClient = kikimr.GetTableClient();
            auto tableSession = tableClient.CreateSession().GetValueSync().GetSession();
            NYdb::NTable::TDescribeTableResult describe = tableSession.DescribeTable("/Root/.sys/show_create").GetValueSync();
            UNIT_ASSERT(!describe.IsSuccess());
        }
    }

    Y_UNIT_TEST(ShowCreateTableNotSuccess) {
        auto serverSettings = TKikimrSettings().SetEnableShowCreate(true);

        TKikimrRunner kikimr(serverSettings);
        auto db = kikimr.GetQueryClient();
        auto session = db.GetSession().GetValueSync().GetSession();

        {
            auto result = session.ExecuteQuery(R"(
                SHOW CREATE TABLE test_show_create;
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT(!result.IsSuccess());

            UNIT_ASSERT_VALUES_EQUAL("<main>: Error: Type annotation, code: 1030\n    <main>:2:35: Error: At function: KiReadTable!\n        <main>:2:35: Error: Cannot find table 'db.[/Root/test_show_create]' because it does not exist or you do not have access permissions. Please check correctness of table path and user permissions., code: 2003\n",
                result.GetIssues().ToString());
        }

        {
            auto result = session.ExecuteQuery(R"(
                SHOW CREATE TABLE `/Root/.sys/show_create`;
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT(!result.IsSuccess());

            UNIT_ASSERT_VALUES_EQUAL("<main>: Error: Type annotation, code: 1030\n    <main>:2:35: Error: At function: KiReadTable!\n        <main>:2:35: Error: Cannot find table 'db.[/Root/.sys/show_create]' because it does not exist or you do not have access permissions. Please check correctness of table path and user permissions., code: 2003\n",
                result.GetIssues().ToString());
        }
    }

    Y_UNIT_TEST(ShowCreateTableOnView) {
        auto serverSettings = TKikimrSettings().SetEnableShowCreate(true);

        TKikimrRunner kikimr(serverSettings);
        auto db = kikimr.GetQueryClient();
        auto session = db.GetSession().GetValueSync().GetSession();

        {
            // note: KeyValue is one of the sample tables created in KikimrRunner
            auto result = session.ExecuteQuery(R"(
                CREATE VIEW test_view WITH security_invoker = TRUE AS
                    SELECT * FROM KeyValue;
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
        }

        {
            auto result = session.ExecuteQuery(R"(
                SHOW CREATE TABLE test_view;
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::BAD_REQUEST, result.GetIssues().ToString());
            UNIT_ASSERT_STRING_CONTAINS(result.GetIssues().ToString(), "Path type mismatch, expected: Table");
        }
    }

    Y_UNIT_TEST(ShowCreateView) {
        auto serverSettings = TKikimrSettings().SetEnableShowCreate(true);

        TKikimrRunner kikimr(serverSettings);
        auto db = kikimr.GetQueryClient();
        auto session = db.GetSession().GetValueSync().GetSession();

        {
            // note: KeyValue is one of the sample tables created in KikimrRunner
            auto result = session.ExecuteQuery(R"(
                CREATE VIEW test_view WITH security_invoker = TRUE AS
                    SELECT * FROM KeyValue;
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
        }

        {
            auto result = session.ExecuteQuery(R"(
                SHOW CREATE VIEW test_view;
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
            UNIT_ASSERT(!result.GetResultSets().empty());

            CompareYson(R"([
                [["CREATE VIEW `test_view` WITH (security_invoker = TRUE) AS\nSELECT\n    *\nFROM\n    KeyValue\n;\n"];["test_view"];["View"]];
            ])", FormatResultSetYson(result.GetResultSet(0)));
        }
    }

    Y_UNIT_TEST(ShowCreateViewOnTable) {
        auto serverSettings = TKikimrSettings().SetEnableShowCreate(true);

        TKikimrRunner kikimr(serverSettings);
        auto db = kikimr.GetQueryClient();
        auto session = db.GetSession().GetValueSync().GetSession();

        {
            // note: KeyValue is one of the sample tables created in KikimrRunner
            auto result = session.ExecuteQuery(R"(
                SHOW CREATE VIEW KeyValue;
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::BAD_REQUEST, result.GetIssues().ToString());
            UNIT_ASSERT_STRING_CONTAINS(result.GetIssues().ToString(), "Path type mismatch, expected: View");
        }
    }

    Y_UNIT_TEST(DdlCache) {
        NKikimrConfig::TAppConfig appConfig;
        auto setting = NKikimrKqp::TKqpSetting();
        auto serverSettings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetKqpSettings({setting});

        TKikimrRunner kikimr(serverSettings);
        auto db = kikimr.GetQueryClient();

        auto settings = TExecuteQuerySettings()
            .StatsMode(EStatsMode::Basic);

        auto result = db.ExecuteQuery(R"(
            CREATE TABLE TestDdl (
                Key Uint64,
                Value String,
                PRIMARY KEY (Key)
            );
        )", TTxControl::NoTx(), settings).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

        auto stats = NYdb::TProtoAccessor::GetProto(*result.GetStats());
        UNIT_ASSERT_VALUES_EQUAL(stats.compilation().from_cache(), false);

        {
            // TODO: Switch to query service.
            auto session = kikimr.GetTableClient().CreateSession().GetValueSync().GetSession();

            UNIT_ASSERT(session.ExecuteSchemeQuery(R"(
                DROP TABLE TestDdl;
            )").GetValueSync().IsSuccess());
        }

        result = db.ExecuteQuery(R"(
            CREATE TABLE TestDdl (
                Key Uint64,
                Value String,
                PRIMARY KEY (Key)
            );
        )", TTxControl::NoTx(), settings).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

        stats = NYdb::TProtoAccessor::GetProto(*result.GetStats());
        UNIT_ASSERT_VALUES_EQUAL(stats.compilation().from_cache(), false);
    }

    Y_UNIT_TEST(DdlTx) {
        NKikimrConfig::TAppConfig appConfig;
        auto setting = NKikimrKqp::TKqpSetting();
        auto serverSettings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetKqpSettings({setting});

        TKikimrRunner kikimr(serverSettings);
        auto db = kikimr.GetQueryClient();

        auto result = db.ExecuteQuery(R"(
            CREATE TABLE TestDdl (
                Key Uint64,
                Value String,
                PRIMARY KEY (Key)
            );
        )", TTxControl::BeginTx().CommitTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::PRECONDITION_FAILED, result.GetIssues().ToString());
    }

    Y_UNIT_TEST(DdlExecuteScript) {
        NKikimrConfig::TAppConfig appConfig;
        auto setting = NKikimrKqp::TKqpSetting();
        auto serverSettings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetKqpSettings({setting})
            .SetEnableScriptExecutionOperations(true);

        TKikimrRunner kikimr(serverSettings);
        auto db = kikimr.GetQueryClient();

        const TString sql = R"sql(
            CREATE TABLE TestDdlExecuteScript (
                Key Uint64,
                Value String,
                PRIMARY KEY (Key)
            );
        )sql";

        auto scriptExecutionOperation = db.ExecuteScript(sql).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(scriptExecutionOperation.Status().GetStatus(), EStatus::SUCCESS, scriptExecutionOperation.Status().GetIssues().ToString());
        UNIT_ASSERT(!scriptExecutionOperation.Metadata().ExecutionId.empty());

        NYdb::NOperation::TOperationClient client(kikimr.GetDriver());
        TMaybe<NYdb::NQuery::TScriptExecutionOperation> readyOp;
        while (true) {
            auto op = client.Get<NYdb::NQuery::TScriptExecutionOperation>(scriptExecutionOperation.Id()).GetValueSync();
            if (op.Ready()) {
                readyOp = std::move(op);
                break;
            }
            UNIT_ASSERT_C(op.Status().IsSuccess(), TStringBuilder() << op.Status().GetStatus() << ":" << op.Status().GetIssues().ToString());
            Sleep(TDuration::MilliSeconds(10));
        }
        UNIT_ASSERT_C(readyOp->Status().IsSuccess(), readyOp->Status().GetIssues().ToString());
        UNIT_ASSERT_EQUAL_C(readyOp->Metadata().ExecStatus, EExecStatus::Completed, readyOp->Status().GetIssues().ToString());
    }

    Y_UNIT_TEST(DdlMixedDml) {
        NKikimrConfig::TAppConfig appConfig;
        auto setting = NKikimrKqp::TKqpSetting();
        auto serverSettings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetKqpSettings({setting});

        TKikimrRunner kikimr(serverSettings);
        auto db = kikimr.GetQueryClient();

        auto result = db.ExecuteQuery(R"(
            CREATE TABLE TestDdl (
                Key Uint64,
                Value String,
                PRIMARY KEY (Key)
            );

            UPSERT INTO TestDdl (Key, Value) VALUES (1, "One");
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::GENERIC_ERROR, result.GetIssues().ToString());
        UNIT_ASSERT(HasIssue(result.GetIssues(), NYql::TIssuesIds::KIKIMR_MIXED_SCHEME_DATA_TX));
    }

    Y_UNIT_TEST(DmlNoTx) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();

        auto result = db.ExecuteQuery(R"(
            UPSERT INTO KeyValue (Key, Value) VALUES (3, "Three");
            SELECT * FROM KeyValue;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
    }

    Y_UNIT_TEST(Tcl) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();

        auto result = db.ExecuteQuery(R"(
            SELECT 1;
            COMMIT;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::GENERIC_ERROR, result.GetIssues().ToString());
        UNIT_ASSERT(HasIssue(result.GetIssues(), NYql::TIssuesIds::KIKIMR_BAD_OPERATION));

        result = db.ExecuteQuery(R"(
            SELECT 1;
            ROLLBACK;
        )", TTxControl::NoTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::GENERIC_ERROR, result.GetIssues().ToString());
        UNIT_ASSERT(HasIssue(result.GetIssues(), NYql::TIssuesIds::KIKIMR_BAD_OPERATION));
    }

    Y_UNIT_TEST(MaterializeTxResults) {
        auto kikimr = DefaultKikimrRunner();
        auto db = kikimr.GetQueryClient();

        auto result = db.ExecuteQuery(R"(
            DELETE FROM KeyValue;
        )", TTxControl::BeginTx().CommitTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

        result = db.ExecuteQuery(R"(
            SELECT * FROM KeyValue;
        )", TTxControl::BeginTx().CommitTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

        CompareYson(R"([])", FormatResultSetYson(result.GetResultSet(0)));
    }

    Y_UNIT_TEST(CloseConnection) {
        auto kikimr = DefaultKikimrRunner();

        NKqp::TKqpCounters counters(kikimr.GetTestServer().GetRuntime()->GetAppData().Counters);

        int maxTimeoutMs = 100;

        for (int i = 1; i < maxTimeoutMs; i++) {
            auto it = kikimr.GetQueryClient().StreamExecuteQuery(R"(
                SELECT * FROM `/Root/EightShard` WHERE Text = "Value1" ORDER BY Key;
            )", TTxControl::BeginTx().CommitTx(), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(i))).GetValueSync();

            if (it.IsSuccess()) {
                try {
                    for (;;) {
                        auto streamPart = it.ReadNext().GetValueSync();
                        if (!streamPart.IsSuccess()) {
                            break;
                        }
                    }
                } catch (const TStreamReadError& ex) {
                    UNIT_ASSERT_VALUES_EQUAL(ex.Status, NYdb::EStatus::CLIENT_DEADLINE_EXCEEDED);
                } catch (const std::exception& ex) {
                    UNIT_ASSERT_C(false, "unknown exception during the test");
                }
            } else {
                UNIT_ASSERT_VALUES_EQUAL(it.GetStatus(), NYdb::EStatus::CLIENT_DEADLINE_EXCEEDED);
            }
        }

        WaitForZeroSessions(counters);
        WaitForZeroReadIterators(kikimr.GetTestServer(), "/Root/EightShard");

        for (const auto& service: kikimr.GetTestServer().GetGRpcServer().GetServices()) {
            UNIT_ASSERT_VALUES_EQUAL(service->RequestsInProgress(), 0);
            UNIT_ASSERT(!service->IsUnsafeToShutdown());
        }
    }

    Y_UNIT_TEST(DdlWithExplicitTransaction) {
        NKikimrConfig::TAppConfig appConfig;
        appConfig.MutableTableServiceConfig()->SetEnableAstCache(true);
        appConfig.MutableTableServiceConfig()->SetEnablePerStatementQueryExecution(true);
        auto setting = NKikimrKqp::TKqpSetting();
        auto serverSettings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetKqpSettings({setting});

        TKikimrRunner kikimr(serverSettings);
        auto db = kikimr.GetQueryClient();

        {
            // DDl with explicit transaction
            auto result = db.ExecuteQuery(R"(
                CREATE TABLE TestDdlDml1 (
                    Key Uint64,
                    PRIMARY KEY (Key)
                );
            )", TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::PRECONDITION_FAILED, result.GetIssues().ToString());
            UNIT_ASSERT(result.GetIssues().ToOneLineString().contains("Scheme operations cannot be executed inside transaction"));
        }

        {
            // DDl with explicit transaction
            auto result = db.ExecuteQuery(R"(
                CREATE TABLE TestDdlDml1 (
                    Key Uint64,
                    PRIMARY KEY (Key)
                );
                CREATE TABLE TestDdlDml2 (
                    Key Uint64,
                    PRIMARY KEY (Key)
                );
            )", TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::PRECONDITION_FAILED, result.GetIssues().ToString());
            UNIT_ASSERT(result.GetIssues().ToOneLineString().contains("Scheme operations cannot be executed inside transaction"));
        }

        {
            // DDl with implicit transaction
            auto result = db.ExecuteQuery(R"(
                CREATE TABLE TestDdlDml1 (
                    Key Uint64,
                    PRIMARY KEY (Key)
                );
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        }

        {
            // DDl + DML with explicit transaction
            auto result = db.ExecuteQuery(R"(
                SELECT * FROM TestDdlDml1;
                CREATE TABLE TestDdlDml2 (
                    Key Uint64,
                    PRIMARY KEY (Key)
                );
                SELECT * FROM TestDdlDml2;
            )", TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::GENERIC_ERROR, result.GetIssues().ToString());
            UNIT_ASSERT(result.GetIssues().ToOneLineString().contains("Queries with mixed data and scheme operations are not supported."));
        }
    }

    Y_UNIT_TEST(Ddl_Dml) {
        NKikimrConfig::TAppConfig appConfig;
        appConfig.MutableTableServiceConfig()->SetEnableAstCache(true);
        appConfig.MutableTableServiceConfig()->SetEnablePerStatementQueryExecution(true);
        auto setting = NKikimrKqp::TKqpSetting();
        auto serverSettings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetKqpSettings({setting});

        TKikimrRunner kikimr(serverSettings);
        auto db = kikimr.GetQueryClient();

        {
            // DDl + DML with implicit transaction
            auto result = db.ExecuteQuery(R"(
                CREATE TABLE TestDdlDml2 (
                    Key Uint64,
                    Value1 String,
                    Value2 String,
                    PRIMARY KEY (Key)
                );
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

            result = db.ExecuteQuery(R"(
                UPSERT INTO TestDdlDml2 (Key, Value1, Value2) VALUES (1, "1", "1");
                SELECT * FROM TestDdlDml2;
                ALTER TABLE TestDdlDml2 DROP COLUMN Value2;
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::ABORTED, result.GetIssues().ToString());

            result = db.ExecuteQuery(R"(
                UPSERT INTO TestDdlDml2 (Key, Value1) VALUES (1, "1");
                SELECT * FROM TestDdlDml2;
                UPSERT INTO TestDdlDml2 (Key, Value1) VALUES (2, "2");
                SELECT * FROM TestDdlDml2;
                CREATE TABLE TestDdlDml33 (
                    Key Uint64,
                    PRIMARY KEY (Key)
                );
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 2);
            CompareYson(R"([[[1u];["1"]]])", FormatResultSetYson(result.GetResultSet(0)));
            CompareYson(R"([[[1u];["1"]];[[2u];["2"]]])", FormatResultSetYson(result.GetResultSet(1)));
            UNIT_ASSERT_EQUAL_C(result.GetIssues().Size(), 0, result.GetIssues().ToString());

            result = db.ExecuteQuery(R"(
                SELECT * FROM TestDdlDml2;
            )", TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 1);
            CompareYson(R"([[[1u];["1"]];[[2u];["2"]]])", FormatResultSetYson(result.GetResultSet(0)));

            result = db.ExecuteQuery(R"(
                SELECT * FROM TestDdlDml33;
            )", TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 1);
            CompareYson(R"([])", FormatResultSetYson(result.GetResultSet(0)));

            result = db.ExecuteQuery(R"(
                CREATE TABLE TestDdlDml4 (
                    Key Uint64,
                    Value1 String,
                    Value2 String,
                    PRIMARY KEY (Key)
                );
                UPSERT INTO TestDdlDml4 (Key, Value1, Value2) VALUES (1, "1", "2");
                SELECT * FROM TestDdlDml4;
                UPSERT INTO TestDdlDml4 (Key, Value1) VALUES (2, "2");
                SELECT * FROM TestDdlDml5;
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SCHEME_ERROR, result.GetIssues().ToString());
            UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 0);

            result = db.ExecuteQuery(R"(
                SELECT * FROM TestDdlDml4;
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 1);
            CompareYson(R"([])", FormatResultSetYson(result.GetResultSet(0)));
        }

        {
            // Base test with ddl and dml statements
            auto result = db.ExecuteQuery(R"(
                DECLARE $name AS Text;
                $a = (SELECT * FROM TestDdl1);
                CREATE TABLE TestDdl1 (
                    Key Uint64,
                    Value String,
                    PRIMARY KEY (Key)
                );
                UPSERT INTO TestDdl1 (Key, Value) VALUES (1, "One");
                CREATE TABLE TestDdl2 (
                    Key Uint64,
                    Value String,
                    PRIMARY KEY (Key)
                );
                UPSERT INTO TestDdl1 (Key, Value) VALUES (2, "Two");
                SELECT * FROM $a;
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 1);
            CompareYson(R"([[[1u];["One"]];[[2u];["Two"]]])", FormatResultSetYson(result.GetResultSet(0)));
            UNIT_ASSERT_EQUAL_C(result.GetIssues().Size(), 0, result.GetIssues().ToString());

            result = db.ExecuteQuery(R"(
                UPSERT INTO TestDdl1 (Key, Value) VALUES (3, "Three");
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            UNIT_ASSERT_EQUAL_C(result.GetIssues().Size(), 0, result.GetIssues().ToString());

            result = db.ExecuteQuery(R"(
                SELECT * FROM TestDdl1;
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 1);
            CompareYson(R"([[[1u];["One"]];[[2u];["Two"]];[[3u];["Three"]]])", FormatResultSetYson(result.GetResultSet(0)));
            UNIT_ASSERT_EQUAL_C(result.GetIssues().Size(), 0, result.GetIssues().ToString());

            result = db.ExecuteQuery(R"(
                CREATE TABLE TestDdl1 (
                    Key Uint64,
                    Value String,
                    PRIMARY KEY (Key)
                );
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::GENERIC_ERROR, result.GetIssues().ToString());
            UNIT_ASSERT(result.GetIssues().ToOneLineString().contains("Check failed: path: '/Root/TestDdl1', error: path exist"));

            result = db.ExecuteQuery(R"(
                CREATE TABLE TestDdl2 (
                    Key Uint64,
                    Value String,
                    PRIMARY KEY (Key)
                );
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::GENERIC_ERROR, result.GetIssues().ToString());
            UNIT_ASSERT(result.GetIssues().ToOneLineString().contains("Check failed: path: '/Root/TestDdl2', error: path exist"));

            result = db.ExecuteQuery(R"(
                UPSERT INTO TestDdl2 SELECT * FROM TestDdl1;
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 0);
            UNIT_ASSERT_EQUAL_C(result.GetIssues().Size(), 0, result.GetIssues().ToString());
        }

        {
            // Test with query with error
            auto result = db.ExecuteQuery(R"(
                SELECT * FROM TestDdl2;
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 1);
            CompareYson(R"([[[1u];["One"]];[[2u];["Two"]];[[3u];["Three"]]])", FormatResultSetYson(result.GetResultSet(0)));
            UNIT_ASSERT_EQUAL_C(result.GetIssues().Size(), 0, result.GetIssues().ToString());

            result = db.ExecuteQuery(R"(
                UPSERT INTO TestDdl2 (Key, Value) VALUES (4, "Four");
                CREATE TABLE TestDdl3 (
                    Key Uint64,
                    Value String,
                    PRIMARY KEY (Key)
                );
                UPSERT INTO TestDdl2 (Key, Value) VALUES (5, "Five");
                CREATE TABLE TestDdl2 (
                    Key Uint64,
                    Value String,
                    PRIMARY KEY (Key)
                );
                CREATE TABLE TestDdl4 (
                    Key Uint64,
                    Value String,
                    PRIMARY KEY (Key)
                );
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::GENERIC_ERROR, result.GetIssues().ToString());
            UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 0);
            UNIT_ASSERT(result.GetIssues().ToOneLineString().contains("Check failed: path: '/Root/TestDdl2', error: path exist"));

            result = db.ExecuteQuery(R"(
                SELECT * FROM TestDdl2;
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 1);
            CompareYson(R"([[[1u];["One"]];[[2u];["Two"]];[[3u];["Three"]]])", FormatResultSetYson(result.GetResultSet(0)));
            UNIT_ASSERT_EQUAL_C(result.GetIssues().Size(), 0, result.GetIssues().ToString());

            result = db.ExecuteQuery(R"(
                SELECT * FROM TestDdl3;
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 1);
            CompareYson(R"([])", FormatResultSetYson(result.GetResultSet(0)));
            UNIT_ASSERT_EQUAL_C(result.GetIssues().Size(), 0, result.GetIssues().ToString());

            result = db.ExecuteQuery(R"(
                SELECT * FROM TestDdl4;
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SCHEME_ERROR, result.GetIssues().ToString());
            UNIT_ASSERT(result.GetIssues().ToOneLineString().contains("Cannot find table 'db.[/Root/TestDdl4]'"));
        }

        {
            // Check result sets
            auto result = db.ExecuteQuery(R"(
                $a = (SELECT * FROM TestDdl1);
                SELECT * FROM $a;
                UPSERT INTO TestDdl1 (Key, Value) VALUES (4, "Four");
                SELECT * FROM $a;
                CREATE TABLE TestDdl4 (
                    Key Uint64,
                    Value Uint64,
                    PRIMARY KEY (Key)
                );
                UPSERT INTO TestDdl4 (Key, Value) VALUES (1, 1);
                SELECT * FROM TestDdl4;
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 3);
            CompareYson(R"([[[1u];["One"]];[[2u];["Two"]];[[3u];["Three"]]])", FormatResultSetYson(result.GetResultSet(0)));
            CompareYson(R"([[[1u];["One"]];[[2u];["Two"]];[[3u];["Three"]];[[4u];["Four"]]])", FormatResultSetYson(result.GetResultSet(1)));
            CompareYson(R"([[[1u];[1u]]])", FormatResultSetYson(result.GetResultSet(2)));
            UNIT_ASSERT_EQUAL_C(result.GetIssues().Size(), 0, result.GetIssues().ToString());

            result = db.ExecuteQuery(R"(
                UPSERT INTO TestDdl2 SELECT * FROM TestDdl1;
            )", TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 0);
            UNIT_ASSERT_EQUAL_C(result.GetIssues().Size(), 0, result.GetIssues().ToString());
        }

        {
            // Check EVALUATE FOR
            auto result = db.ExecuteQuery(R"(
                EVALUATE FOR $i IN AsList(1, 2, 3) DO BEGIN
                    SELECT $i;
                    SELECT $i;
                END DO;
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        }

        {
            // Check parser errors
            auto result = db.ExecuteQuery(R"(
                UPSERT INTO TestDdl4 (Key, Value) VALUES (2, 2);
                SELECT * FROM $a;
                UPSERT INTO TestDdl4 (Key, Value) VALUES (3, 3);
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::GENERIC_ERROR, result.GetIssues().ToString());
            UNIT_ASSERT(result.GetIssues().ToOneLineString().contains("Unknown name: $a"));

            result = db.ExecuteQuery(R"(
                SELECT * FROM TestDdl4;
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 1);
            CompareYson(R"([[[1u];[1u]]])", FormatResultSetYson(result.GetResultSet(0)));

            result = db.ExecuteQuery(R"(
                UPSERT INTO TestDdl4 (Key, Value) VALUES (2, 2);
                UPSERT INTO TestDdl4 (Key, Value) VALUES (3, "3");
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::GENERIC_ERROR, result.GetIssues().ToString());
            UNIT_ASSERT(result.GetIssues().ToOneLineString().contains("Error: Failed to convert 'Value': String to Optional<Uint64>"));

            result = db.ExecuteQuery(R"(
                SELECT * FROM TestDdl4;
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 1);
            CompareYson(R"([[[1u];[1u]]])", FormatResultSetYson(result.GetResultSet(0)));

            result = db.ExecuteQuery(R"(
                CREATE TABLE TestDdl5 (
                    Key Uint64,
                    Value Uint64,
                    PRIMARY KEY (Key)
                );
                UPSERT INTO TestDdl5 (Key, Value) VALUES (1, 1);
                UPSERT INTO TestDdl5 (Key, Value) VALUES (3, "3");
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::GENERIC_ERROR, result.GetIssues().ToString());
            UNIT_ASSERT(result.GetIssues().ToOneLineString().contains("Error: Failed to convert 'Value': String to Optional<Uint64>"));

            result = db.ExecuteQuery(R"(
                SELECT * FROM TestDdl5;
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 1);
            CompareYson(R"([])", FormatResultSetYson(result.GetResultSet(0)));
        }
    }

    Y_UNIT_TEST(CTASWithoutPerStatement) {
        NKikimrConfig::TAppConfig appConfig;
        appConfig.MutableTableServiceConfig()->SetEnableOlapSink(true);
        appConfig.MutableTableServiceConfig()->SetEnableCreateTableAs(true);
        appConfig.MutableTableServiceConfig()->SetEnableAstCache(false);
        appConfig.MutableTableServiceConfig()->SetEnablePerStatementQueryExecution(false);
        auto setting = NKikimrKqp::TKqpSetting();
        auto serverSettings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetKqpSettings({setting})
            .SetWithSampleTables(false)
            .SetEnableTempTables(true);

        TKikimrRunner kikimr(serverSettings);
        auto db = kikimr.GetQueryClient();

        {
            auto result = db.ExecuteQuery(R"(
                CREATE TABLE Table1 (
                    PRIMARY KEY (Key)
                ) AS SELECT 1u AS Key, "1" AS Value1, "1" AS Value2;
                CREATE TABLE Table2 (
                    PRIMARY KEY (Key)
                ) AS SELECT 2u AS Key, "2" AS Value1, "2" AS Value2;
                )", TTxControl::NoTx(), TExecuteQuerySettings()).ExtractValueSync();

            UNIT_ASSERT(!result.IsSuccess());
            UNIT_ASSERT_C(
                result.GetIssues().ToString().contains("Several CTAS statement can't be used without per-statement mode."),
                result.GetIssues().ToString());
        }

        {
            auto result = db.ExecuteQuery(R"(
                CREATE TABLE Table2 (
                    PRIMARY KEY (Key)
                ) AS SELECT 2u AS Key, "2" AS Value1, "2" AS Value2;
                SELECT * FROM Table1 ORDER BY Key;
                )", TTxControl::NoTx(), TExecuteQuerySettings()).ExtractValueSync();

            UNIT_ASSERT(!result.IsSuccess());
            UNIT_ASSERT_C(
                result.GetIssues().ToString().contains("CTAS statement can't be used with other statements without per-statement mode."),
                result.GetIssues().ToString());
        }

        {
            auto result = db.ExecuteQuery(R"(
            SELECT * FROM Table1 ORDER BY Key;
                CREATE TABLE Table2 (
                    PRIMARY KEY (Key)
                ) AS SELECT 2u AS Key, "2" AS Value1, "2" AS Value2;
                )", TTxControl::NoTx(), TExecuteQuerySettings()).ExtractValueSync();

            UNIT_ASSERT(!result.IsSuccess());
            UNIT_ASSERT_C(
                result.GetIssues().ToString().contains("CTAS statement can't be used with other statements without per-statement mode."),
                result.GetIssues().ToString());
        }

        {
            auto result = db.ExecuteQuery(R"(
                CREATE TABLE Table1 (
                    PRIMARY KEY (Key)
                ) AS SELECT 1u AS Key, "1" AS Value1, "1" AS Value2;
                )", TTxControl::NoTx(), TExecuteQuerySettings()).ExtractValueSync();

            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        }

        {
            auto result = db.ExecuteQuery(R"(
                SELECT * FROM Table1 ORDER BY Key;
            )", TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 1);
            CompareYson(R"([[1u;"1";"1"]])", FormatResultSetYson(result.GetResultSet(0)));
        }
    }

    Y_UNIT_TEST_TWIN(SeveralCTAS, UseSink) {
        NKikimrConfig::TAppConfig appConfig;
        appConfig.MutableTableServiceConfig()->SetEnableAstCache(true);
        appConfig.MutableTableServiceConfig()->SetEnableOltpSink(UseSink);
        appConfig.MutableTableServiceConfig()->SetEnableOlapSink(true);
        appConfig.MutableTableServiceConfig()->SetEnableCreateTableAs(true);
        appConfig.MutableTableServiceConfig()->SetEnablePerStatementQueryExecution(true);
        auto setting = NKikimrKqp::TKqpSetting();
        auto serverSettings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetKqpSettings({setting})
            .SetWithSampleTables(false)
            .SetEnableTempTables(true);

        TKikimrRunner kikimr(serverSettings);
        auto db = kikimr.GetQueryClient();

        {
            auto result = db.ExecuteQuery(R"(
                CREATE TABLE Table1 (
                    PRIMARY KEY (Key)
                ) AS SELECT 1u AS Key, "1" AS Value1, "1" AS Value2;
                CREATE TABLE Table2 (
                    PRIMARY KEY (Key)
                ) AS SELECT 2u AS Key, "2" AS Value1, "2" AS Value2;
                CREATE TABLE Table3 (
                    PRIMARY KEY (Key)
                ) AS SELECT * FROM Table2 UNION ALL SELECT * FROM Table1;
            )", TTxControl::NoTx(), TExecuteQuerySettings()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
        }

        {
            auto result = db.ExecuteQuery(R"(
                SELECT * FROM Table1 ORDER BY Key;
                SELECT * FROM Table2 ORDER BY Key;
                SELECT * FROM Table3 ORDER BY Key;
            )", TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 3);
            CompareYson(R"([[1u;"1";"1"]])", FormatResultSetYson(result.GetResultSet(0)));
            CompareYson(R"([[2u;"2";"2"]])", FormatResultSetYson(result.GetResultSet(1)));
            // Also empty now(
            CompareYson(R"([])", FormatResultSetYson(result.GetResultSet(2)));
        }
    }

    Y_UNIT_TEST(CheckIsolationLevelFroPerStatementMode) {
        NKikimrConfig::TAppConfig appConfig;
        appConfig.MutableTableServiceConfig()->SetEnableAstCache(true);
        appConfig.MutableTableServiceConfig()->SetEnablePerStatementQueryExecution(true);
        auto setting = NKikimrKqp::TKqpSetting();
        auto serverSettings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetKqpSettings({setting});

        TKikimrRunner kikimr(serverSettings);
        auto db = kikimr.GetQueryClient();
        auto tableClient = kikimr.GetTableClient();
        auto session = tableClient.CreateSession().GetValueSync().GetSession();

        {
            // 1 ddl statement
            auto result = db.ExecuteQuery(R"(
                CREATE TABLE Test1 (
                    Key Uint64,
                    Value1 String,
                    Value2 String,
                    PRIMARY KEY (Key)
                );
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 0);
            UNIT_ASSERT_EQUAL_C(result.GetIssues().Size(), 0, result.GetIssues().ToString());

            NYdb::NTable::TDescribeTableResult describe = session.DescribeTable("/Root/Test1").GetValueSync();
            UNIT_ASSERT_EQUAL(describe.GetStatus(), EStatus::SUCCESS);
        }

        {
            // 2 ddl statements
            auto result = db.ExecuteQuery(R"(
                CREATE TABLE Test2 (
                    Key Uint64,
                    Value1 String,
                    Value2 String,
                    PRIMARY KEY (Key)
                );
                CREATE TABLE Test3 (
                    Key Uint64,
                    Value1 String,
                    Value2 String,
                    PRIMARY KEY (Key)
                );
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 0);
            UNIT_ASSERT_EQUAL_C(result.GetIssues().Size(), 0, result.GetIssues().ToString());

            NYdb::NTable::TDescribeTableResult describe1 = session.DescribeTable("/Root/Test2").GetValueSync();
            UNIT_ASSERT_EQUAL(describe1.GetStatus(), EStatus::SUCCESS);
            NYdb::NTable::TDescribeTableResult describe2 = session.DescribeTable("/Root/Test3").GetValueSync();
            UNIT_ASSERT_EQUAL(describe2.GetStatus(), EStatus::SUCCESS);
        }

        {
            // 1 dml statement
            auto result = db.ExecuteQuery(R"(
                SELECT * FROM Test1;
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 1);
            UNIT_ASSERT_EQUAL_C(result.GetIssues().Size(), 0, result.GetIssues().ToString());
        }

        {
            // 2 dml statements
            auto result = db.ExecuteQuery(R"(
                SELECT * FROM Test2;
                SELECT * FROM Test3;
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 2);
            UNIT_ASSERT_EQUAL_C(result.GetIssues().Size(), 0, result.GetIssues().ToString());
        }

        {
            // 1 ddl 1 dml statements
            auto result = db.ExecuteQuery(R"(
                CREATE TABLE Test4 (
                    Key Uint64,
                    Value1 String,
                    Value2 String,
                    PRIMARY KEY (Key)
                );
                SELECT * FROM Test4;
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 1);
            UNIT_ASSERT_EQUAL_C(result.GetIssues().Size(), 0, result.GetIssues().ToString());
            NYdb::NTable::TDescribeTableResult describe = session.DescribeTable("/Root/Test4").GetValueSync();
            UNIT_ASSERT_EQUAL(describe.GetStatus(), EStatus::SUCCESS);
        }

        {
            // 1 dml 1 ddl statements
            auto result = db.ExecuteQuery(R"(
                SELECT * FROM Test4;
                CREATE TABLE Test5 (
                    Key Uint64,
                    Value1 String,
                    Value2 String,
                    PRIMARY KEY (Key)
                );
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 1);
            UNIT_ASSERT_EQUAL_C(result.GetIssues().Size(), 0, result.GetIssues().ToString());
            NYdb::NTable::TDescribeTableResult describe = session.DescribeTable("/Root/Test5").GetValueSync();
            UNIT_ASSERT_EQUAL(describe.GetStatus(), EStatus::SUCCESS);
        }

        {
            // 1 ddl 1 dml 1 ddl 1 dml statements
            auto result = db.ExecuteQuery(R"(
                CREATE TABLE Test6 (
                    Key Uint64,
                    Value1 String,
                    Value2 String,
                    PRIMARY KEY (Key)
                );
                SELECT * FROM Test6;
                CREATE TABLE Test7 (
                    Key Uint64,
                    Value1 String,
                    Value2 String,
                    PRIMARY KEY (Key)
                );
                SELECT * FROM Test7;
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 2);
            UNIT_ASSERT_EQUAL_C(result.GetIssues().Size(), 0, result.GetIssues().ToString());
            NYdb::NTable::TDescribeTableResult describe1 = session.DescribeTable("/Root/Test6").GetValueSync();
            UNIT_ASSERT_EQUAL(describe1.GetStatus(), EStatus::SUCCESS);
            NYdb::NTable::TDescribeTableResult describe2 = session.DescribeTable("/Root/Test7").GetValueSync();
            UNIT_ASSERT_EQUAL(describe2.GetStatus(), EStatus::SUCCESS);
        }

        {
            // 1 dml 1 ddl 1 dml 1 ddl statements
            auto result = db.ExecuteQuery(R"(
                SELECT * FROM Test7;
                CREATE TABLE Test8 (
                    Key Uint64,
                    Value1 String,
                    Value2 String,
                    PRIMARY KEY (Key)
                );
                SELECT * FROM Test8;
                CREATE TABLE Test9 (
                    Key Uint64,
                    Value1 String,
                    Value2 String,
                    PRIMARY KEY (Key)
                );
            )", TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
            UNIT_ASSERT_VALUES_EQUAL(result.GetResultSets().size(), 2);
            UNIT_ASSERT_EQUAL_C(result.GetIssues().Size(), 0, result.GetIssues().ToString());
            NYdb::NTable::TDescribeTableResult describe1 = session.DescribeTable("/Root/Test8").GetValueSync();
            UNIT_ASSERT_EQUAL(describe1.GetStatus(), EStatus::SUCCESS);
            NYdb::NTable::TDescribeTableResult describe2 = session.DescribeTable("/Root/Test9").GetValueSync();
            UNIT_ASSERT_EQUAL(describe2.GetStatus(), EStatus::SUCCESS);
        }
    }

    Y_UNIT_TEST(TableSink_ReplaceFromSelectOlap) {
        NKikimrConfig::TAppConfig appConfig;
        appConfig.MutableTableServiceConfig()->SetEnableOlapSink(true);
        auto settings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetWithSampleTables(false);
        TKikimrRunner kikimr(settings);
        Tests::NCommon::TLoggerInit(kikimr).Initialize();

        auto session = kikimr.GetTableClient().CreateSession().GetValueSync().GetSession();

        const TString query = R"(
            CREATE TABLE `/Root/ColumnSource` (
                Col1 Uint64 NOT NULL,
                Col2 String,
                Col3 Int32 NOT NULL,
                PRIMARY KEY (Col1)
            )
            PARTITION BY HASH(Col1)
            WITH (STORE = COLUMN, AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 10);

            CREATE TABLE `/Root/ColumnShard1` (
                Col1 Uint64 NOT NULL,
                Col2 String,
                Col3 Int32 NOT NULL,
                PRIMARY KEY (Col1)
            )
            PARTITION BY HASH(Col1)
            WITH (STORE = COLUMN, AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 3);

            CREATE TABLE `/Root/ColumnShard2` (
                Col1 Uint64 NOT NULL,
                Col2 String,
                Col3 Int32 NOT NULL,
                PRIMARY KEY (Col1)
            )
            PARTITION BY HASH(Col1)
            WITH (STORE = COLUMN, AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 16);
        )";

        auto result = session.ExecuteSchemeQuery(query).GetValueSync();
        UNIT_ASSERT_C(result.GetStatus() == NYdb::EStatus::SUCCESS, result.GetIssues().ToString());

        auto client = kikimr.GetQueryClient();
        {
            auto prepareResult = client.ExecuteQuery(R"(
                REPLACE INTO `/Root/ColumnSource` (Col1, Col2, Col3) VALUES
                    (1u, "test1", 10), (2u, "test2", 11), (3u, "test3", 12), (4u, NULL, 13);
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(prepareResult.IsSuccess(), prepareResult.GetIssues().ToString());
        }

        {
            // empty
            const TString sql = R"(
                REPLACE INTO `/Root/ColumnShard1`
                SELECT * FROM `/Root/ColumnSource` WHERE Col3 == 0
            )";
            auto insertResult = client.ExecuteQuery(sql, NYdb::NQuery::TTxControl::BeginTx().CommitTx()).GetValueSync();
            UNIT_ASSERT_C(insertResult.IsSuccess(), insertResult.GetIssues().ToString());

            auto it = client.StreamExecuteQuery(R"(
                SELECT * FROM `/Root/ColumnShard1` ORDER BY Col1, Col2, Col3;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
            TString output = StreamResultToYson(it);
            CompareYson(
                output,
                R"([])");
        }

        {
            // Missing Nullable column
            const TString sql = R"(
                REPLACE INTO `/Root/ColumnShard1`
                SELECT 10u + Col1 AS Col1, 100 + Col3 AS Col3 FROM `/Root/ColumnSource`
            )";
            auto insertResult = client.ExecuteQuery(sql, NYdb::NQuery::TTxControl::BeginTx().CommitTx()).GetValueSync();
            UNIT_ASSERT_C(insertResult.IsSuccess(), insertResult.GetIssues().ToString());

            auto it = client.StreamExecuteQuery(R"(
                SELECT * FROM `/Root/ColumnShard1` ORDER BY Col1, Col2, Col3;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
            TString output = StreamResultToYson(it);
            CompareYson(
                output,
                R"([[11u;#;110];[12u;#;111];[13u;#;112];[14u;#;113]])");
        }

        {
            // column -> column
            const TString sql = R"(
                REPLACE INTO `/Root/ColumnShard2`
                SELECT * FROM `/Root/ColumnSource`
            )";
            auto insertResult = client.ExecuteQuery(sql, NYdb::NQuery::TTxControl::BeginTx().CommitTx()).GetValueSync();
            UNIT_ASSERT_C(insertResult.IsSuccess(), insertResult.GetIssues().ToString());

            auto it = client.StreamExecuteQuery(R"(
                SELECT * FROM `/Root/ColumnShard2` ORDER BY Col1, Col2, Col3;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
            TString output = StreamResultToYson(it);
            CompareYson(
                output,
                R"([[1u;["test1"];10];[2u;["test2"];11];[3u;["test3"];12];[4u;#;13]])");
        }
    }

    Y_UNIT_TEST_TWIN(TableSink_Htap, withOltpSink) {
        NKikimrConfig::TAppConfig appConfig;
        appConfig.MutableTableServiceConfig()->SetEnableOlapSink(true);
        appConfig.MutableTableServiceConfig()->SetEnableOltpSink(withOltpSink);
        appConfig.MutableTableServiceConfig()->SetEnableHtapTx(true);
        auto settings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetWithSampleTables(false).SetColumnShardReaderClassName("PLAIN");
        TKikimrRunner kikimr(settings);
        Tests::NCommon::TLoggerInit(kikimr).Initialize();

        auto session = kikimr.GetTableClient().CreateSession().GetValueSync().GetSession();

        const TString query = R"(
            CREATE TABLE `/Root/ColumnShard` (
                Col1 Uint64 NOT NULL,
                Col2 String NOT NULL,
                Col3 Int32 NOT NULL,
                PRIMARY KEY (Col1)
            )
            PARTITION BY HASH(Col1)
            WITH (STORE = COLUMN, AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 10);

            CREATE TABLE `/Root/DataShard` (
                Col1 Uint64 NOT NULL,
                Col2 String NOT NULL,
                Col3 Int32 NOT NULL,
                PRIMARY KEY (Col1)
            )
            WITH (UNIFORM_PARTITIONS = 2, AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 2);
        )";

        auto result = session.ExecuteSchemeQuery(query).GetValueSync();
        UNIT_ASSERT_C(result.GetStatus() == NYdb::EStatus::SUCCESS, result.GetIssues().ToString());

        auto client = kikimr.GetQueryClient();

        {
            auto result = client.ExecuteQuery(R"(
                UPSERT INTO `/Root/ColumnShard` (Col1, Col2, Col3) VALUES
                    (1u, "test1", 10), (2u, "test2", 11), (3u, "test3", 12), (4u, "test", 13);
                UPSERT INTO `/Root/DataShard` (Col1, Col2, Col3) VALUES
                    (10u, "test1", 10), (20u, "test2", 11), (30u, "test3", 12), (40u, "test", 13);
                INSERT INTO `/Root/ColumnShard` SELECT * FROM `/Root/DataShard`;
                REPLACE INTO `/Root/DataShard` SELECT * FROM `/Root/ColumnShard`;
                SELECT * FROM `/Root/ColumnShard` ORDER BY Col1;
                SELECT * FROM `/Root/DataShard` ORDER BY Col1;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
            CompareYson(R"([[1u;"test1";10];[2u;"test2";11];[3u;"test3";12];[4u;"test";13];[10u;"test1";10];[20u;"test2";11];[30u;"test3";12];[40u;"test";13]])", FormatResultSetYson(result.GetResultSet(0)));
            CompareYson(R"([[1u;"test1";10];[2u;"test2";11];[3u;"test3";12];[4u;"test";13];[10u;"test1";10];[20u;"test2";11];[30u;"test3";12];[40u;"test";13]])", FormatResultSetYson(result.GetResultSet(1)));
        }

        {
            auto result = client.ExecuteQuery(R"(
                UPSERT INTO `/Root/DataShard` (Col1, Col2, Col3) VALUES (1u, "test", 0);
                UPSERT INTO `/Root/DataShard` (Col1, Col2, Col3) VALUES (2u, "test", 0);
                UPSERT INTO `/Root/ColumnShard` (Col1, Col2, Col3) VALUES (1u, "test", 0);
                UPSERT INTO `/Root/ColumnShard` (Col1, Col2, Col3) VALUES (2u, "test", 0);
                UPSERT INTO `/Root/DataShard` (Col1, Col2, Col3) VALUES (3u, "test", 0);
                UPSERT INTO `/Root/DataShard` (Col1, Col2, Col3) VALUES (4u, "test", 0);
                UPSERT INTO `/Root/ColumnShard` (Col1, Col2, Col3) VALUES (3u, "test", 0);
                UPSERT INTO `/Root/ColumnShard` (Col1, Col2, Col3) VALUES (4u, "test", 0);
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
        }

        {
            auto result = client.ExecuteQuery(R"(
                SELECT COUNT(*) FROM `/Root/DataShard` WHERE Col3 = 0;
                SELECT COUNT(*) FROM `/Root/ColumnShard` WHERE Col3 = 0;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
            CompareYson(R"([[4u]])", FormatResultSetYson(result.GetResultSet(0)));
            CompareYson(R"([[4u]])", FormatResultSetYson(result.GetResultSet(1)));
        }

        {
            auto result = client.ExecuteQuery(R"(
                UPSERT INTO `/Root/ColumnShard` (Col1, Col2, Col3) VALUES (1u, "test", 1);
                UPSERT INTO `/Root/ColumnShard` (Col1, Col2, Col3) VALUES (2u, "test", 1);
                UPSERT INTO `/Root/DataShard` (Col1, Col2, Col3) VALUES (1u, "test", 1);
                UPSERT INTO `/Root/DataShard` (Col1, Col2, Col3) VALUES (2u, "test", 1);
                UPSERT INTO `/Root/ColumnShard` (Col1, Col2, Col3) VALUES (3u, "test", 1);
                UPSERT INTO `/Root/ColumnShard` (Col1, Col2, Col3) VALUES (4u, "test", 1);
                UPSERT INTO `/Root/DataShard` (Col1, Col2, Col3) VALUES (3u, "test", 1);
                UPSERT INTO `/Root/DataShard` (Col1, Col2, Col3) VALUES (4u, "test", 1);
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
        }

        {
            auto result = client.ExecuteQuery(R"(
                SELECT COUNT(*) FROM `/Root/DataShard` WHERE Col3 = 1;
                SELECT COUNT(*) FROM `/Root/ColumnShard` WHERE Col3 = 1;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
            CompareYson(R"([[4u]])", FormatResultSetYson(result.GetResultSet(0)));
            CompareYson(R"([[4u]])", FormatResultSetYson(result.GetResultSet(1)));
        }

        {
            auto result = client.ExecuteQuery(R"(
                INSERT INTO `/Root/DataShard` SELECT Col1 + 100 AS Col1, Col2, Col3 + 100 AS Col3 FROM `/Root/ColumnShard` WHERE Col3 = 1;
                INSERT INTO `/Root/ColumnShard` SELECT Col1 + 100 AS Col1, Col2, Col3 + 100 AS Col3 FROM `/Root/DataShard` WHERE Col3 = 1;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
        }

        {
            auto result = client.ExecuteQuery(R"(
                SELECT COUNT(*) FROM `/Root/DataShard` WHERE Col3 = 101;
                SELECT COUNT(*) FROM `/Root/ColumnShard` WHERE Col3 = 101;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
            CompareYson(R"([[4u]])", FormatResultSetYson(result.GetResultSet(0)));
            CompareYson(R"([[4u]])", FormatResultSetYson(result.GetResultSet(1)));
        }

        {
            auto result = client.ExecuteQuery(R"(
                INSERT INTO `/Root/ColumnShard` SELECT Col1 + 1000 AS Col1, Col2, Col3 + 1000 AS Col3 FROM `/Root/DataShard` WHERE Col3 = 1;
                INSERT INTO `/Root/DataShard` SELECT Col1 + 1000 AS Col1, Col2, Col3 + 1000 AS Col3 FROM `/Root/ColumnShard` WHERE Col3 = 1;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
        }

        {
            auto result = client.ExecuteQuery(R"(
                SELECT COUNT(*) FROM `/Root/DataShard` WHERE Col3 = 1001;
                SELECT COUNT(*) FROM `/Root/ColumnShard` WHERE Col3 = 1001;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
            CompareYson(R"([[4u]])", FormatResultSetYson(result.GetResultSet(0)));
            CompareYson(R"([[4u]])", FormatResultSetYson(result.GetResultSet(1)));
        }

        {
            auto result = client.ExecuteQuery(R"(
                DELETE FROM `/Root/ColumnShard` ON SELECT * FROM `/Root/DataShard` WHERE Col1 > 9;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
        }

        {
            auto result = client.ExecuteQuery(R"(
                DELETE FROM `/Root/DataShard` ON SELECT Col1 FROM `/Root/ColumnShard`;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
        }

        {
            auto result = client.ExecuteQuery(R"(
                SELECT * FROM `/Root/DataShard` ORDER BY Col1;
                SELECT * FROM `/Root/ColumnShard` ORDER BY Col1;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
            CompareYson(R"([[10u;"test1";10];[20u;"test2";11];[30u;"test3";12];[40u;"test";13];[101u;"test";101];[102u;"test";101];[103u;"test";101];[104u;"test";101];[1001u;"test";1001];[1002u;"test";1001];[1003u;"test";1001];[1004u;"test";1001]])",
                FormatResultSetYson(result.GetResultSet(0)));
            CompareYson(R"([[1u;"test";1];[2u;"test";1];[3u;"test";1];[4u;"test";1]])", FormatResultSetYson(result.GetResultSet(1)));
        }

        {
            auto result = client.ExecuteQuery(R"(
                DELETE FROM `/Root/DataShard` WHERE Col2 != "not found";
                DELETE FROM `/Root/ColumnShard` WHERE Col2 != "not found";
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
        }

        {
            auto result = client.ExecuteQuery(R"(
                SELECT * FROM `/Root/DataShard`;
                SELECT * FROM `/Root/ColumnShard`;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
            CompareYson(R"([])", FormatResultSetYson(result.GetResultSet(0)));
            CompareYson(R"([])", FormatResultSetYson(result.GetResultSet(1)));
        }

        {
            auto result = client.ExecuteQuery(R"(
                DELETE FROM `/Root/DataShard` WHERE Col2 = "not found";
                DELETE FROM `/Root/ColumnShard` WHERE Col2 = "not found";
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
        }
    }

    Y_UNIT_TEST_TWIN(TableSink_HtapComplex, withOltpSink) {
        NKikimrConfig::TAppConfig appConfig;
        appConfig.MutableTableServiceConfig()->SetEnableOlapSink(true);
        appConfig.MutableTableServiceConfig()->SetEnableOltpSink(withOltpSink);
        appConfig.MutableTableServiceConfig()->SetEnableHtapTx(true);
        auto settings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetWithSampleTables(false).SetColumnShardReaderClassName("PLAIN");
        TKikimrRunner kikimr(settings);
        Tests::NCommon::TLoggerInit(kikimr).Initialize();

        auto session = kikimr.GetTableClient().CreateSession().GetValueSync().GetSession();

        const TString query = R"(
            CREATE TABLE `/Root/ColumnSrc` (
                Col1 Uint64 NOT NULL,
                Col2 String NOT NULL,
                Col3 Int32 NOT NULL,
                PRIMARY KEY (Col1)
            )
            PARTITION BY HASH(Col1)
            WITH (STORE = COLUMN, AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 10);

            CREATE TABLE `/Root/RowSrc` (
                Col1 Uint64 NOT NULL,
                Col2 String NOT NULL,
                Col3 Int32 NOT NULL,
                PRIMARY KEY (Col1)
            )
            WITH (UNIFORM_PARTITIONS = 2, AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 2);

            CREATE TABLE `/Root/ColumnDst` (
                Col1 Uint64 NOT NULL,
                Col2 String,
                Col3 Int32,
                PRIMARY KEY (Col1)
            )
            PARTITION BY HASH(Col1)
            WITH (STORE = COLUMN, AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 10);

            CREATE TABLE `/Root/RowDst` (
                Col1 Uint64 NOT NULL,
                Col2 String,
                Col3 Int32,
                PRIMARY KEY (Col1)
            )
            WITH (UNIFORM_PARTITIONS = 2, AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 2);
        )";

        auto result = session.ExecuteSchemeQuery(query).GetValueSync();
        UNIT_ASSERT_C(result.GetStatus() == NYdb::EStatus::SUCCESS, result.GetIssues().ToString());

        auto client = kikimr.GetQueryClient();

        {
            auto result = client.ExecuteQuery(R"(
                UPSERT INTO `/Root/ColumnSrc` (Col1, Col2, Col3) VALUES
                    (1u, "test1", 10), (2u, "test2", 11);
                REPLACE INTO `/Root/ColumnSrc` (Col1, Col2, Col3) VALUES
                    (3u, "test3", 12), (4u, "test", 13);
                UPSERT INTO `/Root/RowSrc` (Col1, Col2, Col3) VALUES
                    (10u, "test1", 10), (20u, "test2", 11);
                REPLACE INTO `/Root/RowSrc` (Col1, Col2, Col3) VALUES
                    (30u, "test3", 12), (40u, "test", 13);
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
        }

        {
            auto result = client.ExecuteQuery(R"(
                $data = SELECT c.Col1 as Col1, c.Col2 As Col2, r.Col3 AS Col3
                    FROM `/Root/ColumnSrc`as c
                    JOIN `/Root/RowSrc` as r
                    ON c.Col1 + 10 = r.Col3;
                UPSERT INTO `/Root/ColumnDst` SELECT * FROM $data;
                REPLACE INTO `/Root/RowDst` SELECT * FROM $data;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
        }

        {
            auto result = client.ExecuteQuery(R"(
                SELECT COUNT(*) FROM `/Root/ColumnDst`;
                SELECT COUNT(*) FROM `/Root/RowDst`;
                DELETE FROM `/Root/ColumnDst` WHERE 1=1;
                DELETE FROM `/Root/RowDst` WHERE 1=1;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
            CompareYson(R"([[3u]])", FormatResultSetYson(result.GetResultSet(0)));
            CompareYson(R"([[3u]])", FormatResultSetYson(result.GetResultSet(1)));
        }

        {
            auto result = client.ExecuteQuery(R"(
                $prepare = SELECT *
                    FROM `/Root/ColumnSrc`
                    WHERE Col2 LIKE '%test%test%';
                $data = SELECT c.Col1 as Col1, c.Col2 As Col2, r.Col3 AS Col3
                    FROM `/Root/RowSrc`as c
                    LEFT OUTER JOIN $prepare as r
                    ON c.Col1 + 10 = r.Col3;
                UPSERT INTO `/Root/ColumnDst` SELECT * FROM $data;
                REPLACE INTO `/Root/RowDst` SELECT * FROM $data;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
        }

        {
            auto result = client.ExecuteQuery(R"(
                SELECT COUNT(*) FROM `/Root/ColumnDst`;
                SELECT COUNT(*) FROM `/Root/RowDst`;
                DELETE FROM `/Root/ColumnDst` WHERE 1=1;
                DELETE FROM `/Root/RowDst` WHERE 1=1;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
            CompareYson(R"([[4u]])", FormatResultSetYson(result.GetResultSet(0)));
            CompareYson(R"([[4u]])", FormatResultSetYson(result.GetResultSet(1)));
        }
    }

    Y_UNIT_TEST_TWIN(TableSink_HtapInteractive, withOltpSink) {
        NKikimrConfig::TAppConfig appConfig;
        appConfig.MutableTableServiceConfig()->SetEnableOlapSink(true);
        appConfig.MutableTableServiceConfig()->SetEnableOltpSink(withOltpSink);
        appConfig.MutableTableServiceConfig()->SetEnableHtapTx(true);
        auto settings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetWithSampleTables(false);
        TKikimrRunner kikimr(settings);
        Tests::NCommon::TLoggerInit(kikimr).Initialize();

        auto session = kikimr.GetTableClient().CreateSession().GetValueSync().GetSession();

        const TString query = R"(
            CREATE TABLE `/Root/ColumnShard` (
                Col1 Uint64 NOT NULL,
                Col2 String NOT NULL,
                Col3 Int32 NOT NULL,
                PRIMARY KEY (Col1)
            )
            PARTITION BY HASH(Col1)
            WITH (STORE = COLUMN, AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 10);

            CREATE TABLE `/Root/DataShard` (
                Col1 Uint64 NOT NULL,
                Col2 String NOT NULL,
                Col3 Int32 NOT NULL,
                PRIMARY KEY (Col1)
            )
            WITH (UNIFORM_PARTITIONS = 2, AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 2);
        )";

        auto result = session.ExecuteSchemeQuery(query).GetValueSync();
        UNIT_ASSERT_C(result.GetStatus() == NYdb::EStatus::SUCCESS, result.GetIssues().ToString());

        auto client = kikimr.GetQueryClient();

        {
            auto session = client.GetSession().GetValueSync().GetSession();
            auto result = session.ExecuteQuery(R"(
                INSERT INTO `/Root/ColumnShard` (Col1, Col2, Col3) VALUES
                    (1u, "test1", 1);
            )", NYdb::NQuery::TTxControl::BeginTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());

            auto tx = result.GetTransaction();

            result = session.ExecuteQuery(R"(
                INSERT INTO `/Root/DataShard` (Col1, Col2, Col3) VALUES
                    (2u, "test2", 2);
            )", NYdb::NQuery::TTxControl::Tx(*tx).CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
        }

        {
            auto session = client.GetSession().GetValueSync().GetSession();
            auto result = session.ExecuteQuery(R"(
                INSERT INTO `/Root/DataShard` (Col1, Col2, Col3) VALUES
                    (3u, "test1", 3);
            )", NYdb::NQuery::TTxControl::BeginTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());

            auto tx = result.GetTransaction();

            result = session.ExecuteQuery(R"(
                INSERT INTO `/Root/ColumnShard` (Col1, Col2, Col3) VALUES
                    (4u, "test2", 4);
            )", NYdb::NQuery::TTxControl::Tx(*tx).CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
        }

        {
            auto result = client.ExecuteQuery(R"(
                SELECT Col3 FROM `/Root/ColumnShard`
                UNION
                SELECT Col3 FROM `/Root/DataShard`
                ORDER BY Col3;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
            CompareYson(R"([[1];[2];[3];[4]])", FormatResultSetYson(result.GetResultSet(0)));
        }
    }

    Y_UNIT_TEST(TableSink_BadTransactions) {
        NKikimrConfig::TAppConfig appConfig;
        appConfig.MutableTableServiceConfig()->SetEnableOlapSink(true);
        appConfig.MutableTableServiceConfig()->SetEnableOltpSink(true);
        appConfig.MutableTableServiceConfig()->SetEnableHtapTx(false);
        auto settings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetWithSampleTables(false);
        TKikimrRunner kikimr(settings);
        Tests::NCommon::TLoggerInit(kikimr).Initialize();

        auto session = kikimr.GetTableClient().CreateSession().GetValueSync().GetSession();

        const TString query = R"(
            CREATE TABLE `/Root/ColumnShard` (
                Col1 Uint64 NOT NULL,
                Col2 String,
                Col3 Int32 NOT NULL,
                PRIMARY KEY (Col1)
            )
            PARTITION BY HASH(Col1)
            WITH (STORE = COLUMN, AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 10);

            CREATE TABLE `/Root/DataShard` (
                Col1 Uint64 NOT NULL,
                Col2 String,
                Col3 Int32 NOT NULL,
                PRIMARY KEY (Col1)
            )
            WITH (UNIFORM_PARTITIONS = 2, AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 2);
        )";

        auto result = session.ExecuteSchemeQuery(query).GetValueSync();
        UNIT_ASSERT_C(result.GetStatus() == NYdb::EStatus::SUCCESS, result.GetIssues().ToString());

        auto client = kikimr.GetQueryClient();
        {
            auto prepareResult = client.ExecuteQuery(R"(
                REPLACE INTO `/Root/ColumnShard` (Col1, Col2, Col3) VALUES
                    (1u, "test1", 10), (2u, "test2", 11), (3u, "test3", 12), (4u, NULL, 13);
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(prepareResult.IsSuccess(), prepareResult.GetIssues().ToString());
        }
        {
            auto prepareResult = client.ExecuteQuery(R"(
                REPLACE INTO `/Root/DataShard` (Col1, Col2, Col3) VALUES
                    (10u, "test1", 10), (20u, "test2", 11), (30u, "test3", 12), (40u, NULL, 13);
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(prepareResult.IsSuccess(), prepareResult.GetIssues().ToString());
        }

        {
            // column -> row
            const TString sql = R"(
                REPLACE INTO `/Root/DataShard`
                SELECT * FROM `/Root/ColumnShard`
            )";
            auto insertResult = client.ExecuteQuery(sql, NYdb::NQuery::TTxControl::BeginTx().CommitTx()).GetValueSync();
            UNIT_ASSERT(!insertResult.IsSuccess());
            UNIT_ASSERT_C(
                insertResult.GetIssues().ToString().contains("Write transactions between column and row tables are disabled at current time"),
                insertResult.GetIssues().ToString());
        }

        {
            // row -> column
            const TString sql = R"(
                REPLACE INTO `/Root/ColumnShard`
                SELECT * FROM `/Root/DataShard`
            )";
            auto insertResult = client.ExecuteQuery(sql, NYdb::NQuery::TTxControl::BeginTx().CommitTx()).GetValueSync();
            UNIT_ASSERT(!insertResult.IsSuccess());
            UNIT_ASSERT_C(
                insertResult.GetIssues().ToString().contains("Write transactions between column and row tables are disabled at current time"),
                insertResult.GetIssues().ToString());
        }

        {
            // column & row write
            const TString sql = R"(
                REPLACE INTO `/Root/DataShard` (Col1, Col2, Col3) VALUES
                    (1u, "test1", 10), (2u, "test2", 11), (3u, "test3", 12), (4u, NULL, 13);
                REPLACE INTO `/Root/ColumnShard` (Col1, Col2, Col3) VALUES
                    (1u, "test1", 10), (2u, "test2", 11), (3u, "test3", 12), (4u, NULL, 13);
            )";
            auto insertResult = client.ExecuteQuery(sql, NYdb::NQuery::TTxControl::BeginTx().CommitTx()).GetValueSync();
            UNIT_ASSERT(!insertResult.IsSuccess());
            UNIT_ASSERT_C(
                insertResult.GetIssues().ToString().contains("Write transactions between column and row tables are disabled at current time"),
                insertResult.GetIssues().ToString());
        }

        {
            // column read & row write
            const TString sql = R"(
                REPLACE INTO `/Root/DataShard` (Col1, Col2, Col3) VALUES
                    (1u, "test1", 10), (2u, "test2", 11), (3u, "test3", 12), (4u, NULL, 13);
                SELECT * FROM `/Root/ColumnShard`;
            )";
            auto insertResult = client.ExecuteQuery(sql, NYdb::NQuery::TTxControl::BeginTx().CommitTx()).GetValueSync();
            UNIT_ASSERT(!insertResult.IsSuccess());
            UNIT_ASSERT_C(
                insertResult.GetIssues().ToString().contains("Write transactions between column and row tables are disabled at current time"),
                insertResult.GetIssues().ToString());
        }

        {
            // column write & row read
            const TString sql = R"(
                REPLACE INTO `/Root/ColumnShard` (Col1, Col2, Col3) VALUES
                    (1u, "test1", 10), (2u, "test2", 11), (3u, "test3", 12), (4u, NULL, 13);
                SELECT * FROM `/Root/DataShard`;
            )";
            auto insertResult = client.ExecuteQuery(sql, NYdb::NQuery::TTxControl::BeginTx().CommitTx()).GetValueSync();
            UNIT_ASSERT(!insertResult.IsSuccess());
            UNIT_ASSERT_C(
                insertResult.GetIssues().ToString().contains("Write transactions between column and row tables are disabled at current time"),
                insertResult.GetIssues().ToString());
        }

        {
            auto session = client.GetSession().GetValueSync().GetSession();
            {
                auto insertResult = session.ExecuteQuery(R"(
                    REPLACE INTO `/Root/ColumnShard` (Col1, Col2, Col3) VALUES
                        (1u, "test1", 10), (2u, "test2", 11), (3u, "test3", 12), (4u, NULL, 13);
                )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).GetValueSync();
                UNIT_ASSERT(insertResult.IsSuccess());
            }
            {
                auto insertResult = session.ExecuteQuery(R"(
                    REPLACE INTO `/Root/DataShard` (Col1, Col2, Col3) VALUES
                        (1u, "test1", 10), (2u, "test2", 11), (3u, "test3", 12), (4u, NULL, 13);
                )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).GetValueSync();
                UNIT_ASSERT(insertResult.IsSuccess());
            }
        }
    }

    Y_UNIT_TEST(TableSink_ReplaceFromSelectLargeOlap) {
        NKikimrConfig::TAppConfig appConfig;
        appConfig.MutableTableServiceConfig()->SetEnableOlapSink(true);
        auto settings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetWithSampleTables(false);

        TTestHelper testHelper(settings);

        TKikimrRunner& kikimr = testHelper.GetKikimr();
        Tests::NCommon::TLoggerInit(kikimr).Initialize();

        TVector<TTestHelper::TColumnSchema> schema = {
            TTestHelper::TColumnSchema().SetName("Col1").SetType(NScheme::NTypeIds::Int64).SetNullable(false),
            TTestHelper::TColumnSchema().SetName("Col2").SetType(NScheme::NTypeIds::Int32).SetNullable(false),
        };

        TTestHelper::TColumnTable testTable1;
        testTable1.SetName("/Root/ColumnShard1").SetPrimaryKey({ "Col1" }).SetSharding({ "Col1" }).SetSchema(schema).SetMinPartitionsCount(1000);
        testHelper.CreateTable(testTable1);

        TTestHelper::TColumnTable testTable2;
        testTable2.SetName("/Root/ColumnShard2").SetPrimaryKey({ "Col1" }).SetSharding({ "Col1" }).SetSchema(schema).SetMinPartitionsCount(1000);
        testHelper.CreateTable(testTable2);

        {
            TTestHelper::TUpdatesBuilder tableInserter(testTable1.GetArrowSchema(schema));
            for (size_t index = 0; index < 10000; ++index) {
                tableInserter.AddRow().Add(index).Add(index * 10);
            }
            testHelper.BulkUpsert(testTable1, tableInserter);
        }

        auto session = kikimr.GetTableClient().CreateSession().GetValueSync().GetSession();
        auto client = kikimr.GetQueryClient();

        {
            const TString sql = R"(
                REPLACE INTO `/Root/ColumnShard2`
                SELECT * FROM `/Root/ColumnShard1`
            )";
            auto insertResult = client.ExecuteQuery(sql, NYdb::NQuery::TTxControl::BeginTx().CommitTx()).GetValueSync();
            UNIT_ASSERT_C(insertResult.IsSuccess(), insertResult.GetIssues().ToString());

            auto it = client.StreamExecuteQuery(R"(
                SELECT COUNT(*) FROM `/Root/ColumnShard2`;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
            TString output = StreamResultToYson(it);
            CompareYson(
                output,
                R"([[10000u]])");
        }
    }

    Y_UNIT_TEST(TableSink_Olap_Replace) {
        NKikimrConfig::TAppConfig appConfig;
        appConfig.MutableTableServiceConfig()->SetEnableOlapSink(true);
        auto settings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetWithSampleTables(false);
        TKikimrRunner kikimr(settings);
        Tests::NCommon::TLoggerInit(kikimr).Initialize();

        auto session = kikimr.GetTableClient().CreateSession().GetValueSync().GetSession();

        const TString query = R"(
            CREATE TABLE `/Root/ColumnShard` (
                Col1 Uint64 NOT NULL,
                Col2 Int32,
                Col4 String,
                Col3 String NOT NULL,
                PRIMARY KEY (Col1, Col3)
            )
            PARTITION BY HASH(Col1)
            WITH (STORE = COLUMN, AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 10);
        )";

        auto result = session.ExecuteSchemeQuery(query).GetValueSync();
        UNIT_ASSERT_C(result.GetStatus() == NYdb::EStatus::SUCCESS, result.GetIssues().ToString());

        // Shuffled
        auto client = kikimr.GetQueryClient();
        {
            auto prepareResult = client.ExecuteQuery(R"(
                REPLACE INTO `/Root/ColumnShard` (Col3, Col4, Col2, Col1) VALUES
                    ("test100", "100", 1000, 100u);
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(prepareResult.IsSuccess(), prepareResult.GetIssues().ToString());
        }

        {
            auto prepareResult = client.ExecuteQuery(R"(
                REPLACE INTO `/Root/ColumnShard` (Col1, Col3, Col2, Col4) VALUES
                    (1u, "test1", 10, "1"), (2u, "test2", NULL, "2"), (3u, "test3", 12, NULL), (4u, "test4", NULL, NULL);
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(prepareResult.IsSuccess(), prepareResult.GetIssues().ToString());
        }

        auto it = client.StreamExecuteQuery(R"(
            SELECT Col1, Col3, Col2, Col4 FROM `/Root/ColumnShard` ORDER BY Col1, Col3, Col2, Col4;
        )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
        TString output = StreamResultToYson(it);
        CompareYson(output, R"([[1u;"test1";[10];["1"]];[2u;"test2";#;["2"]];[3u;"test3";[12];#];[4u;"test4";#;#];[100u;"test100";[1000];["100"]]])");
    }

    Y_UNIT_TEST_TWIN(TableSink_OltpReplace, HasSecondaryIndex) {
        NKikimrConfig::TAppConfig appConfig;
        appConfig.MutableTableServiceConfig()->SetEnableOltpSink(true);
        auto settings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetWithSampleTables(false);

        TKikimrRunner kikimr(settings);
        Tests::NCommon::TLoggerInit(kikimr).Initialize();

        auto session = kikimr.GetTableClient().CreateSession().GetValueSync().GetSession();

        const TString query = Sprintf(R"(
            CREATE TABLE `/Root/DataShard` (
                Col1 Uint64 NOT NULL,
                Col2 Int32,
                Col3 String,
                %s
                PRIMARY KEY (Col1)
            );
        )", (HasSecondaryIndex ? "INDEX idx_2 GLOBAL ON (Col2)," : ""));

        auto result = session.ExecuteSchemeQuery(query).GetValueSync();
        UNIT_ASSERT_C(result.GetStatus() == NYdb::EStatus::SUCCESS, result.GetIssues().ToString());

        auto client = kikimr.GetQueryClient();

        {
            auto it = client.ExecuteQuery(R"(
                REPLACE INTO `/Root/DataShard` (Col1, Col2) VALUES (0u, 0);
                REPLACE INTO `/Root/DataShard` (Col1, Col3) VALUES (1u, 'test');
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx(), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(5000))).ExtractValueSync();
            UNIT_ASSERT_C(it.IsSuccess(), it.GetIssues().ToString());
        }

        {
            auto it = client.StreamExecuteQuery(R"(
                SELECT * FROM `/Root/DataShard`;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx(), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(5000))).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
            TString output = StreamResultToYson(it);
            CompareYson(output, R"([[0u;[0];#];[1u;#;["test"]]])");
        }

        {
            auto it = client.ExecuteQuery(R"(
                REPLACE INTO `/Root/DataShard` (Col1, Col3) VALUES (0u, 'null');
                REPLACE INTO `/Root/DataShard` (Col1) VALUES (1u);
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx(), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(5000))).ExtractValueSync();
            UNIT_ASSERT_C(it.IsSuccess(), it.GetIssues().ToString());
        }


        {
            auto it = client.StreamExecuteQuery(R"(
                SELECT * FROM `/Root/DataShard`;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx(), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(5000))).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
            TString output = StreamResultToYson(it);
            CompareYson(output, R"([[0u;#;["null"]];[1u;#;#]])");
        }
    }

    Y_UNIT_TEST(TableSink_OltpLiteralUpsert) {
        NKikimrConfig::TAppConfig appConfig;
        appConfig.MutableTableServiceConfig()->SetEnableOltpSink(true);
        auto settings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetWithSampleTables(false);

        TKikimrRunner kikimr(settings);
        Tests::NCommon::TLoggerInit(kikimr).Initialize();

        auto session = kikimr.GetTableClient().CreateSession().GetValueSync().GetSession();

        const TString query = Sprintf(R"(
            CREATE TABLE `/Root/DataShard` (
                Col1 Uint64 NOT NULL,
                Col2 Int32,
                PRIMARY KEY (Col1)
            );
        )");

        auto result = session.ExecuteSchemeQuery(query).GetValueSync();
        UNIT_ASSERT_C(result.GetStatus() == NYdb::EStatus::SUCCESS, result.GetIssues().ToString());

        auto client = kikimr.GetQueryClient();

        {
            auto it = client.ExecuteQuery(R"(
                UPSERT INTO `/Root/DataShard` (Col1, Col2) VALUES (0u, 0);
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx(), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(1000))).ExtractValueSync();
            UNIT_ASSERT_C(it.IsSuccess(), it.GetIssues().ToString());
        }

        {
            auto it = client.StreamExecuteQuery(R"(
                SELECT * FROM `/Root/DataShard`;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx(), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(1000))).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
            TString output = StreamResultToYson(it);
            CompareYson(output, R"([[0u;[0]]])");
        }
    }

    class TTableDataModificationTester {
    protected:
        std::unique_ptr<TKikimrRunner> Kikimr;
        YDB_ACCESSOR(bool, IsOlap, false);
        virtual void DoExecute() = 0;
    public:
        void Execute() {
            auto settings = TKikimrSettings().SetWithSampleTables(false).SetColumnShardReaderClassName("PLAIN");
            settings.AppConfig.MutableTableServiceConfig()->SetEnableOlapSink(IsOlap);
            settings.AppConfig.MutableTableServiceConfig()->SetEnableOltpSink(!IsOlap);

            Kikimr = std::make_unique<TKikimrRunner>(settings);
            Tests::NCommon::TLoggerInit(*Kikimr).Initialize();

            auto session = Kikimr->GetTableClient().CreateSession().GetValueSync().GetSession();

            auto csController = NYDBTest::TControllers::RegisterCSControllerGuard<NYDBTest::NColumnShard::TController>();
            csController->SetOverridePeriodicWakeupActivationPeriod(TDuration::Seconds(1));
            csController->SetOverrideLagForCompactionBeforeTierings(TDuration::Seconds(1));

            const TString query = Sprintf(R"(
                CREATE TABLE `/Root/DataShard` (
                    Col1 Uint64 NOT NULL,
                    Col2 Int32,
                    Col3 String,
                    PRIMARY KEY (Col1)
                ) WITH (
                    STORE = %s,
                    AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 10
                );
            )", IsOlap ? "COLUMN" : "ROW");

            auto result = session.ExecuteSchemeQuery(query).GetValueSync();
            UNIT_ASSERT_C(result.GetStatus() == NYdb::EStatus::SUCCESS, result.GetIssues().ToString());
            DoExecute();
        }

    };

    class TUpsertFromTableTester: public TTableDataModificationTester {
    protected:
        void DoExecute() override {
            auto client = Kikimr->GetQueryClient();

            {
                auto it = client.ExecuteQuery(R"(
                UPSERT INTO `/Root/DataShard` (Col1, Col2) VALUES (0u, 0);
                UPSERT INTO `/Root/DataShard` (Col1, Col3) VALUES (1u, 'test');
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
                UNIT_ASSERT_C(it.IsSuccess(), it.GetIssues().ToString());
            }

            {
                auto it = client.StreamExecuteQuery(R"(
                SELECT * FROM `/Root/DataShard` ORDER BY Col1;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
                UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
                TString output = StreamResultToYson(it);
                CompareYson(output, R"([[0u;[0];#];[1u;#;["test"]]])");
            }

            {
                auto it = client.ExecuteQuery(R"(
                UPSERT INTO `/Root/DataShard` (Col1, Col3) VALUES (0u, 'null');
                UPSERT INTO `/Root/DataShard` (Col1) VALUES (1u);
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
                UNIT_ASSERT_C(it.IsSuccess(), it.GetIssues().ToString());
            }

            {
                auto it = client.ExecuteQuery(R"(
                UPSERT INTO `/Root/DataShard` (Col3) VALUES ('null');
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
                UNIT_ASSERT(!it.IsSuccess());
            }

            {
                auto it = client.StreamExecuteQuery(R"(
                SELECT * FROM `/Root/DataShard` ORDER BY Col1;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
                UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
                TString output = StreamResultToYson(it);
                CompareYson(output, R"([[0u;[0];["null"]];[1u;#;["test"]]])");
            }
        }
    };

    Y_UNIT_TEST(TableSink_OltpUpsert) {
        TUpsertFromTableTester tester;
        tester.SetIsOlap(false);
        tester.Execute();
    }

    Y_UNIT_TEST(TableSink_OlapUpsert) {
        TUpsertFromTableTester tester;
        tester.SetIsOlap(true);
        tester.Execute();
    }

    class TInsertFromTableTester: public TTableDataModificationTester {
    protected:
        void DoExecute() override {
            auto client = Kikimr->GetQueryClient();

            {
                auto it = client.ExecuteQuery(R"(
                INSERT INTO `/Root/DataShard` (Col1, Col2) VALUES (0u, 0);
                INSERT INTO `/Root/DataShard` (Col1, Col3) VALUES (1u, 'test');
                INSERT INTO `/Root/DataShard` (Col1, Col3, Col2) VALUES (2u, 't', 3);
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx(), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(1000))).ExtractValueSync();
                UNIT_ASSERT_C(it.IsSuccess(), it.GetIssues().ToString());
            }

            {
                auto it = client.StreamExecuteQuery(R"(
                SELECT * FROM `/Root/DataShard` ORDER BY Col1;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx(), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(1000))).ExtractValueSync();
                UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
                TString output = StreamResultToYson(it);
                CompareYson(output, R"([[0u;[0];#];[1u;#;["test"]];[2u;[3];["t"]]])");
            }

            {
                auto it = client.ExecuteQuery(R"(
                INSERT INTO `/Root/DataShard` (Col1, Col3) VALUES (0u, 'null');
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx(), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(1000))).ExtractValueSync();
                UNIT_ASSERT_C(!it.IsSuccess(), it.GetIssues().ToString());
                UNIT_ASSERT_C(
                    it.GetIssues().ToString().contains("Operation is aborting because an duplicate key")
                    || it.GetIssues().ToString().contains("Conflict with existing key."),
                    it.GetIssues().ToString());
            }

            {
                auto it = client.StreamExecuteQuery(R"(
                SELECT * FROM `/Root/DataShard` ORDER BY Col1;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx(), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(1000))).ExtractValueSync();
                UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
                TString output = StreamResultToYson(it);
                CompareYson(output, R"([[0u;[0];#];[1u;#;["test"]];[2u;[3];["t"]]])");
            }
        }
    };

    Y_UNIT_TEST(TableSink_OltpInsert) {
        TInsertFromTableTester tester;
        tester.SetIsOlap(false);
        tester.Execute();
    }

    Y_UNIT_TEST(TableSink_OlapInsert) {
        TInsertFromTableTester tester;
        tester.SetIsOlap(true);
        tester.Execute();
    }

    class TDeleteFromTableTester: public TTableDataModificationTester {
    protected:
        void DoExecute() override {
            auto client = Kikimr->GetQueryClient();

            {
                auto it = client.ExecuteQuery(R"(
                INSERT INTO `/Root/DataShard` (Col1, Col2) VALUES (0u, 0);
                INSERT INTO `/Root/DataShard` (Col1, Col3) VALUES (1u, 'test');
                INSERT INTO `/Root/DataShard` (Col1, Col3, Col2) VALUES (2u, 't', 3);
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
                UNIT_ASSERT_C(it.IsSuccess(), it.GetIssues().ToString());
            }

            {
                auto it = client.StreamExecuteQuery(R"(
                SELECT * FROM `/Root/DataShard` ORDER BY Col1;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
                UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
                TString output = StreamResultToYson(it);
                CompareYson(output, R"([[0u;[0];#];[1u;#;["test"]];[2u;[3];["t"]]])");
            }

            {
                auto it = client.ExecuteQuery(R"(
                DELETE FROM `/Root/DataShard` WHERE Col3 == 't';
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
                UNIT_ASSERT_C(it.IsSuccess(), it.GetIssues().ToString());
            }
            {
                auto it = client.StreamExecuteQuery(R"(
                SELECT * FROM `/Root/DataShard` ORDER BY Col1;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
                UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
                TString output = StreamResultToYson(it);
                CompareYson(output, R"([[0u;[0];#];[1u;#;["test"]]])");
            }

            {
                auto it = client.ExecuteQuery(R"(
                DELETE FROM `/Root/DataShard` WHERE Col3 == 'not found';
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
                UNIT_ASSERT_C(it.IsSuccess(), it.GetIssues().ToString());
            }

            {
                auto it = client.ExecuteQuery(R"(
                DELETE FROM `/Root/DataShard` ON SELECT 0 AS Col1;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
                UNIT_ASSERT_C(it.IsSuccess(), it.GetIssues().ToString());
            }

            {
                auto it = client.StreamExecuteQuery(R"(
                SELECT * FROM `/Root/DataShard` ORDER BY Col1;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
                UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
                TString output = StreamResultToYson(it);
                CompareYson(output, R"([[1u;#;["test"]]])");
            }
        }
    };

    Y_UNIT_TEST(TableSink_OltpDelete) {
        TDeleteFromTableTester tester;
        tester.SetIsOlap(false);
        tester.Execute();
    }

    Y_UNIT_TEST(TableSink_OlapDelete) {
        TDeleteFromTableTester tester;
        tester.SetIsOlap(true);
        tester.Execute();
    }

    class TUpdateFromTableTester: public TTableDataModificationTester {
    protected:
        void DoExecute() override {
            auto client = Kikimr->GetQueryClient();

            {
                auto it = client.ExecuteQuery(R"(
                INSERT INTO `/Root/DataShard` (Col1, Col2) VALUES (0u, 0);
                INSERT INTO `/Root/DataShard` (Col1, Col3) VALUES (1u, 'test');
                INSERT INTO `/Root/DataShard` (Col1, Col3, Col2) VALUES (2u, 't', 3);
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx(), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(10000))).ExtractValueSync();
                UNIT_ASSERT_C(it.IsSuccess(), it.GetIssues().ToString());
            }

            {
                auto it = client.StreamExecuteQuery(R"(
                SELECT * FROM `/Root/DataShard` ORDER BY Col1;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx(), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(10000))).ExtractValueSync();
                UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
                TString output = StreamResultToYson(it);
                CompareYson(output, R"([[0u;[0];#];[1u;#;["test"]];[2u;[3];["t"]]])");
            }

            {
                auto it = client.ExecuteQuery(R"(
                UPDATE `/Root/DataShard` SET Col2 = 42 WHERE Col3 == 'not found';
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx(), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(10000))).ExtractValueSync();
                UNIT_ASSERT_C(it.IsSuccess(), it.GetIssues().ToString());
            }

            {
                auto it = client.ExecuteQuery(R"(
                UPDATE `/Root/DataShard` SET Col2 = 42 WHERE Col3 == 't';
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx(), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(10000))).ExtractValueSync();
                UNIT_ASSERT_C(it.IsSuccess(), it.GetIssues().ToString());
            }

            {
                auto it = client.StreamExecuteQuery(R"(
                SELECT * FROM `/Root/DataShard` ORDER BY Col1;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx(), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(10000))).ExtractValueSync();
                UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
                TString output = StreamResultToYson(it);
                CompareYson(output, R"([[0u;[0];#];[1u;#;["test"]];[2u;[42];["t"]]])");
            }

            {
                auto it = client.ExecuteQuery(R"(
                UPDATE `/Root/DataShard` ON SELECT 0u AS Col1, 1 AS Col2, 'text' AS Col3;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx(), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(10000))).ExtractValueSync();
                UNIT_ASSERT_C(it.IsSuccess(), it.GetIssues().ToString());
            }

            {
                auto it = client.ExecuteQuery(R"(
                UPDATE `/Root/DataShard` ON SELECT 10u AS Col1, 1 AS Col2, 'text' AS Col3;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx(), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(10000))).ExtractValueSync();
                UNIT_ASSERT_C(it.IsSuccess(), it.GetIssues().ToString());
            }

            auto it = client.StreamExecuteQuery(R"(
                SELECT * FROM `/Root/DataShard` ORDER BY Col1;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx(), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(10000))).ExtractValueSync();
                UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
                TString output = StreamResultToYson(it);
                CompareYson(output, R"([[0u;[1];["text"]];[1u;#;["test"]];[2u;[42];["t"]]])");
        }
    };

    Y_UNIT_TEST(TableSink_OltpUpdate) {
        TUpdateFromTableTester tester;
        tester.SetIsOlap(false);
        tester.Execute();
    }

    Y_UNIT_TEST(TableSink_OlapUpdate) {
        TUpdateFromTableTester tester;
        tester.SetIsOlap(true);
        tester.Execute();
    }

    class TSinkOrderTester: public TTableDataModificationTester {
    protected:
        void DoExecute() override {
            auto client = Kikimr->GetQueryClient();

            for (size_t index = 0; index < 100; ++index) {
                auto session = client.GetSession().GetValueSync().GetSession();

                auto result = session.ExecuteQuery(fmt::format(R"(
                    UPSERT INTO `/Root/DataShard` (Col1, Col2) VALUES ({}u, 0);
                )", index), NYdb::NQuery::TTxControl::BeginTx(NYdb::NQuery::TTxSettings::SerializableRW())).ExtractValueSync();
                UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());

                auto tx = result.GetTransaction();
                UNIT_ASSERT(tx);

                result = session.ExecuteQuery(fmt::format(R"(
                    INSERT INTO `/Root/DataShard` (Col1, Col2) VALUES ({}u, 0);
                )", index), NYdb::NQuery::TTxControl::Tx(*tx).CommitTx()).ExtractValueSync();
                UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::PRECONDITION_FAILED, result.GetIssues().ToString());
            }
        }
    };

    Y_UNIT_TEST(TableSink_OltpOrder) {
        TSinkOrderTester tester;
        tester.SetIsOlap(false);
        tester.Execute();
    }

    Y_UNIT_TEST(TableSink_OlapOrder) {
        TSinkOrderTester tester;
        tester.SetIsOlap(true);
        tester.Execute();
    }

    Y_UNIT_TEST(TableSink_ReplaceDuplicatesOlap) {
        NKikimrConfig::TAppConfig appConfig;
        appConfig.MutableTableServiceConfig()->SetEnableOlapSink(true);
        auto settings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetWithSampleTables(false).SetColumnShardReaderClassName("PLAIN");

        TKikimrRunner kikimr(settings);
        Tests::NCommon::TLoggerInit(kikimr).Initialize();

        auto session = kikimr.GetTableClient().CreateSession().GetValueSync().GetSession();

        const TString query = R"(
            CREATE TABLE `/Root/ColumnShard` (
                Col1 Uint64 NOT NULL,
                Col2 Int32,
                PRIMARY KEY (Col1)
            )
            PARTITION BY HASH(Col1)
            WITH (STORE = COLUMN, AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 10);
        )";

        auto result = session.ExecuteSchemeQuery(query).GetValueSync();
        UNIT_ASSERT_C(result.GetStatus() == NYdb::EStatus::SUCCESS, result.GetIssues().ToString());

        auto client = kikimr.GetQueryClient();
        {
            auto prepareResult = client.ExecuteQuery(R"(
                REPLACE INTO `/Root/ColumnShard` (Col1, Col2) VALUES
                    (100u, 1000), (100u, 1000);
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(prepareResult.IsSuccess(), prepareResult.GetIssues().ToString());
        }

        {
            auto prepareResult = client.ExecuteQuery(R"(
                REPLACE INTO `/Root/ColumnShard` (Col1, Col2) VALUES
                    (100u, 1000), (100u, 1000);
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(prepareResult.IsSuccess(), prepareResult.GetIssues().ToString());
        }

        auto it = client.StreamExecuteQuery(R"(
            SELECT * FROM `/Root/ColumnShard` ORDER BY Col1, Col2;
        )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
        TString output = StreamResultToYson(it);
        CompareYson(output, R"([[100u;[1000]]])");
    }

    Y_UNIT_TEST(TableSink_DisableSink) {
        NKikimrConfig::TAppConfig appConfig;
        appConfig.MutableTableServiceConfig()->SetEnableOlapSink(false);
        appConfig.MutableTableServiceConfig()->SetEnableOltpSink(false);
        auto settings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetWithSampleTables(false);

        TKikimrRunner kikimr(settings);
        Tests::NCommon::TLoggerInit(kikimr).Initialize();

        auto session = kikimr.GetTableClient().CreateSession().GetValueSync().GetSession();

        const TString query = R"(
            CREATE TABLE `/Root/ColumnShard` (
                Col1 Uint64 NOT NULL,
                Col2 Int32,
                PRIMARY KEY (Col1)
            )
            PARTITION BY HASH(Col1)
            WITH (STORE = COLUMN, AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 16);
        )";

        auto result = session.ExecuteSchemeQuery(query).GetValueSync();
        UNIT_ASSERT_C(result.GetStatus() == NYdb::EStatus::SUCCESS, result.GetIssues().ToString());

        auto client = kikimr.GetQueryClient();
        {
            auto prepareResult = client.ExecuteQuery(R"(
                REPLACE INTO `/Root/ColumnShard` (Col1, Col2) VALUES (1u, 1)
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT(!prepareResult.IsSuccess());
            UNIT_ASSERT_C(
                prepareResult.GetIssues().ToString().contains("Data manipulation queries do not support column shard tables."),
                prepareResult.GetIssues().ToString());
        }

        {
            auto it = client.StreamExecuteQuery(R"(
                SELECT * FROM `/Root/ColumnShard`;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
            TString output = StreamResultToYson(it);
            CompareYson(output, R"([])");
        }
    }

    Y_UNIT_TEST_TWIN(TableSink_Oltp_Replace, UseSink) {
        NKikimrConfig::TAppConfig appConfig;
        appConfig.MutableTableServiceConfig()->SetEnableOlapSink(UseSink);
        appConfig.MutableTableServiceConfig()->SetEnableOltpSink(UseSink);
        auto settings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetWithSampleTables(false);
        TKikimrRunner kikimr(settings);
        Tests::NCommon::TLoggerInit(kikimr).Initialize();

        auto session = kikimr.GetTableClient().CreateSession().GetValueSync().GetSession();

        const TString query = R"(
            CREATE TABLE `/Root/DataShard` (
                Col1 Uint32 NOT NULL,
                Col2 String,
                Col3 Int32 NOT NULL,
                PRIMARY KEY (Col1)
            )
            WITH (
                AUTO_PARTITIONING_BY_SIZE = DISABLED,
                AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 16,
                AUTO_PARTITIONING_MAX_PARTITIONS_COUNT = 16,
                UNIFORM_PARTITIONS = 16);

            CREATE TABLE `/Root/DataShard2` (
                Col1 Uint32 NOT NULL,
                Col2 String,
                Col3 Int32 NOT NULL,
                PRIMARY KEY (Col1)
            )
            WITH (
                AUTO_PARTITIONING_BY_SIZE = DISABLED,
                AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 17,
                AUTO_PARTITIONING_MAX_PARTITIONS_COUNT = 17,
                UNIFORM_PARTITIONS = 17);
        )";

        auto result = session.ExecuteSchemeQuery(query).GetValueSync();
        UNIT_ASSERT_C(result.GetStatus() == NYdb::EStatus::SUCCESS, result.GetIssues().ToString());

        auto client = kikimr.GetQueryClient();
        {
            auto prepareResult = client.ExecuteQuery(R"(
                REPLACE INTO `/Root/DataShard` (Col1, Col2, Col3) VALUES
                    (10u, "test1", 10), (20u, "test2", 11), (2147483647u, "test3", 12), (2147483640u, NULL, 13);
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx(), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(5000))).ExtractValueSync();
            UNIT_ASSERT_C(prepareResult.IsSuccess(), prepareResult.GetIssues().ToString());
        }

        {
            auto it = client.StreamExecuteQuery(R"(
                SELECT COUNT(*) FROM `/Root/DataShard`;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx(), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(5000))).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
            TString output = StreamResultToYson(it);
            CompareYson(
                output,
                R"([[4u]])");
        }

        {
            auto prepareResult = client.ExecuteQuery(R"(
                REPLACE INTO `/Root/DataShard2` SELECT * FROM `/Root/DataShard`;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx(), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(5000))).ExtractValueSync();
            UNIT_ASSERT_C(prepareResult.IsSuccess(), prepareResult.GetIssues().ToString());
        }

        {
            // Empty replace
            auto prepareResult = client.ExecuteQuery(R"(
                REPLACE INTO `/Root/DataShard2` SELECT * FROM `/Root/DataShard` WHERE Col2 == 'not exists';
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx(), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(5000))).ExtractValueSync();
            UNIT_ASSERT_C(prepareResult.IsSuccess(), prepareResult.GetIssues().ToString());
        }

        {
            auto it = client.StreamExecuteQuery(R"(
                SELECT COUNT(*) FROM `/Root/DataShard2`;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx(), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(5000))).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
            TString output = StreamResultToYson(it);
            CompareYson(
                output,
                R"([[4u]])");
        }

        {
            auto prepareResult = client.ExecuteQuery(R"(
                REPLACE INTO `/Root/DataShard2` (Col1, Col2, Col3) VALUES
                    (11u, "test1", 10), (21u, "test2", 11), (2147483646u, "test3", 12), (2147483641u, NULL, 13);
                SELECT COUNT(*) FROM `/Root/DataShard`;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx(), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(5000))).ExtractValueSync();
            UNIT_ASSERT_C(prepareResult.IsSuccess(), prepareResult.GetIssues().ToString());
        }

        {
            auto it = client.StreamExecuteQuery(R"(
                SELECT COUNT(*) FROM `/Root/DataShard2`;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx(), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(5000))).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
            TString output = StreamResultToYson(it);
            CompareYson(
                output,
                R"([[8u]])");
        }
    }

     Y_UNIT_TEST(TableSink_OltpInteractive) {
        NKikimrConfig::TAppConfig appConfig;
        appConfig.MutableTableServiceConfig()->SetEnableOlapSink(true);
        appConfig.MutableTableServiceConfig()->SetEnableOltpSink(true);
        auto settings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetWithSampleTables(false);
        TKikimrRunner kikimr(settings);
        Tests::NCommon::TLoggerInit(kikimr).Initialize();

        auto session = kikimr.GetTableClient().CreateSession().GetValueSync().GetSession();

        const TString query = R"(
            CREATE TABLE `/Root/DataShard` (
                Col1 Uint32 NOT NULL,
                Col2 String,
                Col3 Int32 NOT NULL,
                PRIMARY KEY (Col1)
            )
            WITH (
                AUTO_PARTITIONING_BY_SIZE = DISABLED,
                AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 16,
                AUTO_PARTITIONING_MAX_PARTITIONS_COUNT = 16,
                UNIFORM_PARTITIONS = 16);

            CREATE TABLE `/Root/DataShard2` (
                Col1 Uint32 NOT NULL,
                Col2 String,
                Col3 Int32 NOT NULL,
                PRIMARY KEY (Col1)
            )
            WITH (
                AUTO_PARTITIONING_BY_SIZE = DISABLED,
                AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 17,
                AUTO_PARTITIONING_MAX_PARTITIONS_COUNT = 17,
                UNIFORM_PARTITIONS = 17);
        )";

        auto result = session.ExecuteSchemeQuery(query).GetValueSync();
        UNIT_ASSERT_C(result.GetStatus() == NYdb::EStatus::SUCCESS, result.GetIssues().ToString());

        auto client = kikimr.GetQueryClient();
        auto session2 = client.GetSession().GetValueSync().GetSession();

        auto tx = session2.BeginTransaction(NYdb::NQuery::TTxSettings::SerializableRW())
                .ExtractValueSync()
                .GetTransaction();
        UNIT_ASSERT(tx.IsActive());
        {
            auto prepareResult = session2.ExecuteQuery(R"(
                REPLACE INTO `/Root/DataShard` (Col1, Col2, Col3) VALUES
                    (10u, "test1", 10), (20u, "test2", 11), (2147483647u, "test3", 12), (2147483640u, NULL, 13);
            )", TTxControl::Tx(tx), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(5000))).ExtractValueSync();
            UNIT_ASSERT_C(prepareResult.IsSuccess(), prepareResult.GetIssues().ToString());
        }

        {
            auto prepareResult = session2.ExecuteQuery(R"(
                REPLACE INTO `/Root/DataShard2` (Col1, Col2, Col3) VALUES
                    (11u, "test1", 10), (21u, "test2", 11), (2147483646u, "test3", 12), (2147483641u, NULL, 13);
            )", TTxControl::Tx(tx), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(5000))).ExtractValueSync();
            UNIT_ASSERT_C(prepareResult.IsSuccess(), prepareResult.GetIssues().ToString());
        }

        {
            auto it = session2.StreamExecuteQuery(R"(
                SELECT COUNT(*) FROM `/Root/DataShard`;
            )", TTxControl::Tx(tx), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(5000))).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
            TString output = StreamResultToYson(it);
            CompareYson(
                output,
                R"([[4u]])");
        }

        {
            auto prepareResult = session2.ExecuteQuery(R"(
                SELECT * FROM `/Root/DataShard2`;
            )", TTxControl::Tx(tx), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(5000))).ExtractValueSync();
            UNIT_ASSERT_C(prepareResult.IsSuccess(), prepareResult.GetIssues().ToString());
        }

        {
            auto prepareResult = session2.ExecuteQuery(R"(
                REPLACE INTO `/Root/DataShard2` (Col1, Col2, Col3) VALUES
                    (11u, "test1", 10), (21u, "test2", 11), (2147483646u, "test3", 12), (2147483641u, NULL, 13);
            )", TTxControl::Tx(tx), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(5000))).ExtractValueSync();
            UNIT_ASSERT_C(prepareResult.IsSuccess(), prepareResult.GetIssues().ToString());
        }

        {
            auto commitResult = tx.Commit().ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(commitResult.GetStatus(), EStatus::SUCCESS, commitResult.GetIssues().ToString());
        }

        {
            auto prepareResult = client.ExecuteQuery(R"(
                REPLACE INTO `/Root/DataShard2` SELECT * FROM `/Root/DataShard`;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx(), TExecuteQuerySettings().ClientTimeout(TDuration::MilliSeconds(5000))).ExtractValueSync();
            UNIT_ASSERT_C(prepareResult.IsSuccess(), prepareResult.GetIssues().ToString());
        }
    }

    Y_UNIT_TEST(ReadDatashardAndColumnshard) {
        NKikimrConfig::TAppConfig appConfig;
        appConfig.MutableTableServiceConfig()->SetEnableOlapSink(true);
        appConfig.MutableTableServiceConfig()->SetEnableOltpSink(true);
        appConfig.MutableTableServiceConfig()->SetEnableHtapTx(false);
        auto settings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetWithSampleTables(false);

        TKikimrRunner kikimr(settings);
        Tests::NCommon::TLoggerInit(kikimr).Initialize();

        auto client = kikimr.GetQueryClient();

        {
            auto createTable = client.ExecuteQuery(R"sql(
                CREATE TABLE `/Root/DataShard` (
                    Col1 Uint64 NOT NULL,
                    Col2 Int32,
                    Col3 String,
                    PRIMARY KEY (Col1)
                ) WITH (
                    STORE = ROW,
                    AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 10
                );
                CREATE TABLE `/Root/ColumnShard` (
                    Col1 Uint64 NOT NULL,
                    Col2 Int32,
                    Col3 String,
                    PRIMARY KEY (Col1)
                ) WITH (
                    STORE = COLUMN,
                    AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 10
                );
            )sql", NYdb::NQuery::TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_C(createTable.IsSuccess(), createTable.GetIssues().ToString());
        }

        {
            auto replaceValues = client.ExecuteQuery(R"sql(
                REPLACE INTO `/Root/DataShard` (Col1, Col2, Col3) VALUES
                    (1u, 1, "row");
            )sql", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(replaceValues.IsSuccess(), replaceValues.GetIssues().ToString());
        }

        {
            auto replaceValues = client.ExecuteQuery(R"sql(
                REPLACE INTO `/Root/ColumnShard` (Col1, Col2, Col3) VALUES
                    (2u, 2, "column");
            )sql", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(replaceValues.IsSuccess(), replaceValues.GetIssues().ToString());
        }

        {
            auto it = client.StreamExecuteQuery(R"sql(
                SELECT * FROM `/Root/ColumnShard` ORDER BY Col1;
            )sql", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
            TString output = StreamResultToYson(it);
            CompareYson(
                output,
                R"([[2u;[2];["column"]]])");
        }

        {
            auto it = client.StreamExecuteQuery(R"sql(
                SELECT * FROM `/Root/DataShard`
                UNION ALL
                SELECT * FROM `/Root/ColumnShard`
                ORDER BY Col1;
            )sql", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
            TString output = StreamResultToYson(it);
            CompareYson(
                output,
                R"([[1u;[1];["row"]];[2u;[2];["column"]]])");
        }

        {
            auto it = client.StreamExecuteQuery(R"sql(
                SELECT r.Col3 AS a, c.Col3 AS b FROM `/Root/DataShard` AS r
                JOIN `/Root/ColumnShard` AS c ON r.Col1 + 1 = c.Col1
                ORDER BY a;
            )sql", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
            TString output = StreamResultToYson(it);
            CompareYson(
                output,
                R"([[["row"];["column"]]])");
        }
    }

    Y_UNIT_TEST(ReplaceIntoWithDefaultValue) {
        NKikimrConfig::TAppConfig appConfig;
        appConfig.MutableTableServiceConfig()->SetEnableOlapSink(false);
        appConfig.MutableTableServiceConfig()->SetEnableOltpSink(false);
        auto settings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetWithSampleTables(false);

        TKikimrRunner kikimr(settings);
        Tests::NCommon::TLoggerInit(kikimr).Initialize();

        // auto session = kikimr.GetTableClient().CreateSession().GetValueSync().GetSession();
        auto client = kikimr.GetQueryClient();

        {
            auto createTable = client.ExecuteQuery(R"sql(
                CREATE TABLE `/Root/test/tb` (
                    id UInt32,
                    val UInt32 NOT NULL DEFAULT(100),
                    PRIMARY KEY(id)
                );
            )sql", NYdb::NQuery::TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_C(createTable.IsSuccess(), createTable.GetIssues().ToString());
        }

        {
            auto replaceValues = client.ExecuteQuery(R"sql(
                REPLACE INTO `/Root/test/tb` (id) VALUES
                    ( 1 )
                ;
            )sql", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(replaceValues.IsSuccess(), replaceValues.GetIssues().ToString());
        }
    }

    Y_UNIT_TEST(AlterTable_DropNotNull_Valid) {
        NKikimrConfig::TAppConfig appConfig;
        auto settings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetWithSampleTables(false);

        TKikimrRunner kikimr(settings);
        Tests::NCommon::TLoggerInit(kikimr).Initialize();

        auto client = kikimr.GetQueryClient();

        {
            auto createTable = client.ExecuteQuery(R"sql(
                CREATE TABLE `/Root/test/alterDropNotNull` (
                    id Int32 NOT NULL,
                    val Int32 NOT NULL,
                    PRIMARY KEY (id)
                );
            )sql", NYdb::NQuery::TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_C(createTable.IsSuccess(), createTable.GetIssues().ToString());
        }

        {
            auto initValues = client.ExecuteQuery(R"sql(
                REPLACE INTO `/Root/test/alterDropNotNull` (id, val)
                VALUES
                ( 1, 1 ),
                ( 2, 10 ),
                ( 3, 100 ),
                ( 4, 1000 ),
                ( 5, 10000 ),
                ( 6, 100000 ),
                ( 7, 1000000 );
            )sql", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(initValues.IsSuccess(), initValues.GetIssues().ToString());
        }

        {
            auto initNullValues = client.ExecuteQuery(R"sql(
                REPLACE INTO `/Root/test/alterDropNotNull` (id, val)
                VALUES
                ( 1, NULL ),
                ( 2, NULL );
            )sql", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(!initNullValues.IsSuccess(), initNullValues.GetIssues().ToString());
            UNIT_ASSERT_STRING_CONTAINS(initNullValues.GetIssues().ToString(), "Failed to convert type: Struct<'id':Int32,'val':Null> to Struct<'id':Int32,'val':Int32>");
        }

        {
            auto setNull = client.ExecuteQuery(R"sql(
                ALTER TABLE `/Root/test/alterDropNotNull`
                ALTER COLUMN val DROP NOT NULL;
            )sql", NYdb::NQuery::TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_C(setNull.IsSuccess(), setNull.GetIssues().ToString());
        }

        {
            auto initNullValues = client.ExecuteQuery(R"sql(
                REPLACE INTO `/Root/test/alterDropNotNull` (id, val)
                VALUES
                ( 1, NULL ),
                ( 2, NULL );
            )sql", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(initNullValues.IsSuccess(), initNullValues.GetIssues().ToString());
        }

        {
            auto getValues = client.StreamExecuteQuery(R"sql(
                SELECT *
                FROM `/Root/test/alterDropNotNull`;
            )sql", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();

            UNIT_ASSERT_VALUES_EQUAL_C(getValues.GetStatus(), EStatus::SUCCESS, getValues.GetIssues().ToString());
            CompareYson(
                StreamResultToYson(getValues),
                R"([[1;#];[2;#];[3;[100]];[4;[1000]];[5;[10000]];[6;[100000]];[7;[1000000]]])"
            );
        }
    }

    Y_UNIT_TEST(AlterTable_DropNotNull_WithSetFamily_Valid) {
        NKikimrConfig::TAppConfig appConfig;
        auto settings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetWithSampleTables(false);

        TKikimrRunner kikimr(settings);
        Tests::NCommon::TLoggerInit(kikimr).Initialize();

        auto client = kikimr.GetQueryClient();

        {
            auto createTable = client.ExecuteQuery(R"sql(
                CREATE TABLE `/Root/test/alterDropNotNullWithSetFamily` (
                    id Int32 NOT NULL,
                    val1 Int32 FAMILY Family1 NOT NULL,
                    val2 Int32,
                    PRIMARY KEY (id),

                    FAMILY default (
                        DATA = "test",
                        COMPRESSION = "lz4"
                    ),
                    FAMILY Family1 (
                        DATA = "test",
                        COMPRESSION = "off"
                    )
                );
            )sql", NYdb::NQuery::TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_C(createTable.IsSuccess(), createTable.GetIssues().ToString());
        }

        {
            auto initValues = client.ExecuteQuery(R"sql(
                REPLACE INTO `/Root/test/alterDropNotNullWithSetFamily` (id, val1, val2)
                VALUES
                ( 1, 1, 1 ),
                ( 2, 10, 10 ),
                ( 3, 100, 100 ),
                ( 4, 1000, 1000 ),
                ( 5, 10000, 10000 ),
                ( 6, 100000, 100000 ),
                ( 7, 1000000, 1000000 );
            )sql", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(initValues.IsSuccess(), initValues.GetIssues().ToString());
        }

        {
            auto initNullValues = client.ExecuteQuery(R"sql(
                REPLACE INTO `/Root/test/alterDropNotNullWithSetFamily` (id, val1, val2)
                VALUES
                ( 1, NULL, 1 ),
                ( 2, NULL, 2 );
            )sql", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(!initNullValues.IsSuccess(), initNullValues.GetIssues().ToString());
            UNIT_ASSERT_STRING_CONTAINS(initNullValues.GetIssues().ToString(), "Failed to convert type: Struct<'id':Int32,'val1':Null,'val2':Int32> to Struct<'id':Int32,'val1':Int32,'val2':Int32?>");
        }

        {
            auto setNull = client.ExecuteQuery(R"sql(
                ALTER TABLE `/Root/test/alterDropNotNullWithSetFamily`
                ALTER COLUMN val1 DROP NOT NULL;
            )sql", NYdb::NQuery::TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_C(setNull.IsSuccess(), setNull.GetIssues().ToString());
        }

        {
            auto setNull = client.ExecuteQuery(R"sql(
                ALTER TABLE `/Root/test/alterDropNotNullWithSetFamily`
                ALTER COLUMN val1 SET FAMILY Family1;
            )sql", NYdb::NQuery::TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_C(setNull.IsSuccess(), setNull.GetIssues().ToString());
        }

        {
            auto initNullValues = client.ExecuteQuery(R"sql(
                REPLACE INTO `/Root/test/alterDropNotNullWithSetFamily` (id, val1, val2)
                VALUES
                ( 1, NULL, 1 ),
                ( 2, NULL, 2 );
            )sql", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(initNullValues.IsSuccess(), initNullValues.GetIssues().ToString());
        }

        {
            auto getValues = client.StreamExecuteQuery(R"sql(
                SELECT *
                FROM `/Root/test/alterDropNotNullWithSetFamily`;
            )sql", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();

            UNIT_ASSERT_VALUES_EQUAL_C(getValues.GetStatus(), EStatus::SUCCESS, getValues.GetIssues().ToString());
            CompareYson(
                StreamResultToYson(getValues),
                R"([[1;#;[1]];[2;#;[2]];[3;[100];[100]];[4;[1000];[1000]];[5;[10000];[10000]];[6;[100000];[100000]];[7;[1000000];[1000000]]])"
            );
        }
    }


    void RunQuery (const TString& query, auto& session, bool expectOk = true) {
        auto qResult = session.ExecuteQuery(query, NYdb::NQuery::TTxControl::NoTx()).ExtractValueSync();
        if (!qResult.IsSuccess()) {
            Cerr << "Query failed, status: " << qResult.GetStatus() << ": " << qResult.GetIssues().ToString() << Endl;
        }
        UNIT_ASSERT(qResult.IsSuccess() == expectOk);
    };

    struct TEntryCheck {
        NYdb::NScheme::ESchemeEntryType Type;
        TString Name;
        bool IsExpected;
        bool WasFound = false;
    };

    TEntryCheck ExpectedTopic(const TString& name) {
        return TEntryCheck{NYdb::NScheme::ESchemeEntryType::Topic, name, true};
    }
    TEntryCheck UnexpectedTopic(const TString& name) {
        return TEntryCheck{NYdb::NScheme::ESchemeEntryType::Topic, name, false};
    }

    void CheckDirEntry(TKikimrRunner& kikimr, TVector<TEntryCheck>& entriesToCheck) {
        auto res = kikimr.GetSchemeClient().ListDirectory("/Root").GetValueSync();
        for (const auto& entry : res.GetChildren()) {
            Cerr << "Scheme entry: " << entry << Endl;
            for (auto& checkEntry : entriesToCheck) {
                if (checkEntry.Name != entry.Name)
                    continue;
                if (checkEntry.IsExpected) {
                    UNIT_ASSERT_C(entry.Type == checkEntry.Type, checkEntry.Name);
                    checkEntry.WasFound = true;
                } else {
                    UNIT_ASSERT_C(entry.Type != checkEntry.Type, checkEntry.Name);
                }
            }
        }
        for (auto& checkEntry : entriesToCheck) {
            if (checkEntry.IsExpected) {
                UNIT_ASSERT_C(checkEntry.WasFound, checkEntry.Name);
            }
        }
    }

    Y_UNIT_TEST(CreateAndDropTopic) {
        NKikimrConfig::TAppConfig appConfig;
        auto setting = NKikimrKqp::TKqpSetting();
        auto serverSettings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetKqpSettings({setting});
        serverSettings.PQConfig.SetRequireCredentialsInNewProtocol(false);
        TKikimrRunner kikimr(
            serverSettings.SetWithSampleTables(false).SetEnableTempTables(true));
        auto client = kikimr.GetQueryClient();
        auto session = client.GetSession().GetValueSync().GetSession();
        auto pq = NYdb::NTopic::TTopicClient(kikimr.GetDriver(),
                                             NYdb::NTopic::TTopicClientSettings().Database("/Root").AuthToken("root@builtin"));

        {
            const auto queryCreateTopic = Q_(R"(
                --!syntax_v1
                CREATE TOPIC `/Root/TempTopic` (CONSUMER cons1);
            )");
            RunQuery(queryCreateTopic, session);
            Cerr << "Topic created\n";
            auto desc = pq.DescribeTopic("/Root/TempTopic").ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL(desc.GetTopicDescription().GetConsumers().size(), 1);
        }
        {
            const auto queryCreateTopic = Q_(R"(
                --!syntax_v1
                CREATE TOPIC IF NOT EXISTS `/Root/TempTopic` (CONSUMER cons1, CONSUMER cons2);
            )");
            RunQuery(queryCreateTopic, session);
            auto desc = pq.DescribeTopic("/Root/TempTopic").ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL(desc.GetTopicDescription().GetConsumers().size(), 1);
        }
        {
            const auto queryCreateTopic = Q_(R"(
                --!syntax_v1
                CREATE TOPIC `/Root/TempTopic` (CONSUMER cons1, CONSUMER cons2, CONSUMER cons3);
            )");
            RunQuery(queryCreateTopic, session, false);
            auto desc = pq.DescribeTopic("/Root/TempTopic").ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL(desc.GetTopicDescription().GetConsumers().size(), 1);
        }

        TVector<TEntryCheck> entriesToCheck = {ExpectedTopic("TempTopic")};
        CheckDirEntry(kikimr, entriesToCheck);
        {
            const auto query = Q_(R"(
                --!syntax_v1
                Drop TOPIC `/Root/TempTopic`;
            )");
            RunQuery(query, session);
            Cerr << "Topic dropped\n";
            TVector<TEntryCheck> entriesToCheck = {UnexpectedTopic("TempTopic")};
            CheckDirEntry(kikimr, entriesToCheck);
        }
        {
            const auto query = Q_(R"(
                --!syntax_v1
                Drop TOPIC IF EXISTS `/Root/TempTopic`;
            )");
            RunQuery(query, session);
        }
        {
            const auto query = Q_(R"(
                --!syntax_v1
                Drop TOPIC `/Root/TempTopic`;
            )");
            RunQuery(query, session, false);
        }
    }

    Y_UNIT_TEST(CreateAndAlterTopic) {
        NKikimrConfig::TAppConfig appConfig;
        auto setting = NKikimrKqp::TKqpSetting();
        auto serverSettings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetKqpSettings({setting});
        TKikimrRunner kikimr{serverSettings};
        auto client = kikimr.GetQueryClient(NYdb::NQuery::TClientSettings{}.AuthToken("root@builtin"));
        auto session = client.GetSession().GetValueSync().GetSession();
        auto pq = NYdb::NTopic::TTopicClient(kikimr.GetDriver(),
                                             NYdb::NTopic::TTopicClientSettings().Database("/Root").AuthToken("root@builtin"));

        {
            const auto queryCreateTopic = Q_(R"(
                --!syntax_v1
                CREATE TOPIC `/Root/TempTopic` (CONSUMER cons1);
            )");
            RunQuery(queryCreateTopic, session);

            auto desc = pq.DescribeTopic("/Root/TempTopic").ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL(desc.GetTopicDescription().GetPartitioningSettings().GetMinActivePartitions(), 1);
        }
        {
            const auto query = Q_(R"(
                --!syntax_v1
                ALTER TOPIC `/Root/TempTopic` SET (min_active_partitions = 10);
            )");
            RunQuery(query, session);
            auto desc = pq.DescribeTopic("/Root/TempTopic").ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL(desc.GetTopicDescription().GetPartitioningSettings().GetMinActivePartitions(), 10);
        }
        {
            const auto query = Q_(R"(
                --!syntax_v1
                ALTER TOPIC IF EXISTS `/Root/TempTopic` SET (min_active_partitions = 15);
            )");
            RunQuery(query, session);
            auto desc = pq.DescribeTopic("/Root/TempTopic").ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL(desc.GetTopicDescription().GetPartitioningSettings().GetMinActivePartitions(), 15);
        }

        {
            const auto query = Q_(R"(
                --!syntax_v1
                ALTER TOPIC `/Root/NoSuchTopic` SET (min_active_partitions = 10);
            )");
            RunQuery(query, session, false);

            TVector<TEntryCheck> entriesToCheck = {UnexpectedTopic("NoSuchTopic")};
            CheckDirEntry(kikimr, entriesToCheck);
        }
        {
            const auto query = Q_(R"(
                --!syntax_v1
                ALTER TOPIC IF EXISTS `/Root/NoSuchTopic` SET (min_active_partitions = 10);
            )");
            RunQuery(query, session);
            TVector<TEntryCheck> entriesToCheck = {UnexpectedTopic("NoSuchTopic")};
            CheckDirEntry(kikimr, entriesToCheck);
        }
    }

    Y_UNIT_TEST(CreateOrDropTopicOverTable) {
        NKikimrConfig::TAppConfig appConfig;
        auto setting = NKikimrKqp::TKqpSetting();
        auto serverSettings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetKqpSettings({setting});
        TKikimrRunner kikimr{serverSettings};
        auto tableClient = kikimr.GetTableClient();

        {
            auto tcSession = tableClient.CreateSession().GetValueSync().GetSession();
            UNIT_ASSERT(tcSession.ExecuteSchemeQuery(R"(
                CREATE TABLE `/Root/TmpTable` (
                    Key Uint64,
                    Value String,
                    PRIMARY KEY (Key)
                );
            )").GetValueSync().IsSuccess());
            tcSession.Close();
        }

        auto client = kikimr.GetQueryClient(NYdb::NQuery::TClientSettings{}.AuthToken("root@builtin"));
        auto session = client.GetSession().GetValueSync().GetSession();

        TVector<TEntryCheck> entriesToCheck = {TEntryCheck{.Type = NYdb::NScheme::ESchemeEntryType::Table,
                                                           .Name = "TmpTable", .IsExpected = true}};
        {
            const auto queryCreateTopic = Q_(R"(
                --!syntax_v1
                CREATE TOPIC `/Root/TmpTable` (CONSUMER cons1);
            )");
            RunQuery(queryCreateTopic, session, false);
            CheckDirEntry(kikimr, entriesToCheck);

        }
        {
            const auto queryCreateTopic = Q_(R"(
                --!syntax_v1
                CREATE TOPIC IF NOT EXISTS `/Root/TmpTable` (CONSUMER cons1);
            )");
            RunQuery(queryCreateTopic, session, false);
            CheckDirEntry(kikimr, entriesToCheck);
        }
        {
            const auto queryDropTopic = Q_(R"(
                --!syntax_v1
                DROP TOPIC `/Root/TmpTable`;
            )");
            RunQuery(queryDropTopic, session, false);
        }
        {
            const auto queryDropTopic = Q_(R"(
                --!syntax_v1
                DROP TOPIC IF EXISTS `/Root/TmpTable`;
            )");
            RunQuery(queryDropTopic, session, false);
            CheckDirEntry(kikimr, entriesToCheck);
        }
        {
            auto tcSession = tableClient.CreateSession().GetValueSync().GetSession();
            auto type = TTypeBuilder().BeginOptional().Primitive(EPrimitiveType::Uint64).EndOptional().Build();
            auto alter = NYdb::NTable::TAlterTableSettings().AppendAddColumns(TColumn("NewColumn", type));

            auto alterResult = tcSession.AlterTable("/Root/TmpTable", alter
                            ).GetValueSync();

            UNIT_ASSERT_VALUES_EQUAL(alterResult.GetStatus(), EStatus::SUCCESS);
        }
    }

    Y_UNIT_TEST(AlterCdcTopic) {
        NKikimrConfig::TAppConfig appConfig;
        auto setting = NKikimrKqp::TKqpSetting();
        auto serverSettings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetKqpSettings({setting});
        TKikimrRunner kikimr{serverSettings};
        auto tableClient = kikimr.GetTableClient();

        {
            auto tcSession = tableClient.CreateSession().GetValueSync().GetSession();
            UNIT_ASSERT(tcSession.ExecuteSchemeQuery(R"(
                CREATE TABLE `/Root/TmpTable` (
                    Key Uint64,
                    Value String,
                    PRIMARY KEY (Key)
                );
            )").GetValueSync().IsSuccess());

            UNIT_ASSERT(tcSession.ExecuteSchemeQuery(R"(
                ALTER TABLE `/Root/TmpTable` ADD CHANGEFEED `feed` WITH (
                    MODE = 'KEYS_ONLY', FORMAT = 'JSON'
                    );
            )").GetValueSync().IsSuccess());
            tcSession.Close();
        }

        auto pq = NYdb::NTopic::TTopicClient(kikimr.GetDriver(),
                                            NYdb::NTopic::TTopicClientSettings().Database("/Root").AuthToken("root@builtin"));

        auto client = kikimr.GetQueryClient(NYdb::NQuery::TClientSettings{}.AuthToken("root@builtin"));
        auto session = client.GetSession().GetValueSync().GetSession();
        {

            const auto query = Q_(R"(
                --!syntax_v1
                ALTER TOPIC `/Root/TmpTable/feed` ADD CONSUMER consumer21;
            )");

            RunQuery(query, session);
            auto desc = pq.DescribeTopic("/Root/TmpTable/feed").ExtractValueSync();
            const auto& consumers = desc.GetTopicDescription().GetConsumers();
            UNIT_ASSERT_VALUES_EQUAL(consumers.size(), 1);
            UNIT_ASSERT_VALUES_EQUAL(consumers[0].GetConsumerName(), "consumer21");

        }
        {
            const auto query = Q_(R"(
                --!syntax_v1
                ALTER TOPIC `/Root/TmpTable/feed` SET (min_active_partitions = 10);
            )");
            RunQuery(query, session, false);
            auto desc = pq.DescribeTopic("/Root/TmpTable/feed").ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL(desc.GetTopicDescription().GetPartitioningSettings().GetMinActivePartitions(), 1);
        }

    }

    Y_UNIT_TEST(TableSink_OlapRWQueries) {
        NKikimrConfig::TAppConfig appConfig;
        appConfig.MutableTableServiceConfig()->SetEnableOlapSink(true);
        auto settings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetWithSampleTables(false);
        TKikimrRunner kikimr(settings);
        Tests::NCommon::TLoggerInit(kikimr).Initialize();

        auto session = kikimr.GetTableClient().CreateSession().GetValueSync().GetSession();

        const TString query = R"(
            CREATE TABLE `/Root/ColumnShard` (
                Col1 Uint64 NOT NULL,
                Col2 String,
                Col3 Int32 NOT NULL,
                PRIMARY KEY (Col1)
            )
            PARTITION BY HASH(Col1)
            WITH (STORE = COLUMN, AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 3);
        )";

        auto result = session.ExecuteSchemeQuery(query).GetValueSync();
        UNIT_ASSERT_C(result.GetStatus() == NYdb::EStatus::SUCCESS, result.GetIssues().ToString());

        auto client = kikimr.GetQueryClient();
        {
            auto result = client.ExecuteQuery(R"(
                REPLACE INTO `/Root/ColumnShard` (Col1, Col2, Col3) VALUES
                    (1u, "test1", 10), (2u, "test2", 11), (3u, "test3", 12), (4u, NULL, 13);
                SELECT * FROM `/Root/ColumnShard` ORDER BY Col1;
                INSERT INTO `/Root/ColumnShard` SELECT Col1 + 100 AS Col1, Col2, Col3 FROM `/Root/ColumnShard`;
                SELECT * FROM `/Root/ColumnShard` ORDER BY Col1;
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
            CompareYson(R"([[1u;["test1"];10];[2u;["test2"];11];[3u;["test3"];12];[4u;#;13]])", FormatResultSetYson(result.GetResultSet(0)));
            CompareYson(R"([[1u;["test1"];10];[2u;["test2"];11];[3u;["test3"];12];[4u;#;13];[101u;["test1"];10];[102u;["test2"];11];[103u;["test3"];12];[104u;#;13]])", FormatResultSetYson(result.GetResultSet(1)));
        }
    }

    Y_UNIT_TEST(MixedReadQueryWithoutStreamLookup) {
        NKikimrConfig::TAppConfig appConfig;
        appConfig.MutableTableServiceConfig()->SetEnableOlapSink(true);
        appConfig.MutableTableServiceConfig()->SetEnableOltpSink(false);
        appConfig.MutableTableServiceConfig()->SetEnableHtapTx(false);

        auto settings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetWithSampleTables(false);

        TKikimrRunner kikimr(settings);
        Tests::NCommon::TLoggerInit(kikimr).Initialize();

        auto client = kikimr.GetQueryClient();

        {
            auto createTable = client.ExecuteQuery(R"sql(
                CREATE TABLE `/Root/DataShard` (
                    Col1 Uint64 NOT NULL,
                    Col2 Int32 NOT NULL,
                    Col3 String,
                    PRIMARY KEY (Col1, Col2)
                ) WITH (STORE = ROW);
                CREATE TABLE `/Root/ColumnShard` (
                    Col1 Uint64 NOT NULL,
                    Col2 Int32 NOT NULL,
                    Col3 String,
                    PRIMARY KEY (Col1, Col2)
                ) WITH (STORE = COLUMN);
            )sql", NYdb::NQuery::TTxControl::NoTx()).ExtractValueSync();
            UNIT_ASSERT_C(createTable.IsSuccess(), createTable.GetIssues().ToString());
        }

        {
            auto replaceValues = client.ExecuteQuery(R"sql(
                REPLACE INTO `/Root/DataShard` (Col1, Col2, Col3) VALUES
                    (1u, 1, "row"), (1u, 2, "row"), (1u, 3, "row"), (2u, 3, "row");
            )sql", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(replaceValues.IsSuccess(), replaceValues.GetIssues().ToString());
        }
        {
            auto replaceValues = client.ExecuteQuery(R"sql(
                REPLACE INTO `/Root/ColumnShard` (Col1, Col2, Col3) VALUES
                    (1u, 1, "row"), (1u, 2, "row"), (1u, 3, "row"), (2u, 3, "row");
            )sql", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(replaceValues.IsSuccess(), replaceValues.GetIssues().ToString());
        }

        {
            auto it = client.StreamExecuteQuery(R"sql(
                SELECT Col3 FROM `/Root/DataShard` WHERE Col1 = 1u
                UNION ALL
                SELECT Col3 FROM `/Root/ColumnShard` WHERE Col1 = 1u;
            )sql", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
            TString output = StreamResultToYson(it);
            CompareYson(
                output,
                R"([[["row"]];[["row"]];[["row"]];[["row"]];[["row"]];[["row"]]])");
        }

        {
            auto it = client.StreamExecuteQuery(R"sql(
                SELECT r.Col3
                FROM `/Root/DataShard` AS r
                JOIN `/Root/ColumnShard` AS c
                ON r.Col1 = c.Col1;
            )sql", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString());
            TString output = StreamResultToYson(it);
            CompareYson(
                output,
                R"([[["row"]];[["row"]];[["row"]];[["row"]];[["row"]];[["row"]];[["row"]];[["row"]];[["row"]];[["row"]]])");
        }
    }

    Y_UNIT_TEST(ReadManyRanges) {
        NKikimrConfig::TAppConfig appConfig;
        auto settings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetWithSampleTables(false);

        TKikimrRunner kikimr(settings);
        Tests::NCommon::TLoggerInit(kikimr).Initialize();

        auto session = kikimr.GetTableClient().CreateSession().GetValueSync().GetSession();

        const TString query = R"(
            CREATE TABLE `/Root/DataShard` (
                Col1 String,
                Col2 String,
                Col3 String,
                PRIMARY KEY (Col1, Col2)
            )
            WITH (
                STORE = ROW,
                AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 10,
                PARTITION_AT_KEYS = (("a"), ("b"), ("c"), ("d"), ("e"), ("f"), ("g"), ("h"), ("k"), ("p"), ("q"), ("x"))
            );
        )";

        auto result = session.ExecuteSchemeQuery(query).GetValueSync();
        UNIT_ASSERT_C(result.GetStatus() == NYdb::EStatus::SUCCESS, result.GetIssues().ToString());

        auto client = kikimr.GetQueryClient();

        {
            auto prepareResult = client.ExecuteQuery(R"(
                UPSERT INTO `/Root/DataShard` (Col1, Col2) VALUES ("y", "1") , ("y", "2"), ("d", "1"), ("b", "1"), ("k", "1"), ("q", "1"), ("p", "1");
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(prepareResult.IsSuccess(), prepareResult.GetIssues().ToString());
        }

        {
            auto result = client.ExecuteQuery(R"(
                SELECT COUNT(*) FROM `/Root/DataShard` WHERE Col1 IN ("d", "b", "k", "q", "p") OR (Col1 = "y" AND Col2 = "2");
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
            CompareYson(R"([[6u]])", FormatResultSetYson(result.GetResultSet(0)));
        }

        {
            auto result = client.ExecuteQuery(R"(
                SELECT COUNT(*) FROM `/Root/DataShard` WHERE Col1 = "y";
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
            CompareYson(R"([[2u]])", FormatResultSetYson(result.GetResultSet(0)));
        }
    }

    Y_UNIT_TEST(ReadManyShardsRange) {
        NKikimrConfig::TAppConfig appConfig;
        auto settings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetWithSampleTables(false);

        TKikimrRunner kikimr(settings);
        Tests::NCommon::TLoggerInit(kikimr).Initialize();

        auto session = kikimr.GetTableClient().CreateSession().GetValueSync().GetSession();

        const TString query = R"(
            CREATE TABLE `/Root/DataShard` (
                Col1 String,
                Col2 String,
                Col3 String,
                PRIMARY KEY (Col1, Col2)
            )
            WITH (
                STORE = ROW,
                AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 10,
                PARTITION_AT_KEYS = (("a", "0"), ("b", "b"), ("c", "d"))
            );
        )";

        auto result = session.ExecuteSchemeQuery(query).GetValueSync();
        UNIT_ASSERT_C(result.GetStatus() == NYdb::EStatus::SUCCESS, result.GetIssues().ToString());

        auto client = kikimr.GetQueryClient();

        {
            auto prepareResult = client.ExecuteQuery(R"(
                UPSERT INTO `/Root/DataShard` (Col1, Col2) VALUES ("a", "a") , ("c", "c"), ("d", "d");
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(prepareResult.IsSuccess(), prepareResult.GetIssues().ToString());
        }

        {
            auto result = client.ExecuteQuery(R"(
                SELECT COUNT(*) FROM `/Root/DataShard` WHERE  "a" <= Col1 AND Col1 <= "c";
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
            CompareYson(R"([[2u]])", FormatResultSetYson(result.GetResultSet(0)));
        }
    }

    Y_UNIT_TEST(ReadManyRangesAndPoints) {
        NKikimrConfig::TAppConfig appConfig;
        auto settings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetWithSampleTables(false);

        TKikimrRunner kikimr(settings);
        Tests::NCommon::TLoggerInit(kikimr).Initialize();

        auto session = kikimr.GetTableClient().CreateSession().GetValueSync().GetSession();

        const TString query = R"(
            CREATE TABLE `/Root/DataShard` (
                Col1 String,
                Col2 String,
                PRIMARY KEY (Col1)
            )
            WITH (
                STORE = ROW,
                AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 10,
                PARTITION_AT_KEYS = (("a"), ("b"), ("c"), ("d"), ("e"), ("f"), ("g"), ("h"), ("i"))
            );
        )";

        auto result = session.ExecuteSchemeQuery(query).GetValueSync();
        UNIT_ASSERT_C(result.GetStatus() == NYdb::EStatus::SUCCESS, result.GetIssues().ToString());

        auto client = kikimr.GetQueryClient();

        {
            auto prepareResult = client.ExecuteQuery(R"(
                UPSERT INTO `/Root/DataShard` (Col1, Col2) VALUES ("a", "a") , ("c", "c"), ("e", "e");
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(prepareResult.IsSuccess(), prepareResult.GetIssues().ToString());
        }

        {
            auto result = client.ExecuteQuery(R"(
                SELECT Col2 FROM `/Root/DataShard` WHERE
                    ('f' <= Col1 AND Col1 <= 'g') OR
                    ('h' <= Col1 AND Col1 <= 'i') OR
                    ('j' <= Col1 AND Col1 <= 'k') OR
                    ('l' <= Col1 AND Col1 <= 'm') OR
                    Col1 == "a" OR
                    Col1 == "c" OR
                    Col1 == "e" OR
                    Col1 == "";
            )", NYdb::NQuery::TTxControl::BeginTx().CommitTx()).ExtractValueSync();
            UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
        }
    }

    Y_UNIT_TEST_TWIN(LargeUpsert, UseSink) {
        NKikimrConfig::TAppConfig appConfig;
        appConfig.MutableTableServiceConfig()->SetEnableOltpSink(UseSink);
        auto settings = TKikimrSettings()
            .SetAppConfig(appConfig)
            .SetWithSampleTables(false);

        TKikimrRunner kikimr(settings);
        Tests::NCommon::TLoggerInit(kikimr).Initialize();


        {
            auto session = kikimr.GetTableClient().CreateSession().GetValueSync().GetSession();

            const TString query = Sprintf(R"(
                CREATE TABLE `/Root/DataShard` (
                    Key Uint64 NOT NULL,
                    Value String,
                    PRIMARY KEY (Key)
                ) WITH (
                    AUTO_PARTITIONING_BY_SIZE = DISABLED,
                    AUTO_PARTITIONING_MIN_PARTITIONS_COUNT = 64,
                    AUTO_PARTITIONING_MAX_PARTITIONS_COUNT = 64,
                    UNIFORM_PARTITIONS = 64
                );
            )");
            auto result = session.ExecuteSchemeQuery(query).GetValueSync();
            UNIT_ASSERT_C(result.GetStatus() == NYdb::EStatus::SUCCESS, result.GetIssues().ToString());
        }

        {
            auto client = kikimr.GetQueryClient();
            auto session = client.GetSession().GetValueSync().GetSession();
            auto tx = session.BeginTransaction(TTxSettings::SerializableRW()).ExtractValueSync().GetTransaction();

            const int batchesCount = 5;
            const int rowsCount = 48'000;
            const int rowLen = 1000;

            for (int i = 0; i < batchesCount; ++i) {
                TParamsBuilder params;
                auto& rowsParam = params.AddParam("$rows");
                rowsParam.BeginList();
                for (int rowIndex = 0; rowIndex < rowsCount; ++rowIndex) {
                    rowsParam.AddListItem()
                        .BeginStruct()
                        .AddMember("Key").Uint64(rowIndex + rowsCount * i)
                        .AddMember("Value").String(TString(rowLen, '?'))
                        .EndStruct();
                }
                rowsParam.EndList();
                rowsParam.Build();

                auto query = R"(
                    PRAGMA kikimr.KqpForceImmediateEffectsExecution="true";

                    DECLARE $rows AS List<Struct<
                        Key: Uint64,
                        Value: String
                    >>;

                    UPSERT INTO `/Root/DataShard`
                    SELECT * FROM AS_TABLE($rows);
                )";

                auto result = session.ExecuteQuery(query, TTxControl::Tx(tx), params.Build()).GetValueSync();
                UNIT_ASSERT_C(result.IsSuccess(), result.GetIssues().ToString());
                Sleep(TDuration::MilliSeconds(500));
            }

            auto commitResult = tx.Commit().GetValueSync();
            UNIT_ASSERT_C(commitResult.IsSuccess(), commitResult.GetIssues().ToString());
        }

        {
            auto client = kikimr.GetQueryClient();
            auto result = client.ExecuteQuery(Q_(R"(
                SELECT COUNT(*) FROM `/Root/DataShard`;
            )"), TTxControl::BeginTx(TTxSettings::SerializableRW()).CommitTx()).ExtractValueSync();
            UNIT_ASSERT(result.IsSuccess());
            CompareYson(R"([[240000u]])", FormatResultSetYson(result.GetResultSet(0)));
        }
    }
}

} // namespace NKqp
} // namespace NKikimr
