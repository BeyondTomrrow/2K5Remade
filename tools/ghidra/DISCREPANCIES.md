# Ghidra vs. recompilation analysis: discrepancy classification (retail XBE)

Source: `logs/ghidra-functions-retail.csv` (Ghidra 12.0.3, XboxExecutableLoader,
full analysis) and `analysis/fdb.sqlite` (our analysis, 33,223 functions).
Tools: `tools/ghidra/compare.py`, the classification code in this session.

## Totals

| Group | Count | Meaning |
|---|---|---|
| Both analyzers | 19,753 | Same start address |
| Ghidra only | 224 | See below |
| Our analysis only | 13,470 | Ghidra is conservative (see below) |
| Size disagreements | 656 | Same start, different end |

## The 224 Ghidra-only functions

- **39 start inside a function we already have.** 22 of them are referenced
  (Ghidra or our xrefs) and so are real second entry points. Our boundaries
  merged them into the containing function. The generated C still contains
  every instruction (jumps become labels), so behavior is preserved for
  jumps within the function. A call *into* one of these would need its own
  entry point. This is the same failure mode as the SOFTDRINK seeds.
  The other 17 have no reference.
- **185 start outside every function we have.** 184 are mapped code
  (1 is in .bss/zero-fill). 178 of the 184 begin with `push`, a normal prologue.
  - 12 have a matching 32-bit value somewhere in the XBE, which could be a
    function pointer. Some may be coincidental byte matches.
  - 173 have no pointer, no caller, and no reference from our analysis.
  - Status: code-shaped but unreachable from the binary's own references.
    These are either dead code or reached only at runtime. Dead or unused
    features would be interesting for modding, but this is not proven.
    Verify with a coverage run (`NFL2K5_COVERAGE` build) during gameplay before
    treating any of them as real features.

## The 13,470 functions only our analysis found

Mostly this is Ghidra being conservative. Its auto-analysis follows direct
calls and seeds from the entry point, while our analysis also follows
indirect call targets (ICALL feedback), jump tables, and the detour and
push-ret patterns from the mod work. Each of these needs a runtime or
pattern check before we trust it, but they are not known to be wrong.

## Kernel imports

186 kernel import slots now carry real names (`xboxkrnl_<Name>`), applied by
`tools/ghidra/ApplyKernelNames.java` from `analysis/xbe_analysis.json`. Verified
by `tools/ghidra/CountKernelLabels.java`: 186 labels in the saved project.

## Not yet done

- The 656 size disagreements have not been classified.
- Function signatures from the `xbox-includes` headers are not applied yet.
  The headers need Ghidra's "Parse C Source" step, which is a GUI operation.
- Coverage verification of the 173 unreferenced candidates.
