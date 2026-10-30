# The Crib's celebrity phone calls, and a copy for My NFL Eras trade offers

2026-10-16. This is research only. No code was changed, nothing was built and the game was
not run. This page covers how retail NFL 2K5 rings the player for a Crib "Celebrity Challenge",
how the alert is shown on the Main Menu, what the call screen is, and how to copy all of it for
My NFL Eras CPU trade offers without touching the Crib, its saves or Franchise.

User request: "Phone calls for trades the game already alerts you when there is a phone call
from the crib let's copy that and make it where you get it for a trade" (PHASE2-PLAN.md, note
"Trade phone calls").

The evidence labels follow `NATIVE-MENU.md`:

* **verified (code)**: read in `src/recomp/gen` (`sub_XXXXXXXX` = Xbox address).
* **verified (XBE)**: read from `original/disc/default.xbe` with `build/eras/native-menu/xbe.py`.
* **verified (disc)**: read from the archive `original/disc/vc_53450030/0`.
* **verified (save)**: read from the VIP files in `saves/UDATA/53450030/*/SAVEGAME.DAT`.
* **inferred**: follows from the code but was not watched live.
* **to test**: needs a run.

---

## 0. Short version

* **There is no "call screen" descriptor of its own.** A Crib call has four parts, in two
  separate subsystems:
  1. **A countdown in the VIP's Crib record.** `sub_000EA050` runs every front-end frame. When
     the countdown reaches 0, it marks the call as pending (`+0x3E4C = 1`). The countdown,
     the caller and the pending flag are saved in the VIP file.
  2. **A popup banner.** `sub_0024ADB0` shows the shared "unlockables_popup" ticker for 5 s,
     with the text "Hey %s, you've got a phone call. Head to your Crib to see who's calling
     you!". Its icon is swapped to the `celebrity_phone_icon` texture, and it plays
     "Ambient: ESPN Ticker". This happens once, when the call arrives.
  3. **A phone icon on the Main Menu.** This is a fixed layout element, `cell_phone_icon_a`,
     in the `main_menu_sub` LAYT. It is drawn at layout position (-122, 80), beside the
     **third row slot**. A layout binding (hash `0x6AFC8790` -> `sub_0024D460`) switches it on
     and off every frame from the pending flag. **It never looks at the rows.** That is why
     it appears beside whatever row sits in slot 3, and why it showed beside My NFL Eras.
  4. **A call screen inside The Crib.** It is Crib sub-state 19, descriptor `0x00524480`, and
     you reach it by walking to the phone in the Crib room. The screen shows:
     * a panel with the celebrity's still portrait (TXTR named by the celebrity's key);
     * the celebrity's name, typed in;
     * "You're being challenged to a game against a celebrity! Do you want to take your
       chances and play against %s's VIP?";
     * two choices, "Okay, let's do this!" and "Later...I'm busy";
     * the celebrity's recorded voice lines (speech banks), over the 3D Crib.
* **No telephone ring sample exists.** The "ring" is made of three things:
  * the ticker sound;
  * the animated phone icon (it has pulsing waves);
  * the `phone_anim` node, shown in the Crib room while a call is pending.
* **Copy plan.** Nothing below needs a generated-code patch. Everything is Eras-owned, served
  from the `0xFEE00000` function range the Eras menus already use:
  * **(a)** Raise the same popup with our own text and icon, through the same two calls.
  * **(b)** Mark the "My NFL Eras" Main Menu row and the office with our own
    phone-icon draw, or a label change.
  * **(c)** Build a new Eras descriptor, "EM_PHONE". It redraws the retail call-screen layout
    with the calling team's helmet or logo, the offer text and three choices: Accept,
    Decline, Counter.
  * **(d)** Fix the existing defect. Repoint the main menu's one-entry binding table to an
    Eras function that hides the retail icon unless The Crib is in slot 3.
* The Eras offer state lives in the Eras league folder. The copy never reads or writes the
  VIP Crib record, never calls the Crib phone functions, and waits while the shared popup is
  busy.

---

## 1. How a Crib call is triggered

### 1.1 Where the state lives (verified (code) + verified (save))

There is one **Crib record** per VIP slot: `0x3E78` bytes at
`0x00BC6E40 + slot * 0x3E78`. The slot is the VIP index from the active VIP pointer
`[0x00E5FE60]`:

* `sub_00077560` returns `[0xE5FE60]`, the active VIP pointer (0 = no VIP).
* `sub_00191D20` / `sub_00191CE0` turn it into an index: `(ptr - 0xBDFCF0) / 0x1278`.
* VIP profiles are `0x1278` bytes each at `0x00BDFCF0`.

The phone fields sit at the end of the Crib record. The accessors `sub_0013FC50..0013FD50`
use `0xBC7C20 + slot*0x3E78 + 0x3064..`, which is the same place as record offset `+0x3E44..`.

