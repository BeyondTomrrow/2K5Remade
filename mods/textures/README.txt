NFL 2K5 texture packs
=====================

Put replacement images here (any sub-folders). Each file name starts with the
8-hex-digit hash of the game texture it replaces, for example

    mods\textures\MyUniforms\ATL\4D7B163D.png
    mods\textures\HD Fields\1625D39D-grass-4x.png

PNG, JPG, BMP and DDS work, at any size up to 16384 x 16384: paint at 4x or 8x
the original for a sharper game. Larger images get a mip chain automatically.

To find a texture's hash, turn on dumping: add the line

    texture_dump = 1

to nfl2k5_game.ini (in %LOCALAPPDATA%\NFL2K5-PC, or the game folder for a
portable install) and play. Every texture the game draws is saved once to the
texture-dump folder beside that ini, named <HASH>_<width>x<height>_fmt<NN>.png.
Copy one here, edit it, keep the hash at the start of the name, restart.

Folders are read in name order; if two files have the same hash, the first wins.
