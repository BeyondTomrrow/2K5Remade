"""Generated code with the guest registers in locals (build step).

The recompiled functions in src/recomp/gen keep the guest's general registers
in thread-local globals (g_eax ... g_edi, via `#define eax g_eax` in
recomp_types.h). Every use is a TLS memory access, so the compiler can keep
none of them in a host register, and game logic was ~11 ms of every frame
(2026-10-03 profile, flat across the generated code).

This writes a copy of each recomp_NNNN.c to build/gen-locals/ in which every
function copies the registers it uses into locals (r_eax ...) on entry and
writes them back around every call and before every return:

  * `RECOMP_REGS_DECL`-style declaration of the registers the body names;
  * `return;`            -> `{ RECOMP_REGS_OUT(); return; }`
  * `sub_XXXXXXXX();`    -> `RECOMP_CALL(sub_XXXXXXXX);` (out, call, in)
  * RECOMP_ABI_CALL / RECOMP_ICALL* sync the same way (recomp_types.h);
  * explicit `g_esp` (indirect-call blocks) -> the local;
  * gen patches (tools/apply-gen-patches.py) run with the globals: their
    block is wrapped in recomp_patch_begin.h / recomp_patch_end.h, which
    write the locals out, point the register names at the globals for the
    block, and read them back after it. A `goto` out of a block reads them
    back first.

src/recomp/gen is left untouched (apply-gen-patches anchors keep matching).
CMake compiles build/gen-locals when NFL2K5_REG_LOCALS is ON. Outputs are
rewritten only when their content changes, so an unchanged chunk does not
rebuild. Run by tools/build.ps1 after apply-gen-patches.py.
"""
import glob
import importlib.util
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(ROOT, "src", "recomp", "gen")
OUT = os.path.join(ROOT, "build", "gen-locals")
# --gen DIR --out DIR: another generated tree (a mod pack's build).
if "--gen" in sys.argv:
    GEN = os.path.abspath(sys.argv[sys.argv.index("--gen") + 1])
if "--out" in sys.argv:
    OUT = os.path.abspath(sys.argv[sys.argv.index("--out") + 1])
REGS = ("eax", "ecx", "edx", "esp", "ebx", "esi", "edi")

FUNC_RE = re.compile(r"^void ([A-Za-z_]\w*)\(void\)$")
REG_RE = re.compile(r"\b(eax|ecx|edx|esp|ebx|esi|edi)\b")
GREG_RE = re.compile(r"\bg_(eax|ecx|edx|esp|ebx|esi|edi)\b")
CALL_RE = re.compile(r"\b(sub_[0-9A-Fa-f]{8})\(\);")
RET_RE = re.compile(r"\breturn;")
TAIL_RE = re.compile(r"RECOMP_CALL\((sub_[0-9A-Fa-f]{8})\); \{ RECOMP_REGS_OUT\(\); return; \}")
GOTO_RE = re.compile(r"\bgoto (\w+);")
MARK_RE = re.compile(r"/\* NFL2K5-GENPATCH:([A-Za-z0-9_]+) \*/")
# Host functions a non-patch line may call with the locals live: none of
# them reads or writes a guest register.
HOST_OK = re.compile(r"\b(xbox_ReadTimeStampCounter|recomp_fist|recomp_frndint|recomp_fxam|recomp_debug_service|recomp_\w+_hit)\(")
HOST_CALL = re.compile(r"\b(nfl2k5_\w+|xbox_\w+|recomp_\w+)\(")


