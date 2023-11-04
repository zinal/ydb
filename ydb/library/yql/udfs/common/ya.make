RECURSE(
    clickhouse/client
    datetime
    datetime2
    digest
    file
    histogram
    hyperloglog
    ip_base
    json
    json2
    math
    pire
    protobuf
    re2
    set
    stat
    streaming
    string
    top
    topfreq
    unicode_base
    url_base
    uuid_helpers
    yson2
)

IF (ARCH_X86_64)
    RECURSE(
        hyperscan
    )
ENDIF()

