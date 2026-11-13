// Bounded numeric operand discovery in an already analyzed, read-only program.
// This does not infer a receiver type, queue ownership, or a safe native thread.
// @category BC2.Offline

import java.io.Reader;
import java.io.Writer;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.time.Instant;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;

import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonNull;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import ghidra.app.util.headless.HeadlessScript;
import ghidra.framework.Application;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.scalar.Scalar;

public class SearchBc2MemberReferences extends HeadlessScript {
    private static final int TEXT_LIMIT = 512;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 2 || currentProgram == null) {
            throw new IllegalArgumentException("Expected invocation.json and a new output.json, with one program open");
        }
        JsonObject request;
        try (Reader input = Files.newBufferedReader(Path.of(args[0]), StandardCharsets.UTF_8)) {
            request = JsonParser.parseReader(input).getAsJsonObject();
        }
        if (!"bc2.ghidra-member-invocation.v1".equals(request.get("schema").getAsString()) ||
                !"cached_readonly_member_search".equals(request.get("mode").getAsString()) ||
                !currentProgram.getExecutableSHA256().equalsIgnoreCase(request.get("executable_sha256").getAsString()) ||
                analysisTimeoutOccurred()) {
            throw new IllegalArgumentException("A matching completed cached analysis and read-only member invocation are required");
        }
        JsonArray offsets = request.getAsJsonArray("member_offsets");
        Set<Long> values = new HashSet<>();
        if (offsets.size() < 1 || offsets.size() > 4) {
            throw new IllegalArgumentException("Exactly 1..4 member offsets are required");
        }
        for (var item : offsets) {
            String value = item.getAsString();
            if (!value.matches("0x[0-9a-f]{1,8}") || !values.add(Long.parseUnsignedLong(value.substring(2), 16))) {
                throw new IllegalArgumentException("Expected distinct normalized unsigned 32-bit offsets");
            }
        }
        int maxInstructions = limit(request, "max_instructions", 5_000_000);
        int maxMatches = limit(request, "max_matches", 1_024);
        int maxFunctions = limit(request, "max_functions", 256);
        int seconds = limit(request, "search_seconds", 120);
        Address start = address(request.get("search_start_address").getAsString());
        Address end = address(request.get("search_end_address").getAsString());
        if (start.compareTo(end) > 0) {
            throw new IllegalArgumentException("Search start must not exceed inclusive end");
        }
        Path output = Path.of(args[1]).toAbsolutePath().normalize();
        if (!output.equals(Path.of(request.get("output_path").getAsString()).toAbsolutePath().normalize()) || Files.exists(output)) {
            throw new IllegalArgumentException("Output must match invocation and must not already exist");
        }

        long began = System.nanoTime();
        long budget = seconds * 1_000_000_000L;
        int examined = 0;
        Address lastExamined = null;
        Address nextScan = null;
        String stopReason = "range_exhausted";
        JsonArray matches = new JsonArray();
        Map<String, JsonObject> functions = new LinkedHashMap<>();
        InstructionIterator instructions = currentProgram.getListing().getInstructions(new AddressSet(start, end), true);
        while (instructions.hasNext()) {
            monitor.checkCancelled();
            Instruction instruction = instructions.next();
            if (examined >= maxInstructions || matches.size() >= maxMatches || System.nanoTime() - began >= budget) {
                nextScan = instruction.getAddress();
                stopReason = examined >= maxInstructions ? "instruction_limit" :
                    matches.size() >= maxMatches ? "match_limit" : "time_limit";
                break;
            }
            examined++;
            lastExamined = instruction.getAddress();
            JsonArray operands = matchingOperands(instruction, values);
            if (operands == null) {
                continue;
            }
            Function function = currentProgram.getFunctionManager().getFunctionContaining(instruction.getAddress());
            String functionKey = function == null ? null : function.getEntryPoint().toString();
            if (functionKey != null && !functions.containsKey(functionKey) && functions.size() >= maxFunctions) {
                // This matching instruction was examined but not emitted; resume at it.
                nextScan = instruction.getAddress();
                stopReason = "function_limit";
                break;
            }
            JsonObject row = new JsonObject();
            row.addProperty("instruction_address", instruction.getAddress().toString());
            text(row, "instruction", instruction.toString());
            row.addProperty("mnemonic", instruction.getMnemonicString());
            row.add("matching_operands", operands);
            if (function != null) {
                row.addProperty("function_address", functionKey);
                if (!functions.containsKey(functionKey)) {
                    JsonObject candidate = new JsonObject();
                    candidate.addProperty("function_address", functionKey);
                    text(candidate, "name", function.getName(true));
                    candidate.addProperty("is_thunk", function.isThunk());
                    candidate.addProperty("matching_instructions", 0);
                    functions.put(functionKey, candidate);
                }
                JsonObject candidate = functions.get(functionKey);
                candidate.addProperty("matching_instructions", candidate.get("matching_instructions").getAsInt() + 1);
            } else {
                row.add("function_address", JsonNull.INSTANCE);
            }
            matches.add(row);
        }

        JsonObject result = new JsonObject();
        result.addProperty("schema", "bc2.ghidra-member-search.v1");
        result.addProperty("tool_version", "1.1");
        result.addProperty("created_utc", Instant.now().toString());
        result.addProperty("status", nextScan == null ? "search_complete" : "search_truncated");
        result.addProperty("executable_sha256", currentProgram.getExecutableSHA256().toLowerCase());
        result.addProperty("ghidra_version", Application.getApplicationVersion());
        result.addProperty("java_version", System.getProperty("java.version"));
        result.addProperty("program_name", currentProgram.getName());
        result.addProperty("image_base", currentProgram.getImageBase().toString());
        result.addProperty("language", currentProgram.getLanguageID().toString());
        result.addProperty("scope", "Scalar unsigned-value equality in existing instruction operands; numeric candidates only, not receiver/type/ownership proof");
        result.addProperty("truncated", nextScan != null);
        result.addProperty("stop_reason", stopReason);
        result.addProperty("instructions_examined", examined);
        result.addProperty("elapsed_seconds", (System.nanoTime() - began) / 1_000_000_000.0);
        result.addProperty("text_character_limit", TEXT_LIMIT);
        result.add("last_examined_address", lastExamined == null ? JsonNull.INSTANCE :
            new com.google.gson.JsonPrimitive(lastExamined.toString()));
        result.add("next_scan_address", nextScan == null ? JsonNull.INSTANCE :
            new com.google.gson.JsonPrimitive("0x" + Long.toUnsignedString(nextScan.getOffset(), 16)));
        JsonArray candidates = new JsonArray();
        for (JsonObject candidate : functions.values()) {
            candidates.add(candidate);
        }
        result.add("functions", candidates);
        result.add("matches", matches);
        result.add("invocation", request);
        try (Writer writer = Files.newBufferedWriter(output, StandardCharsets.UTF_8,
                StandardOpenOption.CREATE_NEW, StandardOpenOption.WRITE)) {
            new GsonBuilder().serializeNulls().setPrettyPrinting().disableHtmlEscaping().create().toJson(result, writer);
        }
        println("Member search emitted " + matches.size() + " instructions; " + stopReason + "; " + output);
    }

    private int limit(JsonObject request, String key, int maximum) {
        int value = request.get(key).getAsInt();
        if (value < 1 || value > maximum) {
            throw new IllegalArgumentException("Out-of-range search limit: " + key);
        }
        return value;
    }

    private Address address(String value) {
        if (!value.matches("0x[0-9a-f]{1,16}")) {
            throw new IllegalArgumentException("Expected normalized hexadecimal search address");
        }
        return currentProgram.getAddressFactory().getDefaultAddressSpace()
            .getAddress(Long.parseUnsignedLong(value.substring(2), 16));
    }

    private void text(JsonObject row, String name, String value) {
        row.addProperty(name, value.substring(0, Math.min(TEXT_LIMIT, value.length())));
        row.addProperty(name + "_truncated", value.length() > TEXT_LIMIT);
    }

    private JsonArray matchingOperands(Instruction instruction, Set<Long> values) {
        JsonArray matches = null;
        for (int index = 0; index < instruction.getNumOperands(); index++) {
            Object[] objects = instruction.getOpObjects(index);
            boolean matched = false;
            for (Object object : objects) {
                if (object instanceof Scalar scalar && values.contains(scalar.getUnsignedValue())) {
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                continue;
            }
            JsonArray scalars = new JsonArray();
            JsonArray registers = new JsonArray();
            for (Object object : objects) {
                if (object instanceof Scalar scalar && values.contains(scalar.getUnsignedValue())) {
                    JsonObject match = new JsonObject();
                    match.addProperty("unsigned_hex", "0x" + Long.toUnsignedString(scalar.getUnsignedValue(), 16));
                    match.addProperty("signed_value", scalar.getSignedValue());
                    match.addProperty("bit_length", scalar.bitLength());
                    scalars.add(match);
                } else if (object instanceof Register register) {
                    registers.add(register.getName());
                }
            }
            if (scalars.size() > 0) {
                if (matches == null) {
                    matches = new JsonArray();
                }
                JsonObject operand = new JsonObject();
                operand.addProperty("index", index);
                operand.addProperty("type_flags", instruction.getOperandType(index));
                text(operand, "representation", instruction.getDefaultOperandRepresentation(index));
                operand.add("matching_scalars", scalars);
                operand.add("registers", registers);
                matches.add(operand);
            }
        }
        return matches;
    }
}
