"""Deterministic input states for original Z119 runtime instruction execution.

The ABI offsets are evidence, while C++ expressions address the independent
portable representation. No expected dispatch/control results are computed here.
"""
import itertools
import struct


def field(name, region, offset, expression, default=0, width=4, output=True):
    return dict(name=name, region=region, offset=offset, expression=expression,
                default=default, width=width, output=output)


WORDS = [
    field("state", "state", 4, "state.id", 3),
    field("state_date", "state", 8, "state.state_date", 100),
    field("pause_reason", "state", 0x0c, "state.pause_reason"),
    field("step_count", "state", 0x10, "state.step_count", 0xffffffff),
    field("manual_step_count", "state", 0x68, "state.manual_step_count", 3),
    field("paused", "state", 0x70, "state.paused", width=1),
    field("pid", "state", 0x71, "state.pid_active", width=1),
    field("start_date", "state", 0x74, "state.start_date", 10),
    field("elapsed", "state", 0x78, "state.elapsed_seconds", 7),
    field("opcode", "state", 0x7c, "state.instruction.opcode", 9),
    field("argument", "state", 0x80, "state.instruction.argument"),
    field("instruction_count", "state", 0x84, "state.instruction_count", 11),
    field("instruction_date", "state", 0x88, "state.instruction_date", 1000),
    field("instruction_left", "state", 0x8c, "state.instruction_left_seconds", 7),
    field("screen", "state", 0x90, "state.screen"),
    field("delay", "state", 0xa8, "state.delay_seconds", 17),
    field("time", "state", 0xac, "state.time_seconds", 23),
    field("total_left", "state", 0xb0, "state.total_left_seconds", 31),
    field("ignore", "state", 0xb4, "state.ignore_monitor_safeties", width=1),
    field("flag_b5", "state", 0xb5, "state.flag_b5", 1, 1),
    field("flag_b6", "state", 0xb6, "state.flag_b6", width=1),
    field("flag_b7", "state", 0xb7, "state.flag_b7", width=1),
    field("manual_notification", "state", 0xb8, "state.manual_step_notification", width=1),
    field("cooling", "state", 0xb9, "state.cooling_to_target", width=1),
    field("flags", "state", 0xc8, "state.flags"),
    field("still_state", "state", 0xd0, "state.still.id", 1),
    field("still_paused", "state", 0xd4, "state.still.paused_id"),
    field("reference_date", "state", 0x188, "state.still.reference_date", 99),
    field("boil_date", "state", 0x1b0, "state.still.boil_date"),
    field("step_date", "state", 0x1c8, "state.still.step_date", 123),
    field("watch_enabled", "state", 0x1dc, "state.t1_drop_watch.enabled", width=1),
    field("watch_triggered", "state", 0x1dd, "state.t1_drop_watch.triggered", width=1),
    field("manual_location", "state", 0x1f4, "state.manual.location", 1),
    field("manual_temp_unit", "state", 0x200, "state.manual.temperature.unit", 5),
    field("manual_hold_unit", "state", 0x210, "state.manual.hold.unit", 6),
    field("manual_drain_unit", "state", 0x220, "state.manual.drain.unit", 6),
    field("requested_valve", "object", 0x92c, "requested_valve", 4),
    field("requested_pumps", "object", 0x930, "requested_pumps", 0x70, 1),
    field("step_reference", "object", 0x934, "saved_step_reference", 0x8004),
    field("substep_reference", "object", 0x938, "saved_substep_reference", 0x800b),
    field("valve_map", "map", 0x91c, "alternative_valve_map"),
    field("filter_required", "object", 0x920, "filter_required", width=1),
    field("stopping", "object", 0x928, "stopping", width=1),
    field("actual_pumps", "core", 0x2e1, "msp->state->pumps", 0x70, 1),
    field("fan", "core", 0x2e2, "msp->state->fan", 8, 1),
    field("actual_valve", "core", 0x2e3, "msp->state->valve_position", 3, 1),
    field("moving", "core", 0x2ec, "msp->state->moving_valve", width=1),
    field("filter_absent", "core", 0x526, "observed.filter_absent", width=1, output=False),
    field("sample", "core", 0x950, "observed.still_sample_present", 1, output=False),
    field("signal_950", "core", 0x9d4, "observed.signal_950_defined", 1, output=False),
    field("signal_a00", "core", 0xa84, "observed.signal_a00_defined", 1, output=False),
    field("signal_ab0", "core", 0xb34, "observed.signal_ab0_defined", 1, output=False),
    field("byte_980", "core", 0x980, "observed.byte_980", 1, 1, False),
    field("still_error", "core", 0x107c, "observed.pico_still_error", width=1, output=False),
    field("system_error", "error", 0x7a8, "observed.system_error", output=False),
    field("msp_error", "core", 0x2e8, "observed.msp_error", output=False),
    field("too_hot", "core", 0x520, "observed.too_hot", width=1, output=False),
    field("too_cold", "core", 0x521, "observed.too_cold", width=1, output=False),
    field("hex_wort_delta", "core", 0x523, "observed.max_hex_wort_delta", width=1, output=False),
    field("wort_drain_delta", "core", 0x524, "observed.max_wort_drain_delta", width=1, output=False),
    field("hex_rise", "core", 0x525, "observed.max_hex_rise", width=1, output=False),
    field("command_id", "object", 0x14, "command.id", output=False),
    field("command_program", "object", 0x18, "command.program_id", output=False),
    field("command_duration", "object", 0x28, "command.duration", 55, output=False),
    field("command_location", "object", 0x2c, "command.location", 3, output=False),
    field("command_manual_location", "object", 0x34, "command.manual.location", 1, output=False),
    field("command_manual_temp_unit", "object", 0x40, "command.manual.temperature.unit", 5, output=False),
    field("command_manual_hold_unit", "object", 0x50, "command.manual.hold.unit", 6, output=False),
    field("command_manual_drain_unit", "object", 0x60, "command.manual.drain.unit", 6, output=False),
]

