// Execute original Z119 MSP protocol functions with explicit I2C/diagnostic boundaries.
// @category PicoBrew.Validation

import java.nio.file.*;
import java.io.*;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.emulator.EmulatorHelper;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.scalar.Scalar;

/** This is a bounded instruction harness, not a full Xtensa or ESP32 emulator. */
public class EmulateMsp extends GhidraScript {
    private static final long STACK = 0x3ff97000L;
    private static final long OBJECT = 0x3ff98000L;
    private static final long STATE = 0x3ff98100L;
    private static final long WIRE = 0x3ff98200L;
    private static final long VTABLE = 0x3ff98240L;
    private static final long DATA = 0x3ff98400L;
    private static final long CRC = 0x3ff98500L;
    private static final long AVAILABLE = 0x7ff00000L;
    private final Map<String, Long> boundaries = new TreeMap<>();
    private final Set<Long> bodies = new TreeSet<>();
    private final Map<Long, Integer> imageCrc = new HashMap<>();
    private final Deque<byte[]> responses = new ArrayDeque<>();
    private final List<Object> events = new ArrayList<>();
    private ByteArrayOutputStream packet = new ByteArrayOutputStream();
    private int wireAddress, receiveOffset, waitPolls, budget;
    private byte[] receiving = new byte[0];
    private boolean cacheImageCrc;
    private EmulatorHelper emu;

    private static final class OriginalAssertion extends Exception {
        final int line;
        OriginalAssertion(int line) { super("OEM assertion at Msp.h:" + line); this.line = line; }
    }
    private long reg(int n) { return emu.readRegister("a" + n).longValue() & 0xffffffffL; }
    private long special(String n) { return emu.readRegister(n).longValue() & 0xffffffffL; }
    private void put(int n, long value) { emu.writeRegister("a" + n, value & 0xffffffffL); }
    private void word(long address, long value) throws Exception {
        emu.writeMemory(toAddr(address), new byte[]{(byte)value, (byte)(value >>> 8),
            (byte)(value >>> 16), (byte)(value >>> 24)});
    }
    private long word(long address) throws Exception {
        byte[] b = emu.readMemory(toAddr(address), 4);
        return (b[0] & 255L) | ((b[1] & 255L) << 8) | ((b[2] & 255L) << 16) | ((b[3] & 255L) << 24);
    }
    private static String hex(byte[] data) { return HexFormat.of().formatHex(data); }
    private static byte[] unhex(String text) { return HexFormat.of().parseHex(text); }
    private void boundary(String name) { boundaries.merge(name, 1L, Long::sum); }
    private static Map<String, Object> row(Object... values) {
        Map<String, Object> result = new LinkedHashMap<>();
        for (int i = 0; i < values.length; i += 2) result.put((String)values[i], values[i + 1]);
        return result;
    }
    private void reset(byte[] state) throws Exception {
        for (int i = 0; i < 16; i++) { put(i, 0); emu.writeRegister("f" + i, 0L); }
        for (String name : List.of("SAR", "PS", "LCOUNT", "LBEG", "LEND")) emu.writeRegister(name, 0L);
        emu.writeMemory(toAddr(STACK - 16384), new byte[16384]);
        emu.writeMemory(toAddr(OBJECT), new byte[2048]);
        emu.writeMemory(toAddr(STATE), state);
        word(OBJECT + 12, WIRE); word(OBJECT + 20, STATE);
        emu.writeMemory(toAddr(OBJECT + 16), new byte[]{0x48}); // fixture address, not an OEM default
        word(OBJECT + 36, 0); word(OBJECT + 40, 41);
        word(OBJECT + 44, 17); word(OBJECT + 48, 23);
        word(WIRE, VTABLE); word(VTABLE + 16, AVAILABLE);
        put(1, STACK);
        responses.clear(); events.clear(); packet.reset(); receiving = new byte[0];
        receiveOffset = 0; waitPolls = 0; budget = 6000000;
    }
    private byte[] state(int id, int moving) {
        byte[] data = new byte[24];
        for (int i = 0; i < data.length; i++) data[i] = (byte)(0x90 + i);
        data[4] = (byte)id; data[5] = 0; data[6] = 0; data[7] = 0;
        data[20] = (byte)moving;
        return data;
    }
    private byte[] currentState() throws Exception { return emu.readMemory(toAddr(STATE), 24); }
    private List<Object> eventCopy() { return new ArrayList<>(events); }

