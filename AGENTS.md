# Project

## Build & Test

```bash
# Build
./ya make --build relwithdebinfo <folder>

# Run all tests
./ya make --build relwithdebinfo -tA <folder>

# Run specific test
./ya make --build relwithdebinfo -tA <folder> -F *test-filter*
```

- Tests include build
- No `-j`
- No force rebuild
- Use `2>&1 | tail` for test output

## C++

- Use C++20 or earlier

## Cursor Cloud specific instructions

YDB builds with the self-bootstrapping `ya` tool (see `BUILD.md` and the Build & Test section above). `ya` downloads its toolchains and prebuilt artifacts from the YDB remote build cache on demand, so no system packages need to be installed. Use `--build relwithdebinfo` to benefit from the remote cache. The cache lives in `~/.ya` and can grow to ~50GB; a first `ydbd` build is mostly cache downloads (a few minutes on 4 CPUs) rather than local compilation.

Key targets and outputs:
- Server: `./ya make --build relwithdebinfo ydb/apps/ydbd` → `ydb/apps/ydbd/ydbd` (relwithdebinfo binary is ~11GB).
- CLI: `./ya make --build relwithdebinfo ydb/apps/ydb` → `ydb/apps/ydb/ydb`.
- Local cluster tool: `./ya make --build relwithdebinfo ydb/public/tools/local_ydb` → `ydb/public/tools/local_ydb/local_ydb`.

Run a local single-node cluster (do NOT run `ydbd` by hand; use the tool):
```
./ydb/public/tools/local_ydb/local_ydb deploy \
  --ydb-working-dir /abs/work/dir --ydb-binary-path /abs/path/to/ydbd
```
Non-obvious caveats:
- `deploy` returns immediately while `ydbd` keeps running in the background; the `ResourceWarning: ... still running` lines printed on exit are harmless.
- The working dir must be an absolute path. Ports are randomized per deploy unless `--fixed-ports` is passed; the chosen gRPC endpoint and database are written to `<work-dir>/ydb_endpoint.txt` and `<work-dir>/ydb_database.txt` (default database is `/local`).
- The `ydb`/`local_ydb` CLIs write a couple of `ydb_endpoint.txt`/`ydb_database.txt` scratch files into the current directory; they are not gitignored, so don't commit them.
- The embedded web Monitoring/Viewer UI is served on the node's `--mon-port` at path `/viewer/` (the mon-port is visible in the running `ydbd server ...` command line).

Query the running cluster:
```
./ydb/apps/ydb/ydb -e grpc://localhost:<grpc-port> -d /local yql -s "SELECT 1;"
```

Lint/style: `./ya style [--dry-run] <file-or-dir>`.

