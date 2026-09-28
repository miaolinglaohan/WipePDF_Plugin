#include "SelectionTool.h"
#include "WatermarkService.h"

#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

namespace {

struct HighlightState {
    bool active = false;
    PDDoc pddoc = NULL;
    ASInt32 hitPageIndex = -1;
    ASFixedRect hitBBox = {0, 0, 0, 0};
    std::vector<wipepdf::WatermarkCandidate> candidates;
};

static HighlightState g_State;
static AVPageViewDrawProc g_DrawProc = NULL;

static void ClearHighlight(AVPageView pageView = NULL) {
    g_State.active = false;
    g_State.candidates.clear();
    g_State.pddoc = NULL;
    g_State.hitPageIndex = -1;

    if (pageView) {
        AVPageViewInvalidateRect(pageView, NULL);
        AVPageViewDrawNow(pageView);
    } else {
        AVDoc avDoc = AVAppGetActiveDoc();
        if (avDoc) {
            AVPageView pv = AVDocGetPageView(avDoc);
            if (pv) {
                AVPageViewInvalidateRect(pv, NULL);
                AVPageViewDrawNow(pv);
            }
        }
    }
}

static void ACCB1 MyPageViewDrawProc(AVPageView pageView, AVDevRect* updateRect, void* data) {
    if (!g_State.active || g_State.candidates.empty()) return;
    
    ASInt32 pageNum = AVPageViewGetPageNum(pageView);

    // Native Acrobat outline color: Pure Red (1.0, 0, 0)
    PDColorValueRec acroColor;
    acroColor.space = PDDeviceRGB;
    acroColor.value[0] = fixedOne;
    acroColor.value[1] = 0;
    acroColor.value[2] = 0;
    AVPageViewSetColor(pageView, &acroColor);

    // Draw all watermark candidates on the currently displayed page (supports multi-page browsing)
    for (size_t i = 0; i < g_State.candidates.size(); ++i) {
        const auto& cand = g_State.candidates[i];
        if (cand.pageIndex != pageNum) continue;

        ASFixedRect bbox = cand.bbox;
        AVDevRect devRect;
        AVPageViewRectToDevice(pageView, &bbox, &devRect);

        // Normalize device rectangle coordinates (left < right, top < bottom)
        int left   = (std::min)((int)devRect.left, (int)devRect.right);
        int right  = (std::max)((int)devRect.left, (int)devRect.right);
        int top    = (std::min)((int)devRect.top, (int)devRect.bottom);
        int bottom = (std::max)((int)devRect.top, (int)devRect.bottom);

        // Ensure minimum 4x4 px size so thin lines don't disappear
        if (right <= left) right = left + 4;
        if (bottom <= top) bottom = top + 4;

        AVDevRect drawRect;
        drawRect.left = (ASInt16)left;
        drawRect.right = (ASInt16)right;
        drawRect.top = (ASInt16)top;
        drawRect.bottom = (ASInt16)bottom;

        // Directly clicked target on primary page gets 3px border, other pages get 2px
        bool isHitTarget = (cand.pageIndex == g_State.hitPageIndex &&
                            std::fabs(ASFixedToFloat(cand.bbox.left) - ASFixedToFloat(g_State.hitBBox.left)) < 3.0f &&
                            std::fabs(ASFixedToFloat(cand.bbox.bottom) - ASFixedToFloat(g_State.hitBBox.bottom)) < 3.0f);

        AVPageViewDrawRectOutline(pageView, &drawRect, isHitTarget ? 3 : 2, NULL, 0);
    }
}

} // anonymous namespace

#include <string>
#include <cstdio>

