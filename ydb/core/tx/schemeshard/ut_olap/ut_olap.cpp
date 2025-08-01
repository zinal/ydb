#include <ydb/core/tx/schemeshard/ut_helpers/helpers.h>
#include <ydb/core/tx/columnshard/columnshard.h>
#include <ydb/core/tx/columnshard/test_helper/columnshard_ut_common.h>
#include <ydb/core/tx/columnshard/hooks/testing/controller.h>
#include <ydb/core/formats/arrow/arrow_helpers.h>
#include <ydb/core/formats/arrow/arrow_batch_builder.h>
#include <ydb/core/protos/table_stats.pb.h>

using namespace NKikimr::NSchemeShard;
using namespace NKikimr;
using namespace NKikimrSchemeOp;
using namespace NSchemeShardUT_Private;

namespace NKikimr {
namespace {

namespace NTypeIds = NScheme::NTypeIds;
using TTypeInfo = NScheme::TTypeInfo;

static const TString defaultStoreSchema = R"(
    Name: "OlapStore"
    ColumnShardCount: 1
    SchemaPresets {
        Name: "default"
        Schema {
            Columns { Name: "timestamp" Type: "Timestamp" NotNull: true }
            Columns { Name: "data" Type: "Utf8" }
            KeyColumnNames: "timestamp"
        }
    }
)";

static const TString invalidStoreSchema = R"(
    Name: "OlapStore"
    ColumnShardCount: 1
    SchemaPresets {
        Name: "default"
        Schema {
            Columns { Name: "timestamp" Type: "Timestamp" NotNull: true }
            Columns { Name: "data" Type: "Utf8" }
            Columns { Name: "mess age" Type: "Utf8" }
            KeyColumnNames: "timestamp"
        }
    }
)";

static const TString defaultTableName = "ColumnTable";
static const TString defaultTableSchema = R"(
    Name: "ColumnTable"
    ColumnShardCount: 1
    Schema {
        Columns { Name: "timestamp" Type: "Timestamp" NotNull: true }
        Columns { Name: "data" Type: "Utf8" }
        KeyColumnNames: "timestamp"
    }
)";

static const TVector<NArrow::NTest::TTestColumn> defaultYdbSchema = {
    NArrow::NTest::TTestColumn("timestamp", TTypeInfo(NTypeIds::Timestamp)).SetNullable(false),
    NArrow::NTest::TTestColumn("data", TTypeInfo(NTypeIds::Utf8) )
};

static const TString tableSchemaFormat = R"(
    Name: "TestTable"
    Schema {
        Columns {
            Name: "Id"
            Type: "Int32"
            NotNull: True
        }
        Columns {
            Name: "%s"
            Type: "Utf8"
        }
        KeyColumnNames: ["Id"]
    }
)";

#define DEBUG_HINT (TStringBuilder() << "at line " << __LINE__)

NLs::TCheckFunc LsCheckDiskQuotaExceeded(
    bool expectExceeded = true,
    const TString& debugHint = ""
) {
    return [=] (const NKikimrScheme::TEvDescribeSchemeResult& record) {
        auto& desc = record.GetPathDescription().GetDomainDescription();
        UNIT_ASSERT_VALUES_EQUAL_C(
            desc.GetDomainState().GetDiskQuotaExceeded(),
            expectExceeded,
            debugHint << ", subdomain's disk space usage:\n" << desc.GetDiskSpaceUsage().DebugString()
        );
    };
}

void CheckQuotaExceedance(TTestActorRuntime& runtime,
                          ui64 schemeShard,
                          const TString& pathToSubdomain,
                          bool expectExceeded,
                          const TString& debugHint = ""
) {
    TestDescribeResult(DescribePath(runtime, schemeShard, pathToSubdomain), {
        LsCheckDiskQuotaExceeded(expectExceeded, debugHint)
    });
}

NKikimrTxDataShard::TEvPeriodicTableStats WaitTableStats(TTestActorRuntime& runtime, ui64 columnShardId, ui64 minPartCount = 0) {
    NKikimrTxDataShard::TEvPeriodicTableStats stats;
    bool captured = false;

    auto observer = runtime.AddObserver<TEvDataShard::TEvPeriodicTableStats>([&](const auto& event) {
            const auto& record = event->Get()->Record;
            if (record.GetDatashardId() == columnShardId && record.GetTableStats().GetPartCount() >= minPartCount) {
                stats = record;
                captured = true;
            }
        }
    );

    for (int i = 0; i < 5 && !captured; ++i) {
        TDispatchOptions options;
        options.CustomFinalCondition = [&]() { return captured; };
        runtime.DispatchEvents(options, TDuration::Seconds(5));
    }

    observer.Remove();

    UNIT_ASSERT(captured);

    return stats;
}
}}

