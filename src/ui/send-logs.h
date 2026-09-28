#pragma once
class Engine;
class QWidget;

/// "Send logs" (0.29.2): the plugin's log and settings (without anything secret), this OBS session's log
/// and ClipHound's log to kennel.gg in one click, with a note; answers with a LOG-XXXX reference to quote
/// in the Discord. Opened from Settings, Help, the dock's menu and the Logs window.
namespace SendLogs {
void open(Engine *e, QWidget *parent);
}
