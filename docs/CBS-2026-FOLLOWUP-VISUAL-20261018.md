# CBS follow-up visual verification — October 18, 2026

The full team-table opener and receiver entrance are now implemented in editable HTML/CSS/JS, not a prerecorded broadcast overlay.

- Team opener: old Eyebar logo turn0–130ms, plate contraction70–200ms, cropped real matchup images270–367ms, image-derived contours367–630ms, then table reveal670–1230ms. The table's source exit is a cut. `cbs-team-opening` owns only the entrance and is removed on completion/cancel; reduced motion skips it.
- Receiver table: measured200ms opacity entrance, independent full name/jersey/position, available receiving statistics, and a cut exit. Unknown TARGETS remains an em dash. The game's `Thrown To` counter was found to exclude intercepted passes, so it was not mislabelled as inclusive NFL targets.
- Native final detection now uses actual game-over flow0x0A, including ties/OT, rather than score inequality and a zero clock. The observed ordinary END1stQTR label uses `context.periodEnding` instead of waiting for the next period's event. Source and timing: `CBS-REGULAR-PERIOD-REFERENCES-20261018.md`.

## Rendered evidence

`outputs/cbs-2026/followup-visual-20261018/CBS-2026-stats-extended-followup-20261018.mp4` is76.6seconds, H.2641280×72030fps, silent, labelled sample data. All frames decoded successfully. `followup-contact.jpg` shows sampled receiver entrance and team-opener phases from the actual WebView2 batch render. The source's full3D logo meshes/materials are unavailable; the editable image planes/contours are an approximation, not a pixel-identity claim.

The first native follow-up run used the rebuilt executable and the shared CBS package: actual SEA/DAL team yardage/defense/TOP and player/QB events continued. Composed frame535 visibly shows the CBS defensive table with actual Seattle0sacks/4tackles/0INT versus Dallas0/4/0. No test-event or synthetic-stat injection was enabled. No `[CRASH]`, `handler error` or `script error` lines were found in that run. Half-size captures establish functionality, not typography identity.

The second isolated native run visibly verified the new period label, continued into OT and emitted one genuine GAME_ENDED at717300.897. Native camera eligibility hid the Eyebar during the subsequent original player interview, so that is not labelled a visible CBS FINAL capture. See the period reference document for the exact test limits. Those review games were stopped by their recorded PID.

Verification: Release builds passed, 403 HTML lifecycle assertions / 28 cases passed, and 34 assertions compiled against the actual native helper implementations passed. JavaScript syntax checks passed. Existing APU, generated code, selected NBC package, user saves and original game data were preserved. No game build was run concurrently with another build.

FOX research is separately preserved in `FOX-REFERENCE-AUDIT-20261018.md`: official source video, primary creator demonstration, bounded measured motion and explicit coverage gaps. No FOX production files were modified in this CBS follow-up.

## 1080p refinement

Obtained bounded 1080p source excerpts rather than enlarging 360p analysis images. Their full-frame Eyebar bounds are approximately x552–1368/y911–1029, so corrected the outer anchor/scale and applied the same mapping to the attached compact player extensions. Independent team/receiver/comparison tables retain their own measured positions. `native-review-hd01` ran the corrected anchor in D3D11 through Q4 without page/crash errors; it was then stopped by its owned PID.

Rendered `font-specimen.html`/PNG against the HD source using the actual available local faces. The supplied All-Pro Heavy gives a closer squared 0 counter/straight 7 than Arial Bold; use it for score numerals and the END label. This is an evidence-based fitted substitute, not a newly proven identity of CBS's proprietary font. Clock/down/stat label faces remain independently scoped. Narrowed the center brand to the measured visible width and applied the observed gray/silver gradient to a copy of the official SVG; all path geometry is unchanged, and the corner mark remains white.

Refined renders and the latest review export are under `outputs/cbs-2026/hd-followup-20261018/`. The earlier 76.6-second follow-up still exists as a prior visual checkpoint. The renderer's standalone transparent-background capture clears to white; native composed captures are the authority for actual over-game transparency.

The exact final executable/package was checked again in `native-review-clean01`. Composed frame165 visibly shows the refined END1STQTR Eyebar without a lingering player strip. The run was stopped by its recorded PID; no crash/page errors were observed. The original large ESPN end-period board can still appear (frame172) and remains an explicit replacement/ownership gap.

Final combined MP4: `outputs/cbs-2026/CBS-2026-package-followup-20261018.mp4`, 187.3 seconds, H.264 1280×720 at30fps, silent, six embedded chapters. Complete decoding passed. Byte-range playback returned HTTP206; the new chaptered player is `http://127.0.0.1:8770/followup-watch.html`. Original delivered MP4s are unchanged. The last six seconds are current native still evidence; the earlier native section is explicitly labelled as an earlier revision.