DOUBLES = [
    field("it_min", "state", 0x98, "state.input_temperature_min", 10, 8),
    field("it_max", "state", 0xa0, "state.input_temperature_max", 93, 8),
    field("estimated_rate", "state", 0xc0, "state.estimated_heating_rate", 1.5, 8),
    field("reference_wort", "state", 0x190, "state.still.reference_wort", 70, 8),
    field("reference_t3", "state", 0x198, "state.still.reference_t3", 65, 8),
    field("heat_loss", "state", 0x1a0, "state.still.heat_loss", 5, 8),
    field("boil_t3", "state", 0x1a8, "state.still.boil_t3", 80, 8),
    field("boil_scalar_978", "state", 0x1b8, "state.still.boil_scalar_978", 18, 8),
    field("boil_target", "state", 0x1c0, "state.still.boil_target", 85, 8),
    field("step_target", "state", 0x1d0, "state.still.step_target", 8, 8),
    field("watch_maximum", "state", 0x1e0, "state.t1_drop_watch.maximum", 70, 8),
    field("watch_threshold", "state", 0x1e8, "state.t1_drop_watch.threshold", 5, 8),
    field("manual_temperature", "state", 0x1f8, "state.manual.temperature.value", 160, 8),
    field("manual_hold", "state", 0x208, "state.manual.hold.value", 3600, 8),
    field("manual_drain", "state", 0x218, "state.manual.drain.value", 300, 8),
    field("target", "core", 0x2b0, "observed.heater_target", 70, 8, False),
    field("hex", "core", 0x548, "observed.hex_temperature", 70, 8, False),
    field("wort", "core", 0x558, "observed.wort_temperature", 70, 8, False),
    field("drain", "core", 0x568, "observed.drain_temperature", 50, 8, False),
    field("wort_rate", "core", 0x588, "observed.wort_rate", 2, 8, False),
    field("scalar_958", "core", 0x958, "observed.scalar_958", 64, 8, False),
    field("scalar_960", "core", 0x960, "observed.scalar_960", 30, 8, False),
    field("t3", "core", 0x968, "observed.still_t3", 62, 8, False),
    field("scalar_978", "core", 0x978, "observed.scalar_978", 11, 8, False),
    field("scalar_9d8", "core", 0x9d8, "observed.scalar_9d8", 7, 8, False),
    field("scalar_a28", "core", 0xa28, "observed.scalar_a28", 11, 8, False),
    field("scalar_a98", "core", 0xa98, "observed.scalar_a98", -0.1, 8, False),
    field("scalar_aa8", "core", 0xaa8, "observed.scalar_aa8", -3, 8, False),
    field("scalar_ab8", "core", 0xab8, "observed.scalar_ab8", 62, 8, False),
    field("scalar_ad8", "core", 0xad8, "observed.scalar_ad8", 3, 8, False),
    field("scalar_b48", "core", 0xb48, "observed.scalar_b48", 0.1, 8, False),
    field("scalar_b58", "core", 0xb58, "observed.scalar_b58", -0.1, 8, False),
    field("command_temperature", "object", 0x20, "command.temperature", 75, 8, False),
    field("command_manual_temperature", "object", 0x38, "command.manual.temperature.value", 150.75, 8, False),
    field("command_manual_hold", "object", 0x48, "command.manual.hold.value", 20, 8, False),
    field("command_manual_drain", "object", 0x58, "command.manual.drain.value", 10, 8, False),
]

