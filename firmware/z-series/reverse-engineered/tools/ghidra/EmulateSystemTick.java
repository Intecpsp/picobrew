// Execute the original System watchdog/timing body; SDK and logging are boundaries.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.security.MessageDigest;
import java.io.*;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;

public class EmulateSystemTick extends GhidraScript {
    private static final long SYSTEM = 0x3ff30000L, STATE = 0x3ff31000L;
    private long watchdog, timestamp, sampled;
    private List<Long> events;
    private static String sha(byte[] bytes) throws Exception {
        return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));
    }
    private void boundary(Z119Execution e, long target) throws Exception {
        if (e.arithmetic(target)) return;
        switch ((int) target) {
        case 0x40136e8c:
            e.countBoundary("esp_task_wdt_reset"); events.add(1L); e.register(10, watchdog); return;
        case 0x40081388:
            e.countBoundary("diagnostic_millis"); events.add(2L); events.add(timestamp); sampled = timestamp;
            e.register(10, timestamp); return;
        case 0x4008c2b4:
            e.countBoundary("diagnostic_basename"); return;
        case 0x400d21cc:
            e.countBoundary("diagnostic_log");
            if (e.register(10) == 0x3f403d1dL) {
                events.add(4L); events.add(sampled); events.add(e.register(15));
            } else if (e.register(10) == 0x3f408d6eL) {
                events.add(3L); events.add(sampled); events.add(e.read(e.register(1), 4));
            } else throw new IllegalStateException("Unknown diagnostic format");
            return;
        case 0x400db014:
            // The actual helper returns after logging, incrementing the global
            // counter and waiting. Execute it; do not model a terminating trap.
            e.invoke(target); return;
        case 0x401289a0:
            e.countBoundary("delay"); events.add(5L); events.add(e.register(10));
            timestamp = (timestamp + e.register(10)) & 0xffffffffL; return;
        default:
            throw new IllegalStateException(String.format("Unreviewed System tick call %08x", target));
        }
    }
    private Map<String,Object> one(Z119Execution e, JsonObject input) throws Exception {
        e.resetRegisters(); e.clear(SYSTEM, 0x100); e.clear(STATE, 0x100);
        e.write(SYSTEM + 0x8c, STATE, 4);
        e.write(0x3ffc2e84L, input.get("assertions").getAsLong(), 4);
        JsonArray words = input.getAsJsonArray("words");
        for (int i = 0; i < 4; i++) e.write(STATE + 0x48 + i * 4, words.get(i).getAsLong(), 4);
        e.write(STATE + 0x58, Long.parseUnsignedLong(input.get("average_bits").getAsString(), 16), 8);
        List<Map<String,Object>> snapshots = new ArrayList<>();
        for (JsonElement value : input.getAsJsonArray("ticks")) {
            JsonArray tick = value.getAsJsonArray();
            long now = tick.get(0).getAsLong(); watchdog = tick.get(1).getAsLong(); timestamp = tick.get(2).getAsLong();
            events = new ArrayList<>(); sampled = 0;
            e.register(10, SYSTEM); e.register(11, now);
            e.invoke(0x400e5e2cL);
            Map<String,Object> snapshot = new LinkedHashMap<>();
            List<Long> updated = new ArrayList<>();
            for (int i = 0; i < 4; i++) updated.add(e.read(STATE + 0x48 + i * 4, 4));
            snapshot.put("words", updated);
            snapshot.put("average_bits", String.format("%016x", e.read(STATE + 0x58, 8)));
            snapshot.put("result", 0); snapshot.put("watchdog_error", (int) watchdog);
            snapshot.put("assertions", e.read(0x3ffc2e84L, 4));
            snapshot.put("events", events); snapshots.add(snapshot);
        }
        Map<String,Object> output = new LinkedHashMap<>();
        output.put("id", input.get("id").getAsString()); output.put("snapshots", snapshots); return output;
    }
    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 2) throw new IllegalArgumentException("Expected input JSON and output JSONL");
        JsonArray inputs = JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();
        Gson gson = new Gson(); int count = 0;
        try (Z119Execution e = new Z119Execution(currentProgram, monitor, this::boundary);
             BufferedWriter output = Files.newBufferedWriter(Path.of(args[1]))) {
            for (JsonElement input : inputs) {
                try { output.write(gson.toJson(one(e, input.getAsJsonObject())) + "\n"); }
                catch (Exception failure) { throw new IllegalStateException("Case " + count, failure); }
                ++count;
            }
            Map<String,Object> report = new LinkedHashMap<>();
            report.put("input_sha256", Z119Execution.IMAGE_SHA); report.put("cases", count);
            report.put("input_file_sha256", sha(Files.readAllBytes(Path.of(args[0]))));
            report.put("original_functions_executed", e.functions); report.put("modeled_boundaries", e.boundaries);
            report.put("hardware_tested", false);
            report.put("scope", "Original System::tick and returning assertion-helper instructions, including its global counter increment and continuation. Watchdog, clock, compiler arithmetic, diagnostic formatting and delay are explicit boundaries. Exact integer state, double bits, assertion counter and ordered watchdog/clock/log/delay arguments are compared. No full Core, real watchdog, interrupt or hardware execution claim.");
            Files.writeString(Path.of(args[1] + ".json"), new GsonBuilder().setPrettyPrinting().create().toJson(report) + "\n");
        }
        println("Executed " + count + " original System tick cases");
    }
}