Y_UNIT_TEST_SUITE(TOlap) {
    Y_UNIT_TEST(CreateStore) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime);
        ui64 txId = 100;

        const TString& olapSchema = defaultStoreSchema;

        TestCreateOlapStore(runtime, ++txId, "/MyRoot", olapSchema);
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/OlapStore", false, NLs::PathExist);

        TString olapSchema1 = R"(
            Name: "OlapStore1"
            ColumnShardCount: 1
            SchemaPresets {
                Name: "default"
                Schema {
                    Columns { Name: "timestamp" Type: "Timestamp" NotNull: true }
                    Columns { Name: "data" Type: "Utf8" }
                    KeyColumnNames: "timestamp"
                }
            }
        )";

        TestCreateOlapStore(runtime, ++txId, "/MyRoot", olapSchema1, {NKikimrScheme::StatusAccepted});
    }

    Y_UNIT_TEST(CreateStoreWithDirs) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime);
        ui64 txId = 100;

        TestCreateOlapStore(runtime, ++txId, "/MyRoot", R"(
            Name: "DirA/DirB/OlapStore"
            ColumnShardCount: 1
            SchemaPresets {
                Name: "default"
                Schema {
                    Columns { Name: "timestamp" Type: "Timestamp" NotNull: true }
                    Columns { Name: "data" Type: "Utf8" }
                    KeyColumnNames: "timestamp"
                }
            }
        )");
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/DirA/DirB/OlapStore", false, NLs::PathExist);
    }

    Y_UNIT_TEST(CreateTableWithNullableKeysNotAllowed) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime);
        ui64 txId = 100;

        auto& appData = runtime.GetAppData();
        appData.ColumnShardConfig.SetAllowNullableColumnsInPK(false);

        TestCreateOlapStore(runtime, ++txId, "/MyRoot", R"(
            Name: "MyStore"
            ColumnShardCount: 1
            SchemaPresets {
                Name: "default"
                Schema {
                    Columns { Name: "timestamp" Type: "Timestamp" NotNull: true }
                    Columns { Name: "key1" Type: "Uint32" }
                    Columns { Name: "data" Type: "Utf8" }
                    KeyColumnNames: [ "timestamp", "key1" ]
                }
            }
        )", {NKikimrScheme::StatusSchemeError});
        env.TestWaitNotification(runtime, txId);
    }

    Y_UNIT_TEST(CreateTableWithNullableKeys) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime);
        ui64 txId = 100;

        auto& appData = runtime.GetAppData();
        appData.ColumnShardConfig.SetAllowNullableColumnsInPK(true);

        TestCreateOlapStore(runtime, ++txId, "/MyRoot", R"(
            Name: "MyStore"
            ColumnShardCount: 1
            SchemaPresets {
                Name: "default"
                Schema {
                    Columns { Name: "timestamp" Type: "Timestamp" NotNull: true }
                    Columns { Name: "key1" Type: "Uint32" }
                    Columns { Name: "data" Type: "Utf8" }
                    KeyColumnNames: [ "timestamp", "key1" ]
                }
            }
        )");
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/MyStore", false, NLs::PathExist);

        TestMkDir(runtime, ++txId, "/MyRoot", "MyDir");
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/MyDir", false, NLs::PathExist);

        const auto expectedTablePathId = GetNextLocalPathId(runtime, txId);
        TestCreateColumnTable(runtime, ++txId, "/MyRoot/MyDir", R"(
            Name: "MyTable"
            ColumnShardCount: 1
            Schema {
                Columns { Name: "timestamp" Type: "Timestamp" NotNull: true }
                Columns { Name: "key1" Type: "Uint32" }
                Columns { Name: "data" Type: "Utf8" }
                KeyColumnNames: [ "timestamp", "key1" ]
            }
        )");
        env.TestWaitNotification(runtime, txId);

        TestLsPathId(runtime, expectedTablePathId, NLs::PathStringEqual("/MyRoot/MyDir/MyTable"));

        TestDropColumnTable(runtime, ++txId, "/MyRoot/MyDir", "MyTable");
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/MyDir/MyTable", false, NLs::PathNotExist);
        TestLsPathId(runtime, expectedTablePathId, NLs::PathStringEqual(""));
    }

    Y_UNIT_TEST(CreateTable) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime);
        ui64 txId = 100;

        const TString& olapSchema = defaultStoreSchema;

        TestCreateOlapStore(runtime, ++txId, "/MyRoot", olapSchema);
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/OlapStore", false, NLs::PathExist);

        TestMkDir(runtime, ++txId, "/MyRoot/OlapStore", "MyDir");
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/OlapStore/MyDir", false, NLs::PathExist);

        TString tableSchema = R"(
            Name: "ColumnTable"
            ColumnShardCount: 1
        )";

        TestCreateColumnTable(runtime, ++txId, "/MyRoot/OlapStore/MyDir", tableSchema);
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/OlapStore/MyDir/ColumnTable", false, NLs::All(
            NLs::PathExist,
            NLs::HasColumnTableSchemaPreset("default")));

        // Missing column from schema preset
        TestCreateColumnTable(runtime, ++txId, "/MyRoot/OlapStore/MyDir", R"(
            Name: "ColumnTableMissingDataColumn"
            ColumnShardCount: 1
            Schema {
                Columns { Name: "timestamp" Type: "Timestamp" }
                KeyColumnNames: "timestamp"
            }
        )", {NKikimrScheme::StatusSchemeError});

        // Extra column not in schema preset
        TestCreateColumnTable(runtime, ++txId, "/MyRoot/OlapStore/MyDir", R"(
            Name: "ColumnTableExtraColumn"
            ColumnShardCount: 1
            Schema {
                Columns { Name: "timestamp" Type: "Timestamp" }
                Columns { Name: "data" Type: "Utf8" }
                Columns { Name: "comment" Type: "Utf8" }
                KeyColumnNames: "timestamp"
            }
        )", {NKikimrScheme::StatusSchemeError});

        // Different column order
        TestCreateColumnTable(runtime, ++txId, "/MyRoot/OlapStore/MyDir", R"(
            Name: "ColumnTableDifferentColumnOrder"
            ColumnShardCount: 1
            Schema {
                Columns { Name: "data" Type: "Utf8" }
                Columns { Name: "timestamp" Type: "Timestamp" }
                KeyColumnNames: "timestamp"
            }
        )", {NKikimrScheme::StatusSchemeError});

        // Extra key column
        TestCreateColumnTable(runtime, ++txId, "/MyRoot/OlapStore/MyDir", R"(
            Name: "ColumnTableExtraKeyColumn"
            ColumnShardCount: 1
            Schema {
                Columns { Name: "timestamp" Type: "Timestamp" }
                Columns { Name: "data" Type: "Utf8" }
                KeyColumnNames: "timestamp"
                KeyColumnNames: "data"
            }
        )", {NKikimrScheme::StatusSchemeError});

        // Unknown key column
        TestCreateColumnTable(runtime, ++txId, "/MyRoot/OlapStore/MyDir", R"(
            Name: "ColumnTableUnknownKeyColumn"
            ColumnShardCount: 1
            Schema {
                Columns { Name: "timestamp" Type: "Timestamp" }
                Columns { Name: "data" Type: "Utf8" }
                KeyColumnNames: "nottimestamp"
            }
        )", {NKikimrScheme::StatusSchemeError});

        // Different data column type
        TestCreateColumnTable(runtime, ++txId, "/MyRoot/OlapStore/MyDir", R"(
            Name: "ColumnTableDataColumnType"
            ColumnShardCount: 1
            Schema {
                Columns { Name: "timestamp" Type: "Timestamp" }
                Columns { Name: "data" Type: "String" }
                KeyColumnNames: "timestamp"
            }
        )", {NKikimrScheme::StatusSchemeError});

        // Repeating preset schema should succeed
        TestCreateColumnTable(runtime, ++txId, "/MyRoot/OlapStore/MyDir", R"(
            Name: "ColumnTableExplicitSchema"
            ColumnShardCount: 1
            Schema {
                Columns { Name: "timestamp" Type: "Timestamp" }
                Columns { Name: "data" Type: "Utf8" }
                KeyColumnNames: "timestamp"
            }
        )");
        env.TestWaitNotification(runtime, txId);

        // Creating table with directories should succeed
        TestCreateColumnTable(runtime, ++txId, "/MyRoot/OlapStore", R"(
            Name: "DirA/DirB/NestedTable"
            ColumnShardCount: 1
        )");
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/OlapStore/DirA/DirB/NestedTable", false, NLs::All(
            NLs::PathExist,
            NLs::HasColumnTableSchemaPreset("default")));

        // Additional storage tier in schema
        TestCreateColumnTable(runtime, ++txId, "/MyRoot/OlapStore/MyDir", R"(
            Name: "TableWithTiers"
            ColumnShardCount: 1
            Schema {
                Columns { Name: "timestamp" Type: "Timestamp" }
                Columns { Name: "data" Type: "Utf8" }
                KeyColumnNames: "timestamp"
            }
        )", {NKikimrScheme::StatusAccepted});
    }

    Y_UNIT_TEST(CustomDefaultPresets) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime);
        ui64 txId = 100;

        const TString& olapSchema = defaultStoreSchema;

        TestCreateOlapStore(runtime, ++txId, "/MyRoot", olapSchema);
        env.TestWaitNotification(runtime, txId);

        TestCreateColumnTable(runtime, ++txId, "/MyRoot/OlapStore", R"(
            Name: "ColumnTable"
            ColumnShardCount: 1
            SchemaPresetName: "default"
        )");
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/OlapStore/ColumnTable", false, NLs::All(
            NLs::PathExist,
            NLs::HasColumnTableSchemaPreset("default")));
    }

    Y_UNIT_TEST(CreateDropTable) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime);
        ui64 txId = 100;

        const TString& olapSchema = defaultStoreSchema;

        const auto expectedStorePathId = GetNextLocalPathId(runtime, txId);
        TestCreateOlapStore(runtime, ++txId, "/MyRoot", olapSchema);
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/OlapStore", false, NLs::PathExist);
        TestLsPathId(runtime, expectedStorePathId, NLs::PathStringEqual("/MyRoot/OlapStore"));

        TestMkDir(runtime, ++txId, "/MyRoot/OlapStore", "MyDir");
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/OlapStore/MyDir", false, NLs::PathExist);

        TString tableSchema = R"(
            Name: "ColumnTable"
            ColumnShardCount: 1
        )";

        const auto expectedTablePathId = GetNextLocalPathId(runtime, txId);
        TestCreateColumnTable(runtime, ++txId, "/MyRoot/OlapStore/MyDir", tableSchema);
        env.TestWaitNotification(runtime, txId);

        TestLsPathId(runtime, expectedTablePathId, NLs::PathStringEqual("/MyRoot/OlapStore/MyDir/ColumnTable"));

        TestDropColumnTable(runtime, ++txId, "/MyRoot/OlapStore/MyDir", "ColumnTable");
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/OlapStore/MyDir/ColumnTable", false, NLs::PathNotExist);
        TestLsPathId(runtime, expectedTablePathId, NLs::PathStringEqual(""));

        TestDropOlapStore(runtime, ++txId, "/MyRoot", "OlapStore", {NKikimrScheme::StatusNameConflict});
        TestRmDir(runtime, ++txId, "/MyRoot/OlapStore", "MyDir");
        env.TestWaitNotification(runtime, txId);

        TestDropOlapStore(runtime, ++txId, "/MyRoot", "OlapStore");
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/OlapStore", false, NLs::PathNotExist);
        TestLsPathId(runtime, expectedStorePathId, NLs::PathStringEqual(""));
    }

    Y_UNIT_TEST(CreateDropStandaloneTable) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime);
        ui64 txId = 100;

        TestMkDir(runtime, ++txId, "/MyRoot", "MyDir");
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/MyDir", false, NLs::PathExist);

        auto expectedTablePathId = GetNextLocalPathId(runtime, txId);
        TestCreateColumnTable(runtime, ++txId, "/MyRoot/MyDir", defaultTableSchema);
        env.TestWaitNotification(runtime, txId);

        TestLsPathId(runtime, expectedTablePathId, NLs::PathStringEqual("/MyRoot/MyDir/ColumnTable"));

        TestDropColumnTable(runtime, ++txId, "/MyRoot/MyDir", "ColumnTable");
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/MyDir/ColumnTable", false, NLs::PathNotExist);
        TestLsPathId(runtime, expectedTablePathId, NLs::PathStringEqual(""));

        // PARTITION BY ()

        TString otherSchema = R"(
            Name: "ColumnTable"
            ColumnShardCount: 4
            Schema {
                Columns { Name: "timestamp" Type: "Timestamp" NotNull: true }
                Columns { Name: "some" Type: "Uint64" NotNull: true }
                Columns { Name: "data" Type: "Utf8" NotNull: true }
                KeyColumnNames: "some"
                KeyColumnNames: "data"
            }
            Sharding {
                HashSharding {
                    Columns: ["some", "data"]
                }
            }
        )";

        expectedTablePathId = GetNextLocalPathId(runtime, txId);
        TestCreateColumnTable(runtime, ++txId, "/MyRoot/MyDir", otherSchema);
        env.TestWaitNotification(runtime, txId);

        auto checkFn = [&](const NKikimrScheme::TEvDescribeSchemeResult& record) {
            UNIT_ASSERT_VALUES_EQUAL(record.GetPath(), "/MyRoot/MyDir/ColumnTable");

            auto& sharding = record.GetPathDescription().GetColumnTableDescription().GetSharding();
            UNIT_ASSERT_VALUES_EQUAL(sharding.ColumnShardsSize(), 4);
            UNIT_ASSERT(sharding.HasHashSharding());
            auto& hashSharding = sharding.GetHashSharding();
            UNIT_ASSERT_VALUES_EQUAL(hashSharding.ColumnsSize(), 2);
            UNIT_ASSERT_EQUAL(hashSharding.GetFunction(),
                              NKikimrSchemeOp::TColumnTableSharding::THashSharding::HASH_FUNCTION_CONSISTENCY_64);
            UNIT_ASSERT_VALUES_EQUAL(hashSharding.GetColumns()[0], "some");
            UNIT_ASSERT_VALUES_EQUAL(hashSharding.GetColumns()[1], "data");
        };

        TestLsPathId(runtime, expectedTablePathId, checkFn);

        TestDropColumnTable(runtime, ++txId, "/MyRoot/MyDir", "ColumnTable");
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/MyDir/ColumnTable", false, NLs::PathNotExist);
        TestLsPathId(runtime, expectedTablePathId, NLs::PathStringEqual(""));
    }

    Y_UNIT_TEST(CreateDropStandaloneTableDefaultSharding) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime);
        ui64 txId = 100;

        TestMkDir(runtime, ++txId, "/MyRoot", "MyDir");
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/MyDir", false, NLs::PathExist);

        auto expectedTablePathId = GetNextLocalPathId(runtime, txId);
        TestCreateColumnTable(runtime, ++txId, "/MyRoot/MyDir", defaultTableSchema);
        env.TestWaitNotification(runtime, txId);

        TestLsPathId(runtime, expectedTablePathId, NLs::PathStringEqual("/MyRoot/MyDir/ColumnTable"));

        TestDropColumnTable(runtime, ++txId, "/MyRoot/MyDir", "ColumnTable");
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/MyDir/ColumnTable", false, NLs::PathNotExist);
        TestLsPathId(runtime, expectedTablePathId, NLs::PathStringEqual(""));

        TString otherSchema = R"(
            Name: "ColumnTable"
            Schema {
                Columns { Name: "timestamp" Type: "Timestamp" NotNull: true }
                Columns { Name: "some" Type: "Uint64" NotNull: true }
                Columns { Name: "data" Type: "Utf8" NotNull: true }
                KeyColumnNames: "some"
                KeyColumnNames: "data"
            }
        )";

        expectedTablePathId = GetNextLocalPathId(runtime, txId);
        TestCreateColumnTable(runtime, ++txId, "/MyRoot/MyDir", otherSchema);
        env.TestWaitNotification(runtime, txId);

        auto checkFn = [&](const NKikimrScheme::TEvDescribeSchemeResult& record) {
            UNIT_ASSERT_VALUES_EQUAL(record.GetPath(), "/MyRoot/MyDir/ColumnTable");

            auto& description = record.GetPathDescription().GetColumnTableDescription();
            UNIT_ASSERT_VALUES_EQUAL(description.GetColumnShardCount(), 64);

            auto& sharding = description.GetSharding();
            UNIT_ASSERT_VALUES_EQUAL(sharding.ColumnShardsSize(), 64);
            UNIT_ASSERT(sharding.HasHashSharding());
            auto& hashSharding = sharding.GetHashSharding();
            UNIT_ASSERT_VALUES_EQUAL(hashSharding.ColumnsSize(), 2);
            UNIT_ASSERT_EQUAL(hashSharding.GetFunction(),
                              NKikimrSchemeOp::TColumnTableSharding::THashSharding::HASH_FUNCTION_CONSISTENCY_64);
            UNIT_ASSERT_VALUES_EQUAL(hashSharding.GetColumns()[0], "some");
            UNIT_ASSERT_VALUES_EQUAL(hashSharding.GetColumns()[1], "data");
        };

        TestLsPathId(runtime, expectedTablePathId, checkFn);

        TestDropColumnTable(runtime, ++txId, "/MyRoot/MyDir", "ColumnTable");
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/MyDir/ColumnTable", false, NLs::PathNotExist);
        TestLsPathId(runtime, expectedTablePathId, NLs::PathStringEqual(""));
    }

    Y_UNIT_TEST(CreateTableTtl) {
        TTestBasicRuntime runtime;
        TTestEnvOptions options;
        options.EnableTieringInColumnShard(true);
        options.RunFakeConfigDispatcher(true);
        TTestEnv env(runtime, options);
        ui64 txId = 100;

        TestCreateOlapStore(runtime, ++txId, "/MyRoot", defaultStoreSchema);
        env.TestWaitNotification(runtime, txId);

        TString tableSchema1 = R"(
            Name: "Table1"
            ColumnShardCount: 1
            TtlSettings {
                Enabled { ColumnName: "timestamp" ExpireAfterSeconds: 300 }
            }
        )";

        TestCreateColumnTable(runtime, ++txId, "/MyRoot/OlapStore", tableSchema1);
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/OlapStore/Table1", false, NLs::All(
            NLs::HasColumnTableSchemaPreset("default"),
            NLs::HasColumnTableSchemaVersion(1),
            NLs::HasColumnTableTtlSettingsVersion(1),
            NLs::HasColumnTableTtlSettingsEnabled("timestamp", TDuration::Seconds(300))));

        TString tableSchema2 = R"(
            Name: "Table2"
            ColumnShardCount: 1
            TtlSettings {
                Disabled {}
            }
        )";

        TestCreateColumnTable(runtime, ++txId, "/MyRoot/OlapStore", tableSchema2);
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/OlapStore/Table2", false, NLs::All(
            NLs::HasColumnTableSchemaPreset("default"),
            NLs::HasColumnTableSchemaVersion(1),
            NLs::HasColumnTableTtlSettingsVersion(1),
            NLs::HasColumnTableTtlSettingsDisabled()));

        TestCreateExternalDataSource(runtime, ++txId, "/MyRoot", R"(
            Name: "Tier1"
            SourceType: "ObjectStorage"
            Location: "http://fake.fake/fake"
            Auth: {
                Aws: {
                    AwsAccessKeyIdSecretName: "secret"
                    AwsSecretAccessKeySecretName: "secret"
                }
            }
        )");
        env.TestWaitNotification(runtime, txId);

        TString tableSchema3 = R"(
            Name: "Table3"
            ColumnShardCount: 1
            TtlSettings {
                Enabled: {
                    ColumnName: "timestamp"
                    ColumnUnit: UNIT_AUTO
                    Tiers: {
                        ApplyAfterSeconds: 360
                        EvictToExternalStorage {
                            Storage: "/MyRoot/Tier1"
                        }
                    }
                }
            }
        )";

        TestCreateColumnTable(runtime, ++txId, "/MyRoot/OlapStore", tableSchema3);
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/OlapStore/Table3", false, NLs::All(
            NLs::HasColumnTableSchemaPreset("default"),
            NLs::HasColumnTableSchemaVersion(1),
            NLs::HasColumnTableTtlSettingsVersion(1),
            NLs::HasColumnTableTtlSettingsTier("timestamp", TDuration::Seconds(360), "/MyRoot/Tier1")));

        TString tableSchema4 = R"(
            Name: "Table4"
            ColumnShardCount: 1
            TtlSettings {
                Enabled: {
                    ColumnName: "timestamp"
                    ColumnUnit: UNIT_AUTO
                    Tiers: {
                        ApplyAfterSeconds: 3600000000
                        EvictToExternalStorage {
                            Storage: "/MyRoot/Tier1"
                        }
                    }
                }
            }
        )";

        TestCreateColumnTable(runtime, ++txId, "/MyRoot/OlapStore", tableSchema4,
                            {NKikimrScheme::StatusAccepted});
    }

    Y_UNIT_TEST(AlterStore) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime);
        ui64 txId = 100;

        const TString& olapSchema = defaultStoreSchema;

        TestCreateOlapStore(runtime, ++txId, "/MyRoot", olapSchema);
        env.TestWaitNotification(runtime, txId);

        TString tableSchemaX = R"(
            Name: "ColumnTable"
            ColumnShardCount: 1
            TtlSettings {
                Enabled {
                    ExpireAfterSeconds: 300
                }
            }
        )";

        TestCreateColumnTable(runtime, ++txId, "/MyRoot/OlapStore", tableSchemaX,
                            {NKikimrScheme::StatusSchemeError});

        TString tableSchema = R"(
            Name: "ColumnTable"
            ColumnShardCount: 1
            TtlSettings {
                Enabled {
                    ColumnName: "timestamp"
                    ExpireAfterSeconds: 300
                }
            }
        )";

        TestCreateColumnTable(runtime, ++txId, "/MyRoot/OlapStore", tableSchema);
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/OlapStore/ColumnTable", false, NLs::All(
            NLs::HasColumnTableSchemaPreset("default"),
            NLs::HasColumnTableSchemaVersion(1),
            NLs::HasColumnTableTtlSettingsVersion(1),
            NLs::HasColumnTableTtlSettingsEnabled("timestamp", TDuration::Seconds(300))));

        TestAlterOlapStore(runtime, ++txId, "/MyRoot", R"(
            Name: "OlapStore"
            AlterSchemaPresets {
                Name: "default"
                AlterSchema {
                    AddColumns { Name: "comment" Type: "Utf8" }
                }
            }
        )", {NKikimrScheme::StatusAccepted});

        env.TestWaitNotification(runtime, txId);
        TestAlterOlapStore(runtime, ++txId, "/MyRoot", R"(
            Name: "OlapStore"
            AlterSchemaPresets {
                Name: "default"
                AlterSchema {
                    AlterColumns { Name: "comment" DefaultValue: "10" }
                }
            }
        )", {NKikimrScheme::StatusSchemeError});
    }

    Y_UNIT_TEST(AlterTtl) {
        TTestBasicRuntime runtime;
        TTestEnvOptions options;
        options.EnableTieringInColumnShard(true);
        options.RunFakeConfigDispatcher(true);
        TTestEnv env(runtime, options);
        ui64 txId = 100;

        TString olapSchema = R"(
            Name: "OlapStore"
            ColumnShardCount: 1
            SchemaPresets {
                Name: "default"
                Schema {
                    Columns { Name: "timestamp" Type: "Timestamp" NotNull: true }
                    Columns { Name: "data" Type: "Utf8" }
                    KeyColumnNames: "timestamp"
                }
            }
        )";

        TestCreateOlapStore(runtime, ++txId, "/MyRoot", olapSchema);
        env.TestWaitNotification(runtime, txId);

        TString tableSchema = R"(
            Name: "ColumnTable"
            ColumnShardCount: 1
            TtlSettings {
                Enabled {
                    ColumnName: "timestamp"
                    ExpireAfterSeconds: 300
                }
            }
        )";

        TestCreateColumnTable(runtime, ++txId, "/MyRoot/OlapStore", tableSchema);
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/OlapStore/ColumnTable", false, NLs::All(
            NLs::HasColumnTableSchemaPreset("default"),
            NLs::HasColumnTableSchemaVersion(1),
            NLs::HasColumnTableTtlSettingsVersion(1),
            NLs::HasColumnTableTtlSettingsEnabled("timestamp", TDuration::Seconds(300))));

        TestAlterColumnTable(runtime, ++txId, "/MyRoot/OlapStore", R"(
            Name: "ColumnTable"
            AlterTtlSettings {
                Enabled {
                    ColumnName: "timestamp"
                    ExpireAfterSeconds: 600
                }
            }
        )");
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/OlapStore/ColumnTable", false, NLs::All(
            NLs::HasColumnTableSchemaPreset("default"),
            NLs::HasColumnTableSchemaVersion(1),
            NLs::HasColumnTableTtlSettingsVersion(2),
            NLs::HasColumnTableTtlSettingsEnabled("timestamp", TDuration::Seconds(600))));

        TestAlterColumnTable(runtime, ++txId, "/MyRoot/OlapStore", R"(
            Name: "ColumnTable"
            AlterTtlSettings {
                Disabled {}
            }
        )");
        env.TestWaitNotification(runtime, txId);

        TestCreateExternalDataSource(runtime, ++txId, "/MyRoot", R"(
            Name: "Tier1"
            SourceType: "ObjectStorage"
            Location: "http://fake.fake/fake"
            Auth: {
                Aws: {
                    AwsAccessKeyIdSecretName: "secret"
                    AwsSecretAccessKeySecretName: "secret"
                }
            }
        )");
        env.TestWaitNotification(runtime, txId);

        TestAlterColumnTable(runtime, ++txId, "/MyRoot/OlapStore", R"(
            Name: "ColumnTable"
            AlterTtlSettings {
                Enabled: {
                    ColumnName: "timestamp"
                    ColumnUnit: UNIT_AUTO
                    Tiers: {
                        ApplyAfterSeconds: 3600000000
                        EvictToExternalStorage {
                            Storage: "/MyRoot/Tier1"
                        }
                    }
                }
            }
        )");
        env.TestWaitNotification(runtime, txId);
    }

    Y_UNIT_TEST(StoreStats) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime);
        runtime.SetLogPriority(NKikimrServices::TX_COLUMNSHARD, NActors::NLog::PRI_DEBUG);
        runtime.UpdateCurrentTime(TInstant::Now() - TDuration::Seconds(600));

        auto csController = NYDBTest::TControllers::RegisterCSControllerGuard<NYDBTest::NColumnShard::TController>();
        csController->SetOverridePeriodicWakeupActivationPeriod(TDuration::Seconds(1));
        csController->SetOverrideLagForCompactionBeforeTierings(TDuration::Seconds(1));

        // disable stats batching
        auto& appData = runtime.GetAppData();
        appData.SchemeShardConfig.SetStatsBatchTimeoutMs(0);
        appData.SchemeShardConfig.SetStatsMaxBatchSize(0);

        // apply config via reboot
        TActorId sender = runtime.AllocateEdgeActor();
        GracefulRestartTablet(runtime, TTestTxConfig::SchemeShard, sender);

        ui64 txId = 100;

        const TString& olapSchema = defaultStoreSchema;

        const auto expectedStorePathId = GetNextLocalPathId(runtime, txId);
        TestCreateOlapStore(runtime, ++txId, "/MyRoot", olapSchema);
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/OlapStore", false, NLs::PathExist);
        TestLsPathId(runtime, expectedStorePathId, NLs::PathStringEqual("/MyRoot/OlapStore"));

        TString tableSchema = R"(
            Name: "ColumnTable"
            ColumnShardCount: 1
        )";

        const auto expectedTablePathId = GetNextLocalPathId(runtime, txId);
        TestCreateColumnTable(runtime, ++txId, "/MyRoot/OlapStore", tableSchema);
        env.TestWaitNotification(runtime, txId);

        ui64 pathId = 0;
        ui64 shardId = 0;
        NTxUT::TPlanStep planStep;
        auto checkFn = [&](const NKikimrScheme::TEvDescribeSchemeResult& record) {
            auto& self = record.GetPathDescription().GetSelf();
            pathId = self.GetPathId();
            txId = self.GetCreateTxId() + 1;
            planStep =  NTxUT::TPlanStep{self.GetCreateStep()};
            auto& sharding = record.GetPathDescription().GetColumnTableDescription().GetSharding();
            UNIT_ASSERT_VALUES_EQUAL(sharding.ColumnShardsSize(), 1);
            shardId = sharding.GetColumnShards()[0];
            UNIT_ASSERT_VALUES_EQUAL(record.GetPath(), "/MyRoot/OlapStore/ColumnTable");
        };

        TestLsPathId(runtime, expectedTablePathId, checkFn);
        UNIT_ASSERT(shardId);
        UNIT_ASSERT(pathId);
        UNIT_ASSERT(planStep.Val());
        {
            auto description = DescribePrivatePath(runtime, TTestTxConfig::SchemeShard, "/MyRoot/OlapStore/ColumnTable", true, true);
            Cerr << description.DebugString() << Endl;
            auto& tabletStats = description.GetPathDescription().GetTableStats();

            UNIT_ASSERT(description.GetPathDescription().HasTableStats());
            UNIT_ASSERT_EQUAL(tabletStats.GetRowCount(), 0);
            UNIT_ASSERT_EQUAL(tabletStats.GetDataSize(), 0);
        }


        ui32 rowsInBatch = 100000;

        {   // Write data directly into shard
            TActorId sender = runtime.AllocateEdgeActor();
            TString data = NTxUT::MakeTestBlob({0, rowsInBatch}, defaultYdbSchema, {}, { "timestamp" });

            //some immidiate writes
            ui64 writeId = 0;
            for (ui32 i = 0; i < 10; ++i) {
                std::vector<ui64> writeIds;
                ++txId;
                NTxUT::WriteData(runtime, sender, shardId, ++writeId, pathId, data, defaultYdbSchema, &writeIds, NEvWrite::EModificationType::Upsert, 0);
            }

            // emulate timeout
            runtime.UpdateCurrentTime(TInstant::Now());

            // trigger periodic stats at shard (after timeout)
            std::vector<ui64> writeIds;
            ++txId;
            NTxUT::WriteData(runtime, sender, shardId, ++writeId, pathId, data, defaultYdbSchema, &writeIds, NEvWrite::EModificationType::Upsert, txId);
            planStep = NTxUT::ProposeCommit(runtime, sender, shardId, txId, writeIds, txId);
            NTxUT::PlanCommit(runtime, sender, shardId, planStep, { txId });
        }
        {
            auto description = DescribePrivatePath(runtime, TTestTxConfig::SchemeShard, "/MyRoot/OlapStore", true, true);
            Cerr << description.DebugString() << Endl;
            auto& tabletStats = description.GetPathDescription().GetTableStats();

            UNIT_ASSERT_GT(tabletStats.GetRowCount(), 0);
            UNIT_ASSERT_GT(tabletStats.GetDataSize(), 0);
            UNIT_ASSERT_GT(tabletStats.GetPartCount(), 0);
            UNIT_ASSERT_GT(tabletStats.GetRowUpdates(), 0);
            UNIT_ASSERT_EQUAL(tabletStats.GetImmediateTxCompleted(), 10);
            UNIT_ASSERT_EQUAL(tabletStats.GetPlannedTxCompleted(), 2);
            UNIT_ASSERT_GT(tabletStats.GetLastAccessTime(), 0);
            UNIT_ASSERT_GT(tabletStats.GetLastUpdateTime(), 0);
        }

        {
            auto description = DescribePrivatePath(runtime, TTestTxConfig::SchemeShard, "/MyRoot/OlapStore/ColumnTable", true, true);
            Cerr << description.DebugString() << Endl;
            auto& tabletStats = description.GetPathDescription().GetTableStats();

            UNIT_ASSERT_GT(tabletStats.GetRowCount(), 0);
            UNIT_ASSERT_GT(tabletStats.GetDataSize(), 0);
            UNIT_ASSERT_GT(tabletStats.GetPartCount(), 0);
            UNIT_ASSERT_GT(tabletStats.GetLastAccessTime(), 0);
            UNIT_ASSERT_GT(tabletStats.GetLastUpdateTime(), 0);
        }

