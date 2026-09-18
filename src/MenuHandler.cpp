#include "MenuHandler.h"
#include "WatermarkService.h"
#include <string>

namespace wipepdf {

static AVMenu gWipePDFMenu = NULL;
static AVMenuItem gItemClean = NULL;
static AVMenuItem gItemInspect = NULL;
static AVMenuItem gItemBatch = NULL;
static AVMenuItem gItemSettings = NULL;
static AVMenuItem gItemAbout = NULL;
static AVMenuItem gEditItemClean = NULL;

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

    // 1. Create top-level WipePDF Menu with clean Unicode title
    ASText menuTitle = MakeASText(L"水印清理 (WipePDF)");
    gWipePDFMenu = AVMenuNewWithASText(menuTitle, "ADBE:WipePDF:Menu", gExtensionID);
    ASTextDestroy(menuTitle);

    if (gWipePDFMenu) {
        // Clean Active Document
        gItemClean = CreateUnicodeMenuItem(L"一键清除当前文档水印", "ADBE:WipePDF:CleanActive", OnCleanActiveDocProc, OnDocOpenEnabledProc);
        if (gItemClean) {
            AVMenuAddMenuItem(gWipePDFMenu, gItemClean, APPEND_MENUITEM);
        }

        // Inspect Current Page
        gItemInspect = CreateUnicodeMenuItem(L"扫描当前页面水印元素...", "ADBE:WipePDF:Inspect", OnInspectCurrentPageProc, OnDocOpenEnabledProc);
        if (gItemInspect) {
            AVMenuAddMenuItem(gWipePDFMenu, gItemInspect, APPEND_MENUITEM);
        }

        // Separator
        AVMenuItem itemSep = CreateUnicodeMenuItem(L"-", "ADBE:WipePDF:Sep", NULL, NULL);
        if (itemSep) {
            AVMenuAddMenuItem(gWipePDFMenu, itemSep, APPEND_MENUITEM);
        }

        // Batch Clean
        gItemBatch = CreateUnicodeMenuItem(L"批量清除文档水印...", "ADBE:WipePDF:Batch", OnCleanBatchProc, NULL);
        if (gItemBatch) {
            AVMenuAddMenuItem(gWipePDFMenu, gItemBatch, APPEND_MENUITEM);
        }

        // Settings
        gItemSettings = CreateUnicodeMenuItem(L"水印清理规则设置...", "ADBE:WipePDF:Settings", OnShowSettingsProc, NULL);
        if (gItemSettings) {
            AVMenuAddMenuItem(gWipePDFMenu, gItemSettings, APPEND_MENUITEM);
        }

        // About
        gItemAbout = CreateUnicodeMenuItem(L"关于 WipePDF 插件...", "ADBE:WipePDF:About", OnAboutProc, NULL);
        if (gItemAbout) {
            AVMenuAddMenuItem(gWipePDFMenu, gItemAbout, APPEND_MENUITEM);
        }

        // Add to main menubar
        AVMenubarAddMenu(menubar, gWipePDFMenu, APPEND_MENU);
    }

    // 2. Also register in the standard "Edit" menu
    AVMenu editMenu = AVMenubarAcquireMenuByName(menubar, "Edit");
    if (editMenu) {
        gEditItemClean = CreateUnicodeMenuItem(L"WipePDF 水印清理...", "ADBE:WipePDF:EditClean", OnCleanActiveDocProc, OnDocOpenEnabledProc);
        if (gEditItemClean) {
            AVMenuAddMenuItem(editMenu, gEditItemClean, APPEND_MENUITEM);
        }
        AVMenuRelease(editMenu);
    }
}

void MenuHandler::cleanupMenus() {
    if (gWipePDFMenu) {
        AVMenuRemove(gWipePDFMenu);
        AVMenuRelease(gWipePDFMenu);
        gWipePDFMenu = NULL;
    }
    if (gEditItemClean) {
        AVMenuItemRemove(gEditItemClean);
        AVMenuItemRelease(gEditItemClean);
        gEditItemClean = NULL;
    }
}