    private boolean sdk(long target) throws Exception {
        switch ((int)target) {
            case 0x401bfe3c:
                boundary("TwoWire.beginTransmission");
                wireAddress = (int)reg(11); packet.reset(); return true;
            case 0x401bf8c4:
                boundary("Msp.writeByte_to_TwoWire.write"); packet.write((int)reg(11)); return true;
            case 0x40125ac8:
                boundary("TwoWire.endTransmission_true");
                events.add(List.of("tx", wireAddress, hex(packet.toByteArray()))); put(10, 0); return true;
            case 0x40125ab4:
                boundary("TwoWire.requestFrom_true");
                int count = (int)reg(12);
                if (responses.isEmpty()) throw new IllegalStateException("Missing fixture response for " + count);
                receiving = responses.removeFirst(); receiveOffset = 0;
                if (receiving.length != count) throw new IllegalStateException("Wrong fixture response length");
                events.add(List.of("rx", (int)reg(11), count)); put(10, count); return true;
            case 0x7ff00000:
                boundary("TwoWire.available_fixture_vtable");
                put(10, waitPolls-- > 0 ? 0 : receiving.length - receiveOffset); return true;
            case 0x401bf3a4:
                boundary("Stream.read_byte_wrapper");
                if (receiveOffset >= receiving.length) throw new IllegalStateException("Original read beyond response");
                put(10, receiving[receiveOffset++] & 255); return true;
            case 0x40128994:
                boundary("yield"); events.add(List.of("yield")); return true;
            case 0x401289a0:
                boundary("delay"); events.add(List.of("delay", reg(10))); return true;
            case 0x40081388: boundary("millis_for_diagnostics"); put(10, 0); return true;
            case 0x4008c2b4: boundary("basename_for_diagnostics"); return true;
            case 0x401c00b4: boundary("__divsf3_for_diagnostics"); put(10, 0); return true;
            case 0x4012d530: boundary("__extendsfdf2_for_diagnostics"); put(10, 0); put(11, 0); return true;
            case 0x400d21cc: boundary("diagnostic_log"); put(10, 0); return true;
            case 0x400d5a44: boundary("scope_log_enter"); return true;
            case 0x400d5a68: boundary("scope_log_exit"); return true;
            case 0x400db014: boundary("OEM_assertion"); throw new OriginalAssertion((int)reg(11));
            case 0x400d6164:
                if (cacheImageCrc && imageCrc.containsKey(reg(10))) {
                    boundary("previously_instruction_validated_image_CRC");
                    put(10, imageCrc.get(reg(10))); return true;
                }
                return false;
            default: return false;
        }
    }

