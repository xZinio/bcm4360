// SPDX-License-Identifier: ISC
//
// Headless Ghidra script: decompile every function of wlc_hybrid.o_shipped.
//
//   analyzeHeadless PROJ_DIR PROJ -process wlc_hybrid.o_shipped \
//       -scriptPath tools/re/ghidra -postScript ExportDecomp.java OUT_DIR [ENTRIES]
//
// ENTRIES is an optional text file with one hexadecimal .text offset per line
// (from `blob.py funcs`): functions are created there if Ghidra's own analysis
// did not find them.  Unnamed functions are renamed sub_<.text offset> so that
// names agree with tools/re/blob.py.
//
// Output: OUT_DIR/c/<offset>_<name>.c and OUT_DIR/functions.tsv
//
//@category bcm4360

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.TreeSet;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.decompiler.parallel.DecompileConfigurer;
import ghidra.app.decompiler.parallel.DecompilerCallback;
import ghidra.app.decompiler.parallel.ParallelDecompiler;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;
import ghidra.util.task.TaskMonitor;

public class ExportDecomp extends GhidraScript {

	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		if (args.length < 1) {
			throw new IllegalArgumentException("usage: ExportDecomp.java OUT_DIR [ENTRIES]");
		}
		File outDir = new File(args[0]);
		File cDir = new File(outDir, "c");
		cDir.mkdirs();

		MemoryBlock text = currentProgram.getMemory().getBlock(".text");
		final Address tbase = text.getStart();
		println("image base " + currentProgram.getImageBase() + ", .text at " + tbase);
		for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
			println("  block " + b.getName() + " " + b.getStart() + " size " + b.getSize());
		}

		int created = 0;
		if (args.length > 1) {
			try (BufferedReader r = new BufferedReader(new FileReader(args[1]))) {
				String line;
				while ((line = r.readLine()) != null) {
					line = line.trim();
					if (line.isEmpty()) {
						continue;
					}
					long off = Long.parseLong(line.split("\\s+")[0], 16);
					Address a = tbase.add(off);
					if (getFunctionAt(a) != null) {
						continue;
					}
					Function in = getFunctionContaining(a);
					if (in != null) {
						// Ghidra merged it into a neighbour; keep a note, do not split blindly
						println("entry " + line + " lies inside " + in.getName());
						continue;
					}
					if (getInstructionAt(a) == null) {
						disassemble(a);
					}
					if (createFunction(a, null) != null) {
						created++;
					}
				}
			}
		}
		println("functions created from the entry list: " + created);

		List<Function> todo = new ArrayList<>();
		for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
			if (f.isExternal() || !text.contains(f.getEntryPoint())) {
				continue;
			}
			long off = f.getEntryPoint().subtract(tbase);
			if (f.getSymbol().getSource() == SourceType.DEFAULT ||
				f.getName().startsWith("FUN_")) {
				f.setName(String.format("sub_%06x", off), SourceType.ANALYSIS);
			}
			todo.add(f);
		}
		println("functions to decompile: " + todo.size());

		try (PrintWriter w = new PrintWriter(new File(outDir, "functions.tsv"), "UTF-8")) {
			w.println("offset\tsize\tname\tcallees");
			for (Function f : todo) {
				long off = f.getEntryPoint().subtract(tbase);
				Set<String> callees = new TreeSet<>();
				for (Function c : f.getCalledFunctions(monitor)) {
					callees.add(c.getName());
				}
				w.printf("%06x\t%d\t%s\t%s%n", off, f.getBody().getNumAddresses(), f.getName(),
					String.join(",", callees));
			}
		}

		DecompileConfigurer conf = new DecompileConfigurer() {
			@Override
			public void configure(DecompInterface d) {
				DecompileOptions o = new DecompileOptions();
				o.grabFromProgram(currentProgram);
				d.setOptions(o);
				d.toggleCCode(true);
				d.toggleSyntaxTree(false);
				d.setSimplificationStyle("decompile");
			}
		};
		DecompilerCallback<String> cb = new DecompilerCallback<String>(currentProgram, conf) {
			@Override
			public String process(DecompileResults res, TaskMonitor m) throws Exception {
				Function f = res.getFunction();
				long off = f.getEntryPoint().subtract(tbase);
				String body;
				if (res.decompileCompleted()) {
					body = res.getDecompiledFunction().getC();
				}
				else {
					body = "/* decompilation failed: " + res.getErrorMessage() + " */\n";
				}
				String head = String.format("// %s  .text+0x%x  size %d%n", f.getName(), off,
					f.getBody().getNumAddresses());
				File out = new File(cDir, String.format("%06x_%s.c", off, f.getName()));
				Files.write(out.toPath(), (head + body).getBytes(StandardCharsets.UTF_8));
				return res.decompileCompleted() ? null : f.getName();
			}
		};
		cb.setTimeout(300);
		List<String> failed = new ArrayList<>();
		try {
			for (String s : ParallelDecompiler.decompileFunctions(cb, todo, monitor)) {
				if (s != null) {
					failed.add(s);
				}
			}
		}
		finally {
			cb.dispose();
		}
		println("decompiled " + (todo.size() - failed.size()) + ", failed " + failed.size());
		for (String s : failed) {
			println("  failed: " + s);
		}
	}
}