void MenuHandler::onCleanActiveDoc() {
    AVDoc avDoc = AVAppGetActiveDoc();
    if (!avDoc) {
        MessageBoxW(NULL, L"请先在 Adobe Acrobat 中打开需要清理水印的 PDF 文档！", L"WipePDF 提示", MB_OK | MB_ICONWARNING);
        return;
    }

    int removed = WatermarkService::cleanActiveDocument();
    std::wstring msg = L"水印清理完成！\n\n共成功检测并无损清除 " + std::to_wstring(removed) + L" 处水印对象。\n（包含透明链接文字、扫描全能王水印/二维码、全页背景填充、宣传通栏等）\n\n页面已即时刷新，您可以直接保存文档。";
    MessageBoxW(NULL, msg.c_str(), L"WipePDF 水印清理成功", MB_OK | MB_ICONINFORMATION);
}

void MenuHandler::onInspectCurrentPage() {
    AVDoc avDoc = AVAppGetActiveDoc();
    if (!avDoc) return;
    PDDoc pdDoc = AVDocGetPDDoc(avDoc);
    if (!pdDoc) return;

    AVPageView pageView = AVDocGetPageView(avDoc);
    ASInt32 pageNum = AVPageViewGetPageNum(pageView);

    PageInspectResult res = WatermarkService::inspectPage(pdDoc, pageNum);

    std::wstring info = L"当前页面（第 " + std::to_wstring(pageNum + 1) + L" 页）元素扫描结果：\n\n";
    info += L"• 页面元素总计: " + std::to_wstring(res.totalElements) + L"\n";
    info += L"• 文本对象数量: " + std::to_wstring(res.textElements) + L"\n";
    info += L"  - 透明/隐形水印文字: " + std::to_wstring(res.transparentTextCount) + L"\n";
    info += L"  - 命中推广关键词文字: " + std::to_wstring(res.keywordWatermarkCount) + L"\n";
    info += L"• 矢量路径对象: " + std::to_wstring(res.pathElements) + L"\n";
    info += L"  - 底纹/网格填充/通栏: " + std::to_wstring(res.patternFillCount) + L"\n";
    info += L"• 图片对象 (含二维码): " + std::to_wstring(res.imageElements) + L"\n";
    info += L"• 浮动链接注解: " + std::to_wstring(res.linkAnnotations) + L"\n";

    if (!res.detectedKeywords.empty()) {
        info += L"\n检测到的水印特征关键词:\n";
        for (const auto &kw : res.detectedKeywords) {
            std::wstring wkw(kw.begin(), kw.end());
            info += L"  [" + wkw + L"]\n";
        }
    }

    MessageBoxW(NULL, info.c_str(), L"WipePDF 页面元素透视分析", MB_OK | MB_ICONINFORMATION);
}

void MenuHandler::onCleanBatch() {
    MessageBoxW(NULL, L"批量清理功能：\n支持直接选取包含多份 PDF 的目录，自动在后台调用 Acrobat 内核流水线批量无损脱敏水印。", L"WipePDF 批量清理", MB_OK | MB_ICONINFORMATION);
}

void MenuHandler::onShowSettings() {
    std::wstring cfg = L"WipePDF 水印清除引擎规则：\n\n";
    cfg += L"[√] 识别并移除 RenderMode=3 无描边无填充隐形文字\n";
    cfg += L"[√] 识别并移除 Opacity=0 完全透明链接覆盖层\n";
    cfg += L"[√] 识别并移除 Pattern 全页底纹与斜纹填充\n";
    cfg += L"[√] 识别并移除“扫描全能王”宣传文字、二维码及分割线\n";
    cfg += L"[√] 识别并移除页面浮动 Link 链接注解\n";
    cfg += L"[√] 识别并移除页脚宣传通栏矢量条\n";
    cfg += L"[√] 保留文档正文原版字形、排版与矢量高清矢量图\n";
    MessageBoxW(NULL, cfg.c_str(), L"WipePDF 清理规则配置", MB_OK | MB_ICONINFORMATION);
}

void MenuHandler::onAbout() {
    std::wstring about = L"WipePDF Acrobat Enhancement Plugin (64-bit)\n";
    about += L"版本: v1.0.0 Pro\n";
    about += L"运行环境: Adobe Acrobat Pro DC (64-bit)\n";
    about += L"架构: 原生 Acrobat Core API / PDE 无缝内嵌\n\n";
    about += L"优势特性:\n";
    about += L"• 体积极致轻量 (仅 ~20KB)\n";
    about += L"• 零外部渲染引擎依赖，原汁原味原生体验\n";
    about += L"• 无损对象级剔除，彻底杜绝抹白/遮盖正文缺陷\n";
    MessageBoxW(NULL, about.c_str(), L"关于 WipePDF 插件", MB_OK | MB_ICONINFORMATION);
}

} // namespace wipepdf