    private long[] execute(long entry, long... arguments) throws Exception {
        bodies.add(entry);
        Instruction prologue = currentProgram.getListing().getInstructionAt(toAddr(entry));
        if (prologue == null || !prologue.getMnemonicString().equalsIgnoreCase("entry")) {
            throw new IllegalStateException(String.format("No original ENTRY at %08x", entry));
        }
        Scalar frame = prologue.getScalar(1);
        if (frame == null) throw new IllegalStateException("Unknown original stack frame");
        long[] caller = new long[16];
        for (int i = 0; i < 16; i++) caller[i] = reg(i);
        for (int i = 0; i < 16; i++) put(i, 0);
        put(1, caller[1] - frame.getUnsignedValue());
        for (int i = 0; i < arguments.length; i++) put(i + 2, arguments[i]);
        emu.getEmulator().setExecuteAddress(entry + prologue.getLength());
        long loopBegin = 0, loopEnd = 0, iterations = 0;
        while (true) {
            if (--budget < 0) throw new IllegalStateException("Original function exceeded instruction budget");
            if ((budget & 255) == 0) monitor.checkCancelled();
            Address pc = emu.getExecutionAddress();
            Instruction instruction = currentProgram.getListing().getInstructionAt(pc);
            if (instruction == null) throw new IllegalStateException("No original instruction at " + pc);
            String mnemonic = instruction.getMnemonicString().toLowerCase(Locale.ROOT);
            if (mnemonic.startsWith("retw")) {
                long[] returned = new long[6];
                for (int i = 0; i < 6; i++) returned[i] = reg(i + 2);
                for (int i = 0; i < 16; i++) put(i, caller[i]);
                return returned;
            }
            long next = pc.getOffset() + instruction.getLength();
            if (mnemonic.equals("loop")) {
                // Ghidra 12.2 loses the actual loop back in this function. Execute
                // the original loop count/end operands explicitly; all body
                // arithmetic, loads and stores still execute original p-code.
                if (pc.getOffset() != 0x400d200eL || entry != 0x400d2000L) {
                    throw new IllegalStateException("Unreviewed hardware LOOP at " + pc);
                }
                iterations = emu.readRegister(instruction.getRegister(0)).longValue() & 0xffffffffL;
                loopBegin = next; loopEnd = 0x400d202bL;
                if (iterations != 8) throw new IllegalStateException("Unexpected original CRC loop count");
                emu.writeRegister("LCOUNT", 0L); // disable inaccurate implicit p-code loop treatment
                emu.getEmulator().setExecuteAddress(next); continue;
            }
            if (mnemonic.startsWith("loop")) throw new IllegalStateException("Unreviewed LOOP variant");
            if (mnemonic.equals("call8") || mnemonic.equals("callx8")) {
                long target;
                if (mnemonic.equals("callx8")) {
                    Register register = instruction.getRegister(0);
                    target = emu.readRegister(register).longValue() & 0xffffffffL;
                } else {
                    Address[] flows = instruction.getFlows();
                    if (flows.length != 1) throw new IllegalStateException("Ambiguous original call");
                    target = flows[0].getOffset();
                }
                if (!sdk(target)) {
                    if (target < 0x400d1f4cL || target > 0x400dd0acL) {
                        throw new IllegalStateException(String.format("Unmodeled call 0x%08x", target));
                    }
                    long[] args = new long[6];
                    for (int i = 0; i < 6; i++) args[i] = reg(i + 10);
                    long[] returned = execute(target, args);
                    for (int i = 0; i < 6; i++) put(i + 10, returned[i]);
                }
                emu.getEmulator().setExecuteAddress(next); continue;
            }
            if (!emu.step(monitor)) throw new IllegalStateException("Instruction failed at " + pc + ": " + emu.getLastError());
            if (iterations > 0 && emu.getExecutionAddress().getOffset() == loopEnd) {
                if (--iterations > 0) emu.getEmulator().setExecuteAddress(loopBegin);
            }
        }
    }

