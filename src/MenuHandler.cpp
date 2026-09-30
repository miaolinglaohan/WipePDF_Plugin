#include "MenuHandler.h"
#include "WatermarkService.h"
#include "MetadataService.h"
#include "BackupService.h"
#include "SelectionTool.h"
#include <string>
#include <vector>

namespace wipepdf {

// Global clean options persisted from the settings dialog.
static CleanOptions gSettings;
static const wchar_t *kSettingsClass = L"WipePDFSettingsWnd";

// Control IDs for the settings dialog.
enum {
    IDC_CHK_TRANSPARENT = 1001,
    IDC_CHK_PATTERN,
    IDC_CHK_KEYWORD,
    IDC_CHK_LINKS,
    IDC_CHK_BOTTOMSTRIP,
    IDC_BTN_OK,
    IDC_BTN_CANCEL
};

// Read checkbox states back into gSettings.
static void SaveSettingsFromDialog(HWND hDlg) {
    gSettings.removeTransparentText = (SendMessageW(GetDlgItem(hDlg, IDC_CHK_TRANSPARENT), BM_GETCHECK, 0, 0) == BST_CHECKED);
    gSettings.removePatternFills = (SendMessageW(GetDlgItem(hDlg, IDC_CHK_PATTERN), BM_GETCHECK, 0, 0) == BST_CHECKED);
    gSettings.removeKeywordText = (SendMessageW(GetDlgItem(hDlg, IDC_CHK_KEYWORD), BM_GETCHECK, 0, 0) == BST_CHECKED);
    gSettings.removeLinks = (SendMessageW(GetDlgItem(hDlg, IDC_CHK_LINKS), BM_GETCHECK, 0, 0) == BST_CHECKED);
    gSettings.removeBottomStrip = (SendMessageW(GetDlgItem(hDlg, IDC_CHK_BOTTOMSTRIP), BM_GETCHECK, 0, 0) == BST_CHECKED);
}

static LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_BTN_OK) {
            SaveSettingsFromDialog(hWnd);
            DestroyWindow(hWnd);
            return 0;
        } else if (id == IDC_BTN_CANCEL) {
            DestroyWindow(hWnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static HWND CreateCheckbox(HWND parent, const wchar_t *text, int id, int x, int y, int w, int h, bool checked) {
    HWND hChk = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    if (hChk && checked) SendMessageW(hChk, BM_SETCHECK, BST_CHECKED, 0);
    return hChk;
}

// A simple custom class for the settings window.
static void ShowSettingsDialog() {
    HINSTANCE hInst = GetModuleHandleW(NULL);

    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = SettingsWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)MAKEINTRESOURCEW(IDC_ARROW));
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kSettingsClass;
    RegisterClassW(&wc);

    HWND hDlg = CreateWindowExW(0, kSettingsClass, L"WipePDF 水印清理规则设置",
                                WS_CAPTION | WS_SYSMENU | WS_OVERLAPPED,
                                CW_USEDEFAULT, CW_USEDEFAULT, 420, 300,
                                NULL, NULL, hInst, NULL);
    if (!hDlg) return;

    // Title hint.
    CreateWindowExW(0, L"STATIC", L"选择启用的水印清理规则：",
                    WS_CHILD | WS_VISIBLE, 16, 14, 380, 20, hDlg, NULL, hInst, NULL);

    int y = 44;
    CreateCheckbox(hDlg, L"清除透明/隐形文字（RenderMode=3、Opacity=0）", IDC_CHK_TRANSPARENT, 20, y, 370, 22, gSettings.removeTransparentText); y += 30;
    CreateCheckbox(hDlg, L"清除全页 Pattern 图案填充与底纹", IDC_CHK_PATTERN, 20, y, 370, 22, gSettings.removePatternFills); y += 30;
    CreateCheckbox(hDlg, L"清除关键词水印文本（URL、推广词等）", IDC_CHK_KEYWORD, 20, y, 370, 22, gSettings.removeKeywordText); y += 30;
    CreateCheckbox(hDlg, L"清除链接注解（含目录跳转/外部链接，默认关闭）", IDC_CHK_LINKS, 20, y, 370, 22, gSettings.removeLinks); y += 30;
    CreateCheckbox(hDlg, L"清除页面底部通栏与贴底小图", IDC_CHK_BOTTOMSTRIP, 20, y, 370, 22, gSettings.removeBottomStrip); y += 44;

    CreateWindowExW(0, L"BUTTON", L"确定",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                    160, y, 90, 28, hDlg, (HMENU)(INT_PTR)IDC_BTN_OK, hInst, NULL);
    CreateWindowExW(0, L"BUTTON", L"取消",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    270, y, 90, 28, hDlg, (HMENU)(INT_PTR)IDC_BTN_CANCEL, hInst, NULL);

    // Center the dialog.
    RECT rc; GetWindowRect(hDlg, &rc);
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int x = (sw - (rc.right - rc.left)) / 2, yy = (sh - (rc.bottom - rc.top)) / 2;
    SetWindowPos(hDlg, NULL, x, yy, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);

    // Run a modal message loop until destroyed.
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    UnregisterClassW(kSettingsClass, hInst);
}

