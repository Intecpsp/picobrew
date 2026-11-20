// Recovered original Z119 programs, not the lost vendor source.
// Source addresses and byte hashes: analysis/programs.json.
// No original image, absolute addresses, or decompiler text used at runtime.
#include "zseries/program_repository.hpp"

namespace zseries { namespace oem {
namespace {
// RINSE, original record 0x3f428b7c.
const Instruction RinseCode[] = {
    {OpCode::StepName, 0x8009u}, // 0x3f429de8
    {OpCode::StopPid, 0x0u}, // 0x3f429df0
    {OpCode::ShowScreen, 0x19u}, // 0x3f429df8
    {OpCode::RequireFilter, 0x1u}, // 0x3f429e00
    {OpCode::ShowScreen, 0x1au}, // 0x3f429e08
    {OpCode::ShowScreen, 0x1bu}, // 0x3f429e10
    {OpCode::MspPumps, 0x40u}, // 0x3f429e18
    {OpCode::MspValve, 0x2u}, // 0x3f429e20
    {OpCode::MspPumps, 0x70u}, // 0x3f429e28
    {OpCode::WaitFor, 0x3cu}, // 0x3f429e30
    {OpCode::MspPumps, 0x40u}, // 0x3f429e38
    {OpCode::MspValve, 0x2u}, // 0x3f429e40
    {OpCode::MspPumps, 0x50u}, // 0x3f429e48
    {OpCode::WaitFor, 0xau}, // 0x3f429e50
    {OpCode::MspPumps, 0x40u}, // 0x3f429e58
    {OpCode::MspValve, 0x4u}, // 0x3f429e60
    {OpCode::MspPumps, 0x50u}, // 0x3f429e68
    {OpCode::WaitFor, 0xau}, // 0x3f429e70
    {OpCode::MspPumps, 0x40u}, // 0x3f429e78
    {OpCode::MspValve, 0x5u}, // 0x3f429e80
    {OpCode::MspPumps, 0x50u}, // 0x3f429e88
    {OpCode::WaitFor, 0xau}, // 0x3f429e90
    {OpCode::MspPumps, 0x40u}, // 0x3f429e98
    {OpCode::MspValve, 0x6u}, // 0x3f429ea0
    {OpCode::MspPumps, 0x50u}, // 0x3f429ea8
    {OpCode::WaitFor, 0xau}, // 0x3f429eb0
    {OpCode::MspPumps, 0x40u}, // 0x3f429eb8
    {OpCode::MspValve, 0x7u}, // 0x3f429ec0
    {OpCode::MspPumps, 0x50u}, // 0x3f429ec8
    {OpCode::WaitFor, 0xau}, // 0x3f429ed0
    {OpCode::MspPumps, 0x40u}, // 0x3f429ed8
    {OpCode::MspValve, 0x8u}, // 0x3f429ee0
    {OpCode::MspPumps, 0x50u}, // 0x3f429ee8
    {OpCode::WaitFor, 0xau}, // 0x3f429ef0
    {OpCode::MspPumps, 0x40u}, // 0x3f429ef8
    {OpCode::MspValve, 0x9u}, // 0x3f429f00
    {OpCode::MspPumps, 0x50u}, // 0x3f429f08
    {OpCode::WaitFor, 0xau}, // 0x3f429f10
    {OpCode::MspPumps, 0x40u}, // 0x3f429f18
    {OpCode::MspValve, 0x2u}, // 0x3f429f20
    {OpCode::MspPumps, 0x50u}, // 0x3f429f28
    {OpCode::WaitFor, 0xau}, // 0x3f429f30
    {OpCode::MspPumps, 0x40u}, // 0x3f429f38
    {OpCode::MspValve, 0x4u}, // 0x3f429f40
    {OpCode::MspPumps, 0x50u}, // 0x3f429f48
    {OpCode::WaitFor, 0xau}, // 0x3f429f50
    {OpCode::MspPumps, 0x40u}, // 0x3f429f58
    {OpCode::MspValve, 0x5u}, // 0x3f429f60
    {OpCode::MspPumps, 0x50u}, // 0x3f429f68
    {OpCode::WaitFor, 0xau}, // 0x3f429f70
    {OpCode::MspPumps, 0x40u}, // 0x3f429f78
    {OpCode::MspValve, 0x6u}, // 0x3f429f80
    {OpCode::MspPumps, 0x50u}, // 0x3f429f88
    {OpCode::WaitFor, 0xau}, // 0x3f429f90
    {OpCode::MspPumps, 0x40u}, // 0x3f429f98
    {OpCode::MspValve, 0x7u}, // 0x3f429fa0
    {OpCode::MspPumps, 0x50u}, // 0x3f429fa8
    {OpCode::WaitFor, 0xau}, // 0x3f429fb0
    {OpCode::MspPumps, 0x40u}, // 0x3f429fb8
    {OpCode::MspValve, 0x8u}, // 0x3f429fc0
    {OpCode::MspPumps, 0x50u}, // 0x3f429fc8
    {OpCode::WaitFor, 0xau}, // 0x3f429fd0
    {OpCode::MspPumps, 0x40u}, // 0x3f429fd8
    {OpCode::MspValve, 0x9u}, // 0x3f429fe0
    {OpCode::MspPumps, 0x50u}, // 0x3f429fe8
    {OpCode::WaitFor, 0xau}, // 0x3f429ff0
    {OpCode::MspPumps, 0x40u}, // 0x3f429ff8
    {OpCode::MspValve, 0x2u}, // 0x3f42a000
    {OpCode::MspPumps, 0x50u}, // 0x3f42a008
    {OpCode::WaitFor, 0xau}, // 0x3f42a010
    {OpCode::MspPumps, 0x40u}, // 0x3f42a018
    {OpCode::MspValve, 0x4u}, // 0x3f42a020
    {OpCode::MspPumps, 0x50u}, // 0x3f42a028
    {OpCode::WaitFor, 0xau}, // 0x3f42a030
    {OpCode::MspPumps, 0x40u}, // 0x3f42a038
    {OpCode::MspValve, 0x5u}, // 0x3f42a040
    {OpCode::MspPumps, 0x50u}, // 0x3f42a048
    {OpCode::WaitFor, 0xau}, // 0x3f42a050
    {OpCode::MspPumps, 0x40u}, // 0x3f42a058
    {OpCode::MspValve, 0x6u}, // 0x3f42a060
    {OpCode::MspPumps, 0x50u}, // 0x3f42a068
    {OpCode::WaitFor, 0xau}, // 0x3f42a070
    {OpCode::MspPumps, 0x40u}, // 0x3f42a078
    {OpCode::MspValve, 0x7u}, // 0x3f42a080
    {OpCode::MspPumps, 0x50u}, // 0x3f42a088
    {OpCode::WaitFor, 0xau}, // 0x3f42a090
    {OpCode::MspPumps, 0x40u}, // 0x3f42a098
    {OpCode::MspValve, 0x8u}, // 0x3f42a0a0
    {OpCode::MspPumps, 0x50u}, // 0x3f42a0a8
    {OpCode::WaitFor, 0xau}, // 0x3f42a0b0
    {OpCode::MspPumps, 0x40u}, // 0x3f42a0b8
    {OpCode::MspValve, 0x9u}, // 0x3f42a0c0
    {OpCode::MspPumps, 0x50u}, // 0x3f42a0c8
    {OpCode::WaitFor, 0xau}, // 0x3f42a0d0
    {OpCode::StopPidKD, 0x0u}, // 0x3f42a0d8
    {OpCode::RequireFilter, 0x0u}, // 0x3f42a0e0
    {OpCode::StepName, 0x8017u}, // 0x3f42a0e8
    {OpCode::ShowScreen, 0x1du}, // 0x3f42a0f0
    {OpCode::ShowScreen, 0x1eu}, // 0x3f42a0f8
};

// DRAIN, original record 0x3f428b60.
const Instruction DrainCode[] = {
    {OpCode::RequireFilter, 0x1u}, // 0x3f4296d0
    {OpCode::StepName, 0x8002u}, // 0x3f4296d8
    {OpCode::MspValve, 0x3u}, // 0x3f4296e0
    {OpCode::StepName, 0x800eu}, // 0x3f4296e8
    {OpCode::SetPumps, 0x2u}, // 0x3f4296f0
    {OpCode::ShowScreen, 0x29u}, // 0x3f4296f8
};

// RACK, original record 0x3f428b44.
const Instruction RackCode[] = {
    {OpCode::StepName, 0x8002u}, // 0x3f429610
    {OpCode::ShowScreen, 0x2du}, // 0x3f429618
    {OpCode::MoveValve, 0x2u}, // 0x3f429620
    {OpCode::SetPumps, 0x2u}, // 0x3f429628
    {OpCode::ShowScreen, 0x2eu}, // 0x3f429630
    {OpCode::SetPumps, 0x0u}, // 0x3f429638
    {OpCode::ShowScreen, 0x2fu}, // 0x3f429640
    {OpCode::ShowScreen, 0x30u}, // 0x3f429648
    {OpCode::ShowScreen, 0x31u}, // 0x3f429650
    {OpCode::ShowScreen, 0x32u}, // 0x3f429658
    {OpCode::SetPumps, 0x2u}, // 0x3f429660
    {OpCode::ShowScreen, 0x33u}, // 0x3f429668
    {OpCode::SetPumps, 0x0u}, // 0x3f429670
    {OpCode::ShowScreen, 0x34u}, // 0x3f429678
    {OpCode::StepName, 0x800fu}, // 0x3f429680
    {OpCode::SetPumps, 0x2u}, // 0x3f429688
    {OpCode::ShowScreen, 0x35u}, // 0x3f429690
    {OpCode::SetPumps, 0x0u}, // 0x3f429698
};

// CIRCULATE, original record 0x3f428b28.
const Instruction CirculateCode[] = {
    {OpCode::RequireFilter, 0x1u}, // 0x3f4295e0
    {OpCode::StepName, 0x8002u}, // 0x3f4295e8
    {OpCode::MoveValve, 0x1u}, // 0x3f4295f0
    {OpCode::StepName, 0x8010u}, // 0x3f4295f8
    {OpCode::SetPumps, 0x3u}, // 0x3f429600
    {OpCode::ShowScreen, 0x2au}, // 0x3f429608
};

// SOUSVIDE, original record 0x3f428ad4.
const Instruction SousVideWithoutKegCode[] = {
    {OpCode::StepName, 0x8002u}, // 0x3f429390
    {OpCode::ShowScreen, 0x3cu}, // 0x3f429398
    {OpCode::ShowScreen, 0x3du}, // 0x3f4293a0
    {OpCode::RequireFilter, 0x1u}, // 0x3f4293a8
    {OpCode::SetTime, 0x1c20u}, // 0x3f4293b0
    {OpCode::SetITMin, 0x1400u}, // 0x3f4293b8
    {OpCode::SetITMax, 0x5a00u}, // 0x3f4293c0
    {OpCode::SetTarget, 0x3800u}, // 0x3f4293c8
    {OpCode::GetTemp, 0x0u}, // 0x3f4293d0
    {OpCode::GetTime, 0x0u}, // 0x3f4293d8
    {OpCode::StepName, 0x8002u}, // 0x3f4293e0
    {OpCode::MoveValve, 0x2u}, // 0x3f4293e8
    {OpCode::SetPumps, 0x1u}, // 0x3f4293f0
    {OpCode::StartPid, 0x0u}, // 0x3f4293f8
    {OpCode::StepName, 0x800au}, // 0x3f429400
    {OpCode::WaitTempA, 0x0u}, // 0x3f429408
    {OpCode::StepName, 0x800bu}, // 0x3f429410
    {OpCode::MoveValve, 0x2u}, // 0x3f429418
    {OpCode::WaitTime, 0x0u}, // 0x3f429420
    {OpCode::StopPidKD, 0x0u}, // 0x3f429428
    {OpCode::RequireFilter, 0x0u}, // 0x3f429430
};

// SOUSVIDE, original record 0x3f428ab8.
const Instruction SousVideWithKegCode[] = {
    {OpCode::StepName, 0x8002u}, // 0x3f429438
    {OpCode::ShowScreen, 0x3eu}, // 0x3f429440
    {OpCode::ShowScreen, 0x3fu}, // 0x3f429448
    {OpCode::ShowScreen, 0x40u}, // 0x3f429450
    {OpCode::RequireFilter, 0x1u}, // 0x3f429458
    {OpCode::SetTime, 0x1c20u}, // 0x3f429460
    {OpCode::SetITMin, 0x1400u}, // 0x3f429468
    {OpCode::SetITMax, 0x5a00u}, // 0x3f429470
    {OpCode::SetTarget, 0x3800u}, // 0x3f429478
    {OpCode::GetTemp, 0x0u}, // 0x3f429480
    {OpCode::GetTime, 0x0u}, // 0x3f429488
    {OpCode::StepName, 0x8002u}, // 0x3f429490
    {OpCode::MoveValve, 0x1u}, // 0x3f429498
    {OpCode::SetPumps, 0x3u}, // 0x3f4294a0
    {OpCode::StartPid, 0x0u}, // 0x3f4294a8
    {OpCode::StepName, 0x800au}, // 0x3f4294b0
    {OpCode::SubStepName, 0x8011u}, // 0x3f4294b8
    {OpCode::WaitTempA, 0x0u}, // 0x3f4294c0
    {OpCode::StepName, 0x800bu}, // 0x3f4294c8
    {OpCode::SubStepName, 0x800bu}, // 0x3f4294d0
    {OpCode::MoveValve, 0x2u}, // 0x3f4294d8
    {OpCode::WaitTime, 0x0u}, // 0x3f4294e0
    {OpCode::StopPidKD, 0x0u}, // 0x3f4294e8
    {OpCode::ShowScreen, 0x41u}, // 0x3f4294f0
    {OpCode::MspValve, 0x3u}, // 0x3f4294f8
    {OpCode::StepName, 0x800eu}, // 0x3f429500
    {OpCode::SetPumps, 0x2u}, // 0x3f429508
    {OpCode::ShowScreen, 0x29u}, // 0x3f429510
    {OpCode::RequireFilter, 0x0u}, // 0x3f429518
};

// PICOSTILL, original record 0x3f428a9c.
const Instruction PicoStillManualCode[] = {
    {OpCode::StepName, 0x8002u}, // 0x3f4292d0
    {OpCode::FlagSet, 0x20u}, // 0x3f4292d8
    {OpCode::ValveMode, 0x0u}, // 0x3f4292e0
    {OpCode::SetTarget, 0x3938u}, // 0x3f4292e8
    {OpCode::ShowScreen, 0x42u}, // 0x3f4292f0
    {OpCode::ShowScreen, 0x43u}, // 0x3f4292f8
    {OpCode::RequireFilter, 0x1u}, // 0x3f429300
    {OpCode::ShowScreen, 0x44u}, // 0x3f429308
    {OpCode::ShowScreen, 0x45u}, // 0x3f429310
    {OpCode::ShowScreen, 0x46u}, // 0x3f429318
    {OpCode::StillScan, 0x0u}, // 0x3f429320
    {OpCode::WaitStillReady, 0x0u}, // 0x3f429328
    {OpCode::GetTemp, 0x0u}, // 0x3f429330
    {OpCode::ShowScreen, 0x47u}, // 0x3f429338
    {OpCode::StillStart, 0x0u}, // 0x3f429340
    {OpCode::StepName, 0x8011u}, // 0x3f429348
    {OpCode::MoveValve, 0x5u}, // 0x3f429350
    {OpCode::SetPumps, 0x1u}, // 0x3f429358
    {OpCode::StartPid, 0x0u}, // 0x3f429360
    {OpCode::WaitTempA, 0x0u}, // 0x3f429368
    {OpCode::StepName, 0x8004u}, // 0x3f429370
    {OpCode::WaitFor, 0xd2f00u}, // 0x3f429378
    {OpCode::StopPidKD, 0x0u}, // 0x3f429380
    {OpCode::RequireFilter, 0x0u}, // 0x3f429388
};

// TEST1, original record 0x3f428a64.
const Instruction Test1Code[] = {
    {OpCode::StepName, 0x8005u}, // 0x3f428bf0
    {OpCode::ValveMode, 0x0u}, // 0x3f428bf8
    {OpCode::MoveValve, 0x1u}, // 0x3f428c00
    {OpCode::MoveValve, 0x2u}, // 0x3f428c08
    {OpCode::MoveValve, 0x3u}, // 0x3f428c10
    {OpCode::MoveValve, 0x4u}, // 0x3f428c18
    {OpCode::MoveValve, 0x5u}, // 0x3f428c20
    {OpCode::MoveValve, 0x6u}, // 0x3f428c28
    {OpCode::ValveMode, 0x1u}, // 0x3f428c30
    {OpCode::MoveValve, 0x1u}, // 0x3f428c38
    {OpCode::MoveValve, 0x2u}, // 0x3f428c40
    {OpCode::MoveValve, 0x3u}, // 0x3f428c48
    {OpCode::MoveValve, 0x4u}, // 0x3f428c50
    {OpCode::MoveValve, 0x5u}, // 0x3f428c58
    {OpCode::MoveValve, 0x6u}, // 0x3f428c60
};

// TEST2, original record 0x3f428a48.
const Instruction Test2Code[] = {
    {OpCode::StepName, 0x8005u}, // 0x3f428bb8
    {OpCode::GetTemp, 0x0u}, // 0x3f428bc0
    {OpCode::GetTime, 0x0u}, // 0x3f428bc8
    {OpCode::WaitTime, 0x0u}, // 0x3f428bd0
    {OpCode::Pause, 0x0u}, // 0x3f428bd8
    {OpCode::ShowScreen, 0x60u}, // 0x3f428be0
    {OpCode::ShowScreen, 0x61u}, // 0x3f428be8
};

// TEST3, original record 0x3f428a2c.
const Instruction Test3Code[] = {
    {OpCode::StepName, 0x8005u}, // 0x3f428b98
    {OpCode::MoveValve, 0x2u}, // 0x3f428ba0
    {OpCode::SetPumps, 0x1u}, // 0x3f428ba8
    {OpCode::WaitFor, 0x1eu}, // 0x3f428bb0
};

// BREWTEST, original record 0x3f428a10.
const Instruction BrewTestCode[] = {
    {OpCode::StepName, 0x8002u}, // 0x3f429b40
    {OpCode::ShowScreen, 0x2u}, // 0x3f429b48
    {OpCode::RequireFilter, 0x1u}, // 0x3f429b50
    {OpCode::ValveMode, 0x0u}, // 0x3f429b58
    {OpCode::MoveValve, 0x1u}, // 0x3f429b60
    {OpCode::ReachTarget, 0x4200u}, // 0x3f429b68
    {OpCode::MoveValve, 0x2u}, // 0x3f429b70
    {OpCode::ReachTarget, 0x4200u}, // 0x3f429b78
    {OpCode::HoldFor, 0x1518u}, // 0x3f429b80
    {OpCode::DrainFor, 0x1e0u}, // 0x3f429b88
    {OpCode::MoveValve, 0x1u}, // 0x3f429b90
    {OpCode::ReachTarget, 0x6100u}, // 0x3f429b98
    {OpCode::MoveValve, 0x3u}, // 0x3f429ba0
    {OpCode::ReachTarget, 0x6100u}, // 0x3f429ba8
    {OpCode::HoldFor, 0xbb8u}, // 0x3f429bb0
    {OpCode::MoveValve, 0x4u}, // 0x3f429bb8
    {OpCode::ReachTarget, 0x6100u}, // 0x3f429bc0
    {OpCode::HoldFor, 0x258u}, // 0x3f429bc8
    {OpCode::DrainFor, 0x12cu}, // 0x3f429bd0
    {OpCode::Pause, 0x0u}, // 0x3f429bd8
    {OpCode::MoveValve, 0x1u}, // 0x3f429be0
    {OpCode::ReachTarget, 0x1200u}, // 0x3f429be8
    {OpCode::HoldFor, 0x258u}, // 0x3f429bf0
    {OpCode::DrainFor, 0x258u}, // 0x3f429bf8
    {OpCode::StopPidKD, 0x0u}, // 0x3f429c00
    {OpCode::RequireFilter, 0x0u}, // 0x3f429c08
};

// CLEAN, original record 0x3f4289d8.
const Instruction CleanCode[] = {
    {OpCode::StepName, 0x8002u}, // 0x3f429700
    {OpCode::ShowScreen, 0x1fu}, // 0x3f429708
    {OpCode::ShowScreen, 0x20u}, // 0x3f429710
    {OpCode::ShowScreen, 0x21u}, // 0x3f429718
    {OpCode::ShowScreen, 0x22u}, // 0x3f429720
    {OpCode::RequireFilter, 0x1u}, // 0x3f429728
    {OpCode::StopPid, 0x0u}, // 0x3f429730
    {OpCode::StepName, 0x1u}, // 0x3f429738
    {OpCode::MspPumps, 0x40u}, // 0x3f429740
    {OpCode::MspValve, 0x2u}, // 0x3f429748
    {OpCode::MspPumps, 0x70u}, // 0x3f429750
    {OpCode::CircFor, 0x3cu}, // 0x3f429758
    {OpCode::WaitTempE, 0x0u}, // 0x3f429760
    {OpCode::StepName, 0x2u}, // 0x3f429768
    {OpCode::MspPumps, 0x40u}, // 0x3f429770
    {OpCode::MspValve, 0x2u}, // 0x3f429778
    {OpCode::MspPumps, 0x70u}, // 0x3f429780
    {OpCode::ReachTarget, 0x30e3u}, // 0x3f429788
    {OpCode::HoldFor, 0x258u}, // 0x3f429790
    {OpCode::StepName, 0x3u}, // 0x3f429798
    {OpCode::MspPumps, 0x40u}, // 0x3f4297a0
    {OpCode::MspValve, 0x5u}, // 0x3f4297a8
    {OpCode::MspPumps, 0x70u}, // 0x3f4297b0
    {OpCode::ReachTarget, 0x30e3u}, // 0x3f4297b8
    {OpCode::HoldFor, 0x78u}, // 0x3f4297c0
    {OpCode::StepName, 0x4u}, // 0x3f4297c8
    {OpCode::MspPumps, 0x40u}, // 0x3f4297d0
    {OpCode::MspValve, 0x6u}, // 0x3f4297d8
    {OpCode::MspPumps, 0x70u}, // 0x3f4297e0
    {OpCode::ReachTarget, 0x30e3u}, // 0x3f4297e8
    {OpCode::HoldFor, 0x78u}, // 0x3f4297f0
    {OpCode::StepName, 0x5u}, // 0x3f4297f8
    {OpCode::MspPumps, 0x40u}, // 0x3f429800
    {OpCode::MspValve, 0x7u}, // 0x3f429808
    {OpCode::MspPumps, 0x70u}, // 0x3f429810
    {OpCode::ReachTarget, 0x30e3u}, // 0x3f429818
    {OpCode::HoldFor, 0x78u}, // 0x3f429820
    {OpCode::StepName, 0x6u}, // 0x3f429828
    {OpCode::MspPumps, 0x40u}, // 0x3f429830
    {OpCode::MspValve, 0x8u}, // 0x3f429838
    {OpCode::MspPumps, 0x70u}, // 0x3f429840
    {OpCode::ReachTarget, 0x30e3u}, // 0x3f429848
    {OpCode::HoldFor, 0x78u}, // 0x3f429850
    {OpCode::StepName, 0x7u}, // 0x3f429858
    {OpCode::MspPumps, 0x40u}, // 0x3f429860
    {OpCode::MspValve, 0x9u}, // 0x3f429868
    {OpCode::MspPumps, 0x70u}, // 0x3f429870
    {OpCode::ReachTarget, 0x30e3u}, // 0x3f429878
    {OpCode::HoldFor, 0x78u}, // 0x3f429880
    {OpCode::StopPid, 0x0u}, // 0x3f429888
    {OpCode::StepName, 0x8u}, // 0x3f429890
    {OpCode::MspPumps, 0x40u}, // 0x3f429898
    {OpCode::MspValve, 0x4u}, // 0x3f4298a0
    {OpCode::MspPumps, 0x70u}, // 0x3f4298a8
    {OpCode::CircFor, 0x78u}, // 0x3f4298b0
    {OpCode::WaitTempE, 0x0u}, // 0x3f4298b8
    {OpCode::StepName, 0x9u}, // 0x3f4298c0
    {OpCode::MspPumps, 0x40u}, // 0x3f4298c8
    {OpCode::MspValve, 0x2u}, // 0x3f4298d0
    {OpCode::MspPumps, 0x70u}, // 0x3f4298d8
    {OpCode::ReachTarget, 0x3c00u}, // 0x3f4298e0
    {OpCode::HoldFor, 0x834u}, // 0x3f4298e8
    {OpCode::StepName, 0xau}, // 0x3f4298f0
    {OpCode::MspPumps, 0x40u}, // 0x3f4298f8
    {OpCode::MspValve, 0x2u}, // 0x3f429900
    {OpCode::MspPumps, 0x70u}, // 0x3f429908
    {OpCode::ReachTarget, 0x5d55u}, // 0x3f429910
    {OpCode::HoldFor, 0x78u}, // 0x3f429918
    {OpCode::StepName, 0xbu}, // 0x3f429920
    {OpCode::MspPumps, 0x40u}, // 0x3f429928
    {OpCode::MspValve, 0x5u}, // 0x3f429930
    {OpCode::MspPumps, 0x70u}, // 0x3f429938
    {OpCode::ReachTarget, 0x5d55u}, // 0x3f429940
    {OpCode::HoldFor, 0x78u}, // 0x3f429948
    {OpCode::StepName, 0xcu}, // 0x3f429950
    {OpCode::MspPumps, 0x40u}, // 0x3f429958
    {OpCode::MspValve, 0x6u}, // 0x3f429960
    {OpCode::MspPumps, 0x70u}, // 0x3f429968
    {OpCode::ReachTarget, 0x5d55u}, // 0x3f429970
    {OpCode::HoldFor, 0x78u}, // 0x3f429978
    {OpCode::StepName, 0xdu}, // 0x3f429980
    {OpCode::MspPumps, 0x40u}, // 0x3f429988
    {OpCode::MspValve, 0x7u}, // 0x3f429990
    {OpCode::MspPumps, 0x70u}, // 0x3f429998
    {OpCode::ReachTarget, 0x5d55u}, // 0x3f4299a0
    {OpCode::HoldFor, 0x78u}, // 0x3f4299a8
    {OpCode::StepName, 0xeu}, // 0x3f4299b0
    {OpCode::MspPumps, 0x40u}, // 0x3f4299b8
    {OpCode::MspValve, 0x8u}, // 0x3f4299c0
    {OpCode::MspPumps, 0x70u}, // 0x3f4299c8
    {OpCode::ReachTarget, 0x5d55u}, // 0x3f4299d0
    {OpCode::HoldFor, 0x78u}, // 0x3f4299d8
    {OpCode::StepName, 0xfu}, // 0x3f4299e0
    {OpCode::MspPumps, 0x40u}, // 0x3f4299e8
    {OpCode::MspValve, 0x9u}, // 0x3f4299f0
    {OpCode::MspPumps, 0x70u}, // 0x3f4299f8
    {OpCode::ReachTarget, 0x5d55u}, // 0x3f429a00
    {OpCode::HoldFor, 0x78u}, // 0x3f429a08
    {OpCode::StopPid, 0x0u}, // 0x3f429a10
    {OpCode::StepName, 0x10u}, // 0x3f429a18
    {OpCode::MspPumps, 0x40u}, // 0x3f429a20
    {OpCode::MspValve, 0x4u}, // 0x3f429a28
    {OpCode::MspPumps, 0x70u}, // 0x3f429a30
    {OpCode::CircFor, 0x78u}, // 0x3f429a38
    {OpCode::WaitTempE, 0x0u}, // 0x3f429a40
    {OpCode::StepName, 0x11u}, // 0x3f429a48
    {OpCode::MspPumps, 0x40u}, // 0x3f429a50
    {OpCode::MspValve, 0x2u}, // 0x3f429a58
    {OpCode::MspPumps, 0x70u}, // 0x3f429a60
    {OpCode::ReachTarget, 0x471cu}, // 0x3f429a68
    {OpCode::HoldFor, 0x3cu}, // 0x3f429a70
    {OpCode::MspPumps, 0x40u}, // 0x3f429a78
    {OpCode::RequireFilter, 0x0u}, // 0x3f429a80
    {OpCode::StopPidKD, 0x0u}, // 0x3f429a88
    {OpCode::StepName, 0x8017u}, // 0x3f429a90
    {OpCode::ShowScreen, 0x23u}, // 0x3f429a98
    {OpCode::ShowScreen, 0x24u}, // 0x3f429aa0
    {OpCode::RequireFilter, 0x1u}, // 0x3f429aa8
    {OpCode::ShowScreen, 0x25u}, // 0x3f429ab0
    {OpCode::ShowScreen, 0x26u}, // 0x3f429ab8
    {OpCode::ShowScreen, 0x27u}, // 0x3f429ac0
    {OpCode::StepName, 0x8009u}, // 0x3f429ac8
    {OpCode::RinseCycle, 0x0u}, // 0x3f429ad0
    {OpCode::RinseCycle, 0x0u}, // 0x3f429ad8
    {OpCode::RinseCycle, 0x0u}, // 0x3f429ae0
    {OpCode::RinseCycle, 0x0u}, // 0x3f429ae8
    {OpCode::RinseCycle, 0x0u}, // 0x3f429af0
    {OpCode::RinseCycle, 0x0u}, // 0x3f429af8
    {OpCode::RinseCycle, 0x0u}, // 0x3f429b00
    {OpCode::RinseCycle, 0x0u}, // 0x3f429b08
    {OpCode::RinseCycle, 0x0u}, // 0x3f429b10
    {OpCode::RinseCycle, 0x0u}, // 0x3f429b18
    {OpCode::RinseCycle, 0x0u}, // 0x3f429b20
    {OpCode::StepName, 0x8017u}, // 0x3f429b28
    {OpCode::ShowScreen, 0x28u}, // 0x3f429b30
    {OpCode::RequireFilter, 0x0u}, // 0x3f429b38
};
const char* const CleanStrings[] = {
    "CLEAN", // 0x3f41d880
    "Balance Temps", // 0x3f41dd2b
    "120 Passthrough", // 0x3f41dd39
    "120 Adj 1 Z1", // 0x3f41dd49
    "120 Adj 2 Z2P1", // 0x3f41dd56
    "120 Adj 3 P2", // 0x3f41dd65
    "120 Adj 4 ZP3", // 0x3f41dd72
    "120 Adj 5 ZP4", // 0x3f41dd80
    "120 Mash", // 0x3f41dd8e
    "140 Passthrough", // 0x3f41dd97
    "200 Passthrough", // 0x3f41dda7
    "200 Adj 1 Z1", // 0x3f41ddb7
    "200 Adj 2 Z2P1", // 0x3f41ddc4
    "200 Adj 3 P2", // 0x3f41ddd3
    "200 Adj 4 ZP3", // 0x3f41dde0
    "200 Adj 5 ZP4", // 0x3f41ddee
    "Mash", // 0x3f41dd92
    "Cool", // 0x3f41ddfc
};

// Pico Pale, original record 0x3f4289bc.
const Instruction PicoPaleDemoCode[] = {
    {OpCode::StepName, 0x8002u}, // 0x3f429d88
    {OpCode::ValveMode, 0x0u}, // 0x3f429d90
    {OpCode::PrepareBrew, 0x0u}, // 0x3f429d98
    {OpCode::StepName, 0x1u}, // 0x3f429da0
    {OpCode::MoveValve, 0x1u}, // 0x3f429da8
    {OpCode::ReachTarget, 0x42aau}, // 0x3f429db0
    {OpCode::StepName, 0x2u}, // 0x3f429db8
    {OpCode::MoveValve, 0x2u}, // 0x3f429dc0
    {OpCode::ReachTarget, 0x42aau}, // 0x3f429dc8
    {OpCode::HoldFor, 0x3840u}, // 0x3f429dd0
    {OpCode::DrainFor, 0x1e0u}, // 0x3f429dd8
    {OpCode::CompleteBrew, 0x0u}, // 0x3f429de0
};
const char* const PicoPaleDemoStrings[] = {
    "Pico Pale", // 0x3f41dc13
    "Heat To Mash", // 0x3f41de74
    "Mash", // 0x3f41dd92
};

// Valve Test, original record 0x3f4289a0.
const Instruction ValveTestCode[] = {
    {OpCode::StepName, 0x1u}, // 0x3f4291b8
    {OpCode::MspValve, 0x3u}, // 0x3f4291c0
    {OpCode::StepName, 0x2u}, // 0x3f4291c8
    {OpCode::MspValve, 0x2u}, // 0x3f4291d0
    {OpCode::StepName, 0x3u}, // 0x3f4291d8
    {OpCode::MspValve, 0x4u}, // 0x3f4291e0
    {OpCode::StepName, 0x4u}, // 0x3f4291e8
    {OpCode::MspValve, 0x5u}, // 0x3f4291f0
    {OpCode::StepName, 0x5u}, // 0x3f4291f8
    {OpCode::MspValve, 0x6u}, // 0x3f429200
    {OpCode::StepName, 0x6u}, // 0x3f429208
    {OpCode::MspValve, 0x7u}, // 0x3f429210
    {OpCode::StepName, 0x7u}, // 0x3f429218
    {OpCode::MspValve, 0x8u}, // 0x3f429220
    {OpCode::StepName, 0x8u}, // 0x3f429228
    {OpCode::MspValve, 0x9u}, // 0x3f429230
};
const char* const ValveTestStrings[] = {
    "Valve Test", // 0x3f41dbe7
    "Testing REST", // 0x3f41dcc9
    "Testing PASS", // 0x3f41dcd6
    "Testing MASH", // 0x3f41dce3
    "Testing Z1", // 0x3f41dcf0
    "Testing Z2P1", // 0x3f41dcfb
    "Testing P2", // 0x3f41dd08
    "Testing ZP3", // 0x3f41dd13
    "Testing ZP4", // 0x3f41dd1f
};

// Pumps Test, original record 0x3f428984.
const Instruction PumpsTestCode[] = {
    {OpCode::MspValve, 0x2u}, // 0x3f429180
    {OpCode::StepName, 0x1u}, // 0x3f429188
    {OpCode::SetPumps, 0x2u}, // 0x3f429190
    {OpCode::WaitFor, 0x2u}, // 0x3f429198
    {OpCode::StepName, 0x2u}, // 0x3f4291a0
    {OpCode::SetPumps, 0x1u}, // 0x3f4291a8
    {OpCode::WaitFor, 0x2u}, // 0x3f4291b0
};
const char* const PumpsTestStrings[] = {
    "Pumps Test", // 0x3f41dc08
    "Testing DRAIN PUMP", // 0x3f41dca5
    "Testing KEG PUMP", // 0x3f41dcb8
};

// Alarm Test, original record 0x3f428968.
const Instruction AlarmTestCode[] = {
    {OpCode::StepName, 0x1u}, // 0x3f428cc0
    {OpCode::Pause, 0x0u}, // 0x3f428cc8
};
const char* const AlarmTestStrings[] = {
    "Alarm test", // 0x3f41dc7e
    "Step with pause", // 0x3f41dc89
};

// Chill Test, original record 0x3f42894c.
const Instruction ChillTestCode[] = {
    {OpCode::StepName, 0x8002u}, // 0x3f428c68
    {OpCode::RequireFilter, 0x1u}, // 0x3f428c70
    {OpCode::ValveMode, 0x0u}, // 0x3f428c78
    {OpCode::StepName, 0x1u}, // 0x3f428c80
    {OpCode::MoveValve, 0x1u}, // 0x3f428c88
    {OpCode::CircFor, 0x2u}, // 0x3f428c90
    {OpCode::StepName, 0x2u}, // 0x3f428c98
    {OpCode::Pause, 0x0u}, // 0x3f428ca0
    {OpCode::StepName, 0x3u}, // 0x3f428ca8
    {OpCode::CircFor, 0x1eu}, // 0x3f428cb0
    {OpCode::CompleteBrew, 0x0u}, // 0x3f428cb8
};
const char* const ChillTestStrings[] = {
    "Chill test", // 0x3f41dc4f
    "Quick step", // 0x3f41dc5a
    "Connect chiller", // 0x3f41dc65
    "Chilling", // 0x3f41dc75
};

// Keg Pump Test, original record 0x3f4289f4.
const Instruction KegPumpTestCode[] = {
    {OpCode::RequireFilter, 0x1u}, // 0x3f4296a0
    {OpCode::StepName, 0x8002u}, // 0x3f4296a8
    {OpCode::MoveValve, 0x2u}, // 0x3f4296b0
    {OpCode::StepName, 0x8013u}, // 0x3f4296b8
    {OpCode::SetPumps, 0x1u}, // 0x3f4296c0
    {OpCode::ShowScreen, 0x5eu}, // 0x3f4296c8
};

// Wet Valve Test, original record 0x3f428930.
const Instruction WetValveTestCode[] = {
    {OpCode::StepName, 0x0u}, // 0x3f428cd0
    {OpCode::RequireFilter, 0x1u}, // 0x3f428cd8
    {OpCode::WaitFor, 0x2u}, // 0x3f428ce0
    {OpCode::MspValve, 0x2u}, // 0x3f428ce8
    {OpCode::SetPumps, 0x3u}, // 0x3f428cf0
    {OpCode::StepName, 0x1u}, // 0x3f428cf8
    {OpCode::WaitFor, 0x2u}, // 0x3f428d00
    {OpCode::SetPumps, 0x2u}, // 0x3f428d08
    {OpCode::StepName, 0x2u}, // 0x3f428d10
    {OpCode::WaitFor, 0x5u}, // 0x3f428d18
    {OpCode::MspValve, 0x4u}, // 0x3f428d20
    {OpCode::SetPumps, 0x3u}, // 0x3f428d28
    {OpCode::StepName, 0x1u}, // 0x3f428d30
    {OpCode::WaitFor, 0x2u}, // 0x3f428d38
    {OpCode::SetPumps, 0x2u}, // 0x3f428d40
    {OpCode::StepName, 0x2u}, // 0x3f428d48
    {OpCode::WaitFor, 0x5u}, // 0x3f428d50
    {OpCode::MspValve, 0x5u}, // 0x3f428d58
    {OpCode::SetPumps, 0x3u}, // 0x3f428d60
    {OpCode::StepName, 0x1u}, // 0x3f428d68
    {OpCode::WaitFor, 0x2u}, // 0x3f428d70
    {OpCode::SetPumps, 0x2u}, // 0x3f428d78
    {OpCode::StepName, 0x2u}, // 0x3f428d80
    {OpCode::WaitFor, 0x5u}, // 0x3f428d88
    {OpCode::MspValve, 0x6u}, // 0x3f428d90
    {OpCode::SetPumps, 0x3u}, // 0x3f428d98
    {OpCode::StepName, 0x1u}, // 0x3f428da0
    {OpCode::WaitFor, 0x2u}, // 0x3f428da8
    {OpCode::SetPumps, 0x2u}, // 0x3f428db0
    {OpCode::StepName, 0x2u}, // 0x3f428db8
    {OpCode::WaitFor, 0x5u}, // 0x3f428dc0
    {OpCode::MspValve, 0x7u}, // 0x3f428dc8
    {OpCode::SetPumps, 0x3u}, // 0x3f428dd0
    {OpCode::StepName, 0x1u}, // 0x3f428dd8
    {OpCode::WaitFor, 0x2u}, // 0x3f428de0
    {OpCode::SetPumps, 0x2u}, // 0x3f428de8
    {OpCode::StepName, 0x2u}, // 0x3f428df0
    {OpCode::WaitFor, 0x5u}, // 0x3f428df8
    {OpCode::MspValve, 0x8u}, // 0x3f428e00
    {OpCode::SetPumps, 0x3u}, // 0x3f428e08
    {OpCode::StepName, 0x1u}, // 0x3f428e10
    {OpCode::WaitFor, 0x2u}, // 0x3f428e18
    {OpCode::SetPumps, 0x2u}, // 0x3f428e20
    {OpCode::StepName, 0x2u}, // 0x3f428e28
    {OpCode::WaitFor, 0x5u}, // 0x3f428e30
    {OpCode::MspValve, 0x9u}, // 0x3f428e38
    {OpCode::SetPumps, 0x3u}, // 0x3f428e40
    {OpCode::StepName, 0x1u}, // 0x3f428e48
    {OpCode::WaitFor, 0x2u}, // 0x3f428e50
    {OpCode::SetPumps, 0x2u}, // 0x3f428e58
    {OpCode::StepName, 0x2u}, // 0x3f428e60
    {OpCode::WaitFor, 0x5u}, // 0x3f428e68
    {OpCode::MspValve, 0x2u}, // 0x3f428e70
    {OpCode::SetPumps, 0x3u}, // 0x3f428e78
    {OpCode::StepName, 0x1u}, // 0x3f428e80
    {OpCode::WaitFor, 0x2u}, // 0x3f428e88
    {OpCode::SetPumps, 0x2u}, // 0x3f428e90
    {OpCode::StepName, 0x2u}, // 0x3f428e98
    {OpCode::WaitFor, 0x5u}, // 0x3f428ea0
    {OpCode::MspValve, 0x4u}, // 0x3f428ea8
    {OpCode::SetPumps, 0x3u}, // 0x3f428eb0
    {OpCode::StepName, 0x1u}, // 0x3f428eb8
    {OpCode::WaitFor, 0x2u}, // 0x3f428ec0
    {OpCode::SetPumps, 0x2u}, // 0x3f428ec8
    {OpCode::StepName, 0x2u}, // 0x3f428ed0
    {OpCode::WaitFor, 0x5u}, // 0x3f428ed8
    {OpCode::MspValve, 0x5u}, // 0x3f428ee0
    {OpCode::SetPumps, 0x3u}, // 0x3f428ee8
    {OpCode::StepName, 0x1u}, // 0x3f428ef0
    {OpCode::WaitFor, 0x2u}, // 0x3f428ef8
    {OpCode::SetPumps, 0x2u}, // 0x3f428f00
    {OpCode::StepName, 0x2u}, // 0x3f428f08
    {OpCode::WaitFor, 0x5u}, // 0x3f428f10
    {OpCode::MspValve, 0x6u}, // 0x3f428f18
    {OpCode::SetPumps, 0x3u}, // 0x3f428f20
    {OpCode::StepName, 0x1u}, // 0x3f428f28
    {OpCode::WaitFor, 0x2u}, // 0x3f428f30
    {OpCode::SetPumps, 0x2u}, // 0x3f428f38
    {OpCode::StepName, 0x2u}, // 0x3f428f40
    {OpCode::WaitFor, 0x5u}, // 0x3f428f48
    {OpCode::MspValve, 0x7u}, // 0x3f428f50
    {OpCode::SetPumps, 0x3u}, // 0x3f428f58
    {OpCode::StepName, 0x1u}, // 0x3f428f60
    {OpCode::WaitFor, 0x2u}, // 0x3f428f68
    {OpCode::SetPumps, 0x2u}, // 0x3f428f70
    {OpCode::StepName, 0x2u}, // 0x3f428f78
    {OpCode::WaitFor, 0x5u}, // 0x3f428f80
    {OpCode::MspValve, 0x8u}, // 0x3f428f88
    {OpCode::SetPumps, 0x3u}, // 0x3f428f90
    {OpCode::StepName, 0x1u}, // 0x3f428f98
    {OpCode::WaitFor, 0x2u}, // 0x3f428fa0
    {OpCode::SetPumps, 0x2u}, // 0x3f428fa8
    {OpCode::StepName, 0x2u}, // 0x3f428fb0
    {OpCode::WaitFor, 0x5u}, // 0x3f428fb8
    {OpCode::MspValve, 0x9u}, // 0x3f428fc0
    {OpCode::SetPumps, 0x3u}, // 0x3f428fc8
    {OpCode::StepName, 0x1u}, // 0x3f428fd0
    {OpCode::WaitFor, 0x2u}, // 0x3f428fd8
    {OpCode::SetPumps, 0x2u}, // 0x3f428fe0
    {OpCode::StepName, 0x2u}, // 0x3f428fe8
    {OpCode::WaitFor, 0x5u}, // 0x3f428ff0
    {OpCode::MspValve, 0x2u}, // 0x3f428ff8
    {OpCode::SetPumps, 0x3u}, // 0x3f429000
    {OpCode::StepName, 0x1u}, // 0x3f429008
    {OpCode::WaitFor, 0x2u}, // 0x3f429010
    {OpCode::SetPumps, 0x2u}, // 0x3f429018
    {OpCode::StepName, 0x2u}, // 0x3f429020
    {OpCode::WaitFor, 0x5u}, // 0x3f429028
    {OpCode::MspValve, 0x4u}, // 0x3f429030
    {OpCode::SetPumps, 0x3u}, // 0x3f429038
    {OpCode::StepName, 0x1u}, // 0x3f429040
    {OpCode::WaitFor, 0x2u}, // 0x3f429048
    {OpCode::SetPumps, 0x2u}, // 0x3f429050
    {OpCode::StepName, 0x2u}, // 0x3f429058
    {OpCode::WaitFor, 0x5u}, // 0x3f429060
    {OpCode::MspValve, 0x5u}, // 0x3f429068
    {OpCode::SetPumps, 0x3u}, // 0x3f429070
    {OpCode::StepName, 0x1u}, // 0x3f429078
    {OpCode::WaitFor, 0x2u}, // 0x3f429080
    {OpCode::SetPumps, 0x2u}, // 0x3f429088
    {OpCode::StepName, 0x2u}, // 0x3f429090
    {OpCode::WaitFor, 0x5u}, // 0x3f429098
    {OpCode::MspValve, 0x6u}, // 0x3f4290a0
    {OpCode::SetPumps, 0x3u}, // 0x3f4290a8
    {OpCode::StepName, 0x1u}, // 0x3f4290b0
    {OpCode::WaitFor, 0x2u}, // 0x3f4290b8
    {OpCode::SetPumps, 0x2u}, // 0x3f4290c0
    {OpCode::StepName, 0x2u}, // 0x3f4290c8
    {OpCode::WaitFor, 0x5u}, // 0x3f4290d0
    {OpCode::MspValve, 0x7u}, // 0x3f4290d8
    {OpCode::SetPumps, 0x3u}, // 0x3f4290e0
    {OpCode::StepName, 0x1u}, // 0x3f4290e8
    {OpCode::WaitFor, 0x2u}, // 0x3f4290f0
    {OpCode::SetPumps, 0x2u}, // 0x3f4290f8
    {OpCode::StepName, 0x2u}, // 0x3f429100
    {OpCode::WaitFor, 0x5u}, // 0x3f429108
    {OpCode::MspValve, 0x8u}, // 0x3f429110
    {OpCode::SetPumps, 0x3u}, // 0x3f429118
    {OpCode::StepName, 0x1u}, // 0x3f429120
    {OpCode::WaitFor, 0x2u}, // 0x3f429128
    {OpCode::SetPumps, 0x2u}, // 0x3f429130
    {OpCode::StepName, 0x2u}, // 0x3f429138
    {OpCode::WaitFor, 0x5u}, // 0x3f429140
    {OpCode::MspValve, 0x9u}, // 0x3f429148
    {OpCode::SetPumps, 0x3u}, // 0x3f429150
    {OpCode::StepName, 0x1u}, // 0x3f429158
    {OpCode::WaitFor, 0x2u}, // 0x3f429160
    {OpCode::SetPumps, 0x2u}, // 0x3f429168
    {OpCode::StepName, 0x2u}, // 0x3f429170
    {OpCode::WaitFor, 0x5u}, // 0x3f429178
};
const char* const WetValveTestStrings[] = {
    "Wet Valve Test", // 0x3f41dbe3
    "Drain + Keg", // 0x3f41dc99
    "Drain", // 0x3f41b20e
};

// Whirlpool Test, original record 0x3f428914.
const Instruction WhirlpoolTestCode[] = {
    {OpCode::StepName, 0x8002u}, // 0x3f429c10
    {OpCode::ShowScreen, 0x2u}, // 0x3f429c18
    {OpCode::ShowScreen, 0x3u}, // 0x3f429c20
    {OpCode::ShowScreen, 0x4u}, // 0x3f429c28
    {OpCode::ShowScreen, 0x5u}, // 0x3f429c30
    {OpCode::ShowScreen, 0x6u}, // 0x3f429c38
    {OpCode::ShowScreen, 0x7u}, // 0x3f429c40
    {OpCode::ShowScreen, 0x8u}, // 0x3f429c48
    {OpCode::ShowScreen, 0x9u}, // 0x3f429c50
    {OpCode::ValveMode, 0x0u}, // 0x3f429c58
    {OpCode::PrepareBrew, 0x0u}, // 0x3f429c60
    {OpCode::StepName, 0x1u}, // 0x3f429c68
    {OpCode::MoveValve, 0x1u}, // 0x3f429c70
    {OpCode::ReachTarget, 0x42aau}, // 0x3f429c78
    {OpCode::StepName, 0x2u}, // 0x3f429c80
    {OpCode::MoveValve, 0x2u}, // 0x3f429c88
    {OpCode::ReachTarget, 0x42aau}, // 0x3f429c90
    {OpCode::HoldFor, 0x3cu}, // 0x3f429c98
    {OpCode::DrainFor, 0x1e0u}, // 0x3f429ca0
    {OpCode::StepName, 0x3u}, // 0x3f429ca8
    {OpCode::MoveValve, 0x1u}, // 0x3f429cb0
    {OpCode::ReachTarget, 0x6138u}, // 0x3f429cb8
    {OpCode::StepName, 0x4u}, // 0x3f429cc0
    {OpCode::MoveValve, 0x3u}, // 0x3f429cc8
    {OpCode::ReachTarget, 0x6138u}, // 0x3f429cd0
    {OpCode::HoldFor, 0x4b0u}, // 0x3f429cd8
    {OpCode::StepName, 0x5u}, // 0x3f429ce0
    {OpCode::MoveValve, 0x4u}, // 0x3f429ce8
    {OpCode::ReachTarget, 0x5500u}, // 0x3f429cf0
    {OpCode::HoldFor, 0x4b0u}, // 0x3f429cf8
    {OpCode::StepName, 0x6u}, // 0x3f429d00
    {OpCode::MoveValve, 0x5u}, // 0x3f429d08
    {OpCode::ReachTarget, 0x49e3u}, // 0x3f429d10
    {OpCode::HoldFor, 0x4b0u}, // 0x3f429d18
    {OpCode::StepName, 0x7u}, // 0x3f429d20
    {OpCode::MoveValve, 0x6u}, // 0x3f429d28
    {OpCode::ReachTarget, 0x3ec7u}, // 0x3f429d30
    {OpCode::HoldFor, 0x4b0u}, // 0x3f429d38
    {OpCode::DrainFor, 0x12cu}, // 0x3f429d40
    {OpCode::StepName, 0x8u}, // 0x3f429d48
    {OpCode::Pause, 0x0u}, // 0x3f429d50
    {OpCode::StepName, 0x9u}, // 0x3f429d58
    {OpCode::MoveValve, 0x1u}, // 0x3f429d60
    {OpCode::ReachTarget, 0x1371u}, // 0x3f429d68
    {OpCode::HoldFor, 0x258u}, // 0x3f429d70
    {OpCode::DrainFor, 0x258u}, // 0x3f429d78
    {OpCode::CompleteBrew, 0x0u}, // 0x3f429d80
};
const char* const WhirlpoolTestStrings[] = {
    "whirlpool test", // 0x3f41de01
    "Heat Water", // 0x3f41de10
    "Mash", // 0x3f41dd92
    "Heat to Boil", // 0x3f41de1b
    "Boil Adjunct 1", // 0x3f41de28
    "Boil Adjunct 2", // 0x3f41de37
    "Boil Adjunct 3", // 0x3f41de46
    "Boil Adjunct 4", // 0x3f41de55
    "Connect Chiller", // 0x3f41de64
    "Chill", // 0x3f41b205
};

// PICOSTILL, original record 0x3f428a80.
const Instruction PicoStillBoilPointCode[] = {
    {OpCode::StepName, 0x8002u}, // 0x3f429238
    {OpCode::FlagSet, 0x20u}, // 0x3f429240
    {OpCode::ValveMode, 0x0u}, // 0x3f429248
    {OpCode::ShowScreen, 0x4eu}, // 0x3f429250
    {OpCode::ShowScreen, 0x42u}, // 0x3f429258
    {OpCode::ShowScreen, 0x43u}, // 0x3f429260
    {OpCode::RequireFilter, 0x1u}, // 0x3f429268
    {OpCode::ShowScreen, 0x44u}, // 0x3f429270
    {OpCode::ShowScreen, 0x45u}, // 0x3f429278
    {OpCode::ShowScreen, 0x46u}, // 0x3f429280
    {OpCode::StillScan, 0x0u}, // 0x3f429288
    {OpCode::WaitStillReady, 0x0u}, // 0x3f429290
    {OpCode::ShowScreen, 0x47u}, // 0x3f429298
    {OpCode::StillStart, 0x0u}, // 0x3f4292a0
    {OpCode::MoveValve, 0x5u}, // 0x3f4292a8
    {OpCode::StFindHeatLoss, 0x0u}, // 0x3f4292b0
    {OpCode::StFindBoilPoint, 0x0u}, // 0x3f4292b8
    {OpCode::StStabilize, 0x0u}, // 0x3f4292c0
    {OpCode::StDistille, 0x0u}, // 0x3f4292c8
};

// CHILL, original record 0x3f428b0c.
const Instruction ChillCode[] = {
    {OpCode::StepName, 0x8002u}, // 0x3f429538
    {OpCode::ShowScreen, 0x2bu}, // 0x3f429540
    {OpCode::SetITMin, 0xa00u}, // 0x3f429548
    {OpCode::SetITMax, 0x5a00u}, // 0x3f429550
    {OpCode::SetTarget, 0x12e3u}, // 0x3f429558
    {OpCode::GetTemp, 0x0u}, // 0x3f429560
    {OpCode::RequireFilter, 0x1u}, // 0x3f429568
    {OpCode::StepName, 0x8010u}, // 0x3f429570
    {OpCode::MoveValve, 0x1u}, // 0x3f429578
    {OpCode::SetPumps, 0x3u}, // 0x3f429580
    {OpCode::WaitFor, 0x78u}, // 0x3f429588
    {OpCode::StepName, 0x800du}, // 0x3f429590
    {OpCode::WaitTempB, 0x0u}, // 0x3f429598
    {OpCode::StepName, 0x8010u}, // 0x3f4295a0
    {OpCode::WaitFor, 0x258u}, // 0x3f4295a8
    {OpCode::SetPumps, 0x2u}, // 0x3f4295b0
    {OpCode::StepName, 0x800eu}, // 0x3f4295b8
    {OpCode::DrainFor, 0x78u}, // 0x3f4295c0
    {OpCode::SetPumps, 0x0u}, // 0x3f4295c8
    {OpCode::RequireFilter, 0x0u}, // 0x3f4295d0
    {OpCode::ShowScreen, 0x2cu}, // 0x3f4295d8
};

// MANUAL BREW, original record 0x3f428af0.
const Instruction ManualBrewCode[] = {
    {OpCode::Setup, 0x214u}, // 0x3f429520
    {OpCode::SetPumps, 0x0u}, // 0x3f429528
    {OpCode::GetStepParams, 0x0u}, // 0x3f429530
};

} // namespace

const std::array<ProgramRecord, 23>& builtin_programs() noexcept {
    static const std::array<ProgramRecord, 23> programs = {{
        ProgramRecord(SessionType::Rinse, 0xffffffffu, "RINSE", RinseCode, 99, nullptr, 0),
        ProgramRecord(SessionType::Drain, 0xffffffffu, "DRAIN", DrainCode, 6, nullptr, 0),
        ProgramRecord(SessionType::Rack, 0xffffffffu, "RACK", RackCode, 18, nullptr, 0),
        ProgramRecord(SessionType::Circulate, 0xffffffffu, "CIRCULATE", CirculateCode, 6, nullptr, 0),
        ProgramRecord(SessionType::Sousvide, 0xffffffffu, "SOUSVIDE", SousVideWithoutKegCode, 21, nullptr, 0),
        ProgramRecord(SessionType::Sousvide, 0xffffffffu, "SOUSVIDE", SousVideWithKegCode, 29, nullptr, 0),
        ProgramRecord(SessionType::PicoStill, 0xffffffffu, "PICOSTILL", PicoStillManualCode, 24, nullptr, 0),
        ProgramRecord(SessionType::Test, 0xffffffffu, "TEST1", Test1Code, 15, nullptr, 0),
        ProgramRecord(SessionType::Test, 0xffffffffu, "TEST2", Test2Code, 7, nullptr, 0),
        ProgramRecord(SessionType::Test, 0xffffffffu, "TEST3", Test3Code, 4, nullptr, 0),
        ProgramRecord(SessionType::Test, 0xffffffffu, "BREWTEST", BrewTestCode, 26, nullptr, 0),
        ProgramRecord(SessionType::Clean, 0xffffffffu, "CLEAN", CleanCode, 136, CleanStrings, 18),
        ProgramRecord(SessionType::BrewDemo, 0xffffffffu, "Pico Pale", PicoPaleDemoCode, 12, PicoPaleDemoStrings, 3),
        ProgramRecord(SessionType::Test, 0xffffffffu, "Valve Test", ValveTestCode, 16, ValveTestStrings, 9),
        ProgramRecord(SessionType::Test, 0xffffffffu, "Pumps Test", PumpsTestCode, 7, PumpsTestStrings, 3),
        ProgramRecord(SessionType::Test, 0xffffffffu, "Alarm Test", AlarmTestCode, 2, AlarmTestStrings, 2),
        ProgramRecord(SessionType::Test, 0xffffffffu, "Chill Test", ChillTestCode, 11, ChillTestStrings, 4),
        ProgramRecord(SessionType::Test, 0xffffffffu, "Keg Pump Test", KegPumpTestCode, 6, nullptr, 0),
        ProgramRecord(SessionType::Test, 0xffffffffu, "Wet Valve Test", WetValveTestCode, 150, WetValveTestStrings, 3),
        ProgramRecord(SessionType::Test, 0xffffffffu, "Whirlpool Test", WhirlpoolTestCode, 47, WhirlpoolTestStrings, 10),
        ProgramRecord(SessionType::PicoStill, 0xffffffffu, "PICOSTILL", PicoStillBoilPointCode, 19, nullptr, 0),
        ProgramRecord(SessionType::Chill, 0xffffffffu, "CHILL", ChillCode, 21, nullptr, 0),
        ProgramRecord(SessionType::ManualBrew, 0xffffffffu, "MANUAL BREW", ManualBrewCode, 3, nullptr, 0),
    }};
    return programs;
}

const ProgramRecord* builtin_program(std::uint32_t id) noexcept {
    switch (id) {
        case 1: return &builtin_programs()[0];
        case 2: return &builtin_programs()[1];
        case 3: return &builtin_programs()[2];
        case 4: return &builtin_programs()[3];
        case 5: return &builtin_programs()[4];
        case 6: return &builtin_programs()[5];
        case 7: return &builtin_programs()[6];
        case 8: return &builtin_programs()[7];
        case 9: return &builtin_programs()[8];
        case 10: return &builtin_programs()[9];
        case 11: return &builtin_programs()[10];
        case 12: return &builtin_programs()[11];
        case 13: return &builtin_programs()[12];
        case 14: return &builtin_programs()[13];
        case 15: return &builtin_programs()[14];
        case 16: return &builtin_programs()[15];
        case 17: return &builtin_programs()[16];
        case 18: return &builtin_programs()[17];
        case 19: return &builtin_programs()[18];
        case 25: return &builtin_programs()[19];
        case 26: return &builtin_programs()[20];
        case 27: return &builtin_programs()[21];
        case 28: return &builtin_programs()[22];
        default: return nullptr;
    }
}
}} // namespace zseries::oem
