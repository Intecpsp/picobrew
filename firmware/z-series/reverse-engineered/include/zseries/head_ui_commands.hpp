// Z119 UI-command dispatch, owned payloads and inline editor/screen selection.
#pragma once
#include "zseries/head_numeric_inputs.hpp"
#include "zseries/head_text_input.hpp"
#include "zseries/head_flow.hpp"
#include "zseries/program_list.hpp"
#include "zseries/ui_catalog.hpp"

namespace zseries { namespace oem {

struct HeadUiEvent {
    UiCommand command = UiCommand::None;
    std::uint8_t flags = 0;
    std::uint32_t index = 0;
    RecipeReference recipe;
    ProgramParameters parameters;
};
// Typed representation of ICore's optional command data. Posting adapters must
// own a copy of this object before retaining CoreCommand::detail.
struct HeadUiCommandData {
    std::uint8_t flags = 0;
    std::uint32_t index = 0;
    RecipeReference recipe;
    ProgramParameters parameters;
    std::array<char,17> search{};
    bool has_search = false;
};
// Suitable for PendingEvent<HeadUiCoreEnvelope>. Every copy rebinds detail to
// the destination's owned storage, including copies made by a one-slot mailbox.
struct HeadUiCoreEnvelope {
    CoreCommand command;
    HeadUiCommandData data;
    bool has_data = false;
    HeadUiCoreEnvelope() noexcept = default;
    HeadUiCoreEnvelope(const CoreCommand& value,const HeadUiCommandData* payload) noexcept { assign(value,payload); }
    HeadUiCoreEnvelope(const HeadUiCoreEnvelope& other) noexcept { assign(other.command,other.has_data?&other.data:nullptr); }
    HeadUiCoreEnvelope& operator=(const HeadUiCoreEnvelope& other) noexcept {
        if(this!=&other)assign(other.command,other.has_data?&other.data:nullptr);
        return *this;
    }
    void assign(const CoreCommand& value,const HeadUiCommandData* payload) noexcept {
        command=value;has_data=payload!=nullptr;if(payload)data=*payload;
        command.detail=has_data?&data:nullptr;
    }
};
// ScreenInfo fields selected by 0x401080d4. Pointer-backed children are copied
// by the concrete screen owner; there is no dependence on OEM stack layouts.
struct HeadUiScreen {
    std::uint32_t layout = 0;
    UiCommand command = UiCommand::None, response = UiCommand::None;
    std::uint32_t argument = 0;
    std::uint16_t selection = 0;
    std::uint32_t strength = 5, quantity_id = 8;
    Quantity quantity{0,Unit::None};
    std::uint32_t input_kind = 0;
    std::uint8_t input_limit = 0;
};
enum class HeadUiEffect : std::uint8_t {
    Alarm, DispatchCoreCommand, PostCoreCommand, Synchronize, WaitingMessage,
    Back, ResetScreen, AppendCommand, AppendCommandText, Menu, InstructionMenu,
    Screen, ConnectWifi, CoffeeMenu, About, NetworkConfiguration,
    EthernetConfiguration, SlideshowCycleConfiguration, WifiDetails, WifiKey,
    CoffeeTweak, ManualStepScreen, UnitsConfiguration, SetImperial, SetStaticCycle,
    SetLanguage, SetSlideshowDetail, StillLoggingConfiguration, SetStillLogging,
    AzureConfiguration, SetAzureMode, BoilerConfiguration, RecipeStrengthScreen,
    Tests, DeveloperTests, DisplayTest, MenuTest, PauseTest, InstructionsTest,
    TitlesTest, ScrollTest, ParagraphTest, AsciiFontTest, SlideshowTest,
    ProgressTest, EepromScreen, HardwareScreen, ResetRuntimeMenu,
    AppendCommandFront, ConfirmStop
};
struct HeadUiAction {
    HeadUiEffect effect;
    std::uint32_t first = 0, second = 0, third = 0;
    const char* title = nullptr;
    const char* text = nullptr;
    HeadUiScreen screen;
    const CoreCommand* core_command = nullptr;
    const HeadUiCommandData* data = nullptr;
    const HeadUiEvent* event = nullptr;
    const RecipeReference* recipe = nullptr;
    explicit HeadUiAction(HeadUiEffect value) noexcept : effect(value) {}
};
struct HeadUiServices {
    void* context = nullptr;
    // All pointers are borrowed only for this immediate call. A serviced
    // drop-new mailbox operation is success even when the slot is occupied.
    bool (*perform)(void*,const HeadUiAction&) = nullptr;
    std::uint32_t (*millis)(void*) = nullptr;
    // Original diagnostics log, increment the shared counter, delay 200ms, return.
    bool (*assertion)(void*,std::uint32_t original_line) = nullptr;
};
struct HeadUiObserved {
    bool service_mode = false, t1_monitor = false;
    std::uint32_t runtime_location = 0;
    CoreStateId core = CoreStateId::Off;
    SessionType session = SessionType::Unknown;
    const char* session_name = nullptr;
    std::uint32_t session_name_length = 0;
};
struct HeadUiState {
    RecipeReference recipe;
    bool display_suspended = false;
    std::uint32_t runtime_menu_duration = 7000, assertion_line = 0;
};
enum class HeadUiResult { Ok, MissingService, ServiceFailure, AssertionFailure };
class HeadUiCommands {
public:
    HeadUiCommands(HeadNumericState& numeric,HeadTextState& text,HeadTimer& transient) noexcept
        : numeric(numeric),text(text),transient(transient) {}
    HeadNumericState& numeric;
    HeadTextState& text;
    HeadTimer& transient;
    HeadUiState state;
    HeadUiObserved observed;
    HeadUiServices services;
    LanguageType language = LanguageType::English;
    HeadUiResult consume(PendingEvent<HeadUiEvent>&); // 0x401080d4
    HeadUiResult handle(const HeadUiEvent&);
private:
    HeadUiResult perform(const HeadUiAction&);
    HeadUiResult effect(HeadUiEffect,std::uint32_t=0,std::uint32_t=0,std::uint32_t=0);
    HeadUiResult assertion(std::uint32_t);
    HeadUiResult post(CoreCommand&,const HeadUiCommandData* = nullptr);
    HeadUiResult start(std::uint32_t);
    HeadUiResult screen(const char*,const HeadUiScreen&);
    HeadUiResult screen(std::uint32_t,const HeadUiScreen&);
    HeadUiResult menu(std::uint32_t,const std::uint32_t*,std::size_t);
    HeadUiResult recipe_command(const HeadUiEvent&);
};

}} // namespace zseries::oem
