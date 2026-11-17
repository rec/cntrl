#include "ui_fonts.h"
#include <cassert>
int main() {
    cntrl::ui::AssetFonts fonts(embedded_fonts, sizeof(embedded_fonts) / sizeof(*embedded_fonts));
    assert(fonts.supports(16, 12));
    const auto bounds = fonts.bounds(16, 12, 0, "Status");
    assert(bounds.width > 0 && bounds.height > 0);
    cntrl::ui::Glyph glyph;
    assert(fonts.glyph(16, 12, 0, "Status", 0, glyph));
    assert(glyph.pixels && glyph.width && glyph.height);
    // Translation-only packs use the shared flash face, including accents.
    cntrl::ui::AssetFonts::localized(nullptr, 0);
    assert(fonts.supports(cntrl::ui::localized_face, 12));
    assert(fonts.glyph(cntrl::ui::localized_face, 12, 0, "Éléments", 0, glyph));
    assert(glyph.pixels && glyph.width && glyph.height);
}
