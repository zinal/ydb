UNITTEST_FOR(ydb/library/backup)

SIZE(SMALL)

SRC(
    data_format_ut.cpp
)

SRC(
    ut.cpp
)

PEERDIR(
    library/cpp/string_utils/quote
    ydb/library/backup
)

END()