| Record offset | Absolute (slot 0) | Accessors | Meaning |
|---|---|---|---|
| `+0x3E44` float | `0xBCAC84` | get `sub_0013FC50`, set `sub_0013FC60` | **Seconds until the next call** |
| `+0x3E48` | `0xBCAC88` | `sub_0013FC80` / `sub_0013FC90` | **Caller**, 1..5 (0 = not chosen yet) |
| `+0x3E4C` | `0xBCAC8C` | `sub_0013FCA0` / `sub_0013FCB0` | **Ringing / call pending** (1) |
| `+0x3E50` | `0xBCAC90` | `sub_0013FCC0` / `sub_0013FCD0` | Count increased after a challenge game (`sub_000EA500`). It picks recap lines. Inferred: games against this caller |
| `+0x3E54` | `0xBCAC94` | `sub_0013FCE0` / `sub_0013FCF0` | **Times declined** (+1 in `sub_000EA440`). It picks the "you blew me off before" line |
| `+0x3E58` | `0xBCAC98` | `sub_0013FD00` / `sub_0013FD10` | **Accepted challenge**: the celebrity whose game is set up or awaiting its recap |
| `+0x3E5C` | `0xBCAC9C` | `sub_0013FD20` / `sub_0013FD30` | Challenge result code 1..6 (`sub_000EA500`), used for the recap |
| `+0x3E64 + 4*n` | `0xBCACA0 + 4n` | `sub_0013FD40` / `sub_0013FD50` | Celebrity *n* beaten (in Speed Dial), n = 1..5 |

The VIP profile also has a flag at `+0x1260`, read by `sub_00196DE0` and set by
`sub_00196DF0`. It stops all further calls. `sub_000EA5C0` sets it to 1 once no unbeaten
celebrity is left.

**Saved.** A VIP save file is the profile followed by the Crib record:
`0x1278 + 0x3E78 = 0x50F0` = 20720 bytes. That is exactly the size of every VIP
`SAVEGAME.DAT`. `sub_00192600` returns that size, and the VIP load `sub_00196EA0` copies
`0x1278` bytes and then `sub_001401D0` copies the `0x3E78` Crib bytes. So the phone fields
are at **file offsets `0x50BC..0x50EF`**. Decoded from the user's saves (read only):

| Save | countdown | caller, ringing, games, declines, accepted, result | speed dial |
|---|---|---|---|
| `1A931897A619` "CribMax" | 174.9 s | 0, 0, 0, 0, 0, 0 | none |
| `AB1B495E618B` "CribMax (2)" | 269.4 s | 0 ... | none |
| `004900501403` "VIP1" | 259.5 s | 0 ... | none |

So the countdown carries across sessions. A VIP that is loaded at boot rings after about
3 to 4.5 minutes of front-end time. This matches the M3 runs, where the phone appeared some
minutes into each test.

Globals used during a call (Crib RAM, not saved):

* `0xAC8750`: the caller shown on the call screen.
* `0xAC8754`: the cursor (1 = "Okay").
* `0xAC8758`: choice made.
* `0xAC8778`: typing timer.
* `0xAC875C`: portrait fade.
* `0xAC7458`: Crib slot.
* `0xB9C214` / `0xB9C21C` / `0xB9C220`: the challenge game's Crib slot, celebrity, and home
  or away side.

### 1.2 The trigger, `sub_000EA050(float dt)` (verified (code))

The front-end loop `sub_00074790` calls `sub_000EA380(dt)` every frame (`0x000747EA`).
`sub_000EA380` calls `sub_000EA050(dt)`, and then the celebrity-game face overlay
`sub_000EA1A0`, which is not part of the call.

`sub_000EA050` does nothing if any of these is true:

