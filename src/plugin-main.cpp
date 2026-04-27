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