#if 0
        TestDropColumnTable(runtime, ++txId, "/MyRoot/OlapStore", "ColumnTable");
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/OlapStore/ColumnTable", false, NLs::PathNotExist);
        TestLsPathId(runtime, 3, NLs::PathStringEqual(""));

        TestDropOlapStore(runtime, ++txId, "/MyRoot", "OlapStore");
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/OlapStore", false, NLs::PathNotExist);
        TestLsPathId(runtime, 2, NLs::PathStringEqual(""));
#endif
    }

    Y_UNIT_TEST(Decimal) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime, TTestEnvOptions().EnableParameterizedDecimal(true));
        ui64 txId = 100;

        TestCreateOlapStore(runtime, ++txId, "/MyRoot", R"_(
            Name: "OlapStore"
            ColumnShardCount: 1
            SchemaPresets {
                Name: "default"
                Schema {
                    Columns { Name: "timestamp" Type: "Timestamp" NotNull: true }
                    Columns { Name: "data" Type: "Decimal(35,9)" }
                    KeyColumnNames: "timestamp"
                }
            }
        )_");
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/OlapStore", false, NLs::PathExist);
    }

    Y_UNIT_TEST(StoreStatsQuota) {
        TTestBasicRuntime runtime;

        TTestEnvOptions opts;
        opts.DisableStatsBatching(true);
        opts.EnablePersistentPartitionStats(true);
        opts.EnableTopicDiskSubDomainQuota(false);

        TTestEnv env(runtime, opts);
        runtime.SetLogPriority(NKikimrServices::TX_COLUMNSHARD, NActors::NLog::PRI_DEBUG);
        runtime.SetLogPriority(NKikimrServices::TX_COLUMNSHARD_COMPACTION, NActors::NLog::PRI_DEBUG);
        runtime.UpdateCurrentTime(TInstant::Now() - TDuration::Seconds(600));

        auto csController = NYDBTest::TControllers::RegisterCSControllerGuard<NYDBTest::NColumnShard::TController>();
        csController->DisableBackground(NKikimr::NYDBTest::ICSController::EBackground::Compaction);
        csController->SetOverridePeriodicWakeupActivationPeriod(TDuration::Seconds(1));
        csController->SetOverrideLagForCompactionBeforeTierings(TDuration::Seconds(1));
        csController->SetOverrideMaxReadStaleness(TDuration::Seconds(1));

        // disable stats batching
        auto& appData = runtime.GetAppData();
        appData.SchemeShardConfig.SetStatsBatchTimeoutMs(0);
        appData.SchemeShardConfig.SetStatsMaxBatchSize(0);

        // apply config via reboot
        TActorId sender = runtime.AllocateEdgeActor();
        GracefulRestartTablet(runtime, TTestTxConfig::SchemeShard, sender);

        constexpr const char* databaseDescription = R"(
            DatabaseQuotas {
                data_size_hard_quota: 1000000
                data_size_soft_quota: 900000
            }
        )";

        ui64 txId = 100;

        TestCreateSubDomain(runtime, ++txId,  "/MyRoot", TStringBuilder() << R"(
                Name: "SomeDatabase"
            )" << databaseDescription
        );

        const TString& olapSchema = defaultStoreSchema;

        const auto expectedStorePathId = GetNextLocalPathId(runtime, txId);
        TestCreateOlapStore(runtime, ++txId, "/MyRoot/SomeDatabase", olapSchema);
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/SomeDatabase/OlapStore", false, NLs::PathExist);
        TestLsPathId(runtime, expectedStorePathId, NLs::PathStringEqual("/MyRoot/SomeDatabase/OlapStore"));

        TString tableSchema = R"(
            Name: "ColumnTable"
            ColumnShardCount: 1
        )";

        const auto expectedTablePathId = GetNextLocalPathId(runtime, txId);
        TestCreateColumnTable(runtime, ++txId, "/MyRoot/SomeDatabase/OlapStore", tableSchema);
        env.TestWaitNotification(runtime, txId);

        ui64 pathId = 0;
        ui64 shardId = 0;
        NTxUT::TPlanStep planStep;
        auto checkFn = [&](const NKikimrScheme::TEvDescribeSchemeResult& record) {
            auto& self = record.GetPathDescription().GetSelf();
            pathId = self.GetPathId();
            txId = self.GetCreateTxId() + 1;
            planStep = NTxUT::TPlanStep{self.GetCreateStep()};
            auto& sharding = record.GetPathDescription().GetColumnTableDescription().GetSharding();
            UNIT_ASSERT_VALUES_EQUAL(sharding.ColumnShardsSize(), 1);
            shardId = sharding.GetColumnShards()[0];
            UNIT_ASSERT_VALUES_EQUAL(record.GetPath(), "/MyRoot/SomeDatabase/OlapStore/ColumnTable");
        };

        TestLsPathId(runtime, expectedTablePathId, checkFn);
        UNIT_ASSERT(shardId);
        UNIT_ASSERT(pathId);
        UNIT_ASSERT(planStep.Val());
        {
            auto description = DescribePrivatePath(runtime, TTestTxConfig::SchemeShard, "/MyRoot/SomeDatabase/OlapStore/ColumnTable", true, true);
            Cerr << description.DebugString() << Endl;
            auto& tabletStats = description.GetPathDescription().GetTableStats();

            UNIT_ASSERT(description.GetPathDescription().HasTableStats());
            UNIT_ASSERT_EQUAL(tabletStats.GetRowCount(), 0);
            UNIT_ASSERT_EQUAL(tabletStats.GetDataSize(), 0);
        }

        CheckQuotaExceedance(runtime, TTestTxConfig::SchemeShard, "/MyRoot/SomeDatabase", false, DEBUG_HINT);

        ui32 rowsInBatch = 100000;
        ui64 writeId = 0;
        TString data;

        {   // Write data directly into shard
            TActorId sender = runtime.AllocateEdgeActor();
            data = NTxUT::MakeTestBlob({0, rowsInBatch}, defaultYdbSchema, {}, { "timestamp" });
            TSet<ui64> txIds;
            for (ui32 i = 0; i < 100; ++i) {
                std::vector<ui64> writeIds;
                ++txId;
                NTxUT::WriteData(runtime, sender, shardId, ++writeId, pathId, data, defaultYdbSchema, &writeIds, NEvWrite::EModificationType::Upsert, txId);
                planStep = NTxUT::ProposeCommit(runtime, sender, shardId, txId, writeIds, txId);
                txIds.insert(txId);
            }

            NTxUT::PlanCommit(runtime, sender, shardId, planStep, txIds);

            WaitTableStats(runtime, shardId);
            CheckQuotaExceedance(runtime, TTestTxConfig::SchemeShard, "/MyRoot/SomeDatabase", true, DEBUG_HINT);

            // Check that writes will fail if quota is exceeded
            std::vector<ui64> writeIds;
            ++txId;
            AFL_VERIFY(!NTxUT::WriteData(runtime, sender, shardId, ++writeId, pathId, data, defaultYdbSchema, &writeIds, NEvWrite::EModificationType::Upsert, txId));
            NTxUT::ProposeCommitFail(runtime, sender, shardId, txId, writeIds, txId);
        }

        {
            auto description = DescribePrivatePath(runtime, TTestTxConfig::SchemeShard, "/MyRoot/SomeDatabase/OlapStore", true, true);
            Cerr << description.DebugString() << Endl;
            auto& tabletStats = description.GetPathDescription().GetTableStats();

            UNIT_ASSERT_GT(tabletStats.GetRowCount(), 0);
            UNIT_ASSERT_GT(tabletStats.GetDataSize(), 0);
            UNIT_ASSERT_GT(tabletStats.GetPartCount(), 0);
            UNIT_ASSERT_GT(tabletStats.GetRowUpdates(), 0);
            UNIT_ASSERT_EQUAL(tabletStats.GetImmediateTxCompleted(), 0);
            UNIT_ASSERT_GT(tabletStats.GetPlannedTxCompleted(), 0);
            UNIT_ASSERT_GT(tabletStats.GetLastAccessTime(), 0);
            UNIT_ASSERT_GT(tabletStats.GetLastUpdateTime(), 0);
        }

        {
            auto description = DescribePrivatePath(runtime, TTestTxConfig::SchemeShard, "/MyRoot/SomeDatabase/OlapStore/ColumnTable", true, true);
            Cerr << description.DebugString() << Endl;
            auto& tabletStats = description.GetPathDescription().GetTableStats();

            UNIT_ASSERT_GT(tabletStats.GetRowCount(), 0);
            UNIT_ASSERT_GT(tabletStats.GetDataSize(), 0);
            UNIT_ASSERT_GT(tabletStats.GetPartCount(), 0);
            UNIT_ASSERT_GT(tabletStats.GetLastAccessTime(), 0);
            UNIT_ASSERT_GT(tabletStats.GetLastUpdateTime(), 0);
        }

        {
            std::vector<ui64> writeIds;
            TSet<ui64> txIds;
            ++txId;
            {
                bool delResult = NTxUT::WriteData(
                    runtime, sender, shardId, ++writeId, pathId, data, defaultYdbSchema, &writeIds, NEvWrite::EModificationType::Delete, txId);
                Y_UNUSED(delResult);
            }
            planStep = NTxUT::ProposeCommit(runtime, sender, shardId, txId, writeIds, txId);
            txIds.insert(txId);
            NTxUT::PlanCommit(runtime, sender, shardId, planStep, txIds);
        }

        csController->EnableBackground(NKikimr::NYDBTest::ICSController::EBackground::Compaction);
        runtime.SimulateSleep(TDuration::Seconds(10));

        runtime.UpdateCurrentTime(TInstant::Now() - TDuration::Seconds(500));
        {
            std::vector<ui64> writeIds;
            TSet<ui64> txIds;
            ++txId;
            {
                data = NTxUT::MakeTestBlob({ 0, 1 }, defaultYdbSchema, {}, { "timestamp" });
                bool delResult = NTxUT::WriteData(
                    runtime, sender, shardId, ++writeId, pathId, data, defaultYdbSchema, &writeIds, NEvWrite::EModificationType::Delete, txId);
                Y_UNUSED(delResult);
            }
            planStep = NTxUT::ProposeCommit(runtime, sender, shardId, txId, writeIds, txId);
            txIds.insert(txId);
            NTxUT::PlanCommit(runtime, sender, shardId, planStep, txIds);
        }
        runtime.SimulateSleep(TDuration::Seconds(10));

        WaitTableStats(runtime, shardId);
        CheckQuotaExceedance(runtime, TTestTxConfig::SchemeShard, "/MyRoot/SomeDatabase", false, DEBUG_HINT);
    }

    Y_UNIT_TEST(MoveTableStats) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime);
        runtime.GetAppData().FeatureFlags.SetEnableMoveColumnTable(true);

        auto csController = NYDBTest::TControllers::RegisterCSControllerGuard<NYDBTest::NColumnShard::TController>();
        csController->SetOverridePeriodicWakeupActivationPeriod(TDuration::Seconds(1));

        TActorId sender = runtime.AllocateEdgeActor();
        {
            auto& appData = runtime.GetAppData();
            appData.SchemeShardConfig.SetStatsBatchTimeoutMs(0);
            appData.SchemeShardConfig.SetStatsMaxBatchSize(0);
            // apply config via reboot
            GracefulRestartTablet(runtime, TTestTxConfig::SchemeShard, sender);
        }

        ui64 txId = 100;

        const auto initialTablePath = "/MyRoot/" + defaultTableName;
        const auto movedTablePath = "/MyRoot/MovedColumnTable";

        const auto expectedTablePathId = GetNextLocalPathId(runtime, txId);
        TestCreateColumnTable(runtime, ++txId, "/MyRoot/", defaultTableSchema);
        env.TestWaitNotification(runtime, txId);

        ui64 pathId = 0;
        ui64 shardId = 0;
        auto checkFn = [&](const NKikimrScheme::TEvDescribeSchemeResult& record) {
            auto& sharding = record.GetPathDescription().GetColumnTableDescription().GetSharding();
            UNIT_ASSERT_VALUES_EQUAL(record.GetPath(), initialTablePath);
            pathId = record.GetPathId();
            UNIT_ASSERT_VALUES_EQUAL(pathId, expectedTablePathId);
            UNIT_ASSERT_VALUES_EQUAL(sharding.ColumnShardsSize(), 1);
            shardId = sharding.GetColumnShards()[0];
        };
        TestLsPathId(runtime, expectedTablePathId, checkFn);
        UNIT_ASSERT(shardId);

        const ui32 rowsInBatch = 100000;
        {   // Write data directly into shard
            const auto& data = NTxUT::MakeTestBlob({ 0, rowsInBatch }, defaultYdbSchema, {}, { "timestamp" });
            ui64 writeId = 0;
            std::vector<ui64> writeIds;
            txId = 200;
            NTxUT::WriteData(runtime, sender, shardId, ++writeId, expectedTablePathId, data, defaultYdbSchema, &writeIds,
                NEvWrite::EModificationType::Upsert, 0);
        }

        const auto& checkStat = [&](const TString& tablePath) -> bool {
            for (auto i = 0; i != 60; ++i) {
                runtime.SimulateSleep(TDuration::Seconds(1));
                auto description = DescribePrivatePath(runtime, TTestTxConfig::SchemeShard, tablePath);
                auto& tabletStats = description.GetPathDescription().GetTableStats();
                if (tabletStats.GetRowCount() == rowsInBatch) {
                    return true;
                }
            }
            return false;
        };

        UNIT_ASSERT(checkStat(initialTablePath));

        const auto expectedMovedTablePathId = GetNextLocalPathId(runtime, txId);

        TestMoveTable(runtime, ++txId, initialTablePath, movedTablePath);

        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, initialTablePath, false, NLs::PathNotExist);

        TestLsPathId(runtime, expectedMovedTablePathId, NLs::PathStringEqual(movedTablePath));

        UNIT_ASSERT(checkStat(movedTablePath));
    }
}