static AVMenu gWipePDFMenu = NULL;
static AVMenuItem gItemClean = NULL;
static AVMenuItem gItemInspect = NULL;
static AVMenuItem gItemBatch = NULL;
static AVMenuItem gItemSettings = NULL;
static AVMenuItem gItemAbout = NULL;
static AVMenuItem gEditItemClean = NULL;
static AVMenuItem gItemSelectionTool = NULL;
static AVMenuItem gItemWhiteout = NULL;
static AVMenuItem gItemMetadata = NULL;
static AVMenuItem gItemBatchMetadata = NULL;
static AVMenuItem gItemOpenBackupFolder = NULL;
static AVMenuItem gItemClearBackups = NULL;

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

ACCB1 void ACCB2 OnCleanMetadataProc(void *clientData) {
    (void)clientData;
    MenuHandler::onCleanMetadata();
}

ACCB1 void ACCB2 OnCleanBatchMetadataProc(void *clientData) {
    (void)clientData;
    MenuHandler::onCleanBatchMetadata();
}

ACCB1 void ACCB2 OnOpenBackupFolderProc(void *clientData) {
    (void)clientData;
    MenuHandler::onOpenBackupFolder();
}

ACCB1 void ACCB2 OnClearBackupsProc(void *clientData) {
    (void)clientData;
    MenuHandler::onClearBackups();
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

        gItemSelectionTool = CreateUnicodeMenuItem(L"点选同款水印清理...", "ADBE:WipePDF:SelectionTool", OnSelectionToolProc, OnDocOpenEnabledProc);
        if (gItemSelectionTool) AVMenuAddMenuItem(gWipePDFMenu, gItemSelectionTool, APPEND_MENUITEM);

        gItemWhiteout = CreateUnicodeMenuItem(L"框选涂白遮盖 (底图融合水印)...", "ADBE:WipePDF:WhiteoutTool", OnSelectionToolProc, OnDocOpenEnabledProc);
        if (gItemWhiteout) AVMenuAddMenuItem(gWipePDFMenu, gItemWhiteout, APPEND_MENUITEM);

        gItemInspect = CreateUnicodeMenuItem(L"扫描当前页面水印元素...", "ADBE:WipePDF:Inspect", OnInspectCurrentPageProc, OnDocOpenEnabledProc);
        if (gItemInspect) AVMenuAddMenuItem(gWipePDFMenu, gItemInspect, APPEND_MENUITEM);

        AVMenuItem itemSep = CreateUnicodeMenuItem(L"-", "ADBE:WipePDF:Sep", NULL, NULL);
        if (itemSep) AVMenuAddMenuItem(gWipePDFMenu, itemSep, APPEND_MENUITEM);

        gItemBatch = CreateUnicodeMenuItem(L"批量文档水印清理...", "ADBE:WipePDF:Batch", OnCleanBatchProc, NULL);
        if (gItemBatch) AVMenuAddMenuItem(gWipePDFMenu, gItemBatch, APPEND_MENUITEM);

        gItemBatchMetadata = CreateUnicodeMenuItem(L"批量文档元数据清理...", "ADBE:WipePDF:BatchMetadata", OnCleanBatchMetadataProc, NULL);
        if (gItemBatchMetadata) AVMenuAddMenuItem(gWipePDFMenu, gItemBatchMetadata, APPEND_MENUITEM);

        gItemMetadata = CreateUnicodeMenuItem(L"一键清除当前文档元数据", "ADBE:WipePDF:CleanMetadata", OnCleanMetadataProc, OnDocOpenEnabledProc);
        if (gItemMetadata) AVMenuAddMenuItem(gWipePDFMenu, gItemMetadata, APPEND_MENUITEM);

        AVMenuItem itemSep2 = CreateUnicodeMenuItem(L"-", "ADBE:WipePDF:Sep2", NULL, NULL);
        if (itemSep2) AVMenuAddMenuItem(gWipePDFMenu, itemSep2, APPEND_MENUITEM);

        gItemOpenBackupFolder = CreateUnicodeMenuItem(L"打开安全备份文件夹...", "ADBE:WipePDF:OpenBackupFolder", OnOpenBackupFolderProc, NULL);
        if (gItemOpenBackupFolder) AVMenuAddMenuItem(gWipePDFMenu, gItemOpenBackupFolder, APPEND_MENUITEM);

        gItemClearBackups = CreateUnicodeMenuItem(L"一键清空历史备份文件...", "ADBE:WipePDF:ClearBackups", OnClearBackupsProc, NULL);
        if (gItemClearBackups) AVMenuAddMenuItem(gWipePDFMenu, gItemClearBackups, APPEND_MENUITEM);

        AVMenuItem itemSep3 = CreateUnicodeMenuItem(L"-", "ADBE:WipePDF:Sep3", NULL, NULL);
        if (itemSep3) AVMenuAddMenuItem(gWipePDFMenu, itemSep3, APPEND_MENUITEM);

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
    // Release child items BEFORE the menu that hosts them, and release every
    // item created in setupMenus (AVMenuAddMenuItem does not transfer
    // ownership, so touching items after the menu is released would be a
    // use-after-free).
    AVMenuItem items[] = { gItemClean, gItemSelectionTool, gItemWhiteout, gItemInspect,
                           gItemBatch, gItemBatchMetadata, gItemMetadata, gItemOpenBackupFolder, gItemClearBackups,
                           gItemSettings, gItemAbout, gEditItemClean };
    for (AVMenuItem item : items) {
        if (item) { AVMenuItemRemove(item); AVMenuItemRelease(item); }
    }
    gItemClean = gItemSelectionTool = gItemWhiteout = gItemInspect = NULL;
    gItemBatch = gItemBatchMetadata = gItemMetadata = gItemOpenBackupFolder = gItemClearBackups = NULL;
    gItemSettings = gItemAbout = NULL;
    gEditItemClean = NULL;

    if (gWipePDFMenu) { AVMenuRemove(gWipePDFMenu); AVMenuRelease(gWipePDFMenu); gWipePDFMenu = NULL; }
}

