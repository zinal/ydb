UNITTEST_FOR(ydb/library/yql/udfs/common/rowid)

SIZE(SMALL)

SRCS(
    rowid_sort_order_ut.cpp
)

PEERDIR(
    yql/essentials/types/rowid
)

END()
