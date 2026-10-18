// Verify raw Motorola 68000 disassembly and decompilation on a source-owned fixture.
// @category StuntCarRacer.Setup
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class SmokeM68k extends GhidraScript {
    @Override
    public void run() throws Exception {
        Address start = toAddr(0x1000);
        if (!disassemble(start)) throw new Exception("68000 disassembly failed");
        Function function = getFunctionAt(start);
        if (function == null) function = createFunction(start, "return_five");
        if (function == null) throw new Exception("Function creation failed");
        DecompInterface decompiler = new DecompInterface();
        try {
            if (!decompiler.openProgram(currentProgram)) throw new Exception("Decompiler open failed");
            DecompileResults result = decompiler.decompileFunction(function, 30, monitor);
            if (!result.decompileCompleted()) throw new Exception(result.getErrorMessage());
            String code = result.getDecompiledFunction().getC();
            println(code);
            if (!code.contains("return 5;")) throw new Exception("Unexpected decompiled return value");
            println("M68K_SMOKE_PASSED");
        } finally {
            decompiler.dispose();
        }
    }
}
