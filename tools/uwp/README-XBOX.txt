NFL2K5 on Xbox (Dev Mode) - install with the game inside the app
=================================================================

Why: Dev Mode limits an app's own storage (LocalState) to a few GB. The game
files are about 6 GB, so uploading them there stops with "out of storage".
Installed this way the game lives in the app's install folder instead, which
has no such limit. Saves and settings still go to the app's storage (small).

You need: the Xbox in Dev Mode with Dev Home open, a Windows PC on the same
network, and your own extracted copy of the game (a folder holding
original\default.xbe and original\vc_53450030\). No USB drive needed.

1. Remove the old NFL2K5 from the Xbox (if you installed the .msix before)
   Dev Home > select NFL2K5 > Uninstall. This also deletes the partly
   uploaded game files and any saves made by that install.
   (Step 3 can do this for you: it asks first.)

2. On the PC: build the Xbox folder
   Double-click tools\uwp\make-xbox-loose.cmd
   (or drag your game folder - the one holding "original" - onto it).
   It makes dist\xbox-loose\NFL2K5 (about 6.2 GB). Your game files are not
   changed: on the same drive they are linked, not copied.

3. On the PC: send it to the Xbox
   Double-click tools\uwp\deploy-xbox-loose.cmd
   - Xbox IP address: shown in Dev Home (Remote Access), e.g. 192.168.1.50
   - Device Portal username / password: the ones you set in Dev Home >
     Remote Access settings (blank if you set none). They are not saved.
   It copies the folder (it can take a while over Wi-Fi), then installs it.
   If it stops, run it again: files already on the Xbox are skipped.

4. On the Xbox: Dev Home > select NFL2K5 > View details > App type: Game
   (Without this the Xbox gives it only 1 GB of memory and it crashes.)

5. Start NFL2K5.

If something goes wrong
-----------------------
- Log file: Device Portal (https://<xbox-ip>:11443) > File explorer >
  LocalAppData > NFL2K5.Recompiled_... > LocalState > nfl2k5.log.
  Its line "[UWP] game folder ..." says where the game was found.
- "Could not reach Device Portal": the Xbox must be in Dev Mode with Dev Home
  open and Remote Access / Device Portal turned on; PC and Xbox on the same
  network. The address is https://<xbox-ip>:11443.
- Manual way (no script):
    a. In a browser signed in to Device Portal, open
       https://<xbox-ip>:11443/ext/smb/developerfolder - it shows a share path,
       username and password.
    b. In File Explorer open \\<xbox-ip>\DevelopmentFiles (use that username
       and password), go into LooseApps and copy the whole NFL2K5 folder from
       dist\xbox-loose into it.
    c. Device Portal > Apps > Add: register the loose folder LooseApps\NFL2K5.
- If the Xbox refuses the install because it is too big: run the app from
  the PC instead (nothing large is stored on the Xbox; the PC must be on and
  on the same network while you play):
    a. Share the folder dist\xbox-loose\NFL2K5 on the PC (right-click >
       Properties > Sharing > Share, read access for your account).
    b. Device Portal > Apps manager > "Register from Network Share", enter
       \\<pc-name>\NFL2K5 and your PC username/password when asked.
    c. Set App type: Game (step 4) and start it.

Nothing here changes any Xbox or PC setting besides installing the app.
