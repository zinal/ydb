#pragma once

#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/types/tx/tx.h>

namespace NYdb::inline Dev::NTopic {

struct TTransactionId {
    std::string SessionId;
    std::string TxId;
};

inline
bool operator==(const TTransactionId& lhs, const TTransactionId& rhs)
{
    return (lhs.SessionId == rhs.SessionId) && (lhs.TxId == rhs.TxId);
}

inline
bool operator!=(const TTransactionId& lhs, const TTransactionId& rhs)
{
    return !(lhs == rhs);
}

TTransactionId MakeTransactionId(const TTransactionBase& tx);

TStatus MakeSessionExpiredError();
TStatus MakeCommitTransactionSuccess();

}

template <>
struct THash<NYdb::NTopic::TTransactionId> {
    size_t operator()(const NYdb::NTopic::TTransactionId& v) const noexcept {
        return CombineHashes(THash<std::string>()(v.SessionId), THash<std::string>()(v.TxId));
    }
};
