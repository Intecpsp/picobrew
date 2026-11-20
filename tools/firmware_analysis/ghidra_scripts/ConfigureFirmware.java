// Import explicitly described image regions before automatic analysis.
// @category PicoBrew.Analysis

import java.math.BigInteger;
import java.nio.file.Files;
import java.nio.file.Path;
import java.io.InputStream;
import java.util.*;
import com.google.gson.*;
import ghidra.app.util.headless.HeadlessScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;

public class ConfigureFirmware extends HeadlessScript {
    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) throw new IllegalArgumentException("Expected validated layout JSON");
        JsonObject config = JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonObject();
        Path input = Path.of(config.get("input_path").getAsString());
        Memory memory = currentProgram.getMemory();
        for (MemoryBlock block : memory.getBlocks()) memory.removeBlock(block, monitor);
        for (JsonElement value : config.getAsJsonArray("segments")) {
            monitor.checkCancelled();
            JsonObject segment = value.getAsJsonObject();
            long size = segment.get("size").getAsLong();
            if (size == 0) continue;
            Address address = toAddr(segment.get("address").getAsLong());
            MemoryBlock block;
            try (InputStream stream = Files.newInputStream(input)) {
                stream.skipNBytes(segment.get("offset").getAsLong());
                block = memory.createInitializedBlock(segment.get("name").getAsString(), address,
                    stream, size, monitor, false);
            }
            block.setRead(segment.get("read").getAsBoolean());
            block.setWrite(segment.get("write").getAsBoolean());
            block.setExecute(segment.get("execute").getAsBoolean());
            block.setComment("Input offset " + segment.get("offset").getAsLong() +
                "; exact bytes are identified by the export manifest.");
            if (config.get("thumb").getAsBoolean() && block.isExecute()) {
                Register mode = currentProgram.getRegister("TMode");
                if (mode == null) throw new IllegalStateException("Selected language has no Thumb context");
                currentProgram.getProgramContext().setValue(mode, block.getStart(), block.getEnd(), BigInteger.ONE);
            }
        }
        List<Map<String, Object>> seedResults = new ArrayList<>();
        for (JsonElement value : config.getAsJsonArray("labels")) {
            JsonObject label = value.getAsJsonObject();
            Address address = toAddr(label.get("address").getAsLong());
            String name = label.get("name").getAsString();
            Map<String, Object> result = new LinkedHashMap<>();
            result.put("address", String.format("0x%08x", address.getOffset()));
            result.put("name", name);
            try {
                currentProgram.getSymbolTable().createLabel(address, name, SourceType.USER_DEFINED);
                if (label.get("function").getAsBoolean()) {
                    disassemble(address);
                    Function function = getFunctionAt(address);
                    if (function == null) function = createFunction(address, name);
                    if (function != null) function.setName(name, SourceType.USER_DEFINED);
                    result.put("function_created", function != null);
                }
            } catch (Exception error) {
                result.put("error", error.getClass().getSimpleName() + ": " + error.getMessage());
            }
            seedResults.add(result);
        }
        for (JsonElement value : config.getAsJsonArray("entry_points")) {
            Address address = toAddr(value.getAsLong());
            currentProgram.getSymbolTable().addExternalEntryPoint(address);
            disassemble(address);
            if (getFunctionAt(address) == null) createFunction(address, null);
        }
        storeHeadlessValue("firmware_seed_results", seedResults);
        println("Configured " + memory.getBlocks().length + " explicit image segments");
    }
}
