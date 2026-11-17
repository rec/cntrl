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
    // A missing Tamzen letter selects Arimo for the complete caption.
    for (const char *caption : {"Ağaç", "Ałą", "Așa"}) {
        cntrl::ui::Glyph fallback;
        assert(fonts.glyph(cntrl::ui::status_face, 13, 0, caption, 0, fallback));
        assert(fonts.glyph(17, 13, 0, caption, 0, glyph));
        assert(fallback.pixels == glyph.pixels && fallback.x == glyph.x && fallback.y == glyph.y);
        const auto status = fonts.bounds(cntrl::ui::status_face, 13, 0, caption);
        const auto arimo = fonts.bounds(17, 13, 0, caption);
        assert(status.width == arimo.width && status.top == arimo.top && status.height == arimo.height);
    }
    assert(fonts.glyph(cntrl::ui::status_face, 13, 0, "Status", 0, glyph));
    cntrl::ui::Glyph arimo;
    assert(fonts.glyph(17, 13, 0, "Status", 0, arimo));
    assert(glyph.pixels != arimo.pixels); // Complete Tamzen captions keep Tamzen.
    // Translation-only packs use the shared flash face, including accents.
    cntrl::ui::AssetFonts::localized(nullptr, 0);
    assert(fonts.supports(cntrl::ui::localized_face, 12));
    assert(fonts.glyph(cntrl::ui::localized_face, 12, 0, "Éléments", 0, glyph));
    assert(glyph.pixels && glyph.width && glyph.height);
}
