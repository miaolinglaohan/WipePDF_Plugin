#include "MenuHandler.h"
#include "WatermarkService.h"
#include "SelectionTool.h"
#include <string>

namespace wipepdf {

static AVMenu gWipePDFMenu = NULL;
static AVMenuItem gItemClean = NULL;
static AVMenuItem gItemInspect = NULL;
static AVMenuItem gItemBatch = NULL;
static AVMenuItem gItemSettings = NULL;
static AVMenuItem gItemAbout = NULL;
static AVMenuItem gEditItemClean = NULL;
static AVMenuItem gItemSelectionTool = NULL;

ACCB1 void ACCB2 OnCleanActiveDocProc(void *clientData) {
    (void)clientData;
    MenuHandler::onCleanActiveDoc();
}

ACCB1 void ACCB2 OnInspectCurrentPageProc(void *clientData) {
    (void)clientData;
    MenuHandler::onInspectCurrentPage();
}

ACCB1 void ACCB2 OnCleanBatchProc(void *clientData) {
    (void)clientData;
    MenuHandler::onCleanBatch();
}

ACCB1 void ACCB2 OnShowSettingsProc(void *clientData) {
    (void)clientData;
    MenuHandler::onShowSettings();
}

ACCB1 void ACCB2 OnAboutProc(void *clientData) {
    (void)clientData;
    MenuHandler::onAbout();
}

ACCB1 void ACCB2 OnSelectionToolProc(void *clientData) {
    (void)clientData;
    MenuHandler::onSelectionTool();
}

ACCB1 ASBool ACCB2 OnDocOpenEnabledProc(void *clientData) {
    (void)clientData;
    AVDoc doc = AVAppGetActiveDoc();
    return (doc != NULL);
}

static ASText MakeASText(const wchar_t *wstr) {
    return ASTextFromUnicode((const ASUTF16Val*)wstr, kUTF16HostEndian);
}

static AVMenuItem CreateUnicodeMenuItem(const wchar_t *title, const char *name, AVExecuteProc execProc, AVComputeEnabledProc enabledProc) {
    ASText asText = MakeASText(title);
    AVMenuItem item = AVMenuItemNewWithASText(asText, name, NULL, false, NO_SHORTCUT, 0, NULL, gExtensionID);
    ASTextDestroy(asText);

    if (item) {
        if (execProc) {
            AVMenuItemSetExecuteProc(item, ASCallbackCreateProto(AVExecuteProc, execProc), NULL);
        }
        if (enabledProc) {
            AVMenuItemSetComputeEnabledProc(item, ASCallbackCreateProto(AVComputeEnabledProc, enabledProc), NULL);
        }
    }
    return item;
}

void MenuHandler::setupMenus() {
    AVMenubar menubar = AVAppGetMenubar();
    if (!menubar) return;

    ASText menuTitle = MakeASText(L"水印清理 (WipePDF)");
    gWipePDFMenu = AVMenuNewWithASText(menuTitle, "ADBE:WipePDF:Menu", gExtensionID);
    ASTextDestroy(menuTitle);

    if (gWipePDFMenu) {
        gItemClean = CreateUnicodeMenuItem(L"一键清除当前文档水印", "ADBE:WipePDF:CleanActive", OnCleanActiveDocProc, OnDocOpenEnabledProc);
        if (gItemClean) AVMenuAddMenuItem(gWipePDFMenu, gItemClean, APPEND_MENUITEM);

        gItemSelectionTool = CreateUnicodeMenuItem(L"手动点选同款水印...", "ADBE:WipePDF:SelectionTool", OnSelectionToolProc, OnDocOpenEnabledProc);
        if (gItemSelectionTool) AVMenuAddMenuItem(gWipePDFMenu, gItemSelectionTool, APPEND_MENUITEM);

        gItemInspect = CreateUnicodeMenuItem(L"扫描当前页面水印元素...", "ADBE:WipePDF:Inspect", OnInspectCurrentPageProc, OnDocOpenEnabledProc);
        if (gItemInspect) AVMenuAddMenuItem(gWipePDFMenu, gItemInspect, APPEND_MENUITEM);

        AVMenuItem itemSep = CreateUnicodeMenuItem(L"-", "ADBE:WipePDF:Sep", NULL, NULL);
        if (itemSep) AVMenuAddMenuItem(gWipePDFMenu, itemSep, APPEND_MENUITEM);

        gItemBatch = CreateUnicodeMenuItem(L"批量文档水印清理...", "ADBE:WipePDF:Batch", OnCleanBatchProc, NULL);
        if (gItemBatch) AVMenuAddMenuItem(gWipePDFMenu, gItemBatch, APPEND_MENUITEM);

        gItemSettings = CreateUnicodeMenuItem(L"水印清理规则设置...", "ADBE:WipePDF:Settings", OnShowSettingsProc, NULL);
        if (gItemSettings) AVMenuAddMenuItem(gWipePDFMenu, gItemSettings, APPEND_MENUITEM);

        gItemAbout = CreateUnicodeMenuItem(L"关于 WipePDF 插件...", "ADBE:WipePDF:About", OnAboutProc, NULL);
        if (gItemAbout) AVMenuAddMenuItem(gWipePDFMenu, gItemAbout, APPEND_MENUITEM);

        AVMenubarAddMenu(menubar, gWipePDFMenu, APPEND_MENU);
    }

    AVMenu editMenu = AVMenubarAcquireMenuByName(menubar, "Edit");
    if (editMenu) {
        gEditItemClean = CreateUnicodeMenuItem(L"WipePDF 水印清理...", "ADBE:WipePDF:EditClean", OnCleanActiveDocProc, OnDocOpenEnabledProc);
        if (gEditItemClean) AVMenuAddMenuItem(editMenu, gEditItemClean, APPEND_MENUITEM);
        AVMenuRelease(editMenu);
    }
}

void MenuHandler::cleanupMenus() {
    if (gWipePDFMenu) { AVMenuRemove(gWipePDFMenu); AVMenuRelease(gWipePDFMenu); gWipePDFMenu = NULL; }
    if (gItemSelectionTool) { AVMenuItemRemove(gItemSelectionTool); AVMenuItemRelease(gItemSelectionTool); gItemSelectionTool = NULL; }
    if (gEditItemClean) { AVMenuItemRemove(gEditItemClean); AVMenuItemRelease(gEditItemClean); gEditItemClean = NULL; }
}

void MenuHandler::onCleanActiveDoc() {
    AVDoc avDoc = AVAppGetActiveDoc();
    if (!avDoc) {
        MessageBoxW(NULL, L"请在 Adobe Acrobat 中打开需要清理水印的 PDF 文档", L"WipePDF 提示", MB_OK | MB_ICONWARNING);
        return;
    }

    CleanResult res = WatermarkService::cleanActiveDocument();
    
    std::wstring msg = L"水印清理完成！\n\n共成功检测并无损清除 " + std::to_wstring(res.totalRemoved) + L" 处水印对象。\n";
    if (res.totalRemoved > 0) {
        msg += L"\n";
        if (res.removedLinks > 0) msg += L"  - " + std::to_wstring(res.removedLinks) + L" 处链接注释\n";
        if (res.removedTransparentText > 0) msg += L"  - " + std::to_wstring(res.removedTransparentText) + L" 处透明文本\n";
        if (res.removedKeywordText > 0) msg += L"  - " + std::to_wstring(res.removedKeywordText) + L" 处关键字文本\n";
        if (res.removedPatternPaths > 0) msg += L"  - " + std::to_wstring(res.removedPatternPaths) + L" 处图案/路径\n";
        if (res.removedImages > 0) msg += L"  - " + std::to_wstring(res.removedImages) + L" 处水印图片\n";
        if (res.removedTargetFingers > 0) msg += L"  - " + std::to_wstring(res.removedTargetFingers) + L" 处手动点选同款水印\n";
    }
    
    msg += L"\n页面已即时刷新，您可以直接保存文档。";
    MessageBoxW(NULL, msg.c_str(), L"WipePDF 水印清理成功", MB_OK | MB_ICONINFORMATION);
}

void MenuHandler::onSelectionTool() {
    AVDoc avDoc = AVAppGetActiveDoc();
    if (!avDoc) {
        MessageBoxW(NULL, L"请在 Adobe Acrobat 中打开需要清理水印的 PDF 文档", L"WipePDF 提示", MB_OK | MB_ICONWARNING);
        return;
    }
    SelectionTool::ActivateTool();
}

void MenuHandler::onInspectCurrentPage() {
    MessageBoxW(NULL, L"页面检查功能", L"WipePDF", MB_OK);
}

void MenuHandler::onCleanBatch() {
    MessageBoxW(NULL, L"批量处理功能即将推出。", L"WipePDF", MB_OK);
}
void MenuHandler::onShowSettings() {
    MessageBoxW(NULL, L"规则设置面板即将推出。", L"WipePDF", MB_OK);
}
void MenuHandler::onAbout() {
    MessageBoxW(NULL, L"WipePDF Pro\nv1.0.0", L"WipePDF", MB_OK);
}

} // namespace wipepdf
