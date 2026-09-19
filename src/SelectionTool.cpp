#include "SelectionTool.h"
#include "WatermarkService.h"
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
    gSelectionTool->ComputeEnabled = ASCallbackCreateProto(AVComputeEnabledProc, &SelectionTool::ComputeEnabledProc);
    gSelectionTool->Activate = ASCallbackCreateProto(ActivateProcType, &SelectionTool::ActivateProc);
    gSelectionTool->Deactivate = ASCallbackCreateProto(DeactivateProcType, &SelectionTool::DeactivateProc);
    gSelectionTool->GetType = ASCallbackCreateProto(GetTypeProcType, &SelectionTool::GetTypeProc);
    gSelectionTool->DoClick = ASCallbackCreateProto(DoClickProcType, &SelectionTool::DoClickProc);

    // A tool must be registered with the viewer before it can be activated.
    // After this call the structure is owned by Acrobat and must not be freed.
    AVAppRegisterTool(gSelectionTool);
}

void SelectionTool::UnregisterTool() {
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
}

ASAtom ACCB1 SelectionTool::GetTypeProc(AVTool tool) {
    return ASAtomFromString("WipePDF:SelectionTool");
}


ASBool ACCB1 SelectionTool::DoClickProc(AVTool tool, AVPageView pageView, ASInt16 x, ASInt16 y, ASInt16 flags, ASInt16 clickNo) {
    // Only handle a real click (not a double-click sequence restart).
    bool confirmed = HandleClick(pageView, x, y);

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
            DiagLog("  hit top elem[%d] type=%d bbox=(%.1f,%.1f)-(%.1f,%.1f)",
                    i, clickedType, ASFixedToFloat(bbox.left), ASFixedToFloat(bbox.bottom),
                    ASFixedToFloat(bbox.right), ASFixedToFloat(bbox.top));

            // If the clicked element is a Form XObject or a marked-content
            // Container (e.g. /Artifact /Subtype /Watermark block), the
            // watermark often lives *inside* it. Recurse into it, transforming
            // child element bboxes into page coordinates via its matrix so we
            // pick the actual watermark, not the full-page background behind it.
            if (clickedType == kPDEForm || clickedType == kPDEContainer) {
                ASFixedMatrix pageMatrix; // identity: container content == page space
                if (clickedType == kPDEForm) {
                    PDEElementGetMatrix(elem, &pageMatrix);
                } else {
                    pageMatrix.a = fixedOne; pageMatrix.b = fixedZero;
                    pageMatrix.c = fixedZero; pageMatrix.d = fixedOne;
                    pageMatrix.h = fixedZero; pageMatrix.v = fixedZero;
                }
                DiagLog("  Container/Form elem: type=%d matrix=(%.3f,%.3f,%.3f,%.3f,%.1f,%.1f)",
                        clickedType,
                        ASFixedToFloat(pageMatrix.a), ASFixedToFloat(pageMatrix.b),
                        ASFixedToFloat(pageMatrix.c), ASFixedToFloat(pageMatrix.d),
                        ASFixedToFloat(pageMatrix.h), ASFixedToFloat(pageMatrix.v));
                TargetFingerprint nestedFp;
                if (HitTestFormContent(elem, pagePt, &pageMatrix, nestedFp)) {
                    fp = nestedFp; // keeps nestedFp.formMatrix = element->page matrix
                    fp.inForm = true;
                    // Normalized page position: the container's page-space bbox
                    // (for a /Artifact watermark container, this is exactly the
                    // watermark's area on the page).
                    ASFixedRect pageBBox;
                    MatrixTransformRect(&pageMatrix, &bbox, &pageBBox);
                    float pbw = ASFixedToFloat(pageBBox.right - pageBBox.left);
                    float pbh = ASFixedToFloat(pageBBox.top - pageBBox.bottom);
                    float pcx = ASFixedToFloat((pageBBox.left + pageBBox.right) / 2);
                    float pcy = ASFixedToFloat((pageBBox.bottom + pageBBox.top) / 2);
                    fp.relX = pcx / pageW;
                    fp.relY = pcy / pageH;
                    fp.relW = pbw / pageW;
                    fp.relH = pbh / pageH;
                    DiagLog("  Container hit OK: type=%d pix=(%dx%d) rel=(%.2f,%.2f) relsize=(%.2f,%.2f)",
                            fp.type, fp.pixelWidth, fp.pixelHeight, fp.relX, fp.relY, fp.relW, fp.relH);
                    break;
                }
                DiagLog("  Container miss inside; continuing to lower elements");
                // Fall through: the container itself may be the target (rare);
                // keep searching lower elements rather than treating it as a hit.
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
        std::wstring msg = L"找到目标元素特征：\n";
        if (fp.type == kPDEImage) {
            msg += L"  - 类型：图片\n  - 尺寸：" + std::to_wstring(fp.width) + L" x " + std::to_wstring(fp.height) + L" pt";
            if (fp.pixelWidth > 0 && fp.pixelHeight > 0)
                msg += L"（像素 " + std::to_wstring(fp.pixelWidth) + L" x " + std::to_wstring(fp.pixelHeight) + L"）";
            msg += L"\n";
        } else if (fp.type == kPDEPath) {
            msg += L"  - 类型：矢量图形\n  - 尺寸：" + std::to_wstring(fp.bboxWidth) + L" x " + std::to_wstring(fp.bboxHeight) + L"\n";
            if (fp.isPattern) msg += L"  - 填充：图案填充\n";
        } else if (fp.type == kPDEText) {
            std::wstring wcontent(fp.textContent.begin(), fp.textContent.end());
            msg += L"  - 类型：文本\n  - 内容：" + wcontent + L"\n";
        }

        // 位置信息：显示命中元素占页面比例，帮助用户识别是否误点了整页背景图。
        msg += L"  - 位置：水平 " + std::to_wstring((int)(fp.relX * 100)) + L"%，垂直 " + std::to_wstring((int)(fp.relY * 100)) + L"%\n";
        msg += L"  - 占页宽度：" + std::to_wstring((int)(fp.relW * 100)) + L"%，占页高度：" + std::to_wstring((int)(fp.relH * 100)) + L"%\n";

        // 整页图保护：命中的元素几乎占满整页，极可能是扫描版背景图而非水印。
        bool fullPageRisk = (fp.relW > 0.6f && fp.relH > 0.6f);
        if (fullPageRisk) {
            msg += L"\n⚠️ 注意：该元素几乎占满整页（很可能是扫描版页面背景图）。\n删除它会把正文背景一起删掉，建议仅当确认这是整页水印时继续。\n";
        }

        // 先统计会删除多少个元素，让用户明确后果。
        CleanOptions opts;
        opts.targetFingerprint = fp;
        opts.removeTransparentText = false;
        opts.removePatternFills = false;
        opts.removeKeywordText = false;
        opts.removeLinks = false;
        opts.removeBottomStrip = false;

        CleanResult count = WatermarkService::countDocument(AVDocGetPDDoc(AVAppGetActiveDoc()), opts);
        msg += L"\n将在全篇文档中匹配并删除 " + std::to_wstring(count.totalRemoved) + L" 处同款元素（覆盖全部页面）。\n\n是否继续删除？";

        bool confirmed = (MessageBoxW(NULL, msg.c_str(), L"确认删除同款水印", MB_YESNO | MB_ICONQUESTION) == IDYES);

        // Full-page elements are almost certainly scanned page backgrounds, not
        // watermarks. Require a second, explicit confirmation before touching
        // the document to make accidental mass-deletion much harder.
        if (confirmed && fullPageRisk) {
            int again = MessageBoxW(NULL,
                L"⚠️ 严重警告：您命中的是几乎占满整页的元素（很可能是页面背景图/扫描图），\n"
                L"删除它会把所有页面的正文背景一起删掉！\n\n"
                L"如确知这就是要删除的整页水印，请点击【是】继续，否则点击【否】取消。",
                L"WipePDF 高危操作确认", MB_YESNO | MB_ICONWARNING);
            if (again != IDYES) confirmed = false;
        }

        if (confirmed) {
            // --- Safety backup: save a full copy of the document before
            //     modifying it, so an accidental deletion can be recovered
            //     even if Acrobat auto-saves the changes over the original.
            std::wstring backupPath;
            AVDoc avDoc = AVAppGetActiveDoc();
            PDDoc pdDoc = avDoc ? AVDocGetPDDoc(avDoc) : NULL;
            if (pdDoc) {
                wchar_t tempDir[MAX_PATH] = {0};
                if (GetTempPathW(MAX_PATH, tempDir) > 0) {
                    backupPath = std::wstring(tempDir) + L"WipePDF_backup_" + std::to_wstring(GetTickCount64()) + L".pdf";
                    ASText diText = ASTextFromUnicode((const ASUTF16Val *)backupPath.c_str(), kUTF16HostEndian);
                    if (diText) {
                        ASPathName backupPathName = ASFileSysCreatePathFromDIPathText(ASGetDefaultFileSys(), diText, NULL);
                        ASTextDestroy(diText);
                        if (backupPathName) {
                            PDDocSave(pdDoc, PDSaveFull, backupPathName, ASGetDefaultFileSys(), NULL, NULL);
                            ASFileSysReleasePath(ASGetDefaultFileSys(), backupPathName);
                        } else {
                            backupPath.clear();
                        }
                    } else {
                        backupPath.clear();
                    }
                } else {
                    backupPath.clear();
                }
            }

            CleanResult cleanRes = WatermarkService::cleanActiveDocument(opts);
            
            std::wstring finishMsg = L"水印清理完成！\n\n共成功清除 " + std::to_wstring(cleanRes.totalRemoved) + L" 处同款水印元素。\n页面已即时刷新。\n\n⚠️ 重要提示：\n";
            if (!backupPath.empty()) {
                finishMsg += L"  - 操作前已自动备份原文档到：\n    " + backupPath + L"\n    如误删可用该文件恢复。\n";
            }
            finishMsg += L"  - 删除会直接修改当前文档，Acrobat 可能在关闭时自动保存覆盖原文件。\n    如需保留，请立即按 Ctrl+S 另存，或先另存一份副本。";
            MessageBoxW(NULL, finishMsg.c_str(), L"WipePDF 水印清理成功", MB_OK | MB_ICONINFORMATION);
            return true;
        }
    } else {
        MessageBoxW(NULL, L"未找到有效的水印元素。\n请确认点击的 PDF 元素存在且未被删除。", L"WipePDF 提示", MB_OK | MB_ICONWARNING);
    }
    return false;
}

} // namespace wipepdf
