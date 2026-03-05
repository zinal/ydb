#pragma once

#include <yql/essentials/minikql/mkql_node.h>

namespace NKikimr::NKqp {

bool GetFirstTypeIncompatibility(
    const NMiniKQL::TType* expected,
    const NMiniKQL::TType* actual,
    TStringBuf path,
    TString& incompatibility);

} // namespace NKikimr::NKqp
