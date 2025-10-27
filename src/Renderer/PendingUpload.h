#pragma once

#include "GPU_GUID.h"


namespace rayvox{
    struct PendingUpload{
        GPU_GUID _guid;
        void* data;
        uint64_t dataSize = 0;
        uint32_t alignment = 256;
        uint64_t destinationOffset = 0;
    };
}