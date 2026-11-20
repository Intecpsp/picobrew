#include "zseries/ui_catalog.hpp"

using namespace zseries::oem;
extern "C" const char* ui_catalog_label(unsigned category, unsigned item, unsigned language) {
    return ui_localized_text(static_cast<UiTextCategory>(category), item, static_cast<LanguageType>(language));
}
extern "C" unsigned ui_catalog_count(unsigned category) {
    return static_cast<unsigned>(ui_text_count(static_cast<UiTextCategory>(category)));
}
extern "C" unsigned ui_catalog_screen(unsigned item, unsigned language, const char** text) {
    return static_cast<unsigned>(ui_screen_text(static_cast<ScreenId>(item), static_cast<LanguageType>(language), *text));
}
extern "C" const unsigned char* ui_catalog_bitmap(unsigned item, unsigned* fields) {
    const DisplayBitmap* bitmap = ui_bitmap(static_cast<UiBitmapId>(item));
    if (!bitmap) return nullptr;
    fields[0] = bitmap->width; fields[1] = bitmap->height; fields[2] = bitmap->row_bytes;
    fields[3] = static_cast<unsigned>(bitmap->bounds.left); fields[4] = static_cast<unsigned>(bitmap->bounds.top);
    fields[5] = static_cast<unsigned>(bitmap->bounds.right); fields[6] = static_cast<unsigned>(bitmap->bounds.bottom);
    fields[7] = static_cast<unsigned>(bitmap->bytes);
    return bitmap->pixels;
}