STRINGS = ["Recipe step", "r" * 70, "", "Boil / sample"]


def cases():
    result = []
    def add(group, action, args=(), words=None, doubles=None, stack=(), now=2000, ready=1):
        result.append(dict(group=group, action=action, args=list(args), words=words or {},
                           doubles=doubles or {}, stack=list(stack), now=now, ready=ready))

    # Every opcode plus undefined encodings. Arguments are independent inputs.
    for op in list(range(60)) + [255, 0xffffffff]:
        args = {1: [0, 1, 0x8000, 0x8025, 0x8026], 2: [0, 69*256, 70*256, 71*256, 98*256],
                6: list(range(7)), 12: [0, 3, 0x800b, 0x8025], 27: list(range(1, 10)),
                28: [0x40, 0x50, 0x60, 0x70], 29: [0, 1, 2], 25: [0, 70*256, 0xffffffff],
                35: [0, 1, 0x80000000, 0xffffffff], 36: [0, 0xffffffff],
                57: [0, 0x100, 0x110, 0x111, 0x200, 0x300, 0x400, 0xc01, 0x810]}.get(op, [0, 1, 60])
        for argument in args:
            add("dispatch", 0, [op, argument])
    for pid, expected, actual, alternate in itertools.product([0, 1], [0x40, 0x50, 0x60, 0x70], [0x40, 0x50], [0, 1]):
        for valve in [1, 3, 6]:
            add("valve_expansion", 0, [6, valve], dict(pid=pid, requested_pumps=expected,
                actual_pumps=actual, valve_map=alternate))
    for flags in [0, 4, 0x10, 0x11, 0x20, 0x21, 0xffffffff]:
        for opcode in [4, 39, 40]:
            add("brew_flags", 0, [opcode, 42], dict(flags=flags))
    for target in [0, 53.9, 54, 54.1, 62, 95]:
        add("still_target", 0, [43, 0], doubles={"t3": target})
    for op in [32, 33]:
        add("ignored_safety_assertion", 0, [op, 1], dict(ignore=1))
    for manual in [0, 1, 255, 0x7fffffff, 0x80000000, 0xffffffff]:
        add("manual_step_name", 0, [1, 0x8025], dict(manual_step_count=manual))
    for count in [254, 255, 256]:
        for op in [7, 6, 39, 56]:
            add("stack_capacity", 0, [op, 1], stack=[[9, i] for i in range(count)])
    for op in [3, 4, 5, 13, 27, 28, 9, 0xffffffff]:
        for arg in [0, 1, 4294967, 4294968, 0xffffffff]:
            add("duration", 2, [op, arg])

    for op, command in itertools.product(range(58), range(13)):
        add("wait_opcode_command", 1, [command], dict(opcode=op, argument=1))
    for op in [9, 13, 27, 28]:
        for start, now, arg in [(1000,999,1),(1000,1000,0),(1000,1001,0),(1000,1999,1),
                                (1000,2000,1),(1000,2001,1),(1,0xffffffff,0xffffffff),
                                (0xfffffff0,0xfffffff1,1),(0xfffffff0,0,1)]:
            add("wait_clock", 1, [0], dict(opcode=op,argument=arg,instruction_date=start), now=now)
    for op, reason, command, absent, error in itertools.product([14], [0, 1, 3, 12], [0, 4, 5], [0, 1], [0, 1]):
        add("wait_user", 1, [command], dict(opcode=op,pause_reason=reason,filter_absent=absent,still_error=error))
    for op in [15, 16, 17, 18]:
        for delta in [-1.01,-1,-.201,-.2,-.199,0,.199,.2,.201,1,1.01]:
            add("wait_temperature", 1, [0], dict(opcode=op), dict(target=70+delta,hex=70+delta,wort=70))
    for op, key, values in [(49,"scalar_a28",[11.999,12,12.001]),(49,"scalar_aa8",[-2.001,-2,-1.999]),
                             (50,"scalar_ad8",[3.999,4,4.001]),(50,"scalar_b58",[-.201,-.2,-.199,.199,.2,.201]),
                             (51,"scalar_b48",[-.201,-.2,-.199,.199,.2,.201]),(52,"scalar_9d8",[5.999,6,6.001]),
                             (52,"scalar_958",[62.999,63,63.001]),(53,"scalar_a98",[-.001,0,.001]),
                             (53,"wort",[85.199,85.2,85.201]),(54,"scalar_960",[32.199,32.2,32.201]),
                             (55,"scalar_978",[9.999,10,10.001])]:
        for value in values:
            for sample, defined in itertools.product([0,1],[0,1]):
                add("wait_still_boundaries",1,[0],dict(opcode=op,sample=sample,signal_950=defined,
                    signal_a00=defined,signal_ab0=defined,byte_980=0),{key:value})
    for ready in [0,1]: add("still_ready",1,[0],dict(opcode=48),ready=ready)
    for moving in [0,1]: add("valve_wait",1,[0],dict(opcode=27,moving=moving))

    for arg, elapsed in itertools.product([0,1,2,10],[0,999,1000,1001,9999,10000]):
        add("save_wait",3,words=dict(opcode=13,argument=arg),now=1000+elapsed)
    for opcode in [9,15,25,59]: add("save_other",3,words=dict(opcode=opcode,argument=0))
    for done, pid, valve, screen, refs in itertools.product([0,1],[0,1],[0,4],[0,21],[0,1]):
        add("resume",4,[done],dict(opcode=13,argument=10,pid=pid,requested_valve=valve,screen=screen,
            step_reference=0x8004 if refs else 0,substep_reference=0x800b if refs else 0))
    for old, new, ignore in itertools.product(range(6),range(6),[0,1]):
        add("transition",5,[new],dict(state=old,ignore=ignore,still_paused=7),stack=[[9,0],[13,20]])
    for new in [2,4]: add("transition_valve_assertion",5,[new],dict(actual_valve=4))
    for old, new, defined in itertools.product(range(10),range(10),[0,1]):
        add("still_transition",6,[new],dict(still_state=old,reference_date=1 if defined else 0))
    for done, pid, stop, paused in itertools.product([0,1],[0,1],[0,1],[0,2]):
        add("still_pause",7,[done,55,0x8023,stop],dict(opcode=13,argument=10,pid=pid,still_paused=paused,still_state=2))
    for still, completed, sample, flag in itertools.product(range(10),[9,51,52,54,55],[0,1],[0,1]):
        add("still_progress",8,[completed],dict(still_state=still,opcode=completed if completed != 9 else 13,
            argument=10,sample=sample,byte_980=flag),dict(scalar_978=9,scalar_960=36))
    for still, completed in [(3,51),(4,52),(7,54),(8,55)]:
        add("still_completed",8,[completed],dict(still_state=still,opcode=completed))
    for date, now, reference in itertools.product([0,123],[60123,60124],[0,99]):
        add("still_boiling_ramp",8,[9],dict(still_state=4,step_date=date,reference_date=reference),now=now)
    add("still_completion_assertion",8,[52],dict(still_state=4,opcode=13))
    add("still_zero_date_assertion",8,[52],dict(still_state=4,opcode=52),now=0)
    for op, pumps, fan, temp in itertools.product([9,13],[0x40,0x50,0x60,0x70],[0,8],[49.9,50,50.1]):
        add("fan",9,[0],dict(opcode=op,actual_pumps=pumps,fan=fan),dict(wort=temp,hex=temp,drain=temp))
    add("fan_state_assertion",9,[0],dict(state=2))
    for flags in range(64): add("setup_prompts",10,[flags])
    for count in [0,1,255,256]:
        add("stack_push",11,[57,0x112],stack=[[9,i] for i in range(count)])
        add("stack_pop",12,stack=[[9,i] for i in range(count)])
    units = list(range(10)) + [0xffffffff]
    for a, b in itertools.product(units, units):
        add("quantity_compatibility", 13, [a,b])
        for value in [-1000000,-273.15,-1,0,1,32,67.5,100,1000000]:
            low, high = struct.unpack("<II", struct.pack("<d", value))
            add("quantity_conversion",14,[a,b,low,high])
    for enabled, triggered, current in itertools.product([0,1],[0,1],[64.9,65,65.1,70,70.1]):
        add("drop_watch",15,words=dict(watch_enabled=enabled,watch_triggered=triggered),doubles=dict(scalar_958=current))
    reasons = ["system_error","msp_error","too_hot","too_cold","hex_wort_delta","wort_drain_delta","hex_rise","filter_absent","still_error"]
    for bits, ignore, filter_required in itertools.product(range(1 << len(reasons)),[0,1],[0,1]):
        words = {name:(bits >> i) & 1 for i,name in enumerate(reasons)}
        words.update(ignore=ignore,filter_required=filter_required)
        add("pause_precedence",16,[3 if bits&1 else 0],words)
    for program in list(range(31)) + [0xffffffff]: add("program_selection",17,[program])
    for opcode in range(58):
        add("stack_time_estimate",18,stack=[[opcode,70*256 if opcode in (2,25) else 20]])
    for rate in [0,.009,.01,.011,1.5,10]:
        for sequence in [[[13,10],[2,75*256]],[[24,0],[26,40],[20,0]],[[18,0]],[[15,0],[25,75*256]],[[2,75*256],[5,120]]]:
            add("stack_time_sequence",18,doubles=dict(estimated_rate=rate),stack=sequence)
    for state in [0,1,2,3,4,5,6]:
        for command in [0,1,2,3]:
            add("runtime_state_tick",19,words=dict(state=state,command_id=command),stack=[[13,20],[9,0]])
    for opcode, command in itertools.product([9,13,14,15,16,17,19,21,22,23,27],range(13)):
        add("runtime_command_tick",19,words=dict(opcode=opcode,argument=10,command_id=command),stack=[[13,20],[9,0]])
    for opcode, command, stopping, paused in itertools.product([13,14,27],[0,2,3,4,6],[0,1],[0,1]):
        add("runtime_pause_stop",19,words=dict(opcode=opcode,argument=10,command_id=command,stopping=stopping,
            paused=paused,pause_reason=2,moving=1 if opcode==27 else 0),stack=[[13,20]])
    for reason in reasons:
        for ignore in [0,1]:
            words=dict(opcode=13,argument=10,filter_required=1,ignore=ignore,pid=1)
            words[reason]=1
            add("runtime_safety_pause",19,words=words,stack=[[13,20]])
    for now, word in itertools.product([1999,2000,2001],[0,1]):
        add("runtime_wait_finish",19,words=dict(opcode=13,argument=1,stopping=word),now=now)
    for current in [64.9,65,65.1]:
        add("runtime_t1_notification",19,words=dict(opcode=13,argument=10,watch_enabled=1),doubles=dict(scalar_958=current))
    for unit, value in [(5,150.1),(5,150.9),(4,65.6),(4,66.0),(0,70)]:
        add("runtime_manual_quantity",19,words=dict(opcode=21,command_id=12,command_manual_temp_unit=unit),
            doubles=dict(command_manual_temperature=value))
    for name_mode, strings_mode, code_mode, size in [(0,0,0,0),(1,0,0,0),(2,0,0,0),(0,1,0,0),(0,2,0,0),(0,0,1,0),(0,0,0,257)]:
        add("program_validation",20,[name_mode,strings_mode,code_mode,size],stack=[[1,0],[13,20]])
    for code in [[],[[59,0]],[[1,99]],[[12,0x8026]],[[1,0x8025]],[[2,99*256]]]:
        add("program_validation",20,[0,0,0,0],stack=code)
    return result
