# Scorebug Studio

Open `index.html` in a browser. It works offline and installs nothing.

Choose a CBS-style, ESPN-style, FOX-style, NBC-style, Netflix-style, or custom
starting point. Change the colours, network label, font, placement, and size;
drag the preview to position it; then use **Download presentation.json**.

To begin with an existing package, use **Load an existing presentation.json**.
The editor keeps the full imported JSON in its own panel. Use **Download
imported JSON** for a lossless round trip, or edit that panel directly when a
package contains advanced layers, images, music, video, or animations that the
basic visual controls do not expose.

To install any JSON package into the game, double-click `Install Scorebug
JSON.cmd` in the project folder. It creates `mods\presentations\<package
name>\` itself, installs the selected file as `presentation.json`, and copies
nearby `images`, `music`, and `video` folders when present.

Create a folder under `mods\presentations\` with your package name and put the
downloaded file in that folder. Start the game, then choose it at **Coach Match
Up > Presentation**.

The editor exports the same package format that the current renderer reads. It
does not contain network logos, theme music, or video: creators can add their
own permitted files to the package later, while the exported scorebug works on
its own.
