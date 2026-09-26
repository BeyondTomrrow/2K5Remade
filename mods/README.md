# Mods

Everything here is read by the game at startup.

## teams/<ABBR>/

One folder per team, named by the game's team code (SF, DAL, KC, ...).

- `team.json` - city, name, colours (`primary`, `secondary`, `text` as `#RRGGBB`),
  and `aliases` (other codes that mean this team).
- `logos/scorebug.png` - the logo broadcast scorebugs show (transparent PNG,
  roughly square). Missing logos are simply left out.
- `uniforms/`, `menu/` - reserved for uniform and menu art (not read yet).

## presentations/<Name>/

A broadcast package: `presentation.json` plus its `images/` and `music/`.
Pick it in game: Quick Game > Coach Match Up > Presentation (under VIP).

`presentation.json`:

- `scorebug.canvas` - design units (`width` x `height`), `overflow_top` room above
  the bar for timeouts and banners.
- `scorebug.placement` - centre (`center_x`, `center_y`) and `width`, as fractions
  of the game picture.
- `scorebug.elements` - drawn in order. Types: `box` (fill or `gradient`
  top/bottom, `radius`, `stroke`), `text` (`text` template, `size`, `weight`,
  `stretch`, `color`, `align`, `italic`, `shadow`), `image` (`src`),
  `timeouts` (`team`, `count`, `bar_w`, `bar_h`, `gap`, `on`, `off`).
  Optional `show`: `possession:away`, `possession:home`, `scrimmage`, `final`,
  `not_final`.
- `scorebug.team_slots` - per side `block`, `score_box`, `timeouts` rectangles;
  animation layers can use `"at": "team.score_box"` etc.
- `animations.<event>` - `duration` and `layers` (elements with `enter` / `exit`
  `{type: fade|slide_up|slide_down|wipe_x, time}`, `blink {period, until}`,
  `start`, `end`). Events: `touchdown`, `field_goal`, `extra_point`,
  `two_point`, `safety`, `timeout`, `first_down`, `two_minute_warning`,
  `end_of_quarter`, `halftime`.
- `music` - `intro` and `outro` lists of `{title, file}`, `volume`, `fade_out`
  (seconds), `duck_game_audio` (game volume while a theme plays).

Templates: `{down_distance} {quarter} {clock} {away.abbr} {away.score}
{away.name} {away.city} {away.logo} {home.*} {team.*}` (`team` = the team an
animation is about), `{ended_quarter}`, `{network}`.
Colours: `#RRGGBB[AA]`, `{away.primary}`, `{home.secondary}`, `{team.text}`,
with modifiers `|darken:0.3`, `|lighten:0.1`, `|alpha:0.5`.

The intro theme starts with the pregame show and fades out when the game
starts; the outro plays at the final whistle.
