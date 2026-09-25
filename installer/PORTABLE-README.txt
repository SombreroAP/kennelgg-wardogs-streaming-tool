Kennel.gg Wardogs Streaming Tool - for portable OBS

Portable OBS only loads plugins from inside its own folder, so the normal installer cannot reach it.
This download is laid out like that folder.

INSTALL
1. Close OBS.
2. Open this zip and drag everything in it (obs-plugins, data, ClipHound) into your portable OBS
   folder - the one with bin, config and obs-plugins in it. Say yes when Windows asks to merge the
   folders.
3. Start OBS. The Kennel.gg Wardogs dock opens its setup, and ClipHound (in the ClipHound folder
   you just copied) is found and started with OBS by itself.

UPDATE
The dock says when a new version is out. Close OBS and drag the new zip's contents in again,
replacing the files. Your settings stay: portable OBS keeps them in its own config folder.

UNINSTALL
Close OBS and delete:
  obs-plugins\64bit\kennelgg.dll
  data\obs-plugins\kennelgg
  ClipHound
For a fresh start also delete config\obs-studio\plugin_config\kennelgg. Your recorded clips are
never inside any of these.

Needs Windows 10 or 11 (64-bit) and OBS 30 or newer. https://kennel.gg/obs
