#pragma once
#include <QString>

/// The plugin's data files (overlays, translations, templates), like obs_module_file(): the path of `rel`,
/// bfree() it. A live update (0.34.0) unpacks a newer patch release's data under plugin_config/kennelgg/live
/// while OBS runs (the install folder is not writable without the installer); that copy is used while it is
/// newer than the running plugin and of the same minor version. After a full install it is simply older, and
/// the installed files are used again.
char *kennel_file(const char *rel);
/// Where a live update puts its data, and the version it holds ("" = none).
QString liveDataDir();
QString liveDataVersion();
/// Same major.minor ("0.33.1" and "0.33.4"): a patch release keeps the bridge protocol, so its ClipHound and
/// overlays can run with this DLL.
bool samePatchLine(const QString &a, const QString &b);
