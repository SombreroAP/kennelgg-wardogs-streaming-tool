#pragma once
#include <QString>
class Engine;
class QWidget;

/// "Send logs" (0.29.2): the plugin's log and settings (without anything secret), this OBS session's log
/// and ClipHound's log to kennel.gg in one click, with a note; answers with a LOG-XXXX reference to quote
/// in the Discord. Opened from Settings, Help, the dock's menu and the Logs window.
namespace SendLogs {
void open(Engine *e, QWidget *parent);
/// Without a window (0.32.0, "help build the plugin"): the same logs with `note` saying why, sent by the plugin
/// itself after a stream that had problems, or at start after OBS crashed (previousObsLog: the crashed
/// session's OBS log instead of this one's). The reference goes to the plugin log.
void sendAuto(Engine *e, const QString &note, bool previousObsLog);
} // namespace SendLogs
