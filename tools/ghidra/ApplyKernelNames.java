// Name the kernel import slots of default.xbe after the kernel exports they hold.
//
// The XBE calls kernel functions through an import thunk table (the entries
// listed in analysis/xbe_analysis.json, copied to tools/ghidra/kernel_thunks.csv).
// Ghidra leaves those slots unnamed, so every kernel call decompiles to an
// address. This labels each slot "xboxkrnl_<Name>" and records the ordinal.
//
// Run after import and analysis:
//   -postScript ApplyKernelNames.java <kernel_thunks.csv>
//
// Labels only: nothing is retyped and no code is changed.

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.symbol.SourceType;
import java.io.BufferedReader;
import java.io.FileReader;

public class ApplyKernelNames extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        String csv = args.length > 0 ? args[0] : "kernel_thunks.csv";
        int named = 0, missing = 0;
        try (BufferedReader r = new BufferedReader(new FileReader(csv))) {
            String line = r.readLine(); // header
            while ((line = r.readLine()) != null) {
                String[] f = line.split(",");
                if (f.length < 3) continue;
                Address a = toAddr(Long.decode(f[0].trim()));
                if (a == null) { missing++; continue; }
                String label = "xboxkrnl_" + f[2].trim();
                currentProgram.getSymbolTable().createLabel(a, label, SourceType.IMPORTED);
                named++;
            }
        }
        println("ApplyKernelNames: labelled " + named + " kernel thunk slots, " + missing + " addresses not in the program");
    }
}