    private int attempt(long entry, long... args) throws Exception {
        try { execute(entry, args); return 0; }
        catch (OriginalAssertion failure) { return failure.line; }
    }

    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 3 || (!args[1].equals("smoke") && !args[1].equals("full"))) {
            throw new IllegalArgumentException("Expected output JSON, smoke|full, and verified input layout JSON");
        }
        JsonObject layout = JsonParser.parseString(Files.readString(Path.of(args[2]))).getAsJsonObject();
        byte[] original = Files.readAllBytes(Path.of(layout.get("input_path").getAsString()));
        String originalSha = hex(MessageDigest.getInstance("SHA-256").digest(original));
        if (!originalSha.equals("d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27")) {
            throw new IllegalStateException("Unexpected original image SHA-256");
        }
        for (JsonElement element : layout.getAsJsonArray("segments")) {
            JsonObject segment = element.getAsJsonObject();
            int size = segment.get("size").getAsInt();
            if (size == 0) continue;
            int offset = segment.get("offset").getAsInt();
            long address = segment.get("address").getAsLong();
            byte[] mapped = new byte[size];
            currentProgram.getMemory().getBytes(toAddr(address), mapped);
            if (!Arrays.equals(mapped, Arrays.copyOfRange(original, offset, offset + size))) {
                throw new IllegalStateException("Analyzed program bytes differ from preserved image");
            }
        }
        boolean full = args[1].equals("full");
        int extent = full ? 256 : 8;
        Map<String, Object> output = row("schema", 1, "input_sha256",
            "d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27",
            "mode", args[1], "hardware_tested", false, "full_cpu_emulation", false);
        emu = new EmulatorHelper(currentProgram);
        try {
            List<Object> crc = new ArrayList<>();
            for (int seed : new int[]{0, 1, 0x1021, 0x1234, 0x7fff, 0x8000, 0xa5a5, 0xffff}) {
                for (int value = 0; value < extent; value++) {
                    reset(state(2, 0)); word(CRC, seed);
                    execute(0x400d2000L, value, CRC);
                    crc.add(List.of(seed, value, word(CRC) & 0xffffL));
                }
            }
            output.put("crc_bytes", crc); println("MSP original CRC byte cases: " + crc.size());

            List<Object> guard = new ArrayList<>();
            for (int id = 0; id < 4; id++) for (int moving = 0; moving < 2; moving++) {
                for (int command = 0; command < extent; command++) {
                    reset(state(id, moving));
                    guard.add(List.of(id, moving, command, attempt(0x400dbcecL, OBJECT, command)));
                }
            }
            // Explicit exception command, including in smoke mode.
            for (int moving = 0; moving < 2; moving++) {
                reset(state(2, moving)); guard.add(List.of(2, moving, 253, attempt(0x400dbcecL, OBJECT, 253)));
            }
            output.put("guards", guard);

            List<Object> decoded = new ArrayList<>();
            for (int kind = 0; kind < 2; kind++) for (int value = 0; value < extent; value++) {
                byte[] before = state(2, 1);
                byte[] response = {(byte)(kind == 0 ? value : 2), (byte)value, (byte)0xa3,
                    (byte)0x91, (byte)0xf2, (byte)0xef, (byte)0xbe, (byte)0xad, (byte)0xde};
                reset(before); responses.add(response); execute(0x400dbd94L, OBJECT);
                decoded.add(List.of(hex(response), hex(before), hex(currentState()), eventCopy()));
            }
            output.put("responses", decoded); println("MSP original response cases: " + decoded.size());

            List<Object> app = new ArrayList<>();
            for (int command = 0; command < extent; command++) for (int width : new int[]{0, 1, 4}) {
                byte[] before = state(2, 0); reset(before); responses.add(new byte[9]);
                long value = (0xdeadbeefL ^ (command * 0x1020304L)) & 0xffffffffL;
                long entry = width == 0 ? 0x400dc050L : width == 1 ? 0x400dc2f8L : 0x400dc528L;
                execute(entry, OBJECT, command, value);
                app.add(List.of(command, width, value, eventCopy()));
            }
            output.put("app_packets", app);

            List<Object> simple = new ArrayList<>();
            for (int command : new int[]{0x19, 0x1c}) {
                for (int response : new int[]{0, 5, 0xa0, 0xaf, 0xff}) {
                    reset(state(1, 0)); if (command != 0x1c) responses.add(new byte[]{(byte)response});
                    long result = execute(0x400dc5e8L, OBJECT, command)[0];
                    simple.add(List.of(command, response, result, eventCopy()));
                }
            }
            output.put("bsl_simple", simple);

            List<Object> program = new ArrayList<>();
            for (int length : new int[]{0, 1, 15, 16, 17, 31, 32, 33}) {
                for (long address : new long[]{0xc002, 0xfffc, 0xfffffc, 0x12345678}) {
                    for (int fail : new int[]{0, 1, 2}) {
                        reset(state(1, 0));
                        byte[] bytes = new byte[length];
                        for (int i = 0; i < length; i++) bytes[i] = (byte)(i * 29 + 7);
                        emu.writeMemory(toAddr(DATA), bytes);
                        for (int i = 1; i <= (length + 15) / 16; i++) responses.add(new byte[]{(byte)(i == fail ? 5 : 0)});
                        long result = execute(0x400dc630L, OBJECT, address, DATA, length)[0];
                        program.add(List.of(address, hex(bytes), fail, result, eventCopy()));
                    }
                }
            }
            output.put("program_segments", program);
            List<Object> erase = new ArrayList<>();
            for (int fail : new int[]{0, 1, 15, 30}) {
                reset(state(1, 0));
                for (int i = 1; i <= 30; i++) responses.add(new byte[]{(byte)(i == fail ? 5 : 0)});
                int line = attempt(0x400dcaa8L, OBJECT);
                erase.add(List.of(fail, line, eventCopy()));
            }
            output.put("erase", erase);

            List<Object> valve = new ArrayList<>();
            for (int position : new int[]{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 255}) {
                for (int moving = 0; moving < 2; moving++) {
                    byte[] before = state(2, moving); reset(before); responses.add(new byte[9]);
                    int line = attempt(0x400dc984L, OBJECT, position);
                    valve.add(List.of(position, hex(before), line, hex(currentState()), eventCopy()));
                }
            }
            output.put("valve", valve);

            List<Object> poll = new ArrayList<>();
            for (int id : new int[]{0, 1, 2, 3}) for (long[] timing : new long[][]{
                {0,0}, {0,1}, {100,99}, {100,100}, {100,101}, {0xfffffffeL,0xffffffffL}, {0xfffffffeL,0}}) {
                reset(state(id, 1)); word(OBJECT + 36, timing[0]); responses.add(new byte[9]);
                execute(0x400dc1c4L, OBJECT, timing[1]);
                poll.add(List.of(id, timing[0], timing[1], word(OBJECT + 36), eventCopy()));
            }
            output.put("poll", poll);

            List<Object> images = new ArrayList<>();
            for (long descriptor : new long[]{0x3f425ddcL, 0x3f425578L}) {
                reset(state(0, 0));
                int result = (int)execute(0x400d6164L, descriptor)[0] & 0xffff;
                imageCrc.put(descriptor, result); images.add(List.of(descriptor, result));
            }
            output.put("image_crc", images); cacheImageCrc = true;
            println("MSP original embedded image CRC cases completed");

            List<Object> setup = new ArrayList<>();
            byte[] boot = unhex("515151515100000000");
            byte[] version = unhex("010100000000000000");
            byte[] wrongVersion = unhex("01ffff000000000000");
            byte[] none = new byte[9];
            // Every scenario response is explicit; programming successes receive 0.
            for (int scenario = 0; scenario < 11; scenario++) {
                reset(new byte[24]);
                byte[] first = scenario == 0 ? none : scenario == 1 ? unhex("510000000000000000") :
                    scenario == 2 ? version : boot;
                responses.add(first);
                if (first[0] == 1) responses.add(none); // app -> boot command response
                if (scenario >= 2) {
                    responses.add(new byte[]{(byte)(scenario == 3 ? 0x90 : 0xa1)});
                    if (scenario != 3) {
                        responses.add(scenario >= 5 ? wrongVersion : version);
                        if (scenario >= 5) {
                            responses.add(none);
                            responses.add(new byte[]{(byte)(scenario == 8 ? 0x90 : 0xa1)});
                            if (scenario != 8) {
                                int operations = scenario == 7 ? 31 : scenario == 9 ? 1 : 30 + 176 + 4 + 1 + 1;
                                for (int i = 0; i < operations; i++) {
                                    responses.add(new byte[]{(byte)(((scenario == 7 || scenario == 9) && i + 1 == operations) ? 5 : 0)});
                                }
                            }
                            if (scenario != 9) responses.add(scenario == 6 ? wrongVersion :
                                scenario == 10 ? unhex("010101000000000000") : version);
                        }
                    }
                }
                List<String> inputResponses = responses.stream().map(EmulateMsp::hex).toList();
                int line = attempt(0x400dcdc0L, OBJECT);
                setup.add(List.of(scenario, line, hex(currentState()), eventCopy(), inputResponses));
            }
            output.put("setup", setup);
            output.put("boundary_calls", boundaries);
            output.put("scope", "Original Xtensa instructions; register-window ENTRY/CALL8/RETW boundaries normalized; CRC LOOP8 explicitly interpreted; I2C, clocks and diagnostic calls supplied by fixtures; setup reuses independently instruction-validated image CRCs");
            List<Object> functions = new ArrayList<>();
            for (long address : bodies) {
                Function function = getFunctionAt(toAddr(address));
                if (function == null) throw new IllegalStateException("Missing analyzed function extent");
                List<Object> ranges = new ArrayList<>();
                var iterator = function.getBody().getAddressRanges();
                while (iterator.hasNext()) {
                    var range = iterator.next();
                    byte[] bytes = new byte[(int)range.getLength()];
                    currentProgram.getMemory().getBytes(range.getMinAddress(), bytes);
                    ranges.add(row("address", range.getMinAddress().getOffset(), "size", bytes.length,
                        "sha256", hex(MessageDigest.getInstance("SHA-256").digest(bytes))));
                }
                functions.add(row("entry", address, "ranges", ranges));
            }
            output.put("original_functions", functions);
            Files.writeString(Path.of(args[0]), new GsonBuilder().setPrettyPrinting().create().toJson(output) + "\n");
            println("Wrote original MSP instruction fixtures to " + args[0]);
        } finally { emu.dispose(); }
    }
}
