// Export only explicitly requested functions; never create/patch code or follow callees.
// @category BC2.Offline

import java.io.Reader;
import java.io.Writer;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.time.Instant;
import java.util.HashSet;
import java.util.Set;

import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.util.headless.HeadlessScript;
import ghidra.framework.Application;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressOutOfBoundsException;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class ExportBc2Functions extends HeadlessScript {
    private static final int MAX_FUNCTIONS = 16;
    private static final int MAX_REFERENCES = 256;
    private static final int MAX_C_CHARACTERS = 1_000_000;
    private static final int DECOMPILE_SECONDS = 60;

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
        if (!"bc2.ghidra-invocation.v1".equals(request.get("schema").getAsString())) {
            throw new IllegalArgumentException("Unknown invocation schema");
        }
        JsonArray addresses = request.getAsJsonArray("addresses");
        if (addresses.size() < 1 || addresses.size() > MAX_FUNCTIONS) {
            throw new IllegalArgumentException("Exactly 1..16 explicit addresses are required");
        }
        String expectedHash = request.get("executable_sha256").getAsString();
        if (!expectedHash.equalsIgnoreCase(currentProgram.getExecutableSHA256())) {
            throw new IllegalStateException("Imported program SHA-256 does not match the locked executable");
        }
        Set<String> unique = new HashSet<>();
        for (var item : addresses) {
            String value = item.getAsString();
            if (!value.matches("0x[0-9a-f]{1,16}") || !unique.add(value)) {
                throw new IllegalArgumentException("Expected distinct normalized hexadecimal addresses");
            }
        }
        Path output = Path.of(args[1]).toAbsolutePath().normalize();
        if (!output.equals(Path.of(request.get("output_path").getAsString()).toAbsolutePath().normalize())) {
            throw new IllegalArgumentException("Output does not match invocation");
        }
        if (Files.exists(output)) {
            throw new IllegalArgumentException("Refusing to overwrite output: " + output);
        }
        JsonObject result = new JsonObject();
        result.addProperty("schema", "bc2.ghidra-functions.v1");
        result.addProperty("tool_version", "1.0");
        result.addProperty("created_utc", Instant.now().toString());
        result.addProperty("status", analysisTimeoutOccurred() ? "analysis_timeout" : "export_complete");
        result.addProperty("executable_sha256", currentProgram.getExecutableSHA256().toLowerCase());
        result.addProperty("ghidra_version", Application.getApplicationVersion());
        result.addProperty("java_version", System.getProperty("java.version"));
        result.addProperty("program_name", currentProgram.getName());
        result.addProperty("image_base", currentProgram.getImageBase().toString());
        result.addProperty("language", currentProgram.getLanguageID().toString());
        result.addProperty("compiler_spec", currentProgram.getCompilerSpec().getCompilerSpecID().toString());
        result.addProperty("references_scope", "incoming references to the resolved function entry only");
        result.addProperty("reference_limit_per_function", MAX_REFERENCES);
        result.addProperty("decompile_timeout_seconds", DECOMPILE_SECONDS);
        result.addProperty("c_character_limit_per_function", MAX_C_CHARACTERS);
        result.add("invocation", request);
        JsonArray functions = new JsonArray();
        DecompInterface decompiler = new DecompInterface();
        try {
            decompiler.toggleCCode(true);
            decompiler.toggleSyntaxTree(false);
            boolean opened = decompiler.openProgram(currentProgram);
            for (var item : addresses) {
                monitor.checkCancelled();
                functions.add(exportFunction(item.getAsString(), decompiler, opened));
            }
        } finally {
            decompiler.dispose();
        }
        result.add("functions", functions);
        // CREATE_NEW protects existing evidence, including against a concurrent writer.
        try (Writer writer = Files.newBufferedWriter(output, StandardCharsets.UTF_8,
                StandardOpenOption.CREATE_NEW, StandardOpenOption.WRITE)) {
            new GsonBuilder().setPrettyPrinting().disableHtmlEscaping().create().toJson(result, writer);
        }
        println("Exported " + functions.size() + " requested function records to " + output);
    }

    private JsonObject exportFunction(String requested, DecompInterface decompiler, boolean opened)
            throws Exception {
        JsonObject row = new JsonObject();
        row.addProperty("requested_address", requested);
        Address address;
        try {
            address = currentProgram.getAddressFactory().getDefaultAddressSpace()
                .getAddress(Long.parseUnsignedLong(requested.substring(2), 16));
        } catch (AddressOutOfBoundsException error) {
            row.addProperty("mapped", false);
            row.addProperty("status", "address_out_of_range");
            return row;
        }
        Function function = currentProgram.getFunctionManager().getFunctionContaining(address);
        row.addProperty("mapped", currentProgram.getMemory().contains(address));
        if (function == null) {
            row.addProperty("status", "no_function");
            return row;
        }
        Address entry = function.getEntryPoint();
        row.addProperty("function_address", entry.toString());
        row.addProperty("address_match", entry.equals(address) ? "entry" : "containing_function");
        row.addProperty("name", function.getName(true));
        row.addProperty("signature", function.getPrototypeString(true, true));
        row.addProperty("body_address_count", function.getBody().getNumAddresses());
        row.addProperty("is_thunk", function.isThunk());
        row.addProperty("is_external", function.isExternal());

        JsonArray references = new JsonArray();
        JsonArray callers = new JsonArray();
        Set<String> callerEntries = new HashSet<>();
        ReferenceIterator iterator = currentProgram.getReferenceManager().getReferencesTo(entry);
        int count = 0;
        while (count < MAX_REFERENCES && iterator.hasNext()) {
            monitor.checkCancelled();
            Reference reference = iterator.next();
            JsonObject ref = new JsonObject();
            ref.addProperty("from", reference.getFromAddress().toString());
            ref.addProperty("to", reference.getToAddress().toString());
            ref.addProperty("type", reference.getReferenceType().toString());
            Function caller = currentProgram.getFunctionManager().getFunctionContaining(reference.getFromAddress());
            if (caller != null) {
                ref.addProperty("from_function", caller.getEntryPoint().toString());
                if (reference.getReferenceType().isCall() && callerEntries.add(caller.getEntryPoint().toString())) {
                    JsonObject callerRow = new JsonObject();
                    callerRow.addProperty("address", caller.getEntryPoint().toString());
                    callerRow.addProperty("name", caller.getName(true));
                    callers.add(callerRow);
                }
            }
            references.add(ref);
            count++;
        }
        boolean truncated = iterator.hasNext();
        row.add("references", references);
        row.addProperty("references_truncated", truncated);
        row.add("callers", callers);
        row.addProperty("callers_may_be_truncated", truncated);
        if (function.isExternal()) {
            row.addProperty("status", "external_function");
        } else if (!opened) {
            row.addProperty("status", "decompiler_unavailable");
            row.addProperty("message", decompiler.getLastMessage());
        } else {
            DecompileResults decompiled = decompiler.decompileFunction(function, DECOMPILE_SECONDS, monitor);
            row.addProperty("message", decompiled.getErrorMessage());
            if (decompiled.decompileCompleted() && decompiled.getDecompiledFunction() != null) {
                String code = decompiled.getDecompiledFunction().getC();
                row.addProperty("status", "decompiled");
                row.addProperty("decompiled_signature", decompiled.getDecompiledFunction().getSignature());
                row.addProperty("c_truncated", code.length() > MAX_C_CHARACTERS);
                row.addProperty("c", code.substring(0, Math.min(code.length(), MAX_C_CHARACTERS)));
            } else {
                row.addProperty("status", decompiled.isTimedOut() ? "decompile_timeout" : "decompile_failed");
            }
        }
        return row;
    }
}
