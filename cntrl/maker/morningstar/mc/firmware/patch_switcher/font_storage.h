#pragma once
#include <cntrl/ui/font_storage.h>
#include "filesystem.h"

namespace font_assets {
class Files : public Storage {
public:
    Files() : Storage(filesystem_, reader_, writer_) {}
private:
    cntrl::device::SdFileSystem filesystem_;
    cntrl::device::SdFile reader_, writer_;
};
} // namespace font_assets
