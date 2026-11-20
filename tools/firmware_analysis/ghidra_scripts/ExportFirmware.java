// Export decompiler evidence, never a claimed buildable source reconstruction.
// @category PicoBrew.Analysis

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.*;
import com.google.gson.*;
import ghidra.app.decompiler.*;
import ghidra.app.util.headless.HeadlessScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.*;
import ghidra.framework.options.Options;

public class ExportFirmware extends HeadlessScript {
    private static final Gson JSON = new GsonBuilder().setPrettyPrinting().disableHtmlEscaping().create();
    private static String hex(Address address) { return String.format("0x%08x", address.getOffset()); }
    private static void write(Path path, Object value) throws Exception {
        Files.writeString(path, JSON.toJson(value) + "\n", StandardCharsets.UTF_8);
    }
    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 3) throw new IllegalArgumentException("Expected output, function timeout, maximum functions");
        Path output = Path.of(args[0]);
        int timeout = Integer.parseInt(args[1]);
        int limit = Integer.parseInt(args[2]);
        Files.createDirectories(output.resolve("decompiled"));
        Files.createDirectories(output.resolve("instructions"));
        DecompInterface decompiler = new DecompInterface();
        DecompileOptions options = new DecompileOptions();
        options.grabFromProgram(currentProgram);
        decompiler.setOptions(options);
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(false);
        if (!decompiler.openProgram(currentProgram)) throw new IllegalStateException(decompiler.getLastMessage());
        List<Map<String, Object>> functions = new ArrayList<>();
        int complete = 0, failed = 0, unresolved = 0;
        try {
            FunctionIterator iterator = currentProgram.getFunctionManager().getFunctions(true);
            while (iterator.hasNext()) {
                monitor.checkCancelled();
                Function function = iterator.next();
                if (function.isExternal()) continue;
                if (functions.size() >= limit) throw new IllegalStateException("Function export limit exceeded");
                Address entry = function.getEntryPoint();
                Map<String, Object> record = new LinkedHashMap<>();
                record.put("address", hex(entry));
                record.put("name", function.getName());
                record.put("signature", function.getPrototypeString(false, false));
                record.put("symbol_source", function.getSymbol().getSource().toString());
                boolean unknown = function.getName().startsWith("FUN_") || function.getName().startsWith("thunk_FUN_") ||
                    function.getName().startsWith("unresolved_");
                record.put("name_unresolved", unknown);
                if (unknown) unresolved++;
                record.put("thunk", function.isThunk());
                List<Map<String, Object>> ranges = new ArrayList<>();
                AddressRangeIterator rangeIterator = function.getBody().getAddressRanges(true);
                while (rangeIterator.hasNext()) {
                    AddressRange range = rangeIterator.next();
                    Map<String, Object> body = new LinkedHashMap<>();
                    body.put("start", hex(range.getMinAddress()));
                    body.put("end", hex(range.getMaxAddress()));
                    ranges.add(body);
                }
                record.put("ranges", ranges);
                List<Map<String, Object>> instructions = new ArrayList<>();
                InstructionIterator instructionIterator = currentProgram.getListing().getInstructions(function.getBody(), true);
                while (instructionIterator.hasNext()) {
                    Instruction instruction = instructionIterator.next();
                    Map<String, Object> decoded = new LinkedHashMap<>();
                    decoded.put("address", hex(instruction.getAddress()));
                    decoded.put("bytes", HexFormat.of().formatHex(instruction.getBytes()));
                    decoded.put("mnemonic", instruction.getMnemonicString());
                    List<String> operands = new ArrayList<>();
                    for (int operand = 0; operand < instruction.getNumOperands(); operand++) {
                        operands.add(instruction.getDefaultOperandRepresentation(operand));
                    }
                    decoded.put("operands", operands);
                    TreeSet<String> flows = new TreeSet<>();
                    for (Address flow : instruction.getFlows()) flows.add(hex(flow));
                    decoded.put("flows", flows);
                    List<Map<String, Object>> references = new ArrayList<>();
                    for (Reference reference : instruction.getReferencesFrom()) {
                        Map<String, Object> target = new LinkedHashMap<>();
                        target.put("type", reference.getReferenceType().toString());
                        target.put("operand", reference.getOperandIndex());
                        target.put("target", reference.getToAddress().isMemoryAddress() ?
                            hex(reference.getToAddress()) : reference.getToAddress().toString());
                        references.add(target);
                    }
                    decoded.put("references", references);
                    instructions.add(decoded);
                }
                String instructionFile = "instructions/fn_" + String.format("%08x", entry.getOffset()) + ".json";
                write(output.resolve(instructionFile), instructions);
                record.put("instructions_file", instructionFile);
                record.put("instruction_count", instructions.size());
                TreeSet<String> callees = new TreeSet<>();
                for (Function callee : function.getCalledFunctions(monitor)) callees.add(hex(callee.getEntryPoint()));
                record.put("calls", callees);
                TreeSet<String> callers = new TreeSet<>();
                for (Function caller : function.getCallingFunctions(monitor)) callers.add(hex(caller.getEntryPoint()));
                record.put("callers", callers);
                DecompileResults result = decompiler.decompileFunction(function, timeout, monitor);
                String message = result.getErrorMessage();
                record.put("decompiler_message", message == null ? "" : message);
                if (result.decompileCompleted() && result.getDecompiledFunction() != null) {
                    String filename = "decompiled/fn_" + String.format("%08x", entry.getOffset()) + ".c";
                    String header = "/* GENERATED DECOMPILER EVIDENCE: not original or buildable source.\n" +
                        " * Input function " + hex(entry) + "; inferred name " + function.getName().replace("*/", "* /") + ".\n" +
                        " * Types, control flow, signatures and names require manual validation.\n */\n\n";
                    Files.writeString(output.resolve(filename), header + result.getDecompiledFunction().getC(), StandardCharsets.UTF_8);
                    record.put("decompilation", "completed");
                    record.put("file", filename);
                    complete++;
                } else {
                    record.put("decompilation", result.isTimedOut() ? "timeout" : "failed");
                    failed++;
                }
                functions.add(record);
            }
        } finally { decompiler.dispose(); }
        write(output.resolve("functions.json"), functions);
        List<Map<String, Object>> strings = new ArrayList<>();
        DataIterator dataIterator = currentProgram.getListing().getDefinedData(true);
        while (dataIterator.hasNext()) {
            Data data = dataIterator.next();
            Object value = data.getValue();
            if (!(value instanceof String)) continue;
            Map<String, Object> record = new LinkedHashMap<>();
            record.put("address", hex(data.getAddress()));
            record.put("text", value);
            record.put("length", data.getLength());
            TreeSet<String> refs = new TreeSet<>();
            ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(data.getAddress());
            while (references.hasNext()) refs.add(hex(references.next().getFromAddress()));
            record.put("references", refs);
            strings.add(record);
        }
        write(output.resolve("defined-strings.json"), strings);
        List<Map<String, Object>> blocks = new ArrayList<>();
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            Map<String, Object> record = new LinkedHashMap<>();
            record.put("name", block.getName());
            record.put("address", hex(block.getStart()));
            record.put("size", block.getSize());
            record.put("read", block.isRead());
            record.put("write", block.isWrite());
            record.put("execute", block.isExecute());
            blocks.add(record);
        }
        Map<String, Object> summary = new LinkedHashMap<>();
        summary.put("schema_version", 1);
        summary.put("language_id", currentProgram.getLanguageID().toString());
        summary.put("compiler_spec", currentProgram.getCompilerSpec().getCompilerSpecID().toString());
        summary.put("analysis_timed_out", analysisTimeoutOccurred());
        summary.put("functions_recognized", functions.size());
        summary.put("decompilation_completed", complete);
        summary.put("decompilation_failed", failed);
        summary.put("unresolved_names", unresolved);
        summary.put("function_inventory_exhaustive", false);
        summary.put("buildable_source", false);
        summary.put("behaviorally_verified", false);
        summary.put("hardware_tested", false);
        summary.put("segments", blocks);
        summary.put("seeds", getStoredHeadlessValue("firmware_seed_results"));
        TreeMap<String, String> analysisSettings = new TreeMap<>();
        Options analysis = currentProgram.getOptions(Program.ANALYSIS_PROPERTIES);
        for (String name : analysis.getOptionNames()) {
            Object value = analysis.getObject(name, null);
            if (value instanceof String || value instanceof Number || value instanceof Boolean || value instanceof Enum<?>) {
                analysisSettings.put(name, value.toString());
            }
        }
        summary.put("analysis_settings", analysisSettings);
        write(output.resolve("ghidra-summary.json"), summary);
        println("Exported " + functions.size() + " recognized functions; " + failed + " decompiler failures");
    }
}