def load_patches():
    spec = importlib.util.spec_from_file_location("agp", os.path.join(ROOT, "tools", "apply-gen-patches.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return {pid: len(code.rstrip("\n").split("\n")) for pid, _f, _a, _w, code in mod.PATCHES}


def transform(text, patch_lines, warnings, name):
    lines = text.split("\n")
    eol = "\r" if lines and lines[0].endswith("\r") else ""
    out = []
    i = 0
    n = len(lines)
    nfunc = 0
    while i < n:
        line = lines[i]
        s = line.rstrip("\r")
        if s == "#define RECOMP_GENERATED_CODE":
            out.append("#define RECOMP_REG_LOCALS 1" + eol)
            out.append(line)
            i += 1
            continue
        m = FUNC_RE.match(s)
        if not (m and i + 1 < n and lines[i + 1].rstrip("\r") == "{"):
            out.append(line)
            i += 1
            continue
        # A function: header line, "{", body up to the closing "}" at column 0.
        end = i + 2
        while end < n and lines[end].rstrip("\r") != "}":
            end += 1
        body = lines[i + 2:end]
        joined = "\n".join(body)
        found = set(REG_RE.findall(joined)) | set(GREG_RE.findall(joined))
        used = [r for r in REGS if r in found]
        # A function that calls anything syncs through RECOMP_REGS_OUT/IN;
        # with no register named there is nothing to keep.
        decl = ", ".join("r_%s = g_%s" % (r, r) for r in used)
        outs = ", ".join("g_%s = r_%s" % (r, r) for r in used)
        ins = ", ".join("r_%s = g_%s" % (r, r) for r in used)
        out.append("#undef RECOMP_REGS_OUT" + eol)
        out.append("#undef RECOMP_REGS_IN" + eol)
        out.append("#define RECOMP_REGS_OUT() (%s)" % (outs + ", (void)0" if outs else "(void)0") + eol)
        out.append("#define RECOMP_REGS_IN() (%s)" % (ins + ", (void)0" if ins else "(void)0") + eol)
        out.append(line)
        out.append(lines[i + 1])
        if decl:
            out.append("    uint32_t %s;" % decl + eol)
        j = 0
        while j < len(body):
            b = body[j]
            mk = MARK_RE.search(b)
            if mk:
                pid = mk.group(1)
                if pid not in patch_lines:
                    raise SystemExit("%s: %s has gen patch %s that tools/apply-gen-patches.py no longer lists"
                                     % (name, m.group(1), pid))
                cnt = patch_lines[pid]
                out.append(b)
                out.append('#include "recomp_patch_begin.h"' + eol)
                for k in range(cnt):
                    pl = body[j + 1 + k]
                    pl = GOTO_RE.sub(lambda g: "{ RECOMP_REGS_IN(); goto %s; }" % g.group(1), pl)
                    out.append(pl)
                out.append('#include "recomp_patch_end.h"' + eol)
                j += 1 + cnt
                continue
            t = GREG_RE.sub(lambda g: g.group(1), b)
            t = CALL_RE.sub(lambda g: "RECOMP_CALL(%s);" % g.group(1), t)
            t = RET_RE.sub("{ RECOMP_REGS_OUT(); return; }", t)
            # A tail call writes out once: no read-back just to write out again.
            t = TAIL_RE.sub(lambda g: "RECOMP_TAILCALL(%s);" % g.group(1), t)
            code = t.split("/*")[0]
            for hc in HOST_CALL.finditer(code):
                if not HOST_OK.match(code, hc.start()):
                    warnings.append("%s: %s calls %s outside a gen patch" % (name, m.group(1), hc.group(1)))
            out.append(t)
            j += 1
        out.append("    RECOMP_REGS_OUT();" + eol)
        out.append(lines[end])
        nfunc += 1
        i = end + 1
    return "\n".join(out), nfunc


def main():
    patch_lines = load_patches()
    os.makedirs(OUT, exist_ok=True)
    warnings = []
    written = total = 0
    srcs = sorted(glob.glob(os.path.join(GEN, "recomp_*.c")))
    for path in srcs:
        name = os.path.basename(path)
        with open(path, encoding="utf-8", newline="") as f:
            text = f.read()
        if re.match(r"recomp_\d{4}\.c$", name):
            text, nf = transform(text, patch_lines, warnings, name)
            total += nf
        dst = os.path.join(OUT, name)
        old = None
        if os.path.exists(dst):
            with open(dst, encoding="utf-8", newline="") as f:
                old = f.read()
        if old != text:
            with open(dst, "w", encoding="utf-8", newline="") as f:
                f.write(text)
            written += 1
    keep = {os.path.basename(p) for p in srcs}
    for stale in glob.glob(os.path.join(OUT, "*.c")):
        if os.path.basename(stale) not in keep:
            os.remove(stale)
    for w in warnings[:40]:
        print("[reg-locals] warning: " + w)
    print("[reg-locals] %d functions, %d of %d files rewritten -> %s" % (total, written, len(srcs), OUT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
