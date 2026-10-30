// Export every function Ghidra found to CSV, for comparison with analysis/fdb.sqlite.
//
// Run after the XBE import and analysis (see tools/ghidra/README.md):
//   -postScript ExportFunctions.java <output.csv>
//
// Columns: address, name, size, entry-point?, ref-count-in, ref-count-out, namespace
// Ghidra's own names (FUN_* or the XBE loader's labels) are kept as-is.

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.program.model.address.Address;
import java.io.PrintWriter;

public class ExportFunctions extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        String out = args.length > 0 ? args[0] : "functions.csv";
        ReferenceManager refs = currentProgram.getReferenceManager();
        int count = 0;
        try (PrintWriter w = new PrintWriter(out, "UTF-8")) {
            w.println("address,name,size,entry,refs_in,refs_out,namespace");
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Function f = it.next();
                Address a = f.getEntryPoint();
                int in = 0;
                for (var r : refs.getReferencesTo(a)) in++;
                int outRefs = 0;
                for (var r : refs.getReferencesFrom(a)) outRefs++;
                w.printf("0x%08X,%s,%d,%s,%d,%d,%s%n",
                        a.getOffset(), f.getName(), f.getBody().getNumAddresses(),
                        currentProgram.getSymbolTable().isExternalEntryPoint(a) ? "yes" : "no",
                        in, outRefs, f.getParentNamespace().getName());
                count++;
            }
        }
        println("ExportFunctions: " + count + " functions -> " + out);
    }
}