void MenuHandler::onCleanActiveDoc() {
    AVDoc avDoc = AVAppGetActiveDoc();
    if (!avDoc) {
        MessageBoxW(NULL, L"请在 Adobe Acrobat 中打开需要清理水印的 PDF 文档", L"WipePDF 提示", MB_OK | MB_ICONWARNING);
        return;
    }

    CleanResult res = WatermarkService::cleanActiveDocument(gSettings);
    
    std::wstring msg = L"水印清理完成！\n\n共成功检测并无损清除 " + std::to_wstring(res.totalRemoved) + L" 处水印对象。\n";
    if (res.totalRemoved > 0) {
        msg += L"\n";
        if (res.removedLinks > 0) msg += L"  - " + std::to_wstring(res.removedLinks) + L" 处链接注释\n";
        if (res.removedTransparentText > 0) msg += L"  - " + std::to_wstring(res.removedTransparentText) + L" 处透明文本\n";
        if (res.removedOverlays > 0) msg += L"  - " + std::to_wstring(res.removedOverlays) + L" 处整页覆盖层/大字水印\n";
        if (res.removedKeywordText > 0) msg += L"  - " + std::to_wstring(res.removedKeywordText) + L" 处关键字文本\n";
        if (res.removedPatternPaths > 0) msg += L"  - " + std::to_wstring(res.removedPatternPaths) + L" 处图案/路径\n";
        if (res.removedBottomStrips > 0) msg += L"  - " + std::to_wstring(res.removedBottomStrips) + L" 处底部通栏/贴底小图\n";
        if (res.removedImages > 0) msg += L"  - " + std::to_wstring(res.removedImages) + L" 处水印图片\n";
        if (res.removedTargetFingers > 0) msg += L"  - " + std::to_wstring(res.removedTargetFingers) + L" 处手动点选同款水印\n";
    }
    
    msg += L"\n页面已即时刷新，您可以直接保存文档。";
    if (!res.backupPath.empty()) {
        msg += L"\n\n⚠️ 操作前已自动备份原文档至：\n    " + res.backupPath + L"\n    如误删可用该文件恢复。";
    }
    MessageBoxW(NULL, msg.c_str(), L"WipePDF 水印清理成功", MB_OK | MB_ICONINFORMATION);
}

