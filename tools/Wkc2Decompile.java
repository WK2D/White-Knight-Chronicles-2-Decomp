// Focused, repeatable analysis of a privately supplied WKC2 executable.
// @category WKC2
import java.io.*;
import java.math.BigInteger;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;

public class Wkc2Decompile extends GhidraScript {
    private final Map<Long, long[]> bounds = new TreeMap<>();

    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 3) throw new IllegalArgumentException("inventory.tsv output-directory hex-address...");
        Path output = Paths.get(args[1]);
        Files.createDirectories(output);
        FunctionManager fm = currentProgram.getFunctionManager();
        for (String line : Files.readAllLines(Paths.get(args[0]))) {
            String[] cells = line.split("\\t");
            long start = Long.parseUnsignedLong(cells[0], 16);
            long end = Long.parseUnsignedLong(cells[1], 16);
            long toc = Long.parseUnsignedLong(cells[2], 16);
            bounds.put(start, new long[]{end, toc});
            Address first = toAddr(start), last = toAddr(end - 1);
            currentProgram.getProgramContext().setValue(currentProgram.getRegister("r2"), first, last, BigInteger.valueOf(toc));
            if (fm.getFunctionAt(first) == null && fm.getFunctionContaining(first) == null)
                fm.createFunction(String.format("wkc_%08x", start), first, new AddressSet(first, last), SourceType.USER_DEFINED);
        }
        println("Seeded " + bounds.size() + " bounded PPU functions with per-function TOCs");
        Set<Long> pending = new LinkedHashSet<>();
        for (int i = 2; i < args.length; i++) pending.add(Long.parseUnsignedLong(args[i].replace("0x", ""), 16));
        // Decode the selected functions and their direct callees. No whole-image auto-analysis.
        Set<Long> decoded = new HashSet<>();
        Set<Long> frontier = new LinkedHashSet<>(pending);
        for (int depth = 0; depth <= 1; depth++) {
            Set<Long> next = new LinkedHashSet<>();
            for (long entry : frontier) {
                monitor.checkCancelled();
                long[] b = bounds.get(entry);
                if (b == null || !decoded.add(entry)) continue;
                AddressSet body = new AddressSet(toAddr(entry), toAddr(b[0] - 1));
                new DisassembleCommand(toAddr(entry), body, true).applyTo(currentProgram, monitor);
                InstructionIterator instructions = currentProgram.getListing().getInstructions(body, true);
                while (instructions.hasNext()) {
                    Instruction ins = instructions.next();
                    if (ins.getFlowType().isCall()) for (Address target : ins.getFlows())
                        if (bounds.containsKey(target.getOffset())) next.add(target.getOffset());
                }
            }
            frontier = next;
        }
        DecompInterface decompiler = new DecompInterface();
        try {
            decompiler.openProgram(currentProgram);
            for (long entry : pending) {
                monitor.checkCancelled();
                Function f = fm.getFunctionAt(toAddr(entry));
                if (f == null) { println("No function: " + Long.toHexString(entry)); continue; }
                DecompileResults result = decompiler.decompileFunction(f, 45, monitor);
                String code = result.decompileCompleted() ? result.getDecompiledFunction().getC() : result.getErrorMessage();
                Files.writeString(output.resolve(String.format("%08X.c", entry)), code, StandardCharsets.UTF_8);
                println(String.format("Decompiled %08X: %s", entry, result.decompileCompleted()));
            }
        } finally { decompiler.dispose(); }
    }
}
