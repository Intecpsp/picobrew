// Define evidence-backed callback candidates in a transient read-only program.
// @category PicoBrew.Analysis
import java.nio.file.*;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;

public class DiscoverZCallbacks extends GhidraScript {
    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 2) throw new IllegalArgumentException("Expected candidate catalog and output report");
        JsonObject catalog = JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonObject();
        if (!catalog.get("input_sha256").getAsString().equals(currentProgram.getExecutableSHA256()))
            throw new IllegalStateException("Original program identity mismatch");
        List<Map<String, Object>> records = new ArrayList<>();
        int created = 0, existing = 0, overlaps = 0, rejected = 0;
        for (JsonElement element : catalog.getAsJsonArray("new_candidates")) {
            monitor.checkCancelled();
            JsonObject candidate = element.getAsJsonObject();
            if (!candidate.get("seed_from_pointer_evidence").getAsBoolean()) continue;
            String spelling = candidate.get("address").getAsString();
            Address address = toAddr(Long.decode(spelling));
            Map<String, Object> record = new LinkedHashMap<>();
            record.put("address", spelling);
            Function at = getFunctionAt(address), containing = getFunctionContaining(address);
            if (at != null) { record.put("status", "already_defined"); existing++; }
            else if (containing != null) {
                record.put("status", "inside_existing_function_manual_review_required");
                record.put("containing_function", containing.getEntryPoint().toString()); overlaps++;
            } else {
                boolean decoded = disassemble(address);
                Instruction instruction = currentProgram.getListing().getInstructionAt(address);
                if (!decoded || instruction == null || !instruction.getMnemonicString().equalsIgnoreCase("entry") ||
                        !instruction.getDefaultOperandRepresentation(0).equals("a1")) {
                    record.put("status", "prologue_not_confirmed"); rejected++;
                } else {
                    at = createFunction(address, "candidate_" + spelling.substring(2));
                    if (at == null) { record.put("status", "function_definition_failed"); rejected++; }
                    else {
                        record.put("status", "defined_from_address_taken_entry");
                        record.put("body_bytes", at.getBody().getNumAddresses()); created++;
                    }
                }
            }
            records.add(record);
        }
        Map<String, Object> report = new LinkedHashMap<>();
        report.put("input_sha256", catalog.get("input_sha256").getAsString());
        report.put("candidates_processed", records.size()); report.put("new_functions_defined", created);
        report.put("already_defined", existing); report.put("overlapping_candidates", overlaps); report.put("rejected_candidates", rejected);
        report.put("scope", "Discovery definitions only; function boundaries and behavior still require independent review; no existing function was split");
        report.put("function_inventory_exhaustive", false); report.put("behaviorally_verified", false);
        report.put("results", records);
        Files.writeString(Path.of(args[1]), new GsonBuilder().setPrettyPrinting().create().toJson(report) + "\n");
        println("Defined " + created + " address-taken ENTRY candidates; " + overlaps + " overlaps retained for manual review");
    }
}