| Check | Global | Meaning |
|---|---|---|
| `sub_00064BF0() != 0` | `[0xA83A1C]` | set by `sub_00064710`, cleared by `sub_000649C0`. Inferred: a match session is running |
| `sub_00270BA0() != 0` | `[0xAC7444]` | **in The Crib** (set by the Crib's msg 1 `sub_00270490`, cleared by msg 2 `sub_00270600`) |
| `sub_00077560() == 0` | `[0xE5FE60]` | no VIP loaded |
| `sub_00196DE0(vip) != 0` | VIP `+0x1260` | phone retired (all celebrities beaten) |
| `crib.+0x3E4C != 0` | | already ringing (the countdown is frozen while a call waits) |

Otherwise it runs:

```
t = crib.timer - dt
if t <= 0 (fcomp [0x4E4180] = 0.0) and the popup is idle (sub_0024AD80() == 0):
    icon = sub_000449E0(ctx 0, 'TXTR', L"celebrity_phone_icon" @0xE6A818)
    sub_0024AD60(ecx = icon)                         ; popup icon
    swprintf(buf, L"Hey %s, you've got a phone call. Head to your Crib to see who's calling you!" @0xE6A848,
             sub_001926A0(vip))                     ; VIP name (vip+4)
    sub_0024ADB0(ecx = buf, 5.0f)                   ; popup for 5 s
    if crib.caller == 0: crib.caller = sub_000E9FF0(slot)   ; random unbeaten celebrity 1..5
    crib.ringing = 1
crib.timer = t                                       ; also kept when the popup is busy: retried next frame
```

The countdown is reset to `300 + rand01 * 600` s (5 to 15 minutes) in two places: after a
decline (`sub_000EA440`) and after a finished challenge (`sub_000EA5C0`). The constants are
`[0x4E4188]` = 300.0 and `[0x4EEA38]` = 600.0, and `sub_00048BD0` is the random float. The
first value given to a new VIP was not traced (**to test**).

Nothing in the trigger checks Franchise. A call can arrive while Franchise is loaded. The
Crib's phone then refuses with "The Celebrity Phone is only active outside Franchise and
Tournament modes." (`0xE8D948`; gate `sub_000C4B80` = `[0xE576A0]`).

The callers are the table at `0x004F5FF8`: 12-byte rows of `{display name, asset key, id}`.

| # | Name | Key | Id |
|---|---|---|---|
| 1 | Carmen Electra | carmen_electra | 0x61 |
| 2 | David Arquette | david_arquette | 0x60 |
| 3 | Funkmaster Flex | funkmaster_flex | 0x62 |
| 4 | Jamie Kennedy | jamie_kennedy | 0x5F |
| 5 | Steve-O | steve_o | 0x63 |

`sub_000EA400` returns the name and `sub_000EA3F0` returns the key.

---

## 2. How the alert is shown

### 2.1 The phone icon beside a Main Menu row (verified (code) + verified (disc) + M3 screenshots)

* The Main Menu descriptor `0x00515660` has layout `main_menu_sub` (+0x18) and layout
  binding table `0xAC60D0` (+0x1C). `sub_00143EA0` copies its words into the layout instance.
  Its `+0x10` points at `0xAC60B8`, a one-entry list `{0x6AFC8790, 0x0024D460}, {0}`
  (verified (XBE)).
* LAYT `main_menu_sub` is at archive offset `0x73159C` in `vc_53450030/0`. It has three
  elements:
  * `main_navi` (scene `navigation`);
  * `title_bar_short_sub` (scene `title_bar`);
  * `cell_phone_icon_a` (scene **`celebrity_phone`**).

  The third element record, at archive offset `0x731700`, carries the hash **`0x6AFC8790`**
  and the position floats **(-122, 80, 30)**. The retail `main_menu` LAYT (`0x7313BC`) has the
  same element.
* `sub_0024D460(edx = layout element)` is the binding callback. It ends in `ret 4`. It does:

  ```
  vip = [0xE5FE60]
  element+0x38 = vip ? sub_000EA410(sub_00191D20()) : 0
  ```

  `sub_000EA410(slot)` returns 1 when a call is waiting:
  `(caller != 0 && ringing == 1) || accepted != 0`. Element `+0x38` is the element's
  "active" flag. `sub_00143EA0` sets it to 1 when the element's scene loads and to 0 when
  the scene is missing.
* **How it picks the row: it does not.** The element sits at a fixed spot in the layout,
  beside the third row slot, where The Crib is in the retail 7-row menu. The binding only
  switches it on and off. With "My NFL Eras" at index 2, the icon was beside My NFL Eras
  (`logs/eras/m3g-main2.png`). With Eras at index 3, it is beside The Crib while the list is
  not scrolled (`logs/eras/m3h-main2.png`). When the list scrolls to Extras, slot 3 holds
  My NFL Eras and the icon is beside it again (`logs/eras/m3h-m5.png`).
* The `celebrity_phone` scene is animated. In `m3h-main2.png` the handset shows pulsing
  "ring" waves, and in `m3h-m5.png` it is caught between pulses. This animation is the
  visible "ring" on the menu.

### 2.2 The popup banner (verified (code))

The popup is the shared "unlockables" ticker. It is also used for "%s%s has earned %u Crib
Credits" (`sub_0010F150` / `sub_0010F440`).

| Function | Does |
|---|---|
| `sub_0024A810` (init; called through a pointer, the caller was not traced) | Text context `0xAC5DA0`: font 2, align 4, colour `0xFFC0C0C0`, box 0x6E x 0x258. Scene **`unlockables_popup`** from context **`GLOBAL`** (`0xE89C2C`, `0xE89C1C`) -> `[0xAC6034]`. Element **`z_crib_logo`** (`0xE89C50`) -> `[0xAC6038]`. Then `0xAC5AE0 \|= 1` (ready) |
| `sub_0024AD60(ecx = TXTR)` | If ready: `[[0xAC6038]+0x30] = texture` (**the icon**) |
| `sub_0024ADB0(ecx = wstr, float seconds)` | Reset (`sub_0024A940`), `0xAC5AE0 \|= 2` (showing), copy the text to `0xAC5E34` (0x100 chars), `sub_0024A710(seconds)` (measures the text, sets the slide distance, hold time `0xAC5E2C`, `0xAC5AE0 \|= 8` = play sound), state `[0xAC5AE4] = 2` |
| `sub_0024AD80()` | 1 while the popup is busy (state 2..7) |
| `sub_0024A960(dt)` (from `sub_00110F40`) | Slide in and out at 600 units/s. The first frame with flag 8 plays **`L"Ambient: ESPN Ticker"`** (`0xE89C68`) through `sub_00038650` + `sub_00089DA0` |
| `sub_0024AC10` (draw; called through a pointer) | Draws the popup scene, then the text at (110, 448 + slide) in font 2 |

**Sound.** No ring sample exists. Every `Ambient:` name in the XBE was listed: the trigger
plays only the ESPN Ticker swoosh. In the Crib, the menu choices use "Ambient: Menu Move
Cursor" / "Menu Select" / "Menu Back" (`0xE8DA28`, `0xE8D868`, `0xE8DA00`). The call's audio
is the celebrity's own recorded voice (2.4).

### 2.3 In The Crib: the phone animation (verified (code))

`sub_00276020` (Crib load) finds scene **`phone`** (`0xE8F830`) and node **`phone_anim`**
(`0xE8D710`) and keeps the node in `[0xAC77A0]`. Every Crib frame, `sub_0026DBE0` shows that
node (clears node flag `0x10`) while `sub_000EA410` says a call is waiting. It hides the node
during the phone sub-states 18-20.

### 2.4 Voice (verified (code))

`sub_002702B0(eax = line)` picks a speech id from the line table `0xAC9178`. It uses one
range per celebrity:

| Caller | Speech id range |
|---|---|
| 1 | 10000-19999 |
| 2 | 20000-29999 |
| 3 | 30000-39999 |
| 4 | 40000-49999 |
| other | 0-9999 |

It plays the line through `sub_00067120` / `sub_00066E80` / `sub_00072A50`.
`sub_002703B0` ducks the music volume `[0xE601C8]` to 0.05 and keeps the old value in
`0xAC8788`. `sub_00270360` returns 1 once the line has finished.

---

## 3. The call screen

### 3.1 Getting there (verified (code) + verified (XBE))

* The Crib (`0x00524FA0`, custom handler `0xF3DA0`) runs a 25-state machine:
  * dispatch `sub_00278390` (jump table `0x27861C`);
  * set-state `sub_00276810` (enter table `0x277008`);
  * state variable `[0xAC7448]`.

  Each sub-state is also a pushed descriptor of its own. That descriptor's msg 1 / msg 2
  set the state, and msg 6 / msg 9 run the Crib's frame and draw.
* In state 3, `sub_002773A0` moves the cursor round a hotspot graph:
  * the current hotspot is `[0xAC8514]`;
  * the records are 20 bytes at `0x51AD30`, `{up, down, left, right, object}`;
  * the button bits are Up `0x1000000`, Down `0x2000000`, Left `0x4000000`,
    Right `0x8000000`.

  The phone is object kind 7, "Celebrity Phone & Bar Items", at hotspots 31 and 32. One path
  is hotspot 2 -> Up -> 32. A on an object calls `sub_00277220(kind)`.
* `sub_00277220`, kind 7 (`loc_0027724B`), checks in this order:
  1. Franchise or Tournament active (`[0xE576A0]`): an OK box with `0xE8D948`.
  2. `accepted` (`+0x3E58`) set: `[0xAC8750] = accepted`, push **`0x00524648`** (state 20,
     the post-game recap).
  3. `caller != 0 && ringing == 1`: `[0xAC8750] = caller`, push **`0x00524480`** (state 19,
     **the call**).
  4. Speed Dial count `sub_00270280() > 0`: push `0x005242B8` (state 18, Speed Dial table).
  5. Otherwise an OK box: "CELEBRITY PHONE ... You've got no one to call!" (`0xE8D898`).

### 3.2 The descriptor `0x00524480` (verified (XBE))

* Title: none.
* Events `0x00524450`:
  * msg 4 `sub_00278310`;
  * msg 6 `sub_00278390` (Crib frame);
  * msg 9 `sub_00275B70` (Crib draw: 3D room plus the per-state 2D overlay);
  * msg 1 `sub_002787A0` (set state 19);
  * msg 2 `sub_002787B0`.
* Handler `0xF3DA0`. Help 0. Rows 0. Layout none.
* **Flags 0**: Back does nothing, so the player must choose.

Entering state 19 (`sub_00276810`, `loc_00276EF9`):

* sets the cursor `[0xAC8754] = 1` ("Okay");
* plays the opening line: line 0 normally, line 1 if this VIP declined before
  (`+0x3E54 != 0`), line 0xC if the shown celebrity is not the caller;
* ducks the music.

### 3.3 What is drawn: `sub_00274F30` (from `sub_00275B70`) (verified (code))

All coordinates are 640x480 front-end units.

| Part | Calls | Values |
|---|---|---|
| Screen dim / frame | `sub_0026E0F0`, `sub_0026E1D0` | |
| **Caller portrait** | `sub_0026EBE0(58, 127, 256, 256)` | Dark panel `sub_00069880` colour `0xFF26254A`, border `sub_0026DD60`, then TXTR named by the celebrity key (`sub_000EA3F0`, e.g. `carmen_electra`), looked up in the Crib package context `[0xAC9078]`, drawn by the textured-quad helper **`sub_0026DE30`** with a fade-in `[0xAC875C]`. **A still portrait, not a video** |
| **Name** | font 6, align 3, `0xFFC0C0C0`, at (320, 92) | `sub_000F1D50(ctx, name, 3, [0xAC8778], 1.0)`: typed in, letter by letter |
| Choice bar | `sub_0006BBE0` colour `0xFF26254A` at x 370, y 285 (Okay) or 313 (Later), width 0xC4, height 0x1B; pulsing alpha from `[0xAC8524]` | plus `sub_0026D2A0` / `sub_0026DD60` frame |
| **Choices** | font 2 at (468, 284) and (468, 312) | **"Okay, let's do this!"** `0xE8EAA8` / **"Later...I'm busy"** `0xE8EAD4`. Selected `0xFFC0C0C0`, other `0xFF808080` |
| **Body** | font 2, align 1, `0xFF808080`, box 0x154 x 0x84 at (344, 132), wrapped by `sub_000F1C80` | "You're being challenged to a game against a celebrity! Do you want to take your chances and play against %s's VIP?" (`0xE8EAF8`, %s = name) |

The title string "Celebrity Challenge" (`0xE8EA80`) is loaded into `edi`, but the code shown
does not draw it.

### 3.4 Input and outcome: `sub_00272800` (state 19 frame) (verified (code))

* Up / Down (`sub_00070A10` pad word bits `0x1000000` / `0x2000000`) toggle `[0xAC8754]`.
  Each move plays "Menu Move Cursor".
* A or Start (bits `0x110`):
  * **Okay**: voice line 2 (or 0xD for another caller). `sub_000EA4B0(slot, celeb)`: set up
    the challenge game (`0xB9C214/1C/20`, `accepted = celeb`). "Menu Select".
  * **Later**: voice line 3 (or 0xE). `sub_000EA440(slot, celeb)`: `ringing = 0`, countdown
    `300 + rand*600`, declines + 1, clear `accepted` / result. "Menu Back".
* After the line finishes (`sub_00270360`):
  * if `accepted`: `sub_0026D710`, `sub_0014E4B0(0)`, then push **`0x005262D0` "Team Select"**
    (the celebrity game);
  * else: replace with the Crib `0x524FA0` (`sub_0006E450`), and push the Speed Dial
    `0x5242B8` if any celebrity is on it.
* After the challenge game: `sub_000EA500` (from `sub_000A2910`) stores the result code.
  State 20's `sub_000EA5C0` adds the celebrity to Speed Dial on a win, resets the countdown,
  and sets the profile's `+0x1260` once all five are beaten.

---

## 4. Copying it for My NFL Eras trade offers

### 4.1 Principles

* **Eras-owned state only.** An offer lives in the league folder, in a new `offers.csv` or a
  `[phone]` section of `league.ini`. It holds: id, calling team, players given and received,
  week created, status (pending, declined, accepted, countered) and a "popup shown" flag.
  **Never** read or write the VIP Crib record (`0xBC6E40 + slot*0x3E78`, file `0x50BC..`).
  Never call `sub_000EA050`, `sub_000EA440`, `sub_000EA4B0`, `sub_000EA500` or
  `sub_000EA5C0`, and never write `0xAC8750..0xAC8788`.
* **Reuse only the engine primitives**, the same way the office does:
  * popup: `sub_0024AD60`, `sub_0024ADB0`, `sub_0024AD80`;
  * text: `sub_00046920`, `sub_000469B0`, `sub_00046A00`, `sub_00046A70`, `sub_00046B50`,
    `sub_00046BB0`, `sub_000EF850`, `sub_000F1C70`, `sub_000F1C80`, `sub_000F1D50`,
    `sub_0004A400`;
  * panels: `sub_00069880` / `sub_0006BBE0`;
  * textured quad: `sub_0026DE30`. It is generic: its only global is the static UV table at
    `0xAC91B8`, and nothing writes that table (verified (code), and the XBE values are
    0/1 UVs);
  * resource lookup: `sub_000449E0`;
  * sounds: `sub_0014D390`, or `sub_00038650` + `sub_00089DA0`.
* **All new entry points** are `0xFEE0xxxx` functions served by `nfl2k5_eras_menu_lookup`.
  No `tools/apply-gen-patches.py` entry is needed.

### 4.2 Alert 1: the popup ("the same ring")

Add an Eras per-frame check, `eras_phone_tick()`. It is called from:

* a **copy of the Main Menu event table** `0x515490` with one more msg-6 type-1 action
  (`0xFEE0....`). Repoint `MAIN_MENU+0x04` at install, next to the existing row insert in
  `nfl2k5_eras_menu.c` line ~895;
* the hub and office msg 6 that `event()` already handles.

```
if offer pending && !popup_shown
   && MEM32(0xA83A1C) == 0            // no match session (same gate as retail)
   && MEM32(0xAC7444) == 0            // not in the Crib
   && (MEM8(0xAC5AE0) & 1)            // popup scene loaded
   && gcall(sub_0024AD80) == 0:       // popup idle: never cut off a Crib/credits popup
    icon = lookup(0, 'TXTR', L"celebrity_phone_icon")   // or the team helmet (4.4)
    gcall(sub_0024AD60, icon, 0)
    swprintf(buf, L"%s GM on line one: a trade offer. Open My NFL Eras to take the call.", team)
    PUSH32(5.0f); gcall(sub_0024ADB0, buf, 0)  // plays "Ambient: ESPN Ticker" itself
    popup_shown = 1 (saved with the offer)
```

This gives the retail look and sound: the same banner, swoosh and phone icon. The retail
call keeps working. If ours is showing, `sub_000EA050` sees `sub_0024AD80() != 0`, keeps its
countdown at <= 0 and rings the next idle frame. The reverse also holds: ours waits.

### 4.3 Alert 2: the icon on the row

* **Office (`EM_OFFICE`, fully ours).** Two parts:
  * Put a row at the top, "Phone: Dallas Cowboys calling", that shows only while an offer is
    pending. Use the existing row update `+0x2C` to hide it, as the depth and label rows do.
    Its action pushes `EM_PHONE`.
  * Draw the phone icon beside that row from a new msg-8 overlay. Use `sub_0026DE30` with
    the `celebrity_phone_icon` TXTR at the row's slot position. The nav text slots come from
    the binding `0xAD0018`; the first visible row is `[0xC8E1CC]`.
* **Main Menu "My NFL Eras" row.** The retail element `cell_phone_icon_a` cannot serve it: it
  is fixed beside slot 3, and Eras is in slot 4 when the list is not scrolled. Use one or both
  of these:
  * **Label (cheap, certain).** Rewrite the row label in place to "My NFL Eras - Call!" while
    an offer is pending. M3 found that label buffers rewritten in place update live.
    Check the width against the `main_navi` text clip.
  * **Own icon (looks like retail; to test).** Use the copied Main Menu event table (4.2) and
    add a msg-8 action that draws `celebrity_phone_icon` with `sub_0026DE30`, beside the
    visible slot of row index 3:
    * `slot = 3 - [0xC8E1CC] + 1`; draw only if `1 <= slot <= visible`;
    * y = slot-3 position + `(slot - 3) * row pitch`;
    * the pitch is about 42 px in the 720x480 captures (`m3h-main2.png`), to be measured in
      layout units.

    The retail icon is an animated scene (`celebrity_phone`). A TXTR quad is static unless
    the alpha or size is pulsed in code. Drawing the scene itself would need a 2D camera:
    look at how `sub_000F2F70` draws LAYT scenes.

### 4.4 The Eras call screen: descriptor `EM_PHONE`

* **Descriptor.** A copy of a custom-screen header (handler `0xF3DA0`, for example Controller
  Setup `0x52C6FC`, as `em_begin` does), with no rows. Use **flags 0x10**: unlike retail,
  B = Decline, because the Eras call has a third choice and a Back is friendlier. Use flags 0
  if the user wants it exactly as retail. Event table of our own:

  | msg | Does |
  |---|---|
  | 1 | load the offer and the caller's art; reset the typing timer and cursor; optional music duck, keeping our own copy of `[0xE601C8]` and restoring it at msg 2 (never use `0xAC8788`) |
  | 4 | "Ambient: Menu Appear" |
  | 6 | input: the pad word via `sub_00070A10(pad, 0)` or `PAD_WORDS 0xB37A78`. Up / Down move over 3 choices with "Menu Move Cursor"; A chooses; B declines |
  | 7 | if pushed over the office, draw the office room (`nfl2k5_eras_office_draw`), so the call sits over the 3D room as the Crib's does |
  | 8 | the overlay below |
  | 10 | Decline |
  | 2 | restore the music |
* **Overlay.** The same layout as `sub_00274F30`, with these parts:
  * The dark panel at (58, 127, 256, 256) (`sub_00069880` `0xFF26254A` + border), with the
    **calling team's helmet or logo** inside:
    * Option 1: the Franchise mini helmet TXTR `"mini_a%s_%d"` (asset code, variant).
      Retail only looks it up in ctx `L"FRMINI"` (`mini.iff`), which Franchise loads. Eras
      would have to load `mini.iff` under its own context, which is to test
      (FRANCHISE-SCREEN-COPY.md).
    * Option 2: the team logo the office pack already binds (`team_logo` TXTR in
      `erasskin.iff` or the era pack).
    * Option 3: a large team logo served by the texture override (`tools/export_team_logos.py`
      assets).
    * The GM: no GM portraits exist in retail. Use the helmet or logo with the line "General
      Manager" under the name.
  * **Caller line**, typed in, font 6 at (320, 92): "Dallas Cowboys", through
    `sub_000F1D50` with our own timer.
  * **Body**, font 2 in the box (344, 132, 0x154 x 0x84): "The Dallas Cowboys want to trade
    QB Danny White and a 1981 2nd-round pick for your WR Lynn Swann. Do you want to make the
    deal?" Build it with `sub_0004A400`.
  * **Three choices** at x 468, y 284 / 312 / 340: **Accept**, **Decline**, **Counter**.
    The bar uses `sub_0006BBE0` `0xFF26254A` at y 285 / 313 / 341, with the same pulse.
    Selected `0xFFC0C0C0`, other `0xFF808080`.

    The third row reaches y 354. The panel bottom is 127 + 256 = 383, so it fits. Check it
    against the popup area (y 448+).
* **Outcomes**, all in Eras data:
  * **Accept**: "Menu Select", then push the Eras trade screen in review mode (the future
    Trades milestone, built on the spreadsheet engine per the PHASE2 user rule). It applies
    the swap to the Eras rosters only. Then clear the offer.
  * **Decline**: "Menu Back", status declined, schedule the next offer window (an Eras
    counterpart of the 5-15 min countdown; per week fits a season better), pop.
  * **Counter**: push the Eras trade screen with the offer prefilled. The AI answers
    afterwards, and may phone back with a new offer.
* **Voice.** The celebrity speech banks belong to the five celebrities, so do not reuse them.
  Stay silent with menu sounds. An optional new "phone ring" sound would need a new audio
  asset (no retail sample).

### 4.5 Fixing the icon beside "My NFL Eras" (the M3 defect)

**Cause.** `cell_phone_icon_a` is fixed at layout (-122, 80), beside slot 3, and the
binding only switches it on or off (2.1). Eras at index 3 keeps The Crib in slot 3 only while
the list is not scrolled.

**Fix (data only, Eras install time, retail only).** At install, repoint the binding entry
**`MEM32(0xAC60BC)`** from `0x0024D460` to an Eras function `0xFEE0....`. Leave it alone
when the Eras row is not inserted (pack builds, `NFL2K5_ERAS_MENU=modes`). The Eras function:

```
// called like sub_0024D460: edx = layout element, one stack argument, must end with g_esp += 8 (ret 4)
on = MEM32(0xE5FE60) && sub_000EA410(sub_00191D20())          // the retail condition, same calls
crib_slot = 2 - MEM32(0xC8E1CC) + 1                            // The Crib is row index 2
if (crib_slot != 3) on = 0                                     // only beside The Crib
MEM32(element + 0x38) = on
```

Notes:

* Writing `0xAC60BC` changes only the Main Menu layout's binding: the table hangs off
  descriptor `0x515660` `+0x1C`. The retail look is unchanged in the unscrolled state.
* When the list is scrolled to Extras, the retail icon would be beside My NFL Eras, so it is
  hidden instead. A pending Crib call is still announced by the popup and the Crib room.
* **To test:** that `[0xC8E1CC]` is the Main Menu's first visible row (it is the nav layouts'
  first-visible variable, set by `sub_002C8880`). Check with `NFL2K5_MEMDUMP=C8E1CC:4` while
  scrolled and not scrolled.
