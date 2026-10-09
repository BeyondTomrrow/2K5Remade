# NFL 2K5 Team Creator

Makes a completely new team for NFL2K5-PC: its own name, code, uniforms,
crowd and roster. Nothing of another team changes.

    Team Creator.cmd            (python tools\team-creator\creator.py)

- **Team**: city or school, nickname, abbreviation, colours, and the NFL team
  whose uniform cut, helmet and crowd it starts from.
- **Uniforms**: previews the home and away textures in your colours.
- **Roster**: generate one (team strength), or import a team from a Madden or
  NCAA Football roster file (the EA "DB" format: Madden 21 PC/console rosters,
  NCAA Football 14 PS3 USR-DATA; other years of the same format should read,
  untested). The best 53 by position become the roster, in depth order.
- **Build**: writes `mods\teams\<Team>\` -- `team.json`, `team.rost` (a roster
  block from your own disc), `roster.csv`, and `files\*.iff` (every uniform,
  helmet and crowd file of the base team, recoloured, under the new team's code).

Restart the game: the team is in Quick Game Team Select after the historic teams.

Needs Python 3 with Tkinter (and Pillow for previews), and the extracted game in
`original\disc`. Texture and archive codecs: `vendor\` from 2K Football Mod
Tools / 2K5 Mod Studio (MIT, see `vendor\LICENSE-2k-football-mod-tools.txt`).
