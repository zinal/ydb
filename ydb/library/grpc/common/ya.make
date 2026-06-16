LIBRARY()

SRCS(
    constants.h
    log_context.cpp
    log_context.h
    time_point.h
)

PEERDIR(
    contrib/libs/grpc
    ydb/library/actors/core
)

END()
