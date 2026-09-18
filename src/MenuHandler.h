#pragma once
#include "PluginInit.h"

namespace wipepdf {

class MenuHandler {
public:
    static void setupMenus();
    static void cleanupMenus();

    // Callbacks
    static void onCleanActiveDoc();
    static void onCleanBatch();
    static void onShowSettings();
    static void onAbout();
};

} // namespace wipepdf
