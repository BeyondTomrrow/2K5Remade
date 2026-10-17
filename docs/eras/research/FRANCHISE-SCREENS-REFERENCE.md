# Retail Franchise screens: a visual reference for the My NFL Eras copies

2026-10-16. These are screenshots of retail Franchise mode, taken as side-by-side references for
the Eras screens. Contact sheet: `logs/franchise-ref/franchise-screens-sheet.png`. The single
frames are `logs/franchise-ref/NN-*.png` (720x480 renderer frames).

**How they were taken.** A copy of the exe (`NFL2K5_ref.exe`, since deleted) ran with `--retail`
in background mode: muted, `NFL2K5_NO_HOST_PAD=always`, its own press file, and
`NFL2K5_PRIVATE_PACKS=1`. LOCALAPPDATA pointed at a sandbox, `logs/franchise-ref/home`.

The run started a new franchise as the Cowboys, using the default Franchise Options. The preseason
was simulated, then the 2004 season, then the playoffs. In the offseason, the run went through the
retirement, re-sign, free agency and combine periods to round 1 of the Draft. Nothing was saved.
No trade was proposed, and no contract was offered or signed. Descriptor addresses come from
`FRANCHISE-OFFICE-MENUS.md`.

Controls used on these screens: LT/RT (the `L`/`R` arrows in a header) page the filter (position,
conference or week). White/Black (`LB`/`RB`, the round icons) page the team, or switch between the
two schedule pages. RS opens a player card or SportsCenter. A opens a row popup.

## Setup and hub

| File | Screen | Menu path | What it shows |
|---|---|---|---|
| 00-franchise-options | Franchise Options `0x500DC8` | Main Menu > Game Modes > Franchise | Settings list of Off/On toggles (Weekly Preparation, FPF, Fantasy Draft Roster, Customize League, Preseason Games, Trade Deadline, Owner Firing, Edit Rookies) and Player/CPU pills (Draft Players, Sign Draft Picks, Re-sign Players, ...). "Press START to Advance". START opens a "lose unsaved data" Yes/No |
| 00-team-select | Team Select `0x589D80` | after START | Checkbox list of 32 nicknames, with the VIP name beside the checked team. Right side: helmet card and Offense/Defense/Overall bars. "Press START to Advance" |
| 01 / 01b / 01c | Coach's Desk `0x522190` | after Team Select | 3D office. The left panel holds 8 rows: Schedule (phase name: Schedule / Playoff Schedule / Off-Season Schedule), Front Office, Gameplan, ESPN.com, The Crib, Features, Options, Quit. The top-right info panel is in two styles. In season it shows "2004 Preseason Week 1 of 5", Record, Power Ranking, Last Game and Next Opponent. In the offseason it shows "2005 Retirement Period Week 1 of 1", Record, Total Salary, Cap Room and Players |
| 60-desk-options | Options `0x503458` | Desk > Options | Navigation list: Franchise Options, Game Options, Load/Save, Difficulty, Presentation, Penalties, Controller Setup |

## 1. Schedule (both pages)

| File | Screen | Menu path | What it shows |
|---|---|---|---|
| 02 / 02b / 05 | Team Schedule `0x522828` | Desk > Schedule | Black tab with the phase (PRE-SEASON / SEASON / WILD CARD ROUND / PRO BOWL) and "DAL Record: (w-l-t)". A blue bar with L/R arrows, the team name and the date. Columns: checkbox (checked = play, unchecked = sim), helmet, OPPONENT ("At ..." for road games) with the opponent's record, SCORE (DAL: n / OPP: n). After a game the checkbox becomes a WON / LOST / DONE stamp. Bye Week rows. Footer: "Press LB RB for Weekly Schedule" or "Click RS to watch this week's SportsCenter" |
| 03 / 03b / 06 | Weekly Schedule | Schedule > LB/RB | Same frame, with "WEEK n" in the bar (LT/RT page the weeks once the season starts; in the preseason it stayed on week 1). Columns: AWAY team and record, HOME team and record. Played games show the score in yellow over each helmet. The user's game is checked |
| 04 | Week prompt | Schedule > START on the current week | Modal "Week 1 - What do you want to do?": Simulate Week / Simulate Preseason (or Season or Playoffs) / Cancel. Then "Simulate ALL ... games?" No/Yes. After the preseason: "see SportsCenter before starting the season?" Start Season / SportsCenter |
| 07 / 07b | Off-Season Schedule | Desk > Off-Season Schedule | "OFF-SEASON", "2005 RESIGNING PERIOD" and similar. Columns: checkbox, sketch icon, TASK, STATUS. Rows: Retirement Period, Re-Signing Period, Free Agency, NFL Combine, NFL Draft, Rookie Signing Period, each Pending / In progress / COMPLETED. "Pull RT for Franchise Options". Unchecking a task and pressing START gives "skip ... have the Front Office do the dirty work?" No/Yes, or a period-specific choice (retirements, combine) |

