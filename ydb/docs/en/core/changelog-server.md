# {{ ydb-short-name }} Server changelog

## Version 25.3 {#25-3}

### Version 25.3.1 {#25-3-1}

Release date: TBD.

#### Functionality

* Federated queries gained support for new external data sources: [Prometheus](https://github.com/ydb-platform/ydb/pull/17148), [Apache Iceberg](https://github.com/ydb-platform/ydb/pull/17007), [OpenSearch](https://github.com/ydb-platform/ydb/pull/18444), and [Redis](https://github.com/ydb-platform/ydb/pull/16957). YDB can also use [YDB Topics as an external source](https://github.com/ydb-platform/ydb/pull/18955), making it possible to query topic data in SQL workflows.
* Extended external-source support for analytical scenarios: YDB can now [create external data sources for Iceberg tables](https://github.com/ydb-platform/ydb/pull/16652), and federated queries can push down [`REGEXP`](https://github.com/ydb-platform/ydb/pull/19227) and [`LIKE`](https://github.com/ydb-platform/ydb/pull/17695) predicates on `Utf8` columns, reducing data scanned by external systems.
* [Topic configuration export to S3](https://github.com/ydb-platform/ydb/pull/18138) and [import from S3](https://github.com/ydb-platform/ydb/pull/18214) are supported. In addition, [compacted topics can now be created via the Kafka API](https://github.com/ydb-platform/ydb/pull/18683), and YDB [automatically creates and removes the internal service consumer used for topic compaction](https://github.com/ydb-platform/ydb/pull/20176).
* Topic auto-partitioning became smarter: when splitting partitions, YDB can now take per-producer write rates into account and try to distribute producers more evenly across new partitions instead of splitting traffic strictly in half ([#25458](https://github.com/ydb-platform/ydb/pull/25458)).
* The HDRF-based scheduling stack was expanded. YDB [re-implemented the KQP CPU scheduler on the HDRF model](https://github.com/ydb-platform/ydb/pull/19618), [enabled the new HDRF-based compute scheduler](https://github.com/ydb-platform/ydb/pull/21997), and [improved the scheduler so it does not execute more tasks than the demand allows](https://github.com/ydb-platform/ydb/pull/19893).
* Script execution became more transparent: YDB now supports [runtime results during script execution](https://github.com/ydb-platform/ydb/pull/17421) and a dedicated [`running` status](https://github.com/ydb-platform/ydb/pull/19134) in script-execution APIs.
* Database administration became more flexible: schema object limits such as `MAX_SHARDS` and `MAX_PATHS` can now be [changed with YQL via `ALTER DATABASE`](https://github.com/ydb-platform/ydb/pull/19580), and BuildIndex operations now track [creation time, end time, and initiating user SID](https://github.com/ydb-platform/ydb/pull/17751).
* Auditability was extended: YDB now records [`ALTER/MODIFY USER` operations in the audit log](https://github.com/ydb-platform/ydb/pull/16989) and adds [YMQ events to the common YDB audit format](https://github.com/ydb-platform/ydb/pull/18333) as well as [dedicated audit log events for YMQ](https://github.com/ydb-platform/ydb/pull/21183).
* Storage and cluster-management capabilities were expanded. YDB added [BS Controller settings in cluster configuration](https://github.com/ydb-platform/ydb/pull/17957), [basic cluster bridging support](https://github.com/ydb-platform/ydb/pull/18297), [Board reconfiguration via DistConf](https://github.com/ydb-platform/ydb/pull/18859), and [automatic StateStorage reconfiguration without full group downtime](https://github.com/ydb-platform/ydb/pull/17525).
* Distributed Storage gained new control and sizing options: YDB introduced [`GroupSizeInUnits` for storage groups and `SlotSizeInUnits` for PDisks](https://github.com/ydb-platform/ydb/pull/19337), added a [`ydb-dstool` command to move a PDisk to another node without full data resynchronization](https://github.com/ydb-platform/ydb/pull/19684), and added [`--iam-token-file` support to `ydb-dstool`](https://github.com/ydb-platform/ydb/pull/20303).
* Observability was improved across several subsystems: YDB added [spilling IO queue wait counters](https://github.com/ydb-platform/ydb/pull/17394), [constant-value monitoring counters for nodes, VDisks, and PDisks](https://github.com/ydb-platform/ydb/pull/18731), and [more attributes in DSProxy and VDisk spans](https://github.com/ydb-platform/ydb/pull/21010) to simplify troubleshooting.
* Monitoring and operations were further extended: YDB [increased the query text size limit in system views to 10 KB](https://github.com/ydb-platform/ydb/pull/15186), added a [customizable healthcheck configuration for restart, tablet, time-drift, and timeout thresholds](https://github.com/ydb-platform/ydb/pull/15693), exposed [processing lag of committed messages in `DescribeConsumer`](https://github.com/ydb-platform/ydb/pull/16857), and added an [ICB control for `ReadRequestsInFlightLimit`](https://github.com/ydb-platform/ydb/pull/22511).
* Cluster and healthcheck behavior was refined: YDB [lowered the severity of `FAULTY` PDisks](https://github.com/ydb-platform/ydb/pull/17095), added [Healthcheck detection of cluster bootstrap state for configuration V2 environments](https://github.com/ydb-platform/ydb/pull/19600), and introduced [basic validation of group names](https://github.com/ydb-platform/ydb/pull/19216).
* YDB improved placement and topology handling for large deployments by adding [a heuristic that prevents self-overloading tablets from endlessly bouncing between nodes](https://github.com/ydb-platform/ydb/pull/18376) and support for [pile identifiers in dynamic nodes of 2-DC topologies](https://github.com/ydb-platform/ydb/pull/18988).
* Security-related integration was improved: YDB [added the `Accept` header to the OIDC whitelist](https://github.com/ydb-platform/ydb/pull/19735).
* Diagnostics and runtime support were extended: YDB added [Google Breakpad integration with configurable minidump paths and processing hooks](https://github.com/ydb-platform/ydb/pull/17362), exposed [minidump-related command-line options in `ydbd server`](https://github.com/ydb-platform/ydb/pull/17711), and added [storage of cluster access resource IDs in the root database for access checks on cluster information](https://github.com/ydb-platform/ydb/pull/17952).
* Topic and Kafka functionality was expanded further: YDB added [idempotent producer support in Kafka API](https://github.com/ydb-platform/ydb/pull/19678), [enabled topic auto-partitioning flags for CDC, replication, and transfer by default](https://github.com/ydb-platform/ydb/pull/20272), and [increased the default number of topic partitions created for replication scenarios](https://github.com/ydb-platform/ydb/pull/21420) to reduce delays caused by later auto-partitioning.
* Analytical and OLAP functionality was expanded: YDB added [index-only search support for vector indexes](https://github.com/ydb-platform/ydb/pull/18137), a [new `Increment` update operation for integer types](https://github.com/ydb-platform/ydb/pull/19567), [ColumnShard statistics for UPDATE and DELETE operations](https://github.com/ydb-platform/ydb/pull/19642), [memory limits for column-oriented tables in Memory Controller configuration](https://github.com/ydb-platform/ydb/pull/19994), [separate bulk and non-bulk statistics handling](https://github.com/ydb-platform/ydb/pull/20253), [grouped limiter and cache statistics](https://github.com/ydb-platform/ydb/pull/20738), and an [optimized `HashV2` shuffle hash function](https://github.com/ydb-platform/ydb/pull/21391).
* Columnar storage internals were improved with [a shared metadata cache for column shards on the same node](https://github.com/ydb-platform/ydb/pull/18544), [support for controlling the size of sliced portions on the zero layer](https://github.com/ydb-platform/ydb/pull/19111), and [DistConf cache support for group-info propagation](https://github.com/ydb-platform/ydb/pull/19315).
* Additional convenience improvements landed across the platform: YDB [automatically cleans up temporary directories and tables created during S3 export](https://github.com/ydb-platform/ydb/pull/16076), [enabled string-type pushdown in the Generic provider](https://github.com/ydb-platform/ydb/pull/16834), [added Solomon read optimizations](https://github.com/ydb-platform/ydb/pull/17663), [added per-process CPU limits in ColumnShard for Workload Manager integration](https://github.com/ydb-platform/ydb/pull/17804), [introduced a new release version format](https://github.com/ydb-platform/ydb/pull/18063), [added audit logging for `create tenant` actions](https://github.com/ydb-platform/ydb/pull/18095), [enabled verbose memory limit mode by default in recipes](https://github.com/ydb-platform/ydb/pull/20040), and [added support for a user-defined CA in asynchronous replication](https://github.com/ydb-platform/ydb/pull/21448).

##### Additional PR-level changes

* [22572](https://github.com/ydb-platform/ydb/pull/22572) - Topics: add partition level metrics.
* [15474](https://github.com/ydb-platform/ydb/pull/15474) - drain api as part of cms api.
* [25237](https://github.com/ydb-platform/ydb/pull/25237) - Set and show pdisk maintenance status in dstool. Show maintenance and decommit status in blobstorage controller internal table.
* [23996](https://github.com/ydb-platform/ydb/pull/23996) - Add SIMD-based implementation of TTupleLayout interface.
* [25090](https://github.com/ydb-platform/ydb/pull/25090) - IAM authentication support has been added to asynchronous replication.
* [25106](https://github.com/ydb-platform/ydb/pull/25106) - Add support for VDisk checksums.
* [25062](https://github.com/ydb-platform/ydb/pull/25062) - PreferLessOccupiedRack and WithAttentionToReplication were added only to one instance of ReassignGroupDisk command, now added it for the second one.
* [24599](https://github.com/ydb-platform/ydb/pull/24599) - Reintroduce DSProxy timeout fixes.
* [24939](https://github.com/ydb-platform/ydb/pull/24939) - Stop gRPC server when in DISCONNECTED state.
* [20860](https://github.com/ydb-platform/ydb/pull/20860) - EXT-1082 Add replication aware vdisk mapper option to the bs controller.
* [24759](https://github.com/ydb-platform/ydb/pull/24759) - do not report nonexistent databases as missing nodes and pools in healthcheck api.
* [24608](https://github.com/ydb-platform/ydb/pull/24608) - Add TMaintenanceStatus::NO_NEW_VDISKS to PDisk. If this status is set, PDisk will not accept new VDisks, but VDisks that are already on that PDisk will not be moved.
* [24618](https://github.com/ydb-platform/ydb/pull/24618) - add more test for concurrent scheme tx.
* [24241](https://github.com/ydb-platform/ydb/pull/24241) - Add more details to log messages and ErrorReasons about occurring timeouts in DSProxy.
* [23806](https://github.com/ydb-platform/ydb/pull/23806) - A new option has been added to ydbd: --force-start-with-local-config.
* [24346](https://github.com/ydb-platform/ydb/pull/24346) - Supported streaming queries starting by DDL: `CREATE / ALTER / DROP STREAMING QUERY`.
* [24456](https://github.com/ydb-platform/ydb/pull/24456) - Increase query service default query timeout to 2h.
* [23656](https://github.com/ydb-platform/ydb/pull/23656) - Topic SDK: Add TDataReceivedEvent::TMessage::GetBrokenData method to retrieve original data that caused an error on decompression.
* [24392](https://github.com/ydb-platform/ydb/pull/24392) - The program waits initial seqno for 1 second. Sometimes it's not enough. - Wait 5 seconds by default. - Hidden option `init-seqno-timeout` for waiting time.
* [23400](https://github.com/ydb-platform/ydb/pull/23400) - [#23359](https://github.com/ydb-platform/ydb/issues/23359).
* [23799](https://github.com/ydb-platform/ydb/pull/23799) - Make histogram bounds configurable like this: ``` metrics_config: common_latency_hist_bounds: rot: [0.25, 0.5] ssd: [0.25, 0.5] nvme: [0.25, 0.5] ```.
* [23922](https://github.com/ydb-platform/ydb/pull/23922) - Block Hash Hoin utils.
* [23817](https://github.com/ydb-platform/ydb/pull/23817) - * Supported runtime ast saving for script executions * Compressed script plan before saving.
* [22406](https://github.com/ydb-platform/ydb/pull/22406) - SET NOT NULL in KQP.
* [23247](https://github.com/ydb-platform/ydb/pull/23247) - SchemeShard: Try to infer default StorageConfig during table creation.
* [22221](https://github.com/ydb-platform/ydb/pull/22221) - autopartitioning of mirrored topics.
* [22365](https://github.com/ydb-platform/ydb/pull/22365) - Changes from #22269 Added a distributed transaction execution trace using the PQ tablet. Optimized the operation of the PQ tablet: - fewer Schema Cache requests - transaction predicates are calculated in batches.
* [22737](https://github.com/ydb-platform/ydb/pull/22737) - Added the ability to receive tokens from the vm metadata and use them to authorize ydb in other services.
* [23178](https://github.com/ydb-platform/ydb/pull/23178) - Added the sending metrics from external boot tablets to hive. This new behavior hidden under the flag LockedTabletsSendMetrics that 'false' by default. We need to publish Hive after the HealthChecker and then set LockedTabletsSendMetrics to 'true'.
* [23347](https://github.com/ydb-platform/ydb/pull/23347) - Support Distributed Storage tracing in configuration with multiple piles.
* [22912](https://github.com/ydb-platform/ydb/pull/22912) - Lower base table statistics propagation intervals for dedicated databases.
* [23127](https://github.com/ydb-platform/ydb/pull/23127) - Support CDC topic in topic data handler.
* [22685](https://github.com/ydb-platform/ydb/pull/22685) - An audit log entry is required upon request reception.
* [23024](https://github.com/ydb-platform/ydb/pull/23024) - Enable topic compactification by key feature.
* [22232](https://github.com/ydb-platform/ydb/pull/22232) - Configuration for the result set format in QueryService with Arrow.
* [22800](https://github.com/ydb-platform/ydb/pull/22800) - Secondary index creation will require `ydb.granular.describe_schema` and `ydb.granular.alter_schema` grants instead of `ydb.generic.read` and `ydb.generic.write`.
* [22522](https://github.com/ydb-platform/ydb/pull/22522) - Implement basic update support for global vector indexes. Clusters are not recalculated, rows are just reclassified according to the existing clusters.
* [22837](https://github.com/ydb-platform/ydb/pull/22837) - Remove finished queries from compute scheduler.
* [22549](https://github.com/ydb-platform/ydb/pull/22549) - Added pile info in Discovery/ListEndpoints response for 2DC configuration, that allows to implement PreferPrimaryPile policy in SDKs.
* [22602](https://github.com/ydb-platform/ydb/pull/22602) - Add more detailed output to Distconf cache VERIFY assertion, re-enable Encryption UT.
* [21593](https://github.com/ydb-platform/ydb/pull/21593) - Adds ability to manually approve scheduled CMS request and receive permissions. This allows, for example, locking an already failed node.
* [22541](https://github.com/ydb-platform/ydb/pull/22541) - Add unit tests for storage group configuration propagation via DistConf cache. Improve BSC's SendToWarden method, send message directly to local warden.
* [22385](https://github.com/ydb-platform/ydb/pull/22385) - no cache when access with user token.
* [22226](https://github.com/ydb-platform/ydb/pull/22226) - meta forward authorize header.
* [22146](https://github.com/ydb-platform/ydb/pull/22146) - More stable scan mode with deduplication on CS.
* [20475](https://github.com/ydb-platform/ydb/pull/20475) - refactoring after https://github.com/ydb-platform/ydb/pull/19674 . it's better to avoid use extra actor in final extension.
* [22135](https://github.com/ydb-platform/ydb/pull/22135) - Fix changing tablet external channels is not reusing previously allocated channels.
* [21280](https://github.com/ydb-platform/ydb/pull/21280) - Support XDS protocols in GRPC for client load balancing in order to request to Access Service.
* [21262](https://github.com/ydb-platform/ydb/pull/21262) - add audit loggint for monitoring HTTP API https://github.com/ydb-platform/ydb/issues/22123.
* [21874](https://github.com/ydb-platform/ydb/pull/21874) - reserve vdisk control params for full comapction throttler.
* [21649](https://github.com/ydb-platform/ydb/pull/21649) - Add X-Forwarded-For http header to keep source address.
* [21336](https://github.com/ydb-platform/ydb/pull/21336) - Introspection consists of human-readable strings when the stat mode is "Full" or higher. It's possible to manually override the number of tasks using pragma `OverridePlanner`.
* [21480](https://github.com/ydb-platform/ydb/pull/21480) - Route all handler responses through monitoring https://github.com/ydb-platform/ydb/issues/21482.
* [21172](https://github.com/ydb-platform/ydb/pull/21172) - raw bytes have been supported.
* [20966](https://github.com/ydb-platform/ydb/pull/20966) - - Introduced new Distributed Storage parameter `InferPDiskSlotCountFromUnitSize` that infers `ExpectedSlotCount` and `SlotSizeInUnits` from this value and drive size.
* [21007](https://github.com/ydb-platform/ydb/pull/21007) - Config section to enable default allocator on start: ``` allocator_config: enable_default_allocator: true ```.
* [20753](https://github.com/ydb-platform/ydb/pull/20753) - https://github.com/ydb-platform/ydb/issues/20767 Return CORS headers on 403, mon. Pages show Network Error without CORS; we need CORS so the client can interpret the received response.
* [20525](https://github.com/ydb-platform/ydb/pull/20525) - YMQ: Do not send x-amz-crc32 HTTP header (AWS does not do it).
* [20676](https://github.com/ydb-platform/ydb/pull/20676) - increasing wait time.
* [20231](https://github.com/ydb-platform/ydb/pull/20231) - In [other PR](https://github.com/ydb-platform/ydb/pull/18333) was problem with some field's values. This PR fixes all 'bad' fields. There are list of those fields: 1. masked_token 2. queue_name 3. idempotency_id 4. remote_address.
* [20245](https://github.com/ydb-platform/ydb/pull/20245) - Account as extra usage which doesn't affect scheduling for now.
* [18856](https://github.com/ydb-platform/ydb/pull/18856) - Adds periodic balancing task to BlobStorageController that moves VDisks to PDisks which are less occupied to make distribution of VDisks even.
* [19674](https://github.com/ydb-platform/ydb/pull/19674) - Add Extended Info field to OIDC whoami response.
* [12365](https://github.com/ydb-platform/ydb/pull/12365) - Described asynchronous replication's consistency levels.
* [20284](https://github.com/ydb-platform/ydb/pull/20284) - Added GeneralCache limit control to MemoryController.
* [19990](https://github.com/ydb-platform/ydb/pull/19990) - Add compaction logs.
* [19989](https://github.com/ydb-platform/ydb/pull/19989) - RequireDbPrefixInSecretName flag has been added.
* [19444](https://github.com/ydb-platform/ydb/pull/19444) - Introduce new fullsync protocol, which takes snapshot only once per synchronization.
* [19817](https://github.com/ydb-platform/ydb/pull/19817) - Added CS compaction ResourceBroker queues configuration to MemoryController.
* [19663](https://github.com/ydb-platform/ydb/pull/19663) - CLOUD_EVENTS format in audit config.
* [19653](https://github.com/ydb-platform/ydb/pull/19653) - OIDC needs pass tracing headers.
* [19679](https://github.com/ydb-platform/ydb/pull/19679) - * YDB FQ: add default protocol for OpenSearch.
* [17625](https://github.com/ydb-platform/ydb/pull/17625) - ydbd admin bs disk obliterate - improvement.
* [19376](https://github.com/ydb-platform/ydb/pull/19376) - Add read iterator cancellation to ReadRows RPC.
* [19036](https://github.com/ydb-platform/ydb/pull/19036) - Add secondary index followers compatibility test.
* [19194](https://github.com/ydb-platform/ydb/pull/19194) - YDB-local Docker container started with Kafka service and open Kafka port.
* [18360](https://github.com/ydb-platform/ydb/pull/18360) - Switch on new query workload.
* [18968](https://github.com/ydb-platform/ydb/pull/18968) - Added new config parameter _EnbaleRuntimeListing, that enables/disables runtime metrics listing.
* [19024](https://github.com/ydb-platform/ydb/pull/19024) - * YDB FQ: support REGEXP pushdown for Generic provider.
* [18964](https://github.com/ydb-platform/ydb/pull/18964) - Исправление теста TestReadUpdateWriteLoad.test[read_update_write_load].
* [18263](https://github.com/ydb-platform/ydb/pull/18263) - - Topic SDK read session connects directly to partition nodes reducing inter-node network communication.
* [18663](https://github.com/ydb-platform/ydb/pull/18663) - pass folder_id parameter to ticket parser.
* [17888](https://github.com/ydb-platform/ydb/pull/17888) - Add new check configuration version command for ydb cli.
* [18886](https://github.com/ydb-platform/ydb/pull/18886) - Solomon responses with http code 503 are now retryable.
* [18159](https://github.com/ydb-platform/ydb/pull/18159) - Index impl table is not private.
* [18561](https://github.com/ydb-platform/ydb/pull/18561) - Add miss kafka port support .
* [18258](https://github.com/ydb-platform/ydb/pull/18258) - Increase timeout on BS_QUEUE reestablishing session when many actors try to reconnect simultaneously.
* [17061](https://github.com/ydb-platform/ydb/pull/17061) - Add of date range parameters (--date-to, --date-from to support uniform PK distribution) for ydb workload log run operations including bulk_upsert, insert, and upsert.
* [18339](https://github.com/ydb-platform/ydb/pull/18339) - grpc check actor: check folder_id user attribute instead of container_id.
* [13406](https://github.com/ydb-platform/ydb/pull/13406) - Refactor Wilson Tracing in PDisk.
* [18081](https://github.com/ydb-platform/ydb/pull/18081) - Added progress stats to `ydb workload query` with one thread.
* [16491](https://github.com/ydb-platform/ydb/pull/16491) - Added hidden option `--progress` to `ydb sql` command with default: "none", that print progress of query execution.
* [17965](https://github.com/ydb-platform/ydb/pull/17965) - Add ability to enable followers (read replicas) for secondary indexes.
* [17864](https://github.com/ydb-platform/ydb/pull/17864) - #17863 added file name and line number to logs.
* [17798](https://github.com/ydb-platform/ydb/pull/17798) - Export/import encryption features in YDB SDK.
* [17618](https://github.com/ydb-platform/ydb/pull/17618) - Don't allow SelfHeal actor to create more than 1 active ReassignerActor. This change will prevent SelfHeal from overloading BSC with ReassignItem requests thus causing DoS.
* [17682](https://github.com/ydb-platform/ydb/pull/17682) - Last offset and custom message size limit support.
* [17686](https://github.com/ydb-platform/ydb/pull/17686) - Now test wait for all the events before finishing .
* [17672](https://github.com/ydb-platform/ydb/pull/17672) - Added additional CLI parameters for `ydb debug latency`: * --min-inflight * allow to specify multiple percentiles using multiple "-p" params.
* [17324](https://github.com/ydb-platform/ydb/pull/17324) - Support for reading data from Yandex Monitoring.
* [17561](https://github.com/ydb-platform/ydb/pull/17561) - Added support for traceparent and updated gRPC client and request settings in the C++ SDK.
* [15984](https://github.com/ydb-platform/ydb/pull/15984) - [C++ SDK] Supported topic-to-table transactions in Query Service.
* [17360](https://github.com/ydb-platform/ydb/pull/17360) - Add a setting that specifies the initial stream offset by timestamp or duration.
* [14910](https://github.com/ydb-platform/ydb/pull/14910) - Build binaries before running tests to avoid 20 parallel builds.
* [17430](https://github.com/ydb-platform/ydb/pull/17430) - Add NFederatedTopic::TDeferredCommit implementation.
* [15335](https://github.com/ydb-platform/ydb/pull/15335) - - YDB FQ: support MongoDB as an external data source.
* [16699](https://github.com/ydb-platform/ydb/pull/16699) - add fast bs queue to donor for online read.
* [16993](https://github.com/ydb-platform/ydb/pull/16993) - do not propagate disk issues to group level in health check when the disk is operational.
* [17012](https://github.com/ydb-platform/ydb/pull/17012) - The pull request is aimed at clarifying the reason why the user could not log in.
* [16994](https://github.com/ydb-platform/ydb/pull/16994) - Create 30 groups of columns instead of 100 under asan to eliminate timeout .
* [15351](https://github.com/ydb-platform/ydb/pull/15351) - Added parameter which value is subtracted from timestamp .
* [11629](https://github.com/ydb-platform/ydb/pull/11629) - 1) Read session id field for CommitOffset RPC 2) Commit offsets logic for autopartitioned topics.
* [16792](https://github.com/ydb-platform/ydb/pull/16792) - The patchset makes `ReplicateScalars` work only with WideStream I/O type in DQ.
* [16751](https://github.com/ydb-platform/ydb/pull/16751) - Supported C++ SDK build with gcc.
* [16686](https://github.com/ydb-platform/ydb/pull/16686) - enable gzip compression in main by default resolves [#16328](https://github.com/ydb-platform/ydb/issues/16328).
* [16614](https://github.com/ydb-platform/ydb/pull/16614) - new parameter in AuthConfig — ClusterAccessResourceId — for defining a new resource with new roles for `Nebius_v1`.
* [16189](https://github.com/ydb-platform/ydb/pull/16189) - Supported parse parameters type in CLI.
* [16266](https://github.com/ydb-platform/ydb/pull/16266) - "--no-discovery" option allows to skip discovery and use user provided endpoint to connect to YDB cluster.
* [16222](https://github.com/ydb-platform/ydb/pull/16222) - This test implements zip bomb. It writes equal columns in many rows and checks used memory during selects.
* [16368](https://github.com/ydb-platform/ydb/pull/16368) - Change logger in comp nodes.
* [14403](https://github.com/ydb-platform/ydb/pull/14403) - New statement 'ALTER DATABASE path OWNER TO newowner'.
* [16309](https://github.com/ydb-platform/ydb/pull/16309) - adds real cores and system threads stats to whiteboard system information.
* [16103](https://github.com/ydb-platform/ydb/pull/16103) - TLI system views.
* [16347](https://github.com/ydb-platform/ydb/pull/16347) - Add version option to check current version of dstool.
* [16140](https://github.com/ydb-platform/ydb/pull/16140) - Add missing functions for topic data handler.
* [16251](https://github.com/ydb-platform/ydb/pull/16251) - fixes [#16276](https://github.com/ydb-platform/ydb/issues/16276).
* [15800](https://github.com/ydb-platform/ydb/pull/15800) - YDB CLI help message improvements. Different display for detailed help and brief help.
* [16227](https://github.com/ydb-platform/ydb/pull/16227) - The patchset makes `ReplicateScalars` work with both WideFlow and WideStream I/O types in DQ.
* [15823](https://github.com/ydb-platform/ydb/pull/15823) - Make shuffle elimination in JOIN work by default .
* [16064](https://github.com/ydb-platform/ydb/pull/16064) - Add support for CMS API to address single PDisk for REPLACE_DEVICES action. Since PDisks can be restarted, there is no need to lock and shutdown entire host.
* [16226](https://github.com/ydb-platform/ydb/pull/16226) - in some cases (k8s clusters, other network LB solutions) we need to skip client balancer and use database endpoint to perform query. Old way to do it - configure it for each request.
* [15968](https://github.com/ydb-platform/ydb/pull/15968) - This test checks that writing are disable after exceeding quota and deletions are always enabled.
* [16129](https://github.com/ydb-platform/ydb/pull/16129) - Support coordination nodes in `ydb scheme rmdir --recursive`.
* [15662](https://github.com/ydb-platform/ydb/pull/15662) - Unlimited reading from solomon is now supported through external data sources.
* [16065](https://github.com/ydb-platform/ydb/pull/16065) - In case of investigation problems with unexpected timeout response It is helpful to know actual timeout for grpc call from the server perspective.
* [16090](https://github.com/ydb-platform/ydb/pull/16090) - class TPrettyTable: improve compliance with the C++20 standard.
* [15595](https://github.com/ydb-platform/ydb/pull/15595) - Cache has been added to restrict to calculate argone2 hash function.
* [15911](https://github.com/ydb-platform/ydb/pull/15911) - - `ic_port` is now again recognized as correct in `template.yaml` format.
* [14268](https://github.com/ydb-platform/ydb/pull/14268) - Implement reliable signal handler which prints stacktrace to logs.
* [15829](https://github.com/ydb-platform/ydb/pull/15829) - Cast unaligned pointer to pointer to multiple bytes integers is UB.
* [15250](https://github.com/ydb-platform/ydb/pull/15250) - Get topic data handler for UI.
* [15743](https://github.com/ydb-platform/ydb/pull/15743) - healthcheck report storage group layout incorrect.
* [15599](https://github.com/ydb-platform/ydb/pull/15599) - remove automatic secondary indices warning.
* [15690](https://github.com/ydb-platform/ydb/pull/15690) - Добавлены время создания и время последнего обновления задачи обслуживания.
* [15587](https://github.com/ydb-platform/ydb/pull/15587) - Yield fullsync processing when it takes too long.
* [15477](https://github.com/ydb-platform/ydb/pull/15477) - add validation to never have tablet migration from root hive to itself.
* [14416](https://github.com/ydb-platform/ydb/pull/14416) - add health check overload shard hint.
* [15358](https://github.com/ydb-platform/ydb/pull/15358) - Add functional tests for spilling.
* [15483](https://github.com/ydb-platform/ydb/pull/15483) - - added option `--backport-to-template` to ydb_configure.
* [14614](https://github.com/ydb-platform/ydb/pull/14614) - Read from a single columntable shard.
* [14860](https://github.com/ydb-platform/ydb/pull/14860) - added healthcheck config.
* [15376](https://github.com/ydb-platform/ydb/pull/15376) - Add an option to case out-of-range doubles coming from bulk-upsert to double::inf.
* [15265](https://github.com/ydb-platform/ydb/pull/15265) - Some small fixes after merge first part of decoupling.
* [15416](https://github.com/ydb-platform/ydb/pull/15416) - Added YDB_AUTH_TICKET_HEADER for fq proxy ydb requests.
* [15152](https://github.com/ydb-platform/ydb/pull/15152) - Add confirmation modal dialog in CMS UI when user wants to change tablet settings.
* [14979](https://github.com/ydb-platform/ydb/pull/14979) - Add S3 support into BlobDepot.
* [15344](https://github.com/ydb-platform/ydb/pull/15344) - Do not suggest NOT NULL in `ydb import file csv`.
* [14816](https://github.com/ydb-platform/ydb/pull/14816) - Added scripts for starting prometheus and connector in kqprun.
* [15038](https://github.com/ydb-platform/ydb/pull/15038) - Get topic data handler for UI.
* [15147](https://github.com/ydb-platform/ydb/pull/15147) - Cannot accept the invalid account lockout config.
* [15157](https://github.com/ydb-platform/ydb/pull/15157) - Switch to library/cpp/charset/lite to avoid unneded dependency on libiconv.
* [15016](https://github.com/ydb-platform/ydb/pull/15016) - * Supported trace opt * Fixed flame graph script * Added repeats and async queries * Supported using external databases as cp / checkpoint storage.
* [8406](https://github.com/ydb-platform/ydb/pull/8406) - Command line options for ydb cli that provide client certificate to mTLS connection. Implement according to RFC: https://github.com/ydb-platform/ydb-rfc/blob/main/cli_client_certificate_options.md The next PRs will support env vars and improved help.
* [14347](https://github.com/ydb-platform/ydb/pull/14347) - Remove broadcast collect stage.
* [15094](https://github.com/ydb-platform/ydb/pull/15094) - - Add yaml_configurator instead of configurator - Add warning for old style cluster.yaml - Add dynconfig-generator for generating simple dynconfig.yaml - Add --save-raw-cfg option for saving raw config from old cluster.yaml.
* [15095](https://github.com/ydb-platform/ydb/pull/15095) - Added Ru return in GetConsumedRu method with streaming calls.
* [15143](https://github.com/ydb-platform/ydb/pull/15143) - add kind options to storage pools for create specific set of pdisks .
* [15102](https://github.com/ydb-platform/ydb/pull/15102) - Cleanup the page cache periodically to prevent uncontrollable OOMs.
* [15031](https://github.com/ydb-platform/ydb/pull/15031) - Supported in memory cp storage.
* [12738](https://github.com/ydb-platform/ydb/pull/12738) - Add ok and error request metrics.
* [15005](https://github.com/ydb-platform/ydb/pull/15005) - Start to use antlr4 by default.
* [15045](https://github.com/ydb-platform/ydb/pull/15045) - Autoconfig in the actor system configuration can be used without the field 'use_auto_config.' Autoconfig is enabled when the fields 'node_type' or 'cpu_count' are used.
* [14901](https://github.com/ydb-platform/ydb/pull/14901) - Elimination of shuffles to improve performance.
* [12386](https://github.com/ydb-platform/ydb/pull/12386) - Add YQL keywords suggestions to YDB CLI.
* [14912](https://github.com/ydb-platform/ydb/pull/14912) - Make serializer/deserializer for encrypted backup files. Use it in export code, but higher level code yet does not set the settings for encryption.
* [15020](https://github.com/ydb-platform/ydb/pull/15020) - Improve error log message in ticket parser. Log can print extended information.
* [15006](https://github.com/ydb-platform/ydb/pull/15006) - Support LayoutCorrect fields for SysView.
* [12737](https://github.com/ydb-platform/ydb/pull/12737) - Enabled by default ANTLRv4 parser of YQL.
* [14861](https://github.com/ydb-platform/ydb/pull/14861) - Add CredentialsProvider for system service account (SSA) in C++ SDK.
* [14868](https://github.com/ydb-platform/ydb/pull/14868) - Fix an issue where column table shards would not be balanced under high load.
* [14890](https://github.com/ydb-platform/ydb/pull/14890) - While enabled the cleanup method cleans not only physically-allocated (RSS) pages, but also reserved ones that are never used. Thus we get a lot of "holes" inside mapped memory regions: ``` 3'000'000 - regions with cleanup 100'000 - regions without cleanup ```.
* [14708](https://github.com/ydb-platform/ydb/pull/14708) - Supported insert values syntax for s3. Added type cast.
#### Performance

* YDB [enabled multi-broadcast in the table service by default](https://github.com/ydb-platform/ydb/pull/17794), reducing overhead for distributed queries where broadcasting is the optimal strategy.
* Write-path performance was improved in several places: YDB introduced [a new PDisk priority scheme that separates realtime and compaction writes](https://github.com/ydb-platform/ydb/pull/21705), [optimized the propagation of `TStorageConfig` updates to subscribers](https://github.com/ydb-platform/ydb/pull/18765), and [improved the algorithm of the new scheduler](https://github.com/ydb-platform/ydb/pull/20195).
* Upsert operations on tables with default values are now [more efficient](https://github.com/ydb-platform/ydb/pull/20048): when possible, YDB avoids extra row reads and performs more of the work directly on shards.
* Authentication overhead was reduced by [moving password verification into a dedicated actor instead of handling it inside `TSchemeShard` local transactions](https://github.com/ydb-platform/ydb/pull/19687).
* Query execution for column-oriented tables was further accelerated by [using binary search for predicate-bound detection in portions](https://github.com/ydb-platform/ydb/pull/16867), [pushing more filter types into column shards](https://github.com/ydb-platform/ydb/pull/17884), and [improving parallel execution of OLAP queries](https://github.com/ydb-platform/ydb/pull/20428).
* Planner and execution efficiency were improved with [constant folding for deterministic UDFs](https://github.com/ydb-platform/ydb/pull/17533), [better task placement for reads from external sources](https://github.com/ydb-platform/ydb/pull/18461), a [cache for SchemeNavigate responses](https://github.com/ydb-platform/ydb/pull/20128), and the new [`buffer_page_alloc_size` configuration parameter](https://github.com/ydb-platform/ydb/pull/19724).
* Roaring UDF performance and expressiveness were improved with the [`Intersect`](https://github.com/ydb-platform/ydb/pull/17611) operation.

##### Additional PR-level changes

* [25000](https://github.com/ydb-platform/ydb/pull/25000) - Always set ArrayBufferMinFillPercentage from default value.
* [25084](https://github.com/ydb-platform/ydb/pull/25084) - Optimize TEvAssimilate.
* [24944](https://github.com/ydb-platform/ydb/pull/24944) - Optimized forget operation for script executions (~ x4.3 speedup).
* [23762](https://github.com/ydb-platform/ydb/pull/23762) - Added retries for retryable errors for batch uploading to row table.
* [23617](https://github.com/ydb-platform/ydb/pull/23617) - The program can add transactions to the batch until it reaches the request size in KV. As a result, transactions may be delayed. They will wait for predicates and commits.
* [22311](https://github.com/ydb-platform/ydb/pull/22311) - Invoke TRope::Compact in OnVGetResult only when occupied memory exceeds threshold.
* [22473](https://github.com/ydb-platform/ydb/pull/22473) - The compute scheduler tries to utilize all pools fair-share by resuming throttled tasks.
* [22620](https://github.com/ydb-platform/ydb/pull/22620) - ![profdata](https://github.com/user-attachments/assets/81a7f464-2890-4c8f-81f4-01cebe274b49).
* [19807](https://github.com/ydb-platform/ydb/pull/19807) - Changed the retry policy settings. Users will receive faster confirmation that the server has written the message. Added logging of requests to KQP.
* [16375](https://github.com/ydb-platform/ydb/pull/16375) - YQ-4161 support block writing in s3.
* [17725](https://github.com/ydb-platform/ydb/pull/17725) - Limit internal inflight config updates.
* [15792](https://github.com/ydb-platform/ydb/pull/15792) - Add naive bulk And to Roaring UDF.
* [15890](https://github.com/ydb-platform/ydb/pull/15890) - Speed up the startup of dynamic nodes in the cluster.
* [15540](https://github.com/ydb-platform/ydb/pull/15540) - XDC allows to send huge events without splitting into small IC chunks.
* [15342](https://github.com/ydb-platform/ydb/pull/15342) - Supported yt parallel reading.
* [15345](https://github.com/ydb-platform/ydb/pull/15345) - Supported yt block reading.
#### Bug Fixes

* Fixed several correctness and stability issues in column-oriented tables, including [predicate handling in scan queries](https://github.com/ydb-platform/ydb/pull/16061), [CPU-limiter races that could return inconsistent results](https://github.com/ydb-platform/ydb/pull/20238), and [crashes or failures when altering tables with vector indexes](https://github.com/ydb-platform/ydb/pull/18121), including [`RENAME INDEX`](https://github.com/ydb-platform/ydb/pull/17982).
* Fixed transaction-consistency issues: YDB added safeguards against [incorrect results in specific read-write transactions](https://github.com/ydb-platform/ydb/pull/18088) and resolved a bug where [conflicting read-write transactions could violate serializability after shard restarts](https://github.com/ydb-platform/ydb/pull/18234).
* Fixed several topic and session-management issues, including [rare node failures during read-session balancing](https://github.com/ydb-platform/ydb/pull/16016), [commit-offset failures on auto-partitioned topics](https://github.com/ydb-platform/ydb/pull/20560), [unexpected `PathErrorUnknown` errors while committing offsets](https://github.com/ydb-platform/ydb/pull/20084), and a bug where [attach streams remained active after session shutdown](https://github.com/ydb-platform/ydb/pull/22298).
* Fixed backup, restore, and replication edge cases: YDB no longer [stores destination tables and changefeeds of asynchronous replication in local backups](https://github.com/ydb-platform/ydb/pull/18401), and YDB CLI backup/restore now [handles `UUID` columns correctly](https://github.com/ydb-platform/ydb/pull/17198).
* Fixed several Distributed Storage stability issues, including missing [encryption checks in zero-copy transfers](https://github.com/ydb-platform/ydb/pull/18698), [VDisk freezes in local recovery after failed `ChunkRead` requests](https://github.com/ydb-platform/ydb/pull/20519), and [phantom VDisks caused by races between group creation and deletion](https://github.com/ydb-platform/ydb/pull/18924).
* Improved cluster-management safety: YDB fixed [duplicate PDisk locking attempts in CMS](https://github.com/ydb-platform/ydb/pull/19781), added a [resource-manager race fix](https://github.com/ydb-platform/ydb/pull/25412), and changed `RateLimiter` to return [`SCHEME_ERROR` instead of `INTERNAL_ERROR` for nonexistent coordination nodes or resources](https://github.com/ydb-platform/ydb/pull/16901).
* Fixed healthcheck and maintenance corner cases by handling [unknown `StatusV2` values in VDisk healthcheck](https://github.com/ydb-platform/ydb/pull/17606), switching PDisk-state interpretation to a [less ambiguous source](https://github.com/ydb-platform/ydb/pull/17687), and adding a [BSC PDisk status intended for long-term maintenance scenarios](https://github.com/ydb-platform/ydb/pull/17920).
* Fixed issues in Workload Manager and scheduler-related code, including [use-after-free and VERIFY failures in the CPU scheduler and CPU limiter](https://github.com/ydb-platform/ydb/pull/20157).
* Fixed DDL usability problems for external sources and improved diagnostics for [`ALTER TABLE ... RENAME TO`](https://github.com/ydb-platform/ydb/pull/20670).
* Fixed additional OLAP and query-engine issues, including [crashes in scan queries with predicates and `LIMIT`](https://github.com/ydb-platform/ydb/pull/16879), [JOIN compilation failures with an empty constant input](https://github.com/ydb-platform/ydb/pull/17009), [language-version propagation into the computation layer](https://github.com/ydb-platform/ydb/pull/17537), [comprehensive filter pushdown into column shards](https://github.com/ydb-platform/ydb/pull/17743), [crashes in some `DESC` OLAP queries](https://github.com/ydb-platform/ydb/pull/18059), and [float aggregation issues in `arrow::Kernel`](https://github.com/ydb-platform/ydb/pull/19466).
* Fixed usability and diagnostics issues around scripts, configuration, and external sources: YDB added [better error details for `Generic::TPartition` parsing](https://github.com/ydb-platform/ydb/pull/17230), [periodic execution-progress statistics and dynamic query-service configuration updates](https://github.com/ydb-platform/ydb/pull/17314), [correct handling of optional structs in output](https://github.com/ydb-platform/ydb/pull/17468), [improved diagnostics for external data sources by listing dependent objects](https://github.com/ydb-platform/ydb/pull/17911), and [correct passing of query-service configuration into SchemeShard](https://github.com/ydb-platform/ydb/pull/16208).
* Fixed networking, proxying, and deployment issues, including [additional socket reuse options on proxy ports](https://github.com/ydb-platform/ydb/pull/17429), [redirects from cluster endpoints to database nodes](https://github.com/ydb-platform/ydb/pull/16764), [forward-compatibility issues around `MSG_ZEROCOPY`](https://github.com/ydb-platform/ydb/pull/17872), [guard-actor startup logic for zero-copy transfers](https://github.com/ydb-platform/ydb/pull/17826), [serverless gRPC metrics reporting](https://github.com/ydb-platform/ydb/pull/20386), and a [data race in the Jaeger settings configurator](https://github.com/ydb-platform/ydb/pull/20785).
* Fixed additional storage and maintenance issues, including [rare VERIFY failures during replication](https://github.com/ydb-platform/ydb/pull/16021), [support for `MaxFaultyPDisksPerNode` in CMS/Sentinel limits](https://github.com/ydb-platform/ydb/pull/16932), [better handling of low-space status flags during replication](https://github.com/ydb-platform/ydb/pull/17418), [data-erasure requests for PQ tablets](https://github.com/ydb-platform/ydb/pull/17420), [PDisk `Stop` handling in Error and Init states](https://github.com/ydb-platform/ydb/pull/17780), and [correction of operation ordering for BuildIndex, Export, and Import](https://github.com/ydb-platform/ydb/pull/17814).
* Fixed additional product behavior issues, including [schema-version collisions in serverless databases](https://github.com/ydb-platform/ydb/pull/17729), [`SHOW CREATE TABLE` returning output for views instead of failing](https://github.com/ydb-platform/ydb/pull/16423), [force-availability mode incorrectly considering offline-node limits](https://github.com/ydb-platform/ydb/pull/20217), and [incorrect handling of absolute paths in workloads](https://github.com/ydb-platform/ydb/pull/19762).
* Roaring UDF functionality was corrected and extended with the [`Add` and `IsEmpty`](https://github.com/ydb-platform/ydb/pull/17650) operations.

##### Additional PR-level changes

* [25408](https://github.com/ydb-platform/ydb/pull/25408) - Fixed tests: * TestRetryLimiter * RestoreScriptPhysicalGraphOnRetry * CreateStreamingQueryMatchRecognize Also increased default test logs level.
* [25236](https://github.com/ydb-platform/ydb/pull/25236) - Preparation before switched fq and s3 feature flags by default.
* [25311](https://github.com/ydb-platform/ydb/pull/25311) - Fixed multipart uploading in s3 provider.
* [25331](https://github.com/ydb-platform/ydb/pull/25331) - Export encryption. Use __msan_unpoison to mark initialized memory. For some reason it was not done in case when chacha20-poly1305 algorithm is used.
* [25309](https://github.com/ydb-platform/ydb/pull/25309) - fixes access checking. improves scheme cache fallback and data collection. closes #25121.
* [25232](https://github.com/ydb-platform/ydb/pull/25232) - Added kikimr init waiting and fixed ULID gen usage (stabilization of tests TestReadLargeParquetFile and OverridePlannerDefaults).
* [25194](https://github.com/ydb-platform/ydb/pull/25194) - Fix crashing on select from sys-view after an olap table is dropped.
* [25196](https://github.com/ydb-platform/ydb/pull/25196) - Fixed race in logger tests.
* [25113](https://github.com/ydb-platform/ydb/pull/25113) - Unmuted tests ExecuteScriptWithExternalTableResolve and ExecuteScriptWithExternalTableResolveCheckPartitionedBy on s3 url escaping.
* [25191](https://github.com/ydb-platform/ydb/pull/25191) - Make wakeup callback thread safe in pure compute actor.
* [24736](https://github.com/ydb-platform/ydb/pull/24736) - Write stats for Column Store even in NoTx mode. Column shards were not reporting table access statistics when queries were executed in immediate transaction mode (all DDL and queries without BEGIN ... COMMIT). https://github.com/ydb-platform/ydb/issues/22446.
* [24877](https://github.com/ydb-platform/ydb/pull/24877) - https://github.com/ydb-platform/ydb/issues/24671 Check AppData before audit enabled.
* [24937](https://github.com/ydb-platform/ydb/pull/24937) - fix crash after follower alter https://github.com/ydb-platform/ydb/issues/20866 https://github.com/ydb-platform/ydb/issues/20868.
* [24621](https://github.com/ydb-platform/ydb/pull/24621) - Fixed streaming queries synchronization (do not stop in CREATING status).
* [23855](https://github.com/ydb-platform/ydb/pull/23855) - - fix verify on whoami requests when actor system shutdown - fix double PassAway on 404 whoami **refactoring:** - changed response status check - TProxiedResponseParams stored by value, simplifying memory management.
* [24780](https://github.com/ydb-platform/ydb/pull/24780) - Don't leave processed checkpoints in the buffer. Fixes #24779.
* [24590](https://github.com/ydb-platform/ydb/pull/24590) - resolves security issue with database user being able to get info about storage nodes closes #24588 also solves small issue with incorrect pdisk id parsing.
* [24723](https://github.com/ydb-platform/ydb/pull/24723) - Turned off hash propagation when shuffle elimination is off.
* [24573](https://github.com/ydb-platform/ydb/pull/24573) - fix a bug where tablet deletion might get stuck https://github.com/ydb-platform/ydb/issues/23858.
* [24578](https://github.com/ydb-platform/ydb/pull/24578) - Fixing If predicate pushdown into column shards by expanding constant folding and getting rid of the if https://github.com/ydb-platform/ydb/issues/23731.
* [24489](https://github.com/ydb-platform/ydb/pull/24489) - Reduce cpu consumption by deduplication without intersections.
* [24341](https://github.com/ydb-platform/ydb/pull/24341) - Fixed read from topic with column order.
* [24033](https://github.com/ydb-platform/ydb/pull/24033) - Do not mix scalar and block HashShuffle connections #23895.
* [24282](https://github.com/ydb-platform/ydb/pull/24282) - Unpoison trace id to prevent false msan alert.
* [24272](https://github.com/ydb-platform/ydb/pull/24272) - Поправлена ошибка, когда при записи сообщений kafka , использующих формат батча v0 и v1 сохранялось только первое сообщение батча (все остальные игнорировались).
* [24254](https://github.com/ydb-platform/ydb/pull/24254) - Fixed CPU limiting in composite conveyor.
* [24251](https://github.com/ydb-platform/ydb/pull/24251) - Fixes issue https://github.com/ydb-platform/ydb/issues/24143.
* [24164](https://github.com/ydb-platform/ydb/pull/24164) - Fix replication query error in Bridge mode.
* [23830](https://github.com/ydb-platform/ydb/pull/23830) - * Fixed empty issues for disabled external sources * Fixed fault when script executions are disabled.
* [24030](https://github.com/ydb-platform/ydb/pull/24030) - solves problem with access denied on /viewer/capabilities handler closes #24013.
* [24010](https://github.com/ydb-platform/ydb/pull/24010) - Fix node warden assertion.
* [23908](https://github.com/ydb-platform/ydb/pull/23908) - Fix crashing on kikimr stop due to a bug in deduplication.
* [23816](https://github.com/ydb-platform/ydb/pull/23816) - Switched off FSM Sort Optimizations that were breaking existing queries https://github.com/ydb-platform/ydb/issues/23694.
* [23790](https://github.com/ydb-platform/ydb/pull/23790) - Fix quorum problem in distconf.
* [23711](https://github.com/ydb-platform/ydb/pull/23711) - Fixed missing sdk change then moving from v2 to v3.
* [23685](https://github.com/ydb-platform/ydb/pull/23685) - Fix DS proxy batching problem.
* [23577](https://github.com/ydb-platform/ydb/pull/23577) - fixes bug with incorrect group storage sizes calculation (on cluster and on storage groups views). closes #23576.
* [23607](https://github.com/ydb-platform/ydb/pull/23607) - fix race in dsproxy report throttler in test SystemView::PartitionStatsTtlFields.
* [23667](https://github.com/ydb-platform/ydb/pull/23667) - Fix process crashing with assertion when follower promotes to leader or reattaches while booting. Fixes #17469.
* [23664](https://github.com/ydb-platform/ydb/pull/23664) - Refactor assimilation reverse iterating in VDisk.
* [23633](https://github.com/ydb-platform/ydb/pull/23633) - Fix RestartCounter issue.
* [23543](https://github.com/ydb-platform/ydb/pull/23543) - Fixed hash shuffle channels restore for streaming queries.
* [23461](https://github.com/ydb-platform/ydb/pull/23461) - Fixed hanging in row dispatcher coordinator (race between start of nodes manager and coordinator).
* [23491](https://github.com/ydb-platform/ydb/pull/23491) - Fixed federated queries providers registration.
* [23422](https://github.com/ydb-platform/ydb/pull/23422) - After this PR PotentialMaxThreadCount represent the true maximum number of threads a pool could obtain, including threads that could be re‑allocated from lower‑priority pools that are currently under‑utilised #23232.
* [23375](https://github.com/ydb-platform/ydb/pull/23375) - Fixed PartitionedTaskParams type (repeated string -> repeated bytes). PartitionedTaskParams contains serialized protobufs.
* [23373](https://github.com/ydb-platform/ydb/pull/23373) - Quick fix for https://st.yandex-team.ru/YQL-20288.
* [23352](https://github.com/ydb-platform/ydb/pull/23352) - WilsonSpan should not be re-assigned, if not ended, otherwise it will end with error. This change allows to end BufferWriteActorStateSpan in TKqpBufferWriteActor correctly. #23351.
* [23318](https://github.com/ydb-platform/ydb/pull/23318) - increase memory limit for run execution plan fixes #23171.
* [23168](https://github.com/ydb-platform/ydb/pull/23168) - Decrement SchemeShard/Paths counter if MkDir operation is aborted. Issue: - https://github.com/ydb-platform/ydb/issues/23263.
* [23181](https://github.com/ydb-platform/ydb/pull/23181) - Fixed fault when YdbTopics specified in AvailableExternalDataSources.
* [23195](https://github.com/ydb-platform/ydb/pull/23195) - Fixed logs spam from topic sdk.
* [22907](https://github.com/ydb-platform/ydb/pull/22907) - Fix crashing on CS reader destruction when actor system is stopped.
* [22691](https://github.com/ydb-platform/ydb/pull/22691) - TEvAskTabletDataAccessors has been fixed.
* [22881](https://github.com/ydb-platform/ydb/pull/22881) - Fixed memory monitoring in kqprun and added threads flag.
* [22778](https://github.com/ydb-platform/ydb/pull/22778) - Add pool to compute scheduler as soon as possible #22621.
* [22752](https://github.com/ydb-platform/ydb/pull/22752) - Fix use-after-free in CMS remove permission.
* [22564](https://github.com/ydb-platform/ydb/pull/22564) - Incorrect arg position was used Fixed as part of SOLOMON-16506.
* [21642](https://github.com/ydb-platform/ydb/pull/21642) - The PQ tablet did not receive a TEvReadSet (#20025).
* [22705](https://github.com/ydb-platform/ydb/pull/22705) - Fix OOM crash when many column shards with tiering enabled are initialized.
* [22663](https://github.com/ydb-platform/ydb/pull/22663) - Fixed a parallel modification of the HierarchyData variable in the topic SDK, which could lead to rare crashes of the process.
* [22556](https://github.com/ydb-platform/ydb/pull/22556) - fix false-positive unresponsive tablet issues in healthcheck during restarts https://github.com/ydb-platform/ydb/issues/22390.
* [22518](https://github.com/ydb-platform/ydb/pull/22518) - Fix default values for Kafka protocol.
* [22168](https://github.com/ydb-platform/ydb/pull/22168) - Fix crashing on CS init when all nodes restart simultaneously.
* [21515](https://github.com/ydb-platform/ydb/pull/21515) - Fix usage of storage until alter the topic.
* [21299](https://github.com/ydb-platform/ydb/pull/21299) - fix compaction max inflight.
* [22160](https://github.com/ydb-platform/ydb/pull/22160) - Temporary fix for: https://github.com/ydb-platform/ydb/issues/21207.
* [21698](https://github.com/ydb-platform/ydb/pull/21698) - Fix for: https://github.com/ydb-platform/ydb/issues/21240.
* [22042](https://github.com/ydb-platform/ydb/pull/22042) - Fix: Async index check if size of index columns is less than 1MB. https://github.com/ydb-platform/ydb/issues/14386.
* [22021](https://github.com/ydb-platform/ydb/pull/22021) - ensure tablets are launched after nodes stop being overloaded https://github.com/ydb-platform/ydb/issues/22030.
* [21839](https://github.com/ydb-platform/ydb/pull/21839) - Fixes #20524 using coroutines for waiting query to be created.
* [21883](https://github.com/ydb-platform/ydb/pull/21883) - Support in asynchronous replication new kind of change record — `reset` record (in addition to `update` & `erase` records).
* [21377](https://github.com/ydb-platform/ydb/pull/21377) - Fix issue where dedicated database deletion may leave database system tablets improperly cleaned.
* [21815](https://github.com/ydb-platform/ydb/pull/21815) - Fixed an [issue](https://github.com/ydb-platform/ydb/issues/21814) where a replication instance with an unspecified `COMMIT_INTERVAL` option caused the process to crash.
* [21746](https://github.com/ydb-platform/ydb/pull/21746) - fixes crash, described in #21744.
* [21702](https://github.com/ydb-platform/ydb/pull/21702) - Fix for https://github.com/ydb-platform/ydb/issues/21275 and https://github.com/ydb-platform/ydb/issues/21673.
* [21613](https://github.com/ydb-platform/ydb/pull/21613) - Fixed race in create s3 read actor: https://github.com/ydb-platform/ydb/issues/21240.
* [21412](https://github.com/ydb-platform/ydb/pull/21412) - Fixed releasing of non locked partitions.
* [21384](https://github.com/ydb-platform/ydb/pull/21384) - Fix for https://github.com/ydb-platform/ydb/issues/21156.
* [21424](https://github.com/ydb-platform/ydb/pull/21424) - Fixed temp dir owner id column name due to compatibility fail with 25-1-3.
* [21322](https://github.com/ydb-platform/ydb/pull/21322) - MaxChunksToDefragInflight didn't work when changed from ICB UI.
* [21314](https://github.com/ydb-platform/ydb/pull/21314) - Add gettxtype in txwrite.
* [21235](https://github.com/ydb-platform/ydb/pull/21235) - Fixed reversed sorting data reading. It caused deleted and outdated data to appear in query responses.
* [21317](https://github.com/ydb-platform/ydb/pull/21317) - Added validation for reading variant type from s3.
* [20683](https://github.com/ydb-platform/ydb/pull/20683) - Fixed temp dir owner actor id serialization.
* [20946](https://github.com/ydb-platform/ydb/pull/20946) - Fix BSC config proto.
* [20081](https://github.com/ydb-platform/ydb/pull/20081) - Improved s3 read / write partitions validation.
* [20974](https://github.com/ydb-platform/ydb/pull/20974) - Fixed url escape during schema inference.
* [20916](https://github.com/ydb-platform/ydb/pull/20916) - [Ticket](https://github.com/ydb-platform/ydb/issues/20833) .
* [20863](https://github.com/ydb-platform/ydb/pull/20863) - fixes crash when request was made with URL longer than 2048 bytes and DEBUG level for logging HTTP is active closes #20859.
* [20673](https://github.com/ydb-platform/ydb/pull/20673) - Fixed ast output for ctas.
* [20748](https://github.com/ydb-platform/ydb/pull/20748) - Fixed the [KesusQuoterService freeze](https://github.com/ydb-platform/ydb/issues/20747) in case of several unsuccessful attempts to connect to the Kesus tablet.
* [20720](https://github.com/ydb-platform/ydb/pull/20720) - return CORS to bad 403 response https://github.com/ydb-platform/ydb/issues/20682.
* [20733](https://github.com/ydb-platform/ydb/pull/20733) - Fixed race in topic to table transactions in C++ SDK.
* [20681](https://github.com/ydb-platform/ydb/pull/20681) - fix incorrect error reporting https://github.com/ydb-platform/ydb/issues/20682.
* [20495](https://github.com/ydb-platform/ydb/pull/20495) - cpu time has been fixed.
* [20434](https://github.com/ydb-platform/ydb/pull/20434) - there was asan issue after https://github.com/ydb-platform/ydb/pull/19674 . here is the proper fix ticket https://github.com/ydb-platform/ydb/issues/20437.
* [20307](https://github.com/ydb-platform/ydb/pull/20307) - [Ticket](https://github.com/ydb-platform/ydb/issues/20293).
* [20175](https://github.com/ydb-platform/ydb/pull/20175) - [KQP] Add ShuffleEliminated flag.
* [20269](https://github.com/ydb-platform/ydb/pull/20269) - Fixed verify fail during concurrent alter for: * external data source * external table * resource pool Also fixed status code for object creation fails.
* [20243](https://github.com/ydb-platform/ydb/pull/20243) - If the CDC stream was recorded in an auto-partitioned topic, then it could stop after several splits of the topic. In this case, modification of rows in the table would result in the error that the table is overloaded.
* [20223](https://github.com/ydb-platform/ydb/pull/20223) - Do not fold SafeCast because it could produce unexpected result Fix for KIKIMR-23489.
* [20172](https://github.com/ydb-platform/ydb/pull/20172) - [KQP / RBO] Return old behavior for yql_dq_key_input (as before shuffle elimination).
* [20138](https://github.com/ydb-platform/ydb/pull/20138) - Fix PDisk SIGSEGV on dangling TLight reference.
* [19901](https://github.com/ydb-platform/ydb/pull/19901) - If writing is done to the topic using a transaction and the retention of messages in the topic is less than the duration of the transaction, then inconsistent data could be written to the partition.
* [19860](https://github.com/ydb-platform/ydb/pull/19860) - Fix compilation error darwin.
* [19677](https://github.com/ydb-platform/ydb/pull/19677) - make nodes less critical (to make cluster less critical), closes #19676.
* [19399](https://github.com/ydb-platform/ydb/pull/19399) - Add setting to configure drain timeout before node shutdown.
* [19396](https://github.com/ydb-platform/ydb/pull/19396) - Fixed race in compute scheduler.
* [19424](https://github.com/ydb-platform/ydb/pull/19424) - Fixed missing step in EvReadSetAck, enabled skipped tests related to the fix.
* [19341](https://github.com/ydb-platform/ydb/pull/19341) - Сoncurrent execution of rw-transactions and completion of the initial scan or online index building can lead to disruption of persistent locks and loss of a schema snapshot used for serialization of CDC records.
* [19298](https://github.com/ydb-platform/ydb/pull/19298) - Fix CS crahing on scan failure.
* [19019](https://github.com/ydb-platform/ydb/pull/19019) - Fix not consistent generation counters for data erasure in SchemeShard tablet and BSC tablet.
* [19094](https://github.com/ydb-platform/ydb/pull/19094) - fixes issue in stream lookup join https://github.com/ydb-platform/ydb/issues/19083.
* [19048](https://github.com/ydb-platform/ydb/pull/19048) - fixes crash on double pass away, closes #19044.
* [18997](https://github.com/ydb-platform/ydb/pull/18997) - Fixed a bug where AwaitingDecisions remained non-empty even when there were no commitTxIds present.
* [18930](https://github.com/ydb-platform/ydb/pull/18930) - Fixed aws auth in timezones.
* [18764](https://github.com/ydb-platform/ydb/pull/18764) - 1. Исправлена обработка OlapApply внутри CS для Timestamp 2. Добавлена валидация на негативные timestamp при заливке данных через BulkUpsert https://github.com/ydb-platform/ydb/issues/18747.
* [18819](https://github.com/ydb-platform/ydb/pull/18819) - Fixed s3 provider win build.
* [18752](https://github.com/ydb-platform/ydb/pull/18752) - fix for https://st.yandex-team.ru/YQL-19988.
* [18462](https://github.com/ydb-platform/ydb/pull/18462) - Fixed script execution id parsing.
* [18701](https://github.com/ydb-platform/ydb/pull/18701) - Fixed topic SDK flaky tests: https://github.com/ydb-platform/ydb/issues/17867, https://github.com/ydb-platform/ydb/issues/17985, https://github.com/ydb-platform/ydb/issues/17867.
* [18664](https://github.com/ydb-platform/ydb/pull/18664) - In the table description columns are returned in the same order as they were specified in CREATE TABLE.
* [18553](https://github.com/ydb-platform/ydb/pull/18553) - fixes list of nodes and databases in broken environment closes #16477.
* [18594](https://github.com/ydb-platform/ydb/pull/18594) - [Kafka API] fix auth flags check.
* [18601](https://github.com/ydb-platform/ydb/pull/18601) - fix a bug where node memory usage was not tracked https://github.com/ydb-platform/ydb/issues/18576.
* [18607](https://github.com/ydb-platform/ydb/pull/18607) - Fixed fqrun start without queries.
* [18502](https://github.com/ydb-platform/ydb/pull/18502) - Remove template specialization redundancy error issued in https://st.yandex-team.ru/TAXIARCH-558.
* [18475](https://github.com/ydb-platform/ydb/pull/18475) - Avoid expensive table merge checks when operation inflight limits have already been exceeded. Fixes #18473.
* [18460](https://github.com/ydb-platform/ydb/pull/18460) - Fixed operation id ProtoToString. Empty operation id allowed in get scriptexec operation response (in case of NOT_FOUND error).
* [18307](https://github.com/ydb-platform/ydb/pull/18307) - The serialized TEvReadSet takes up a lot of memory.
* [18198](https://github.com/ydb-platform/ydb/pull/18198) - Improved S3 read / write schema validation.
* [18222](https://github.com/ydb-platform/ydb/pull/18222) - Fix `ydb operation get` not working for running operations (#17001).
* [18201](https://github.com/ydb-platform/ydb/pull/18201) - Table auto splitting-merging: Fixed crash when selecting split key from access samples containing a mix of full key and key prefix operations (e.g. exact/range reads).
* [18226](https://github.com/ydb-platform/ydb/pull/18226) - Fixi memory leak in persqueue write session actor.
* [15974](https://github.com/ydb-platform/ydb/pull/15974) - fixes for handling per-dc followers created on older YDB versions https://github.com/ydb-platform/ydb/issues/16000.
* [18157](https://github.com/ydb-platform/ydb/pull/18157) - Add new handler we saw on our cluster to get readable error instead of VERIFY.
* [17836](https://github.com/ydb-platform/ydb/pull/17836) - Fix segfault that could happen while retrying Whiteboard requests https://github.com/ydb-platform/ydb/issues/18145.
* [17027](https://github.com/ydb-platform/ydb/pull/17027) - - Bug fixes for direct read in topics.
* [18079](https://github.com/ydb-platform/ydb/pull/18079) - Fix reading big messages in Kafka Proxy.
* [18091](https://github.com/ydb-platform/ydb/pull/18091) - - Fix: in rare cases `workload topic write` command could segfault.
* [18072](https://github.com/ydb-platform/ydb/pull/18072) - Issue #18071 The metric value is reset to zero when the `TEvPQ::TEvPartitionCounters` event arrives. Added a re-calculation of the values.
* [18062](https://github.com/ydb-platform/ydb/pull/18062) - fixing crash in /viewer/storage handler, closes https://github.com/ydb-platform/ydb/issues/17813.
* [17830](https://github.com/ydb-platform/ydb/pull/17830) - Fix a bug in writing chunk metadata that makes older versions incompatible with 24-4-analytics. #17791.
* [17913](https://github.com/ydb-platform/ydb/pull/17913) - Don't wait for TEvReadSetAck from non-existent tablets.
* [17925](https://github.com/ydb-platform/ydb/pull/17925) - Fixed ReadRows rpc timeouts while datashard tablet restarts.
* [17895](https://github.com/ydb-platform/ydb/pull/17895) - Passed query service config into SS with app data (fixed delay with console configs).
* [17842](https://github.com/ydb-platform/ydb/pull/17842) - The PQ tablet loses its TEvReadSet.
* [17809](https://github.com/ydb-platform/ydb/pull/17809) - Check row count change in the main thread https://github.com/ydb-platform/ydb/issues/16949.
* [17570](https://github.com/ydb-platform/ydb/pull/17570) - Fix for CVE-2023-33460: Memory leak in yajl 2.1.0 with use of yajl_tree_parse function.
* [16072](https://github.com/ydb-platform/ydb/pull/16072) - currently all [0,1) value go to 0.5ms bucket since due to round down all these numbers become 0ms [1, 2) go to 1ms bucket and so on fix this error by using double Collect function available in Histogram.
* [17556](https://github.com/ydb-platform/ydb/pull/17556) - Fix S3 export multipart upload: EntityTooSmall error https://github.com/ydb-platform/ydb/issues/16873.
* [17335](https://github.com/ydb-platform/ydb/pull/17335) - Fix crash on direct reads restore.
* [17497](https://github.com/ydb-platform/ydb/pull/17497) - The PQ tablet does not receive a TEvTxCalcPredicateResult.
* [17290](https://github.com/ydb-platform/ydb/pull/17290) - Copy table should not check feature flags for columns types. If the types in original table are created then they should be allowed in destination table.
* [17192](https://github.com/ydb-platform/ydb/pull/17192) - Fixed https://github.com/ydb-platform/ydb-cpp-sdk/issues/399 register of GZIP and ZSTD codecs in C++ SDK topic client.
* [17116](https://github.com/ydb-platform/ydb/pull/17116) - Issue #17118 The message `TEvDeletePartition` may arrive earlier than `TEvApproveWriteQuota`. The batch did not send `TEvConsumed` and this blocked the queue of write quota requests.
* [16713](https://github.com/ydb-platform/ydb/pull/16713) - ensure the process of booting tablets does not get stuck in the presence of tablets that are not allowed to be launched.
* [17063](https://github.com/ydb-platform/ydb/pull/17063) - * Rename NYql::NGeneric -> NYql::Generic.
* [16975](https://github.com/ydb-platform/ydb/pull/16975) - Added nested protos validation for result type and issues.
* [16986](https://github.com/ydb-platform/ydb/pull/16986) - Added validation for ShuffleLeftSideBy / ShuffleRightSideBy.
* [16940](https://github.com/ydb-platform/ydb/pull/16940) - set a default value for `layoutCorrect`.
* [16902](https://github.com/ydb-platform/ydb/pull/16902) - Fixed s3 write with large path.
* [16958](https://github.com/ydb-platform/ydb/pull/16958) - Fixed unauthorized error in `ydb admin database restore` when multiple database admins are in dump. https://github.com/ydb-platform/ydb/issues/16833.
* [16943](https://github.com/ydb-platform/ydb/pull/16943) - Fixed scheme error in `ydb admin cluster dump` when specifying a domain database. https://github.com/ydb-platform/ydb/issues/16262.
* [16974](https://github.com/ydb-platform/ydb/pull/16974) - YQ kqprun, fixed settings passing.
* [16842](https://github.com/ydb-platform/ydb/pull/16842) - Fix histograms usage in KQP.
* [16837](https://github.com/ydb-platform/ydb/pull/16837) - Fix comp nodes logging in case when it's disabled.
* [16790](https://github.com/ydb-platform/ydb/pull/16790) - * YDB FQ: fix creation of MySQL connections.
* [16795](https://github.com/ydb-platform/ydb/pull/16795) - YQ kqprun, fixed storage setupp with domains.
* [16768](https://github.com/ydb-platform/ydb/pull/16768) - Fixed altering of max_active_partition property of the topic with YQL query.
* [16734](https://github.com/ydb-platform/ydb/pull/16734) - Fix TStartPartitionSessionEvent::Confirm.
* [16722](https://github.com/ydb-platform/ydb/pull/16722) - Here was new feature ALTER DATABASE OWNER TO on SQL: https://github.com/ydb-platform/ydb/pull/14403 Then, it was [discovered](https://github.com/ydb-platform/ydb/issues/16430) that unit-test in KQP was flapping. There is fix with waiting tenant's up.
* [16125](https://github.com/ydb-platform/ydb/pull/16125) - Fix bugs possibly leading to crash or unexpected client error during direct read session restore.
* [16588](https://github.com/ydb-platform/ydb/pull/16588) - Fix vanishing node bug.
* [16438](https://github.com/ydb-platform/ydb/pull/16438) - Fix for reading from column shard without specifying any columns.
* [16583](https://github.com/ydb-platform/ydb/pull/16583) - YQ fixed bugs with grpc endpoints in kqprun.
* [15850](https://github.com/ydb-platform/ydb/pull/15850) - Fix optional columns handling in read_rows rpc #15701.
* [16413](https://github.com/ydb-platform/ydb/pull/16413) - Currently, messages from the topic are deleted upon the occurrence of one of the events: the size of messages stored in the topic has been exceeded or the message lifetime has expired.
* [16506](https://github.com/ydb-platform/ydb/pull/16506) - YQ make create database operation sync.
* [16389](https://github.com/ydb-platform/ydb/pull/16389) - fix database mapping and add name filter, closes https://github.com/ydb-platform/ydb/issues/13279.
* [16315](https://github.com/ydb-platform/ydb/pull/16315) - YQ-4170 fixed external table alter.
* [16049](https://github.com/ydb-platform/ydb/pull/16049) - YQ-3655 added block splitting into DQ output channel.
* [16297](https://github.com/ydb-platform/ydb/pull/16297) - YQ-4207 used database name from table by default.
* [16369](https://github.com/ydb-platform/ydb/pull/16369) - Fixed use-after-move vulnerability in C++ SDK Topic Client.
* [15281](https://github.com/ydb-platform/ydb/pull/15281) - Fix SeqNo conflict for concurrent transactions using same SourceId.
* [16351](https://github.com/ydb-platform/ydb/pull/16351) - Fix bug in auth for kafka proxy.
* [16216](https://github.com/ydb-platform/ydb/pull/16216) - The `PQ` tablet can receive the message `TEvPersQueue::TEvProposeTransaction` twice from the `SS`. For example, when there are delays in the operation of the `IC`. As a result, the `Drop Tablet` operation may hang in the `PQ` tablet. #16218.
* [16193](https://github.com/ydb-platform/ydb/pull/16193) - Fixed sls database creation errors handling.
* [16151](https://github.com/ydb-platform/ydb/pull/16151) - Fix VDisk compaction bug.
* [16060](https://github.com/ydb-platform/ydb/pull/16060) - fix returning clause in upsert/insert on missing input columns.
* [11217](https://github.com/ydb-platform/ydb/pull/11217) - add simple lookup + orderby test.
* [15386](https://github.com/ydb-platform/ydb/pull/15386) - Fix donor's BSQueue not ready slows down replication.
* [15899](https://github.com/ydb-platform/ydb/pull/15899) - Inflight limit for ReadRows to prevent dynnodes OOM.
* [15889](https://github.com/ydb-platform/ydb/pull/15889) - At the start, the partition loads the list of consumers not only from the config, but also from the KV. Fixed a bug where the background partition had an empty list of config consumers, but the consumers were stored in KV. Issue #15826.
* [15931](https://github.com/ydb-platform/ydb/pull/15931) - Don't crash after portion has stopped being optimized in tiering actializer. [#15684](https://github.com/ydb-platform/ydb/issues/15684).
* [15878](https://github.com/ydb-platform/ydb/pull/15878) - Initialize field Engine before write to local database, to make possible to work by branch stable-24-3-15 .
* [15874](https://github.com/ydb-platform/ydb/pull/15874) - * Fixed `BackoffTimeout_` overflow in IAM credentials provider.
* [15847](https://github.com/ydb-platform/ydb/pull/15847) - Fix config delivery to quoter on startup.
* [15720](https://github.com/ydb-platform/ydb/pull/15720) - Fixes invalid data shards histograms when `enable_local_dbbtree_index ` is enabled (#15235).
* [15773](https://github.com/ydb-platform/ydb/pull/15773) - There's nothing to tell about this change, but 20 characters are required.
* [15721](https://github.com/ydb-platform/ydb/pull/15721) - Fix UUID type conversion in ReadTable.
* [15730](https://github.com/ydb-platform/ydb/pull/15730) - Precharge for external blobs in DataShard Read Iterator Keys request (#14707).
* [15520](https://github.com/ydb-platform/ydb/pull/15520) - Хотим дожидаться данных из BS в любом случае даже в случае FastRead по аналогии с DataShard.
* [15373](https://github.com/ydb-platform/ydb/pull/15373) - Cost-based optimizer was stripping aliases from column names, but turns out EquiJoin should not have aliases in the first place, but column names actually may contain '.' https://github.com/ydb-platform/ydb/issues/15372.
* [15453](https://github.com/ydb-platform/ydb/pull/15453) - Unifying column names validation for both row and column tables #14722.
* [15479](https://github.com/ydb-platform/ydb/pull/15479) - direct read fix.
* [15557](https://github.com/ydb-platform/ydb/pull/15557) - At startup, the background partition used the configuration of the main partition. #15559.
* [15516](https://github.com/ydb-platform/ydb/pull/15516) - Fix bug when sanitizer caused nullptr dereference https://github.com/ydb-platform/ydb/issues/15519 Add stress tests for sanitizer with INACTIVE/FAULTY statuses.
* [15542](https://github.com/ydb-platform/ydb/pull/15542) - fix tabletid in TBuildSlicesTask.
* [15314](https://github.com/ydb-platform/ydb/pull/15314) - Fix handle password complexity default values.
* [15439](https://github.com/ydb-platform/ydb/pull/15439) - Fix the bug causing possible loss of partition read request.
* [15400](https://github.com/ydb-platform/ydb/pull/15400) - Fix VDisk premature donor drop.
* [15370](https://github.com/ydb-platform/ydb/pull/15370) - supported 7 `config.yaml` style options in ydb_configure (e.g. audit_config, see PR for details).
* [15375](https://github.com/ydb-platform/ydb/pull/15375) - Fix authentification in BSC/Distconf (#15071).
* [15253](https://github.com/ydb-platform/ydb/pull/15253) - Fix fetch and replace for database config #14787.
* [15341](https://github.com/ydb-platform/ydb/pull/15341) - fix for unstable version grouping, closes #14827.
* [15327](https://github.com/ydb-platform/ydb/pull/15327) - FetchScriptResults. Fix crash on parsing of incorrect operation id https://github.com/ydb-platform/ydb/issues/15315.
* [15297](https://github.com/ydb-platform/ydb/pull/15297) - don't return childrenExist prop for databases, closes #15256.
* [15331](https://github.com/ydb-platform/ydb/pull/15331) - Fix sync to cmakebuild flow.
* [15311](https://github.com/ydb-platform/ydb/pull/15311) - The alias name was incorrectly determined for YT CBO usage An alias may contain a '.' character, but we were mishandling this case https://github.com/ydb-platform/ydb/issues/15317.
* [15290](https://github.com/ydb-platform/ydb/pull/15290) - switch to scheme shard describe to get storage stats closes #14180.
* [15288](https://github.com/ydb-platform/ydb/pull/15288) - Fixed possible stop of reading from the topic partition.
* [15258](https://github.com/ydb-platform/ydb/pull/15258) - correct uptime group-by in viewer/nodes handler, closes #14992.
* [15254](https://github.com/ydb-platform/ydb/pull/15254) - Drop scheme entries in order in scenario tests.
* [15134](https://github.com/ydb-platform/ydb/pull/15134) - fix a bug where a tablet would remain inactive after failing during promotion from follower to leader https://github.com/ydb-platform/ydb/issues/14961.
* [15117](https://github.com/ydb-platform/ydb/pull/15117) - after the commit https://github.com/ydb-platform/ydb/pull/13316, a poller actor is needed in oidc_proxy and meta. Without this, the build hangs and does not work.
* [15115](https://github.com/ydb-platform/ydb/pull/15115) - client-id doesn't fill right.
* [14567](https://github.com/ydb-platform/ydb/pull/14567) - Break loop dependency in TDataAccessorRequest destructor.
* [15088](https://github.com/ydb-platform/ydb/pull/15088) - A pointer to the transaction was stored in the recorded message. This could lead to a `use-heap-after-free` error in the `linux-x86_64-release-asan` configuration #15089.
* [15063](https://github.com/ydb-platform/ydb/pull/15063) - Fix memory leak in ldap auth provider.
* [15049](https://github.com/ydb-platform/ydb/pull/15049) - Fixed a rare assertion (process crash) when followers attached to leaders with an inconsistent snapshot. Fixes #15042.
* [15010](https://github.com/ydb-platform/ydb/pull/15010) - Refresh ldap token if it get retryable error previously https://github.com/ydb-platform/ydb/issues/14324.
* [14957](https://github.com/ydb-platform/ydb/pull/14957) - Fix #14903 Return error from datashard instead of UNAVAILABLE in case of exceeded retry limit.
* [14931](https://github.com/ydb-platform/ydb/pull/14931) - Fixed race on RM initialization, issue: https://github.com/ydb-platform/ydb/issues/14932.
#### YDB UI

* Updated authentication handling for the viewer's [`whoami` and `capabilities` handlers](https://github.com/ydb-platform/ydb/pull/17942).
* Fixed a viewer issue where [PDisk information requests could time out when the target node was disconnected or unavailable](https://github.com/ydb-platform/ydb/pull/20432).
* Fixed several additional viewer issues, including [retrieval of tablet lists for tables with secondary indexes](https://github.com/ydb-platform/ydb/pull/17157), [loss of double precision during serialization](https://github.com/ydb-platform/ydb/pull/18056), and [data duplication during memory reallocation while processing incoming chunks](https://github.com/ydb-platform/ydb/pull/20929).

##### Additional PR-level changes

* [19680](https://github.com/ydb-platform/ydb/pull/19680) - Vector select workload.
* [15530](https://github.com/ydb-platform/ydb/pull/15530) - Add configuration version in configs_dispatcher page.
## Version 25.2 {#25-2}

### Version 25.2.1.24 {#25-2-1-24}

Release date: January 28, 2026.

#### Bug Fixes

* [Fixed](https://github.com/ydb-platform/ydb/pull/25112) an [issue](https://github.com/ydb-platform/ydb/issues/23858) where [tablet](./concepts/glossary.md#tablet) deletion might get stuck
* [Fixed](https://github.com/ydb-platform/ydb/pull/25145) an [issue](https://github.com/ydb-platform/ydb/issues/20866) that caused an error when changing a table's follower
* Fixed a couple of [changefeed](./concepts/glossary.md#changefeed) related issues:
  * [Fixed](https://github.com/ydb-platform/ydb/pull/25689) an [issue](https://github.com/ydb-platform/ydb/issues/25524) where importing a table with a Utf8 primary key and an enabled changefeed could fail
  * [Fixed](https://github.com/ydb-platform/ydb/pull/25453) an [issue](https://github.com/ydb-platform/ydb/issues/25454) where importing a table without changefeeds could fail due to incorrect changefeed file lookup.
* [Fixed](https://github.com/ydb-platform/ydb/pull/26069) an [issue](https://github.com/ydb-platform/ydb/issues/25869) that could cause errors during UPSERT operations in column tables.
* [Fixed](https://github.com/ydb-platform/ydb/pull/26504) an [error](https://github.com/ydb-platform/ydb/issues/26225) that could cause a crash due to accessing freed memory
* [Fixed](https://github.com/ydb-platform/ydb/pull/26657) an [issue](https://github.com/ydb-platform/ydb/issues/23122) with duplicates in unique secondary index
* [Fixed](https://github.com/ydb-platform/ydb/pull/26879) an [issue](https://github.com/ydb-platform/ydb/issues/26565) with checksum mismatch error on restoration compressed backup from s3
* [Fixed](https://github.com/ydb-platform/ydb/pull/27528) an [issue](https://github.com/ydb-platform/ydb/issues/27193) where some queries from the TPC-H 1000 benchmark could fail
* Fixed a couple of cluster bootstrap related issues:
  * [Fixed](https://github.com/ydb-platform/ydb/pull/25678) an [issue](https://github.com/ydb-platform/ydb/issues/25023) where cluster bootstrap could hang when mandatory authorization was enabled.
  * [Fixed](https://github.com/ydb-platform/ydb/pull/28886) an [issue](https://github.com/ydb-platform/ydb/issues/27228) where it was impossible to create new databases for several minutes immediately after cluster deployment
* [Fixed](https://github.com/ydb-platform/ydb/pull/28655) an [issue](https://github.com/ydb-platform/ydb/issues/28510) where race condition could occur and clients receive `Could not find correct token validator` error when mising newly issued tokens before `LoginProvider` state is updated.
* [Fixed](https://github.com/ydb-platform/ydb/pull/29940) an [issue](https://github.com/ydb-platform/ydb/issues/29903) where named expression containing another named expression caused incorrect `VIEW` backup

### Release candidate 25.2.1.10 {#25-2-1-10-rc}

Release date: September 21, 2025.

#### Functionality

* [Analytical capabilities](./concepts/analytics/index.md) are available by default: [column-oriented tables](./concepts/datamodel/table.md?version=v25.2#column-oriented-tables) can be created without special flags, using LZ4 compression and hash partitioning. Supported operations include a wide range of DML operations (UPDATE, DELETE, UPSERT, INSERT INTO ... SELECT) and CREATE TABLE AS SELECT. Integration with dbt, Apache Airflow, Jupyter, Superset, and federated queries to S3 enables building end-to-end analytical pipelines in YDB.
* [Cost-Based Optimizer](./concepts/optimizer.md?version=v25.2) is enabled by default for queries involving at least one column-oriented table but can also be enabled manually for other queries. The Cost-Based Optimizer improves query performance by determining the optimal join order and join types based on table statistics; supported [hints](./dev/query-hints.md) allow fine-tuning execution plans for complex analytical queries.
* Added YDB Transfer – an asynchronous mechanism for transferring data from a topic to a table. You can create a transfer, update or delete it using YQL commands.
* Added [spilling](./concepts/spilling.md?version=v25.2), a memory management mechanism, that temporarily offloads intermediate data arising from computations and exceeding available node RAM capacity to external storage. Spilling allows executing user queries that require processing large data volumes exceeding available node memory.
* Increased the [maximum amount of time allowed for a single query to execute](./concepts/limits-ydb?version=v25.2) from 30 minutes to 2 hours.
* Added support for a user-defined Certificate Authority (CA) and [Yandex Cloud Identity and Access Management (IAM)](https://yandex.cloud/ru/docs/iam) authentication in [asynchronous replication](./yql/reference/syntax/create-async-replication.md?version=v25.2).
* Enabled by default:

  * [vector index](./dev/vector-indexes.md?version=v25.2) for approximate vector similarity search,
  * support for [client-side consumer balancing](https://www.confluent.io/blog/cooperative-rebalancing-in-kafka-streams-consumer-ksqldb), [compacted topics](https://docs.confluent.io/kafka/design/log_compaction.html) and [transactions](https://www.confluent.io/blog/transactions-apache-kafka/) in [YDB Topics Kafka API](./reference/kafka-api/index.md?version=v25.2),
  * support for [auto-partitioning topics](./concepts/cdc.md?version=v25.2#topic-partitions) for row-oriented tables in CDC,
  * support for auto-partitioning topics in asynchronous replication,
  * support for [parameterized Decimal type](./yql/reference/types/primitive.md?version=v25.2#numeric),
  * support for [Datetime64 data type](./yql/reference/types/primitive.md?version=v25.2#datetime),
  * automatic cleanup of temporary tables and directories during export to S3,
  * support for [changefeeds](./concepts/cdc.md?version=v25.2) in backup and restore operations,
  * the ability to [enable followers (read replicas)](./yql/reference/syntax/alter_table/indexes.md?version=v25.2) for covered secondary indexes,
  * system views with [history of overloaded partitions](./dev/system-views.md?version=v25.2#top-overload-partitions).

#### Bug Fixes

* [Fixed](https://github.com/ydb-platform/ydb/pull/24265) CPU resource limiting for column-oriented tables in Workload Manager. Previously CPU consumption could exceed the configured limits.


## Version 25.1 {#25-1}

### Version 25.1.4.7 {#25-1-4-7}

Release date: September 15, 2025.

#### Functionality

* [Added](https://github.com/ydb-platform/ydb/pull/21119) support for the Kafka frameworks, such as Kafka Connect, Kafka Streams, Confluent Schema Registry, Kafka Streams, Apache Flink, etc. Now [YDB Topics Kafka API](./reference/kafka-api/index.md) supports the following features:
  * client-side consumer balancing. To enable it, use the `enable_kafka_native_balancing` flag in the [cluster configuration](./reference/configuration/index.md). For for information, see [How consumer balancing works in Apache Kafka](https://www.confluent.io/blog/cooperative-rebalancing-in-kafka-streams-consumer-ksqldb/). When enabled, consumer balancing will work the same way in YDB Topics.
  * [compacted topics](https://docs.confluent.io/kafka/design/log_compaction.html). To enable topic compaction, use the `enable_topic_compactification_by_key` flag.
  * [transactions](https://www.confluent.io/blog/transactions-apache-kafka/). To enable transactions, use the `enable_kafka_transactions` flag.
* [Added](https://github.com/ydb-platform/ydb/pull/20982) a [new protocol](https://github.com/ydb-platform/ydb/issues/11064) to [Node Broker](./concepts/glossary.md#node-broker) that eliminates the long startup of nodes on large clusters (more than 1000 servers).

#### YDB UI

* [Fixed](https://github.com/ydb-platform/ydb/pull/17839) an [issue](https://github.com/ydb-platform/ydb-embedded-ui/issues/18615) where not all tablets are shown for pers queue group on the tablets tab in diagnostics.
* Fixed an [issue](https://github.com/ydb-platform/ydb/issues/18735) where the storage tab on the diagnostics page displayed nodes of other types in addition to storage nodes.
* Fixed a [serialization issue](https://github.com/ydb-platform/ydb-embedded-ui/issues/2164) that caused an error when opening query execution statistics.
* Changed the logic for nodes transitioning to critical state – the CPU pool, which is 75-99% full, now triggers a warning, not a critical state.

#### Performance

* [Optimized](https://github.com/ydb-platform/ydb/pull/20197) processing of empty inputs when performing JOIN operations.

#### Bug fixes

* [Added support](https://github.com/ydb-platform/ydb/pull/21918) for a new kind of change record in asynchronous replication — `reset` record (in addition to `update` & `erase` records).
* [Fixed](https://github.com/ydb-platform/ydb/pull/21836) an [issue](https://github.com/ydb-platform/ydb/issues/21814) where a replication instance with an unspecified `COMMIT_INTERVAL` option caused the process to crash.
* [Fixed](https://github.com/ydb-platform/ydb/pull/21652) rare errors when reading from a topic during partition balancing.
* [Fixed](https://github.com/ydb-platform/ydb/pull/22455) an [issue](https://github.com/ydb-platform/ydb/issues/19842) where dedicated database deletion might leave database system tablets improperly cleaned.
* [Fixed](https://github.com/ydb-platform/ydb/pull/22203) an [issue](https://github.com/ydb-platform/ydb/issues/22030) that caused tablets to hang when nodes experienced critical memory shortage. Now tablets will automatically start as soon as any of the nodes frees up sufficient resources.
* [Fixed](https://github.com/ydb-platform/ydb/pull/24278) an issue where only the first message from a batch was saved when writing Kafka messages, with all other messages in the batch being ignored.

### Release candidate 25.1.2.7 {#25-1-2-7-rc}

Release date: July 14, 2025.

#### Functionality

* [Implemented](https://github.com/ydb-platform/ydb/issues/19504) a [vector index](./dev/vector-indexes.md?version=v25.1) for approximate vector similarity search.
* [Added](https://github.com/ydb-platform/ydb/issues/11454) support for [consistent asynchronous replication](./concepts/async-replication.md?version=v25.1).
* Implemented [BATCH UPDATE](./yql/reference/syntax/batch-update?version=v25.1) and [BATCH DELETE](./yql/reference/syntax/batch-delete?version=v25.1) statements, allowing the application of changes to large row-oriented tables outside of transactional constraints. This mode is enabled by setting the `enable_batch_updates` flag in the cluster configuration.
* Added [configuration mechanism V2](./devops/configuration-management/configuration-v2/config-overview?version=v25.1) that simplifies the deployment of new {{ ydb-short-name }} clusters and further work with them. [Comparison](./devops/configuration-management/compare-configs?version=v25.1) of configuration mechanisms V1 and V2.
* Added support for the parameterized [Decimal type](./yql/reference/types/primitive.md?version=v25.1#numeric).
* [Added](https://github.com/ydb-platform/ydb/pull/8065) the ability to omit the `DECLARE` operator for query parameter type declarations. Parameter types are now automatically inferred from the provided values.
* [Implemented](https://github.com/ydb-platform/ydb/issues/18017) client balancing of partitions when reading using the [Kafka protocol](https://kafka.apache.org/documentation/#consumerconfigs_partition.assignment.strategy) (like Kafka itself). Previously, balancing took place on the server. This mode is enabled by setting the `enable_kafka_native_balancing` flag in the cluster configuration.
* Added support for [auto-partitioning topics](./concepts/cdc.md?version=v25.1#topic-partitions) for row-oriented tables in CDC. This mode is enabled by setting the `enable_topic_autopartitioning_for_cdc` flag in the cluster configuration.
* [Added](https://github.com/ydb-platform/ydb/pull/8264) the ability to [alter the retention period of CDC topics](./concepts/cdc.md?version=v25.1#topic-settings) using the `ALTER TOPIC` statement.
* [Added support](https://github.com/ydb-platform/ydb/pull/7052) for [the DEBEZIUM_JSON format](./concepts/cdc.md?version=v25.1#debezium-json-record-structure) for CDC.
* [Added](https://github.com/ydb-platform/ydb/pull/19507) the ability to create changefeed streams to index tables.
* [Added](https://github.com/ydb-platform/ydb/issues/19310) the ability to [enable followers (read replicas)](./yql/reference/syntax/alter_table/indexes.md?version=v25.1) for covered secondary indexes. This mode is enabled by setting the `enable_access_to_index_impl_tables` flag in the cluster configuration.
* The scope of supported objects in backup and restore operations has been expanded:
  * [Support for changefeeds](https://github.com/ydb-platform/ydb/issues/7054) (enabled with the `enable_changefeeds_export` and `enable_changefeeds_import` flags).
  * [Support for views](https://github.com/ydb-platform/ydb/issues/12724) (enabled with the `enable_view_export` flag).
* [Added](https://github.com/ydb-platform/ydb/issues/17734) automatic cleanup of temporary tables and directories during export to S3. This mode is enabled by setting the `enable_export_auto_dropping` flag in the cluster configuration.
* [Added](https://github.com/ydb-platform/ydb/pull/12909) automatic integrity checks of backups during import, which prevent restoration from corrupted backups and protect against data loss.
* [Added](https://github.com/ydb-platform/ydb/pull/15570) the ability to create views that refer to [UDFs](./yql/reference/builtins/basic?version=v25.1#udf) in queries.
* Added system views with information about [access right settings](./dev/system-views.md?version=v25.1#auth), [history of overloaded partitions](./dev/system-views.md?version=v25.1#top-overload-partitions) - enabled by setting the `enable_followers_stats` flag in the cluster configuration, [history of partitions with broken locks](./dev/system-views?version=v25.1#top-tli-partitions).
* Added new parameters to the [CREATE USER](./yql/reference/syntax/create-user.md?version=v25.1) and [ALTER USER](./yql/reference/syntax/alter-user.md?version=v25.1) operators:
  * `HASH` — sets a password in encrypted form.
  * `LOGIN` and `NOLOGIN` — unlocks and blocks a user, respectively.
* Enhanced account security:
  * [Added](https://github.com/ydb-platform/ydb/pull/11963) user [password complexity](./reference/configuration/?version=v25.1#password-complexity) verification.
  * [Implemented](https://github.com/ydb-platform/ydb/pull/12578) [automatic user lockout](./reference/configuration/?version=v25.1#account-lockout) after a specified number of failed attempts to enter the correct password.
  * [Added](https://github.com/ydb-platform/ydb/pull/12983) the ability for users to change their own passwords.
* [Implemented](https://github.com/ydb-platform/ydb/issues/9748) the ability to toggle functional flags at runtime. Changes to flags that do not specify `(RequireRestart) = true` in the [proto file](https://github.com/ydb-platform/ydb/blob/main/ydb/core/protos/feature_flags.proto#L60) are applied without a cluster restart.
* [Changed](https://github.com/ydb-platform/ydb/pull/11329) lock behavior when shard locks exceed the limit. Once the limit is exceeded, the oldest locks (rather than the newest) are converted into full-shard locks.
* [Implemented](https://github.com/ydb-platform/ydb/pull/12567) a mechanism to preserve optimistic locks in memory during graceful datashard restarts, reducing `ABORTED` errors caused by lock loss during table balancing.
* [Implemented](https://github.com/ydb-platform/ydb/pull/12689) a mechanism to abort volatile transactions with the `ABORTED` status during graceful datashard restarts.
* [Added](https://github.com/ydb-platform/ydb/pull/6342) support for removing `NOT NULL` constraints from a table column using the `ALTER TABLE ... ALTER COLUMN ... DROP NOT NULL` statement.
* [Added](https://github.com/ydb-platform/ydb/pull/9168) a limit of 100,000 concurrent session-creation requests in the coordination service.
* [Increased](https://github.com/ydb-platform/ydb/pull/14219) the maximum number of columns in the primary key from 20 to 30.
* Improved diagnostics and introspection of memory errors ([#10419](https://github.com/ydb-platform/ydb/pull/10419), [#11968](https://github.com/ydb-platform/ydb/pull/11968)).
* **_(Experimental)_** [Added](https://github.com/ydb-platform/ydb/pull/14075) an experimental mode with strict access control checks. This mode is enabled by setting these flags:
  * `enable_strict_acl_check` – do not allow granting rights to non-existent users and delete users with permissions;
  * `enable_strict_user_management` – enables strict checks for local users (i.e. only the cluster or database administrator can administer local users);
  * `enable_database_admin` – add the role of database administrator;

#### Backward Incompatible Changes

* If you are using queries that access named expressions as tables using the AS_TABLE function, update [temporary over YDB](https://github.com/yandex/temporal-over-ydb) to version [v1.23.0-ydb-compat](https://github.com/yandex/temporal-over-ydb/releases/tag/v1.23.0-ydb-compat) before updating {{ ydb-short-name }} to the current version to avoid errors in query execution.

#### YDB UI

* Query Editor was redesigned to [support partial results load](https://github.com/ydb-platform/ydb-embedded-ui/pull/1974) - it starts displaying results when receives a chunk from the server, doesn't have to wait until the query completion. This approach allows application developers to see query results faster.
* [Security Improvement](https://github.com/ydb-platform/ydb-embedded-ui/pull/1967): controls that are could not be activated by current user due to lack of permissions are not displayed. Users won't click and experience Access Denied error.
* [Added](https://github.com/ydb-platform/ydb-embedded-ui/pull/1981) search by tablet id on Tablets tab.
* HotKeys help tab accessible by ⌘+K key is added.
* Operations tab is added to Database page. Operations allow to list operations and cancel them.
* Cluster dashboard redesign and make it collapsable.
* JsonViewer: handle case sensitive search.
* Added code snippets for YDB SDK to connect to selected database. Such snippets must speed up development.
* Rows on Queries tab were sorted by string values after proper backend sort.
* QueryEditor: removed extra confirmation requests on leaving browser page – do not ask confirmation when it's irrelevant.

#### Performance

* [Added](https://github.com/ydb-platform/ydb/pull/6509) support for [constant folding](https://en.wikipedia.org/wiki/Constant_folding) in the query optimizer by default. This feature enhances query performance by evaluating constant expressions at compile time, thereby reducing runtime overhead and enabling faster, more efficient execution of complex static expressions.
* [Added](https://github.com/ydb-platform/ydb/issues/6512) a granular timecast protocol for distributed transactions, ensuring that slowing one shard does not affect the performance of others.
* [Implemented](https://github.com/ydb-platform/ydb/issues/11561) in-memory state migration on a graceful restart, preserving locks and improving transaction success rates. This reduces the execution time of long transactions by decreasing the number of retries.
* [Implemented](https://github.com/ydb-platform/ydb/issues/15255) pipeline processing of internal transactions in Node Broker, accelerating the startup of dynamic nodes in the cluster.
* [Improved](https://github.com/ydb-platform/ydb/pull/15607) Node Broker resilience under increased cluster load.
* [Enabled](https://github.com/ydb-platform/ydb/pull/19440) evictable B-Tree indexes by default instead of non-evictable SST indexes, reducing memory consumption when storing cold data.
* [Optimized](https://github.com/ydb-platform/ydb/pull/15264) memory consumption by storage nodes.
* [Reduced](https://github.com/ydb-platform/ydb/pull/10969) Hive startup times to 30%.
* [Optimized](https://github.com/ydb-platform/ydb/pull/6561) the distributed storage replication process.
* [Optimized](https://github.com/ydb-platform/ydb/pull/9491) the header size of large binary objects in VDisk.
* [Reduced](https://github.com/ydb-platform/ydb/pull/15517) memory consumption through allocator page cleaning.

#### Bug Fixes

* [Fixed](https://github.com/ydb-platform/ydb/pull/9707) an error in the [Interconnect](../concepts/glossary?version=v25.1#actor-system-interconnect) configuration that caused performance degradation.
* [Fixed](https://github.com/ydb-platform/ydb/pull/13993) an out-of-memory error that occurred when deleting very large tables by limiting the number of tablets that process this operation concurrently.
* [Fixed](https://github.com/ydb-platform/ydb/pull/9848) an issue that caused accidental duplicate entries in the system tablet configuration.
* [Fixed](https://github.com/ydb-platform/ydb/pull/11059) an issue where data reads took too long (seconds) during frequent table resharding operations.
* [Fixed](https://github.com/ydb-platform/ydb/pull/9723) an error reading from asynchronous replicas that caused failures.
* [Fixed](https://github.com/ydb-platform/ydb/pull/9507) an issue that caused rare [CDC](./dev/cdc.md?version=v25.1) initial scan freezes.
* [Fixed](https://github.com/ydb-platform/ydb/pull/11483) an issue handling incomplete schema transactions in datashards during system restart.
* [Fixed](https://github.com/ydb-platform/ydb/pull/10460) an issue causing inconsistent reads from a topic when explicitly confirming a message read within a transaction; users now receive an error when attempting to confirm a message.
* [Fixed](https://github.com/ydb-platform/ydb/pull/12220) an issue in which topic auto-partitioning functioned incorrectly within a transaction.
* [Fixed](https://github.com/ydb-platform/ydb/pull/12905) an issue in which transactions hang when working with topics during tablet restarts.
* [Fixed](https://github.com/ydb-platform/ydb/pull/13910) the "Key is out of range" error when importing data from S3-compatible storage.
* [Fixed](https://github.com/ydb-platform/ydb/pull/13741) an issue in which the end of the metadata field in the cluster configuration.
* [Improved](https://github.com/ydb-platform/ydb/pull/16420) the secondary index build process: the system now retries on certain errors instead of interrupting the build.
* [Fixed](https://github.com/ydb-platform/ydb/pull/16635) an error executing the `RETURNING` expression in `INSERT` and `UPSERT` operations.
* [Fixed](https://github.com/ydb-platform/ydb/pull/16269) an issue causing Drop Tablet operations in PQ tablets to hang during Interconnect delays.
* [Fixed](https://github.com/ydb-platform/ydb/pull/16194) an error during VDisk [compaction](../concepts/glossary?version=v25.1#compaction).
* [Fixed](https://github.com/ydb-platform/ydb/pull/15233) an issue in which long topic-reading sessions ended with "too big inflight" errors.
* [Fixed](https://github.com/ydb-platform/ydb/pull/15515) an issue where reading a topic by multiple consumers hangs if at least one partition has no incoming data.
* [Fixed](https://github.com/ydb-platform/ydb/pull/18614) a rare issue with PQ tablet restarts.
* [Fixed](https://github.com/ydb-platform/ydb/pull/18378) an issue in which, after updating the cluster version, Hive started subscribers in data centers without running database nodes.
* [Fixed](https://github.com/ydb-platform/ydb/pull/19057) an issue in which the `Failed to set up listener on port 9092 errno# 98 (Address already in use)` error occurred during version updates.
* [Fixed](https://github.com/ydb-platform/ydb/pull/18905) an error that led to a segmentation fault when a healthcheck request and a cluster-node disable request executed simultaneously.
* [Fixed](https://github.com/ydb-platform/ydb/pull/18899) an issue that caused partitioning of [row-oriented tables](./concepts/datamodel/table.md?version=v25.1#partitioning_row_table) to fail when a split key was selected from access samples containing a mix of full-key and key-prefix operations (such as exact and range reads).
* [Fixed](https://github.com/ydb-platform/ydb/pull/18647) an [issue](https://github.com/ydb-platform/ydb/issues/17885) where the index type defaulted to `GLOBAL SYNC` despite `UNIQUE` being explicitly specified.
* [Fixed](https://github.com/ydb-platform/ydb/pull/16797) an issue where topic auto-partitioning did not work when the `max_active_partition` parameter was set via `ALTER TOPIC`.
* [Fixed](https://github.com/ydb-platform/ydb/pull/18938) an issue that caused `db scheme describe` to return columns out of their original creation order.

## Version 24.4 {#24-4}

### Version 24.4.4.12 {#24-4-4-12}

Release date: June 3, 2025.

## Performance

* [Limited](https://github.com/ydb-platform/ydb/pull/17755) the number of internal inflight configuration updates.
* [Optimized](https://github.com/ydb-platform/ydb/issues/18289) memory consumption by PQ tablets.
* [Optimized](https://github.com/ydb-platform/ydb/issues/18473) CPU consumption of Scheme shard and reduced query latencies by checking operation count limits before performing tablet split and merge operations.

## Bug Fixes

* [Fixed](https://github.com/ydb-platform/ydb/pull/17123) a rare issue of client applications hanging during transaction commit where deleting partition had been done before write quota update.
* [Fixed](https://github.com/ydb-platform/ydb/pull/17312) an error in copying tables with Decimal type, which caused failures when rolling back to a previous version.
* [Fixed](https://github.com/ydb-platform/ydb/pull/17519) an [issue](https://github.com/ydb-platform/ydb/issues/17499) where a commit without confirmation of writing to a topic led to the blocking of the current and subsequent transactions with topics.
* Fixed transaction hanging when working with topics during tablet [restart](https://github.com/ydb-platform/ydb/issues/17843) or [deletion](https://github.com/ydb-platform/ydb/issues/17915).
* [Fixed](https://github.com/ydb-platform/ydb/pull/18114) [issues](https://github.com/ydb-platform/ydb/issues/18071) with reading messages larger than 6Mb via [Kafka API](./reference/kafka-api).
* [Fixed](https://github.com/ydb-platform/ydb/pull/18319) memory leak during writing to the [topic](./concepts/glossary#topic).
* Fixed errors in processing [nullable columns](https://github.com/ydb-platform/ydb/issues/15701) and [columns with UUID type](https://github.com/ydb-platform/ydb/issues/15697) in row tables.

### Version 24.4.4.2 {#24-4-4-2}

Release date: April 15, 2025

#### Functionality

* Enabled by default:

  * support for [views](./concepts/datamodel/view.md)
  * [auto-partitioning mode](./concepts/datamodel/topic.md#autopartitioning) for topics
  * [transactions involving topics and row-oriented tables simultaneously](./concepts/transactions.md#topic-table-transactions)
  * [volatile distributed transactions](./contributor/datashard-distributed-txs.md#volatile-transactions)


* Added the ability to [read and write to a topic](./reference/kafka-api/examples.md#kafka-api-usage-examples) using the Kafka API without authentication.

#### Performance

* Enabled by default automatic secondary index selection for queries.

#### Bug Fixes

* [Fixed](https://github.com/ydb-platform/ydb/pull/14811) an error that led to a significant decrease in reading speed from [tablet followers](./concepts/glossary.md#tablet-follower).
* [Fixed](https://github.com/ydb-platform/ydb/pull/14516) an error that caused volatile distributed transactions to sometimes wait for confirmations until the next reboot.
* [Fixed](https://github.com/ydb-platform/ydb/pull/15077) a rare assertion failure (server process crash) when followers attached to leaders with an inconsistent snapshot.
* [Fixed](https://github.com/ydb-platform/ydb/pull/15074) a rare datashard crash when a dropped table shard is restarted with uncommitted persistent changes.
* [Fixed](https://github.com/ydb-platform/ydb/pull/15194) an error that could disrupt the order of message processing in a topic.
* [Fixed](https://github.com/ydb-platform/ydb/pull/15308) a rare error that could stop reading from a topic partition.
* [Fixed](https://github.com/ydb-platform/ydb/pull/15160) an issue where a transaction could hang if a user performed a control plane operation on a topic (for example, adding partitions or a consumer) while the PQ tablet is moving to another node.
* [Fixed](https://github.com/ydb-platform/ydb/pull/15233) a memory leak issue with the UserInfo counter value. Because of the memory leak, a reading session would eventually return a "too big in flight" error.
* [Fixed](https://github.com/ydb-platform/ydb/pull/15467) a proxy crash due to duplicate topics in a request.
* [Fixed](https://github.com/ydb-platform/ydb/pull/15933) a rare bug where a user could write to a topic without any account quota being applied or consumed.
* [Fixed](https://github.com/ydb-platform/ydb/pull/16288) an issue where topic deletion returned "OK" while the topic tablets persisted in a functional state. To remove such tablets, follow the instructions from the [pull request](https://github.com/ydb-platform/ydb/pull/16288).
* [Fixed](https://github.com/ydb-platform/ydb/pull/16418) a rare issue that prevented the restoration of a backup for a large secondary indexed table.
* [Fixed](https://github.com/ydb-platform/ydb/pull/15862) an issue that caused errors when inserting data using `UPSERT` into row-oriented tables with default values.
* [Resolved](https://github.com/ydb-platform/ydb/pull/15334) a bug that caused failures when executing queries to tables with secondary indexes that returned result lists using the RETURNING * expression.

## Version 24.3 {#24-3}

### Version 24.3.15.5 {#24-3-15-5}

Release date: February 6, 2025

#### Functionality

* Added the ability to register a [database node](./concepts/glossary.md#database-node) using a certificate. In the [Node Broker](./concepts/glossary.md#node-broker) the flag `AuthorizeByCertificate` has been added to enable certificate-based registration.
* [Added](https://github.com/ydb-platform/ydb/pull/11775) priorities for authentication ticket through a [third-party IAM provider](./security/authentication.md#iam), with the highest priority given to requests from new users. Tickets in the cache update their information with a lower priority.

#### Performance

* [Improved](https://github.com/ydb-platform/ydb/pull/12747) tablet startup time on large clusters: 210 ms → 125 ms (SSD), 260 ms → 165 ms (HDD).

#### Bug Fixes

* [Removed](https://github.com/ydb-platform/ydb/pull/11901) the restriction on writing values greater than 127 to the Uint8 type.
* [Fixed](https://github.com/ydb-platform/ydb/pull/12221) an issue where reading small messages from a topic in small chunks significantly increased CPU load, which could lead to delays in reading and writing to the topic.
* [Fixed](https://github.com/ydb-platform/ydb/pull/12915) an issue with restoring from a backup stored in S3 with path-style addressing.
* [Fixed](https://github.com/ydb-platform/ydb/pull/13222) an issue with restoring from a backup that was created during an automatic table split.
* [Fixed](https://github.com/ydb-platform/ydb/pull/12601) an issue with Uuid serialization for [CDC](./concepts/cdc.md).
* [Fixed](https://github.com/ydb-platform/ydb/pull/12018) an issue with ["frozen" locks](./contributor/datashard-locks-and-change-visibility.md#interaction-with-distributed-transactions), which could be caused by bulk operations (e.g., TTL-based deletions).
* [Fixed](https://github.com/ydb-platform/ydb/pull/12804) an issue where reading from a follower of tablets sometimes caused crashes during automatic table splits.
* [Fixed](https://github.com/ydb-platform/ydb/pull/12807) an issue where the [coordination node](./concepts/datamodel/coordination-node.md) successfully registered proxy servers despite a connection loss.
* [Fixed](https://github.com/ydb-platform/ydb/pull/11593) an issue that occurred when opening the Embedded UI tab with information about [distributed storage groups](./concepts/glossary.md#storage-group).
* [Fixed](https://github.com/ydb-platform/ydb/pull/12448) an issue where the Health Check did not report time synchronization issues.
* [Fixed](https://github.com/ydb-platform/ydb/pull/11658) a rare issue that caused errors during read queries.
* [Fixed](https://github.com/ydb-platform/ydb/pull/13501) an uncommitted changes leak and cleaned them up on startup.
* [Fixed](https://github.com/ydb-platform/ydb/pull/13948) consistency issues related to caching deleted ranges.

### Version 24.3.11.14 {#24-3-11-14}

Release date: January 9, 2025.

#### Functionality

* [Added](https://github.com/ydb-platform/ydb/pull/13251) support for restart without downtime in [a minimal fault-tolerant configuration of a cluster](./concepts/topology.md#reduced) that uses the three-node variant of `mirror-3-dc`.
* [Added](https://github.com/ydb-platform/ydb/pull/13220) new UDF Roaring Bitmap functions: AndNotWithBinary, FromUint32List, RunOptimize.

### Version 24.3.11.13 {#24-3-11-13}

Release date: December 24, 2024.

#### Functionality

* Introduced [query tracing](./reference/observability/tracing/setup), a tool that allows you to view the detailed path of a request through a distributed system.
* Added support for [asynchronous replication](./concepts/async-replication), that allows synchronizing data between YDB databases in near real time. It can also be used for data migration between databases with minimal downtime for applications interacting with these databases.
* Added support for [views](./concepts/datamodel/view), which can be enabled by the cluster administrator using the `enable_views` setting in [dynamic configuration](./maintenance/manual/dynamic-config#updating-dynamic-configuration).
* Extended [federated query](./concepts/federated_query/) capabilities to support new external data sources: MySQL, Microsoft SQL Server, and Greenplum.
* Published [documentation](./devops/deployment-options/manual/federated-queries/connector-deployment.md) on deploying YDB with [federated query](./concepts/federated_query/) functionality (manual setup).
* Added a new launch parameter `FQ_CONNECTOR_ENDPOINT` for YDB Docker containers that specifies an external data source connector address. Added support for TLS encryption for connections to the connector and the ability to expose the connector service port locally on the same host as the dynamic YDB node.
* Added an [auto-partitioning mode](./concepts/datamodel/topic.md#autopartitioning) for topics, where partitions can dynamically split based on load while preserving message read-order and exactly-once guarantees. The mode can be enabled by the cluster administrator using the settings `enable_topic_split_merge` and `enable_pqconfig_transactions_at_scheme_shard` in [dynamic configuration](./maintenance/manual/dynamic-config#updating-dynamic-configuration).
* Added support for transactions involving [topics](./concepts/topic) and row-based tables, enabling transactional data transfer between tables and topics, or between topics, ensuring no data loss or duplication. Transactions can be enabled by the cluster administrator using the settings `enable_topic_service_tx` and `enable_pqconfig_transactions_at_scheme_shard` in [dynamic configuration](./maintenance/manual/dynamic-config#updating-dynamic-configuration).
* [Implemented](https://github.com/ydb-platform/ydb/pull/7150) [Change Data Capture (CDC)](./concepts/cdc) for synchronous secondary indexes.
* Added support for changing record retention periods in [CDC](./concepts/cdc) topics.
* Added support for auto-increment columns as part of a table's primary key.
* Added audit logging for user login events in YDB, session termination events in the user interface, and backup/restore operations.
* Added a system view with information about sessions installed from the database using a query.
* Added support literal default values for row-oriented tables. When inserting a new row in YDB Query default values will be assigned to the column if specified.
* Added the `version()` [built-in function](./yql/reference/builtins/basic.md#version).
* Added support for `RETURNING` clause in queries.
* [Added](https://github.com/ydb-platform/ydb/pull/8708) start/end times and authors in the metadata for backup/restore operations from S3-compatible storage.
* Added support for backup/restore of ACL for tables from/to S3-compatible storage.
* Included paths and decompression methods in query plans for reading from S3.
* Added new parsing options for timestamp/datetime fields when reading data from S3.
* Added support for the `Decimal` type in [partitioning keys](./dev/primary-key/column-oriented#klyuch-particionirovaniya).
* Improved diagnostics for storage issues in HealthCheck.
* **_(Experimental)_** Added a [cost-based optimizer](./concepts/optimizer#cost-based-query-optimizer) for complex queries, involving [column-oriented tables](./concepts/glossary#column-oriented-table). The cost-based optimizer considers a large number of alternative execution plans for each query and selects the best one based on the cost estimate for each option.  Currently, this optimizer only works with plans that contain [JOIN](./yql/reference/syntax/join) operations.
* **_(Experimental)_** Initial version of the workload manager was implemented. It allows to create resource pools with CPU, memory and active queries count limits. Resource classifiers were implemented to assign queries to specific resource pool.
* **_(Experimental)_** Implemented [automatic index selection](./dev/secondary-indexes#avtomaticheskoe-ispolzovanie-indeksov-pri-vyborke) for queries, which can be enabled via the `index_auto_choose_mode setting` in `table_service_config` in [dynamic configuration](./maintenance/manual/dynamic-config#updating-dynamic-configuration).

#### YDB UI

* Added support for creating and [viewing information on](https://github.com/ydb-platform/ydb-embedded-ui/issues/782) asynchronous replication instances.
* [Added](https://github.com/ydb-platform/ydb-embedded-ui/issues/929) an indicator for auto-increment columns.
* [Added](https://github.com/ydb-platform/ydb-embedded-ui/pull/1438) a tab with information about [tablets](./concepts/glossary#tablet).
* [Added](https://github.com/ydb-platform/ydb-embedded-ui/pull/1289) a tab with details about [distributed storage groups](./concepts/glossary#storage-group).
* [Added](https://github.com/ydb-platform/ydb-embedded-ui/pull/1218) a setting to trace all queries and display tracing results.
* Enhanced the PDisk page with [attributes](https://github.com/ydb-platform/ydb-embedded-ui/pull/1069), disk space consumption details, and a button to initiate [disk decommissioning](./devops/deployment-options/manual/decommissioning.md).
* [Added](https://github.com/ydb-platform/ydb-embedded-ui/pull/1313) information about currently running queries.
* [Added](https://github.com/ydb-platform/ydb-embedded-ui/pull/1291) a row limit setting for query editor output and a notification when results exceed the limit.
* [Added](https://github.com/ydb-platform/ydb-embedded-ui/pull/1049) a tab to display top CPU-consuming queries over the last hour.
* [Added](https://github.com/ydb-platform/ydb-embedded-ui/pull/1127) a control to search the history and saved queries pages.
* [Added](https://github.com/ydb-platform/ydb-embedded-ui/pull/1117) the ability to cancel query execution.
* [Added](https://github.com/ydb-platform/ydb-embedded-ui/issues/944) a shortcut to save queries in the editor.
* [Separated](https://github.com/ydb-platform/ydb-embedded-ui/pull/1422) donor disks from other disks in the UI.
* [Added](https://github.com/ydb-platform/ydb-embedded-ui/pull/1154) support for InterruptInheritance ACL and improved visualization of active ACLs.
* [Added](https://github.com/ydb-platform/ydb-embedded-ui/pull/889) a display of the current UI version.
* [Added](https://github.com/ydb-platform/ydb-embedded-ui/pull/1229) a tab  with information about the status of settings for enabling experimental functionality.

#### Performance

* [Accelerated](https://github.com/ydb-platform/ydb/pull/7589) recovery of tables with secondary indexes from backups up to 20% according to our tests.
* [Optimized](https://github.com/ydb-platform/ydb/pull/9721) Interconnect throughput.
* Improved the performance of CDC topics with thousands of partitions.
* Enhanced the Hive tablet balancing algorithm.

#### Bug fixes

* [Fixed](https://github.com/ydb-platform/ydb/pull/6850) an issue that caused databases with a large number of tables or partitions to become non-functional during restoration from a backup. Now, if database size limits are exceeded, the restoration operation will fail, but the database will remain operational.
* [Implemented](https://github.com/ydb-platform/ydb/pull/11532) a mechanism to forcibly trigger background [compaction](./concepts/glossary#compaction) when discrepancies between the data schema and stored data are detected in [DataShard](./concepts/glossary#data-shard). This resolves a rare issue with delays in schema changes.
* [Resolved](https://github.com/ydb-platform/ydb/pull/10447) duplication of authentication tickets, which led to an increased number of requests to authentication providers.
* [Fixed](https://github.com/ydb-platform/ydb/pull/9377) an invariant violation issue during the initial scan of CDC, leading to an abnormal termination of the `ydbd` server process.
* [Prohibited](https://github.com/ydb-platform/ydb/pull/9446) schema changes for backup tables.
* [Fixed](https://github.com/ydb-platform/ydb/pull/9509) an issue with an initial scan freezing during CDC when the table is frequently updated.
* [Excluded](https://github.com/ydb-platform/ydb/pull/9934) deleted indexes from the count against the [maximum index limit](./concepts/limits-ydb#schema-object).
* Fixed a [bug](https://github.com/ydb-platform/ydb/issues/6985) in the display of the scheduled execution time for a set of transactions (planned step).
* [Fixed](https://github.com/ydb-platform/ydb/pull/9161) a [problem](https://github.com/ydb-platform/ydb/issues/8942) with interruptions in blue–green deployment in large clusters caused by frequent updates to the node list.
* [Resolved](https://github.com/ydb-platform/ydb/pull/8925) a rare issue that caused transaction order violations.
* [Fixed](https://github.com/ydb-platform/ydb/pull/9841) an [issue](https://github.com/ydb-platform/ydb/issues/9797) in the EvWrite API that resulted in incorrect memory deallocation.
* [Resolved](https://github.com/ydb-platform/ydb/pull/10698) a [problem](https://github.com/ydb-platform/ydb/issues/10674) with volatile transactions hanging after a restart.
* Fixed a bug in the CDC, which in some cases leads to increased CPU consumption, up to a core per CDC partition.
* [Eliminated](https://github.com/ydb-platform/ydb/pull/11061) read delays occurring during and after the splitting of certain partitions.
* Fixed issues when reading data from S3.
* [Corrected](https://github.com/ydb-platform/ydb/pull/4793) the calculation of the AWS signature for S3 requests.
* Resolved false positives in the HealthCheck system during database backups involving a large number of shards.

## Version 24.2 {#24-2}

Release date: August 20, 2024.

### Functionality

* Added the ability to set [maintenance task priorities](./devops/concepts/maintenance-without-downtime.md#priority) in the [cluster management system](./concepts/glossary.md#cms).
* Added a setting to enable [stable names](./reference/configuration/#node-broker-config) for cluster nodes within a tenant.
* Enabled retrieval of nested groups from the [LDAP server](./concepts/auth#ldap-auth-provider), improved host parsing in the [LDAP-configuration](./reference/configuration/#ldap-auth-config), and added an option to disable built-in authentication via login and password.
* Added support for authenticating [dynamic nodes](./concepts/glossary#dynamic) using SSL-certificates.
* Implemented the removal of inactive nodes from [Hive](./concepts/glossary#hive) without a restart.
* Improved management of inflight pings during Hive restarts in large clusters.
* Changed the order of establishing connections with nodes during Hive restarts.

### YDB UI

* [Added](https://github.com/ydb-platform/ydb/pull/7485) the option to set a TTL for user sessions in the configuration file.
* [Added](https://github.com/ydb-platform/ydb-embedded-ui/issues/996) an option to sort the list of queries by `CPUTime`.
* [Fixed](https://github.com/ydb-platform/ydb/pull/7779) precision loss when working with `double`, `float` data types.
* [Added support](https://github.com/ydb-platform/ydb-embedded-ui/pull/958) for creating directories in the UI.
* [Added](https://github.com/ydb-platform/ydb-embedded-ui/pull/976) an auto-refresh control on all pages.
* [Improved](https://github.com/ydb-platform/ydb-embedded-ui/pull/955) ACL display.
* Enabled autocomplete in the queries editor by default.
* Added support for views.

### Bug fixes

* Added a check on the size of the local transaction prior to its commit to fix [errors](https://github.com/db-platform/ydb/issues/6677) in scheme shard operations when exporting/backing up large databases.
* [Fixed](https://github.com/ydb-platform/ydb/pull/7709) an issue with duplicate results in SELECT queries when reducing quotas in [DataShard](./concepts/glossary#data-shard).
* [Fixed](https://github.com/ydb-platform/ydb/pull/6461) [errors](https://github.com/ydb-platform/ydb/issues/6220) occurring during [coordinator](./concepts/glossary#coordinator) state changes.
* [Fixed](https://github.com/ydb-platform/ydb/pull/5992) issues during the initial CDC scan.
* [Resolved](https://github.com/ydb-platform/ydb/pull/6615) race conditions in asynchronous change delivery (asynchronous indexes, CDC).
* [Fixed](https://github.com/ydb-platform/ydb/pull/5993) a crash that sometimes occurred during [TTL-based](./concepts/ttl) deletions.
* [Fixed](https://github.com/ydb-platform/ydb/pull/5760) an issue with PDisk status display in the [CMS](./concepts/glossary#cms).
* [Fixed](https://github.com/ydb-platform/ydb/pull/6008) an issue that might cause soft tablet transfers (drain) from a node to hang.
* [Resolved](https://github.com/ydb-platform/ydb/pull/6445) an issue with the interconnect proxy stopping on a node that is running without restarts. The issue occurred when adding another node to the cluster.
* [Corrected](https://github.com/ydb-platform/ydb/pull/7023) string escaping in error messages.
* [Fixed](https://github.com/ydb-platform/ydb/pull/6695) an issue with managing free memory in the [interconnect](./concepts/glossary#actor-system-interconnect).
* [Corrected](https://github.com/ydb-platform/ydb/issues/6405) UnreplicatedPhantoms and UnreplicatedNonPhantoms counters in VDisk.
* [Fixed](https://github.com/ydb-platform/ydb/issues/6398) an issue with handling empty garbage collection requests on VDisk.
* [Resolved](https://github.com/ydb-platform/ydb/pull/5894) issues with managing TVDiskControls settings through CMS.
* [Fixed](https://github.com/ydb-platform/ydb/pull/5883) an issue with failing to load the data created by newer versions of VDisk.
* [Fixed](https://github.com/ydb-platform/ydb/pull/5862) an issue with executing the `REPLACE INTO` queries with default values.
* [Fixed](https://github.com/ydb-platform/ydb/pull/7714) errors in queries with multiple LEFT JOINs to a single string table.
* [Fixed](https://github.com/ydb-platform/ydb/pull/7740) precision loss for `float`,`double` types when using CDC.

## Version 24.1 {#24-1}

Release date: July 31, 2024.

### Functionality

* The [Knn UDF](./yql/reference/udf/list/knn.md) function for precise nearest vector search has been implemented.
* The gRPC Query service has been developed, enabling the execution of all types of queries (DML, DDL) and retrieval of unlimited amounts of data.
* [Integration with the LDAP protocol](./security/authentication.md) has been implemented, allowing the retrieval of a list of groups from external LDAP directories.

### Embedded UI

* The database information tab now includes a resource consumption diagnostic dashboard, which allows users to assess the current consumption of key resources: processor cores, RAM, and distributed storage space.
* Charts for monitoring the key performance indicators of the {{ ydb-short-name }} cluster have been added.

### Performance

* [Session timeouts](https://github.com/ydb-platform/ydb/pull/1837) for the coordination service between server and client have been optimized. Previously, the timeout was 5 seconds, which could result in a 10-second delay in identifying an unresponsive client and releasing its resources. In the new version, the check interval depends on the session's wait time, allowing for faster responses during leader changes or when acquiring distributed locks.
* CPU consumption by [SchemeShard](./concepts/glossary.md#scheme-shard) replicas has been [optimized](https://github.com/ydb-platform/ydb/pull/2391), particularly when handling rapid updates for tables with a large number of partitions.

### Bug fixes

* A possible queue overflow error has been [fixed](https://github.com/ydb-platform/ydb/pull/3917). [Change Data Capture](./dev/cdc.md) now reserves the change queue capacity during the initial scan.
* A potential deadlock between receiving and sending CDC records has been [fixed](https://github.com/ydb-platform/ydb/pull/4597).
* An issue causing the loss of the mediator task queue during mediator reconnection has been [fixed](https://github.com/ydb-platform/ydb/pull/2056). This fix allows processing of the mediator task queue during resynchronization.
* A rarely occurring error has been [fixed](https://github.com/ydb-platform/ydb/pull/2624), where with volatile transactions enabled, a successful transaction confirmation result could be returned before the transaction was fully committed. Volatile transactions remain disabled by default and are still under development.
* A rare error that led to the loss of established locks and the successful confirmation of transactions that should have failed with a "Transaction Locks Invalidated" error has been [fixed](https://github.com/ydb-platform/ydb/pull/2839).
* A rare error that could result in a violation of data integrity guarantees during concurrent read and write operations on a specific key has been [fixed](https://github.com/ydb-platform/ydb/pull/3074).
* An issue causing read replicas to stop processing requests has been [fixed](https://github.com/ydb-platform/ydb/pull/4343).
* A rare error that could cause abnormal termination of database processes if there were uncommitted transactions on a table during its renaming has been [fixed](https://github.com/ydb-platform/ydb/pull/4979).
* An error in determining the status of a static group, where it was not marked as non-working when it should have been, has been [fixed](https://github.com/ydb-platform/ydb/pull/3632).
* An error involving partial commits of a distributed transaction with uncommitted changes, caused by certain race conditions with restarts, has been [fixed](https://github.com/ydb-platform/ydb/pull/2169).
* Anomalies related to reading outdated data, [detected using Jepsen](https://blog.ydb.tech/hardening-ydb-with-jepsen-lessons-learned-e3238a7ef4f2), have been [fixed](https://github.com/ydb-platform/ydb/pull/2374).

## Version 23.4 {#23-4}

Release date: May 14, 2024.

### Performance

* [Fixed](https://github.com/ydb-platform/ydb/pull/3638) an issue of increased CPU consumption by a topic actor `PERSQUEUE_PARTITION_ACTOR`.
* [Optimized](https://github.com/ydb-platform/ydb/pull/2083) resource usage by SchemeBoard replicas. The greatest effect is noticeable when modifying the metadata of tables with a large number of partitions.

### Bug fixes

* [Fixed a bug](https://github.com/ydb-platform/ydb/pull/2169) of possible partial commit of accumulated changes when using persistent distributed transactions. This error occurs in an extremely rare combination of events, including restarting tablets that service the table partitions involved in the transaction.
* [Fixed a bug](https://github.com/ydb-platform/ydb/pull/3165) involving a race condition between the table merge and garbage collection processes, which could result in garbage collection ending with an invariant violation error, leading to an abnormal termination of the `ydbd` server process.
* [Fixed a bug](https://github.com/ydb-platform/ydb/pull/2696) in Blob Storage, where information about changes to the composition of a storage group might not be received in a timely manner by individual cluster nodes. As a result, reads and writes of data stored in the affected group could become blocked in rare cases, requiring manual intervention.
* [Fixed a bug](https://github.com/ydb-platform/ydb/pull/3002) in Blob Storage, where data storage nodes might not start despite the correct configuration. The error occurred on systems with the experimental "blob depot" feature explicitly enabled (this feature is disabled by default).
* [Fixed a bug](https://github.com/ydb-platform/ydb/pull/2475) that sometimes occurred when writing to a topic with an empty `producer_id` with turned off deduplication. It could lead to abnormal termination of the `ydbd` server process.
* [Fixed a bug](https://github.com/ydb-platform/ydb/pull/2651) that caused the `ydbd` process to crash due to an incorrect session state when writing to a topic.
* [Fixed a bug](https://github.com/ydb-platform/ydb/pull/3587) in displaying the metric of number of partitions in a topic, where it previously displayed an incorrect value.
* [Fixed a bug](https://github.com/ydb-platform/ydb/pull/2126) causing memory leaks that appeared when copying topic data between clusters. These could cause `ydbd` server processes to terminate due to out-of-memory issues.

## Version 23.3 {#23-3}

Release date: October 12, 2023.

### Functionality

* Implemented visibility of own changes. With this feature enabled you can read changed values from the current transaction, which has not been committed yet. This functionality also allows multiple modifying operations in one transaction on a table with secondary indexes.
* Added support for [column tables](concepts/datamodel/table.md#column-tables). It is now possible to create analytical reports based on stored data in YDB with performance comparable to specialized analytical DBMS.
* Added support for Kafka API for topics. YDB topics can now be accessed via a Kafka-compatible API designed for migrating existing applications. Support for Kafka protocol version 3.4.0 is provided.
* Added the ability to [write to a topic without deduplication](concepts/datamodel/topic.md#no-dedup). This is important in cases where message processing order is not critical.
* YQL has added the capabilities to [create](yql/reference/syntax/create-topic.md), [modify](yql/reference/syntax/alter-topic.md), and [delete](yql/reference/syntax/delete.md) topics.
* Added support of assigning and revoking access rights using the YQL `GRANT` and `REVOKE` commands.
* Added support of DML-operations logging in the audit log.
* **_(Experimental)_** When writing messages to a topic, it is now possible to pass metadata. To enable this functionality, add `enable_topic_message_meta: true` to the [configuration file](reference/configuration/index.md).
* **_(Experimental)_** Added support for [reading from topics in a transaction](reference/ydb-sdk/topic.md#read-tx). It is now possible to read from topics and write to tables within a transaction, simplifying the data transfer scenario from a topic to a table. To enable this functionality, add `enable_topic_service_tx: true` to the [configuration file](reference/configuration/index.md).
* **_(Experimental)_** Added support for PostgreSQL compatibility. This involves executing SQL queries in PostgreSQL dialect on the YDB infrastructure using the PostgreSQL network protocol. With this capability, familiar PostgreSQL tools such as psql and drivers (e.g., pq for Golang and psycopg2 for Python) can be used. Queries can be developed using the familiar PostgreSQL syntax and take advantage of YDB's benefits such as horizontal scalability and fault tolerance.
* **_(Experimental)_** Added support for federated queries. This enables retrieving information from various data sources without the need to move the data into YDB. Federated queries support interaction with ClickHouse and PostgreSQL databases, as well as S3 class data stores (Object Storage). YQL queries can be used to access these databases without duplicating data between systems.

### Embedded UI

* A new option `PostgreSQL` has been added to the query type selector settings, which is available when the `Enable additional query modes` parameter is enabled. Also, the query history now takes into account the syntax used when executing the query.
* The YQL query template for creating a table has been updated. Added a description of the available parameters.
* Now sorting and filtering for Storage and Nodes tables takes place on the server. To use this functionality, you need to enable the parameter `Offload tables filters and sorting to backend` in the experiments section.
* Buttons for creating, changing and deleting [topics](concepts/datamodel/topic.md) have been added to the context menu.
* Added sorting by criticality for all issues in the tree in `Healthcheck`.

### Performance

* Implemented read iterators. This feature allows to separate reads and computations. Read iterators allow datashards to increase read queries throughput.
* The performance of writing to YDB topics has been optimized.
* Improved tablet balancing during node overload.

### Bug fixes

* Fixed an error regarding potential blocking of reading iterators of snapshots, of which the coordinators were unaware.
* Memory leak when closing the connection in Kafka proxy has been fixed.
* Fixed an issue where snapshots taken through reading iterators may fail to recover on restarts.
* Fixed an issue with an incorrect residual predicate for the `IS NULL` condition on a column.
* Fixed an occurring verification error: `VERIFY failed: SendResult(): requirement ChunksLimiter.Take(sendBytes) failed`.
* Fixed `ALTER TABLE` for TTL on column-based tables.
* Implemented a `FeatureFlag` that allows enabling/disabling work with `CS` and `DS`.
* Fixed a 50ms time difference between coordinator time in 23-2 and 23-3.
* Fixed an error where the storage endpoint was returning extra groups when the `viewer backend` had the `node_id` parameter in the request.
* Added a usage filter to the `/storage` endpoint in the `viewer backend`.
* Fixed an issue in Storage v2 where an incorrect number was returned in the `Degraded field`.
* Fixed an issue with cancelling subscriptions from sessions during tablet restarts.
* Fixed an error where `healthcheck alerts` for storage were flickering during rolling restarts when going through a load balancer.
* Updated `CPU usage metrics` in YDB.
* Fixed an issue where `NULL` was being ignored when specifying `NOT NULL` in the table schema.
* Implemented logging of `DDL` operations in the common log.
* Implemented restriction for the YDB table attribute `add/drop` command to only work with tables and not with any other objects.
* Disabled `CloseOnIdle` for interconnect.
* Fixed the doubling of read speed in the UI.
* Fixed an issue where data could be lost on block-4-2.
* Added a check for topic name validity.
* Fixed a possible deadlock in the actor system.
* Fixed the `KqpScanArrowInChanels::AllTypesColumns` test.
* Fixed the `KqpScan::SqlInParameter` test.
* Fixed parallelism issues for OLAP queries.
* Fixed the insertion of `ClickBench` parquet files.
* Added a missing call to `CheckChangesQueueOverflow` in the general `CheckDataTxReject`.
* Fixed an error that returned an empty status in `ReadRows` API calls.
* Fixed incorrect retry behavior in the final stage of export.
* Fixed an issue with infinite quota for the number of records in a `CDC topic`.
* Fixed the import error of `string` and `parquet` columns into an `OLAP string column`.
* Fixed a crash in `KqpOlapTypes.Timestamp` under `tsan`.
* Fixed a `viewer backend` crash when attempting to execute a query against the database due to version incompatibility.
* Fixed an error where the viewer did not return a response from the `healthcheck` due to a timeout.
* Fixed an error where incorrect `ExpectedSerial` values could be saved in `Pdisks`.
* Fixed an error where database nodes were crashing due to segfault in the S3 actor.
* Fixed a race condition in `ThreadSanitizer: data race KqpService::ToDictCache-UseCache`.
* Fixed a race condition in `GetNextReadId`.
* Fixed an issue with an inflated result in `SELECT COUNT(*)` immediately after import.
* Fixed an error where `TEvScan` could return an empty dataset in the case of shard splitting.
* Added a separate `issue/error` code in case of available space exhaustion.
* Fixed a `GRPC_LIBRARY Assertion` failed error.
* Fixed an error where scanning queries on secondary indexes returned an empty result.
* Fixed validation of `CommitOffset` in `TopicAPI`.
* Reduced shared cache consumption when approaching OOM.
* Merged scheduler logic from data executer and scan executer into one class.
* Added discovery and `proxy` handlers to the query execution process in the `viewer backend`.
* Fixed an error where the `/cluster` endpoint returned the root domain name, such as `/ru`, in the `viewer backend`.
* Implemented a seamless table update scheme for `QueryService`.
* Fixed an issue where `DELETE` returned data and did not delete it.
* Fixed an error in `DELETE ON` operation in query service.
* Fixed an unexpected batching disablement in `default` schema settings.
* Fixed a triggering check `VERIFY failed: MoveUserTable(): requirement move.ReMapIndexesSize() == newTableInfo->Indexes.size()`.
* Increased the `default` timeout for `grpc-streaming`.
* Excluded unused messages and methods from `QueryService`.
* Added sorting by `Rack` in /nodes in the `viewer backend`.
* Fixed an error where sorting queries returned an error in descending order.
* Improved interaction between `QP` and `NodeWhiteboard`.
* Removed support for old parameter formats.
* Fixed an error where `DefineBox` was not being applied to disks with a static group.
* Fixed a `SIGSEGV` error in the dinnode during `CSV` import via `YDB CLI`.
* Fixed an error that caused a crash when processing `NGRpcService::TRefreshTokenImpl`.
* Implemented a `gossip protocol` for exchanging cluster resource information.
* Fixed an error in `DeserializeValuePickleV1(): requirement data.GetTransportVersion() == (ui32) NDqProto::DATA_TRANSPORT_UV_PICKLE_1_0 failed`.
* Implemented `auto-increment` columns.
* Use `UNAVAILABLE` status instead of `GENERIC_ERROR` when shard identification fails.
* Added support for rope payload in `TEvVGet`.
* Added ignoring of deprecated events.
* Fixed a crash of write sessions on an invalid topic name.
* Fixed an error in `CheckExpected(): requirement newConstr failed, message: Rewrite error, missing Distinct((id)) constraint in node FlatMap`.
* Enabled `self-heal` by default.

## Version 23.2 {#23-2}

Release date: August 14, 2023.

### Functionality

* **_(Experimental)_** Implemented visibility of own changes. With this feature enabled you can read changed values from the current transaction, which has not been committed yet. This functionality also allows multiple modifying operations in one transaction on a table with secondary indexes. To enable this feature add `enable_kqp_immediate_effects: true` under `table_service_config` section into [configuration file](reference/configuration/index.md).
* **_(Experimental)_** Implemented read iterators. This feature allows to separate reads and computations. Read iterators allow datashards to increase read queries throughput. To enable this feature add `enable_kqp_data_query_source_read: true` under `table_service_config` section into [configuration file](reference/configuration/index.md).

### Embedded UI

* Navigation improvements:
  * Diagnostics and Development mode switches are moved to the left panel.
  * Every page has breadcrumbs.
  * Storage groups and nodes info are moved from left buttons to tabs on the database page.
* Query history and saved queries are moved to tabs over the text editor area in query editor.
* Info tab for scheme objects displays parameters using terms from `CREATE` or `ALTER` statements.
* Added [column tables](concepts/datamodel/table.md#column-tables) support.

### Performance

* For scan queries, you can now effectively search for individual rows using a primary key or secondary indexes. This can bring you a substantial performance gain in many cases. Similarly to regular queries, you need to explicitly specify its name in the query text using the `VIEW` keyword to use a secondary index.

* **_(Experimental)_** Added an option to give control of the system tablets of the database (SchemeShard, Coordinators, Mediators, SysViewProcessor) to its own Hive instead of the root Hive, and do so immediately upon creating a new database. Without this flag, the system tablets of the new database are created in the root Hive, which can negatively impact its load. Enabling this flag makes databases completely isolated in terms of load, that may be particularly relevant for installations, consisting from a roughly hundred or more databases. To enable this feature add `alter_database_create_hive_first: true` under `feature_flags` section into [configuration file](reference/configuration/index.md).

### Bug fixes

* Fixed a bug in the autoconfiguration of the actor system, resulting in all the load being placed on the system pool.
* Fixed a bug that caused full scanning when searching by prefix of the primary key using `LIKE`.
* Fixed bugs when interacting with datashard followers.
* Fixed bugs when working with memory in column tables.
* Fixed a bug in processing conditions for immediate transactions.
* Fixed a bug in the operation of iterator-based reads on datasharrd followers.
* Fixed a bug that caused cascading reinstallation of data delivery sessions to asynchronous indexes.
* Fixed bugs in the optimizer for scanning queries.
* Fixed a bug in the incorrect calculation of storage consumption by Hive after expanding the database.
* Fixed a bug that caused operations to hang on non-existent iterators.
* Fixed bugs when reading a range on a `NOT NULL` column.
* Fixed a bug in the replication of VDisks.
* Fixed a bug in the work of the `run_interval` option in TTL.

## Version 23.1 {#23-1}

Release date: May 5, 2023. To update to version 23.1, select the [Downloads](downloads/index.md#ydb-server) section.

### Functionality

* Added [initial table scan](concepts/cdc.md#initial-scan) when creating a CDC changefeed. Now, you can export all the data existing at the time of changefeed creation.
* Added [atomic index replacement](dev/secondary-indexes.md#atomic-index-replacement). Now, you can atomically replace one pre-defined index with another. This operation is absolutely transparent for your application. Indexes are replaced seamlessly, with no downtime.
* Added the [audit log](security/audit-log.md): Event stream including data about all the operations on {{ ydb-short-name }} objects.

### Performance

* Improved formats of data exchanged between query stages. As a result, we accelerated SELECTs by 10% on parameterized queries and by up to 30% on write operations.
* Added [autoconfiguring](reference/configuration/actor_system_config.md#autoconfig) for the actor system pools based on the workload against them. This improves performance through more effective CPU sharing.
* Optimized the predicate logic: Processing of parameterized OR or IN constraints is automatically delegated to DataShard.
* (Experimental) For scan queries, you can now effectively search for individual rows using a primary key or secondary indexes. This can bring you a substantial gain in performance in many cases. Similarly to regular queries, to use a secondary index, you need to explicitly specify its name in the query text using the `VIEW` keyword.
* The query's computational graph is now cached at query runtime, reducing the CPU resources needed to build the graph.

### Bug fixes

* Fixed bugs in the distributed data warehouse implementation. We strongly recommend all our users to upgrade to the latest version.
* Fixed the error that occurred on building an index on NOT NULL columns.
* Fixed statistics calculation with MVCC enabled.
* Fixed errors with backups.
* Fixed the race condition that occurred at splitting and deleting a table with SDC.

## Version 22.5 {#22-5}

Release date: March 7, 2023. To update to version **22.5**, select the [Downloads](downloads/index.md#ydb-server) section.

### What's new

* Added [changefeed configuration parameters](yql/reference/syntax/alter_table/changefeed.md) to transfer additional information about changes to a topic.
* You can now [rename tables](concepts/datamodel/table.md#rename) that have TTL enabled.
* You can now [manage the record retention period](concepts/cdc.md#retention-period).

### Bug fixes and improvements

* Fixed an error inserting 0 rows with a BulkUpsert.
* Fixed an error importing Date/DateTime columns from CSV.
* Fixed an error importing CSV data with line breaks.
* Fixed an error importing CSV data with NULL values.
* Improved Query Processing performance (by replacing WorkerActor with SessionActor).
* DataShard compaction now starts immediately after a split or merge.

## Version 22.4 {#22-4}

Release date: October 12, 2022. To update to version **22.4**, select the [Downloads](downloads/index.md#ydb-server) section.

### What's new

* {{ ydb-short-name }} Topics and Change Data Capture (CDC):

  * Introduced the new Topic API. {{ ydb-short-name }} [Topic](concepts/datamodel/topic.md) is an entity for storing unstructured messages and delivering them to various subscribers.
  * Added support for the Topic API to the [{{ ydb-short-name }} CLI](reference/ydb-cli/topic-overview.md) and [SDK](reference/ydb-sdk/topic.md). The Topic API provides methods for message streaming writes and reads as well as topic management.
  * Added the ability to [capture table updates](concepts/cdc.md) and send change messages to a topic.

* SDK:

  * Added the ability to handle topics in the {{ ydb-short-name }} SDK.
  * Added official support for the database/sql driver for working with {{ ydb-short-name }} in Golang.

* Embedded UI:

  * The CDC changefeed and the secondary indexes are now displayed in the database schema hierarchy as separate objects.
  * Improved the visualization of query explain plan graphics.
  * Problem storage groups have more visibility now.
  * Various improvements based on UX research.

* Query Processing:

  * Added Query Processor 2.0, a new subsystem to execute OLTP queries with significant improvements compared to the previous version.
  * Improved write performance by up to 60%, and by up to 10% for reads.
  * Added the ability to include a NOT NULL restriction for YDB primary keys when creating tables.
  * Added support for renaming a secondary index online without shutting the service down.
  * Improved the query explain view that now also includes fields for the physical operators.

* Core:

  * For read only transactions, added consistent snapshot support that does not conflict with write transactions.
  * Added BulkUpsert support for tables with asynchronous secondary indexes.
  * Added TTL support for tables with asynchronous secondary indexes.
  * Added compression support for data export to S3.
  * Added an audit log for DDL statements.
  * Added support for authentication with static credentials.
  * Added system tables for query performance troubleshooting.
