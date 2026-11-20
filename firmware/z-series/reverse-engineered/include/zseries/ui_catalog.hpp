#pragma once
#include "zseries/display.hpp"
#include "zseries/enums.hpp"

namespace zseries { namespace oem {

// Category names are present in the original lookup diagnostics. The numeric
// category values below are a reconstruction API, not an OEM wire format.
enum class UiTextCategory : std::uint32_t {
    TestEnum, PauseReason, SessionType, RuntimeStillStateId, InfoScreenType,
    TextInfo, TextCommon, TextTime, TextTitle, TextAbout, TextAzure,
    TextSessions, TextInput, TextPause, TextEeprom, SlideshowAutoCycle,
    Units, UiCommand
};
constexpr std::size_t UiTextCategoryCount = 18;
std::size_t ui_text_count(UiTextCategory category) noexcept;
const char* ui_localized_text(UiTextCategory category, std::uint32_t item,
                             LanguageType language = LanguageType::English) noexcept;

enum class UiScreenTextResult { Ok, Unavailable, InvalidLanguage };
// 0x400d9a2c leaves the caller's pointer unchanged for None and unknown IDs.
// Its valid-screen path assumes a valid language. This interface reports an
// invalid language explicitly instead of reproducing the OEM out-of-range read.
UiScreenTextResult ui_screen_text(ScreenId screen, LanguageType language,
                                  const char*& text) noexcept;

// Names describe the recovered pixels and evidenced uses; vendor asset names
// were not retained. Diagnostic images are preserved separately from logos.
enum class UiBitmapId : std::uint32_t {
    LogoLarge, LogoSmall, DiagnosticChecker18, DiagnosticGradient16,
    DiagnosticChecker16A, DiagnosticChecker16B, DiagnosticBitmap64x32,
    TimeSeparatorLarge, DiagnosticEllipse30x8, SelectionSingle,
    DiagnosticEllipse10x8, TimeSeparatorSmall, SelectionBullet,
    SelectionUp, SelectionDown, SelectionBoth
};
constexpr std::size_t UiBitmapCount = 16;
const DisplayBitmap* ui_bitmap(UiBitmapId id) noexcept;

}} // namespace zseries::oem
