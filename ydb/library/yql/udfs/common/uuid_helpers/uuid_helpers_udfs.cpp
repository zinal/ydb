#include "uuid_helpers_udfs.h"

namespace {
    SIMPLE_MODULE(TUuidModule,
        TToBase64<UuidToBase64_Uuid>,
        TToBase64<UuidToBase64_Text>,
        TFromBase64)
}
REGISTER_MODULES(TUuidModule)