* Fallback if it is not: compare the cursor index (`FSM stack[depth]+4`) with the visible
  count 7. The list scrolls only when the cursor reaches row 7 (Extras).
* A fully retail-identical alternative: put Eras at the **end** of the list (index 7, after
  Extras). Slot 3 then holds The Crib when the list is not scrolled and Features when it is,
  so the icon is never beside Eras. It would still sit beside Features when scrolled, so the
  4.5 rule is needed anyway. The cost is that My NFL Eras is visible only when the list is
  scrolled. Not recommended.

### 4.6 Implementation plan (after the Trades screen, per PHASE2-PLAN note)

1. **Offer store**: `nfl2k5_eras_offers.c`, which reads and writes `offers.csv` in the league
   folder. The AI generator is part of the Trades milestone. Until then, a test hook such as
   `NFL2K5_ERAS_TEST_OFFER=dal,<give>,<get>` creates one.
2. **Defect fix (4.5).** It is independent and can ship first.
   * Test: the Main Menu with a pending Crib call, unscrolled (icon beside The Crib) and
     scrolled (no icon).
   * Use `NFL2K5_MEMDUMP=C8E1CC:4,AC60B8:8`.
3. **Popup (4.2)**: the Main Menu event-table copy, `eras_phone_tick`, a log line
   `[ERAS] phone: popup for offer N`.
   * Test: the banner and swoosh appear once.
   * Test: no collision with a Crib popup. Let both be due in the same session: they must
     show one after the other.
