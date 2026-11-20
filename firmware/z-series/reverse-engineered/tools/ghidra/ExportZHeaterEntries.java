// Recover original heater constructor/callback entries omitted by initial analysis.
// Run read-only; this adds function definitions only to the transient program.
// @category PicoBrew.Analysis
import java.nio.file.*;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;

public class ExportZHeaterEntries extends GhidraScript {
    private static String hex(Address address) { return String.format("0x%08x", address.getOffset()); }
    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) throw new IllegalArgumentException("Expected output directory");
        String original = "d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27";
        if (!original.equals(currentProgram.getExecutableSHA256())) throw new IllegalStateException("Original program identity mismatch");
        Path output = Path.of(args[0]);
        Files.createDirectories(output);
        Gson json = new GsonBuilder().setPrettyPrinting().create();
        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(false);
        if (!decompiler.openProgram(currentProgram)) throw new IllegalStateException(decompiler.getLastMessage());
        List<Map<String, Object>> records = new ArrayList<>();
        try {
            for (long entry : new long[] {0x400d001cL, 0x40080540L, 0x40080570L, 0x400805dcL}) {
                Address address = toAddr(entry);
                if (!disassemble(address)) throw new IllegalStateException("Cannot disassemble " + address);
                Function function = getFunctionAt(address);
                if (function == null) function = createFunction(address, "callback_" + Long.toHexString(entry));
                if (function == null) throw new IllegalStateException("Cannot define function " + address);
                Instruction first = currentProgram.getListing().getInstructionAt(address);
                if (first == null || !first.getMnemonicString().equalsIgnoreCase("entry"))
                    throw new IllegalStateException("Unexpected callback prologue");
                DecompileResults result = decompiler.decompileFunction(function, 60, monitor);
                if (!result.decompileCompleted() || result.getDecompiledFunction() == null)
                    throw new IllegalStateException("Decompilation failed " + address + ": " + result.getErrorMessage());
                String name = "fn_" + Long.toHexString(entry);
                Files.writeString(output.resolve(name + ".c"), "/* GENERATED EVIDENCE, not original or buildable source. */\n" + result.getDecompiledFunction().getC());
                List<Map<String, Object>> instructions = new ArrayList<>();
                InstructionIterator iterator = currentProgram.getListing().getInstructions(function.getBody(), true);
                while (iterator.hasNext()) {
                    Instruction instruction = iterator.next();
                    Map<String, Object> item = new LinkedHashMap<>();
                    item.put("address", hex(instruction.getAddress()));
                    item.put("bytes", HexFormat.of().formatHex(instruction.getBytes()));
                    item.put("mnemonic", instruction.getMnemonicString());
                    List<String> operands = new ArrayList<>();
                    for (int i = 0; i < instruction.getNumOperands(); i++) operands.add(instruction.getDefaultOperandRepresentation(i));
                    item.put("operands", operands);
                    List<String> flows = new ArrayList<>();
                    for (Address flow : instruction.getFlows()) flows.add(hex(flow));
                    item.put("flows", flows);
                    instructions.add(item);
                }
                Files.writeString(output.resolve(name + ".instructions.json"), json.toJson(instructions) + "\n");
                Map<String, Object> record = new LinkedHashMap<>();
                record.put("address", hex(address)); record.put("instructions", instructions.size());
                List<String> ranges = new ArrayList<>();
                for (AddressRange range : function.getBody().getAddressRanges(true)) ranges.add(hex(range.getMinAddress()) + ".." + hex(range.getMaxAddress()));
                record.put("ranges", ranges); records.add(record);
            }
        } finally { decompiler.dispose(); }
        Map<String, Object> report = new LinkedHashMap<>();
        report.put("input_sha256", original); report.put("language", currentProgram.getLanguageID().toString());
        report.put("entries", records); report.put("behaviorally_verified", false); report.put("hardware_tested", false);
        Files.writeString(output.resolve("manifest.json"), json.toJson(report) + "\n");
        println("Exported " + records.size() + " original callback entries");
    }
}
