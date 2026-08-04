#pragma once

#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/system/types.h>

#include <array>
#include <cstring>

class IOutputStream;

namespace NKikimr::NRowid {

static constexpr ui32 ROWID_LEN = 14;
// Base64 without padding for 14 bytes is 19 characters (14*8/6 rounded up).
static constexpr ui32 ROWID_BASE64_LEN = 19;

inline bool IsValidRowidBytes(TStringBuf buf) {
    return buf.size() == ROWID_LEN;
}

bool IsValidRowidBase64(TStringBuf buf);
TString RowidBytesToBase64(TStringBuf in);
void RowidBytesToBase64(TStringBuf in, IOutputStream& out);
bool ParseRowidBase64(TStringBuf buf, char* out /*[ROWID_LEN]*/);

inline bool ParseRowidBase64ToArray(TStringBuf buf, std::array<char, ROWID_LEN>& out) {
    return ParseRowidBase64(buf, out.data());
}

} // namespace NKikimr::NRowid
