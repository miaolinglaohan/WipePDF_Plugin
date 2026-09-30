#pragma once
#include "PluginInit.h"

namespace wipepdf {

class MenuHandler {
public:
    static void setupMenus();
    static void cleanupMenus();

    static void onCleanActiveDoc();
    static void onInspectCurrentPage();
    static void onCleanBatch();
    static void onSelectionTool();
    static void onCleanMetadata();
    static void onCleanBatchMetadata();
    static void onOpenBackupFolder();
    static void onClearBackups();
    static void onShowSettings();
    static void onAbout();
};

} // namespace wipepdf
