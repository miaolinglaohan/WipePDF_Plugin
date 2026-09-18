#include "PluginInit.h"
#include "MenuHandler.h"

// Windows DLL Entry Point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    (void)hModule; (void)lpReserved;
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

// Acrobat Plug-in Export Handlers
#if HAS_ACROBAT_SDK

ACCALLBACK ASBool PluginInit(void) {
    wipepdf::MenuHandler::setupMenus();
    return TRUE;
}

ACCALLBACK ASBool PluginUnload(void) {
    wipepdf::MenuHandler::cleanupMenus();
    return TRUE;
}

ACCALLBACK ASBool PluginExportHFTs(void) {
    return TRUE;
}

ACCALLBACK ASBool PluginImportHFTs(void) {
    return TRUE;
}

// Required Handshake function for Acrobat Plugin loading
ACCALLBACK ASBool CheckHandshake(ASUns32 handshakeVersion, void *handshakeData) {
    if (handshakeVersion != HANDSHAKE_V0200) return FALSE;
    // Setup function pointers
    return TRUE;
}

#endif
