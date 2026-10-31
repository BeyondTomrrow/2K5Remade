"""List a binary's imports that the UWP/Xbox API allow-list does not contain.

The Windows App Certification Kit ships SupportedAPIs-x64.xml: every
(module, function) a UWP app may import. On Xbox Dev Mode the OS only offers
that API set, so an import missing from it means the exe will not load there
(or the call fails). Usage:

    python tools/uwp/check-imports.py <exe-or-dll> [--dumpbin PATH] [--all]

Needs dumpbin (MSVC) on PATH or --dumpbin.
"""
import argparse, collections, os, re, subprocess, sys, xml.etree.ElementTree as ET

KIT = r"C:\Program Files (x86)\Windows Kits\10\App Certification Kit\SupportedAPIs-x64.xml"

def load_allow(path):
    allow = collections.defaultdict(set)   # func -> set(modules)
    for el in ET.parse(path).getroot().iter("API"):
        allow[el.get("Name")].add(el.get("ModuleName").lower())
    return allow

def imports(dumpbin, binary):
    out = subprocess.run([dumpbin, "/nologo", "/imports", binary], capture_output=True, text=True, errors="replace").stdout
    mod, res = None, collections.defaultdict(list)
    for line in out.splitlines():
        if line.strip() == "Summary": break
        m = re.match(r"^\s{4}(\S+\.(?:dll|DLL|drv|exe))\s*$", line)
        if m:
            mod = m.group(1).lower(); continue
        m = re.match(r"^\s+[0-9A-Fa-f]+\s+(\S+)\s*$", line)
        if mod and m:
            res[mod].append(m.group(1))
        m = re.match(r"^\s+Ordinal\s+(\d+)", line)
        if mod and m:
            res[mod].append("#" + m.group(1))
    return res

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary"); ap.add_argument("--dumpbin", default="dumpbin"); ap.add_argument("--all", action="store_true")
    a = ap.parse_args()
    allow = load_allow(KIT)
    bad_total = 0
    for mod, funcs in sorted(imports(a.dumpbin, a.binary).items()):
        if mod.startswith(("msvcp140", "vcruntime140", "concrt140")):
            continue   # the store CRT (msvcp140_app etc.) replaces these
        bad = [f for f in funcs if not f.startswith("#") and mod not in allow.get(f, ())]
        # A function allowed from a different module (e.g. kernel32 vs an
        # api-ms-win set) still resolves on Xbox: the umbrella libs forward.
        moved = [f for f in bad if f in allow]
        hard = [f for f in bad if f not in allow]
        bad_total += len(hard)
        if hard or (a.all and moved):
            print(f"{mod}: {len(hard)} not in the UWP API set" + (f", {len(moved)} only under another module" if moved else ""))
            for f in sorted(hard): print("    " + f)
            if a.all:
                for f in sorted(moved): print("    (other module) " + f)
    print(f"TOTAL not allowed: {bad_total}")
    return 1 if bad_total else 0

if __name__ == "__main__":
    sys.exit(main())
