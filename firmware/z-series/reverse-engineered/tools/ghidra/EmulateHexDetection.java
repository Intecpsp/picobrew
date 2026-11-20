// Execute original Z119 sensor-selection instructions with explicit inputs.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.io.*;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.emulator.EmulatorHelper;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.lang.Register;
import ghidra.program.model.address.Address;

public class EmulateHexDetection extends GhidraScript {
    private static final long ENTRY = 0x400dd9f4L;
    private static final long STACK = 0x3ff90000L;
    private static final long OUT = STACK + 512;
    private final Map<String, Long> calls = new TreeMap<>();
    private double[] temperatures;
    private int reads;

    private long word(EmulatorHelper e, int r) { return e.readRegister("a" + r).longValue() & 0xffffffffL; }
    private double number(EmulatorHelper e, int r) { return Double.longBitsToDouble(word(e, r) | word(e, r + 1) << 32); }
    private void result(EmulatorHelper e, double v) {
        long bits = Double.doubleToRawLongBits(v);
        e.writeRegister("a10", bits & 0xffffffffL); e.writeRegister("a11", bits >>> 32);
    }
    private void count(String name) { calls.merge(name, 1L, Long::sum); }
    private void boundary(EmulatorHelper e, long target) throws Exception {
        double a, b;
        switch ((int) target) {
            case 0x400dd908: count("adc_transport_boundary"); return;
            case 0x400dd770:
                int[] channel = {3, 1, 2, 0, 0};
                int[] type = {2, 1, 1, 0, 3};
                if (reads >= 5 || word(e, 11) != channel[reads] || word(e, 12) != type[reads])
                    throw new IllegalStateException("Unexpected sensor conversion arguments/order");
                count("calibrated_temperature_input"); result(e, temperatures[reads++]); return;
            case 0x401c01a4: count("__floatsidf"); result(e, (int) word(e, 10)); return;
            case 0x4012cb84: count("__adddf3"); result(e, number(e, 10) + number(e, 12)); return;
            case 0x4012ccd8: count("__subdf3"); result(e, number(e, 10) - number(e, 12)); return;
            case 0x4012cf40: count("__muldf3"); result(e, number(e, 10) * number(e, 12)); return;
            case 0x4012d3e0:
                count("__fixdfsi"); a = number(e, 10);
                if (!Double.isFinite(a) || a <= Integer.MIN_VALUE || a >= Integer.MAX_VALUE)
                    throw new IllegalStateException("Input outside defined integer-conversion test domain");
                e.writeRegister("a10", ((long) (int) a) & 0xffffffffL); return;
            case 0x4012d29c:
            case 0x4012d350:
                count(target == 0x4012d29cL ? "__gtdf2" : "__ltdf2");
                a = number(e, 10); b = number(e, 12);
                if (!Double.isFinite(a) || !Double.isFinite(b)) throw new IllegalStateException("Nonfinite comparison");
                e.writeRegister("a10", (a < b ? -1L : a > b ? 1L : 0L) & 0xffffffffL); return;
            case 0x40081388: count("clock"); e.writeRegister("a10", 0L); return;
            case 0x4008c2b4: count("basename"); return;
            case 0x400d21cc: count("diagnostic_log"); e.writeRegister("a10", 0L); return;
            case 0x401c00b4:
                count("__divsf3");
                float q = Float.intBitsToFloat((int) word(e, 10)) / Float.intBitsToFloat((int) word(e, 11));
                e.writeRegister("a10", Float.floatToRawIntBits(q) & 0xffffffffL); return;
            case 0x4012d530: count("__extendsfdf2"); result(e, Float.intBitsToFloat((int) word(e, 10))); return;
            case 0x400db014: throw new IllegalStateException("Original detector assertion fired");
            default: throw new IllegalStateException(String.format("Unmodeled boundary %08x", target));
        }
    }

