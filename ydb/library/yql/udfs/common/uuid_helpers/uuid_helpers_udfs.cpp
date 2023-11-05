#include "uuid_helpers_udfs.h"

namespace {
    SIMPLE_MODULE(TUuidModule,
        TToBase64,
        TFromBase64)
}
REGISTER_MODULES(TUuidModule)

