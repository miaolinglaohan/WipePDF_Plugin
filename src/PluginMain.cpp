#include "PluginInit.h"
#include "MenuHandler.h"
#include "SelectionTool.h"
#include <cstdio>

static void LogPlugin(const char *msg) {
    char tempPath[512];
    DWORD len = GetTempPathA(sizeof(tempPath), tempPath);
    if (len > 0) {
        strcat_s(tempPath, sizeof(tempPath), "WipePDF_Plugin.log");
        FILE *fp = NULL;
        if (fopen_s(&fp, tempPath, "a") == 0 && fp) {
            fprintf(fp, "[WipePDF] %s\n", msg);
            fclose(fp);
        }
    }
}

// Windows DLL Entry Point
BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    (void)hModule; (void)lpReserved;
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        LogPlugin("DllMain: DLL_PROCESS_ATTACH");
        break;
    case DLL_PROCESS_DETACH:
        LogPlugin("DllMain: DLL_PROCESS_DETACH");
        break;
    }
    return TRUE;
}

// Required Extension Name for Acrobat Plugin
ACCB1 const char* ACCB2 GetExtensionName(void) {
    return "ADBE:WipePDF";
}

// Export HFTs
ACCB1 ASBool ACCB2 PluginExportHFTs(void) {
    return TRUE;
}

// Import HFTs
ACCB1 ASBool ACCB2 PluginImportHFTs(void) {
    return TRUE;
}

// Plugin Initialization
ACCB1 ASBool ACCB2 PluginInit(void) {
    LogPlugin("PluginInit: setting up menus...");
    wipepdf::MenuHandler::setupMenus();
    wipepdf::SelectionTool::RegisterTool();
    LogPlugin("PluginInit: completed successfully.");
    return TRUE;
}

// Plugin Unload
ACCB1 ASBool ACCB2 PluginUnload(void) {
    LogPlugin("PluginUnload: cleaning up menus...");
    wipepdf::MenuHandler::cleanupMenus();
    wipepdf::SelectionTool::UnregisterTool();
    return TRUE;
}

// Handshake routine required by PIMain.c
ACCB1 ASBool ACCB2 PIHandshake(ASUns32 handshakeVersion, void *handshakeData) {
    LogPlugin("PIHandshake called");
    if (handshakeVersion == HANDSHAKE_V0200) {
        PIHandshakeData_V0200 *hsData = (PIHandshakeData_V0200 *)handshakeData;
        hsData->extensionName = ASAtomFromString("ADBE:WipePDF");
        hsData->exportHFTsCallback = (ASCallback)ASCallbackCreateProto(PIExportHFTsProcType, &PluginExportHFTs);
        hsData->importReplaceAndRegisterCallback = (ASCallback)ASCallbackCreateProto(PIImportReplaceAndRegisterProcType, &PluginImportHFTs);
        hsData->initCallback = (ASCallback)ASCallbackCreateProto(PIInitProcType, &PluginInit);
        hsData->unloadCallback = (ASCallback)ASCallbackCreateProto(PIUnloadProcType, &PluginUnload);
        LogPlugin("PIHandshake: V0200 callbacks registered successfully");
        return TRUE;
    }
    LogPlugin("PIHandshake: unsupported version");
    return FALSE;
}
