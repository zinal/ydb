#include "rowid.h"

#include <library/cpp/string_utils/base64/base64.h>

#include <util/stream/output.h>

namespace NKikimr::NRowid {
namespace {

bool DecodeRowidBase64(TStringBuf buf, char* out) {
    if (buf.size() != ROWID_BASE64_LEN) {
        return false;
    }

    try {
        constexpr size_t paddedLen = ROWID_BASE64_LEN + 1;
        char padded[paddedLen];
        std::memcpy(padded, buf.data(), ROWID_BASE64_LEN);
        padded[ROWID_BASE64_LEN] = '=';

        char decoded[Base64DecodeBufSize(paddedLen)];
        const size_t n = Base64StrictDecode(decoded, padded, padded + paddedLen);
        if (n != ROWID_LEN) {
            return false;
        }
        std::memcpy(out, decoded, ROWID_LEN);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

bool IsValidRowidBase64(TStringBuf buf) {
    char decoded[ROWID_LEN];
    return DecodeRowidBase64(buf, decoded);
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
    return DecodeRowidBase64(buf, out);
}

} // namespace NKikimr::NRowid
