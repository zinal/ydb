# Список изменений {{ ydb-short-name }} Server

## Версия 25.3 {#25-3}

### Версия 25.3.1 {#25-3-1}

Дата выхода: будет объявлена дополнительно.

#### Функциональность

* В [федеративных запросах](./concepts/query_execution/federated_query/) добавлена поддержка новых внешних источников данных: [Prometheus](https://github.com/ydb-platform/ydb/pull/17148), [Apache Iceberg](https://github.com/ydb-platform/ydb/pull/17007), [OpenSearch](https://github.com/ydb-platform/ydb/pull/18444) и [Redis](https://github.com/ydb-platform/ydb/pull/16957). Кроме того, [YDB Topics теперь можно использовать как внешний источник данных](https://github.com/ydb-platform/ydb/pull/18955) в SQL-сценариях.
* Расширена поддержка аналитических сценариев с внешними источниками: теперь можно [создавать внешние источники данных для таблиц Iceberg](https://github.com/ydb-platform/ydb/pull/16652), а также выполнять pushdown предикатов [`REGEXP`](https://github.com/ydb-platform/ydb/pull/19227) и [`LIKE`](https://github.com/ydb-platform/ydb/pull/17695) по колонкам `Utf8`, уменьшая объём читаемых внешних данных.
* Реализованы [экспорт конфигурации топика в S3](https://github.com/ydb-platform/ydb/pull/18138) и [импорт конфигурации топика из S3](https://github.com/ydb-platform/ydb/pull/18214). Также [в Kafka API появилась возможность создавать компактифицированные топики](https://github.com/ydb-platform/ydb/pull/18683), а YDB [автоматически создаёт и удаляет служебного consumer'а для компактификации топиков](https://github.com/ydb-platform/ydb/pull/20176).
* Улучшено автопартиционирование топиков: при разделении партиций YDB теперь может учитывать скорость записи отдельных producer'ов и стремится распределять их по новым партициям более равномерно, а не просто делить трафик пополам ([#25458](https://github.com/ydb-platform/ydb/pull/25458)).
* Расширен стек планирования на базе HDRF. В YDB [переработан KQP CPU scheduler на модели HDRF](https://github.com/ydb-platform/ydb/pull/19618), [включён новый compute scheduler на базе HDRF](https://github.com/ydb-platform/ydb/pull/21997), а также [улучшен алгоритм планировщика, чтобы он не запускал больше задач, чем допускает спрос](https://github.com/ydb-platform/ydb/pull/19893).
* Улучшена работа со скриптами: добавлена [поддержка выдачи промежуточных результатов во время выполнения скрипта](https://github.com/ydb-platform/ydb/pull/17421) и отдельный [статус `running`](https://github.com/ydb-platform/ydb/pull/19134) в API выполнения скриптов.
* Расширены административные возможности: лимиты схемных объектов, такие как `MAX_SHARDS` и `MAX_PATHS`, теперь можно [изменять через YQL с помощью `ALTER DATABASE`](https://github.com/ydb-platform/ydb/pull/19580), а операции BuildIndex теперь сохраняют [время создания, время завершения и SID пользователя, инициировавшего операцию](https://github.com/ydb-platform/ydb/pull/17751).
* Расширен аудит: YDB теперь пишет в audit log [`ALTER/MODIFY USER` операции](https://github.com/ydb-platform/ydb/pull/16989), [события YMQ в общем формате аудита YDB](https://github.com/ydb-platform/ydb/pull/18333), а также [отдельные audit log события для YMQ](https://github.com/ydb-platform/ydb/pull/21183).
* Расширены возможности управления кластером и хранилищем. Добавлены [настройки BS Controller в конфигурации кластера](https://github.com/ydb-platform/ydb/pull/17957), [базовая поддержка cluster bridging](https://github.com/ydb-platform/ydb/pull/18297), [реконфигурация Board через DistConf](https://github.com/ydb-platform/ydb/pull/18859) и [автоматическая реконфигурация StateStorage без простоя всей группы](https://github.com/ydb-platform/ydb/pull/17525).
* Для Distributed Storage добавлены новые параметры управления и размерности: [`GroupSizeInUnits` для storage groups и `SlotSizeInUnits` для PDisks](https://github.com/ydb-platform/ydb/pull/19337), [команда `ydb-dstool` для переноса PDisk между узлами без полной пересинхронизации данных](https://github.com/ydb-platform/ydb/pull/19684) и [поддержка параметра `--iam-token-file` в `ydb-dstool`](https://github.com/ydb-platform/ydb/pull/20303).
* Улучшена наблюдаемость: добавлены [счётчики ожидания операций в spilling IO queue](https://github.com/ydb-platform/ydb/pull/17394), [мониторинговые счётчики с постоянным значением 1 для nodes, VDisks и PDisks](https://github.com/ydb-platform/ydb/pull/18731), а также [дополнительные атрибуты в span'ах DSProxy и VDisk](https://github.com/ydb-platform/ydb/pull/21010), упрощающие диагностику.
* Расширены monitoring и operational-возможности: YDB [увеличил лимит длины текста запроса в system views до 10 КБ](https://github.com/ydb-platform/ydb/pull/15186), добавил [настраиваемую конфигурацию healthcheck для порогов перезапусков, числа таблеток, расхождения времени и таймаутов](https://github.com/ydb-platform/ydb/pull/15693), стал показывать [processing lag подтверждённых сообщений в `DescribeConsumer`](https://github.com/ydb-platform/ydb/pull/16857), а также получил [ICB-настройку для `ReadRequestsInFlightLimit`](https://github.com/ydb-platform/ydb/pull/22511).
* Скорректировано поведение кластера и Healthcheck: [понижен уровень серьёзности состояния `FAULTY` для PDisks](https://github.com/ydb-platform/ydb/pull/17095), добавлено [определение состояния bootstrap кластера в Healthcheck API для окружений с configuration V2](https://github.com/ydb-platform/ydb/pull/19600), а также [базовая валидация имён групп](https://github.com/ydb-platform/ydb/pull/19216).
* Улучшено размещение нагрузок в крупных инсталляциях: добавлена [эвристика, предотвращающая бесконечное перемещение по узлам таблеток, которые способны перегрузить узел в одиночку](https://github.com/ydb-platform/ydb/pull/18376), а также поддержка [pile identifiers для динамических узлов в 2-DC топологии](https://github.com/ydb-platform/ydb/pull/18988).
* Улучшена интеграция с OIDC: в white list [добавлен заголовок `Accept`](https://github.com/ydb-platform/ydb/pull/19735).
* Расширены средства диагностики и runtime-поддержки: добавлена [интеграция с Google Breakpad и настраиваемыми путями/хуками обработки minidump](https://github.com/ydb-platform/ydb/pull/17362), в `ydbd server` появились [параметры командной строки для работы с minidump](https://github.com/ydb-platform/ydb/pull/17711), а также реализовано [хранение идентификатора access resource в корневой базе данных для проверки доступа к информации о кластере](https://github.com/ydb-platform/ydb/pull/17952).
* Дополнительно расширены возможности Topics и Kafka: YDB добавил [поддержку idempotent producer в Kafka API](https://github.com/ydb-platform/ydb/pull/19678), [включил по умолчанию флаги автопартиционирования топиков для CDC, replication и transfer](https://github.com/ydb-platform/ydb/pull/20272), а также [увеличил число партиций, создаваемых по умолчанию для сценариев репликации](https://github.com/ydb-platform/ydb/pull/21420), чтобы уменьшить задержки из-за последующего автопартиционирования.
* Расширена аналитическая и OLAP-функциональность: добавлена [поддержка index-only search для vector index](https://github.com/ydb-platform/ydb/pull/18137), [новая операция `Increment` для целочисленных типов](https://github.com/ydb-platform/ydb/pull/19567), [статистика ColumnShard для UPDATE и DELETE в строках и байтах](https://github.com/ydb-platform/ydb/pull/19642), [лимиты памяти для колоночных таблиц в Memory Controller](https://github.com/ydb-platform/ydb/pull/19994), [разделение bulk и non-bulk статистики](https://github.com/ydb-platform/ydb/pull/20253), [статистика grouped limiter и cache](https://github.com/ydb-platform/ydb/pull/20738), а также [оптимизированная hash-функция `HashV2` для shuffle](https://github.com/ydb-platform/ydb/pull/21391).
* Улучшены внутренние механизмы колоночного хранения: добавлен [shared metadata cache для column shard на одном узле](https://github.com/ydb-platform/ydb/pull/18544), [поддержка управления размером sliced portions на нулевом слое](https://github.com/ydb-platform/ydb/pull/19111), а также [DistConf cache для распространения информации о группах](https://github.com/ydb-platform/ydb/pull/19315).
* В платформе реализован ряд дополнительных улучшений удобства: YDB [автоматически очищает временные директории и таблицы, создаваемые при экспорте в S3](https://github.com/ydb-platform/ydb/pull/16076), [включил pushdown строковых типов в Generic provider](https://github.com/ydb-platform/ydb/pull/16834), [оптимизировал чтение из Solomon](https://github.com/ydb-platform/ydb/pull/17663), [добавил per-process CPU limit в ColumnShard для интеграции с Workload Manager](https://github.com/ydb-platform/ydb/pull/17804), [ввёл новый формат версии релиза](https://github.com/ydb-platform/ydb/pull/18063), [добавил audit logging для операций `create tenant`](https://github.com/ydb-platform/ydb/pull/18095), [включил verbose memory limit mode по умолчанию в recipes](https://github.com/ydb-platform/ydb/pull/20040) и [добавил поддержку пользовательского CA в асинхронной репликации](https://github.com/ydb-platform/ydb/pull/21448).

##### Дополнительные изменения по PR

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
#### Производительность

* В YDB [по умолчанию включён multi-broadcast в table service](https://github.com/ydb-platform/ydb/pull/17794), что уменьшает накладные расходы для распределённых запросов, где широковещательная рассылка является оптимальной стратегией.
* Производительность на пути записи улучшена в нескольких местах: добавлена [новая схема приоритетов PDisk, разделяющая realtime- и compaction-записи](https://github.com/ydb-platform/ydb/pull/21705), [оптимизирована рассылка обновлений `TStorageConfig` подписчикам](https://github.com/ydb-platform/ydb/pull/18765), а также [улучшен алгоритм нового планировщика](https://github.com/ydb-platform/ydb/pull/20195).
* [Ускорены UPSERT-операции для таблиц со значениями по умолчанию](https://github.com/ydb-platform/ydb/pull/20048): в ряде случаев YDB избегает дополнительных чтений строк и выполняет больше работы непосредственно на шардах.
* Накладные расходы на аутентификацию снижены за счёт [выноса проверки пароля в отдельный actor вместо выполнения этой логики внутри локальных транзакций `TSchemeShard`](https://github.com/ydb-platform/ydb/pull/19687).
* Выполнение запросов к колоночным таблицам дополнительно ускорено за счёт [использования бинарного поиска для определения границ предикатов в portions](https://github.com/ydb-platform/ydb/pull/16867), [расширения набора фильтров, которые можно проталкивать в column shards](https://github.com/ydb-platform/ydb/pull/17884), и [улучшения параллельного выполнения OLAP-запросов](https://github.com/ydb-platform/ydb/pull/20428).
* Эффективность планирования и исполнения повышена за счёт [constant folding для детерминированных UDF](https://github.com/ydb-platform/ydb/pull/17533), [улучшенного размещения задач при чтении из внешних источников](https://github.com/ydb-platform/ydb/pull/18461), [кеша для ответов SchemeNavigate](https://github.com/ydb-platform/ydb/pull/20128) и нового [параметра конфигурации `buffer_page_alloc_size`](https://github.com/ydb-platform/ydb/pull/19724).
* Производительность и выразительность Roaring UDF улучшены за счёт операции [`Intersect`](https://github.com/ydb-platform/ydb/pull/17611).

##### Дополнительные изменения по PR

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
#### Исправления ошибок

* Исправлен ряд проблем корректности и стабильности в колоночных таблицах, включая [обработку предикатов в scan-запросах](https://github.com/ydb-platform/ydb/pull/16061), [гонки в CPU limiter, которые могли приводить к неконсистентным результатам](https://github.com/ydb-platform/ydb/pull/20238), а также [сбои и ошибки при изменении таблиц с векторными индексами](https://github.com/ydb-platform/ydb/pull/18121), включая [`RENAME INDEX`](https://github.com/ydb-platform/ydb/pull/17982).
* Исправлены проблемы консистентности транзакций: добавлены защитные механизмы от [некорректных результатов в отдельных read-write транзакциях](https://github.com/ydb-platform/ydb/pull/18088), а также устранена ошибка, из-за которой [конфликтующие read-write транзакции могли нарушать сериализуемость после рестартов шардов](https://github.com/ydb-platform/ydb/pull/18234).
* Исправлен ряд проблем в Topics и управлении сессиями, включая [редкие падения узлов при балансировке read sessions](https://github.com/ydb-platform/ydb/pull/16016), [ошибки commit offset в автопартиционируемых топиках](https://github.com/ydb-platform/ydb/pull/20560), [неожиданные ошибки `PathErrorUnknown` при подтверждении offset'ов](https://github.com/ydb-platform/ydb/pull/20084), а также проблему, из-за которой [attach streams оставались активными после завершения сессии](https://github.com/ydb-platform/ydb/pull/22298).
* Исправлены крайние случаи в backup, restore и replication: YDB больше [не сохраняет в локальные бэкапы таблицы-получатели и changefeed'ы асинхронной репликации](https://github.com/ydb-platform/ydb/pull/18401), а YDB CLI backup/restore теперь [корректно обрабатывает колонки типа `UUID`](https://github.com/ydb-platform/ydb/pull/17198).
* Исправлен ряд проблем стабильности в Distributed Storage, включая отсутствие [проверок включённого шифрования в zero-copy передаче](https://github.com/ydb-platform/ydb/pull/18698), [зависание VDisk в local recovery после ошибки `ChunkRead`](https://github.com/ydb-platform/ydb/pull/20519) и появление [phantom VDisks из-за гонок между созданием и удалением группы](https://github.com/ydb-platform/ydb/pull/18924).
* Повышена безопасность управления кластером: исправлены [повторные попытки захвата PDisk без VDisk в CMS](https://github.com/ydb-platform/ydb/pull/19781), устранена [гонка в resource manager](https://github.com/ydb-platform/ydb/pull/25412), а также изменён ответ `RateLimiter` на [`SCHEME_ERROR` вместо `INTERNAL_ERROR` для несуществующих coordination nodes или ресурсов](https://github.com/ydb-platform/ydb/pull/16901).
* Исправлены пограничные случаи в Healthcheck и maintenance: добавлена обработка [неизвестных значений `StatusV2` в VDisk healthcheck](https://github.com/ydb-platform/ydb/pull/17606), выбор состояния PDisk переключён на [менее двусмысленный источник данных](https://github.com/ydb-platform/ydb/pull/17687), а также добавлен [новый статус BSC PDisk для сценариев длительного обслуживания](https://github.com/ydb-platform/ydb/pull/17920).
* Исправлены проблемы в Workload Manager и связанном с планировщиком коде, включая [use-after-free и VERIFY failures в CPU scheduler и CPU limiter](https://github.com/ydb-platform/ydb/pull/20157).
* Исправлены проблемы удобства работы с DDL для внешних источников и улучшена диагностика для [`ALTER TABLE ... RENAME TO`](https://github.com/ydb-platform/ydb/pull/20670).
* Исправлен ряд дополнительных проблем в OLAP и query engine, включая [падения scan-запросов с предикатами и `LIMIT`](https://github.com/ydb-platform/ydb/pull/16879), [ошибки компиляции `JOIN` с пустой константной стороной](https://github.com/ydb-platform/ydb/pull/17009), [передачу версии языка в computation layer](https://github.com/ydb-platform/ydb/pull/17537), [корректность pushdown фильтров в column shards](https://github.com/ydb-platform/ydb/pull/17743), [падения некоторых OLAP-запросов с `DESC`](https://github.com/ydb-platform/ydb/pull/18059) и [ошибки агрегации `float` в `arrow::Kernel`](https://github.com/ydb-platform/ydb/pull/19466).
* Улучшены удобство и диагностика в скриптах, конфигурации и внешних источниках: YDB добавил [более подробные сообщения об ошибках для `Generic::TPartition`](https://github.com/ydb-platform/ydb/pull/17230), [периодическую статистику прогресса выполнения и динамическое обновление конфигурации query service](https://github.com/ydb-platform/ydb/pull/17314), [корректный вывод optional structs](https://github.com/ydb-platform/ydb/pull/17468), [диагностику внешних источников через список зависимых объектов](https://github.com/ydb-platform/ydb/pull/17911), а также [корректную передачу конфигурации query service в SchemeShard](https://github.com/ydb-platform/ydb/pull/16208).
* Исправлены сетевые, proxy и deployment-проблемы, включая [дополнительные опции reuse сокетов на proxy-портах](https://github.com/ydb-platform/ydb/pull/17429), [редиректы с cluster endpoints на database nodes](https://github.com/ydb-platform/ydb/pull/16764), [вопросы forward compatibility вокруг `MSG_ZEROCOPY`](https://github.com/ydb-platform/ydb/pull/17872), [логику запуска guard actor для zero-copy transfers](https://github.com/ydb-platform/ydb/pull/17826), [репортинг gRPC-метрик для serverless-баз](https://github.com/ydb-platform/ydb/pull/20386) и [гонку в Jaeger settings configurator](https://github.com/ydb-platform/ydb/pull/20785).
* Исправлены дополнительные проблемы хранилища и maintenance-сценариев, включая [редкие VERIFY failures во время репликации](https://github.com/ydb-platform/ydb/pull/16021), [поддержку `MaxFaultyPDisksPerNode` в лимитах CMS/Sentinel](https://github.com/ydb-platform/ydb/pull/16932), [корректную обработку low-space status flags при репликации](https://github.com/ydb-platform/ydb/pull/17418), [запросы на стирание данных для PQ tablets](https://github.com/ydb-platform/ydb/pull/17420), [обработку `PDiskStop` в состояниях Error и Init](https://github.com/ydb-platform/ydb/pull/17780) и [исправление порядка операций BuildIndex, Export и Import](https://github.com/ydb-platform/ydb/pull/17814).
* Исправлены дополнительные продуктовые проблемы, включая [коллизии версии схемы в serverless-базах](https://github.com/ydb-platform/ydb/pull/17729), поведение [`SHOW CREATE TABLE` для `VIEW`](https://github.com/ydb-platform/ydb/pull/16423), [неверный учёт лимитов недоступных узлов в force-availability mode](https://github.com/ydb-platform/ydb/pull/20217) и [некорректную работу с абсолютными путями в workloads](https://github.com/ydb-platform/ydb/pull/19762).
* Функциональность Roaring UDF была расширена и скорректирована за счёт операций [`Add` и `IsEmpty`](https://github.com/ydb-platform/ydb/pull/17650).

##### Дополнительные изменения по PR

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

* Обновлена обработка аутентификации для [`whoami` и `capabilities` handlers](https://github.com/ydb-platform/ydb/pull/17942) во viewer.
* Исправлена проблема во viewer, из-за которой [запросы информации о PDisk могли завершаться таймаутом, если целевой узел был отключён или недоступен](https://github.com/ydb-platform/ydb/pull/20432).
* Исправлен ряд дополнительных проблем viewer, включая [получение списков таблеток для таблиц со вторичными индексами](https://github.com/ydb-platform/ydb/pull/17157), [потерю точности `double` при сериализации](https://github.com/ydb-platform/ydb/pull/18056) и [дублирование данных при realocation памяти во время обработки входящих чанков](https://github.com/ydb-platform/ydb/pull/20929).

##### Дополнительные изменения по PR

* [19680](https://github.com/ydb-platform/ydb/pull/19680) - Vector select workload.
* [15530](https://github.com/ydb-platform/ydb/pull/15530) - Add configuration version in configs_dispatcher page.
## Версия 25.2 {#25-2}

### Версия 25.2.1.24 {#25-2-1-24}

Дата выхода: 28 января 2026.

#### Исправления ошибок

* [Исправлена](https://github.com/ydb-platform/ydb/pull/25112) [проблема](https://github.com/ydb-platform/ydb/issues/23858), из-за которой удаление [таблетки](./concepts/glossary.md#tablet) могло зависать
* [Исправлена](https://github.com/ydb-platform/ydb/pull/25145) [ошибка](https://github.com/ydb-platform/ydb/issues/20866) вызывающая ошибку, при изменении follower'a таблицы
* Исправлен ряд ошибок, связанных с [changefeed](./concepts/glossary.md#changefeed):
  * [Исправлена](https://github.com/ydb-platform/ydb/pull/25689) [ошибка](https://github.com/ydb-platform/ydb/issues/25524), из-за которой импорт таблицы с Utf8-ключом и включённым changefeed мог завершиться неудачно
  * [Исправлена](https://github.com/ydb-platform/ydb/pull/25453) [ошибка](https://github.com/ydb-platform/ydb/issues/25454), когда импорт таблицы без потоков изменений мог завершаться сбоем из-за некорректного поиска файлов changefeed
* [Исправлена](https://github.com/ydb-platform/ydb/pull/26069) [ошибка](https://github.com/ydb-platform/ydb/issues/25869), которая могла приводить к сбоям при UPSERT-операциях в колоночных таблицах
* [Исправлена](https://github.com/ydb-platform/ydb/pull/26504) [ошибка](https://github.com/ydb-platform/ydb/issues/26225), вызывавшая сбой из-за обращения к уже освобождённой памяти
* [Исправлена](https://github.com/ydb-platform/ydb/pull/26657) [ошибка](https://github.com/ydb-platform/ydb/issues/23122) с дубликатами в уникальных вторичных индексах
* [Исправлена](https://github.com/ydb-platform/ydb/pull/26879) [ошибка](https://github.com/ydb-platform/ydb/issues/26565) неверного совпадения контрольных сумм при восстановлении сжатых бэкапов из S3
* [Исправлена](https://github.com/ydb-platform/ydb/pull/27528) [ошибка](https://github.com/ydb-platform/ydb/issues/27193), из-за которой некоторые запросы бенчмарка TPC-H 1000 могли завершаться с ошибкой
* Исправлен ряд проблем, связанных с инициализацией кластера:
  * [Исправлена](https://github.com/ydb-platform/ydb/pull/25678) [ошибка](https://github.com/ydb-platform/ydb/issues/25023), из-за которой инициализация кластера могла зависать при обязательной авторизации
  * [Исправлена](https://github.com/ydb-platform/ydb/pull/28886) [проблема](https://github.com/ydb-platform/ydb/issues/27228) из-за которой создание новых баз данных сразу после развёртывания кластера было невозможно в течение нескольких минут
* [Исправлена](https://github.com/ydb-platform/ydb/pull/28655) [ошибка](https://github.com/ydb-platform/ydb/issues/28510), при которой мог возникать race condition и клиенты получали ошибку `Could not find correct token validator`, если использовались недавно выданные токены до обновления состояния `LoginProvider`
* [Исправлена](https://github.com/ydb-platform/ydb/pull/29940) [ошибка](https://github.com/ydb-platform/ydb/issues/29903), при которой именованное выражение, содержащее другое именованное выражение, приводило к некорректному бэкапу `VIEW`

### Релиз кандидат 25.2.1.10 {#25-2-1-10-rc}

Дата выхода: 21 сентября 2025.

#### Функциональность

* [Аналитические возможности](./concepts/analytics/index.md) доступны по умолчанию: [колоночные таблицы](./concepts/datamodel/table.md?version=v25.2#column-oriented-tables) могут создаваться без включения специальных флагов, с использованием сжатия LZ4 и хеш-партиционирования. Поддерживаемые операции включают широкий набор DML (UPDATE, DELETE, UPSERT, INSERT INTO ... SELECT) и CREATE TABLE AS SELECT. Интеграция с dbt, Apache Airflow, Jupyter, Superset и федеративные запросы к S3 позволяют строить сквозные аналитические пайплайны в YDB.
* [Стоимостной оптимизатор](./concepts/query_execution/optimizer.md?version=v25.2) работает по умолчанию для запросов, использующих хотя бы одну колоночную таблицу, но может быть включён принудительно и для остальных запросов. Стоимостной оптимизатор улучшает производительность выполнения запросов, вычисляя оптимальный порядок и тип соединений на основе статистики таблиц; поддерживаемые [hints](./dev/query-hints.md) позволяют тонко настраивать планы выполнения для сложных аналитических запросов.
* Реализован [трансфер данных](./concepts/transfer.md?version=v25.2) – асинхронный механизм переноса данных из топика в таблицу. [Создание](./yql/reference/syntax/create-transfer.md?version=v25.2) экземпляра трансфера, его [изменение](./yql/reference/syntax/alter-transfer.md?version=v25.2) и [удаление](./yql/reference/syntax/drop-transfer.md?version=v25.2) осуществляется с использованием YQL. Для быстрого старта воспользуйтесь [инструкцией с примером](./recipes/transfer/quickstart.md?version=v25.2).
* Добавлен [спиллинг](./concepts/query_execution/spilling.md?version=v25.2), механизм управления памятью, при котором промежуточные данные, возникающие в результате выполнения запросов и превышающие доступный объём оперативной памяти узла, временно выгружаются во внешнее хранилище. Спиллинг обеспечивает выполнение пользовательских запросов, которые требуют обработки больших объёмов данных, превышающих доступную память узла.
* Увеличено [максимальное время на выполнение одного запроса](./concepts/limits-ydb?version=v25.2) с 30 минут до 2 часов.
* Добавлена поддержка Certificate Authority (CA) и [Yandex Cloud Identity and Access Management (IAM)](https://yandex.cloud/ru/docs/iam) аутентификации в [асинхронной репликации](./yql/reference/syntax/create-async-replication.md?version=v25.2).
* Включены по умолчанию:

  * [векторный индекс](./dev/vector-indexes.md?version=v25.2) для приближённого векторного поиска;
  * поддержка в [YDB Topics Kafka API](./reference/kafka-api/index.md?version=v25.2) [клиентской балансировки читателей](https://www.confluent.io/blog/cooperative-rebalancing-in-kafka-streams-consumer-ksqldb), [компактифицированных топиков](https://docs.confluent.io/kafka/design/log_compaction.html) и [транзакций](https://www.confluent.io/blog/transactions-apache-kafka);
  * поддержка [автопартиционирования топиков](./concepts/cdc.md?version=v25.2#topic-partitions) в CDC для строковых таблиц;
  * поддержка автопартиционирования топиков для асинхронной репликации;
  * поддержка параметризованного [типа Decimal](./yql/reference/types/primitive.md?version=v25.2#numeric);
  * поддержка [типа DateTime64](./yql/reference/types/primitive.md?version=v25.2#datetime);
  * автоудаление временных директорий и таблиц при экспорте в S3;
  * поддержка [потока изменений](./concepts/cdc.md?version=v25.2) в операциях резервного копирования и восстановления;
  * возможность [указания числа реплик](./yql/reference/syntax/alter_table/indexes.md?version=v25.2) для вторичного индекса;
  * системные представления с [историей перегруженных партиций](./dev/system-views?version=v25.2#top-overload-partitions).

#### Исправления ошибок

* [Исправлена](https://github.com/ydb-platform/ydb/pull/24265) ошибка в [Workload Manager](./dev/resource-consumption-management.md), из-за которой потребление CPU колоночными таблицами могло превышать установленные пределы.

## Версия 25.1 {#25-1}

### Версия 25.1.4.7 {#25-1-4-7}

Дата выхода: 15 сентября 2025.

#### Функциональность

* [Добавлена](https://github.com/ydb-platform/ydb/pull/21119) возможность использовать привычные инструменты потоковой обработки данных –  Kafka Connect, Confluent Schema Registry, Kafka Streams, Apache Flink, AKH через [Kafka API](./reference/kafka-api/index.md) при работе с YDB Topics. Теперь YDB Topics Kafka API поддерживает:
  * клиентскую балансировку читателей – включается установкой флага `enable_kafka_native_balancing` в [конфигурации кластера](./reference/configuration/feature_flags.md). [Как работает балансировка читателей в Apache Kafka](https://www.confluent.io/blog/cooperative-rebalancing-in-kafka-streams-consumer-ksqldb). Теперь балансировка читателей в Kafka API YDB Topics будет работать точно так же,
  * [компактифицированные топики](https://docs.confluent.io/kafka/design/log_compaction.html) – включается установкой флага `enable_topic_compactification_by_key`,
  * [транзакции](https://www.confluent.io/blog/transactions-apache-kafka) – включается установкой флага `enable_kafka_transactions`.
* [Добавлен](https://github.com/ydb-platform/ydb/pull/20982) [новый протокол](https://github.com/ydb-platform/ydb/issues/11064) в [Node Broker](./concepts/glossary.md#node-broker), устраняющий всплески сетевого трафика на больших кластерах (более 1000 серверов), связанного с рассылкой информации об узлах.

#### YDB UI

* [Исправлена](https://github.com/ydb-platform/ydb/pull/17839) [ошибка](https://github.com/ydb-platform/ydb/issues/15230), из-за которой не все таблетки отображались на вкладке Tablets в разделе диагностики.
* Исправлена [ошибка](https://github.com/ydb-platform/ydb/issues/18735), из-за которой на вкладке Storage в разделе диагностики базы данных отображались не только узлы хранения.
* Исправлена [ошибка сериализации](https://github.com/ydb-platform/ydb-embedded-ui/issues/2164), которая могла приводить к падению при открытии статистики выполнения запроса.
* Изменена логика перехода узлов в критическое состояние – заполненный на 75-99% CPU pool теперь вызывает предупреждение, а не критическое состояние.

#### Производительность

* [Оптимизирована](https://github.com/ydb-platform/ydb/pull/20197) обработка пустых входов при выполнении JOIN-операций.

#### Исправления ошибок

* [Добавлена](https://github.com/ydb-platform/ydb/pull/21918) поддержка в асинхронной репликации нового типа записи об изменениях — `reset`-записи (в дополнение к `update`- и `erase`-записям).
* [Исправлена](https://github.com/ydb-platform/ydb/pull/21836) [ошибка](https://github.com/ydb-platform/ydb/issues/21814), из-за которой экземпляр репликации с неуказанным параметром `COMMIT_INTERVAL` приводил к сбою процесса.
* [Исправлены](https://github.com/ydb-platform/ydb/pull/21652) редкие ошибки при чтении из топика во время балансировки партиций.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/22455) ошибка, из-за которой при удалении dedicated-базы данных системные таблетки базы могли остаться неудалёнными.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/22203) ошибка, из-за которой таблетки могли зависать при недостатки памяти на узлах. Теперь таблетки будут автоматически запускаться, как только на каком-либо из узлов освободится достаточное количество ресурсов.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/24278) ошибка, из-за которой при записи сообщений Kafka сохранялось только первое сообщение из батча, а остальные сообщения игнорировались.

### Релиз кандидат 25.1.2.7 {#25-1-2-7-rc}

Дата выхода: 14 июля 2025.


#### Функциональность

* [Реализован](https://github.com/ydb-platform/ydb/pull/19504) [векторный индекс](./dev/vector-indexes.md?version=v25.1) для приближённого векторного поиска. Для векторного поиска опубликованы рецепты для [YDB CLI и YQL](./recipes/vector-search?version=v25.1), а также примеры работы [на С++ и Python](./recipes/ydb-sdk/vector-search?version=v25.1).
* [Добавлена](https://github.com/ydb-platform/ydb/issues/11454) поддержка [консистентной асинхронной репликации](./concepts/async-replication.md?version=v25.1).
* Поддержаны запросы [BATCH UPDATE](./yql/reference/syntax/batch-update?version=v25.1) и [BATCH DELETE](./yql/reference/syntax/batch-delete?version=v25.1), позволяющие изменять большие строковые таблицы вне транзакционных ограничений. Включается установкой флага `enable_batch_updates` в конфигурации кластера.
* Добавлен [механизм конфигурации V2](./devops/configuration-management/configuration-v2/config-overview?version=v25.1), упрощающий развёртывание новых кластеров {{ ydb-short-name }} и дальнейшую работу с ними. [Сравнение](./devops/configuration-management/compare-configs?version=v25.1) механизмов конфигурации V1 и V2.
* Добавлена поддержка параметризованного [типа Decimal](./yql/reference/types/primitive.md?version=v25.1#numeric).
* [Добавлена](https://github.com/ydb-platform/ydb/pull/8065) возможность не использовать оператор `DECLARE` для объявления типов параметров в запросах. Теперь типы параметров определяются автоматически на основе переданных значений.
* Реализована клиентская балансировка партиций при чтении по [протоколу Kafka](https://kafka.apache.org/documentation/#consumerconfigs_partition.assignment.strategy) (как у самой Kafka). Раньше балансировка происходила на сервере. Включается установкой флага `enable_kafka_native_balancing` в конфигурации кластера.
* Добавлена поддержка [автопартиционирования топиков](./concepts/cdc.md?version=v25.1#topic-partitions) в CDC для строковых таблиц. Включается установкой флага `enable_topic_autopartitioning_for_cdc` в конфигурации кластера.
* [Добавлена](https://github.com/ydb-platform/ydb/pull/8264) возможность [изменить время хранения данных](./concepts/cdc.md?version=v25.1#topic-options) в CDC-топике с использованием выражения `ALTER TOPIC`.
* [Поддержан](https://github.com/ydb-platform/ydb/pull/7052) [формат DEBEZIUM_JSON](./concepts/cdc.md?version=v25.1#debezium-json-record-structure) для потоков изменений (changefeed).
* [Добавлена](https://github.com/ydb-platform/ydb/pull/19507) возможность создавать потоки изменений к индексным таблицам.
* Добавлена возможность [указания числа реплик](./yql/reference/syntax/alter_table/indexes.md?version=v25.1) для вторичного индекса. Включается установкой флага `enable_access_to_index_impl_tables` в конфигурации кластера.
* В операциях резервного копирования и восстановления расширен состав поддерживаемых объектов. Включается установкой флагов, указанных в скобках:
  * [поддержка](https://github.com/ydb-platform/ydb/issues/7054) потока изменений (флаги `enable_changefeeds_export` и `enable_changefeeds_import`);
  * [поддержка](https://github.com/ydb-platform/ydb/issues/12724) представлений (`VIEW`) (флаг `enable_view_export`).
* Добавлено автоудаление временных директорий и таблиц при экспорте в S3. Включается установкой флага `enable_export_auto_dropping` в конфигурации кластера.
* [Добавлена](https://github.com/ydb-platform/ydb/pull/12909) автоматическая проверка целостности резервных копий при импорте, предотвращающая восстановление из повреждённых резервных копий и защищающая от потери данных.
* [Добавлена](https://github.com/ydb-platform/ydb/pull/15570) возможность создания представлений, использующих [UDF](./yql/reference/builtins/basic.md?version=v25.1#udf) в запросах.
* Добавлены системные представления с информацией о [настройках прав доступа](./dev/system-views?version=v25.1#auth), [истории перегруженных партиций](./dev/system-views?version=v25.1#top-overload-partitions) - включается установкой флага `enable_followers_stats` в конфигурации кластера,  [истории партиций строковых таблиц со сломанными блокировками (TLI)](./dev/system-views?version=v25.1#top-tli-partitions).
* Добавлены новые параметры в операторы [CREATE USER](./yql/reference/syntax/create-user.md?version=v25.1) и [ALTER USER](./yql/reference/syntax/alter-user.md?version=v25.1):
  * `HASH` — возможность задания пароля в зашифрованном виде;
  * `LOGIN` и `NOLOGIN` — разблокировка и блокировка пользователя.
* Повышена безопасность учётных записей:
  * [Добавлена](https://github.com/ydb-platform/ydb/pull/11963) [проверка сложности пароля](./reference/configuration/?version=v25.1#password-complexity) пользователя;
  * [Реализована](https://github.com/ydb-platform/ydb/pull/12578) [автоматическая блокировка пользователя](./reference/configuration/?version=v25.1#account-lockout) при исчерпании лимита попыток ввода пароля;
  * [Добавлена](https://github.com/ydb-platform/ydb/pull/12983) возможность самостоятельной смены пароля пользователем.
* [Реализована](https://github.com/ydb-platform/ydb/issues/9748) возможность переключения функциональных флагов во время работы сервера {{ ydb-short-name }}. Флаги, для которых в [proto-файле](https://github.com/ydb-platform/ydb/blob/main/ydb/core/protos/feature_flags.proto#L60) не указан параметр `(RequireRestart) = true`, будут применяться без рестарта кластера.
* Теперь самые старые (а не новые) блокировки [меняются на полношардовые](https://github.com/ydb-platform/ydb/pull/11329) при превышении количества блокировок на шардах.
* [Реализовано](https://github.com/ydb-platform/ydb/pull/12567) сохранение оптимистичных блокировок в памяти при плавном перезапуске даташардов, что должно уменьшить число ошибок ABORTED из-за потери блокировок при балансировке таблиц между узлами.
* [Реализована](https://github.com/ydb-platform/ydb/pull/12689) отмена волатильных транзакций со статусом ABORTED при плавном перезапуске даташардов.
* [Добавлена](https://github.com/ydb-platform/ydb/pull/6342) возможность удалить `NOT NULL`-ограничения на столбец в таблице с помощью запроса `ALTER TABLE ... ALTER COLUMN ... DROP NOT NULL`.
* [Добавлено](https://github.com/ydb-platform/ydb/pull/9168) ограничение в 100 000 на число одновременных запросов на создание сессий в сервисе координации.
* [Увеличено](https://github.com/ydb-platform/ydb/pull/14219) максимальное [число столбцов в первичном ключе](./concepts/limits-ydb.md?version=v25.1#schema-object) с 20 до 30.
* Улучшена диагностика и интроспекция ошибок, связанных с памятью ([#10419](https://github.com/ydb-platform/ydb/pull/10419), [#11968](https://github.com/ydb-platform/ydb/pull/11968)).
* **_(Экспериментально)_** [Добавлен](https://github.com/ydb-platform/ydb/pull/14075) экспериментальный режим работы с более строгими проверками прав доступа. Включается установкой следующих флагов:
  * `enable_strict_acl_check` – не позволять выдавать права несуществующим пользователям и удалять пользователей, если им выданы права;
  * `enable_strict_user_management` — включает строгие правила администрирования локальных пользователей (т.е. администрировать локальных пользователей может только администратор кластера или базы данных);
  * `enable_database_admin` — добавляет роль администратора базы данных.

#### Изменения с потерей обратной совместимости

* Если вы используете запросы, в которых происходит обращение к именованным выражениям как к таблицам с помощью [AS_TABLE](./yql/reference/syntax/select/from_as_table?version=v25.1), обновите [temporal over YDB](https://github.com/yandex/temporal-over-ydb) на версию [v1.23.0-ydb-compat](https://github.com/yandex/temporal-over-ydb/releases/tag/v1.23.0-ydb-compat) перед обновление YDB на текущую версию, чтобы избежать ошибок в выполнении таких запросов.

#### YDB UI

* В редактор запросов [добавлена](https://github.com/ydb-platform/ydb-embedded-ui/pull/1974) поддержка частичной загрузки результатов — отображение начинается сразу при получении первого фрагмента с сервера без ожидания полного завершения запроса. Это позволяет быстрее получать результаты.
* [Улучшена](https://github.com/ydb-platform/ydb-embedded-ui/pull/1967) безопасность: элементы управления, которые недоступны пользователю, теперь не отображаются в интерфейсе. Пользователи не будут сталкиваться с ошибками "Доступ запрещен".
* [Добавлен](https://github.com/ydb-platform/ydb-embedded-ui/pull/1981) добавлен поиск по идентификатору таблетки на вкладку "Tablets".
* Добавлена подсказка по горячим клавишам, которая открывается по комбинации `⌘+K`.
* На страницу базы данных добавлена вкладка "Операции", которая позволяет просматривать список операций и отменять их.
* Обновлена панель мониторинга кластера, добавлена возможность ее свернуть.
* Реализована поддержка поиска с учетом регистра в инструменте иерархического отображения JSON.
* На верхнюю панель после выбора базы данных добавлены примеры кода для подключения в YDB SDK, что ускоряет процесс разработки.
* Исправлена сортировка строк на вкладке Запросы.
* Удалены лишние запросы подтверждения при закрытии страницы браузера в редакторе запросов — подтверждение запрашивается только когда это необходимо.

#### Производительность

* [Добавлена](https://github.com/ydb-platform/ydb/pull/6509) поддержка [свёртки констант](https://ru.wikipedia.org/wiki/%D0%A1%D0%B2%D1%91%D1%80%D1%82%D0%BA%D0%B0_%D0%BA%D0%BE%D0%BD%D1%81%D1%82%D0%B0%D0%BD%D1%82) в оптимизаторе запросов по умолчанию, что повышает производительность запросов за счёт вычисления константных выражений на этапе компиляции.
* [Добавлен](https://github.com/ydb-platform/ydb/issues/6512) новый протокол гранулярного таймкаста, который позволит сократить время выполнения распределённых транзакций (замедление одного шарда не будет приводить к замедлению всех).
* [Реализована](https://github.com/ydb-platform/ydb/issues/11561) функциональность сохранения состояния даташардов в памяти при перезапусках, что позволяет сохранить блокировки и повысить шансы на успешное выполнение транзакций. Это сокращает время выполнения длительных транзакций за счёт уменьшения числа повторных попыток.
* [Реализована](https://github.com/ydb-platform/ydb/pull/15255) конвейерная обработка внутренних транзакций в [Node Broker](./concepts/glossary?version=v25.1#node-broker), что ускорило запуск динамических узлов в кластере {{ ydb-short-name }}.
* [Улучшена](https://github.com/ydb-platform/ydb/pull/15607) устойчивость Node Broker к повышенной нагрузке со стороны узлов кластера.
* [Включены](https://github.com/ydb-platform/ydb/pull/19440) по умолчанию выгружаемые B-Tree-индексы вместо невыгружаемых SST-индексов, что позволяет сократить потребление памяти при хранении «холодных» данных.
* [Оптимизировано](https://github.com/ydb-platform/ydb/pull/15264) потребление памяти узлами хранения.
* [Сократили](https://github.com/ydb-platform/ydb/pull/10969) время запуска Hive до 30%.
* [Оптимизирован](https://github.com/ydb-platform/ydb/pull/6561) процесс репликации в распределённом хранилище.
* [Оптимизирован](https://github.com/ydb-platform/ydb/pull/9491) размер заголовка больших двоичных объектов в VDisk.
* [Уменьшено](https://github.com/ydb-platform/ydb/pull/15517) потребление памяти за счёт очистки страниц аллокатора.

#### Исправления ошибок

* [Исправлена](https://github.com/ydb-platform/ydb/pull/9707) ошибка в настройке [Interconnect](./concepts/glossary.md?version=v25.1#actor-system-interconnect), приводящая к снижению производительности.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/13993) ошибка «Out of memory» при удалении очень больших таблиц за счёт регулирования числа одновременно обрабатывающих данную операцию таблеток.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/9848) ошибка, возникавшая при указании одного и того же узла базы данных несколько раз в конфигурации для системных таблеток.
* [Устранена](https://github.com/ydb-platform/ydb/pull/11059) ошибка длительного (секунды) чтения данных при частых операциях перешардирования таблицы.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/9723) ошибка чтения из асинхронных реплик, приводившая к сбою.
* [Исправлены](https://github.com/ydb-platform/ydb/pull/9507) редкие зависания при первоначальном сканировании [CDC](./dev/cdc.md?version=v25.1).
* [Исправлена](https://github.com/ydb-platform/ydb/pull/11483) обработка незавершённых схемных транзакций в даташардах при перезапуске системы.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/10460) ошибка несогласованного чтения из топика при попытке явно подтвердить сообщение, прочитанное в рамках транзакции. Теперь пользователь при попытке подтвердить сообщение получит ошибку.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/12220) ошибка, из-за которой автопартиционирование некорректно работало при работе с топиком в транзакции.
* [Исправлены](https://github.com/ydb-platform/ydb/pull/12905) зависания транзакций при работе с топиками во время перезапуска таблеток.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/13910) ошибка «Key is out of range» при импорте из S3-совместимого хранилища.
* [Исправлено](https://github.com/ydb-platform/ydb/pull/13741) некорректное определение конца поля с метаданными в конфигурации кластера.
* [Улучшено](https://github.com/ydb-platform/ydb/pull/16420) построение вторичных индексов: при возникновении некоторых ошибок система ретраит процесс, а не прерывает его.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/16635) ошибка выполнения выражения `RETURNING` в запросах `INSERT INTO` и `UPSERT INTO`.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/16269) проблема зависания операции «Drop Tablet» в PQ tablet, особенно во время задержек в работе Interconnect.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/16194) ошибка, возникавшая во время [компакшна](./concepts/glossary.md?version=v25.1#compaction) VDisk.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/15233) проблема, из-за которой длительные сессии чтения топика завершались с ошибками «too big inflight».
* [Исправлено](https://github.com/ydb-platform/ydb/pull/15515) зависание при чтении топика, если хотя бы одна партиция не имела входящих данных, но читалась несколькими потребителями.
* [Устранена](https://github.com/ydb-platform/ydb/pull/18614) редкая проблема перезагрузок PQ tablet.
* [Устранена](https://github.com/ydb-platform/ydb/pull/18378) проблема, при которой после обновления версии кластера Hive запускались подписчики в датацентрах без работающих узлов баз данных.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/19057) ошибка `Failed to set up listener on port 9092 errno# 98 (Address already in use)`, возникавшая при обновлении версии.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/18905) ошибка, приводившая к segmentation fault при одновременном выполнении запроса к healthcheck и отключении узла кластера.
* [Исправлен](https://github.com/ydb-platform/ydb/pull/18899) сбой в [партиционировании строковой таблицы](./concepts/datamodel/table.md?version=v25.1#partitioning_row_table) при выборе разделённого ключа из образцов доступа, содержащих смешанные операции с полным ключом и префиксом ключа (например, точное чтение или чтение диапазона).
* [Исправлена](https://github.com/ydb-platform/ydb/pull/18647) [ошибка](https://github.com/ydb-platform/ydb/issues/17885), из-за которой тип индекса ошибочно определялся как `GLOBAL SYNC`, хотя в запросе явно указывался `UNIQUE`.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/16797) ошибка, из-за которой автопартиционирование топиков не работало, когда параметр конфигурации `max_active_partition` задавался с помощью выражения `ALTER TOPIC`.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/18938) ошибка, из-за которой `ydb scheme describe` возвращал список столбцов не в том порядке, в котором они были заданы при создании таблицы.

## Версия 24.4 {#24-4}

### Версия 24.4.4.12 {#24-4-4-12}

Дата выхода: 3 июня 2025.

#### Производительность

* [Ограничено](https://github.com/ydb-platform/ydb/pull/17755) количество одновременно обрабатываемых изменений конфигураций.
* [Оптимизировано](https://github.com/ydb-platform/ydb/issues/18289) потребление памяти PQ-таблетками.
* [Оптимизировано](https://github.com/ydb-platform/ydb/issues/18473) потребление CPU таблеткой Scheme shard, что уменьшило задержки ответов на запросы. Теперь лимит на число операций Scheme shard  проверяется до выполнения операций разделения и слияния таблеток.

#### Исправления ошибок

* [Исправлена](https://github.com/ydb-platform/ydb/pull/17123) редкая ошибка зависания клиентских приложений во время выполнения коммита транзакции, когда удаление партиции совершалось раньше обновления квоты на запись в топик.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/17312) ошибка в копировании таблиц с типом Decimal, которая приводила к сбою при откате на предыдущую версию.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/17519) [ошибка](https://github.com/ydb-platform/ydb/issues/17499), при которой коммит без подтверждения записи в топик приводил к блокировке текущей и следующих транзакций с топиками.
* Исправлены зависания транзакций при работе с топиками при [перезагрузке](https://github.com/ydb-platform/ydb/issues/17843) или [удалении](https://github.com/ydb-platform/ydb/issues/17915) таблетки.
* [Исправлены](https://github.com/ydb-platform/ydb/pull/18114) [проблемы](https://github.com/ydb-platform/ydb/issues/18071) с чтением сообщений больше 6Mb через [Kafka API](./reference/kafka-api).
* [Устранена](https://github.com/ydb-platform/ydb/pull/18319) утечка памяти во время записи в [топик](./concepts/glossary#topic).
* Исправлены ошибки обработки [nullable столбцов](https://github.com/ydb-platform/ydb/issues/15701) и [столбцов с типом UUID](https://github.com/ydb-platform/ydb/issues/15697) в строковых таблицах.

### Версия 24.4.4.2 {#24-4-4-2}

Дата выхода: 15 апреля 2025.

#### Функциональность

* Включены по умолчанию:

  * поддержка {% if feature_view %}[представлений (VIEW)](./concepts/datamodel/view.md){% else %}представлений (VIEW){% endif %};
  * режим [автопартиционирования](./concepts/datamodel/topic.md#autopartitioning) топиков;
  * [транзакции с участием топиков и строковых таблиц](./concepts/transactions.md#topic-table-transactions);
  * [волатильные распределённые транзакции](./contributor/datashard-distributed-txs.md#osobennosti-vypolneniya-volatilnyh-tranzakcij).

* Добавлена возможность [чтения и записи в топик](./reference/kafka-api/examples.md#primery-raboty-s-kafka-api) с использованием Kafka API без аутентификации.

#### Производительность

* Включен по умолчанию [автоматический выбор вторичного индекса](./dev/secondary-indexes.md#avtomaticheskoe-ispolzovanie-indeksov-pri-vyborke) при выполнении запроса.

#### Исправления ошибок

* [Исправлена](https://github.com/ydb-platform/ydb/pull/14811) ошибка, которая приводила к существенному снижению скорости чтения с [подписчиков таблетки](./concepts/glossary.md#tablet-follower).
* [Исправлена](https://github.com/ydb-platform/ydb/pull/14516) ошибка, которая приводила к ожиданию подтверждения волатильной распределённой транзакции до следующего рестарта.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/15077) редкая ошибка, которая приводила к сбою при подключении подписчиков таблетки к лидеру с несогласованным состоянием журнала команд.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/15074) редкая ошибка, которая приводила к сбою при перезапуске удалённого datashard с неконсистентными изменениями.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/15194) ошибка, из-за которой мог нарушаться порядок обработки сообщений в топике.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/15308) редкая ошибка, из-за которой могло зависать чтение из топика.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/15160) проблема, из-за которой транзакция зависала при одновременном управлении топиком пользователем и перемещении PQ таблетки на другой узел.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/15233) проблема с утечкой значения счётчика для userInfo, которая могла приводить к ошибке чтения `too big in flight`.
* [Исправлен](https://github.com/ydb-platform/ydb/pull/15467) сбой прокси-сервера из-за дублирования топиков в запросе.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/15933) редкая ошибка, из-за которой пользователь мог писать в топик в обход ограничений квоты аккаунта.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/16288) проблема, из-за которой после удаления топика система возвращала "OK", но его таблетки продолжали работать. Для удаления таких таблеток воспользуйтесь инструкцией из [pull request](https://github.com/ydb-platform/ydb/pull/16288).
* [Исправлена](https://github.com/ydb-platform/ydb/pull/16418) редкая ошибка, из-за которой не восстанавливалась резервная копия большой таблицы с вторичным индексом.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/15862) проблема, которая приводила к ошибке при вставке данных с помощью `UPSERT` в строковые таблицы с значениями по умолчанию.
* [Устранена](https://github.com/ydb-platform/ydb/pull/15334) ошибка, которая приводила к сбою при выполнении запросов к таблицам с вторичными индексами, возвращающих списки результатов с помощью выражения `RETURNING *`.

## Версия 24.3 {#24-3}

### Версия 24.3.15.5 {#24-3-15-5}

Дата выхода: 6 февраля 2025.

#### Функциональность

* Добавлена возможность регистрировать [узел базы данных](./concepts/glossary.md#database-node) по сертификату. В [Node Broker](./concepts/glossary.md#node-broker) добавлен флаг `AuthorizeByCertificate` использования сертификата при регистрации.
* [Добавлены](https://github.com/ydb-platform/ydb/pull/11775) приоритеты проверки аутентификационных тикетов [с использованием стороннего IAM-провайдера](./security/authentication.md#iam), с самым высоким приоритетом обрабатываются запросы от новых пользователей. Тикеты в кеше обновляют свою информацию с приоритетом ниже.

#### Производительность

* [Ускорено](https://github.com/ydb-platform/ydb/pull/12747) поднятие таблеток на больших кластерах:​ 210 мс **→** 125 мс (ssd)​, 260 мс **→** 165 мс (hdd)​.

#### Исправления ошибок

* [Снято](https://github.com/ydb-platform/ydb/pull/11901) ограничение на запись в тип Uint8 значений больше 127.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/12221) ошибка, из-за которой при чтении из топика маленьких сообщений маленькими порциями значительно увеличивалась назгрузка на CPU. Это могло приводить к задержкам в чтении/записи в этот топик.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/12915) ошибка восстановления из резервной копии, сохраненной в хранилище S3 с Path-style адресацией.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/13918) ошибка восстановления из резервной копии, которая была создана в момент автоматического разделения таблицы.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/12601) ошибка в сериализации `Uuid` для [CDC](./concepts/cdc.md).
* [Исправлена](https://github.com/ydb-platform/ydb/pull/12018) потенциальная поломка ["замороженных" блокировок](./contributor/datashard-locks-and-change-visibility#vzaimodejstvie-s-raspredelyonnymi-tranzakciyami), к которой могли приводить массовые операции (например, удаление по TTL).
* [Исправлена](https://github.com/ydb-platform/ydb/pull/12804) ​​ошибка, из-за которой чтение на подписчиках таблетки могло приводить к сбоям во время автоматического разделения таблицы.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/12807) ошибка, при которой [узел координации](./concepts/datamodel/coordination-node.md) успешно регистрировал прокси-серверы несмотря на разрыв связи.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/11593) ошибка, возникающие при открытии в интерфейсе вкладки с информацией о [группах распределенного хранилища](./concepts/glossary.md#storage-group).
* [Исправлена](https://github.com/ydb-platform/ydb/pull/12448) [ошибка](https://github.com/ydb-platform/ydb/issues/12443), из-за которой [Health Check](./reference/ydb-sdk/health-check-api) не сообщал о проблемах в синхронизации времени.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/11658) редкая проблема, которая приводила к ошибкам при выполнении запроса на чтение.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/13501) редкая проблема, которая приводила к утечкам незакоммиченных изменений.
* [Исправлены](https://github.com/ydb-platform/ydb/pull/13948) проблемы с согласованностью, связанные с кэшированием удаленных диапазонов.

### Версия 24.3.11.14 {#24-3-11-14}

Дата выхода: 9 января 2025.

* [Поддержан](https://github.com/ydb-platform/ydb/pull/11276) рестарт без потери доступности кластера в [минимальной отказоустойчивой конфигурации](./concepts/topology#reduced) из трех узлов.
* [Добавлены](https://github.com/ydb-platform/ydb/pull/13218) новый функции UDF Roaring bitmap: AndNotWithBinary, FromUint32List, RunOptimize

### Версия 24.3.11.13 {#24-3-11-13}


Дата выхода: 24 декабря 2024.

#### Функциональность

* Добавлена [трассировка запросов](./reference/observability/tracing/setup) – инструмент, позволяющий детально посмотреть путь следования запроса по распределенной системе.
* Добавлена поддержка [асинхронной репликации](./concepts/async-replication), которая позволяет синхронизировать данные между базами YDB почти в реальном времени. Также она может быть использована для миграции данных между базами с минимальным простоем работающих с ними приложений.
* Добавлена поддержка [представлений (VIEW)](https://ydb.tech/docs/ru/concepts/datamodel/view), которая может быть включена администратором кластера с помощью настройки `enable_views` в [динамической конфигурации](./maintenance/manual/dynamic-config#obnovlenie-dinamicheskoj-konfiguracii).
* В [федеративных запросах](./concepts/query_execution/federated_query/) поддержаны новые внешние источники данных: MySQL, Microsoft SQL Server, Greenplum.
* Разработана [документация](./devops/deployment-options/manual/federated-queries/connector-deployment) по разворачиванию YDB с функциональностью федеративных запросов (в ручном режиме).
* Для Docker-контейнера с YDB добавлен параметр запуска `FQ_CONNECTOR_ENDPOINT`, позволяющий указать адрес коннектора ко внешним источникам данных. Добавлена возможность TLS-шифрования соединения с коннектором. Добавлена возможность вывода порта сервиса коннектора, локально работающего на том же хосте, что и динамический узел YDB.
* Добавлен режим [автопартиционирования](./concepts/datamodel/topic#autopartitioning) топиков, в котором топики могут разбивать партиции в зависимости от нагрузки с сохранением гарантий порядка чтения сообщений и exactly once записи. Режим может быть включен администратором кластера с помощью настроек `enable_topic_split_merge` и `enable_pqconfig_transactions_at_scheme_shard` в [динамической конфигурации](./maintenance/manual/dynamic-config#obnovlenie-dinamicheskoj-konfiguracii).
* Добавлены [транзакции](./concepts/transactions#topic-table-transactions) с участием [топиков](https://ydb.tech/docs/ru/concepts/datamodel/topic) и строковых таблиц. Таким образом, можно транзакционно перекладывать данные из таблиц в топики и в обратном направлении, а также между топиками, чтобы данные не терялись и не дублировались. Транзакции могут быть включены администратором кластера с помощью настроек `enable_topic_service_tx` и `enable_pqconfig_transactions_at_scheme_shard` в [динамической конфигурации](./maintenance/manual/dynamic-config#obnovlenie-dinamicheskoj-konfiguracii).
* [Добавлена](https://github.com/ydb-platform/ydb/pull/7150) поддержка [CDC](./concepts/cdc) для синхронных вторичных индексов.
* Добавлена возможность изменить период хранения записей в [CDC](./concepts/cdc.md) топиках.
* Добавлена поддержка [автоинкремента](./yql/reference/types/serial) для колонок, включенных в первичный ключ таблицы.
* Добавлена запись в [аудитный лог](./security/audit-log) событий логина пользователей в YDB, событий завершения сессии пользователя в пользовательском интерфейсе, а также запроса бэкапа и восстановления из бэкапа.
* Добавлено системное представление, позволяющее получить информацию о сессиях, установленных с базой данных, с помощью запроса.
* Добавлена поддержка константных значений по умолчанию для колонок строковых таблиц.
* Добавлена поддержка выражения `RETURNING` в запросах.
* Добавлена [встроенная функция](./yql/reference/builtins/basic.md#version) `version()`.
* [Добавлены](https://github.com/ydb-platform/ydb/pull/8708) время запуска/завершения и автор в метаданные операций резервного копирования/восстановления из S3-совместимого хранилища.
* Добавлена поддержка резервного копирования/восстановления из S3-совместимого хранилища ACL для таблиц.
* Для запросов, читающих из S3, в план добавлены пути и метод декомпрессии.
* Добавлены новые настройки парсинга для `timestamp`, `datetime` при чтении данных из S3.
* Добавлена поддержка типа `Decimal` в [ключах партиционирования](https://ydb.tech/docs/ru/dev/primary-key/column-oriented#klyuch-particionirovaniya).
* Улучшена диагностика проблем хранилища в HealthCheck.
* **_(Экспериментально)_** Добавлен [стоимостной оптимизатор](./concepts/query_execution/optimizer#stoimostnoj-optimizator-zaprosov) для сложных запросов, где участвуют [колоночные таблицы](./concepts/glossary#column-oriented-table). Оптимизатор рассматривает большое количество альтернативных планов выполнения и выбирает из них лучший на основе оценки стоимости каждого варианта. На текущий момент оптимизатор работает только с планами, где есть операции [JOIN](./yql/reference/syntax/join).
* **_(Экспериментально)_** Реализована начальная версия [менеджера рабочей нагрузки](./dev/resource-consumption-management), который позволяет создавать пулы ресурсов с ограничениями по процессору, памяти и количеству активных запросов. Реализованы классификаторы ресурсов для отнесения запросов к определенному пулу ресурсов.
* **_(Экспериментально)_** Реализован [автоматический выбор индекса](https://ydb.tech/docs/ru/dev/secondary-indexes#avtomaticheskoe-ispolzovanie-indeksov-pri-vyborke) при выполнении запроса, который может быть включен администратором кластера с помощью настройки `index_auto_choose_mode` в `table_service_config` в [динамической конфигурации](./maintenance/manual/dynamic-config#obnovlenie-dinamicheskoj-konfiguracii).

#### YDB UI

* Поддержано создание и [отображение](https://github.com/ydb-platform/ydb-embedded-ui/issues/782) экземпляра асинхронной репликации.
* [Добавлено](https://github.com/ydb-platform/ydb-embedded-ui/issues/929) обозначение [столбцов с автоинкрементом](./yql/reference/types/serial).
* [Добавлена](https://github.com/ydb-platform/ydb-embedded-ui/pull/1438) вкладка с информацией о [таблетках](./concepts/glossary#tablet).
* [Добавлена](https://github.com/ydb-platform/ydb-embedded-ui/pull/1289) вкладка с информацией о [группах распределенного хранилища](./concepts/glossary#storage-group).
* [Добавлена](https://github.com/ydb-platform/ydb-embedded-ui/pull/1218) настройка для добавления [трассировки](./reference/observability/tracing/setup) ко всем запросам и отображение результатов трассировки запроса.
* На страницу PDisk добавлены [атрибуты](https://github.com/ydb-platform/ydb-embedded-ui/pull/1069), информация о потреблении дискового пространства, а также кнопка, которая запускает [декомиссию диска](./devops/deployment-options/manual/decommissioning).
* [Добавлена](https://github.com/ydb-platform/ydb-embedded-ui/pull/1313) информация о выполняющихся запросах.
* [Добавлена](https://github.com/ydb-platform/ydb-embedded-ui/pull/1291) настройка лимита строк в выдаче для редактора запроса и отображение, если результаты запроса превысили лимит.
* [Добавлено](https://github.com/ydb-platform/ydb-embedded-ui/pull/1049) отображение перечня запросов с максимальным потреблением CPU за последний час.
* [Добавлен](https://github.com/ydb-platform/ydb-embedded-ui/pull/1127) поиск на страницах с историей запросов и списком сохраненных запросов.
* [Добавлена](https://github.com/ydb-platform/ydb-embedded-ui/pull/1117) возможность прервать исполнение запроса.
* [Добавлена](https://github.com/ydb-platform/ydb-embedded-ui/issues/944) возможность сохранять запрос из редактора горячими клавишами.
* [Разделено](https://github.com/ydb-platform/ydb-embedded-ui/pull/1422) отображение дисков от дисков-доноров.
* [Добавлена](https://github.com/ydb-platform/ydb-embedded-ui/pull/1154) поддержка InterruptInheritance ACL и улучшено отображение действующих ACL.
* [Добавлено](https://github.com/ydb-platform/ydb-embedded-ui/pull/889) отображение текущей версии пользовательского интерфейса.
* [Добавлена](https://github.com/ydb-platform/ydb-embedded-ui/pull/1229) с информацией о состоянии настроек включения экспериментальной функциональности.

#### Производительность

* [Ускорено](https://github.com/ydb-platform/ydb/pull/7589) восстановление из бэкапа таблиц со вторичными индексами до 20% по нашим тестам.
* [Оптимизирована](https://github.com/ydb-platform/ydb/pull/9721) пропускная способность Interconnect.
* Улучшена производительность CDC-топиков, содержащих тысячи партиций.
* Сделан ряд улучшений алгоритма балансировки таблеток Hive.

#### Исправления ошибок

* [Исправлена](https://github.com/ydb-platform/ydb/pull/6850) ошибка, которая приводила в неработоспособное состояние базу с большим количеством таблиц или партиций при восстановлении из резервной копии. Теперь при превышении лимитов на размер базы, операция восстановления завершится ошибкой, база продолжит работать в штатном режиме.
* [Реализован](https://github.com/ydb-platform/ydb/pull/11532) механизм, принудительно запускающий фоновый [компакшн](./concepts/glossary#compaction) при обнаружении несоответствий между схемой данных и данными, хранящимися в [DataShard](./concepts/glossary#data-shard). Это решает редко возникающую проблему задержки в изменении схемы данных.
* [Устранено](https://github.com/ydb-platform/ydb/pull/10447) дублирование аутентификационных тикетов, которое приводило к повышенному числу запросов в провайдеры аутентификации.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/9377) ошибка нарушения инварианта при первоначальном сканировании CDC, приводившая к аварийному завершению серверного процесса ydbd.
* [Запрещено](https://github.com/ydb-platform/ydb/pull/9446) изменение схемы таблиц резервного копирования.
* [Исправлено](https://github.com/ydb-platform/ydb/pull/9509) зависание первоначального сканирования CDC при частых обновлениях таблицы.
* [Исключены](https://github.com/ydb-platform/ydb/pull/9934) удаленные индексы из подсчета лимита на [максимальное количество индексов](https://ydb.tech/docs/ru/concepts/limits-ydb#schema-object).
* [Исправлена](https://github.com/ydb-platform/ydb/pull/8847) [ошибка](https://github.com/ydb-platform/ydb/issues/6985) в отображении времени, на которое запланировано выполнение набора транзакций (планируемый шаг).
* [Исправлена](https://github.com/ydb-platform/ydb/pull/9161) [проблема](https://github.com/ydb-platform/ydb/issues/8942) прерывания blue–green deployment в больших кластерах, возникающая из-за частого обновления списка узлов.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/8925) редко возникающая ошибка, которая приводила к нарушению порядка выполнения транзакций.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/9841) [ошибка](https://github.com/ydb-platform/ydb/issues/9797) в EvWrite API, которая приводила к некорректному освобождению памяти.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/10698) [проблема](https://github.com/ydb-platform/ydb/issues/10674) зависания волатильных транзакций после перезапуска.
* Исправлена ошибка в CDC, приводящая в некоторых случаях к повышенному потреблению CPU, вплоть до ядра на одну CDC-партицию.
* [Устранена](https://github.com/ydb-platform/ydb/pull/11061) задержка чтения, возникающая во время и после разделения некоторых партиций.
* Исправлены ошибки при чтении данных из S3.
* [Исправлен](https://github.com/ydb-platform/ydb/pull/4793) способ расчета aws signature при обращении к S3.
* Исправлены ложные срабатывания системы HealthCheck в момент бэкапа базы с большим количеством шардов.

## Версия 24.2 {#24-2}

Дата выхода: 20 августа 2024.

### Функциональность

* Добавлена возможность [задать приоритеты](./devops/deployment-options/manual/maintenance.md) задачам обслуживания в [системе управления кластером](./concepts/glossary#cms).
* Добавлена [настройка стабильных имён](reference/configuration/node_broker_config.md#node-broker-config) для узлов кластера в рамках тенанта.
* Добавлено получение вложенных групп от [LDAP-сервера](./security/authentication.md#ldap), в [LDAP-конфигурации](reference/configuration/auth_config.md#ldap-auth-config) улучшен парсинг хостов и добавлена настройка для отключения встроенной аутентификацию по логину и паролю.
* Добавлена возможность аутентификации [динамических узлов](./concepts/glossary#dynamic) по SSL-сертификату.
* Реализовано удаление неактивных узлов из [Hive](./concepts/glossary#hive) без его перезапуска.
* Улучшено управление inflight pings при перезапуске Hive в кластерах большого размера.
* [Изменен](https://github.com/ydb-platform/ydb/pull/6381) порядок установления соединения с узлами при перезапуске Hive.

### YDB UI

* [Добавлена](https://github.com/ydb-platform/ydb/pull/7485) возможность задать TTL для сессии пользователя в конфигурационном файле.
* [Добавлена](https://github.com/ydb-platform/ydb-embedded-ui/pull/1028) сортировка по `CPUTime` в таблицу со списком запросов.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/7779) потеря точности при работе с `double`, `float`.
* Поддержано [создание директорий из UI](https://github.com/ydb-platform/ydb-embedded-ui/issues/958).
* [Добавлена возможность](https://github.com/ydb-platform/ydb-embedded-ui/pull/976) задать интервал фонового обновления данных на всех страницах.
* [Улучшено](https://github.com/ydb-platform/ydb-embedded-ui/issues/955) отображения ACL.
* Включено автодополнение в редакторе запросов по умолчанию.
* [Добавлена](https://github.com/ydb-platform/ydb-embedded-ui/pull/834) поддержка View.

### Исправления ошибок

* Добавлена проверка на размер локальной транзакции до ее коммита, чтобы исправить [ошибки](https://github.com/ydb-platform/ydb/issues/6677) в работе схемных операции при выполнении экспорта/бекапа больших баз.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/7709) [ошибка](https://github.com/ydb-platform/ydb/issues/7674) дублирования результатов SELECT-запроса при уменьшении квоты в [DataShard](./concepts/glossary#data-shard).
* [Исправлены](https://github.com/ydb-platform/ydb/pull/6461) [ошибки](https://github.com/ydb-platform/ydb/issues/6220), возникающие при изменении состояния [координатора](./concepts/glossary#coordinator).
* [Исправлены](https://github.com/ydb-platform/ydb/pull/5992) ошибки, возникающие в момент первичного сканирования [CDC](./dev/cdc).
* [Исправлено](https://github.com/ydb-platform/ydb/pull/6615) состояние гонки в асинхронной доставке изменений (асинхронные индексы, CDC).
* [Исправлена](https://github.com/ydb-platform/ydb/pull/5993) редкая ошибка, из-за которой удаление по [TTL](./concepts/ttl) приводило к аварийному завершению процесса.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/5760) ошибка отображения статуса PDisk в интерфейсе [CMS](./concepts/glossary#cms).
* [Исправлены](https://github.com/ydb-platform/ydb/pull/6008) ошибки, из-за которых мягкий перенос (drain) таблеток с узла мог зависать.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/6445) ошибка остановки interconnect proxy на узле, работающем без перезапусков, при добавлении другого узла в кластер.
* [Исправлен](https://github.com/ydb-platform/ydb/pull/6695) учет свободной памяти в [interconnect](./concepts/glossary#actor-system-interconnect).
* [Исправлены](https://github.com/ydb-platform/ydb/issues/6405) счетчики UnreplicatedPhantoms/UnreplicatedNonPhantoms в VDisk.
* [Исправлена](https://github.com/ydb-platform/ydb/issues/6398) обработка пустых запросов сборки мусора на VDisk.
* [Исправлено](https://github.com/ydb-platform/ydb/pull/5894) управление настройками TVDiskControls через CMS.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/5883) ошибка загрузки данных, созданных более новыми версиями VDisk.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/5862) ошибка выполнении запроса `REPLACE INTO` со значением по умолчанию.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/7714) ошибка исполнения запросов, в которых выполнялось несколько left join'ов к одной строковой таблице.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/7740) потеря точности для `float`, `double` типов при использовании CDC.


## Версия 24.1 {#24-1}

Дата выхода: 31 июля 2024.

### Функциональность

* Реализована [Knn UDF](./yql/reference/udf/list/knn.md) для точного поиска ближайших векторов.
* Разработан gRPC сервис QueryService, обеспечивающий возможность выполнения всех типов запросов (DML, DDL) и выборку неограниченных объёмов данных.
* Реализована [интеграция с LDAP протоколом](./security/authentication.md) и возможность получения перечня групп из внешних LDAP-каталогов.

### Встроенный UI

* Добавлен дашборд диагностики потребления ресурсов, который находится на вкладке с информацией о базе данных и позволяет определить текущее состояние потребление основных ресурсов: ядер процессора, оперативной памяти и места в сетевом распределенном хранилище.
* Добавлены графики для мониторинга основных показателей работы кластера {{ ydb-short-name }}.

### Производительность

* [Оптимизированы](https://github.com/ydb-platform/ydb/pull/1837) таймауты сессий сервиса координации от сервера до клиента. Ранее таймаут составлял 5 секунд, что в худшем случае приводило к определению неработающего клиента (и освобождению удерживаемых им ресурсов) в течение 10 секунд. В новой версии время проверки зависит от времени ожидания сеанса, что обеспечивает более быстрое реагирование при смене лидера или захвате распределённых блокировок.
* [Оптимизировано](https://github.com/ydb-platform/ydb/pull/2391) потребление CPU репликами [SchemeShard](./concepts/glossary.md#scheme-shard), особенно при обработке быстрых обновлений для таблиц с большим количеством партиций.

### Исправления ошибок

* [Исправлена](https://github.com/ydb-platform/ydb/pull/3917) ошибка возможного переполнения очереди, [Change Data Capture](./dev/cdc.md) резервирует емкость очереди изменений при первоначальном сканировании.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/4597) потенциальная взаимоблокировка между получением записей CDC и их отправкой.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/2056) проблема потери очереди задач медиатора при переподключении медиатора, исправление позволяет обработать очередь задач медиатора при ресинхронизации.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/2624) редко возникающая ошибка, когда при включённых и используемых волатильных транзакциях возвращался успешный результат подтверждения транзакции до того, как она была успешно закоммичена. Волатильные транзакции по умолчанию выключены, находятся в разработке.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/2839) редко возникающая ошибка, приводившая к потере установленных блокировок и успешному подтверждению транзакций, которые должны были завершиться ошибкой Transaction Locks Invalidated.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/3074) редкая ошибка, приводящая к возможному нарушению гарантий целостности данных при конкурентной записи и чтении данных по определённому ключу.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/4343) проблема, из-за которой реплики для чтения переставали обрабатывать запросы.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/4979) редкая ошибка, которая могла привести к аварийному завершению процессов базы данных при наличии неподтверждённых транзакций над таблицей в момент её переименования.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/3632) ошибка в логике определения статуса статической группы, когда статическая группа не помечалась нерабочей, хотя должна была.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/2169) ошибка частичного коммита распределённой транзакции с незакоммиченными изменениями в случае некоторых гонок с рестартами.
* [Исправлены](https://github.com/ydb-platform/ydb/pull/2374) аномалии с чтением устаревших данных, которые были [обнаружены с помощью Jepsen](https://blog.ydb.tech/hardening-ydb-with-jepsen-lessons-learned-e3238a7ef4f2).


## Версия 23.4 {#23-4}

Дата выхода: 14 мая 2024.

### Производительность

* [Исправлена](https://github.com/ydb-platform/ydb/pull/3638) проблема повышенного потребления вычислительных ресурсов актором топиков `PERSQUEUE_PARTITION_ACTOR`.
* [Оптимизировано](https://github.com/ydb-platform/ydb/pull/2083) использование ресурсов репликами SchemeBoard. Наибольший эффект заметен при модификации метаданных таблиц с большим количеством партиций.

### Исправления ошибок

* [Исправлена](https://github.com/ydb-platform/ydb/pull/2169) ошибка возможной неполной фиксации накопленных изменений при использовании распределенных транзакций. Данная ошибка возникает при крайне редкой комбинации событий, включающей в себя перезапуск таблеток, обслуживающих вовлеченные в транзакцию партиции таблиц.
* [Устранена](https://github.com/ydb-platform/ydb/pull/3165) гонка между процессами слияния таблиц и сборки мусора, из-за которой сборка мусора могла завершиться ошибкой нарушения инвариантов и, как следствие, аварийным завершением серверного процесса `ydbd`.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/2696) ошибка в Blob Storage, из-за которой информация о смене состава группы хранения могла не поступать своевременно на отдельные узлы кластера. В результате в редких случаях могли блокироваться операции чтения и записи данных, хранящихся в затронутой группе, и требовалось ручное вмешательство администратора.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/3002) ошибка в Blob Storage, из-за которой при корректной конфигурации могли не запускаться узлы хранения данных. Ошибка проявлялась для систем с явным образом включенной экспериментальной функцией "blob depot" (по умолчанию эта функция выключена).
* [Исправлена](https://github.com/ydb-platform/ydb/pull/2475) ошибка, возникавшая в некоторых ситуациях записи в топик с пустым `producer_id` при выключенной дедупликации. Она могла приводить к аварийному завершению серверного процесса `ydbd`.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/2651) проблема, приводящая к падению процесса `ydbd` из-за ошибочного состояния сессии записи в топик.
* [Исправлена](https://github.com/ydb-platform/ydb/pull/3587) ошибка отображения метрики количества партиций в топике, ранее в ней отображалось некорректное значение.
* [Устранены](https://github.com/ydb-platform/ydb/pull/2126) утечки памяти, которые проявлялись при копировании данных топиков между кластерами {{ ydb-short-name }}. Они могли приводить к завершению серверных процессов `ydbd` из-за исчерпания доступной оперативной памяти.


## Версия 23.3 {#23-3}

Дата выхода: 12 октября 2023.

### Функциональность

* Реализована видимость собственных изменений внутри транзакций. Ранее при попытке прочитать данные, уже модифицированные в текущей транзакцией, запрос завершался ошибкой. Это приводило к необходимости упорядочивать чтения и записи внутри транзакции. С появлением видимости собственных изменений эти ограничения снимаются, и запросы могут читать измененные в данной транзакции строчки.
* Добавлена поддержка [колоночных таблиц](concepts/datamodel/table.md#column-tables). Колоночные таблицы хорошо подходят для работы с аналитическими запросами (Online Analytical Processing), так как при выполнении запроса считываются только те столбцы, которые непосредственно участвуют в запросе. Колоночные таблицы YDB позволяют создавать аналитические отчёты с производительностью, сопоставимой со специализированными аналитическими СУБД.
* Добавлена поддержка [Kafka API для топиков](reference/kafka-api/index.md). Теперь с YDB-топиками можно работать через Kafka-совместимый API, предназначенный для миграции существующих приложений. Обеспечена поддержка протокола Kafka версии 3.4.0.
* Добавлена возможность [записи в топик без дедупликации](concepts/datamodel/topic.md#no-dedup). Такой вид записи хорошо подходит для случаев, когда порядок обработки сообщений не критичен. Запись без дедупликации работает быстрее и потребляет меньше ресурсов на сервере, но упорядочение и дедупликация сообщений на сервере не происходит.
* В YQL добавлены возможности [создавать](yql/reference/syntax/create-topic.md), [изменять](yql/reference/syntax/alter-topic.md) и [удалять](yql/reference/syntax/drop-topic.md) топики.
* Добавлена возможность назначать и отзывать права доступа с помощью команд YQL [GRANT](yql/reference/syntax/grant.md) и [REVOKE](yql/reference/syntax/revoke.md).
* Добавлена возможность логгировать DML-операции в аудитном логе.
* **_(Экспериментально)_** При записи сообщений в топик теперь можно передавать метаданные. Для включения этой функциональности добавьте `enable_topic_message_meta: true` в [конфигурационный файл](reference/configuration/index.md).
* **_(Экспериментально)_** Добавлена возможность [чтения из топиков](reference/ydb-sdk/topic.md#read-tx) и запись в таблицу в рамках одной транзакции. Новая возможность упрощает сценарий переноса данных из топика в таблицу. Для её включения добавьте `enable_topic_service_tx: true` в конфигурационный файл.
* **_(Экспериментально)_** Добавлена поддержка [совместимости с PostgreSQL](postgresql/intro.md). Новый механизм позволяет выполнять SQL запросы в PostgreSQL диалекте на инфраструктуре YDB с использованием сетевого протокола PostgreSQL. Можно использовать привычные инструменты работы с PostgreSQL, такие, как psql и драйверы (pq для Golang и psycopg2 для Python), а также разрабатывать запросы на привычном PostgreSQL синтаксисе с горизонтальной масштабируемостью и отказоустойчивость YDB.
* **_(Экспериментально)_** Добавлена поддержка [федеративных запросов](concepts/query_execution/federated_query/index.md). Она позволяет получать информацию из различных источников данных без их переноса в YDB. Поддерживается взаимодействие с ClickHouse, PostgreSQL, S3 через YQL-запросы без дублирования данных между системами.

### Встроенный UI

* В настройках селектора типа запроса добавлена новая опция `PostgreSQL`, которая доступна при включении параметра `Enable additional query modes`. Также в истории запросов теперь учитывается синтаксис, используемый при выполнении запроса.
* Обновлен шаблон YQL-запроса для создания таблицы. Добавлено описание доступных параметров.
* Сортировка и фильтрация для таблиц Storage и Nodes вынесена на сервер. Необходимо включить параметр `Offload tables filters and sorting to backend` в разделе экспериментов, чтобы использовать данный функционал.
* В контекстное меню были добавлены кнопки для создания, изменения и удаления [топиков](concepts/datamodel/topic.md).
* Добавлена сортировка по критичности для всех issues в дереве в `Healthcheck`.

### Производительность

* Реализованы итераторные чтения. Новая функциональность позволяет разделить чтения и вычисления между собой. Итераторные чтения позволяют даташардам увеличить пропускную способность читающих запросов.
* Оптимизирована производительность записи в топики YDB.
* Улучшена балансировка таблеток при перегрузке нод.

### Исправления ошибок

* Исправлена ошибка возможной блокировки читающими итераторами снепшотов, о которых не знают координаторы.
* Исправлена утечка памяти при закрытии соединения в kafka proxy.
* Исправлена ошибка, при которой снепшоты, взятые через читающие итераторы, могут не восстанавливаться на рестартах.
* Исправлен некорректный residual предикат для условия `IS NULL` на колонку.
* Исправлена срабатывающая проверка `VERIFY failed: SendResult(): requirement ChunksLimiter.Take(sendBytes) failed`.
* Исправлен `ALTER TABLE` по `TTL` для колоночных таблиц.
* Реализован `FeatureFlag`, который позволяет отключать/включать работу с `CS` и `DS`.
* Исправлено различие координаторного времени между 23-2 и 23-3 на 50мс.
* Исправлена ошибка, при которой ручка `storage` возвращала лишние группы, когда в запросе параметр `node_id` во `viewer backend`.
* Добавлен `usage` фильтр в `/storage` во `viewer backend`.
* Исправлена ошибка в Storage v2, при которой возвращалось некорректное число в `Degraded`.
* Исправлена отмена подписки от сессий в итераторных чтениях при рестарте таблетки.
* Исправлена ошибка, при которой во время роллинг-рестарта при походе через балансер моргает `healthcheck` алертами про storage.
* Обновлены метрики `cpu usage` в ydb.
* Исправлено игнорирование `NULL` при указании `NOT NULL` в схеме таблицы.
* Реализован вывод записей об операциях `DDL` в общий лог.
* Реализован запрет для команды `ydb table attribute add/drop` работать с любыми объектами, кроме таблиц.
* Отключён `CloseOnIdle` для `interconnect`.
* Исправлено задваивание скорости чтения в UI.
* Исправлена ошибка, при которой могли теряться данные на `block-4-2`.
* Добавлена проверка имени топика.
* Исправлен возможный `deadlock` в акторной системе.
* Исправлен тест `KqpScanArrowInChanels::AllTypesColumns`.
* Исправлен тест `KqpScan::SqlInParameter`.
* Исправлены проблемы параллелизма для OLAP-запросов.
* Исправлена вставка `ClickBench parquet`.
* Добавлен недостающий вызов `CheckChangesQueueOverflow` в общем `CheckDataTxReject`.
* Исправлена ошибка возврата пустого статуса при вызовах `ReadRows API`.
* Исправлен некорректны ретрай экспорта в финальной стадии.
* Исправлена проблема с бесконечной квотой на число записей в CDC-топике.
* Исправлена ошибка импорта колонки `string` и `parquet` в колонку `string` OLAP.
* Исправлено падение `KqpOlapTypes.Timestamp` под tsan.
* Исправлено падение во `viewer backend` при попытке выполнить запрос к базе из-за несовместимости версий.
* Исправлена ошибка, при которой `viewer` не возвращал ответ от `healthcheck` из-за таймаута.
* Исправлена ошибка, при которой в Pdisk'ах могло сохраняться некорректное значение `ExpectedSerial`.
* Исправлена ошибка, при которой ноды базы падают по `segfault` в S3 акторе.
* Исправлена гонка в `ThreadSanitizer: data race KqpService::ToDictCache-UseCache`.
* Исправлена гонка в `GetNextReadId`.
* Исправлено завышение результата `SELECT COUNT(*)` сразу после импорта.
* Исправлена ошибка, при которой `TEvScan` мог вернуть пустой набор данных в случае сплита даташарда.
* Добавлен отдельный issue/код ошибки в случае исчерпания доступного места.
* Исправлена ошибка `GRPC_LIBRARY Assertion failed`.
* Исправлена ошибка, при которой при чтении по вторичному индексу в сканирующих запросах получался пустой результат.
* Исправлена валидация `CommitOffset` в `TopicAPI`.
* Уменьшено потребление `shared cache` при приближении к OOM.
* Смержена логика планировщиков из `data executer` и `scan executer` в один класс.
* Добавлены ручки `discovery` и `proxy` в процесс выполнения `query` во `viewer backend`.
* Исправлена ошибка, при которой ручка `/cluster` возвращает название корневого домена типа `/ru` во `viewer backend`.
* Реализована схема бесшовного обновления табличек для `QueryService`.
* Исправлена ошибка, при которой `DELETE` возвращал данные и НЕ удалял их.
* Исправлена ошибка работы `DELETE ON` в `query service`.
* Исправлено неожиданное выключение батчинга в дефолтных настройках схемы.
* Исправлена срабатывающая проверка `VERIFY failed: MoveUserTable(): requirement move.ReMapIndexesSize() == newTableInfo->Indexes.size()`.
* Увеличен дефолтный таймаут grpc-сриминга.
* Исключены неиспользуемые сообщения и методы из `QueryService`.
* Добавлена сортировка по `Rack` в `/nodes` во `viewer backend`.
* Исправлена ошибка, при которой запрос с сортировкой возвращает ошибку при убывании.
* Исправлено взаимодействие `QP` с `NodeWhiteboard`.
* Удалена поддержка старых форматов параметров.
* Исправлена ошибка, при которой `DefineBox` не применялся для дисков, на которых есть статическая группа.
* Исправлена ошибка `SIGSEGV` в диннодах при импорте `CSV` через `YDB CLI`.
* Исправлена ошибка с падением при обработке `NGRpcService::TRefreshTokenImpl`.
* Реализован `gossip` протокол обмена информацией о ресурсах кластера.
* Исправлена ошибка `DeserializeValuePickleV1(): requirement data.GetTransportVersion() == (ui32) NDqProto::DATA_TRANSPORT_UV_PICKLE_1_0 failed`.
* Реализованы автоинкрементные колонки.
* Использовать статус `UNAVAILABLE` вместо `GENERIC_ERROR` при ошибке идентификации шарда.
* Добавлена поддержка `rope payload` в `TEvVGet`.
* Добавлено игнорирование устаревших событий.
* Исправлено падение write-сессий на невалидном имени топика.
* Исправлена ошибка `CheckExpected(): requirement newConstr failed, message: Rewrite error, missing Distinct((id)) constraint in node FlatMap`.
* Включён `safe heal` по умолчанию.

## Версия 23.2 {#23-2}

Дата выхода: 14 августа 2023.

### Функциональность

* **_(Экспериментально)_** Реализована видимость собственных изменений. При включении этой функции вы можете читать измененные значения из текущей транзакции, которая еще не была закоммичена. Также эта функциональность позволяет выполнять несколько модифицирующих операций в одной транзакции над таблицей с вторичными индексами. Для включения этой функциональности добавьте `enable_kqp_immediate_effects: true` в секцию `table_service_config` в [конфигурационный файл](reference/configuration/index.md).
* **_(Экспериментально)_** Реализованы итераторные чтения. Эта функциональность позволяет разделить чтения и вычисления между собой. Итераторные чтения позволяют даташардам увеличить пропускную способность читающих запросов. Для включения этой функциональности добавьте `enable_kqp_data_query_source_read: true` в секцию `table_service_config` в [конфигурационный файл](reference/configuration/index.md).

### Встроенный UI

* Улучшена навигация:
  * Кнопки переключения между режимами диагностики и разработки вынесены на левую панель.
  * На всех страницах добавлены хлебные крошки.
  * На странице базы данных информация о группах хранения и узлах базы перенесена во вкладки.
* История и сохраненные запросы перенесены во вкладки над редактором запросов.
* На вкладках Info для объектов схемы настройки выведены в терминах конструкции `CREATE` или `ALTER`.
* Поддержано отображение [колоночных таблиц](concepts/datamodel/table.md#column-table) в дереве схемы.

### Производительность

* Для сканирующих запросов реализована возможность эффективного поиска отдельных строк с использованием первичного ключа или вторичных индексов, что позволяет во многих случаях значительно улучшить производительность. Как и в обычных запросах, для использования вторичного индекса необходимо явно указать его имя в тексте запроса с использованием ключевого слова `VIEW`.

* **_(Экспериментально)_** Добавлена возможность управлять системными таблетками базы (SchemeShard, Coordinators, Mediators, SysViewProcessor) её собственному Hive'у, вместо корневого Hive'а, и делать это сразу в момент создания новой базы. Без этого флага системные таблетки новой базы создаются в корневом Hive'е, что может негативно сказаться на его загруженности. Включение этого флага делает базы полностью изолированными по нагрузке, что может быть особенно актуально для инсталляций, состоящих из ста и более узлов. Для включения этой функциональности добавьте `alter_database_create_hive_first: true` в секцию `feature_flags` в [конфигурационный файл](reference/configuration/index.md).

### Исправления ошибок

* Исправлена ошибка в автоконфигурации акторной системы, в результате чего вся нагрузка ложится на системный пул.
* Исправлена ошибка, приводящая к полному сканированию при поиске по префиксу первичного ключа через `LIKE`.
* Исправлены ошибки при взаимодействии с репликами даташардов.
* Исправлены ошибки при работе с памятью в колоночных таблицах.
* Исправлена ошибки при обработке условий для immediate-транзакций.
* Исправлена ошибка в работе итераторных чтений на репликах даташардов.
* Исправлена ошибка, приводящая к лавинообразной переустановке сессий доставки данных до асинхронных индексов
* Исправлены ошибки в оптимизаторе в сканирующих запросах
* Исправлена ошибка некорректного расчёта потребления хранилища hive'ом после расширения базы
* Исправлена ошибка зависания операций от несуществующих итераторов
* Исправлены ошибки при чтении диапазона на `NOT NULL` колонке
* Исправлена ошибка зависания репликации VDisk'ов
* Исправлена ошибка в работе опции `run_interval` в TTL

## Версия 23.1 {#23-1}

Дата выхода 5 мая 2023. Для обновления до версии 23.1 перейдите в раздел [Загрузки](downloads/index.md#ydb-server).

### Функциональность

* Добавлено [первоначальное сканирование таблицы](concepts/cdc.md#initial-scan) при создании потока изменений CDC. Теперь можно выгрузить все данные, которые существуют на момент создания потока.
* Добавлена возможность [атомарной замены индекса](dev/secondary-indexes.md#atomic-index-replacement). Теперь можно атомарно и прозрачно для приложения подменить один индекс другим заранее созданным индексом. Замена выполняется без простоя.
* Добавлен [аудитный лог](security/audit-log.md) — поток событий, который содержит информацию обо всех операциях над объектами {{ ydb-short-name }}.

### Производительность

* Улучшены форматы передачи данных между стадиями исполнения запроса, что ускорило SELECT на запросах с параметрами на 10%, на операциях записи — до 30%.
* Добавлено [автоматическое конфигурирование](reference/configuration/index.md) пулов акторной системы в зависимости от их нагруженности. Это повышает производительность за счет более эффективного совместного использования ресурсов ЦПУ.
* Оптимизирована логика применения предикатов — выполнение ограничений с использованием OR и IN с параметрами автоматически переносится на сторону DataShard.
* (Экспериментально) Для сканирующих запросов реализована возможность эффективного поиска отдельных строк с использованием первичного ключа или вторичных индексов, что позволяет во многих случаях значительно улучшить производительность. Как и в обычных запросах, для использования вторичного индекса необходимо явно указать его имя в тексте запроса с использованием ключевого слова `VIEW`.
* Реализовано кеширование графа вычисления при выполнении запросов, что уменьшает потребление ЦПУ при его построении.

### Исправления ошибок

* Исправлен ряд ошибок в реализации распределенного хранилища данных. Мы настоятельно рекомендуем всем пользователям обновиться на актуальную версию.
* Исправлена ошибка построения индекса на not null колонках.
* Исправлен подсчет статистики при включенном MVCC.
* Исправлены ошибки с бэкапами.
* Исправлена гонка во время сплита и удаления таблицы с CDC.

## Версия 22.5 {#22-5}

Дата выхода 7 марта 2023. Для обновления до версии **22.5** перейдите в раздел [Загрузки](downloads/index.md#ydb-server).

### Что нового

* Добавлены [параметры конфигурации потока изменения](yql/reference/syntax/alter_table/changefeed.md) для передачи дополнительной информации об изменениях в топик.
* Добавлена поддержка [переименования для таблиц](concepts/datamodel/table.md#rename) с включенным TTL.
* Добавлено [управление временем хранения записей](concepts/cdc.md#retention-period) для потока изменений.

### Исправления ошибок и улучшения

* Исправлена ошибка при вставке 0 строк операцией BulkUpsert.
* Исправлена ошибка при импорте колонок типа Date/DateTime из CSV.
* Исправлена ошибка импорта данных из CSV с разрывом строки.
* Исправлена ошибка импорта данных из CSV с пустыми значениями.
* Улучшена производительность Query Processing (WorkerActor заменен на SessionActor).
* Компактификация DataShard теперь запускается сразу после операций split или merge.

## Версия 22.4 {#22-4}

Дата выхода 12 октября 2022. Для обновления до версии **22.4** перейдите в раздел [Загрузки](downloads/index.md#ydb-server).

### Что нового

* {{ ydb-short-name }} Topics и Change Data Capture (CDC):

  * Представлен новый Topic API. [Топик](concepts/datamodel/topic.md) {{ ydb-short-name }} — это сущность для хранения неструктурированных сообщений и доставки их различным подписчикам.
  * Поддержка нового Topic API добавлена в [{{ ydb-short-name }} CLI](reference/ydb-cli/topic-overview.md) и [SDK](reference/ydb-sdk/topic.md). Topic API предоставляет методы потоковой записи и чтения сообщений, а также управления топиками.
  * Добавлена возможность [захвата изменений данных таблицы](concepts/cdc.md) с отправкой сообщений об изменениях в топик.

* SDK:

  * Добавлена возможность взаимодействовать с топиками в {{ ydb-short-name }} SDK.
  * Добавлена официальная поддержка драйвера database/sql для работы с {{ ydb-short-name }} в Golang.

* Embedded UI:

  * Поток изменений CDC и вторичные индексы теперь отображаются в иерархии схемы базы данных как отдельные объекты.
  * Улучшена визуализация графического представления query explain планов.
  * Проблемные группы хранения теперь более заметны.
  * Различные улучшения на основе UX-исследований.

* Query Processing:

  * Добавлен Query Processor 2.0 — новая подсистема выполнения OLTP-запросов со значительными улучшениями относительно предыдущей версии.
  * Улучшение производительности записи составило до 60%, чтения до 10%.
  * Добавлена возможность включения ограничения NOT NULL для первичных ключей в YDB во время создания таблиц.
  * Включена поддержка переименования вторичного индекса в режиме онлайн без остановки сервиса.
  * Улучшено представление query explain, которое теперь включает графы для физических операторов.

* Core:

  * Для read-only транзакций добавлена поддержка консистентного снапшота, который не конфликтует с пишущими транзакциями.
  * Добавлена поддержка BulkUpsert для таблиц с асинхронными вторичными индексами.
  * Добавлена поддержка TTL для таблиц с асинхронными вторичными индексами.
  * Добавлена поддержка сжатия при экспорте данных в S3.
  * Добавлен audit log для DDL statements.
  * Поддержана аутентификация со статическими учетными данными.
  * Добавлены системные представления для диагностики производительности запросов.
