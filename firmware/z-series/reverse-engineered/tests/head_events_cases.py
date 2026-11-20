"""Deterministic original Head button and runtime-event comparison inputs."""


def case(operation=0, runtime=3, instruction=14, pause=0, screen=0,
         notification=0, hijacked=0, event=1, value=1, long_press=0):
    return dict(operation=operation, words=[runtime, instruction, pause, screen,
                notification, hijacked, event, value, long_press])


def cases():
    result = []
    # Every opcode, including invalid values, across all Runtime states.
    for runtime in list(range(5)) + [255, 0xffffffff]:
        for opcode in list(range(58)) + [255, 0xffffffff]:
            result.append(case(runtime=runtime, instruction=opcode))
    # All screen selectors and pause reasons in the state-sensitive branch.
    for runtime in list(range(5)) + [0xffffffff]:
        for pause in list(range(18)) + [255, 0xffffffff]:
            for screen in list(range(104)) + [255, 0xffffffff]:
                result.append(case(runtime=runtime, pause=pause, screen=screen))
    # Both special wait opcodes override pause/screen selection in every state.
    for opcode in [54, 55]:
        for runtime in list(range(5)) + [0xffffffff]:
            for pause in [0, 1, 2, 3, 17, 0xffffffff]:
                for screen in [0, 1, 14, 44, 74, 89, 101, 102, 0xffffffff]:
                    result.append(case(runtime=runtime, instruction=opcode, pause=pause, screen=screen))
    for event in [0, 2, 3, 255, 0xffffffff]:
        for value in [0, 1, 2, 0xffffffff]:
            result.append(case(event=event, value=value, notification=123))
    for long_press in [0, 1]:
        for hijacked in [0, 1]:
            result.append(case(operation=1, hijacked=hijacked, long_press=long_press))
    return result
