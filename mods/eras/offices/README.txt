My NFL Eras: the office looks
=============================

The My NFL Eras league screen is a 3D office, a copy of Franchise mode's offices
(docs/eras/ERAS-OFFICE-DESIGN.md). Each era from the 1970s to 2025 has its own look. The
look follows the league's season (Auto: the 1980 season uses the 1980s office); the office's
"Office Look" row changes it and saves it in the league, and LT / RT in the office browse the
looks for the session.

Nothing here changes Franchise mode: these folders are read only while the Eras office shows.

Before the first run
--------------------
The rooms are copied from your own game disc, once:

    python -I tools\eras\build-office-pack.py

It writes mods\eras\office\files\erasoffice.iff and erasskin.iff (retail data from your disc;
never shared or committed). Without them the league screen is a plain list.

Folders
-------
    office-textures.ini   friendly name = texture hash(es) (no pixels)
    default\              used by every era
    1970s\ 1980s\ 1990s\ 2000s\ 2010s\ 2020s\
    1980\                 optional: an exact season, used before 1980s\ when the look is Auto

A folder is searched in this order: <season>\, <decade>\, default\, then mods\textures\, then
the game's own texture.

What goes in an era folder (every file optional)
------------------------------------------------
era.ini      the room, the props, the camera stops and colour grades:

    [room]   scene = coach_desk | front_office
    [hide]   nodes = monitor_group1, phone_group1, ...     whole props (scene node names)
    [swap]   vcSwShader16 = vcHwShader27                    a material takes another's texture
    [stops]  play / schedule / standings / team / settings = a, b
             (camera times of the room's own camera curve; coach_desk: 1,3 monitor, 5,7 day
             planner, 9,11 notebook, 13,15 door, 17,19 file cabinets; front_office: 3,5
             computer, 7,9 contract; play = -1,0 walks out of the front office)
    [grade]  <texture name> = sat s, contrast c, bright b, mul r g b, add r g b, tint r g b amount
             a colour grade of the game's own pixels (no image file needed)

<name>.png   an image that replaces the texture <name> (names in office-textures.ini), any
             size (4x the original is a good target), the same shape as the original:
             square for most; the wall TV picture shows rows 5%-81% (about 4:3); the computer
             screen shows rows 0-80% (5:4). Tileable for carpet, wood and leather.

             Useful names:
               office_atlas      walls, books, door frame, day planner (one shared atlas)
               wood_panel        door, end table, floor lamp (and the 1970s desk top)
               carpet            both rooms
               desk_metal        the glass desk top
               couch_leather_1..4, file_cabinet, phone, computer_monitor (the bezel)
               computer_screen   the front_office computer
               wall_tv_picture   the front_office wall TV
               skin_team_logo    the coach_desk monitor wallpaper and the notebook sticker
               skin_wall_photos  the coach_desk wall photos and pennant (one 256x256 atlas)
               skin_office_photos the front_office diplomas and photos (one 256x256 atlas)

<HASH>.png   a raw texture hash also works (texture_dump = 1 in nfl2k5_game.ini writes every
             texture the game uses as <HASH>_<w>x<h>_fmt<NN>.png in the user folder's
             texture-dump\; paint over a copy and keep the first 8 characters).

Your own photos win: put a file with the same name in the era folder in place of the made one.

The made images in these folders (the 1980s terminal and room surfaces, the 1990s desktop and
TV, the 2010s wallpaper, the 2020s dashboard and TV) are drawn by tools\eras\make-office-art.py
from shapes and text only; run it again to restore them. 1980s\computer_monitor.png (the IBM
PC, the desk phone, the ashtray, the schedule poster and the pennant) is written by
tools\erasuild-office-ibmpc.py from the user's photo 1980seference\ibm_pc_5150.jpg and
made art.

Limits
------
Textures cannot change a prop's shape: the coach_desk monitor stays a flat panel and the phone
a cordless phone in every era (the 1970s hides both), except in the 1980s, whose room is the
reshaped copy coach_d80s (IBM PC 5150, keyboard, multi-line desk phone, ashtray, poster,
pennant; tools\erasuild-office-ibmpc.py). Shared textures change together: the
office atlas holds the walls and the books; the team logo is both the monitor wallpaper and the
notebook sticker.
