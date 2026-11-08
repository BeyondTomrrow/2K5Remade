"""Package NFL2K5.apk from build-android (the native libraries) and android/app.

    python android/build_apk.py [--build-dir build-android] [--out dist/NFL2K5-android.apk]

Uses the Android SDK's own tools (aapt2, javac from the JDK, d8, zipalign,
apksigner); no Gradle. Paths default to C:/Android (JDK, SDK) and the SDL
source tree whose Java glue SDLActivity is (C:/Android/src/SDL).
The APK is signed with a local debug key (android/debug.keystore, created on
first use): fine for sideloading, not for a store.
"""
import argparse
import glob
import hashlib
import json
import os
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

ap = argparse.ArgumentParser()
ap.add_argument("--build-dir", default=os.path.join(ROOT, "build-android"))
ap.add_argument("--abi", default="arm64-v8a")
ap.add_argument("--out", default=os.path.join(ROOT, "dist", "NFL2K5-android.apk"))
ap.add_argument("--sdk", default="C:/Android/sdk")
ap.add_argument("--jdk", default=glob.glob("C:/Android/jdk-17*")[0] if glob.glob("C:/Android/jdk-17*") else "")
ap.add_argument("--sdl", default="C:/Android/src/SDL")
ap.add_argument("--local-presentations", action="store_true", help="pack every presentation file on disk (test builds), not only tracked ones")
ap.add_argument("--mycareer", help="MyCareer home asset tree (game-folder paths: mods/mycareer-home, data/mycareer-home), unpacked on first start")
ap.add_argument("--platform", default="android-36")
ap.add_argument("--build-tools", default="35.0.0")
ap.add_argument("--version-name", help="versionName shown by Android (default: the manifest's)")
ap.add_argument("--version-code", type=int, help="versionCode (default: the manifest's)")
a = ap.parse_args()

bt = os.path.join(a.sdk, "build-tools", a.build_tools)
android_jar = os.path.join(a.sdk, "platforms", a.platform, "android.jar")
exe = ".exe" if os.name == "nt" else ""
bat = ".bat" if os.name == "nt" else ""
java_bin = os.path.join(a.jdk, "bin")
env = dict(os.environ, JAVA_HOME=a.jdk, PATH=java_bin + os.pathsep + os.environ.get("PATH", ""))
work = os.path.join(a.build_dir, "apk")
app = os.path.join(HERE, "app")


def run(*cmd):
    print(">", " ".join(os.path.basename(c) if i == 0 else c for i, c in enumerate(cmd)))
    subprocess.run(cmd, check=True, env=env)


shutil.rmtree(work, ignore_errors=True)
for d in ("res", "gen", "classes", "dex"):
    os.makedirs(os.path.join(work, d))

# 1. resources and manifest
run(os.path.join(bt, "aapt2" + exe), "compile", "--dir", os.path.join(app, "res"),
    "-o", os.path.join(work, "res", "res.zip"))
base = os.path.join(work, "base.apk")
version = []
if a.version_name:
    version += ["--version-name", a.version_name]
if a.version_code:
    version += ["--version-code", str(a.version_code)]
if version:
    version.append("--replace-version")   # the manifest has its own
run(os.path.join(bt, "aapt2" + exe), "link", "-I", android_jar, "--manifest", os.path.join(app, "AndroidManifest.xml"),
    "--java", os.path.join(work, "gen"), "--min-sdk-version", "28", "--target-sdk-version", "35", *version,
    "-o", base, os.path.join(work, "res", "res.zip"))

# 2. Java: SDL's activity glue, ours, and R
srcs = glob.glob(os.path.join(a.sdl, "android-project", "app", "src", "main", "java", "**", "*.java"), recursive=True)
srcs += glob.glob(os.path.join(app, "java", "**", "*.java"), recursive=True)
srcs += glob.glob(os.path.join(work, "gen", "**", "*.java"), recursive=True)
run(os.path.join(java_bin, "javac" + exe), "--release", "11", "-nowarn", "-encoding", "UTF-8", "-classpath", android_jar,
    "-d", os.path.join(work, "classes"), *srcs)
classes = glob.glob(os.path.join(work, "classes", "**", "*.class"), recursive=True)
run(os.path.join(bt, "d8" + bat), "--release", "--min-api", "28", "--lib", android_jar,
    "--output", os.path.join(work, "dex"), *classes)

# 3. native libraries
libs = {}
for name, pats in (("libmain.so", ["nfl2k5/libmain.so", "**/libmain.so"]),
                   ("libSDL3.so", ["SDL3/libSDL3.so", "**/libSDL3.so"]),
                   # libadrenotools' hooks (custom GPU drivers), loaded by name
                   ("libhook_impl.so", ["**/libhook_impl.so"]),
                   ("libmain_hook.so", ["**/libmain_hook.so"])):
    for p in pats:
        hit = glob.glob(os.path.join(a.build_dir, p), recursive=True)
        if hit:
            libs[name] = hit[0]
            break
    else:
        sys.exit(f"{name} not found under {a.build_dir}: build it first")

