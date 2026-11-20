// Execute the original Z119 thermistor instruction body with explicit SDK boundaries.
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

public class EmulateTemperature extends GhidraScript {
    private static final long ENTRY = 0x400d6ac0L;
    private static final long PARAMETERS = 0x3f4287f0L;
    private static final long STACK = 0x3ff90000L;
    private final Map<String, Long> hookCalls = new TreeMap<>();

    private long word(EmulatorHelper emulator, int register) {
        return emulator.readRegister("a" + register).longValue() & 0xffffffffL;
    }
    private double number(EmulatorHelper emulator, int register) {
        return Double.longBitsToDouble(word(emulator, register) | (word(emulator, register + 1) << 32));
    }
    private void result(EmulatorHelper emulator, double value) {
        long bits = Double.doubleToRawLongBits(value);
        emulator.writeRegister("a10", bits & 0xffffffffL);
        emulator.writeRegister("a11", bits >>> 32);
    }
    private void count(String hook) { hookCalls.merge(hook, 1L, Long::sum); }

    private void boundary(EmulatorHelper emulator, long target) throws Exception {
        double a, b;
        switch ((int) target) {
            case 0x401c01a4: count("__floatsidf"); result(emulator, (int) word(emulator, 10)); return;
            case 0x4012cb84: count("__adddf3"); result(emulator, number(emulator, 10) + number(emulator, 12)); return;
            case 0x4012ccd8: count("__subdf3"); result(emulator, number(emulator, 10) - number(emulator, 12)); return;
            case 0x4012cf40: count("__muldf3"); result(emulator, number(emulator, 10) * number(emulator, 12)); return;
            case 0x4012d144: count("__divdf3"); result(emulator, number(emulator, 10) / number(emulator, 12)); return;
            case 0x4013b288: count("log"); result(emulator, Math.log(number(emulator, 10))); return;
            case 0x40081388: count("clock"); emulator.writeRegister("a10", 0L); return;
            case 0x4008c2b4: count("basename"); return;
            case 0x400d21cc: count("diagnostic_log"); emulator.writeRegister("a10", 0L); return;
            case 0x401c00b4:
                count("__divsf3");
                float quotient = Float.intBitsToFloat((int) word(emulator, 10)) / Float.intBitsToFloat((int) word(emulator, 11));
                emulator.writeRegister("a10", Float.floatToRawIntBits(quotient) & 0xffffffffL); return;
            case 0x4012d530:
                count("__extendsfdf2"); result(emulator, Float.intBitsToFloat((int) word(emulator, 10))); return;
            default: throw new IllegalStateException(String.format("Unmodeled call boundary 0x%08x", target));
        }
    }

    private double execute(EmulatorHelper emulator, int sensor, int adc) throws Exception {
        for (int i = 0; i < 16; i++) emulator.writeRegister("a" + i, 0L);
        for (int i = 0; i < 16; i++) emulator.writeRegister("f" + i, 0L);
        emulator.writeRegister("SAR", 0L);
        emulator.writeRegister("PS", 0L);
        emulator.writeMemory(toAddr(STACK), new byte[4096]);
        // We begin after ENTRY with an already allocated frame. No register
        // window is claimed to be emulated; RETW is likewise the stop boundary.
        emulator.writeRegister("a1", STACK + 2048);
        emulator.writeRegister("a2", PARAMETERS + sensor * 40L);
        emulator.writeRegister("a3", adc & 0xffffffffL);
        emulator.getEmulator().setExecuteAddress(ENTRY + 3);
        for (int steps = 0; steps < 1000; steps++) {
            monitor.checkCancelled();
            Address pc = emulator.getExecutionAddress();
            Instruction instruction = currentProgram.getListing().getInstructionAt(pc);
            if (instruction == null) throw new IllegalStateException("No decoded instruction at " + pc);
            String mnemonic = instruction.getMnemonicString().toLowerCase(Locale.ROOT);
            if (mnemonic.startsWith("retw")) return number(emulator, 2);
            if (mnemonic.equals("callx8") || mnemonic.equals("call8")) {
                long target;
                if (mnemonic.equals("callx8")) {
                    Register register = instruction.getRegister(0);
                    if (register == null) throw new IllegalStateException("Missing indirect call register");
                    target = emulator.readRegister(register).longValue() & 0xffffffffL;
                } else {
                    Address[] destinations = instruction.getFlows();
                    if (destinations.length != 1) throw new IllegalStateException("Ambiguous direct call");
                    target = destinations[0].getOffset();
                }
                boundary(emulator, target);
                emulator.getEmulator().setExecuteAddress(pc.getOffset() + instruction.getLength());
                continue;
            }
            if (!emulator.step(monitor)) throw new IllegalStateException("Instruction failed at " + pc + ": " + emulator.getLastError());
        }
        throw new IllegalStateException("Original temperature function exceeded instruction budget");
    }

    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 2) throw new IllegalArgumentException("Expected output CSV and maximum raw ADC (0..4095)");
        Path output = Path.of(args[0]);
        int maximum = Integer.parseInt(args[1]);
        if (maximum < 0 || maximum > 4095) throw new IllegalArgumentException("Invalid ADC extent");
        Instruction entry = currentProgram.getListing().getInstructionAt(toAddr(ENTRY));
        if (entry == null || !entry.getMnemonicString().equalsIgnoreCase("entry") || entry.getLength() != 3) {
            throw new IllegalStateException("Unexpected original function prologue");
        }
        EmulatorHelper emulator = new EmulatorHelper(currentProgram);
        int cases = 0;
        try (BufferedWriter writer = Files.newBufferedWriter(output)) {
            writer.write("sensor_type,raw_adc,kelvin\n");
            for (int sensor = 0; sensor < 4; sensor++) {
                for (int adc = 0; adc <= maximum; adc++) {
                    double value = execute(emulator, sensor, adc);
                    if (!Double.isFinite(value)) throw new IllegalStateException("Nonfinite temperature result");
                    writer.write(sensor + "," + adc + "," + Double.toString(value) + "\n");
                    cases++;
                }
            }
        } finally { emulator.dispose(); }
        Map<String, Object> report = new LinkedHashMap<>();
        report.put("input_sha256", "d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27");
        report.put("function", "0x400d6ac0");
        report.put("cases", cases);
        report.put("sdk_hook_calls", hookCalls);
        report.put("scope", "Original Xtensa instruction body; explicit SDK arithmetic/log/diagnostic boundaries; register-window prologue and return excluded");
        report.put("full_cpu_or_hardware_emulation", false);
        report.put("hardware_tested", false);
        Files.writeString(Path.of(args[0] + ".json"), new GsonBuilder().setPrettyPrinting().create().toJson(report) + "\n");
        println("Executed " + cases + " original temperature cases with explicit SDK boundaries");
    }
}
