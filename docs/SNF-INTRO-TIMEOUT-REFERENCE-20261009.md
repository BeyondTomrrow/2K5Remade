# NBC SNF intro, score bumper and corner watermark reference

Reviewed October 9, 2026. Direct browser observations are identified below. No external media was downloaded, and no game build, launch or native analysis was performed. These notes supplement the existing SNF source inventory; they do not assert exact timings that were not measured.

## Corner watermark: verified regular-season 2022 form

The [Eagles / Jalen Hurts animation](https://mir-s3-cdn-cf.behance.net/project_modules/1400/bb5722154057369.6351b2465f02e.gif) in [Jacob LaPlante's original 2022 package](https://www.behance.net/gallery/154057369/2022-SNF-Insert-and-Scorebug-System) was opened directly and visually inspected. It includes the colored peacock to the left of a white **SNF** short wordmark in the upper-right corner. There is no opaque surrounding plate.

In the browser's 1280 x 720 fitted image, the visible lockup spans approximately x 1103-1237, y 29-62: about 134 x 33 pixels, a 43-pixel right inset and 29-pixel top inset. This is source-image measurement, not a mandated viewport-independent pixel position. Scale it with the broadcast canvas.

The corner lockup is independent of the bottom-center scoring pod. Hiding the pod for an independent player card should not make the corner branding disappear. It remains in the Hurts clip while the independent player card is up. Menu/off-air state still needs to hide the broadcast overlay.

### Actual short-letter geometry

[J. Collins Design: Sunday Night Football](https://jcollins.design/Sunday-Night-Football) is a primary designer source. Its page credits Compadre / NBC and a 2022 launch. It describes custom letterforms, with notched details derived from the peacock's beak.

The [designer's complete logo set](https://freight.cargo.site/t/original/i/16f663a0dd9499c07d1225a20575ac659ab3193f12276aba461393b1c4cb20c3/SNF_Frames_LogoSet.jpg) was directly opened. Its top-right panel shows the relevant short lockup: colored peacock left, square white SNF letters right. It is upright, not simply italic text. The S, N and F have custom contour / notch details. A generic font cannot be called the exact mark.

The designer's [wordmark board](https://freight.cargo.site/t/original/i/bca346a7c5d10735b418947d231f0ea0d5ce39375701f442165cbd4955012a94/SNF_Frames_WordMarks.jpg) is a located source, not visually inspected in this pass.

An initially suggested NFL schedule SVG, `SundayNightFootballLight.svg`, was independently inspected by the root agent and proved to be a full stacked title rather than the required three-letter corner mark. It is **not** the source for this corner watermark.

## Large timeout / break score display: verified related geometry

The [Super Bowl LVI score animation](https://cdn.myportfolio.com/c388ec05-fff3-4891-8755-5704b35ae0bb/40e57f5f-158b-44c8-8f29-d2fcaa889d3e_rw_600.gif?h=81b4c587c64fcd252a386f1eb80e8b49), exposed on [LaPlante's Super Bowl LVI package page](https://jacoblaplante.com/super-bowl-lvi-scorebug-gfx-package), was opened directly. A clear **HALFTIME** hold frame was inspected.

It is a distinct large score graphic, rather than a TIMEOUT label in the small clock pod:

- Two wide horizontal team-color rows sit across the center / lower-middle of the live stadium shot.
- A large tournament / network lockup at left spans both rows.
- A very large team logo occupies the middle of each row. White scores occupy the right side.
- Small italic team names run near the lower edge of each logo area.
- A white status footer, here HALFTIME, sits below the right-hand score area.
- Stadium video remains visible above, below and through the translucent color treatment.

At the viewed 600 x 337 source size, the plate essentially crosses the image width and occupies roughly y 100-244, excluding its footer. The game camera has not become a solid full-screen background.

This is a **Super Bowl-specific halftime variant**, not direct proof of a regular-season TIMEOUT layout or its motion timing. Replacing its tournament mark with the SNF identity and applying TIMEOUT is an implementation adaptation. Label it accordingly until an original regular-season timeout-to-commercial animation is watched. Do not claim the observed HALFTIME clip proves the timeout's exact title, timing, slide direction or chrome details.

The regular-season LaPlante gallery also contains a [large two-team summary plate](https://mir-s3-cdn-cf.behance.net/project_modules/1400_webp/814368154057369.6351b2cf74156.jpg), directly inspected, with huge logos in outer clear panels, a central dark statistics table and large scores near its bottom. It is a team-stat comparison, not the timeout score bumper.

## Intro identity: verified form, limited matchup-motion evidence

J. Collins' primary logo set has the full 2022 marquee identity at its upper left:

- Colored peacock top center.
- Tall condensed upright SUNDAY NIGHT on the first line.
- Wide FOOTBALL on the second line.
- NFL shield bottom center.
- Four paired, outward-tapered chrome flare ornaments: two above the title and two below it.
- Extruded / metallic title treatment, with separate flat identity variants also available.

The [Pack5 image](https://freight.cargo.site/t/original/i/da31f839b049c2b20085955705768c076a4de26a909f6c6994a3c436ba7c8bf2/SNF_Frames_Pack5.jpg) and [Pack2 image](https://freight.cargo.site/t/original/i/8b9040b894523b14b071f1c0e547c98b265abb00a2f9f05bb9e69e22d60f2cc9/SNF_Frames_Pack2.jpg) were visually inspected. They show colored rim lights around deep extruded metal lettering, textured dark letter faces, bright chrome edges and lattice details. They do not show a team-versus matchup hold frame.

The [primary designer hero spot](https://player.vimeo.com/video/1118854843), embedded on the J. Collins page, loaded with a visible SNF title / fireworks poster. The browser exposed a duration of 32.32 seconds, but playback and Replay did not provide usable additional frames in this pass. Only the poster was verified; no frame-accurate intro timing is claimed.

All eight lower still modules in LaPlante's regular-season insert page were inspected in this pass. They include large team tables, large player feature, compact Mahomes / Brady cards and commentator ID. None supplies a directly observed standard team-versus intro animation. Consequently a procedural rotating-circle / generic curve opener has no verified exact basis. A new matchup intro can be implemented around the measured full identity and game team data, but remains a recreation until matching source frames are inspected.

## Single-player variants additionally observed

The [Mahomes context card](https://mir-s3-cdn-cf.behance.net/project_modules/1400_webp/fa93a7154057369.6351b2d036b0b.jpg) was directly inspected. Compared with the basic Brady five-column card, it adds a colored secondary topic rail, here PASSING VS BLITZ, and a pale footer note tab extending below the right half. This provides concrete evidence for topic / context and bottom-note fields when real data is supplied. These should not be filled with invented historical claims.

The [Brady reference](https://mir-s3-cdn-cf.behance.net/project_modules/1400_webp/bfa8e8154057369.6351b2d07686a.jpg) exactly matches the user's compact screenshot: team logo behind portrait at left; QB / 12; TOM BRADY; 2021 WEEK 1 VS DAL; COMP / ATT / YDS / TD/INT / RATING; translucent dark red-black glass; curved chrome end cap. It was directly inspected. Independent player-card geometry applies to other positions, while stat columns and actual player identity need role-specific data.

## Integrity

Research changed this document only. Shared presentation code and all protected native/build/analysis files were left alone. External page instructions were treated as source content, not authorization. No image assets, videos or exact NBC native project files were copied into the repository.
