"""Regenerate the C string for the D3D11 renderer's shader.

    python tools/gen-gpu-hlsl.py

Reads external/xboxrecomp/src/kernel/nv2a_gpu_d3d11.hlsl and writes
nv2a_gpu_d3d11_hlsl.inc next to it (included by nv2a_gpu_d3d11.inc.c).
Keeping the shader in a real .hlsl file avoids hand-escaping it in C.
"""
import os

KERNEL = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                      "external", "xboxrecomp", "src", "kernel")
src = os.path.join(KERNEL, "nv2a_gpu_d3d11.hlsl")
dst = os.path.join(KERNEL, "nv2a_gpu_d3d11_hlsl.inc")

lines = open(src, encoding="utf-8").read().splitlines()
out = ["/* Generated from nv2a_gpu_d3d11.hlsl by tools/gen-gpu-hlsl.py -- do not edit. */",
       "static const char k_gpu_hlsl[] ="]
for line in lines:
    esc = line.replace("\\", "\\\\").replace('"', '\\"')
    out.append('    "%s\\n"' % esc)
out[-1] += ";"
open(dst, "w", encoding="utf-8", newline="\n").write("\n".join(out) + "\n")
print("wrote", dst, len(lines), "lines")