    private int[] execute(EmulatorHelper e, double[] input) throws Exception {
        temperatures = input; reads = 0;
        for (int i = 0; i < 16; i++) { e.writeRegister("a" + i, 0L); e.writeRegister("f" + i, 0L); }
        e.writeRegister("SAR", 0L); e.writeRegister("PS", 0L);
        e.writeMemory(toAddr(STACK), new byte[4096]);
        e.writeRegister("a1", STACK + 2048); e.writeRegister("a2", STACK + 256); e.writeRegister("a3", OUT);
        e.getEmulator().setExecuteAddress(ENTRY + 3);
        for (int step = 0; step < 2000; step++) {
            monitor.checkCancelled();
            Address pc = e.getExecutionAddress();
            Instruction instruction = currentProgram.getListing().getInstructionAt(pc);
            if (instruction == null) throw new IllegalStateException("Undecoded instruction " + pc);
            String op = instruction.getMnemonicString().toLowerCase(Locale.ROOT);
            if (op.startsWith("loop")) throw new IllegalStateException("Unmodeled Xtensa hardware loop");
            if (op.startsWith("retw")) {
                if (reads != 5) throw new IllegalStateException("Missing temperature reads");
                byte[] bytes = e.readMemory(toAddr(OUT), 4);
                int sensor = (bytes[0] & 255) | (bytes[1] & 255) << 8 | (bytes[2] & 255) << 16 | (bytes[3] & 255) << 24;
                return new int[] {(int) word(e, 2), sensor};
            }
            if (op.equals("call8") || op.equals("callx8")) {
                long target;
                if (op.equals("callx8")) {
                    Register r = instruction.getRegister(0);
                    if (r == null) throw new IllegalStateException("Missing indirect-call register");
                    target = e.readRegister(r).longValue() & 0xffffffffL;
                } else {
                    Address[] destinations = instruction.getFlows();
                    if (destinations.length != 1) throw new IllegalStateException("Ambiguous direct call");
                    target = destinations[0].getOffset();
                }
                boundary(e, target);
                e.getEmulator().setExecuteAddress(pc.getOffset() + instruction.getLength());
            } else if (!e.step(monitor)) throw new IllegalStateException("Execution failed at " + pc + ": " + e.getLastError());
        }
        throw new IllegalStateException("Instruction budget exceeded");
    }

    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 2) throw new IllegalArgumentException("Expected input CSV and output CSV");
        Instruction entry = currentProgram.getListing().getInstructionAt(toAddr(ENTRY));
        if (entry == null || !entry.getMnemonicString().equalsIgnoreCase("entry") || entry.getLength() != 3)
            throw new IllegalStateException("Unexpected original prologue");
        EmulatorHelper e = new EmulatorHelper(currentProgram);
        int cases = 0;
        Map<Integer, Long> errors = new TreeMap<>();
        try (BufferedReader in = Files.newBufferedReader(Path.of(args[0])); BufferedWriter out = Files.newBufferedWriter(Path.of(args[1]))) {
            String header = "ambient,wort,drain,senstech_hex,aet_hex";
            if (!header.equals(in.readLine())) throw new IllegalArgumentException("Unexpected CSV header");
            out.write(header + ",error,sensor_type\n");
            for (String line; (line = in.readLine()) != null;) {
                String[] fields = line.split(",");
                if (fields.length != 5) throw new IllegalArgumentException("Unexpected CSV row");
                double[] values = new double[5];
                for (int i = 0; i < 5; i++) values[i] = Double.parseDouble(fields[i]);
                int[] result = execute(e, values);
                if (result[0] < 0 || result[0] > 8 || result[1] < 0 || result[1] > 4)
                    throw new IllegalStateException("Unexpected original result");
                out.write(line + "," + result[0] + "," + result[1] + "\n");
                errors.merge(result[0], 1L, Long::sum); cases++;
            }
        } finally { e.dispose(); }
        Map<String, Object> report = new LinkedHashMap<>();
        report.put("input_sha256", "d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27");
        report.put("function", "0x400dd9f4"); report.put("cases", cases);
        report.put("scope", "Original decision instructions; explicit calibrated-temperature, SDK arithmetic and diagnostic boundaries; ENTRY/RETW window transitions excluded");
        report.put("sdk_hook_calls", calls); report.put("error_counts", errors);
        report.put("full_cpu_or_hardware_emulation", false); report.put("hardware_tested", false);
        Files.writeString(Path.of(args[1] + ".json"), new GsonBuilder().setPrettyPrinting().create().toJson(report) + "\n");
        println("Executed " + cases + " original heat-exchanger detection cases");
    }
}