void MenuHandler::onSelectionTool() {
    AVDoc avDoc = AVAppGetActiveDoc();
    if (!avDoc) {
        MessageBoxW(NULL, L"请在 Adobe Acrobat 中打开需要清理水印的 PDF 文档", L"WipePDF 提示", MB_OK | MB_ICONWARNING);
        return;
    }
    AVTool activeTool = AVAppGetActiveTool();
    SelectionTool::ActivateTool(activeTool);
}

void MenuHandler::onInspectCurrentPage() {
    AVDoc avDoc = AVAppGetActiveDoc();
    if (!avDoc) {
        MessageBoxW(NULL, L"请在 Adobe Acrobat 中打开需要检查的 PDF 文档", L"WipePDF 提示", MB_OK | MB_ICONWARNING);
        return;
    }
    PDDoc pdDoc = AVDocGetPDDoc(avDoc);
    if (!pdDoc) return;

    AVPageView pageView = AVDocGetPageView(avDoc);
    if (!pageView) {
        MessageBoxW(NULL, L"未找到当前页面视图", L"WipePDF 提示", MB_OK | MB_ICONWARNING);
        return;
    }
    PDPageNumber pageNum = AVPageViewGetPageNum(pageView);
    if (pageNum < 0 || pageNum >= PDDocGetNumPages(pdDoc)) {
        MessageBoxW(NULL, L"无法确定当前页", L"WipePDF 提示", MB_OK | MB_ICONWARNING);
        return;
    }

    CleanOptions opts;
    PageInspectResult r = WatermarkService::inspectPage(pdDoc, (ASInt32)pageNum, opts);

    std::wstring msg;
    msg += L"当前页面（第 " + std::to_wstring((long long)pageNum + 1) + L" 页）水印元素透视分析：\n\n";
    msg += L"  页面元素总数：   " + std::to_wstring(r.totalElements) + L"\n";
    msg += L"  文本元素：       " + std::to_wstring(r.textElements) + L"\n";
    msg += L"    其中隐形文本： " + std::to_wstring(r.transparentTextCount) + L"\n";
    msg += L"    其中关键词水印：" + std::to_wstring(r.keywordWatermarkCount) + L"\n";
    msg += L"  路径元素：       " + std::to_wstring(r.pathElements) + L"\n";
    msg += L"    其中图案填充： " + std::to_wstring(r.patternFillCount) + L"\n";
    msg += L"  图片元素：       " + std::to_wstring(r.imageElements) + L"\n";
    msg += L"  链接注解：       " + std::to_wstring(r.linkAnnotations) + L"\n";

    if (!r.detectedKeywords.empty()) {
        msg += L"\n命中关键词：";
        for (const auto &kw : r.detectedKeywords) {
            msg += L"\n  - " + wipepdf::Utf8ToWString(kw);
        }
    }

    msg += L"\n\n提示：以上为按当前规则识别出的水印特征，可返回主菜单执行一键清除。";
    MessageBoxW(NULL, msg.c_str(), L"WipePDF 页面扫描分析", MB_OK | MB_ICONINFORMATION);
}

