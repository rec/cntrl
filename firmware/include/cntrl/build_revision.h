#pragma once
#include <cstdint>

extern "C" const uint32_t cntrl_build_revision;

namespace cntrl {
inline uint32_t build_revision() {
#ifdef CNTRL_FIRMWARE_REVISION
    return cntrl_build_revision;
#elif defined(CNTRL_GIT_COMMIT)
    return CNTRL_GIT_COMMIT;
#else
    return 0;
#endif
}
}
