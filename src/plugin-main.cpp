#include <obs-module.h>
#include <plugin-support.h>

#include "mouse-sliding-source.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Mouse Sliding - velocity-based mouse overlay for streamers";
}

bool obs_module_load(void)
{
	obs_register_source(&mouse_sliding_source_info);
	obs_log(LOG_INFO, "Mouse Sliding loaded (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "Mouse Sliding unloaded");
}
