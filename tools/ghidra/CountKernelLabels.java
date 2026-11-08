// Read-only check: how many xboxkrnl_ labels exist in the saved project.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

public class CountKernelLabels extends GhidraScript {
    @Override
    protected void run() throws Exception {
        int n = 0;
        SymbolIterator it = currentProgram.getSymbolTable().getAllSymbols(true);
        while (it.hasNext()) {
            Symbol s = it.next();
            if (s.getName().startsWith("xboxkrnl_")) {
                n++;
                if (n <= 5) println("  " + s.getAddress() + " " + s.getName());
            }
        }
        println("CountKernelLabels: " + n + " xboxkrnl_ labels");
    }
}
