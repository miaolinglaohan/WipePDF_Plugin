#include "MenuHandler.h"
#include "WatermarkService.h"
#include <string>

namespace wipepdf {

void MenuHandler::setupMenus() {
#if HAS_ACROBAT_SDK
    // Attach menu items under Acrobat "Edit" or "Tools" menu
#endif
}

void MenuHandler::cleanupMenus() {
#if HAS_ACROBAT_SDK
    // Cleanup allocated menu items
#endif
}

void MenuHandler::onCleanActiveDoc() {
    int removed = WatermarkService::cleanActiveDocument();
    std::wstring msg = L"WipePDF Processing Complete!\nSuccessfully detected and removed " + std::to_wstring(removed) + L" watermark elements.";
    MessageBoxW(NULL, msg.c_str(), L"WipePDF (Acrobat Plugin)", MB_OK | MB_ICONINFORMATION);
}

void MenuHandler::onCleanBatch() {
    MessageBoxW(NULL, L"Batch Processing: Please select folder containing PDF documents to clean.", L"WipePDF Batch Clean", MB_OK | MB_ICONINFORMATION);
}

void MenuHandler::onShowSettings() {
    MessageBoxW(NULL, L"WipePDF Settings:\n- [x] Remove Transparent Link Watermarks\n- [x] Remove Full-page Pattern Background Fills\n- [x] Remove Footer Promotion Strips", L"WipePDF Settings", MB_OK | MB_ICONINFORMATION);
}

void MenuHandler::onAbout() {
    MessageBoxW(NULL, L"WipePDF Acrobat Enhancement Plugin v1.0.0\nBuilt natively on Adobe Acrobat Core API\n\nUltra-lightweight · Zero external engine · Lossless Removal", L"About WipePDF Plugin", MB_OK | MB_ICONINFORMATION);
}

} // namespace wipepdf