Y_UNIT_TEST_SUITE(TOlapNaming) {

    Y_UNIT_TEST(CreateColumnTableOk) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime);
        ui64 txId = 100;

        TString allowedChars = "_-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

        TString tableSchema = Sprintf(tableSchemaFormat.c_str(), allowedChars.c_str());

        TestCreateColumnTable(runtime, ++txId, "/MyRoot", tableSchema, {NKikimrScheme::StatusAccepted});
        env.TestWaitNotification(runtime, txId);
    }

    Y_UNIT_TEST(CreateColumnTableExtraSymbolsOk) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime);
        runtime.GetAppData().ColumnShardConfig.SetAllowExtraSymbolsForColumnTableColumns(true);
        ui64 txId = 100;

        TString allowedChars = "@_-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

        TString tableSchema = Sprintf(tableSchemaFormat.c_str(), allowedChars.c_str());

        TestCreateColumnTable(runtime, ++txId, "/MyRoot", tableSchema, {NKikimrScheme::StatusAccepted});
        env.TestWaitNotification(runtime, txId);
    }

    Y_UNIT_TEST(CreateColumnTableFailed) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime);
        ui64 txId = 100;

        TVector<TString> notAllowedNames = {"mess age", "~!@#$%^&*()+=asdfa"};

        for (const auto& colName: notAllowedNames) {
            TString tableSchema = Sprintf(tableSchemaFormat.c_str(), colName.c_str());

            TestCreateColumnTable(runtime, ++txId, "/MyRoot", tableSchema, {NKikimrScheme::StatusSchemeError});
            env.TestWaitNotification(runtime, txId);
        }
    }

    Y_UNIT_TEST(CreateColumnStoreOk) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime);
        ui64 txId = 100;

        const TString& storeSchema = defaultStoreSchema;

        TestCreateOlapStore(runtime, ++txId, "/MyRoot", storeSchema, {NKikimrScheme::StatusAccepted});
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/OlapStore", false, NLs::PathExist);
    }

    Y_UNIT_TEST(CreateColumnStoreFailed) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime);
        ui64 txId = 100;

        const TString& storeSchema = invalidStoreSchema;

        TestCreateOlapStore(runtime, ++txId, "/MyRoot", storeSchema, {NKikimrScheme::StatusSchemeError});
        env.TestWaitNotification(runtime, txId);

        TestLs(runtime, "/MyRoot/OlapStore", false, NLs::PathNotExist);
    }

    Y_UNIT_TEST(AlterColumnTableOk) {
        TTestBasicRuntime runtime;
        TTestEnvOptions options;
        TTestEnv env(runtime, options);
        ui64 txId = 100;

        TString tableSchema = Sprintf(tableSchemaFormat.c_str(), "message");

        TestCreateColumnTable(runtime, ++txId, "/MyRoot", tableSchema, {NKikimrScheme::StatusAccepted});
        env.TestWaitNotification(runtime, txId);

        TestAlterColumnTable(runtime, ++txId, "/MyRoot", R"(
            Name: "TestTable"
            AlterSchema {
                AddColumns {
                    Name: "NewColumn"
                    Type: "Int32"
                }
            }
        )");
        env.TestWaitNotification(runtime, txId);
    }

    Y_UNIT_TEST(AlterColumnTableFailed) {
        TTestBasicRuntime runtime;
        TTestEnvOptions options;
        TTestEnv env(runtime, options);
        ui64 txId = 100;

        TString tableSchema = Sprintf(tableSchemaFormat.c_str(), "message");

        TestCreateColumnTable(runtime, ++txId, "/MyRoot", tableSchema, {NKikimrScheme::StatusAccepted});
        env.TestWaitNotification(runtime, txId);

        TestAlterColumnTable(runtime, ++txId, "/MyRoot", R"(
            Name: "TestTable"
            AlterSchema {
                AddColumns {
                    Name: "New Column"
                    Type: "Int32"
                }
            }
        )", {NKikimrScheme::StatusSchemeError});
        env.TestWaitNotification(runtime, txId);
    }

    Y_UNIT_TEST(AlterColumnStoreOk) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime);
        ui64 txId = 100;

        const TString& olapSchema = defaultStoreSchema;

        TestCreateOlapStore(runtime, ++txId, "/MyRoot", olapSchema);
        env.TestWaitNotification(runtime, txId);

        const TString& tableSchema = defaultTableSchema;

        TestCreateColumnTable(runtime, ++txId, "/MyRoot/OlapStore", tableSchema);
        env.TestWaitNotification(runtime, txId);

        TestAlterOlapStore(runtime, ++txId, "/MyRoot", R"(
            Name: "OlapStore"
            AlterSchemaPresets {
                Name: "default"
                AlterSchema {
                    AddColumns { Name: "comment" Type: "Utf8" }
                }
            }
        )", {NKikimrScheme::StatusAccepted});

        env.TestWaitNotification(runtime, txId);
    }

    Y_UNIT_TEST(AlterColumnStoreFailed) {
        TTestBasicRuntime runtime;
        TTestEnv env(runtime);
        ui64 txId = 100;

        const TString& olapSchema = defaultStoreSchema;

        TestCreateOlapStore(runtime, ++txId, "/MyRoot", olapSchema);
        env.TestWaitNotification(runtime, txId);

        const TString& tableSchema = defaultTableSchema;

        TestCreateColumnTable(runtime, ++txId, "/MyRoot/OlapStore", tableSchema);
        env.TestWaitNotification(runtime, txId);

        TestAlterOlapStore(runtime, ++txId, "/MyRoot", R"(
            Name: "OlapStore"
            AlterSchemaPresets {
                Name: "default"
                AlterSchema {
                    AddColumns { Name: "mess age" Type: "Utf8" }
                }
            }
        )", {NKikimrScheme::StatusSchemeError});

        env.TestWaitNotification(runtime, txId);
    }
}