4. **Row marks (4.3)**: the office "Phone" row and overlay icon, and the Main Menu label (then
   the icon if wanted).
5. **`EM_PHONE` screen (4.4)**: the layout first with fixed text, then the helmet or logo
   art. Compare it side by side with a retail call screenshot before shipping (user rule:
   "make it look real good").
6. **Outcomes** wired into the Eras trade screen when it exists. Until then, Accept and
   Counter refuse with a label, as "Play Next Game" did in M3.
7. **Regression checks:**
   * The VIP `SAVEGAME.DAT` phone bytes (`0x50BC..0x50EF`) are unchanged by any Eras action.
     Compare MD5s before and after an Eras session with no Crib visit.
   * The retail Crib call still rings, shows the retail icon and answers.
   * Franchise Coach's Desk: no Eras popup appears while Franchise is loaded. Also gate on
     `[0xE576A0] == 0`.

### 4.7 Risks

| Risk | Mitigation |
|---|---|
| The shared popup is used by Crib credits and the Crib call too | Always check `sub_0024AD80()` and the ready bit. Show once per offer. Set our own icon each time: the retail callers set theirs too |
| `0xAC60BC` repoint while a pack runs: the interpreter cannot call `0xFEE` functions (NATIVE-MENU 2.2 hazard) | Install only when `retail != 0`, as the Eras menus already do |
| `[0xC8E1CC]` might not be the Main Menu's first-visible index | Test first. Fallback is the cursor-based rule (4.5) |
| Helmet TXTR context `FRMINI` belongs to Franchise | Load `mini.iff` under an Eras context, or use the office's team-logo TXTR. Never load or free Franchise's context |
| The music duck leaves the menu quiet if the screen is left oddly | Keep our own saved value. Restore it at msg 2 and msg 5 |
| An offer pending across a match | Gate on `[0xA83A1C]`. The offer stays pending in `offers.csv`; the popup comes back at the menus |
| Crib saved state | Never written. VIP file bytes checked in the regression step |
| Franchise | No Franchise globals or functions. Gate on `[0xE576A0]` so a loaded Franchise never sees an Eras popup |

