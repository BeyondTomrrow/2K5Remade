# Saves: no artificial limit, only the PC's disk (2026-10-21)

User report: "we are still limited by saves its saying that im low on blocks when we
should be unlimited only thing holding us back needs to be our actual pc storage".

Screenshot sheet (before/after, and the warning reproduced): `logs\saves-fix-sheet.png`.

## Which check shows the warning

There are two "blocks" messages in the XBE (UTF-16, `.rdata`):

| VA | Text | Shown by |
|---|---|---|
| `0xE81A70` | "Your Xbox doesn't have enough free blocks to save games ... To save you need an additional %u blocks." | `sub_0016A100`, at boot / hard-disk check, device status 2 |
| `0xE822B0` | "There are less than %d blocks available on %s. This save requires %d blocks ..." | `sub_0016BD70`, when one save does not fit (`sub_0016BE80`, error 0x19) |

The boot check is `sub_0016B610` (device table `[0xBDBDBC]`, 0x34-byte entries):

1. free blocks = 64-bit free bytes (`sub_0004B6D0`, which calls the title's XAPI
   `GetDiskFreeSpaceExA` = `sub_0001C54C` -> kernel `NtOpenFile` + `NtQueryVolumeInformationFile`)
   / 16 KB, low 32 bits kept as a **signed** int;
2. status 5 "Unusable" if the device cannot be used;
3. status 3 "Save Game Limit Reached" if the device holds **>= 256 saves** (`cmp eax, 0x100`);
4. status 2 (the warning above) if free blocks < `sub_0016B490` = the sizes of **all ten save
   types together** (Settings, VIP, Roster, Franchise, Playbook, ...). Measured: **303-313 blocks
   (about 5 MB)**.

The save list scanner `sub_0004C8E0` has a second 256 limit: with >= 256 saves it **sets the
disk's free space to 0** (`sub_0003A8A0(0,0)`), which turns into the same "not enough free blocks"
warning.

There is no 50,000-block disk and no per-title block quota: "50000+ blocks" is only the label
`sub_0016A300` prints for anything over 50,000.

### What actually triggered it on this PC

The free space already came from the real disk, but from the wrong place and without a guard:

- the kernel answered `NtQueryVolumeInformationFile` with `GetDiskFreeSpaceExW(NULL)`: the
  **working directory's** drive, not the drive holding `saves\` (wrong whenever NFL2K5_ROOT or a
  shortcut's start folder is on another drive);
- E: (game + saves) is 99-100% full. During this session it was seen at 3.3 GB, 2.4 GB and,
  for about two minutes, **16 MB** free (other agents' recordings/builds plus my own sandbox
  copy, see Notes). Below ~5 MB the boot check correctly says "not enough free blocks".

So with the drive that full, the message was the game reporting real PC space. The fixes below
make sure nothing *other* than real space can produce it, and remove the save-count limits.

## Fix

### 1. Free space = the saves volume, clamped (external/xboxrecomp, kernel_file.c)

