#include "rowid.h"

#include <library/cpp/string_utils/base64/base64.h>

#include <util/stream/output.h>

namespace NKikimr::NRowid {

bool IsValidRowidBase64(TStringBuf buf) {
    if (buf.size() != ROWID_BASE64_LEN) {
        return false;
    }
    try {
        char decoded[Base64DecodeBufSize(ROWID_BASE64_LEN)];
        const size_t n = Base64StrictDecode(decoded, buf.begin(), buf.end());
        return n == ROWID_LEN;
    } catch (...) {
        return false;
    }
}

TString RowidBytesToBase64(TStringBuf in) {
    Y_ABORT_UNLESS(in.size() == ROWID_LEN);
    return Base64EncodeNoPadding(in);
}

void RowidBytesToBase64(TStringBuf in, IOutputStream& out) {
    Y_ABORT_UNLESS(in.size() == ROWID_LEN);
    char encoded[Base64EncodeBufSize(ROWID_LEN)];
    const auto encodedBuf = Base64EncodeNoPadding(in, encoded);
    out.Write(encodedBuf.data(), encodedBuf.size());
}

bool ParseRowidBase64(TStringBuf buf, char* out) {
    if (buf.size() != ROWID_BASE64_LEN) {
        return false;
    }
    try {
        char decoded[Base64DecodeBufSize(ROWID_BASE64_LEN)];
        const size_t n = Base64StrictDecode(decoded, buf.begin(), buf.end());
        if (n != ROWID_LEN) {
            return false;
        }
        std::memcpy(out, decoded, ROWID_LEN);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace NKikimr::NRowid
