YQL_UDF_YDB(rowid_udf)

YQL_ABI_VERSION(
    2
    46
    0
)

SRCS(
    rowid.cpp
)

PEERDIR(
    yql/essentials/types/rowid
)

END()

RECURSE_FOR_TESTS(
    test
    ut
)
