// Original Z119 instruction validator and fixed-point temperature decoder.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.io.*;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.emulator.EmulatorHelper;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;

public class EmulateRuntimeInstruction extends GhidraScript {
    private static final long VALIDATE = 0x400dff64L, TEMPERATURE = 0x400dff0cL;
    private static final long STACK = 0x3ff90000L, OBJECT = STACK + 256;
    private final Map<String, Long> calls = new TreeMap<>();
    private long currentArgument;
    private long word(EmulatorHelper e, int r) { return e.readRegister("a" + r).longValue() & 0xffffffffL; }
    private double number(EmulatorHelper e, int r) { return Double.longBitsToDouble(word(e, r) | word(e, r + 1) << 32); }
    private void result(EmulatorHelper e, double value) {
        long bits = Double.doubleToRawLongBits(value);
        e.writeRegister("a10", bits & 0xffffffffL); e.writeRegister("a11", bits >>> 32);
    }
    private void count(String label) { calls.merge(label, 1L, Long::sum); }
    private void boundary(EmulatorHelper e, long target) throws Exception {
        switch ((int) target) {
            case (int) TEMPERATURE:
                // The decoder body is also executed separately for every
                // applicable input below. This boundary avoids claiming a
                // model of Xtensa register-window nesting.
                count("temperature_decoder_boundary");
                if (word(e, 10) != OBJECT) throw new IllegalStateException("Wrong instruction object");
                result(e, currentArgument * (1.0 / 256.0)); return;
            case 0x401c0198: count("__floatunsidf"); result(e, word(e, 10)); return;
            case 0x4012cf40: count("__muldf3"); result(e, number(e, 10) * number(e, 12)); return;
            case 0x4012d2c4:
            case 0x4012d328:
                count(target == 0x4012d2c4L ? "__ledf2" : "__gedf2");
                double a = number(e, 10), b = number(e, 12);
                if (!Double.isFinite(a) || !Double.isFinite(b)) throw new IllegalStateException("Nonfinite comparison");
                e.writeRegister("a10", (a < b ? -1L : a > b ? 1L : 0L) & 0xffffffffL); return;
            case 0x400db014: throw new IllegalStateException("Original instruction assertion fired");
            default: throw new IllegalStateException(String.format("Unmodeled call %08x", target));
        }
    }
    private void storeWord(EmulatorHelper e, long address, long value) {
        byte[] bytes = new byte[4];
        for (int i = 0; i < 4; i++) bytes[i] = (byte) (value >>> (i * 8));
        e.writeMemory(toAddr(address), bytes);
    }
    private double execute(EmulatorHelper e, long entry, long opcode, long argument) throws Exception {
        currentArgument = argument;
        for (int i = 0; i < 16; i++) e.writeRegister("a" + i, 0L);
        e.writeRegister("SAR", 0L); e.writeRegister("PS", 0L);
        e.writeMemory(toAddr(STACK), new byte[4096]);
        storeWord(e, OBJECT, opcode); storeWord(e, OBJECT + 4, argument);
        e.writeRegister("a1", STACK + 2048); e.writeRegister("a2", OBJECT);
        e.getEmulator().setExecuteAddress(entry + 3);
        for (int step = 0; step < 300; step++) {
            monitor.checkCancelled();
            Address pc = e.getExecutionAddress();
            Instruction instruction = currentProgram.getListing().getInstructionAt(pc);
            if (instruction == null) throw new IllegalStateException("Undecoded instruction " + pc);
            String op = instruction.getMnemonicString().toLowerCase(Locale.ROOT);
            if (op.startsWith("loop")) throw new IllegalStateException("Unmodeled Xtensa hardware loop");
            if (op.startsWith("retw")) return entry == VALIDATE ? word(e, 2) : number(e, 2);
            if (op.equals("call8") || op.equals("callx8")) {
                long target;
                if (op.equals("callx8")) {
                    Register register = instruction.getRegister(0);
                    if (register == null) throw new IllegalStateException("Missing indirect-call register");
                    target = e.readRegister(register).longValue() & 0xffffffffL;
                } else {
                    Address[] targets = instruction.getFlows();
                    if (targets.length != 1) throw new IllegalStateException("Ambiguous call");
                    target = targets[0].getOffset();
                }
                boundary(e, target);
                e.getEmulator().setExecuteAddress(pc.getOffset() + instruction.getLength());
            } else if (op.startsWith("call")) throw new IllegalStateException("Unexpected call form");
            else if (!e.step(monitor)) throw new IllegalStateException("Execution failed at " + pc + ": " + e.getLastError());
        }
        throw new IllegalStateException("Instruction budget exceeded");
    }
    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 2) throw new IllegalArgumentException("Expected input CSV and output CSV");
        for (long address : new long[] {VALIDATE, TEMPERATURE}) {
            Instruction entry = currentProgram.getListing().getInstructionAt(toAddr(address));
            if (entry == null || !entry.getMnemonicString().equalsIgnoreCase("entry") || entry.getLength() != 3)
                throw new IllegalStateException("Unexpected original prologue");
        }
        EmulatorHelper e = new EmulatorHelper(currentProgram);
        int validations = 0, conversions = 0;
        try (BufferedReader in = Files.newBufferedReader(Path.of(args[0])); BufferedWriter out = Files.newBufferedWriter(Path.of(args[1]))) {
            if (!"opcode,argument".equals(in.readLine())) throw new IllegalArgumentException("Unexpected CSV header");
            out.write("opcode,argument,valid,celsius\n");
            for (String line; (line = in.readLine()) != null;) {
                String[] fields = line.split(",");
                if (fields.length != 2) throw new IllegalArgumentException("Unexpected CSV row");
                long opcode = Long.parseLong(fields[0]), argument = Long.parseLong(fields[1]);
                if (opcode < 0 || opcode > 0xffffffffL || argument < 0 || argument > 0xffffffffL)
                    throw new IllegalArgumentException("Out-of-range word");
                int valid = (int) execute(e, VALIDATE, opcode, argument);
                if (valid != 0 && valid != 1) throw new IllegalStateException("Invalid boolean return");
                String temperature = "";
                if (opcode == 2 || opcode == 25 || opcode == 35 || opcode == 36) {
                    temperature = Double.toString(execute(e, TEMPERATURE, opcode, argument)); conversions++;
                }
                out.write(line + "," + valid + "," + temperature + "\n"); validations++;
            }
        } finally { e.dispose(); }
        Map<String, Object> report = new LinkedHashMap<>();
        report.put("input_sha256", "d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27");
        report.put("validation_function", "0x400dff64"); report.put("temperature_function", "0x400dff0c");
        report.put("validation_cases", validations); report.put("temperature_cases", conversions);
        report.put("scope", "Original instruction validator and decoder bodies; explicit SDK arithmetic/comparison boundaries; nested decoder modeled and independently executed; ENTRY/RETW transitions excluded");
        report.put("sdk_hook_calls", calls); report.put("full_cpu_or_hardware_emulation", false); report.put("hardware_tested", false);
        Files.writeString(Path.of(args[1] + ".json"), new GsonBuilder().setPrettyPrinting().create().toJson(report) + "\n");
        println("Executed " + validations + " original instruction validations and " + conversions + " original temperature decodes");
    }
}
