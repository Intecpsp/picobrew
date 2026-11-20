// Reconstructed from PicoBrew Z 0.0.119 lookup tables and bounds.
// Original names/values: analysis/enums.json. This is not the lost vendor source.
// No original image or absolute application RAM is used at runtime.
#pragma once
#include <cstddef>
#include <cstdint>

namespace zseries { namespace oem {

// DROM 0x3f448420; observed bound in 0x400d0458.
enum class WifiEventType : std::uint32_t {
    None = 0,
    Error = 1,
    ScanStarted = 2,
    ScanCompleted = 3,
    AccessPointReady = 4,
    Connected = 5,
    Disconnecting = 6,
};
constexpr std::size_t WifiEventTypeCount = 7;
const char* to_string(WifiEventType value) noexcept;

// DROM 0x3f448358; observed bound in 0x400d04c0.
enum class ValveLocation : std::uint32_t {
    None = 0,
    Passthrough = 1,
    Mash = 2,
    Adjunct1 = 3,
    Adjunct2 = 4,
    Adjunct3 = 5,
    Adjunct4 = 6,
};
constexpr std::size_t ValveLocationCount = 7;
const char* to_string(ValveLocation value) noexcept;

// DROM 0x3f448374; observed bound in 0x400d0518.
enum class ZProgramId : std::uint32_t {
    None = 0,
    Rinse = 1,
    Drain = 2,
    Rack = 3,
    Circulate = 4,
    SousVideWithoutKeg = 5,
    SousVideWithKeg = 6,
    PicoStillManual = 7,
    Test1 = 8,
    Test2 = 9,
    Test3 = 10,
    BrewTest = 11,
    Clean = 12,
    PicoPaleDemo = 13,
    ValveTest = 14,
    PumpsTest = 15,
    AlarmTest = 16,
    ChillTest = 17,
    KegPumpTest = 18,
    WetValveTest = 19,
    CoffeeColdBrew = 20,
    CoffeeHybrid = 21,
    CoffeeHot = 22,
    CoffeeCustom = 23,
    Buffered = 24,
    WhirlpoolTest = 25,
    PicoStillBoilPoint = 26,
    Chill = 27,
    ManualBrew = 28,
};
constexpr std::size_t ZProgramIdCount = 29;
const char* to_string(ZProgramId value) noexcept;

// DROM 0x3f448324; observed bound in 0x400d0518.
enum class RuntimeCommandId : std::uint32_t {
    None = 0,
    Start = 1,
    Stop = 2,
    Pause = 3,
    Resume = 4,
    Skip = 5,
    Drain = 6,
    ShowSetupInstructions = 7,
    RunPreBrewTests = 8,
    SetTemperature = 9,
    SetDuration = 10,
    SetLocation = 11,
    StartManualStep = 12,
};
constexpr std::size_t RuntimeCommandIdCount = 13;
const char* to_string(RuntimeCommandId value) noexcept;

// DROM 0x3f448310; observed bound in 0x400d05f4.
enum class RuntimeStateId : std::uint32_t {
    Off = 0,
    Booting = 1,
    Idle = 2,
    Running = 3,
    Standby = 4,
};
constexpr std::size_t RuntimeStateIdCount = 5;
const char* to_string(RuntimeStateId value) noexcept;

// DROM 0x3f4482fc; observed bound in 0x400d05f4.
enum class RuntimeNotificationType : std::uint32_t {
    None = 0,
    T1Dropped = 1,
};
constexpr std::size_t RuntimeNotificationTypeCount = 2;
const char* to_string(RuntimeNotificationType value) noexcept;

// DROM 0x3f448304; observed bound in 0x400d05f4.
enum class RuntimeEventType : std::uint32_t {
    Transition = 0,
    Update = 1,
    Notification = 2,
};
constexpr std::size_t RuntimeEventTypeCount = 3;
const char* to_string(RuntimeEventType value) noexcept;

// DROM 0x3f448198; observed bound in 0x400d06b4.
enum class CoreCommandId : std::uint32_t {
    None = 0,
    PowerButton = 1,
    CoffeeProgramLoad = 2,
    MyRecipeListGet = 3,
    ZKitRecipeListGet = 4,
    ZKitSmartListGet = 5,
    ZymaticRecipeLoad = 6,
    ZymaticRecipeRefLoad = 7,
    BrewPenRecipeLoad = 8,
    PicoPakRecipeLoad = 9,
    PicoPakRecipeRefLoad = 10,
    PicoPakShippedListGet = 11,
    PicoPaleDemo = 12,
    StartProgram = 13,
    StartLoadedProgram = 14,
    StartSessionOffline = 15,
    StartSessionOnline = 16,
    PicoStillSelected = 17,
    PicoStillCleanedAck = 18,
    PicoStillFanOn = 19,
    PicoStillFanOff = 20,
    PicoStillScan = 21,
    PicoStillReconnect = 22,
    T1MonitorActivate = 23,
    T1MonitorCancel = 24,
    RuntimeStart = 25,
    Stop = 26,
    Pause = 27,
    Resume = 28,
    Skip = 29,
    Drain = 30,
    ShowSetupInstructions = 31,
    RunPreBrewTests = 32,
    SetTemperature = 33,
    SetDuration = 34,
    SetLocation = 35,
    EepromReset = 36,
    FactoryReset = 37,
    RefreshRegistrationCode = 38,
    DlAndRunProgram = 39,
    EthernetConnect = 40,
    WiFiScan = 41,
    WiFiConnect = 42,
    BoilerDetect = 43,
    SetBoilerTypeBig = 44,
    SetBoilerTypeSmall = 45,
    AzureSessionLoad = 46,
    AzureSessionDiscard = 47,
    AzureWindowTimeout = 48,
    StandBy = 49,
    ResumeSession = 50,
    ResumeLoadedProgram = 51,
    UseZOffline = 52,
    UseZOnline = 53,
    StartStep = 54,
};
constexpr std::size_t CoreCommandIdCount = 55;
const char* to_string(CoreCommandId value) noexcept;

// DROM 0x3f448150; observed bound in 0x400d06b4.
enum class CoreEventType : std::uint32_t {
    StateTransition = 0,
    RegistrationNewToken = 1,
    RegistrationTokenExpired = 2,
    Loading = 3,
    LoadingError = 4,
    SessionCreationError = 5,
    NetworkMissingError = 6,
    ProgramLoaded = 7,
    RecipeRefListLoaded = 8,
    BoilerDetectResult = 9,
    RinseNeeded = 10,
    CleanNeeded = 11,
    PowerButton = 12,
    CoreError = 13,
    ResumedProgramLoaded = 14,
};
constexpr std::size_t CoreEventTypeCount = 15;
const char* to_string(CoreEventType value) noexcept;

// DROM 0x3f4484a0; observed bound in 0x400d06b4.
enum class BoilerType : std::uint32_t {
    Big = 0,
    Small = 1,
    Unknown = 2,
};
constexpr std::size_t BoilerTypeCount = 3;
const char* to_string(BoilerType value) noexcept;

// DROM 0x3f44847c; observed bound in 0x400d06b4.
enum class HexDetectError : std::uint32_t {
    None = 0,
    AmbientAboveMax = 1,
    WortAboveMax = 2,
    DrainAboveMax = 3,
    AmbientWortDelta = 4,
    WortDrainDelta = 5,
    DrainAmbientDelta = 6,
    BothHexInvalid = 7,
    BothHexValid = 8,
};
constexpr std::size_t HexDetectErrorCount = 9;
const char* to_string(HexDetectError value) noexcept;

// DROM 0x3f448140; observed bound in 0x400d076c.
enum class AzureEventType : std::uint32_t {
    None = 0,
    TimeoutStartProgram = 1,
    DisplayStartProgram = 2,
    ExitStartProgram = 3,
};
constexpr std::size_t AzureEventTypeCount = 4;
const char* to_string(AzureEventType value) noexcept;

// DROM 0x3f448048; observed bound in 0x400d07a0.
enum class StillCommandId : std::uint32_t {
    None = 0,
    Scan = 1,
    Connect = 2,
    Start = 3,
    Stop = 4,
    CleanedAck = 5,
    FanOn = 6,
    FanOff = 7,
    Reconnect = 8,
};
constexpr std::size_t StillCommandIdCount = 9;
const char* to_string(StillCommandId value) noexcept;

// DROM 0x3f448040; observed bound in 0x400d07f4.
enum class StillEventId : std::uint32_t {
    None = 0,
    StateTransition = 1,
};
constexpr std::size_t StillEventIdCount = 2;
const char* to_string(StillEventId value) noexcept;

// DROM 0x3f4480bc; observed bound in 0x400d07f4.
enum class PicoStillStateId : std::uint32_t {
    Unknown = 0,
    Idle = 1,
    Scanning = 2,
    SelectingStill = 3,
    ConnectingToHomeAP1 = 4,
    Authorizing = 5,
    PendingCleanedAck = 6,
    SendingCleanedAck = 7,
    ConnectingToStillAP = 8,
    PingingOnStillAP = 9,
    GettingIp = 10,
    Configuring = 11,
    ConnectingStillToHomeAP = 12,
    ConnectingToHomeAP2 = 13,
    PingingOnHomeAP = 14,
    PendingStart = 15,
    Starting = 16,
    Running = 17,
    Stopping = 18,
    Error = 19,
};
constexpr std::size_t PicoStillStateIdCount = 20;
const char* to_string(PicoStillStateId value) noexcept;

// DROM 0x3f4275f4; observed bound in 0x400d0834.
enum class UiCommand : std::uint32_t {
    None = 0,
    Back = 1,
    BackFirst = 2,
    Exit = 3,
    ZKitRecipeListGet = 4,
    MyRecipeListGet = 5,
    ZKitRecipeListGetSearch = 6,
    PacksMenu = 7,
    ZymaticRecipeLoad = 8,
    BrewPenRecipeLoad = 9,
    PicoPakRecipeLoad = 10,
    PicoPakRecipeLoadTweaked = 11,
    PicoPakRecipeTweak = 12,
    PicoPakRecipeTweakAbv = 13,
    PicoPakRecipeTweakIbu = 14,
    PicoPakShippedListGet = 15,
    PicoPakRecipeInput = 16,
    ZymaticRecipeInput = 17,
    PicoPaksMenu = 18,
    ZKitsSmartListMenu = 19,
    PicoStillMenu = 20,
    PicoStillSelected = 21,
    PicoStillCleanedAck = 22,
    PicoStillFanOn = 23,
    PicoStillFanOff = 24,
    PicoStillScan = 25,
    PicoStillReconnect = 26,
    T1MonitorMenu = 27,
    T1MonitorActivate = 28,
    T1MonitorCancel = 29,
    SousVideMenu = 30,
    CoffeeMenu = 31,
    CoffeeColdBrewTweak = 32,
    CoffeeHotTweak = 33,
    CoffeeHybridTweak = 34,
    CoffeeCustomTweak = 35,
    CoffeeBatchSizeInput = 36,
    CoffeeIntensityInput = 37,
    CoffeeTemperatureInput = 38,
    CoffeeDurationInput = 39,
    CoffeeProgramLoad = 40,
    Utilities = 41,
    Settings = 42,
    Tests = 43,
    DevTests = 44,
    About = 45,
    StartLoadedProgram = 46,
    StartSessionOnline = 47,
    StartSessionOffline = 48,
    RuntimeStart = 49,
    ConfirmStop = 50,
    ConfirmFinish = 51,
    Stop = 52,
    StopAsCancel = 53,
    StopAsFinish = 54,
    Pause = 55,
    Resume = 56,
    ResumeAsContinue = 57,
    ResumeAsDone = 58,
    ResumeAsStart = 59,
    ResumeAsSkip = 60,
    Skip = 61,
    Drain = 62,
    ShowSetupInstructions = 63,
    RunPreBrewTests = 64,
    SetZeroDelay = 65,
    GetDelay = 66,
    SilenceAlarm = 67,
    AdjustTemp = 68,
    AdjustTime = 69,
    AdjustLocation = 70,
    StartRinse = 71,
    StartClean = 72,
    StartDrain = 73,
    StartRack = 74,
    StartCirculate = 75,
    StartChill = 76,
    StartSousVideWithoutKeg = 77,
    StartSousVideWithKeg = 78,
    StartPicoStillBoilPoint = 79,
    StartPicoStillManual = 80,
    StartPicoPaleDemo = 81,
    StartManualBrew = 82,
    ShowRuntimeMenu = 83,
    RefreshRegistrationCode = 84,
    DlAndRunProgram = 85,
    Level = 86,
    GroupSessionExit = 87,
    StandBy = 88,
    StandByAsPowerOff = 89,
    ResumeInterruptedSession = 90,
    ResumeLoadedProgram = 91,
    UseZOfflineMenu = 92,
    UseZOffline = 93,
    UseZOnline = 94,
    StepTweak = 95,
    StepLocationInput = 96,
    StepTemperatureInput = 97,
    StepTimeInput = 98,
    StepDrainInput = 99,
    StepStart = 100,
    StepTerminate = 101,
    ConfigureNetwork = 102,
    ConfigureNetworkAsBack = 103,
    WiFiConnect = 104,
    WiFiReconnect = 105,
    WiFiScan = 106,
    WiFiDetails = 107,
    WiFiSsidGetKey = 108,
    ConfigureEthernet = 109,
    EthernetConnect = 110,
    ConfigureLanguage = 111,
    SetEnglish = 112,
    SetFrench = 113,
    SetSpanish = 114,
    ConfigureUnits = 115,
    UnitsSetImperial = 116,
    UnitsSetMetric = 117,
    ConfigureSlideshowDetail = 118,
    SetDetailedSlideshow = 119,
    SetBasicSlideshow = 120,
    ConfigureSlideshowCycle = 121,
    SetAutoCycle = 122,
    UnsetAutoCycle = 123,
    SetAutoCycleAsTurnOn = 124,
    UnsetAutoCycleAsTurnOff = 125,
    ConfigurePicoStill = 126,
    SetStillLoggingEnabled = 127,
    SetStillLoggingDisabled = 128,
    ConfigureAzure = 129,
    SetAzureEnabled = 130,
    SetAzureDisabled = 131,
    ShowEeprom = 132,
    EepromReset = 133,
    EepromResetMenu = 134,
    FactoryResetMenu = 135,
    FactoryReset = 136,
    ConfigureBoiler = 137,
    BoilerDetect = 138,
    SetBoilerTypeBig = 139,
    SetBoilerTypeSmall = 140,
    ShowHardwareState = 141,
    StartValveTest = 142,
    StartPumpsTest = 143,
    StartAlarmTest = 144,
    StartWhirlpoolTest = 145,
    StartChillTest = 146,
    StartDrainTest = 147,
    StartKegPumpTest = 148,
    StartWetValveTest = 149,
    InstructionsTest = 150,
    TitlesTest = 151,
    PauseTests = 152,
    PauseTestMenuMessage = 153,
    PauseTestMenu = 154,
    PauseTestMessage = 155,
    PauseTestJustReason = 156,
    SlideshowBrewTest = 157,
    SlideshowSousVideTest = 158,
    TextInputTest = 159,
    NumInputTest = 160,
    RfidInputTest = 161,
    TempInputTest = 162,
    TimeInputTest = 163,
    ABVInputTest = 164,
    LocationInputTest = 165,
    InputCoffeeBatchSizeTest = 166,
    InputCoffeeIntensityTest = 167,
    InputCoffeeTempTest = 168,
    ScrollTest = 169,
    MenuTest = 170,
    MenuLargeTest = 171,
    ProgressBarTest = 172,
    AsciiFontTest = 173,
    DisplayTest = 174,
    ParagraphTest = 175,
};
constexpr std::size_t UiCommandCount = 176;
const char* to_string(UiCommand value) noexcept;

// DROM 0x3f448130; observed bound in 0x400d10a0.
enum class RecipeKind : std::uint32_t {
    Zymatic = 0,
    PicoPak = 1,
    BrewPen = 2,
    Unknown = 3,
};
constexpr std::size_t RecipeKindCount = 4;
const char* to_string(RecipeKind value) noexcept;

// DROM 0x3f448450; observed bound in 0x400d5bc0.
enum class WiFiStateId : std::uint32_t {
    Booting = 0,
    Idle = 1,
    Scanning = 2,
    ScanCompleted = 3,
    AccessPoint = 4,
    AccessPointDisconnecting = 5,
    PoweringUp = 6,
    Connecting = 7,
    Checking = 8,
    Connected = 9,
    Disconnecting = 10,
};
constexpr std::size_t WiFiStateIdCount = 11;
const char* to_string(WiFiStateId value) noexcept;

// DROM 0x3f448030; observed bound in 0x400d6108.
enum class MspStateId : std::uint32_t {
    Unknown = 0,
    BootRunning = 1,
    AppRunning = 2,
    BootloaderError = 3,
};
constexpr std::size_t MspStateIdCount = 4;
const char* to_string(MspStateId value) noexcept;

// DROM 0x3f4484d0; observed bound in 0x400d6558.
enum class LanguageType : std::uint32_t {
    English = 0,
    French = 1,
    Spanish = 2,
};
constexpr std::size_t LanguageTypeCount = 3;
const char* to_string(LanguageType value) noexcept;

// DROM 0x3f4484c4; observed bound in 0x400d6558.
enum class SessionMode : std::uint32_t {
    Unknown = 0,
    Online = 1,
    Offline = 2,
};
constexpr std::size_t SessionModeCount = 3;
const char* to_string(SessionMode value) noexcept;

// DROM 0x3f4484b8; observed bound in 0x400d6558.
enum class BoolOption : std::uint32_t {
    Unknown = 0,
    False = 1,
    True = 2,
};
constexpr std::size_t BoolOptionCount = 3;
const char* to_string(BoolOption value) noexcept;

// DROM 0x3f428890; observed bound in 0x400d6ac0.
enum class TempSensorType : std::uint32_t {
    SENSTECH_10_366 = 0, // Identifier normalized from the display label.
    SENSTECH_10_364_5 = 1, // Identifier normalized from the display label.
    MURATA_NCU18 = 2,
    AET_14015 = 3,
};
constexpr std::size_t TempSensorTypeCount = 4;
const char* to_string(TempSensorType value) noexcept;

// DROM 0x3f42a138; observed bound in 0x400d7264.
enum class RuntimeStillStateId : std::uint32_t {
    None = 0,
    Idle = 1,
    Running = 2,
    MeasuringHeatLoss = 3,
    FindingBoilPoint = 4,
    Stabilizing = 5,
    Distilling = 6,
    T2OverTemp = 7,
    ReleasePressure = 8,
};
constexpr std::size_t RuntimeStillStateIdCount = 9;
const char* to_string(RuntimeStillStateId value) noexcept;

// DROM 0x3f44843c; observed bound in 0x400d9f90.
enum class WiFiLevel : std::uint32_t {
    Bar0 = 0,
    Bar1 = 1,
    Bar2 = 2,
    Bar3 = 3,
    Bar4 = 4,
};
constexpr std::size_t WiFiLevelCount = 5;
const char* to_string(WiFiLevel value) noexcept;

// DROM 0x3f44810c; observed bound in 0x400da808.
enum class ZBackendError : std::uint32_t {
    None = 0,
    BoilerSNInv = 1,
    BTypeMismatch = 2,
    BUpdateError = 3,
};
constexpr std::size_t ZBackendErrorCount = 4;
const char* to_string(ZBackendError value) noexcept;

// DROM 0x3f4482b8; observed bound in 0x400da808.
enum class SessionType : std::uint32_t {
    Rinse = 0,
    Clean = 1,
    Drain = 2,
    Rack = 3,
    Circulate = 4,
    Sousvide = 5,
    BrewZymatic = 6,
    BrewPicoPak = 7,
    BrewZRecipe = 8,
    BrewDemo = 9,
    Test = 10,
    PicoStill = 11,
    Coffee = 12,
    Chill = 13,
    ManualBrew = 14,
    LocalBrew = 15,
    Unknown = 16,
};
constexpr std::size_t SessionTypeCount = 17;
const char* to_string(SessionType value) noexcept;

// DROM 0x3f4288d4; observed bound in 0x400db324.
enum class HttpClientErrorId : std::uint32_t {
    None = 0,
    HttpStatusError = 1,
    BrokenInvariant = 2,
    ConnectError = 3,
    SendError = 4,
    StartFailed = 5,
    InvalidState = 6,
    InvalidArg = 7,
    InvalidRequest = 8,
    SystemClientError = 9,
    WaitingReplyTimeout = 10,
    ReadError = 11,
    ResponseTooBig = 12,
    PrematureClose = 13,
    ReadingHeadersTimeout = 14,
    ApplicationError = 15,
};
constexpr std::size_t HttpClientErrorIdCount = 16;
const char* to_string(HttpClientErrorId value) noexcept;

// DROM 0x3f42a128; observed bound in 0x400dd818.
enum class TempSensorId : std::uint32_t {
    Hex = 0,
    Wort = 1,
    Drain = 2,
    Ambient = 3,
};
constexpr std::size_t TempSensorIdCount = 4;
const char* to_string(TempSensorId value) noexcept;

// DROM 0x3f42a258; observed bound in 0x400de648.
enum class ScreenId : std::uint32_t {
    None = 0,
    Registration1 = 1,
    Brew1 = 2,
    Brew2 = 3,
    Brew3 = 4,
    Brew4 = 5,
    Brew5 = 6,
    Brew6 = 7,
    Brew7 = 8,
    Brew8 = 9,
    SetupInstructionsOffer = 10,
    PreBrewTestsOffer = 11,
    PreBrewTestsCirculation = 12,
    PreBrewTestsRunning = 13,
    BrewCompleted = 14,
    GenericSetup = 15,
    Pak1 = 16,
    Pak2 = 17,
    Pak3 = 18,
    Pak4 = 19,
    Pak5 = 20,
    Pak6 = 21,
    Pak7 = 22,
    Pak8 = 23,
    PakBrewCompleted = 24,
    Rinse1 = 25,
    Rinse2 = 26,
    Rinse3 = 27,
    Rinse4 = 28,
    Rinse5 = 29,
    Rinse6 = 30,
    Clean1 = 31,
    Clean2 = 32,
    Clean3 = 33,
    Clean4 = 34,
    Clean5 = 35,
    Clean6 = 36,
    Clean7 = 37,
    Clean8 = 38,
    Clean9 = 39,
    Clean10 = 40,
    Drain1 = 41,
    Circulate1 = 42,
    Chill1 = 43,
    ChillCompleted = 44,
    Rack1 = 45,
    Rack2 = 46,
    Rack3 = 47,
    Rack4 = 48,
    Rack5 = 49,
    Rack6 = 50,
    Rack7 = 51,
    Rack8 = 52,
    Rack9 = 53,
    ExitConfirm = 54,
    FinishConfirm = 55,
    ErrorLoading = 56,
    ErrorNetwork = 57,
    ErrorSession = 58,
    BoilerConfig = 59,
    SousVide1 = 60,
    SousVide2 = 61,
    SousVideK1 = 62,
    SousVideK2 = 63,
    SousVideK3 = 64,
    SousVideK4 = 65,
    PicoStill1 = 66,
    PicoStill2 = 67,
    PicoStill3 = 68,
    PicoStill4 = 69,
    PicoStill5 = 70,
    PicoStill6 = 71,
    PicoStill7 = 72,
    PicoStillWaitSealed = 73,
    PicoStillBoilPointFound = 74,
    PicoStillNotFound = 75,
    PicoStillFound = 76,
    PicoStillChooseProgram = 77,
    PicoStillBoilPointWarning = 78,
    PicoStillT1MonitorActivate = 79,
    PicoStillT1MonitorCancel = 80,
    AskSessionLogging = 81,
    Coffee1 = 82,
    Coffee2 = 83,
    Coffee3 = 84,
    Coffee4 = 85,
    Coffee5 = 86,
    Coffee6 = 87,
    Coffee7 = 88,
    CoffeeCompleted1 = 89,
    CoffeeCompleted2 = 90,
    RinseNeeded = 91,
    CleanNeeded = 92,
    FanOffAfterCooling = 93,
    KegTest1 = 94,
    TestLong = 95,
    Test2_1 = 96,
    Test2_2 = 97,
    SessionInterrupted = 98,
    AltitudeAdjustedTempWarning = 99,
    UseZOfflineConfirm = 100,
    StepCompleted = 101,
    FactoryResetWarning = 102,
};
constexpr std::size_t ScreenIdCount = 103;
const char* to_string(ScreenId value) noexcept;

// DROM 0x3f42a15c; observed bound in 0x400de850.
enum class PauseReason : std::uint32_t {
    None = 0,
    Program = 1,
    User = 2,
    StepFilter = 3,
    TooHot = 4,
    TooCold = 5,
    MaxHexWortDelta = 6,
    MaxWortDrainDelta = 7,
    MaxHexRise = 8,
    TempInput = 9,
    TimeInputFine = 10,
    TimeInputCoarse = 11,
    PicoStillError = 12,
    StillPumpOff = 13,
    SystemError = 14,
    MspError = 15,
    CannotRun = 16,
    StillT2Overtemp = 17,
};
constexpr std::size_t PauseReasonCount = 18;
const char* to_string(PauseReason value) noexcept;

// DROM 0x3f4483e8; observed bound in 0x400dee30.
enum class Unit : std::uint32_t {
    Liter = 0,
    Gallon = 1,
    Gram = 2,
    Ounce = 3,
    Celsius = 4,
    Fahrenheit = 5,
    Second = 6,
    Intensity = 7,
    None = 8,
};
constexpr std::size_t UnitCount = 9;
const char* to_string(Unit value) noexcept;

// DROM 0x3f44811c; observed bound in 0x400ed990.
enum class AzureState : std::uint32_t {
    Disabled = 0,
    Connecting = 1,
    Listening = 2,
    SessionLoaded = 3,
    SessionRunning = 4,
};
constexpr std::size_t AzureStateCount = 5;
const char* to_string(AzureState value) noexcept;

// DROM 0x3f4484ac; observed bound in 0x400ee8dc.
enum class NetworkType : std::uint32_t {
    None = 0,
    Ethernet = 1,
    WiFi = 2,
};
constexpr std::size_t NetworkTypeCount = 3;
const char* to_string(NetworkType value) noexcept;

// DROM 0x3f448274; observed bound in 0x400efc24.
enum class CoreStateId : std::uint32_t {
    Off = 0,
    Booting = 1,
    ConnectingWiFi = 2,
    ReConnectingWiFi = 3,
    ConnectingEthernet = 4,
    NeedsNetwork = 5,
    SyncPending = 6,
    Syncing = 7,
    Updating = 8,
    Registering = 9,
    DownloadingProgram = 10,
    StandBy = 11,
    Idle = 12,
    Starting = 13,
    Running = 14,
    Stopping = 15,
    Error = 16,
};
constexpr std::size_t CoreStateIdCount = 17;
const char* to_string(CoreStateId value) noexcept;

// DROM 0x3f44840c; observed bound in 0x400f2738.
enum class WifiError : std::uint32_t {
    None = 0,
    WifiSsidNotFound = 1,
    WifiConnectFailed = 2,
    WifiConnectingTimeout = 3,
    WifiNoInternet = 4,
};
constexpr std::size_t WifiErrorCount = 5;
const char* to_string(WifiError value) noexcept;

// DROM 0x3f425538; observed bound in 0x400f947c.
enum class Command : std::uint32_t {
    Empty = 0, // Identifier normalized from the display label.
    configure = 1,
    stillip = 2,
    connecttolan = 3,
    start = 4,
    end = 5,
    fanon = 6,
    fanoff = 7,
    ping = 8,
    data = 9,
    version = 10,
    sensors = 11,
};
constexpr std::size_t CommandCount = 12;
const char* to_string(Command value) noexcept;

// DROM 0x3f4269a0; observed bound in 0x40102d30.
enum class Layout : std::uint32_t {
    RuntimeState = 0,
    RuntimeTransient = 1,
    Notification = 2,
    Associate = 3,
    Slideshow = 4,
    StillTransient = 5,
    VScroll = 6,
    TempInput = 7,
    TextInput = 8,
    TimeInput = 9,
    ABVInput = 10,
    LocationInput = 11,
    QuantityInput = 12,
    Waiting = 13,
    Level = 14,
    Progress = 15,
    Pause = 16,
    Menu = 17,
    MenuWithImage = 18,
    MenuWithText = 19,
    MenuWithVScroll = 20,
    Text = 21,
    None = 22,
};
constexpr std::size_t LayoutCount = 23;
const char* to_string(Layout value) noexcept;

// DROM 0x3f44818c; observed bound in 0x401095b4.
enum class CoreErrorId : std::uint32_t {
    None = 0,
    NoIpAddress = 1,
    NoInternet = 2,
};
constexpr std::size_t CoreErrorIdCount = 3;
const char* to_string(CoreErrorId value) noexcept;

// DROM 0x3f425520; observed bound in 0x4011b0e4.
enum class HttpMethod : std::uint32_t {
    Unknown = 0,
    GET = 1,
    POST = 2,
    PUT = 3,
    DELETE = 4,
    UPDATE = 5,
};
constexpr std::size_t HttpMethodCount = 6;
const char* to_string(HttpMethod value) noexcept;

// DROM 0x3f4288a0; observed bound in 0x4011cadc.
enum class State : std::uint32_t {
    Idle = 0,
    Creating = 1,
    Waiting = 2,
    Logging = 3,
    Ending = 4,
};
constexpr std::size_t StateCount = 5;
const char* to_string(State value) noexcept;

// DROM 0x3f42a3f4; observed bound in 0x400de7b0.
enum class OpCode : std::uint32_t {
    Pause = 0,
    StepName = 1,
    ReachTarget = 2,
    HoldFor = 3,
    CircFor = 4,
    DrainFor = 5,
    MoveValve = 6,
    SetPumps = 7,
    NoTarget = 8,
    Noop = 9,
    FlagSet = 10,
    FlagClear = 11,
    SubStepName = 12,
    WaitFor = 13,
    WaitUser = 14,
    WaitTempA = 15,
    WaitTempB = 16,
    WaitTempC = 17,
    WaitTempE = 18,
    GetDelay = 19,
    WaitDelay = 20,
    GetStepParams = 21,
    GetTemp = 22,
    GetTime = 23,
    WaitTime = 24,
    SetTarget = 25,
    SetTime = 26,
    MspValve = 27,
    MspPumps = 28,
    ValveMode = 29,
    SetScreen = 30,
    ShowScreen = 31,
    Heater = 32,
    StartPid = 33,
    StopPid = 34,
    SetITMin = 35,
    SetITMax = 36,
    RequireFilter = 37,
    StopPidKD = 38,
    PrepareBrew = 39,
    CompleteBrew = 40,
    StillScan = 41,
    StillStart = 42,
    StSetTargetHL = 43,
    StFindHeatLoss = 44,
    StFindBoilPoint = 45,
    StStabilize = 46,
    StDistille = 47,
    WaitStillReady = 48,
    WaitSealed = 49,
    WaitVacuum = 50,
    WaitT3Stable = 51,
    WaitT1Rise = 52,
    WaitT3Drop = 53,
    WaitT2Safety = 54,
    WaitPumpOk = 55,
    RinseCycle = 56,
    Setup = 57,
};
constexpr std::size_t OpCodeCount = 58;
const char* to_string(OpCode value) noexcept;

}} // namespace zseries::oem