unsigned = os.path.join(work, "unsigned.apk")
shutil.copy(base, unsigned)
with zipfile.ZipFile(unsigned, "a", zipfile.ZIP_DEFLATED) as z:
    z.write(os.path.join(work, "dex", "classes.dex"), "classes.dex")
    for name, path in libs.items():
        print(f"  lib/{a.abi}/{name}: {os.path.getsize(path) / 1e6:.1f} MB")
        z.write(path, f"lib/{a.abi}/{name}")
    # what this build is, for the first lines of nfl2k5.log (src/main.c)
    def commit(path):
        return subprocess.run(["git", "-C", path, "rev-parse", "--short", "HEAD"], capture_output=True,
                              text=True).stdout.strip()
    z.writestr("assets/build-info.json", json.dumps({
        "apk": os.path.basename(a.out), "version": a.version_name or "",
        "commit": commit(ROOT), "xboxrecomp": commit(os.path.join(ROOT, "external", "xboxrecomp")),
        "libmain_sha256": hashlib.sha256(open(libs["libmain.so"], "rb").read()).hexdigest()[:16],
    }, separators=(",", ":")))
    # broadcast presentation packages (mods/presentations), unpacked into the
    # game folder on first start (src/main.c android_unpack_presentations);
    # the same file types the PC installer ships
    pres = os.path.join(ROOT, "mods", "presentations")
    keep = (".html", ".css", ".js", ".json", ".png", ".ttf", ".otf", ".woff", ".woff2", ".md", ".txt", ".mp3", ".jpg", ".svg", ".webp")
    files = []
    # tracked files only, as the PC release (git-ignored local art and music stay out), and
    # not the CBS / NBC packages' own fonts (licences unclear); --local-presentations packs
    # everything on disk, as test builds did
    tracked = set(subprocess.run(["git", "-C", ROOT, "ls-files", "mods/presentations"], capture_output=True,
                                 text=True, encoding="utf8").stdout.splitlines())
    no_fonts = ("CBS_2026/fonts/", "NBC_SNF/fonts/")
    for dirpath, _, names in os.walk(pres):
        for n in sorted(names):
            if n.lower().endswith(keep):
                rel = os.path.relpath(os.path.join(dirpath, n), pres).replace(os.sep, "/")
                if not a.local_presentations and ("mods/presentations/" + rel not in tracked or rel.startswith(no_fonts)):
                    continue
                files.append(rel)
                src = os.path.join(dirpath, n)
                if n.lower().endswith(".png") and os.path.getsize(src) > 200_000:
                    # team logos are 1-3 MB at up to 2000 px; the phone draws
                    # the page at 960 px wide, so 256 px is plenty (logos are drawn far smaller)
                    from PIL import Image
                    import io
                    im = Image.open(src)
                    if max(im.size) > 256:
                        im.thumbnail((256, 256), Image.LANCZOS)
                    buf = io.BytesIO()
                    im.save(buf, "PNG", optimize=True)
                    z.writestr("assets/presentations/" + rel, buf.getvalue())
                else:
                    z.write(src, "assets/presentations/" + rel)
    stamp = str(int(max(os.path.getmtime(os.path.join(pres, f)) for f in files))) if files else "0"
    z.writestr("assets/presentations/manifest.txt", stamp + "\n" + "\n".join(files) + "\n")
    print(f"  assets/presentations: {len(files)} files")
    # MyCareer home: CC0 models, licences, credits and the shop list, at their game-folder paths
    if a.mycareer:
        mfiles = []
        for dirpath, _, names in os.walk(a.mycareer):
            for n in sorted(names):
                src = os.path.join(dirpath, n)
                rel = os.path.relpath(src, a.mycareer).replace(os.sep, "/")
                if not (rel.startswith("mods/mycareer-home/") or rel.startswith("data/mycareer-home/") or rel.startswith("docs/third-party/")):
                    sys.exit("--mycareer: unexpected path " + rel)
                mfiles.append(rel)
                z.write(src, "assets/mycareer/" + rel)
        mstamp = str(int(max(os.path.getmtime(os.path.join(a.mycareer, f)) for f in mfiles))) if mfiles else "0"
        z.writestr("assets/mycareer/manifest.txt", mstamp + "\n" + "\n".join(mfiles) + "\n")
        print(f"  assets/mycareer: {len(mfiles)} files")

# 4. align and sign
aligned = os.path.join(work, "aligned.apk")
run(os.path.join(bt, "zipalign" + exe), "-p", "-f", "4", unsigned, aligned)
ks = os.path.join(HERE, "debug.keystore")
if not os.path.exists(ks):
    run(os.path.join(java_bin, "keytool" + exe), "-genkeypair", "-keystore", ks, "-storepass", "android",
        "-keypass", "android", "-alias", "nfl2k5", "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000",
        "-dname", "CN=NFL2K5 local build")
os.makedirs(os.path.dirname(a.out), exist_ok=True)
run(os.path.join(bt, "apksigner" + bat), "sign", "--ks", ks, "--ks-pass", "pass:android",
    "--key-pass", "pass:android", "--out", a.out, aligned)
print(f"\n{a.out}: {os.path.getsize(a.out) / 1e6:.1f} MB")