`NtQueryVolumeInformationFile(FileFsSizeInformation)` now asks for the free space of the
**volume of the handle the title opened** (XAPI opens `U:\` -> `<root>\saves\UserData`), via
`GetFinalPathNameByHandleW` + `GetDiskFreeSpaceExW`; the working directory is only the fallback.

**Clamp: 0x7FFFC000 bytes (2 GB - 16 KB) = 131,071 blocks.** The game keeps the block count as a
signed 32-bit int (safe to 32 TB), but code linked into the title (CRI `cvFsGetFreeSize`, XAPI
helpers) works in 32-bit *bytes*; below 2^31 bytes every one of those is safe. 131,071 blocks still
shows as "50000+ blocks" and is ~2,700 franchise saves. Below 2 GB free the real value is passed
through, so a really full disk still warns (correctly). The query-failure fallback is the clamp
too (it was 8 GB, over 2^31).

`RECOMP_FREE_SPACE_MB=<n>` overrides the host value for testing.

external/xboxrecomp is a separate, gitignored checkout and is not committed here. Exact local diff
(on top of the other agents' uncommitted changes in the same file). `config/xboxrecomp-local.patch`
was last regenerated 2026-10-02 and is not kept in sync with the checkout, so it was not
regenerated (that would sweep in other agents' unreviewed work); fold this hunk in when it is next
refreshed:

```diff
@@ -560,2 +564,59 @@ NTSTATUS __stdcall xbox_NtSetInformationFile(
 
+/* Free space as a title sees it: the host volume the handle lives on.
+ *
+ * The handle is what the title opened for the query (XAPI's GetDiskFreeSpaceEx
+ * opens the drive root, e.g. U:\, with FILE_OPEN_FOR_FREE_SPACE_QUERY), so
+ * its own volume is the right one: saves can live on a different drive from
+ * the working directory (NFL2K5_ROOT). The working directory is only the
+ * fallback.
+ *
+ * Clamped to XBOX_FREE_BYTES_MAX. Xbox titles were written for an 8 GB disk
+ * and do their own arithmetic on the result: NFL 2K5 divides the 64-bit byte
+ * count by its 16 KB block and then keeps only the low 32 bits as a signed
+ * int, and other code linked into titles (CRI's cvFsGetFreeSize, dashboard
+ * helpers) works in 32-bit bytes. Keeping the byte count below 2^31 is safe
+ * for every one of those, and 2 GB is still 131,071 blocks -- the game shows
+ * anything over 50,000 as "50000+ blocks" and its largest save is ~100.
+ *
+ * RECOMP_FREE_SPACE_MB=<n> overrides the host value (testing a full disk). */
+#include <stdlib.h>   /* getenv, strtoul */
+#define XBOX_FREE_BYTES_MAX 0x7FFFC000ull   /* 2 GB - one 16 KB cluster */
+static BOOL host_volume_space(HANDLE h, ULONGLONG *free_out, ULONGLONG *total_out)
+{
+    ULARGE_INTEGER avail, total, total_free;
+    WCHAR path[MAX_PATH + 8];
+    const char *fake = getenv("RECOMP_FREE_SPACE_MB");
+    BOOL ok = FALSE;
+    DWORD n = 0;
+
+    if (h && h != INVALID_HANDLE_VALUE)
+        n = GetFinalPathNameByHandleW(h, path, MAX_PATH,
+                                      FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
+    if (n && n < MAX_PATH) {
+        if (path[n - 1] != L'\\' && n + 1 < MAX_PATH) { path[n++] = L'\\'; path[n] = 0; }
+        ok = GetDiskFreeSpaceExW(path, &avail, &total, &total_free);
+        if (!ok) {   /* a file handle: ask for its directory */
+            WCHAR *slash;
+            path[n - 1] = 0;
+            slash = wcsrchr(path, L'\\');
+            if (slash) { slash[1] = 0; ok = GetDiskFreeSpaceExW(path, &avail, &total, &total_free); }
+        }
+    }
+    if (!ok)
+        ok = GetDiskFreeSpaceExW(NULL, &avail, &total, &total_free);
+    if (!ok)
+        return FALSE;
+    *free_out = avail.QuadPart;
+    *total_out = total.QuadPart;
+    if (fake && *fake)
+        *free_out = (ULONGLONG)strtoul(fake, NULL, 10) * 1024ull * 1024ull;
+    if (*free_out > XBOX_FREE_BYTES_MAX)
+        *free_out = XBOX_FREE_BYTES_MAX;
+    if (*total_out > XBOX_FREE_BYTES_MAX)
+        *total_out = XBOX_FREE_BYTES_MAX;
+    if (*total_out < *free_out)
+        *total_out = *free_out;
+    return TRUE;
+}
+
 NTSTATUS __stdcall xbox_NtQueryVolumeInformationFile(
@@ -564,3 +625,3 @@ NTSTATUS __stdcall xbox_NtQueryVolumeInformationFile(
 {
-    (void)FileHandle; (void)Length;
+    (void)Length;
     if (!IoStatusBlock || !FsInformation)
@@ -571,4 +632,13 @@ NTSTATUS __stdcall xbox_NtQueryVolumeInformationFile(
             PXBOX_FILE_FS_SIZE_INFORMATION info = (PXBOX_FILE_FS_SIZE_INFORMATION)FsInformation;
-            ULARGE_INTEGER free_bytes, total_bytes, total_free;
-            if (GetDiskFreeSpaceExW(NULL, &free_bytes, &total_bytes, &total_free)) {
+            ULONGLONG free_q, total_q;
+            if (host_volume_space(FileHandle, &free_q, &total_q)) {
+                ULARGE_INTEGER free_bytes, total_bytes;
+                static int logged;
+                free_bytes.QuadPart = free_q;
+                total_bytes.QuadPart = total_q;
+                if (logged++ < 4) {
+                    fprintf(stderr, "  [FILE] volume free space: %llu MB reported (%llu blocks)\n",
+                            free_q >> 20, free_q / (XBOX_BYTES_PER_SECTOR * XBOX_SECTORS_PER_CLUSTER));
+                    fflush(stderr);
+                }
                 info->BytesPerSector = XBOX_BYTES_PER_SECTOR;
@@ -581,4 +651,6 @@ NTSTATUS __stdcall xbox_NtQueryVolumeInformationFile(
                 info->SectorsPerAllocationUnit = XBOX_SECTORS_PER_CLUSTER;
-                info->TotalAllocationUnits.QuadPart = 1048576;
-                info->AvailableAllocationUnits.QuadPart = 524288;
+                /* the clamp, not 8 GB: see XBOX_FREE_BYTES_MAX */
+                info->TotalAllocationUnits.QuadPart =
+                    XBOX_FREE_BYTES_MAX / (XBOX_BYTES_PER_SECTOR * XBOX_SECTORS_PER_CLUSTER);
+                info->AvailableAllocationUnits.QuadPart = info->TotalAllocationUnits.QuadPart;
             }
```

### 2. Save-count limits (gen patches, tools/apply-gen-patches.py)

Capacity of the game's own structures (measured from the code):

- Load/Save list rows: static table `0xBDC1C8..0xBDD9C8`, **256 rows x 24 bytes**
  (`sub_0016AC40` fills it); this is the real hard limit of the in-game list.
- Names: heap buffer `[0xBDD9C8]`, 8 KB (256 names of 15 characters).
- The scanner's own records are heap-allocated per save (count x 48), no fixed limit.

| ID | Where | What |
|---|---|---|
| `SAVE_ENUM_CAP_255` (replaces `SAVE_ENUM_CAP_15`, now in RETIRED) | `sub_0003A950` | The hard disk's save count is reported as at most **255** (was 15). The lists show every save up to the table's size, and the stock ">= 256 saves" paths (status 3 "Save Game Limit Reached", free space zeroed) can never fire. |
| `SAVESCAN_SKIP_BAD` | `sub_0004C8E0` | The fill pass stopped at the first save it could not open, leaving the rest of the rows garbage (why the list had been capped to 15). That save is now skipped and the list stays contiguous; `[SAVE-LIST] a save could not be read` is logged. |
| `SAVE_NAMES_32K` | `sub_0016B060` | Name buffer 8 KB -> 32 KB (255 names of 63 characters). |
| `SAVE_SPACE_LOG` | `sub_0016B610` | Logs `[SAVE-SPACE] <free> free blocks, all save types need <n>: ok / NOT ENOUGH`. |

Helpers `nfl2k5_savescan_skipped` / `nfl2k5_save_space_log` are in `src/nfl2k5_save_library.cpp`.
The save library (name-derived folder IDs, repair pass, mirrors) is unchanged.

## Remaining real limits

- **Disk space**: the game warns at boot when the saves drive has less than ~313 blocks
  (~5 MB) free, and refuses a single save that does not fit. That is real PC storage. E: is
  currently 99-100% full; freeing space on E: is the actual cure for the user's message.
- **255 saves listed** in the in-game Load/Save/Delete lists (the 256-row table). Saves beyond
  that stay on disk and in the PC save library (`saves\franchise`, `rosters`, ...) but do not
  appear in the in-game list; creating new saves still works. Raising this means relocating the
  static table at `0xBDC1C8` (every reference to it); not done.
- Cosmetic, stock behaviour: when any listed save has a long name (here
  "SLUS-20919 ESPN NFL 2K5 NFL25RW1 (20AA9A70)", 43 characters, and "2026 NFL Rosters Week 2"),
  the list widget drops the Blocks and Type columns for the whole list. Verified: renaming those
  three saves (sandbox only) brings the columns back with all 126 saves listed. Before, it did not
  show only because the cap listed the first 15 saves.
- A pack running interpreted (SOFTDRINK) does not run gen patches: it gets fix 1 (kernel) but not
  the count patches. Not tested.

## Test evidence (background mode, sandbox)

Sandbox root `logs\saves-root` (junctions to original, data, mods, assets, cache, config,
dependencies, external, font, Music, tools + a full copy of `saves\`, 2,987 files, 126 saves),
LOCALAPPDATA sandbox, exe copy `build\Release\NFL2K5_saves.exe --retail`, RECOMP_BACKGROUND=1,
RECOMP_AUDIO_MUTE=1, NFL2K5_NO_HOST_PAD=always, press file, NFL2K5_PRIVATE_PACKS=1.

- Free space: `[FILE] volume free space: 2047 MB reported (131071 blocks)`;
  `[SAVE-SPACE] 131071 free blocks, all save types need 313: ok` (later 97,522-128,045 as E:
  dipped under 2 GB: real values pass through). No warning at boot or on save.
- Load list: all **126** saves pass the scanner (`[SAVESCAN] save check -> 1` x126); the list
  scrolls to the last save "Frani2k21" with clean names and no garbage. Old build, same sandbox:
  15 saves, then `<create new save>`.
- Save: Options > Load/Save > Save > Save Settings > `<create new save>` > "Settings3":
  "Successfully saved Settings Settings3 to Xbox Hard Disk."; new container
  `saves\UDATA\53450030\83C3760943CD` (SAVEGAME.DAT, EXTRA, TYPE, SaveMeta.xbx) and library folder
  `saves\settings\Settings3` in the sandbox. Device line: "Xbox Hard Disk [50000+ blocks]".
- Warning reproduced on purpose with `RECOMP_FREE_SPACE_MB=3`: `[SAVE-SPACE] 192 free blocks,
  all save types need 313: NOT ENOUGH`, and the game's "need an additional 111 blocks" box.
- Save library repair pass on renamed saves (sandbox): 3 saves moved to their name's folder.
- Real `saves\`: SHA-1 list of all 2,987 files before (`logs\saves-hash-before.txt`) and after
  (`logs\saves-hash-after.txt`) identical. Junctions removed with `cmd /c rmdir`, sandbox deleted.

## Notes

- Copying `saves\` with robocopy wrote the sparse `Partition1-5.img` files (4.8 GB apparent,
  ~1 MB real) at full size and briefly took E: to 16 MB free; they were replaced by sparse copies
  within about two minutes. Other processes writing to E: in that window may have failed.
- The first build's link failed with LNK1201 (PDB write, disk nearly full); the retry succeeded.
