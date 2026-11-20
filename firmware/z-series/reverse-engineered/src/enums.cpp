// Reconstructed from PicoBrew Z 0.0.119 lookup tables and bounds.
// Original names/values: analysis/enums.json. This is not the lost vendor source.
// No original image or absolute application RAM is used at runtime.
#include "zseries/enums.hpp"

namespace zseries { namespace oem {

const char* to_string(WifiEventType value) noexcept {
    static const char* const names[] = {
        "None",
        "Error",
        "ScanStarted",
        "ScanCompleted",
        "AccessPointReady",
        "Connected",
        "Disconnecting",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < WifiEventTypeCount ? names[index] : "<WifiEventType::INVALID>";
}

const char* to_string(ValveLocation value) noexcept {
    static const char* const names[] = {
        "None",
        "Passthrough",
        "Mash",
        "Adjunct1",
        "Adjunct2",
        "Adjunct3",
        "Adjunct4",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < ValveLocationCount ? names[index] : "<ValveLocation::INVALID>";
}

const char* to_string(ZProgramId value) noexcept {
    static const char* const names[] = {
        "None",
        "Rinse",
        "Drain",
        "Rack",
        "Circulate",
        "SousVideWithoutKeg",
        "SousVideWithKeg",
        "PicoStillManual",
        "Test1",
        "Test2",
        "Test3",
        "BrewTest",
        "Clean",
        "PicoPaleDemo",
        "ValveTest",
        "PumpsTest",
        "AlarmTest",
        "ChillTest",
        "KegPumpTest",
        "WetValveTest",
        "CoffeeColdBrew",
        "CoffeeHybrid",
        "CoffeeHot",
        "CoffeeCustom",
        "Buffered",
        "WhirlpoolTest",
        "PicoStillBoilPoint",
        "Chill",
        "ManualBrew",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < ZProgramIdCount ? names[index] : "<ZProgramId::INVALID>";
}

const char* to_string(RuntimeCommandId value) noexcept {
    static const char* const names[] = {
        "None",
        "Start",
        "Stop",
        "Pause",
        "Resume",
        "Skip",
        "Drain",
        "ShowSetupInstructions",
        "RunPreBrewTests",
        "SetTemperature",
        "SetDuration",
        "SetLocation",
        "StartManualStep",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < RuntimeCommandIdCount ? names[index] : "<RuntimeCommandId::INVALID>";
}

const char* to_string(RuntimeStateId value) noexcept {
    static const char* const names[] = {
        "Off",
        "Booting",
        "Idle",
        "Running",
        "Standby",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < RuntimeStateIdCount ? names[index] : "<RuntimeStateId::INVALID>";
}

const char* to_string(RuntimeNotificationType value) noexcept {
    static const char* const names[] = {
        "None",
        "T1Dropped",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < RuntimeNotificationTypeCount ? names[index] : "<RuntimeNotificationType::INVALID>";
}

const char* to_string(RuntimeEventType value) noexcept {
    static const char* const names[] = {
        "Transition",
        "Update",
        "Notification",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < RuntimeEventTypeCount ? names[index] : "<RuntimeEventType::INVALID>";
}

const char* to_string(CoreCommandId value) noexcept {
    static const char* const names[] = {
        "None",
        "PowerButton",
        "CoffeeProgramLoad",
        "MyRecipeListGet",
        "ZKitRecipeListGet",
        "ZKitSmartListGet",
        "ZymaticRecipeLoad",
        "ZymaticRecipeRefLoad",
        "BrewPenRecipeLoad",
        "PicoPakRecipeLoad",
        "PicoPakRecipeRefLoad",
        "PicoPakShippedListGet",
        "PicoPaleDemo",
        "StartProgram",
        "StartLoadedProgram",
        "StartSessionOffline",
        "StartSessionOnline",
        "PicoStillSelected",
        "PicoStillCleanedAck",
        "PicoStillFanOn",
        "PicoStillFanOff",
        "PicoStillScan",
        "PicoStillReconnect",
        "T1MonitorActivate",
        "T1MonitorCancel",
        "RuntimeStart",
        "Stop",
        "Pause",
        "Resume",
        "Skip",
        "Drain",
        "ShowSetupInstructions",
        "RunPreBrewTests",
        "SetTemperature",
        "SetDuration",
        "SetLocation",
        "EepromReset",
        "FactoryReset",
        "RefreshRegistrationCode",
        "DlAndRunProgram",
        "EthernetConnect",
        "WiFiScan",
        "WiFiConnect",
        "BoilerDetect",
        "SetBoilerTypeBig",
        "SetBoilerTypeSmall",
        "AzureSessionLoad",
        "AzureSessionDiscard",
        "AzureWindowTimeout",
        "StandBy",
        "ResumeSession",
        "ResumeLoadedProgram",
        "UseZOffline",
        "UseZOnline",
        "StartStep",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < CoreCommandIdCount ? names[index] : "<CoreCommandId::INVALID>";
}

const char* to_string(CoreEventType value) noexcept {
    static const char* const names[] = {
        "StateTransition",
        "RegistrationNewToken",
        "RegistrationTokenExpired",
        "Loading",
        "LoadingError",
        "SessionCreationError",
        "NetworkMissingError",
        "ProgramLoaded",
        "RecipeRefListLoaded",
        "BoilerDetectResult",
        "RinseNeeded",
        "CleanNeeded",
        "PowerButton",
        "CoreError",
        "ResumedProgramLoaded",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < CoreEventTypeCount ? names[index] : "<CoreEventType::INVALID>";
}

const char* to_string(BoilerType value) noexcept {
    static const char* const names[] = {
        "Big",
        "Small",
        "Unknown",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < BoilerTypeCount ? names[index] : "<BoilerType::INVALID>";
}

const char* to_string(HexDetectError value) noexcept {
    static const char* const names[] = {
        "None",
        "AmbientAboveMax",
        "WortAboveMax",
        "DrainAboveMax",
        "AmbientWortDelta",
        "WortDrainDelta",
        "DrainAmbientDelta",
        "BothHexInvalid",
        "BothHexValid",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < HexDetectErrorCount ? names[index] : "<HexDetectError::INVALID>";
}

const char* to_string(AzureEventType value) noexcept {
    static const char* const names[] = {
        "None",
        "TimeoutStartProgram",
        "DisplayStartProgram",
        "ExitStartProgram",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < AzureEventTypeCount ? names[index] : "<AzureEventType::INVALID>";
}

const char* to_string(StillCommandId value) noexcept {
    static const char* const names[] = {
        "None",
        "Scan",
        "Connect",
        "Start",
        "Stop",
        "CleanedAck",
        "FanOn",
        "FanOff",
        "Reconnect",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < StillCommandIdCount ? names[index] : "<StillCommandId::INVALID>";
}

const char* to_string(StillEventId value) noexcept {
    static const char* const names[] = {
        "None",
        "StateTransition",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < StillEventIdCount ? names[index] : "<StillEventId::INVALID>";
}

const char* to_string(PicoStillStateId value) noexcept {
    static const char* const names[] = {
        "Unknown",
        "Idle",
        "Scanning",
        "SelectingStill",
        "ConnectingToHomeAP1",
        "Authorizing",
        "PendingCleanedAck",
        "SendingCleanedAck",
        "ConnectingToStillAP",
        "PingingOnStillAP",
        "GettingIp",
        "Configuring",
        "ConnectingStillToHomeAP",
        "ConnectingToHomeAP2",
        "PingingOnHomeAP",
        "PendingStart",
        "Starting",
        "Running",
        "Stopping",
        "Error",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < PicoStillStateIdCount ? names[index] : "<PicoStillStateId::INVALID>";
}

const char* to_string(UiCommand value) noexcept {
    static const char* const names[] = {
        "None",
        "Back",
        "BackFirst",
        "Exit",
        "ZKitRecipeListGet",
        "MyRecipeListGet",
        "ZKitRecipeListGetSearch",
        "PacksMenu",
        "ZymaticRecipeLoad",
        "BrewPenRecipeLoad",
        "PicoPakRecipeLoad",
        "PicoPakRecipeLoadTweaked",
        "PicoPakRecipeTweak",
        "PicoPakRecipeTweakAbv",
        "PicoPakRecipeTweakIbu",
        "PicoPakShippedListGet",
        "PicoPakRecipeInput",
        "ZymaticRecipeInput",
        "PicoPaksMenu",
        "ZKitsSmartListMenu",
        "PicoStillMenu",
        "PicoStillSelected",
        "PicoStillCleanedAck",
        "PicoStillFanOn",
        "PicoStillFanOff",
        "PicoStillScan",
        "PicoStillReconnect",
        "T1MonitorMenu",
        "T1MonitorActivate",
        "T1MonitorCancel",
        "SousVideMenu",
        "CoffeeMenu",
        "CoffeeColdBrewTweak",
        "CoffeeHotTweak",
        "CoffeeHybridTweak",
        "CoffeeCustomTweak",
        "CoffeeBatchSizeInput",
        "CoffeeIntensityInput",
        "CoffeeTemperatureInput",
        "CoffeeDurationInput",
        "CoffeeProgramLoad",
        "Utilities",
        "Settings",
        "Tests",
        "DevTests",
        "About",
        "StartLoadedProgram",
        "StartSessionOnline",
        "StartSessionOffline",
        "RuntimeStart",
        "ConfirmStop",
        "ConfirmFinish",
        "Stop",
        "StopAsCancel",
        "StopAsFinish",
        "Pause",
        "Resume",
        "ResumeAsContinue",
        "ResumeAsDone",
        "ResumeAsStart",
        "ResumeAsSkip",
        "Skip",
        "Drain",
        "ShowSetupInstructions",
        "RunPreBrewTests",
        "SetZeroDelay",
        "GetDelay",
        "SilenceAlarm",
        "AdjustTemp",
        "AdjustTime",
        "AdjustLocation",
        "StartRinse",
        "StartClean",
        "StartDrain",
        "StartRack",
        "StartCirculate",
        "StartChill",
        "StartSousVideWithoutKeg",
        "StartSousVideWithKeg",
        "StartPicoStillBoilPoint",
        "StartPicoStillManual",
        "StartPicoPaleDemo",
        "StartManualBrew",
        "ShowRuntimeMenu",
        "RefreshRegistrationCode",
        "DlAndRunProgram",
        "Level",
        "GroupSessionExit",
        "StandBy",
        "StandByAsPowerOff",
        "ResumeInterruptedSession",
        "ResumeLoadedProgram",
        "UseZOfflineMenu",
        "UseZOffline",
        "UseZOnline",
        "StepTweak",
        "StepLocationInput",
        "StepTemperatureInput",
        "StepTimeInput",
        "StepDrainInput",
        "StepStart",
        "StepTerminate",
        "ConfigureNetwork",
        "ConfigureNetworkAsBack",
        "WiFiConnect",
        "WiFiReconnect",
        "WiFiScan",
        "WiFiDetails",
        "WiFiSsidGetKey",
        "ConfigureEthernet",
        "EthernetConnect",
        "ConfigureLanguage",
        "SetEnglish",
        "SetFrench",
        "SetSpanish",
        "ConfigureUnits",
        "UnitsSetImperial",
        "UnitsSetMetric",
        "ConfigureSlideshowDetail",
        "SetDetailedSlideshow",
        "SetBasicSlideshow",
        "ConfigureSlideshowCycle",
        "SetAutoCycle",
        "UnsetAutoCycle",
        "SetAutoCycleAsTurnOn",
        "UnsetAutoCycleAsTurnOff",
        "ConfigurePicoStill",
        "SetStillLoggingEnabled",
        "SetStillLoggingDisabled",
        "ConfigureAzure",
        "SetAzureEnabled",
        "SetAzureDisabled",
        "ShowEeprom",
        "EepromReset",
        "EepromResetMenu",
        "FactoryResetMenu",
        "FactoryReset",
        "ConfigureBoiler",
        "BoilerDetect",
        "SetBoilerTypeBig",
        "SetBoilerTypeSmall",
        "ShowHardwareState",
        "StartValveTest",
        "StartPumpsTest",
        "StartAlarmTest",
        "StartWhirlpoolTest",
        "StartChillTest",
        "StartDrainTest",
        "StartKegPumpTest",
        "StartWetValveTest",
        "InstructionsTest",
        "TitlesTest",
        "PauseTests",
        "PauseTestMenuMessage",
        "PauseTestMenu",
        "PauseTestMessage",
        "PauseTestJustReason",
        "SlideshowBrewTest",
        "SlideshowSousVideTest",
        "TextInputTest",
        "NumInputTest",
        "RfidInputTest",
        "TempInputTest",
        "TimeInputTest",
        "ABVInputTest",
        "LocationInputTest",
        "InputCoffeeBatchSizeTest",
        "InputCoffeeIntensityTest",
        "InputCoffeeTempTest",
        "ScrollTest",
        "MenuTest",
        "MenuLargeTest",
        "ProgressBarTest",
        "AsciiFontTest",
        "DisplayTest",
        "ParagraphTest",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < UiCommandCount ? names[index] : "<UiCommand::INVALID>";
}

const char* to_string(RecipeKind value) noexcept {
    static const char* const names[] = {
        "Zymatic",
        "PicoPak",
        "BrewPen",
        "Unknown",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < RecipeKindCount ? names[index] : "<RecipeKind::INVALID>";
}

const char* to_string(WiFiStateId value) noexcept {
    static const char* const names[] = {
        "Booting",
        "Idle",
        "Scanning",
        "ScanCompleted",
        "AccessPoint",
        "AccessPointDisconnecting",
        "PoweringUp",
        "Connecting",
        "Checking",
        "Connected",
        "Disconnecting",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < WiFiStateIdCount ? names[index] : "<WiFiStateId::INVALID>";
}

const char* to_string(MspStateId value) noexcept {
    static const char* const names[] = {
        "Unknown",
        "BootRunning",
        "AppRunning",
        "BootloaderError",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < MspStateIdCount ? names[index] : "<MspStateId::INVALID>";
}

const char* to_string(LanguageType value) noexcept {
    static const char* const names[] = {
        "English",
        "French",
        "Spanish",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < LanguageTypeCount ? names[index] : "<LanguageType::INVALID>";
}

const char* to_string(SessionMode value) noexcept {
    static const char* const names[] = {
        "Unknown",
        "Online",
        "Offline",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < SessionModeCount ? names[index] : "<SessionMode::INVALID>";
}

const char* to_string(BoolOption value) noexcept {
    static const char* const names[] = {
        "Unknown",
        "False",
        "True",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < BoolOptionCount ? names[index] : "<BoolOption::INVALID>";
}

const char* to_string(TempSensorType value) noexcept {
    static const char* const names[] = {
        "SENSTECH_10-366",
        "SENSTECH_10-364&5",
        "MURATA_NCU18",
        "AET_14015",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < TempSensorTypeCount ? names[index] : "<TempSensorType::INVALID>";
}

const char* to_string(RuntimeStillStateId value) noexcept {
    static const char* const names[] = {
        "None",
        "Idle",
        "Running",
        "MeasuringHeatLoss",
        "FindingBoilPoint",
        "Stabilizing",
        "Distilling",
        "T2OverTemp",
        "ReleasePressure",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < RuntimeStillStateIdCount ? names[index] : "<RuntimeStillStateId::INVALID>";
}

const char* to_string(WiFiLevel value) noexcept {
    static const char* const names[] = {
        "Bar0",
        "Bar1",
        "Bar2",
        "Bar3",
        "Bar4",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < WiFiLevelCount ? names[index] : "<WiFiLevel::INVALID>";
}

const char* to_string(ZBackendError value) noexcept {
    static const char* const names[] = {
        "None",
        "BoilerSNInv",
        "BTypeMismatch",
        "BUpdateError",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < ZBackendErrorCount ? names[index] : "<ZBackendError::INVALID>";
}

const char* to_string(SessionType value) noexcept {
    static const char* const names[] = {
        "Rinse",
        "Clean",
        "Drain",
        "Rack",
        "Circulate",
        "Sousvide",
        "BrewZymatic",
        "BrewPicoPak",
        "BrewZRecipe",
        "BrewDemo",
        "Test",
        "PicoStill",
        "Coffee",
        "Chill",
        "ManualBrew",
        "LocalBrew",
        "Unknown",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < SessionTypeCount ? names[index] : "<SessionType::INVALID>";
}

const char* to_string(HttpClientErrorId value) noexcept {
    static const char* const names[] = {
        "None",
        "HttpStatusError",
        "BrokenInvariant",
        "ConnectError",
        "SendError",
        "StartFailed",
        "InvalidState",
        "InvalidArg",
        "InvalidRequest",
        "SystemClientError",
        "WaitingReplyTimeout",
        "ReadError",
        "ResponseTooBig",
        "PrematureClose",
        "ReadingHeadersTimeout",
        "ApplicationError",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < HttpClientErrorIdCount ? names[index] : "<HttpClientErrorId::INVALID>";
}

const char* to_string(TempSensorId value) noexcept {
    static const char* const names[] = {
        "Hex",
        "Wort",
        "Drain",
        "Ambient",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < TempSensorIdCount ? names[index] : "<TempSensorId::INVALID>";
}

const char* to_string(ScreenId value) noexcept {
    static const char* const names[] = {
        "None",
        "Registration1",
        "Brew1",
        "Brew2",
        "Brew3",
        "Brew4",
        "Brew5",
        "Brew6",
        "Brew7",
        "Brew8",
        "SetupInstructionsOffer",
        "PreBrewTestsOffer",
        "PreBrewTestsCirculation",
        "PreBrewTestsRunning",
        "BrewCompleted",
        "GenericSetup",
        "Pak1",
        "Pak2",
        "Pak3",
        "Pak4",
        "Pak5",
        "Pak6",
        "Pak7",
        "Pak8",
        "PakBrewCompleted",
        "Rinse1",
        "Rinse2",
        "Rinse3",
        "Rinse4",
        "Rinse5",
        "Rinse6",
        "Clean1",
        "Clean2",
        "Clean3",
        "Clean4",
        "Clean5",
        "Clean6",
        "Clean7",
        "Clean8",
        "Clean9",
        "Clean10",
        "Drain1",
        "Circulate1",
        "Chill1",
        "ChillCompleted",
        "Rack1",
        "Rack2",
        "Rack3",
        "Rack4",
        "Rack5",
        "Rack6",
        "Rack7",
        "Rack8",
        "Rack9",
        "ExitConfirm",
        "FinishConfirm",
        "ErrorLoading",
        "ErrorNetwork",
        "ErrorSession",
        "BoilerConfig",
        "SousVide1",
        "SousVide2",
        "SousVideK1",
        "SousVideK2",
        "SousVideK3",
        "SousVideK4",
        "PicoStill1",
        "PicoStill2",
        "PicoStill3",
        "PicoStill4",
        "PicoStill5",
        "PicoStill6",
        "PicoStill7",
        "PicoStillWaitSealed",
        "PicoStillBoilPointFound",
        "PicoStillNotFound",
        "PicoStillFound",
        "PicoStillChooseProgram",
        "PicoStillBoilPointWarning",
        "PicoStillT1MonitorActivate",
        "PicoStillT1MonitorCancel",
        "AskSessionLogging",
        "Coffee1",
        "Coffee2",
        "Coffee3",
        "Coffee4",
        "Coffee5",
        "Coffee6",
        "Coffee7",
        "CoffeeCompleted1",
        "CoffeeCompleted2",
        "RinseNeeded",
        "CleanNeeded",
        "FanOffAfterCooling",
        "KegTest1",
        "TestLong",
        "Test2_1",
        "Test2_2",
        "SessionInterrupted",
        "AltitudeAdjustedTempWarning",
        "UseZOfflineConfirm",
        "StepCompleted",
        "FactoryResetWarning",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < ScreenIdCount ? names[index] : "<ScreenId::INVALID>";
}

const char* to_string(PauseReason value) noexcept {
    static const char* const names[] = {
        "None",
        "Program",
        "User",
        "StepFilter",
        "TooHot",
        "TooCold",
        "MaxHexWortDelta",
        "MaxWortDrainDelta",
        "MaxHexRise",
        "TempInput",
        "TimeInputFine",
        "TimeInputCoarse",
        "PicoStillError",
        "StillPumpOff",
        "SystemError",
        "MspError",
        "CannotRun",
        "StillT2Overtemp",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < PauseReasonCount ? names[index] : "<PauseReason::INVALID>";
}

const char* to_string(Unit value) noexcept {
    static const char* const names[] = {
        "Liter",
        "Gallon",
        "Gram",
        "Ounce",
        "Celsius",
        "Fahrenheit",
        "Second",
        "Intensity",
        "None",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < UnitCount ? names[index] : "<Unit::INVALID>";
}

const char* to_string(AzureState value) noexcept {
    static const char* const names[] = {
        "Disabled",
        "Connecting",
        "Listening",
        "SessionLoaded",
        "SessionRunning",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < AzureStateCount ? names[index] : "<AzureState::INVALID>";
}

const char* to_string(NetworkType value) noexcept {
    static const char* const names[] = {
        "None",
        "Ethernet",
        "WiFi",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < NetworkTypeCount ? names[index] : "<NetworkType::INVALID>";
}

const char* to_string(CoreStateId value) noexcept {
    static const char* const names[] = {
        "Off",
        "Booting",
        "ConnectingWiFi",
        "ReConnectingWiFi",
        "ConnectingEthernet",
        "NeedsNetwork",
        "SyncPending",
        "Syncing",
        "Updating",
        "Registering",
        "DownloadingProgram",
        "StandBy",
        "Idle",
        "Starting",
        "Running",
        "Stopping",
        "Error",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < CoreStateIdCount ? names[index] : "<CoreStateId::INVALID>";
}

const char* to_string(WifiError value) noexcept {
    static const char* const names[] = {
        "None",
        "WifiSsidNotFound",
        "WifiConnectFailed",
        "WifiConnectingTimeout",
        "WifiNoInternet",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < WifiErrorCount ? names[index] : "<WifiError::INVALID>";
}

const char* to_string(Command value) noexcept {
    static const char* const names[] = {
        "",
        "configure",
        "stillip",
        "connecttolan",
        "start",
        "end",
        "fanon",
        "fanoff",
        "ping",
        "data",
        "version",
        "sensors",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < CommandCount ? names[index] : "<Command::INVALID>";
}

const char* to_string(Layout value) noexcept {
    static const char* const names[] = {
        "RuntimeState",
        "RuntimeTransient",
        "Notification",
        "Associate",
        "Slideshow",
        "StillTransient",
        "VScroll",
        "TempInput",
        "TextInput",
        "TimeInput",
        "ABVInput",
        "LocationInput",
        "QuantityInput",
        "Waiting",
        "Level",
        "Progress",
        "Pause",
        "Menu",
        "MenuWithImage",
        "MenuWithText",
        "MenuWithVScroll",
        "Text",
        "None",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < LayoutCount ? names[index] : "<Layout::INVALID>";
}

const char* to_string(CoreErrorId value) noexcept {
    static const char* const names[] = {
        "None",
        "NoIpAddress",
        "NoInternet",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < CoreErrorIdCount ? names[index] : "<CoreErrorId::INVALID>";
}

const char* to_string(HttpMethod value) noexcept {
    static const char* const names[] = {
        "Unknown",
        "GET",
        "POST",
        "PUT",
        "DELETE",
        "UPDATE",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < HttpMethodCount ? names[index] : "<HttpMethod::INVALID>";
}

const char* to_string(State value) noexcept {
    static const char* const names[] = {
        "Idle",
        "Creating",
        "Waiting",
        "Logging",
        "Ending",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < StateCount ? names[index] : "<State::INVALID>";
}

const char* to_string(OpCode value) noexcept {
    static const char* const names[] = {
        "Pause",
        "StepName",
        "ReachTarget",
        "HoldFor",
        "CircFor",
        "DrainFor",
        "MoveValve",
        "SetPumps",
        "NoTarget",
        "Noop",
        "FlagSet",
        "FlagClear",
        "SubStepName",
        "WaitFor",
        "WaitUser",
        "WaitTempA",
        "WaitTempB",
        "WaitTempC",
        "WaitTempE",
        "GetDelay",
        "WaitDelay",
        "GetStepParams",
        "GetTemp",
        "GetTime",
        "WaitTime",
        "SetTarget",
        "SetTime",
        "MspValve",
        "MspPumps",
        "ValveMode",
        "SetScreen",
        "ShowScreen",
        "Heater",
        "StartPid",
        "StopPid",
        "SetITMin",
        "SetITMax",
        "RequireFilter",
        "StopPidKD",
        "PrepareBrew",
        "CompleteBrew",
        "StillScan",
        "StillStart",
        "StSetTargetHL",
        "StFindHeatLoss",
        "StFindBoilPoint",
        "StStabilize",
        "StDistille",
        "WaitStillReady",
        "WaitSealed",
        "WaitVacuum",
        "WaitT3Stable",
        "WaitT1Rise",
        "WaitT3Drop",
        "WaitT2Safety",
        "WaitPumpOk",
        "RinseCycle",
        "Setup",
    };
    const auto index = static_cast<std::uint32_t>(value);
    return index < OpCodeCount ? names[index] : "<OpCode::INVALID>";
}

}} // namespace zseries::oem
