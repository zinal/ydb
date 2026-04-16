# Low-code platforms: priority candidates for YDB infrastructure

This note evaluates popular open-source low-code platforms as candidates for native support of {{ ydb-short-name }} as the platform's own storage and message delivery infrastructure.

{% note info %}

This document is a prioritization note, not a statement of existing compatibility. The goal is to identify where YDB engineering effort is most likely to produce a useful upstream integration or a compelling reference architecture.

{% endnote %}

## Why YDB is relevant for this class of products

The strongest low-code platforms are no longer simple UI builders. Most of them now include:

- A transactional metadata store for users, projects, permissions, credentials, and execution history.
- A worker model for background execution, retries, schedules, or webhooks.
- A need for high availability and horizontal scale in self-hosted enterprise deployments.

These requirements align well with {{ ydb-short-name }} capabilities:

- [Row-oriented tables](../concepts/datamodel/table.md#row-oriented-tables) for transactional state.
- [Distributed ACID transactions](../concepts/transactions.md) for consistent workflow and metadata updates.
- [Topics](../concepts/datamodel/topic.md) for durable message delivery with at-least-once consumption and exactly-once publishing.
- [Kafka API compatibility](../reference/kafka-api/index.md) for ecosystems that already expect Kafka-style producers and consumers.

## Evaluation criteria

The candidate list below is ranked using four practical criteria:

1. **Market pull**: community size, mindshare, and self-hosted adoption.
2. **Infrastructure leverage**: whether YDB can replace both the persistent store and the message delivery layer, rather than acting as only another user-facing connector.
3. **Architectural fit**: similarity between the platform's current internals and YDB strengths such as SQL transactions, automatic sharding, and durable streams.
4. **Integration scope**: expected implementation complexity and the amount of platform value unlocked by the integration.

## Candidate set

GitHub star counts below are a point-in-time snapshot taken on 2026-04-16.

| Platform | GitHub stars | Core persistence today | Job queue / message delivery today | YDB fit | Priority |
| --- | ---: | --- | --- | --- | --- |
| [n8n](https://github.com/n8n-io/n8n) | 184,279 | Postgres recommended for queue mode | Redis-backed queue mode with separate workers | Excellent combined storage + messaging fit | **Top 3** |
| [NocoDB](https://github.com/nocodb/nocodb) | 62,728 | SQLite / PostgreSQL / MySQL / MSSQL for metadata | Not a central platform concern | Good storage fit, weak messaging fit | Medium |
| [Appsmith](https://github.com/appsmithorg/appsmith) | 39,624 | MongoDB is the primary application store | Redis cache, Temporal for workflows, PostgreSQL only for selected subsystems | Weak fit for core storage replacement | Low |
| [ToolJet](https://github.com/ToolJet/ToolJet) | 37,742 | PostgreSQL for app state and ToolJet DB metadata | Redis + BullMQ workflows with dedicated workers | Strong fit with some compatibility risk | **Top 3** |
| [Directus](https://github.com/directus/directus) | 34,802 | SQL databases | Redis mainly for cache and horizontal scaling | Good storage fit, limited message-infra upside | Medium |
| [Budibase](https://github.com/Budibase/budibase) | 27,826 | CouchDB | Redis + worker services | Large porting effort, weaker fit | Low |
| [Node-RED](https://github.com/node-red/node-red) | 23,015 | Memory / filesystem by default, pluggable context store | Event-driven runtime, not centered on a durable broker | Good plugin target, weak platform-infra target | Medium |
| [NocoBase](https://github.com/nocobase/nocobase) | 22,130 | SQL-backed application metadata | Built-in async workflow queueing | Interesting, but smaller and less differentiated than the top 3 | Medium |
| [Activepieces](https://github.com/activepieces/activepieces) | 21,728 | PostgreSQL | Redis / BullMQ | Good technical fit, but overlaps heavily with n8n | Medium |
| [Windmill](https://github.com/windmill-labs/windmill) | 16,245 | PostgreSQL for the whole platform state | Database-backed job queue with worker replicas | Excellent technical fit, good strategic leverage | **Top 3** |

## Recommended top 3

### 1. n8n

`n8n` is the best first target.

Why it ranks first:

- It has by far the largest open-source footprint in the set.
- Its production architecture already separates durable state from worker execution.
- It has a clear queue-driven execution model, so YDB Topics can be used for real platform value rather than only as an optional integration.

Why YDB fits:

- Workflow definitions, credentials metadata, execution logs, and audit records map naturally to YDB tables.
- Worker dispatch, retries, webhook fan-out, and execution events align with YDB Topics.
- The platform's need for HA, scale-out workers, and durable execution history matches YDB's distributed architecture.

What a YDB-backed design could look like:

- **Tables**: workflows, users, credentials metadata, execution state, execution history, scheduling metadata.
- **Topics**: execution dispatch, retry queues, webhook delivery, event/audit streams.
- **Optional protocol strategy**: Kafka API support can help where the surrounding ecosystem already expects Kafka-style messaging.

Main risk:

- `n8n` queue mode is implemented around Redis today, so the work is not a drop-in configuration change. It likely requires either a dedicated YDB-backed queue adapter or a deeper execution-engine integration.

### 2. Windmill

`Windmill` is the strongest pure infrastructure match, even though its community is smaller than `ToolJet` or `Appsmith`.

Why it ranks this high:

- Windmill documents a simple production architecture centered on a PostgreSQL database that stores the entire platform state, including the job queue.
- The product is built around workers, job throughput, and reliable execution, which is exactly where YDB's storage-plus-streaming model is compelling.
- A successful integration would be easy to explain: one distributed backend for both metadata durability and execution delivery.

Why YDB fits:

- Platform state, resource metadata, scripts, schedules, job results, and audit history fit naturally in YDB tables.
- Topic-based dispatch can replace or augment the database queue path for higher-scale worker coordination.
- Strong consistency is valuable for job ownership, retries, and status transitions.

Main risk:

- Windmill currently benefits from a very simple "everything in Postgres" model. Replacing that with a YDB-native design requires careful changes around queue semantics and worker coordination, not only SQL compatibility.

### 3. ToolJet

`ToolJet` is the best app-builder candidate in the group.

Why it makes the top 3:

- It has meaningful community traction.
- It combines a metadata-heavy internal tools platform with a real workflow engine and dedicated workers.
- YDB support would showcase not only low-code UI storage, but also background automation and operational workflows.

Why YDB fits:

- App metadata, permissions, environment settings, workflow state, and execution logs are a good table fit.
- Workflow scheduling and execution queues are a good topic fit.
- Multi-tenant enterprise deployments could benefit from YDB's scaling and fault-tolerance characteristics.

Main risk:

- ToolJet is coupled to PostgreSQL and Redis in several places, including its internal ToolJet DB and workflow stack. In particular, any subsystem that assumes PostgreSQL-specific behavior will need a focused compatibility review before committing to an implementation plan.

## Why the other platforms are not first-wave targets

### Appsmith

Very popular, but not a strong first target for YDB-backed infrastructure. Appsmith's primary application store is MongoDB, while PostgreSQL and Temporal are secondary subsystems. That means YDB would not replace the platform's main storage dependency without a much larger architectural rewrite.

### NocoDB and Directus

Both are strong candidates for **external data source** support, but weaker candidates for YDB as the platform's own storage and message delivery backbone. They are storage-centric products and underuse YDB Topics compared with workflow-driven platforms.

### Budibase

Budibase's self-hosted architecture centers on CouchDB plus Redis and object storage. That makes it a higher-effort port with less architectural alignment than `n8n`, `Windmill`, or `ToolJet`.

### Node-RED

Node-RED is attractive as a plugin ecosystem target because it has a pluggable context store model and an event-driven user base. However, its runtime is not built around a durable brokered execution backend in the same way as `n8n` or `ToolJet`, so the strategic leverage is lower.

### NocoBase and Activepieces

Both are credible second-wave options. `NocoBase` has an interesting async workflow model, while `Activepieces` is a solid workflow-automation fit. They do not make the top 3 because they either have less market pull or overlap too much with stronger candidates already selected.

## Final recommendation

If YDB support is limited to three first-wave targets, the recommended order is:

1. **n8n** for maximum ecosystem impact.
2. **Windmill** for the cleanest infrastructure story and the best technical fit.
3. **ToolJet** for coverage of the internal-tools / app-builder segment.

If only one pilot integration is funded, start with **n8n**. If two are funded, pair **n8n** with **Windmill** to cover both the largest community and the best technical fit.
