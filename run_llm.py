import os
import json
import urllib.request
import subprocess
import re

project_path = r"E:\NFL2K5-PC"
source_file = os.path.join(project_path, "src", "recomp_manual.c")

print("[1/5] Reading recomp_manual.c context...")
with open(source_file, "r", encoding="utf-8") as f:
    code_context = f.read()

ollama_uri = "http://localhost:11434/api/generate"
prompt = f"""We are fixing a stack-state crash in a native static recompilation of NFL 2K5 at E:\\NFL2K5-PC.
The crash occurs inside sub_004952F8 right after the frontend state-6 branch execution.

Here is the content of src\\recomp_manual.c:
{code_context}

Please provide ONLY the corrected C code block for sub_004952F8 with proper stack alignment and return handling so it fixes the crash without syntax errors. Output only valid C code."""

payload = {
    "model": "qwen2.5-coder:7b",
    "prompt": prompt,
    "stream": True,
    "options": {
        "temperature": 0.1,
        "num_ctx": 8192
    }
}

print("[2/5] Querying local Qwen2.5-Coder...")
req = urllib.request.Request(
    ollama_uri,
    data=json.dumps(payload).encode("utf-8"),
    headers={"Content-Type": "application/json"}
)

ai_output = ""
try:
    with urllib.request.urlopen(req, timeout=300) as response:
        print("\n--- AI STREAM START ---")
        for line in response:
            if line:
                chunk = json.loads(line.decode("utf-8"))
                token = chunk.get("response", "")
                print(token, end="", flush=True)
                ai_output += token
        print("\n--- AI STREAM END ---\n")

    print("[3/5] Extracting and injecting code fix into recomp_manual.c...")
    match = re.search(r"```(?:c)?\s*([\s\S]*?)\s*```", ai_output)
    if match:
        new_func_code = match.group(1)
    else:
        new_func_code = ai_output.strip()

    # Replace sub_004952F8 in recomp_manual.c or append if not found
    func_pattern = re.compile(r"(?:static\s+)?void\s+sub_004952F8\s*\([^)]*\)\s*\{[^}]*\}", re.DOTALL)
    if func_pattern.search(code_context):
        updated_code = func_pattern.sub(new_func_code, code_context)
    else:
        updated_code = code_context + "\n\n" + new_func_code

    backup_path = source_file + ".bak"
    with open(backup_path, "w", encoding="utf-8") as f:
        f.write(code_context)

    with open(source_file, "w", encoding="utf-8") as f:
        f.write(updated_code)
    print("Successfully updated src\\recomp_manual.c with Qwen's fix!")

    print("[4/5] Locating build environment...")
    build_dir = os.path.join(project_path, "build")
    if not os.path.exists(build_dir):
        print(f"Build directory not found at {build_dir}. Creating it...")
        os.makedirs(build_dir, exist_ok=True)
        subprocess.run(["cmake", "-S", project_path, "-B", build_dir], check=True)

    print("[5/5] Triggering CMake build to update NFL2K5.exe...")
    subprocess.run(["cmake", "--build", build_dir, "--config", "Release"], check=True)
    print("Build complete! NFL2K5.exe successfully updated.")

except Exception as e:
    print(f"\nExecution failed: {e}")