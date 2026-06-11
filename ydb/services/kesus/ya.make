LIBRARY()

SRCS(
    grpc_list_semaphores.cpp
    grpc_service.cpp
)

PEERDIR(
    ydb/library/aclib
    ydb/library/grpc/server
    ydb/core/base
    ydb/core/grpc_services
    ydb/core/grpc_streaming
    ydb/core/kesus/proxy
    ydb/core/kesus/tablet
    ydb/public/api/grpc
    ydb/public/api/grpc/draft
    ydb/public/sdk/cpp/src/library/operation_id
)

END()
