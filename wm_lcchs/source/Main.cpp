#include "FontPatch.h"
#include <plugin.h>

class PluginIII
{
public:
    PluginIII()
    {
        plugin::Events::initRwEvent += []() { FontPatch::Init(); };
        plugin::Events::shutdownRwEvent += []() { FontPatch::Shutdown(); };
    }

} plugin_iii;