---

## 5. In-game evidence and what was blocked

* **Phone icon (retail, beside slot 3):** M3 captured it.
  * `logs/eras/m3h-main2.png`: The Crib, ring waves visible.
  * `logs/eras/m3h-m5.png`: scrolled, beside My NFL Eras.
  * `logs/eras/m3g-main2.png`: Eras at index 2.
* **Call screen and popup: not captured.** Two problems blocked a run.
  * **The PC was in use.** EA SPORTS College Football 27 was open, and the memory rule says
    to skip in-game tests then. `NFL2K5_ref` was also running for another agent.
  * **The run could not be sandboxed.** Saves are not under `LOCALAPPDATA`. `main.c` puts
    them in `<root>\saves` (line 2951), and the VIP autoloads from there. Reaching the call
    screen means entering The Crib, and answering would change and save the user's real VIP
    Crib record. A safe capture needs `NFL2K5_ROOT` pointed at a sandbox root with a copied
    `saves\` folder (and `original\`, `data\`, `mods\` reachable). Then:
    * wait about 3-4.5 min on the Main Menu for the popup (the saved countdowns are
      175-270 s);
    * enter The Crib;
    * walk the hotspot graph to the phone (hotspot 2 -> Up -> 32), press A;
    * capture, and stop the process without choosing.

    Not done here.
* Nothing was launched, no copy of the executable was made, and nothing needs cleaning up.

## 6. Reproduce

* XBE tables: `python build/eras/native-menu/xbe.py` helpers.
  * Celebrity table: `0x4F5FF8`, 12-byte rows.
  * Hotspots: `0x51AD30`, 20-byte rows.
  * Crib state tables: `0x27861C`, `0x277008`.
  * Binding: `0xAC60B8`.
  * Descriptor dumps: `python build/eras/native-menu/dump_menu.py 524480 0`, and `evt.py
    e:524450`.
* LAYT: archive `original/disc/vc_53450030/0`, offset `0x731570` (`main_menu_sub`), element
  record `0x731700`.
* VIP fields: `SAVEGAME.DAT` (20720 bytes), float at `0x50BC`, dwords from `0x50C0`.