void MenuHandler::onCleanBatch() {
    // Multi-select PDF files via native open dialog.
    wchar_t fileBuf[32768] = {0};
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = L"PDF 文档 (*.pdf)\0*.pdf\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = _countof(fileBuf);
    ofn.lpstrTitle = L"选择要批量清理水印的 PDF 文件（可多选）";
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER;

    if (!GetOpenFileNameW(&ofn)) return;

    // Parse multi-select buffer: first string is directory, then filenames.
    std::wstring dir = fileBuf;
    size_t offset = dir.size() + 1;
    std::vector<std::wstring> files;
    if (fileBuf[offset] != L'\0') {
        while (fileBuf[offset] != L'\0') {
            files.push_back(std::wstring(&fileBuf[offset]));
            offset += files.back().size() + 1;
        }
    } else {
        // Single file selected.
        files.push_back(dir);
        dir.clear();
    }

    if (files.empty()) return;

    // Confirm.
    std::wstring confirm = L"将在以下 " + std::to_wstring(files.size()) + L" 个文件上执行水印清理（会直接覆盖保存）：\n\n";
    for (const auto &f : files) confirm += L"  - " + f + L"\n";
    confirm += L"\n是否继续？";
    if (MessageBoxW(NULL, confirm.c_str(), L"WipePDF 批量清理确认", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;

    // Process each file.
    CleanResult total;
    int okCount = 0;
    std::vector<std::wstring> errors;
    for (const auto &file : files) {
        std::wstring fullPath = dir.empty() ? file : dir + L"\\" + file;

        // Build ASPathName from the wide-char path (Windows: use DIPath via ASText).
        ASText diPath = ASTextFromUnicode((const ASUTF16Val *)fullPath.c_str(), kUTF16HostEndian);
        if (!diPath) {
            errors.push_back(fullPath + L"（无法解析路径）");
            continue;
        }
        ASPathName path = ASFileSysCreatePathFromDIPathText(ASGetDefaultFileSys(), diPath, NULL);
        ASTextDestroy(diPath);
        if (!path) {
            errors.push_back(fullPath + L"（无法解析路径）");
            continue;
        }

        PDDoc doc = PDDocOpen(path, ASGetDefaultFileSys(), NULL, false);
        ASFileSysReleasePath(ASGetDefaultFileSys(), path);
        if (!doc) {
            errors.push_back(fullPath + L"（打开失败）");
            continue;
        }

        CleanResult res = WatermarkService::cleanDocument(doc);
        total.add(res);

        if (res.totalRemoved > 0) {
            // In-place save (NULL path): PDSaveCopy is for save-to-new-path only.
            PDDocSave(doc, PDSaveFull, NULL, ASGetDefaultFileSys(), NULL, NULL);
        }
        PDDocClose(doc);
        okCount++;
    }

    // Report.
    std::wstring msg = L"批量清理完成！\n\n";
    msg += L"  处理文件：   " + std::to_wstring(files.size()) + L" 个（成功 " + std::to_wstring(okCount) + L" 个）\n";
    msg += L"  共清除水印：" + std::to_wstring(total.totalRemoved) + L" 处\n";
    if (total.removedLinks > 0) msg += L"  - 链接注释：" + std::to_wstring(total.removedLinks) + L"\n";
    if (total.removedTransparentText > 0) msg += L"  - 透明文本：" + std::to_wstring(total.removedTransparentText) + L"\n";
    if (total.removedKeywordText > 0) msg += L"  - 关键词文本：" + std::to_wstring(total.removedKeywordText) + L"\n";
    if (total.removedPatternPaths > 0) msg += L"  - 图案/路径：" + std::to_wstring(total.removedPatternPaths) + L"\n";
    if (total.removedBottomStrips > 0) msg += L"  - 底部通栏：" + std::to_wstring(total.removedBottomStrips) + L"\n";
    if (total.removedImages > 0) msg += L"  - 水印图片：" + std::to_wstring(total.removedImages) + L"\n";
    if (total.removedTargetFingers > 0) msg += L"  - 手动点选同款：" + std::to_wstring(total.removedTargetFingers) + L"\n";

    if (!errors.empty()) {
        msg += L"\n以下文件未能处理：\n";
        for (const auto &e : errors) msg += L"  - " + e + L"\n";
    }

    msg += L"\n注意：已处理的文件已直接覆盖保存原文件。\n每个文件处理前均已在安全备份目录（%TEMP%\\WipePDF_Backups\\）自动生成备份，\n可通过主菜单“打开安全备份文件夹”查验。";
    MessageBoxW(NULL, msg.c_str(), L"WipePDF 批量清理完成", MB_OK | MB_ICONINFORMATION);
}
void MenuHandler::onCleanMetadata() {
    AVDoc avDoc = AVAppGetActiveDoc();
    if (!avDoc) {
        MessageBoxW(NULL, L"请在 Adobe Acrobat 中打开需要清理元数据的 PDF 文档", L"WipePDF 提示", MB_OK | MB_ICONWARNING);
        return;
    }
    PDDoc pdDoc = AVDocGetPDDoc(avDoc);
    if (!pdDoc) return;

    // Confirmation: list what metadata will be wiped.
    std::wstring confirm =
        L"将清除当前文档的全部元数据（文档内容不受影响）：\n\n"
        L"  - 文档信息：标题、作者、主题、关键词\n"
        L"  - 生成/转换工具（Creator、Producer）\n"
        L"  - 创建时间与修改时间\n"
        L"  - XMP 元数据（含拍摄设备、地理位置、自定义属性等隐藏信息）\n\n"
        L"注意：操作前会自动在安全备份目录生成原文档备份；\n此操作会直接修改当前文档，Acrobat 将在关闭时提示保存。\n\n是否继续？";
    if (MessageBoxW(NULL, confirm.c_str(), L"WipePDF 清除元数据确认", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;

    // Safety backup of the pristine document before modification (aligned
    // with the batch metadata flow - single-doc clean gets the same net).
    std::wstring backupPath;
    DURING
        backupPath = WatermarkService::BackupDocumentToTemp(pdDoc);
    HANDLER
        backupPath.clear();
    END_HANDLER

    MetadataCleanResult res = MetadataService::cleanActiveDocument();

    std::wstring msg = L"元数据清除完成！\n\n";
    msg += L"  - XMP 元数据流：" + std::wstring(res.xmpReplaced ? L"已重置为空" : L"重置失败（文档可能受保护）") + L"\n";
    msg += L"  - 文档信息条目：" + std::to_wstring(res.removedInfoKeys) + L" 项已删除\n";
    if (!backupPath.empty()) {
        msg += L"\n操作前已自动备份原文档，可通过主菜单“打开安全备份文件夹”查验。";
    } else {
        msg += L"\n⚠️ 备份创建失败，本次操作未生成备份文件。";
    }
    msg += L"\n文档内容未做任何改动。\n请按 Ctrl+S 保存，或直接关闭文档（Acrobat 会提示保存）。";
    MessageBoxW(NULL, msg.c_str(), L"WipePDF 元数据清除完成", MB_OK | MB_ICONINFORMATION);
}

void MenuHandler::onCleanBatchMetadata() {
    // Multi-select PDF files via native open dialog.
    wchar_t fileBuf[32768] = {0};
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = L"PDF 文档 (*.pdf)\0*.pdf\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = _countof(fileBuf);
    ofn.lpstrTitle = L"选择要批量清除元数据的 PDF 文件（可多选）";
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER;

    if (!GetOpenFileNameW(&ofn)) return;

    // Parse multi-select buffer: first string is directory, then filenames.
    std::wstring dir = fileBuf;
    size_t offset = dir.size() + 1;
    std::vector<std::wstring> files;
    if (fileBuf[offset] != L'\0') {
        while (fileBuf[offset] != L'\0') {
            files.push_back(std::wstring(&fileBuf[offset]));
            offset += files.back().size() + 1;
        }
    } else {
        // Single file selected.
        files.push_back(dir);
        dir.clear();
    }

    if (files.empty()) return;

    std::vector<std::wstring> fullPaths;
    fullPaths.reserve(files.size());
    for (const auto &f : files) {
        fullPaths.push_back(dir.empty() ? f : dir + L"\\" + f);
    }

    // Confirmation dialog with clear security assurances
    std::wstring confirm = L"将在以下 " + std::to_wstring(files.size()) + L" 个文件上执行元数据彻底清除：\n\n";
    size_t previewCount = files.size() > 8 ? 8 : files.size();
    for (size_t i = 0; i < previewCount; ++i) {
        confirm += L"  - " + files[i] + L"\n";
    }
    if (files.size() > 8) {
        confirm += L"  ... （共 " + std::to_wstring(files.size()) + L" 个文件）\n";
    }
    confirm += L"\n安全保障与操作说明：\n"
               L"  1. 每个文件在修改前均会自动在安全备份目录生成备份；\n"
               L"  2. 彻底清除文档标题、作者、创建工具、创建/修改日期及全部 XMP 扩展流；\n"
               L"  3. 采用物理覆写（PDSaveFull + 垃圾回收），防止残留元数据被二进制读取；\n"
               L"  4. 文档正文图文、格式、注释完全保持原样，不做任何改动。\n\n"
               L"是否立即开始批量处理？";

    if (MessageBoxW(NULL, confirm.c_str(), L"WipePDF 批量清除元数据确认", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;

    // Execute batch clean
    BatchMetadataResult res = MetadataService::cleanBatch(fullPaths);

    // Summary dialog
    std::wstring msg = L"批量清除元数据完成！\n\n";
    msg += L"  处理文件：   " + std::to_wstring(res.totalFiles) + L" 个（成功 " + std::to_wstring(res.successFiles) + L" 个";
    if (!res.errors.empty()) {
        msg += L"，失败 " + std::to_wstring(res.errors.size()) + L" 个";
    }
    msg += L"）\n";
    msg += L"  - XMP 元数据流重置：" + std::to_wstring(res.totalXmpReplaced) + L" 个文档\n";
    msg += L"  - 文档信息条目删除：" + std::to_wstring(res.totalRemovedInfoKeys) + L" 项\n\n";
    msg += L"安全提示：修改前已自动备份至专属安全备份文件夹。\n"
           L"（可通过主菜单“打开安全备份文件夹”随时查验）\n";

    if (!res.errors.empty()) {
        msg += L"\n以下文件未能处理：\n";
        for (const auto &e : res.errors) {
            msg += L"  - " + e + L"\n";
        }
    }

    MessageBoxW(NULL, msg.c_str(), L"WipePDF 批量元数据清除结果", MB_OK | (res.errors.empty() ? MB_ICONINFORMATION : MB_ICONWARNING));
}

void MenuHandler::onOpenBackupFolder() {
    if (!BackupService::OpenBackupFolder()) {
        std::wstring dir = BackupService::GetBackupDirectory();
        MessageBoxW(NULL, (L"无法自动打开文件夹，备份目录路径为：\n" + dir).c_str(), L"WipePDF 提示", MB_OK | MB_ICONWARNING);
    }
}

void MenuHandler::onClearBackups() {
    BackupStats stats = BackupService::GetStats();
    if (stats.fileCount == 0) {
        std::wstring dir = BackupService::GetBackupDirectory();
        std::wstring emptyMsg = L"当前没有历史备份文件，备份目录为空：\n" + dir;
        MessageBoxW(NULL, emptyMsg.c_str(), L"WipePDF 提示", MB_OK | MB_ICONINFORMATION);
        return;
    }

    wchar_t sizeStr[32];
    swprintf_s(sizeStr, L"%.1f", stats.totalSizeMB);
    std::wstring prompt = L"检测到本地共有 " + std::to_wstring(stats.fileCount) + L" 个历史备份文件，\n"
                          L"共占用磁盘空间 " + sizeStr + L" MB。\n\n"
                          L"备份存放于专用目录：\n" + BackupService::GetBackupDirectory() + L"\n\n"
                          L"清空后不可恢复，是否确认立即清空全部历史备份？";

    if (MessageBoxW(NULL, prompt.c_str(), L"WipePDF 清空历史备份确认", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        BackupStats res = BackupService::ClearAll();
        wchar_t freedStr[32];
        swprintf_s(freedStr, L"%.1f", res.totalSizeMB);
        std::wstring doneMsg = L"历史备份清理完成！\n\n"
                               L"共清除了 " + std::to_wstring(res.fileCount) + L" 个备份文件，\n"
                               L"成功释放磁盘空间 " + freedStr + L" MB。";
        MessageBoxW(NULL, doneMsg.c_str(), L"WipePDF 清理成功", MB_OK | MB_ICONINFORMATION);
    }
}

void MenuHandler::onShowSettings() {
    ShowSettingsDialog();
}
void MenuHandler::onAbout() {
    MessageBoxW(NULL, L"WipePDF Pro\nv1.7.0", L"WipePDF", MB_OK);
}

} // namespace wipepdf