namespace wipepdf {

// Diagnostics: append a line to %TEMP%\WipePDF_Plugin.log.
static void DiagLog(const char *fmt, ...) {
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    char tempPath[512];
    DWORD len = GetTempPathA(sizeof(tempPath), tempPath);
    if (len > 0) {
        char fullPath[600];
        snprintf(fullPath, sizeof(fullPath), "%sWipePDF_Plugin.log", tempPath);
        FILE *fp = NULL;
        if (fopen_s(&fp, fullPath, "a") == 0 && fp) {
            fprintf(fp, "[SelectionTool] %s\n", line);
            fclose(fp);
        }
    }
}

// Transform a point in local coordinates into parent/page coordinates using
// a PDF transformation matrix (column-vector convention, ASFixedMatrix).
static ASFixedPoint MatrixTransformPoint(const ASFixedMatrix *m, const ASFixedPoint *p) {
    ASFixedPoint out;
    ASFixedMatrixTransform(&out, (ASFixedMatrixP)m, (ASFixedPointP)p);
    return out;
}

// Transform a rectangle (bbox) into parent/page coordinates via a matrix.
static void MatrixTransformRect(const ASFixedMatrix *m, const ASFixedRect *r, ASFixedRect *out) {
    ASFixedMatrixTransformRect(out, (ASFixedMatrixP)m, (ASFixedRectP)r);
}

AVTool SelectionTool::gSelectionTool = NULL;
AVTool SelectionTool::gPreviousTool = NULL;

void SelectionTool::RegisterTool() {
    gSelectionTool = (AVTool)ASmalloc(sizeof(AVToolRec));
    memset(gSelectionTool, 0, sizeof(AVToolRec));

    gSelectionTool->size = sizeof(AVToolRec);
    gSelectionTool->cursorID = CROSSHAIR_CURSOR;
    gSelectionTool->ComputeEnabled = ASCallbackCreateProto(AVComputeEnabledProc, &SelectionTool::ComputeEnabledProc);
    gSelectionTool->Activate = ASCallbackCreateProto(ActivateProcType, &SelectionTool::ActivateProc);
    gSelectionTool->Deactivate = ASCallbackCreateProto(DeactivateProcType, &SelectionTool::DeactivateProc);
    gSelectionTool->GetType = ASCallbackCreateProto(GetTypeProcType, &SelectionTool::GetTypeProc);
    gSelectionTool->DoClick = ASCallbackCreateProto(DoClickProcType, &SelectionTool::DoClickProc);

    // A tool must be registered with the viewer before it can be activated.
    // After this call the structure is owned by Acrobat and must not be freed.
    AVAppRegisterTool(gSelectionTool);

    // Register drawing callback once globally so it is always active in the viewer render loop
    if (!g_DrawProc) {
        g_DrawProc = ASCallbackCreateProto(AVPageViewDrawProc, MyPageViewDrawProc);
        AVAppRegisterForPageViewDrawing(g_DrawProc, NULL);
    }
}

void SelectionTool::UnregisterTool() {
    if (g_DrawProc) {
        AVAppUnregisterForPageViewDrawingEx(g_DrawProc, NULL);
        g_DrawProc = NULL;
    }
    // The registered tool structure is owned by Acrobat; do not free it here.
    gSelectionTool = NULL;
}

void SelectionTool::ActivateTool(AVTool prevTool) {
    AVDoc avDoc = AVAppGetActiveDoc();
    if (!avDoc) return;

    // Prefer the document-level tool API (recommended over AVAppSetActiveTool).
    gPreviousTool = prevTool ? prevTool : AVDocGetActiveTool(avDoc);
    if (!gPreviousTool) gPreviousTool = AVAppGetDefaultTool();

    AVDocSetActiveTool(avDoc, gSelectionTool, false);
}

ASBool ACCB1 SelectionTool::ComputeEnabledProc(void *data) {
    return true; 
}

void ACCB1 SelectionTool::ActivateProc(AVTool tool, ASBool persistent) {
}

void ACCB1 SelectionTool::DeactivateProc(AVTool tool) {
    ClearHighlight();
}

ASAtom ACCB1 SelectionTool::GetTypeProc(AVTool tool) {
    return ASAtomFromString("WipePDF:SelectionTool");
}


ASBool ACCB1 SelectionTool::DoClickProc(AVTool tool, AVPageView pageView, ASInt16 x, ASInt16 y, ASInt16 flags, ASInt16 clickNo) {
    if (!pageView) return true;

    // Acrobat native marquee drag routine:
    // Tracks dragging with a live marquee rectangle while left mouse button is pressed.
    AVDevRect dragRect;
    memset(&dragRect, 0, sizeof(dragRect));
    AVPageViewDragOutNewRect(pageView, x, y, &dragRect);

    int dragW = std::abs((int)dragRect.right - (int)dragRect.left);
    int dragH = std::abs((int)dragRect.bottom - (int)dragRect.top);

    bool confirmed = false;
    if (dragW >= 5 && dragH >= 5) {
        // User dragged out a rubberband box -> Option D (Box Selection) & Option A (Whiteout Patch)
        confirmed = HandleBoxDrag(pageView, dragRect);
    } else {
        // User single-clicked at (x, y) -> Point-and-click hit testing
        confirmed = HandleClick(pageView, x, y);
    }

    // Restore the previously active tool after the pick-and-confirm flow.
    // This keeps the tool one-shot, matching the "点选同款删除" interaction.
    if (confirmed) {
        AVTool restore = gPreviousTool ? gPreviousTool : AVAppGetDefaultTool();
        AVDoc avDoc = AVPageViewGetAVDoc(pageView);
        if (avDoc && restore) {
            AVDocSetActiveTool(avDoc, restore, false);
        }
    }
    return true;
}

// Extract a fingerprint from a hit Form/Container. The container's own
// page-space bbox is authoritative for size/position (Acrobat computed it).
// Pixel/text attributes come from the first pickable child in its content.
bool SelectionTool::ExtractContainerFingerprint(PDEElement container, const ASFixedRect &pageBBox,
                                                TargetFingerprint &fp) {
    ASInt32 ctype = PDEObjectGetType((PDEObject)container);
    PDEContent inner = NULL;
    if (ctype == kPDEForm) {
        inner = PDEFormGetContent((PDEForm)container);
    } else if (ctype == kPDEContainer) {
        inner = PDEContainerGetContent((PDEContainer)container);
        // Mark the fingerprint so deletion uses the PDF-standard watermark
        // signal instead of geometry heuristics.
        if (WatermarkService::isWatermarkMarkedContainer(container)) {
            fp.isWatermarkMarked = true;
        }
    }
    if (!inner) return false;

    ASInt32 numElems = PDEContentGetNumElems(inner);
    for (ASInt32 i = numElems - 1; i >= 0; --i) {
        PDEElement elem = PDEContentGetElem(inner, i);
        if (!elem) continue;
        ASInt32 type = PDEObjectGetType((PDEObject)elem);

        // Recurse into nested containers/forms to find a pickable child.
        if (type == kPDEForm || type == kPDEContainer) {
            if (ExtractContainerFingerprint(elem, pageBBox, fp)) {
                return true;
            }
            continue;
        }

        if (type == kPDEImage) {
            fp.active = true;
            fp.type = kPDEImage;
            fp.width = ASFixedToFloat(pageBBox.right - pageBBox.left);
            fp.height = ASFixedToFloat(pageBBox.top - pageBBox.bottom);
            PDEImageAttrs attrs;
            memset(&attrs, 0, sizeof(attrs));
            PDEImageGetAttrs((PDEImage)elem, &attrs, sizeof(attrs));
            fp.pixelWidth = attrs.width;
            fp.pixelHeight = attrs.height;
            return true;
        } else if (type == kPDEPath) {
            fp.active = true;
            fp.type = kPDEPath;
            fp.bboxWidth = ASFixedToFloat(pageBBox.right - pageBBox.left);
            fp.bboxHeight = ASFixedToFloat(pageBBox.top - pageBBox.bottom);
            PDEGraphicState gState;
            memset(&gState, 0, sizeof(gState));
            PDEElementGetGState(elem, &gState, sizeof(gState));
            if (gState.fillColorSpec.space != NULL) {
                ASAtom spName = PDEColorSpaceGetName(gState.fillColorSpec.space);
                if (spName == ASAtomFromString("Pattern")) {
                    fp.isPattern = true;
                }
            }
            return true;
        } else if (type == kPDEText) {
            fp.active = true;
            fp.type = kPDEText;
            ASInt32 len = PDETextGetText((PDEText)elem, kPDETextRun, 0, NULL);
            if (len > 0) {
                std::string buf(len, '\0');
                PDETextGetText((PDEText)elem, kPDETextRun, 0, (ASUns8*)&buf[0]);
                fp.textContent = buf;
            }
            return true;
        }
    }
    return false;
}

bool SelectionTool::HitTestFormContent(PDEElement container, const ASFixedPoint &pagePt,
                                       const ASFixedMatrix *parentMatrix, TargetFingerprint &fp) {
    // Get the inner content depending on the container kind.
    ASInt32 ctype = PDEObjectGetType((PDEObject)container);
    PDEContent inner = NULL;
    if (ctype == kPDEForm) {
        inner = PDEFormGetContent((PDEForm)container);
    } else if (ctype == kPDEContainer) {
        inner = PDEContainerGetContent((PDEContainer)container);
    }
    if (!inner) return false;

    // Important: a marked-content Container (/Artifact BDC..EMC) does NOT
    // introduce a coordinate transform - its children live in the parent's
    // (page) coordinate space. A Form XObject DOES: child coordinates must be
    // transformed by the Form's matrix. `parentMatrix` here is the transform
    // from this container's content space into page space.
    ASInt32 numElems = PDEContentGetNumElems(inner);
    for (ASInt32 i = numElems - 1; i >= 0; --i) {
        PDEElement elem = PDEContentGetElem(inner, i);
        if (!elem) continue;

        ASInt32 type = PDEObjectGetType((PDEObject)elem);
        ASFixedRect bbox;
        PDEElementGetBBox(elem, &bbox);

        // Nested container/form: recurse with the correct accumulated matrix.
        if (type == kPDEForm || type == kPDEContainer) {
            ASFixedMatrix total;
            if (type == kPDEForm) {
                // Form child: its content coordinates are transformed by the
                // Form's own matrix first, then by the parent's matrix.
                // ASFixedMatrixConcat(result, m1, m2) computes m2 x m1, so to
                // get (parent x child) we pass (child, parent).
                ASFixedMatrix childMatrix;
                PDEElementGetMatrix(elem, &childMatrix);
                ASFixedMatrixConcat(&total, &childMatrix, parentMatrix);
            } else {
                // Container child: no coordinate change; pass parent matrix.
                total = *parentMatrix;
            }
            if (HitTestFormContent(elem, pagePt, &total, fp)) {
                return true;
            }
            continue;
        }

        // For a non-container child, its own matrix maps its user space
        // coordinates into the parent (container) content space. Combined
        // with parentMatrix (content -> page) we get the page transform.
        // PDEElementGetMatrix returns identity for text, and the real cm
        // matrix for images/paths; it is NOT valid for containers (handled
        // above) and we ignore it here.
        ASFixedMatrix elemMatrix;
        PDEElementGetMatrix(elem, &elemMatrix);
        ASFixedMatrix toPage;
        // ASFixedMatrixConcat(result, m1, m2) computes m2 x m1, so passing
        // (elemMatrix, parentMatrix) yields parentMatrix x elemMatrix, i.e.
        // element user space -> page space.
        ASFixedMatrixConcat(&toPage, &elemMatrix, parentMatrix);

        // Transform the child bbox into page coordinates.
        ASFixedRect pageBBox;
        MatrixTransformRect(&toPage, &bbox, &pageBBox);
        DiagLog("    form child[%d] type=%d local=(%.1f,%.1f)-(%.1f,%.1f) page=(%.1f,%.1f)-(%.1f,%.1f)",
                i, type,
                ASFixedToFloat(bbox.left), ASFixedToFloat(bbox.bottom),
                ASFixedToFloat(bbox.right), ASFixedToFloat(bbox.top),
                ASFixedToFloat(pageBBox.left), ASFixedToFloat(pageBBox.bottom),
                ASFixedToFloat(pageBBox.right), ASFixedToFloat(pageBBox.top));

        if (pagePt.h >= pageBBox.left && pagePt.h <= pageBBox.right &&
            pagePt.v >= pageBBox.bottom && pagePt.v <= pageBBox.top) {
            // Fill type-specific fingerprint fields (dimensions/pixels/text).
            TargetFingerprint tmp;
            tmp.active = true;
            // Record the full element->page matrix so position matching later
            // can transform other candidates into page space correctly.
            tmp.inForm = true;
            tmp.formMatrix = toPage;
            if (type == kPDEImage) {
                tmp.type = kPDEImage;
                tmp.width = ASFixedToFloat(pageBBox.right - pageBBox.left);
                tmp.height = ASFixedToFloat(pageBBox.top - pageBBox.bottom);
                PDEImageAttrs attrs;
                memset(&attrs, 0, sizeof(attrs));
                PDEImageGetAttrs((PDEImage)elem, &attrs, sizeof(attrs));
                tmp.pixelWidth = attrs.width;
                tmp.pixelHeight = attrs.height;
            } else if (type == kPDEPath) {
                tmp.type = kPDEPath;
                tmp.bboxWidth = ASFixedToFloat(pageBBox.right - pageBBox.left);
                tmp.bboxHeight = ASFixedToFloat(pageBBox.top - pageBBox.bottom);
                PDEGraphicState gState;
                memset(&gState, 0, sizeof(gState));
                PDEElementGetGState(elem, &gState, sizeof(gState));
                if (gState.fillColorSpec.space != NULL) {
                    ASAtom spName = PDEColorSpaceGetName(gState.fillColorSpec.space);
                    if (spName == ASAtomFromString("Pattern")) {
                        tmp.isPattern = true;
                    }
                }
            } else if (type == kPDEText) {
                tmp.type = kPDEText;
                ASInt32 len = PDETextGetText((PDEText)elem, kPDETextRun, 0, NULL);
                if (len > 0) {
                    std::string buf(len, '\0');
                    PDETextGetText((PDEText)elem, kPDETextRun, 0, (ASUns8*)&buf[0]);
                    tmp.textContent = buf;
                }
            } else {
                continue; // not a pickable type
            }
            fp = tmp;
            return true;
        }
    }
    return false;
}

bool SelectionTool::HandleBoxDrag(AVPageView pageView, const AVDevRect &dragRect) {
    if (!pageView) return false;
    PDPage page = AVPageViewGetPage(pageView);
    if (!page) return false;
    AVDoc avDoc = AVAppGetActiveDoc();
    PDDoc pdDoc = avDoc ? AVDocGetPDDoc(avDoc) : NULL;
    if (!pdDoc) return false;

    ASInt32 pageNum = AVPageViewGetPageNum(pageView);
    ASFixedRect cropBox;
    PDPageGetCropBox(page, &cropBox);

    float pageW = ASFixedToFloat(cropBox.right - cropBox.left);
    float pageH = ASFixedToFloat(cropBox.top - cropBox.bottom);
    if (pageW <= 0.0f || pageH <= 0.0f) return false;

    // Normalize device rect
    AVDevRect normDev;
    normDev.left = (std::min)(dragRect.left, dragRect.right);
    normDev.right = (std::max)(dragRect.left, dragRect.right);
    normDev.top = (std::min)(dragRect.top, dragRect.bottom);
    normDev.bottom = (std::max)(dragRect.top, dragRect.bottom);

    // Convert to page coordinates
    ASFixedRect pageRect;
    AVPageViewDeviceRectToPage(pageView, &normDev, &pageRect);
    if (pageRect.left > pageRect.right) std::swap(pageRect.left, pageRect.right);
    if (pageRect.bottom > pageRect.top) std::swap(pageRect.bottom, pageRect.top);

    float bw = ASFixedToFloat(pageRect.right - pageRect.left);
    float bh = ASFixedToFloat(pageRect.top - pageRect.bottom);
    float cx = ASFixedToFloat((pageRect.left + pageRect.right) / 2);
    float cy = ASFixedToFloat((pageRect.bottom + pageRect.top) / 2);

    float relX = (cx - ASFixedToFloat(cropBox.left)) / pageW;
    float relY = (cy - ASFixedToFloat(cropBox.bottom)) / pageH;
    float relW = bw / pageW;
    float relH = bh / pageH;

    DiagLog("HandleBoxDrag: dev=(%d,%d)-(%d,%d) page=(%.1f,%.1f)-(%.1f,%.1f) rel=(%.2f,%.2f)",
            normDev.left, normDev.top, normDev.right, normDev.bottom,
            ASFixedToFloat(pageRect.left), ASFixedToFloat(pageRect.bottom),
            ASFixedToFloat(pageRect.right), ASFixedToFloat(pageRect.top),
            relX, relY);

    // Set up highlight candidates for all pages so user can verify on screen
    g_State.candidates.clear();
    ASInt32 totalDocPages = PDDocGetNumPages(pdDoc);
    for (ASInt32 p = 0; p < totalDocPages; ++p) {
        wipepdf::WatermarkCandidate c;
        c.pageIndex = p;
        if (p == pageNum) {
            c.bbox = pageRect;
        } else {
            PDPage otherPage = PDDocAcquirePage(pdDoc, p);
            if (otherPage) {
                ASFixedRect otherCrop;
                PDPageGetCropBox(otherPage, &otherCrop);
                PDPageRelease(otherPage);
                float oW = ASFixedToFloat(otherCrop.right - otherCrop.left);
                float oH = ASFixedToFloat(otherCrop.top - otherCrop.bottom);
                c.bbox.left = otherCrop.left + FloatToASFixed((relX - relW / 2.0f) * oW);
                c.bbox.right = otherCrop.left + FloatToASFixed((relX + relW / 2.0f) * oW);
                c.bbox.bottom = otherCrop.bottom + FloatToASFixed((relY - relH / 2.0f) * oH);
                c.bbox.top = otherCrop.bottom + FloatToASFixed((relY + relH / 2.0f) * oH);
            } else {
                c.bbox = pageRect;
            }
        }
        c.selected = true;
        c.matchType = "WhiteoutBox";
        g_State.candidates.push_back(c);
    }

    g_State.active = true;
    g_State.pddoc = pdDoc;
    g_State.hitPageIndex = pageNum;
    g_State.hitBBox = pageRect;

    // Immediately draw red highlight outline
    AVPageViewInvalidateRect(pageView, NULL);
    AVPageViewDrawNow(pageView);

    // Build user prompt
    std::wstring msg = L"【已框选目标区域（无损纯白遮罩模式）】\n\n";
    msg += L"  - 框选尺寸：" + std::to_wstring((int)bw) + L" x " + std::to_wstring((int)bh) + L" pt\n";
    msg += L"  - 相对位置：水平 " + std::to_wstring((int)(relX * 100)) + L"%，垂直 " + std::to_wstring((int)(relY * 100)) + L"%\n\n";
    msg += L"💡 适用场景：消除底图融合水印（如边角网址、作者名、印章等）。\n";
    msg += L"此模式将在框选区域覆盖一层无损纯白矢量遮罩，彻底消除底图融合水印，\n";
    msg += L"原文档扫描底图和正文画质 100% 保持无损无衰减！\n\n";
    msg += L"请选择遮盖应用范围：\n";
    msg += L"【是 (Yes)】    —— 应用到【全篇文档所有页面】该位置（自动创建备份）\n";
    msg += L"【否 (No)】     —— 仅应用到【当前页面】该位置\n";
    msg += L"【取消 (Cancel)】 —— 误选，放弃修改退出\n";

    int choice = MessageBoxW(NULL, msg.c_str(), L"WipePDF 区域涂白遮盖确认", MB_YESNOCANCEL | MB_ICONQUESTION);

    if (choice == IDCANCEL) {
        ClearHighlight(pageView);
        return false;
    }

    bool applyAll = (choice == IDYES);
    wipepdf::WhiteoutResult result = wipepdf::WatermarkService::applyWhiteoutPatch(pdDoc, pageRect, pageNum, applyAll);

    // Clear highlights and force redraw
    ClearHighlight(pageView);
    AVPageViewInvalidateRect(pageView, NULL);
    AVPageViewDrawNow(pageView);

    if (result.success) {
        std::wstring fin = L"区域涂白遮盖完成！\n\n";
        fin += L"已在 " + std::to_wstring(result.pagesPatched) + L" 个页面成功覆盖无损纯白遮罩。\n";
        fin += L"水印已消除，页面底图画质 100% 保持无损无衰减。\n\n";
        if (!result.backupPath.empty()) {
            fin += L"⚠️ 安全备份提示：\n  - 操作前已自动备份原文档至：\n    " + result.backupPath + L"\n    如误遮可用该文件恢复。\n\n";
        }
        fin += L"⚠️ 重要提示：\n  - 修改会直接影响当前文档，如需保留请立即按 Ctrl+S 保存。";
        MessageBoxW(NULL, fin.c_str(), L"WipePDF 涂白遮盖成功", MB_OK | MB_ICONINFORMATION);
        return true;
    } else {
        MessageBoxW(NULL, L"涂白遮罩应用失败，请检查文档是否被加密或只读保护。", L"WipePDF 错误", MB_OK | MB_ICONERROR);
        return false;
    }
}

bool SelectionTool::HandleClick(AVPageView pageView, ASInt16 x, ASInt16 y) {
    PDPage page = AVPageViewGetPage(pageView);
    if (!page) return false;

    ASFixedPoint pagePt;
    AVPageViewDevicePointToPage(pageView, x, y, &pagePt);
    DiagLog("HandleClick: device(%d,%d) page=(%.2f,%.2f)", x, y,
            ASFixedToFloat(pagePt.h), ASFixedToFloat(pagePt.v));
    
    PDEContent content = PDPageAcquirePDEContent(page, gExtensionID);
    if (!content) {
        DiagLog("HandleClick: PDPageAcquirePDEContent failed");
        return false;
    }

    // Page geometry for normalizing element positions (avoid false matches
    // against full-page scanned backgrounds).
    ASFixedRect cropBox;
    PDPageGetCropBox(page, &cropBox);
    float pageW = ASFixedToFloat(cropBox.right - cropBox.left);
    float pageH = ASFixedToFloat(cropBox.top - cropBox.bottom);
    DiagLog("HandleClick: page=(%.2f x %.2f) numElems=%d", pageW, pageH,
            PDEContentGetNumElems(content));
    if (pageW <= 0.0f || pageH <= 0.0f) {
        PDPageReleasePDEContent(page, gExtensionID);
        return false;
    }

    ASInt32 numElems = PDEContentGetNumElems(content);
    PDEElement clickedElem = NULL;
    ASInt32 clickedType = -1;
    ASFixedRect hitPageBBox = {0, 0, 0, 0};
    
    TargetFingerprint fp;

    for (ASInt32 i = numElems - 1; i >= 0; --i) {
        PDEElement elem = PDEContentGetElem(content, i);
        if (!elem) continue;

        ASFixedRect bbox;
        PDEElementGetBBox(elem, &bbox);

        if (pagePt.h >= bbox.left && pagePt.h <= bbox.right &&
            pagePt.v >= bbox.bottom && pagePt.v <= bbox.top) {

            clickedElem = elem;
            clickedType = PDEObjectGetType((PDEObject)elem);
            hitPageBBox = bbox;
            DiagLog("  hit top elem[%d] type=%d bbox=(%.1f,%.1f)-(%.1f,%.1f)",
                    i, clickedType, ASFixedToFloat(bbox.left), ASFixedToFloat(bbox.bottom),
                    ASFixedToFloat(bbox.right), ASFixedToFloat(bbox.top));

            // A Form XObject or marked-content Container (e.g. /Artifact
            // /Subtype /Watermark block) is the watermark's own box: Acrobat
            // has already computed its page-space bbox. We treat the container
            // itself as the target and extract a fingerprint from its content
            // (pixel dimensions, element type), WITHOUT attempting recursive
            // matrix transforms which proved unreliable for nested content.
            if (clickedType == kPDEForm || clickedType == kPDEContainer) {
                TargetFingerprint nestedFp;
                if (ExtractContainerFingerprint((PDEElement)elem, bbox, nestedFp)) {
                    fp = nestedFp;
                    fp.inForm = true;
                    // Page-space position from the container's own bbox.
                    fp.relX = ASFixedToFloat((bbox.left + bbox.right) / 2) / pageW;
                    fp.relY = ASFixedToFloat((bbox.bottom + bbox.top) / 2) / pageH;
                    fp.relW = ASFixedToFloat(bbox.right - bbox.left) / pageW;
                    fp.relH = ASFixedToFloat(bbox.top - bbox.bottom) / pageH;
                    DiagLog("  Container hit OK: type=%d pix=(%dx%d) rel=(%.2f,%.2f) relsize=(%.2f,%.2f)",
                            fp.type, fp.pixelWidth, fp.pixelHeight, fp.relX, fp.relY, fp.relW, fp.relH);
                    break;
                }
                DiagLog("  Container no pickable content; continuing to lower elements");
                continue;
            }
            
            float bw = ASFixedToFloat(bbox.right - bbox.left);
            float bh = ASFixedToFloat(bbox.top - bbox.bottom);
            float cx = ASFixedToFloat((bbox.left + bbox.right) / 2);
            float cy = ASFixedToFloat((bbox.bottom + bbox.top) / 2);

            // Normalized position/size used as a secondary fingerprint.
            fp.relX = cx / pageW;
            fp.relY = cy / pageH;
            fp.relW = bw / pageW;
            fp.relH = bh / pageH;

            if (clickedType == kPDEImage) {
                fp.active = true;
                fp.type = kPDEImage;
                fp.width = bw;
                fp.height = bh;

                PDEImageAttrs attrs;
                memset(&attrs, 0, sizeof(attrs));
                PDEImageGetAttrs((PDEImage)elem, &attrs, sizeof(attrs));
                fp.pixelWidth = attrs.width;
                fp.pixelHeight = attrs.height;
                break;
            } else if (clickedType == kPDEPath) {
                fp.active = true;
                fp.type = kPDEPath;
                fp.bboxWidth = bw;
                fp.bboxHeight = bh;
                
                PDEGraphicState gState;
                memset(&gState, 0, sizeof(gState));
                PDEElementGetGState(elem, &gState, sizeof(gState));
                if (gState.fillColorSpec.space != NULL) {
                    ASAtom spName = PDEColorSpaceGetName(gState.fillColorSpec.space);
                    if (spName == ASAtomFromString("Pattern")) {
                        fp.isPattern = true;
                    }
                }
                break;
            } else if (clickedType == kPDEText) {
                fp.active = true;
                fp.type = kPDEText;
                
                ASInt32 len = PDETextGetText((PDEText)elem, kPDETextRun, 0, NULL);
                if (len > 0) {
                    std::string buf(len, '\0');
                    PDETextGetText((PDEText)elem, kPDETextRun, 0, (ASUns8*)&buf[0]);
                    fp.textContent = buf;
                }
                break;
            }
        }
    }
    
    PDPageReleasePDEContent(page, gExtensionID);

    DiagLog("HandleClick final: fp.active=%d type=%d", fp.active ? 1 : 0, fp.type);

    if (fp.active) {
        AVDoc avDoc = AVAppGetActiveDoc();
        PDDoc pdDoc = avDoc ? AVDocGetPDDoc(avDoc) : NULL;
        if (!pdDoc) return false;

        ASInt32 pageIndex = AVPageViewGetPageNum(pageView);

        CleanOptions opts;
        opts.targetFingerprint = fp;
        opts.removeTransparentText = false;
        opts.removePatternFills = false;
        opts.removeKeywordText = false;
        opts.removeLinks = false;
        opts.removeBottomStrip = false;

        // Scan document for all matching candidate elements
        std::vector<wipepdf::WatermarkCandidate> candidates = WatermarkService::scanDocument(pdDoc, opts);

        // Ensure the clicked element itself is in the candidates list
        bool hitIncluded = false;
        for (const auto& c : candidates) {
            if (c.pageIndex == pageIndex &&
                std::fabs(ASFixedToFloat(c.bbox.left) - ASFixedToFloat(hitPageBBox.left)) < 5.0f &&
                std::fabs(ASFixedToFloat(c.bbox.bottom) - ASFixedToFloat(hitPageBBox.bottom)) < 5.0f) {
                hitIncluded = true;
                break;
            }
        }
        if (!hitIncluded) {
            wipepdf::WatermarkCandidate cHit;
            cHit.pageIndex = pageIndex;
            cHit.bbox = hitPageBBox;
            cHit.selected = true;
            cHit.matchType = "DirectHit";
            cHit.elemType = clickedType;
            candidates.insert(candidates.begin(), cHit);
        }

        // Deduplicate overlapping candidate boxes on the same page (e.g. form container vs inner content)
        std::vector<wipepdf::WatermarkCandidate> uniqueCands;
        for (const auto& c : candidates) {
            bool isDup = false;
            for (const auto& u : uniqueCands) {
                if (c.pageIndex == u.pageIndex) {
                    float cx1 = ASFixedToFloat((c.bbox.left + c.bbox.right) / 2);
                    float cy1 = ASFixedToFloat((c.bbox.bottom + c.bbox.top) / 2);
                    float cx2 = ASFixedToFloat((u.bbox.left + u.bbox.right) / 2);
                    float cy2 = ASFixedToFloat((u.bbox.bottom + u.bbox.top) / 2);
                    if (std::fabs(cx1 - cx2) < 15.0f && std::fabs(cy1 - cy2) < 15.0f) {
                        isDup = true;
                        break;
                    }
                }
            }
            if (!isDup) uniqueCands.push_back(c);
        }
        candidates = uniqueCands;

        // Set highlight state and activate drawing proc
        g_State.active = true;
        g_State.pddoc = pdDoc;
        g_State.hitPageIndex = pageIndex;
        g_State.hitBBox = hitPageBBox;
        g_State.candidates = candidates;

        // 1. Invalidate Acrobat viewer and force immediate redraw
        AVPageViewInvalidateRect(pageView, NULL);
        AVPageViewDrawNow(pageView);

        // Build detailed information message
        std::wstring msg = L"【已在页面上用红色矩形框标出选中的水印】\n\n";
        if (fp.type == kPDEImage) {
            msg += L"  - 目标类型：图片\n  - 尺寸：" + std::to_wstring(fp.width) + L" x " + std::to_wstring(fp.height) + L" pt";
            if (fp.pixelWidth > 0 && fp.pixelHeight > 0)
                msg += L"（像素 " + std::to_wstring(fp.pixelWidth) + L" x " + std::to_wstring(fp.pixelHeight) + L"）";
            msg += L"\n";
        } else if (fp.type == kPDEPath) {
            msg += L"  - 目标类型：矢量图形\n  - 尺寸：" + std::to_wstring(fp.bboxWidth) + L" x " + std::to_wstring(fp.bboxHeight) + L"\n";
            if (fp.isPattern) msg += L"  - 填充：图案填充\n";
        } else if (fp.type == kPDEText) {
            std::wstring wcontent(fp.textContent.begin(), fp.textContent.end());
            msg += L"  - 目标类型：文本\n  - 内容：" + wcontent + L"\n";
        }

        msg += L"  - 占页位置：水平 " + std::to_wstring((int)(fp.relX * 100)) + L"%，垂直 " + std::to_wstring((int)(fp.relY * 100)) + L"%\n";
        msg += L"  - 占页宽度：" + std::to_wstring((int)(fp.relW * 100)) + L"%，占页高度：" + std::to_wstring((int)(fp.relH * 100)) + L"%\n";

        bool fullPageRisk = (fp.relW > 0.6f && fp.relH > 0.6f);
        if (fullPageRisk) {
            msg += L"\n⚠️ 注意：该元素是整页扫描底图/背景图片（底图融合水印）。\n"
                   L"若直接删除该底图会导致页面正文内容全部丢失！\n"
                   L"💡 推荐方案：请使用当前工具，按住鼠标左键【拖动拉框】框选水印区域进行纯白遮盖。\n";
        }

        msg += L"\n全篇文档共匹配到 " + std::to_wstring(candidates.size()) + L" 处同款水印元素。\n\n";
        msg += L"请直接核对页面上红框圈出的标记：\n";
        msg += L"【是】—— 确认清除所有红框标出的同款水印（自动创建备份）\n";
        msg += L"【否】—— 误选/取消，清除红框标记退出";

        int choice = MessageBoxW(NULL, msg.c_str(), L"WipePDF 水印清除确认", MB_YESNO | MB_ICONQUESTION);

        if (choice == IDYES) {
            if (fullPageRisk) {
                int again = MessageBoxW(NULL,
                    L"⚠️ 严重警告：您命中的是整页背景图/扫描底图！\n\n"
                    L"该水印已与底图完全融合为一体，直接删除底图会导致页面正文全部丢失！\n\n"
                    L"💡 推荐操作：\n"
                    L"点击【否】返回，然后用鼠标【按住左键拖动拉框】直接框选水印文字区域，\n"
                    L"系统会自动进行【全文档无损纯白遮盖】（绝对不伤害正文底图）！\n\n"
                    L"您是否仍要强制删除整个底图？",
                    L"WipePDF 底图融合水印提示", MB_YESNO | MB_ICONWARNING);
                if (again != IDYES) {
                    ClearHighlight(pageView);
                    return false;
                }
            }

            // Safety backup
            std::wstring backupPath;
            wchar_t tempDir[MAX_PATH] = {0};
            if (GetTempPathW(MAX_PATH, tempDir) > 0) {
                backupPath = std::wstring(tempDir) + L"WipePDF_backup_" + std::to_wstring(GetTickCount64()) + L".pdf";
                ASText diText = ASTextFromUnicode((const ASUTF16Val *)backupPath.c_str(), kUTF16HostEndian);
                if (diText) {
                    ASPathName backupPathName = ASFileSysCreatePathFromDIPathText(ASGetDefaultFileSys(), diText, NULL);
                    ASTextDestroy(diText);
                    if (backupPathName) {
                        PDDocSave(pdDoc, (PDSaveFull | PDSaveCopy), backupPathName, ASGetDefaultFileSys(), NULL, NULL);
                        ASFileSysReleasePath(ASGetDefaultFileSys(), backupPathName);
                    } else {
                        backupPath.clear();
                    }
                } else {
                    backupPath.clear();
                }
            }

            // Execute removal plan
            wipepdf::CleanResult cleanRes = wipepdf::WatermarkService::executePlan(pdDoc, g_State.candidates, opts);

            // Clear highlights and force refresh
            ClearHighlight(pageView);

            std::wstring finishMsg = L"水印清理完成！\n\n共成功清除 " + std::to_wstring(cleanRes.totalRemoved) + L" 处同款水印元素。\n页面已即时刷新。\n\n⚠️ 重要提示：\n";
            if (!backupPath.empty()) {
                finishMsg += L"  - 操作前已自动备份原文档到：\n    " + backupPath + L"\n    如误删可用该文件恢复。\n";
            }
            finishMsg += L"  - 删除会直接修改当前文档，Acrobat 可能在关闭时自动保存覆盖原文件。\n    如需保留，请立即按 Ctrl+S 另存，或先另存一份副本。";
            MessageBoxW(NULL, finishMsg.c_str(), L"WipePDF 水印清理成功", MB_OK | MB_ICONINFORMATION);
            return true;
        } else {
            // Cancelled: clear highlight and restore clean page
            ClearHighlight(pageView);
            return false;
        }
    } else {
        MessageBoxW(NULL, L"未找到有效的水印元素。\n请确认点击的 PDF 元素存在且未被删除。", L"WipePDF 提示", MB_OK | MB_ICONWARNING);
    }
    return false;
}

} // namespace wipepdf
