# 2K5 Remade: Proton setup

Set up the [2K5 Remade Windows repack](https://github.com/BeyondTomrrow/2K5Remade)
on Steam Deck or Linux using Proton.

Tested on Steam Deck with the stock ESPN scorebug and 2× render scale.
See [test results and remaining limits](TESTING.md).

## Setup on Steam Deck

1. Switch to Desktop Mode and install **Proton Experimental** from Steam → Library → Tools.
2. Download and extract this repo.
3. Have your own extracted Xbox disc ready. Its folder must contain `default.xbe`
   and `vc_53450030`.
4. Open Konsole in this repo and run:

```sh
bash setup.sh
```

The script finds Steam and Proton Experimental, asks for your disc and installation
folders, downloads and verifies the original repack installer and Microsoft shader
compiler, installs into a fresh Proton prefix, and adds a Steam shortcut. Python 3 is required and is
included with SteamOS. It does not need sudo or a separate PC.

5. Find **2K5 Remade (Repack)** in Steam. Leave **Force compatibility tool**
   unchecked—the script already starts Proton. Select the **Gamepad** controller
   template and launch from Gaming Mode.

If it doesn't appear in Steam, add `play.sh` from the output folder as a non-Steam
game. On other Linux PCs, adjust the Steam and Proton paths and add it manually.

## Troubleshooting

Check `setup-proton.log` and `installer.log` in the output folder. Keep your disc
folder in place. Retry failed installations into a new output folder.

This helper uses a separate Proton prefix and leaves existing installations alone.
It includes no game files and does not modify the repack executable.
It selects the original ESPN presentation because HTML scorebugs require WebView2.
The tested settings use Wine’s OpenGL renderer; DXVK is not the verified default.

## Credits and license

Repack: [BeyondTomrrow/2K5Remade](https://github.com/BeyondTomrrow/2K5Remade).
Setup helper: [Cosmo Kay](https://github.com/cosmokay1), [MIT License](LICENSE).
The repack, game assets and Microsoft runtime retain their own terms and credits.
