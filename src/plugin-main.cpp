/*
Kennel.gg Wardogs Streaming Tool - show a squad mate's POV while you are downed in WARDOGS.
Copyright (C) 2026 Sombrero / The Kennel

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>
#include <QMainWindow>
#include <QTimer>
#include <QPointer>
#include <QFontDatabase>
#include "engine.h"
#include "ui/dock.h"
#include "ui/settings-dialog.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

// Both are owned by OBS's main window (the Engine as a child QObject, the Dock through the dock
// system) and die with it, which happens BEFORE obs_module_unload runs. QPointer goes null the
// moment they do, so the unload never touches a dead object (the 0.18.x shutdown crash).
static QPointer<Engine> g_engine;
static QPointer<Dock> g_dock;
static obs_hotkey_id g_hkToggle = OBS_INVALID_HOTKEY_ID, g_hkCapture = OBS_INVALID_HOTKEY_ID,
		     g_hkClip = OBS_INVALID_HOTKEY_ID, g_hkDual = OBS_INVALID_HOTKEY_ID,
		     g_hkReplay = OBS_INVALID_HOTKEY_ID, g_hkHighlights = OBS_INVALID_HOTKEY_ID;

static void hotkeyToggle(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed && g_engine)
		QMetaObject::invokeMethod(g_engine, "toggle", Qt::QueuedConnection);
}

static void hotkeyDual(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed && g_engine)
		QMetaObject::invokeMethod(g_engine, "toggleDual", Qt::QueuedConnection);
}

static void hotkeyReplay(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed && g_engine)
		QMetaObject::invokeMethod(
			g_engine,
			[] {
				if (g_engine)
					g_engine->replaying() ? g_engine->stopReplay("hotkey")
							      : g_engine->playReplay("hotkey");
			},
			Qt::QueuedConnection);
}

static void hotkeyHighlights(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed && g_engine)
		QMetaObject::invokeMethod(
			g_engine,
			[] {
				if (g_engine)
					g_engine->replaying() ? g_engine->stopReplay("hotkey")
							      : g_engine->playCompilation("hotkey");
			},
			Qt::QueuedConnection);
}

static void hotkeyCapture(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed && g_engine)
		QMetaObject::invokeMethod(g_engine, "captureTemplate", Qt::QueuedConnection);
}

static void hotkeyClip(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed && g_engine)
		QMetaObject::invokeMethod(
			g_engine,
			[] {
				if (g_engine)
					g_engine->clipNow();
			},
			Qt::QueuedConnection);
}

static void loadHotkeys()
{
	std::string path = Config::configFile("hotkeys.json");
	obs_data_t *d = obs_data_create_from_json_file(path.c_str());
	if (!d)
		return;
	obs_data_array_t *a = obs_data_get_array(d, "toggle");
	if (a) {
		obs_hotkey_load(g_hkToggle, a);
		obs_data_array_release(a);
	}
	a = obs_data_get_array(d, "capture");
	if (a) {
		obs_hotkey_load(g_hkCapture, a);
		obs_data_array_release(a);
	}
	a = obs_data_get_array(d, "clip");
	if (a) {
		obs_hotkey_load(g_hkClip, a);
		obs_data_array_release(a);
	}
	a = obs_data_get_array(d, "dual");
	if (a) {
		obs_hotkey_load(g_hkDual, a);
		obs_data_array_release(a);
	}
	a = obs_data_get_array(d, "replay");
	if (a) {
		obs_hotkey_load(g_hkReplay, a);
		obs_data_array_release(a);
	}
	a = obs_data_get_array(d, "highlights");
	if (a) {
		obs_hotkey_load(g_hkHighlights, a);
		obs_data_array_release(a);
	}
	obs_data_release(d);
}

static void saveHotkeys()
{
	obs_data_t *d = obs_data_create();
	obs_data_array_t *a = obs_hotkey_save(g_hkToggle);
	obs_data_set_array(d, "toggle", a);
	obs_data_array_release(a);
	a = obs_hotkey_save(g_hkCapture);
	obs_data_set_array(d, "capture", a);
	obs_data_array_release(a);
	a = obs_hotkey_save(g_hkDual);
	obs_data_set_array(d, "dual", a);
	obs_data_array_release(a);
	a = obs_hotkey_save(g_hkClip);
	obs_data_set_array(d, "clip", a);
	obs_data_array_release(a);
	a = obs_hotkey_save(g_hkReplay);
	obs_data_set_array(d, "replay", a);
	obs_data_array_release(a);
	a = obs_hotkey_save(g_hkHighlights);
	obs_data_set_array(d, "highlights", a);
	obs_data_array_release(a);
	obs_data_save_json_safe(d, Config::configFile("hotkeys.json").c_str(), "tmp", "bak");
	obs_data_release(d);
}

static void onFrontendEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		if (g_engine) {
			g_engine->start();
			if (g_engine->needsSetup() && g_dock)
				QTimer::singleShot(1500, g_dock, [] {
					if (g_dock)
						g_dock->openWizard();
				});
			else if (g_engine->wantsSupportNote() && g_dock)
				QTimer::singleShot(4000, g_dock, [] {
					if (g_dock && g_engine && g_engine->wantsSupportNote())
						g_dock->showSupportNote();
				});
		}
	} else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP) {
		// before OBS releases every source (at exit, or a collection change): ours go first
		if (g_engine)
			g_engine->sceneCleanup();
	} else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED) {
		if (g_engine)
			g_engine->reloadConfig();
	} else if (event == OBS_FRONTEND_EVENT_SCENE_CHANGED) {
		if (g_dock)
			QTimer::singleShot(0, g_dock, [] {
				if (g_dock)
					g_dock->refresh(); // the "live scene is not the plugin's scene" note
			});
	} else if (event == OBS_FRONTEND_EVENT_STREAMING_STARTED) {
		if (g_engine)
			g_engine->onStreaming(true);
	} else if (event == OBS_FRONTEND_EVENT_STREAMING_STOPPED) {
		if (g_engine)
			g_engine->onStreaming(false);
	} else if (event == OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED) {
		if (g_engine)
			g_engine->onReplaySaved();
	} else if (event == OBS_FRONTEND_EVENT_EXIT) {
		saveHotkeys();
		if (g_engine)
			g_engine->stop();
	}
}

bool obs_module_load(void)
{
	// the brand faces the dock and the settings use, shipped with the overlay page
	for (const char *f : {"overlay/SairaCondensed-Bold.ttf", "overlay/IBMPlexMono-SemiBold.ttf"}) {
		char *p = obs_module_file(f);
		if (p) {
			QFontDatabase::addApplicationFont(QString::fromUtf8(p));
			bfree(p);
		}
	}
	auto *main = (QMainWindow *)obs_frontend_get_main_window();
	g_engine = new Engine(main);
	g_dock = new Dock(g_engine);
	obs_frontend_add_dock_by_id("kennelgg_dock", obs_module_text("KennelWardogs"), g_dock);
	obs_frontend_add_tools_menu_item(
		obs_module_text("KennelWardogs.Settings"),
		[](void *) {
			if (g_dock)
				g_dock->openSettings();
		},
		nullptr);
	g_hkToggle = obs_hotkey_register_frontend("kennel.pov.toggle", obs_module_text("KennelWardogs.Hotkey.Toggle"),
						  hotkeyToggle, nullptr);
	g_hkCapture = obs_hotkey_register_frontend(
		"kennel.pov.capture", obs_module_text("KennelWardogs.Hotkey.Capture"), hotkeyCapture, nullptr);
	g_hkClip = obs_hotkey_register_frontend("kennel.clip.now", obs_module_text("KennelWardogs.Hotkey.Clip"),
						hotkeyClip, nullptr);
	g_hkDual = obs_hotkey_register_frontend("kennel.dual.toggle", obs_module_text("KennelWardogs.Hotkey.Dual"),
						hotkeyDual, nullptr);
	g_hkReplay = obs_hotkey_register_frontend("kennel.replay.play", obs_module_text("KennelWardogs.Hotkey.Replay"),
						  hotkeyReplay, nullptr);
	g_hkHighlights = obs_hotkey_register_frontend("kennel.replay.highlights",
						      obs_module_text("KennelWardogs.Hotkey.Highlights"),
						      hotkeyHighlights, nullptr);
	loadHotkeys();
	obs_frontend_add_event_callback(onFrontendEvent, nullptr);
	obs_log(LOG_INFO, "Kennel.gg Wardogs Streaming Tool loaded (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
	if (g_hkToggle != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(g_hkToggle);
	if (g_hkCapture != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(g_hkCapture);
	if (g_hkDual != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(g_hkDual);
	if (g_hkClip != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(g_hkClip);
	if (g_hkReplay != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(g_hkReplay);
	if (g_hkHighlights != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(g_hkHighlights);
	if (g_engine) // still alive only when the main window is (an unload without an exit)
		g_engine->stop();
	g_engine.clear();
	g_dock.clear();
	obs_log(LOG_INFO, "Kennel.gg Wardogs Streaming Tool unloaded");
}
