/*
Zoominator - Professional Zoom & Mouse Follow tool for OBS Studio
Copyright (C) 2026 Marco Maxim

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include <obs-module.h>
#include <obs-frontend-api.h>

#include "plugin-support.h"
#include "zoominator-controller.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static void open_dialog_cb(void *)
{
	ZoominatorController::instance().showDialog();
}

static void show_dock_cb(void *)
{
	ZoominatorController::instance().toggleDockVisibility(true);
}

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "[Zoominator] loaded (version %s)", PLUGIN_VERSION);

	ZoominatorController::instance().initialize();

	obs_frontend_add_tools_menu_item("Zoominator ...", open_dialog_cb, nullptr);
	return true;
}

void obs_module_unload(void)
{
	ZoominatorController::instance().shutdown();
	obs_log(LOG_INFO, "[Zoominator] unloaded");
}
