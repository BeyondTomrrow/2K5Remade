# Sandbox menu integration

The optional Sandbox entry clones the existing Situation Mode settings. It
does not replace or edit the original Situation descriptor, value rows, or
events. Native Situation Mode still owns initial scores, clock, possession,
timeouts, quarter, down, line of scrimmage, yards to go and team selection.

Enable by installing `mods/sandbox/pack.json`, or by setting `sandbox=1` in
`mods/features.ini`. An explicit `sandbox=0` disables it. The diagnostic
environment variable `NFL2K5_SANDBOX=0/1` overrides discovery.

Host integration:

- Call `nfl2k5_sandbox_menu_install` after the native menus exist and after any
  other features have inserted Game Modes rows.
- Route `nfl2k5_sandbox_menu_lookup` through the manual VA lookup. Its only
  served addresses are `FEF00000`, `FEF00004`, `FEF00008`.
- Forward native FSM-pop events to `nfl2k5_sandbox_menu_fsm_pop`.
- Forward native match-exit events to `nfl2k5_sandbox_menu_match_exit`.
- `nfl2k5_sandbox_menu_open` includes setup/team selection, before the game
  core is active; use it to scope any Situation team-list extension.
- `nfl2k5_sandbox_menu_validate` checks the actual cloned graph: independent
  rows/events, exact preserved native rows/actions, and Start callback order.
- Provide `nfl2k5_sandbox_begin/end`. Begin runs immediately before the cloned
  Situation menu's actual Start event, not when someone browses Game Modes.

Settings and team-selection words are snapshotted on entering the private
setup. Returning to Game Modes/Main Menu restores them. The existing native
match teardown runs before any settings restoration. The clone validates the
generic settings schema and expected Situation Start action before installing;
a package that rewrites those layouts is rejected with a diagnostic.

`nfl2k5_sandbox_menu_request_reselect` queues a return to the private native
setup after a real native match exit. It does not swap loaded team/stadium
resources in the middle of a match, invoke teardown unsafely, or claim those
resources can change without reloading. `nfl2k5_sandbox_menu_quit_for_reselect`
invokes the actual Pause -> Quit action (`sub_0006EBE0`) on the guest thread,
only in an armed mode-8 match whose gameplay descriptor is on the native FSM
stack. The host must resume simulation before invoking it. Native dialogs
and cancellation still apply. A request alone does not exit the running game.

Known limitations at implementation time: setup still follows Situation's
settings-first/team-selection flow. A stadium-first chooser and an in-world
team browser require additional host UI and a verified native quit/reload
path. SOFTDRINK Situation Mode's previously reported team-resolution issue
is not repaired merely by copying its menu. Build success is not evidence of
successful gameplay, lifecycle restoration, or SOFTDRINK compatibility.