## 2. Standings

| File | Screen | Menu path | What it shows |
|---|---|---|---|
| 43 / 44 | Team Standings `0x54CEA0` | Desk > ESPN.com > Team Standings | ESPN.com header with the team logo tile and "Team Standings", and an L/R conference toggle (NFC / AFC; only these two views). A 2x2 grid of divisions (NORTH / SOUTH / EAST / WEST) with W L T columns. The user's team is in yellow. Clinch prefixes x / y / z / *, with a legend footer |
| 53 | Playoff Picture `0x560DC0` | ESPN.com > Playoff Picture (in season) | L/R AFC/NFC. "If the playoffs started today:" Columns: seed, helmet, Team, W, L, T, Clinched (Home Field Adv. / First-Round Bye / Division Title / Playoff Berth). Row 7 is shown as "--" |
| 51 | Power Rankings `0x561448` | ESPN.com > Power Rankings | Columns: rank, logo, TEAM (with clinch prefix), W-L-T, PREV, LAST (for example "@CHI W12-7") |

## 3. League Leaders and stats

| File | Screen | Menu path | What it shows |
|---|---|---|---|
| 46 | Statistics `0x561910` | ESPN.com > Statistics | Team Stats, Player Stats, Season Stats, League Leaders, Rookie Watch. Before any game only the first three rows show, and Season Stats is missing |
| 47 | Team Stats | Statistics > Team Stats | Spreadsheet of all teams. The header tab shows the sort ("TOTAL YARDS OFFENSIVE"). Columns: Team, TYO, PASS, AVG, RUSH, AVG, TYD |
| 48 | Player Season Stats | Statistics > Season Stats | Player card strip (photo, team/number/position badge, HT/WT/YRS PRO/COLLEGE, health box). L/R category (PASSING), White/Black team. Columns: #, NAME, RAT, COM, ATT, PCT ... |
| 48b | Player Stats | Statistics > Player Stats | Same card layout with a wider column set (# NAME RAT COM ATT PCT YDS YPA TD INT LNG SK 2PT). Empty after simulated games: it appears to count only games that were played |
| 49 / 49b | League Leaders | Statistics > League Leaders | Card of the leader. L/R category (PASSING, RUSHING, ...). The column tab names the sort stat (QB RATING, RUSHING ATTEMPTS). Columns: #, NAME, TEAM, then category stats |
| 50 / 50b | Awards `0x561750` | ESPN.com > Player Awards | Primetime Players: 4 tiles (team-coloured name bar, photo, team/position/number badges, stat lines), with the week on White/Black. Season Awards (L/R): a wood plaque with a gold trophy and a stat box ("Most Valuable Player") |
| 45 | Transactions `0x5623C0` | ESPN.com > Transactions | Re-themed in the selected team's colours and logo. L/R team, White/Black filter (ALL TRANSACTIONS). Player card, then a list of rows with the team logo and text ("Placed ... on Injured Reserve", "Waived ...") |

## 4. Roster

Retail Franchise has **no dedicated roster screen**: there is none in the desk, Front Office or
ESPN.com trees. Two screens act as the roster:

| File | Screen | Menu path | What it shows |
|---|---|---|---|
| 12 | Player Contracts `0x540650` | Front Office > Contracts | The full team list. L/R position filter (ALL POSITIONS), White/Black team. Player card on top: photo, team logo, number, position, name, HT/WT/YRS PRO/COLLEGE, health status and a body icon. Columns: #, NAME, POS, Status (years left), Salary, Contract, Penalty. Footer: SALARY, CAP ROOM, PLAYERS 53 |
| 19 | Team Needs `0x582EA0` | Front Office > Team Needs | L/R position. A "ROSTER BREAKDOWN" box with team salary, players (42-54) and a have/need grid per position (shortfalls in yellow). "RS VIEW PLAYER CARD". "Currently Signed" table: #, NAME, RTG, Salary, Status, then 3 position ratings (QB: PST, PAC, PRC) |
| 20 | Player Card | Team Needs > RS | White card: 3D player model, name/number/position, HT/WT/DOB/COLLEGE/YRS PRO, year-by-year stats table (Yr, Games, COM, ATT, YDS, PCT, YPA, TD, INT, RAT), rating list, OVERALL box |

## 5. Depth Chart

| File | Screen | Menu path | What it shows |
|---|---|---|---|
| 30 | Gameplan `0x503D6C` | Desk > Gameplan | Depth Chart, Playbook Manager, Coach's Card, Coach Gameplan |
| 31 | Depth Chart `0x532D14` | Gameplan > Depth Chart | Offense, Defense, 3-4 Defense, Special Teams |
| 32 / 34 | Depth chart table `0x533088` | Depth Chart > Offense / Defense | Subtitle (OFFENSE / 4-3 DEFENSE). Header bar: 1ST TEAM, 2ND TEAM, 3RD TEAM. One row per slot (QB HB FB LWR RWR TE LT LG C RG RT; LDE LDT RDT RDE ROLB ILB LOLB RCB LCB SS FS), each holding "number initial. name" ×3 |
| 33 | Position depth | table > A on a slot | Three portrait cards (name bar, OVR box, photo or NO PHOTO, team/number/position badges, HEIGHT, WEIGHT, three key rating bars). The selected card has a yellow frame. "Press A to select player" |
| 35 | Coach's Card `0x52D9F8` | Gameplan > Coach's Card | Portrait box, CURRENT SEASON row (W L T PCT PF PA HM RD STK PWRRNK), Coaching Bio, Coaching History (years with team, streaks, playoffs, Super Bowl, winning seasons, career) |
| 36 | Coaching Gameplan `0x52BAE0` | Gameplan > Coach Gameplan | 7 slider rows: Offense Run/Pass, Pass Short/Deep, Run Outside/Inside, Defense Run/Pass, Defense Man/Zone, Blitz, Cover Short/Deep |

## 6. Trades

| File | Screen | Menu path | What it shows |
|---|---|---|---|
| 21 / 22 | Player Trade `0x55A0F0` | Front Office > Trades | L/R position, White/Black team. Two offer rows. Each has a text box ("DAL Offer (Cap: 9.63m)" and the player names) and three portrait slots (a "?" placeholder until filled). An "Interest" meter (red segments). Roster table: #, NAME, RTG, Salary, Contract, Yrs, EXP, PST. 22 shows Testaverde (DAL) for J. Sorgi (IND), not proposed |
| 23 | Trading Block `0x55A544` | Front Office > Trading Block | White/Black team. "DAL Offer" box with 3 slots, a "Wants" box and a "Team Offering Offer" box |

## 7. Free Agents

| File | Screen | Menu path | What it shows |
|---|---|---|---|
| 16 | Free Agents | Contracts > White/Black to FREE AGENTS (before the 49ers) | The Player Contracts frame with team = FREE AGENTS. The team badge is black. Status reads "not signed", and Salary/Contract are "-" |
| 17 | Sign Contract (offer) | Free Agent > A > Negotiate Contract | The same screen as re-sign (below), for a free agent (not offered) |

## 8. Contracts and Re-sign

| File | Screen | Menu path | What it shows |
|---|---|---|---|
| 13 | Player popup | Contracts > A | Modal: Cancel, View Contract, Re-negotiate Contract, Release To Free Agency, Place on Trading Block, Begin Trade, Assign Jersey Number. Free agents get only Cancel / Negotiate Contract |
| 14 | Player Contract | popup > View Contract | Card plus a per-year table: Year, Salary, Bonus, Team, +/- Cap |
| 15 / 15b | Sign Contract `0x557B2C` | popup > Re-negotiate (offseason re-sign period / in season) | Card with OVERALL, YEARS PRO, CONTRACT type, SALARY, YEARS LEFT, HEIGHT/WEIGHT. PLAYER WANTS box (Salary, Contract, Bonus, Length) and an Interest Meter. Year table (Salary, Bonus, Team Salary, Cap Room). OFFER box (Salary, Contract, Bonus, Length; the selected row is red). "Press A to Decrease". Backed out with B, nothing offered |
| 25 / 26 | Retirement and Re-Signing periods | Off-Season Schedule > START (retirement) or Front Office > Contracts | The Player Contracts list with Status "retiring" / "expiring" |
| 18 | Cap Manager `0x584BA8` | Front Office > Cap Management | Bar chart "100% CAP" for 04..09 over the team logo. Table: Year, Cap, Team, +/- Cap. HIGHEST PAID photo, plus SECOND and THIRD HIGHEST (name, salary, "Yr 2 of 5") |
| 24 | Pro Bowl Votes `0x54AC78` | Front Office > Pro Bowl Votes (postseason row) | L/R position, White/Black conference. Card of the leader. NAME, TEAM, VOTES |

## 9. Front Office and Franchise Status

| File | Screen | Menu path | What it shows |
|---|---|---|---|
| 10 / 10b | The Front Office `0x52533C` | Desk > Front Office | Second 3D office (door, photos, plant, monitor with ESPN NFL Football). Rows in the preseason: Franchise Status, Contracts, Cap Management, Team Needs, Trades, Trading Block. In the postseason, Trades and Trading Block are hidden and Pro Bowl Votes is shown. Popup on first entry: "Hey VIP1 (2), you've got a phone call. Head to your Crib..." |
| 11 | Franchise Status `0x585548` | Front Office > Franchise Status | CURRENT SEASON (W L T PCT PF PA HM RD STK PWRRNK). COACHING GOALS: one PRIMARY goal and two SECONDARY goals, each with STATUS (Not Yet). CONTRACT DETAILS (years remaining, owner mood line, Last Game) |

## 10. Other notable screens

| File | Screen | Menu path | What it shows |
|---|---|---|---|
| 40 | ESPN.com `0x525664` | Desk > ESPN.com | Logo tile and "ESPN.com" banner. "NFL:" rows E-mail Inbox, Team Standings, Transactions, Statistics, Player Awards, Injury Report. "FEATURES:" rows Power Rankings, Playoff Picture, Rookie Report, Mel's Mock Draft. Rows are phase-gated: Injury Report is greyed with no injuries, and the last three appear only in season |
| 41 / 42 | E-mail Inbox `0x522A68` | ESPN.com > E-mail Inbox | SENDER / TOPIC list. The opened mail has body text with two links (Franchise Options, A; Company Handbook, Y) |
| 52 | Injuries `0x52D478` | ESPN.com > Injury Report | L/R team. Player card with the status (Out). Columns: #, NAME, POS, Injury, Duration. "Press A to place on Injured Reserve" |
| 54 | Rookie Report `0x53C7F4` | ESPN.com > Rookie Report | L/R position. Prospect card (COLLEGE, college stats, VITALS height/weight). Columns: NAME, POS, 40YD, SCHOOL |
| 55 | Mel's Mock Draft `0x53CC1C` | ESPN.com > Mel's Mock Draft | Prospect card plus the drafting team. Numbered rows with team logo, "name, pos, school" and "MEL'S TAKE: ..." |
| 56 | NFL Combine | Off-Season Schedule > START at the combine > Take me to the Combine | "DAY 1 OF 4", HOURS LEFT. Columns: NAME, HRS, POS, STOCK (arrows), 40YD, BNCH, JMP, SHUT. Key Attributes and Coach's Notes panels. "Press A to Scout". The menu above it is The Combine: NFL Combine, The Big Board |
| 57 | The Big Board | The Combine > The Big Board | Prospect card. Ranked list: NAME, POS, STOCK, SCHOOL |
| 58 | The Draft `0x537E38` | Off-Season Schedule > START at the NFL Draft | 3D draft room: a "NFL ENTRY DRAFT - DRAFT ORDER SUMMARY" board (RND, PICK, NAME, TEAM), team helmets on the shelves, a curtain stage, an announcer, and a "3:30 Round 1" clock bar |
| 59 | The Draft: candidates | the user's pick in the draft room | "NFL DRAFT" panel over the room. L/R position, White/Black DRAFT CANDIDATES. Prospect card. Columns: NAME, POS, SCOUTED, 40YARD, BENCH, JUMP. The clock and an RS icon in the bar |

## Not captured

* **Player Progress** (`0x551800`): its Front Office row was not shown in the preseason or postseason
  phases reached.
* **Playbook Manager, Features (VIP, Celebrations, Stadium Music), The Crib, Weekly Preparation,
  Rookie Signing, Draft Recap:** outside the requested list, or past the point where the run stopped
  (round 1 of the Draft, before the user's pick).
* **Standings:** has only the NFC and AFC views. White/Black does nothing there.
