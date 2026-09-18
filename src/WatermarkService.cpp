#include "WatermarkService.h"
#include <algorithm>

namespace wipepdf {

int WatermarkService::cleanActiveDocument(const CleanOptions &opts) {
    AVDoc avDoc = AVAppGetActiveDoc();
    if (!avDoc) return 0;
    PDDoc pdDoc = AVDocGetPDDoc(avDoc);
    if (!pdDoc) return 0;

    return cleanDocument(pdDoc, opts);
}

int WatermarkService::cleanDocument(PDDoc pddoc, const CleanOptions &opts) {
    if (!pddoc) return 0;
    int totalRemoved = 0;
    ASInt32 numPages = PDDocGetNumPages(pddoc);

    for (ASInt32 p = 0; p < numPages; ++p) {
        PDPage page = PDDocAcquirePage(pddoc, p);
        if (!page) continue;

        ASFixedRect cropBox;
        PDPageGetCropBox(page, &cropBox);

        // 1. Remove Link Annotations
        if (opts.removeLinks) {
            ASInt32 numAnnots = PDPageGetNumAnnots(page);
            for (ASInt32 a = numAnnots - 1; a >= 0; --a) {
                PDAnnot annot = PDPageGetAnnot(page, a);
                if (PDAnnotIsValid(annot)) {
                    ASAtom subtype = PDAnnotGetSubtype(annot);
                    if (subtype == ASAtomFromString("Link")) {
                        PDPageRemoveAnnot(page, a);
                        totalRemoved++;
                    }
                }
            }
        }

        // 2. Clean PDE Elements
        PDEContent content = PDPageAcquirePDEContent(page, gExtensionID);
        if (content) {
            int removed = cleanPageContent(page, content, opts, cropBox);
            if (removed > 0) {
                PDPageSetPDEContent(page, gExtensionID);
                PDPageNotifyContentsDidChange(page);
                totalRemoved += removed;
            }
            PDPageReleasePDEContent(page, gExtensionID);
        }

        PDPageRelease(page);
    }

    return totalRemoved;
}

int WatermarkService::cleanPageContent(PDPage page, PDEContent content, const CleanOptions &opts, const ASFixedRect &cropBox) {
    int removedCount = 0;
    ASInt32 numElems = PDEContentGetNumElems(content);

    for (ASInt32 i = numElems - 1; i >= 0; --i) {
        PDEElement elem = PDEContentGetElem(content, i);
        if (!elem) continue;

        ASInt32 type = PDEObjectGetType((PDEObject)elem);

        if (type == kPDEText) {
            std::string matched;
            if (isWatermarkText((PDEText)elem, opts, cropBox, matched)) {
                PDEContentRemoveElem(content, i);
                removedCount++;
            }
        } else if (type == kPDEPath) {
            if (isWatermarkPath((PDEPath)elem, opts, cropBox)) {
                PDEContentRemoveElem(content, i);
                removedCount++;
            }
        } else if (type == kPDEImage) {
            if (isWatermarkImage((PDEImage)elem, opts, cropBox)) {
                PDEContentRemoveElem(content, i);
                removedCount++;
            }
        } else if (type == kPDEForm) {
            removedCount += cleanForm((PDEForm)elem, opts, cropBox);
        }
        // Skip kPDEContainer, kPDEGroup, kPDEPlace, kPDEShading, kPDEUnknown, etc.
    }

    return removedCount;
}

int WatermarkService::cleanForm(PDEForm form, const CleanOptions &opts, const ASFixedRect &cropBox) {
    PDEContent formContent = PDEFormGetContent(form);
    if (!formContent) return 0;

    int removed = 0;
    ASInt32 numElems = PDEContentGetNumElems(formContent);
    for (ASInt32 i = numElems - 1; i >= 0; --i) {
        PDEElement elem = PDEContentGetElem(formContent, i);
        if (!elem) continue;

        ASInt32 type = PDEObjectGetType((PDEObject)elem);
        if (type == kPDEText) {
            std::string matched;
            if (isWatermarkText((PDEText)elem, opts, cropBox, matched)) {
                PDEContentRemoveElem(formContent, i);
                removed++;
            }
        } else if (type == kPDEPath) {
            if (isWatermarkPath((PDEPath)elem, opts, cropBox)) {
                PDEContentRemoveElem(formContent, i);
                removed++;
            }
        } else if (type == kPDEImage) {
            if (isWatermarkImage((PDEImage)elem, opts, cropBox)) {
                PDEContentRemoveElem(formContent, i);
                removed++;
            }
        }
    }

    if (removed > 0) {
        PDEFormSetContent(form, formContent);
    }
    return removed;
}

// ======================================================================
// KEY FIX: PDEText does NOT support PDEElementGetGState().
// Must use PDETextGetGState(text, kPDETextRun, runIndex, ...) per run.
// ======================================================================
bool WatermarkService::isWatermarkText(PDEText text, const CleanOptions &opts, const ASFixedRect &cropBox, std::string &outMatchedKeyword) {
    ASInt32 numRuns = PDETextGetNumRuns(text);
    if (numRuns <= 0) return false;

    ASFixedRect bbox;
    PDEElementGetBBox((PDEElement)text, &bbox);
    float bBottom = ASFixedToFloat(bbox.bottom);
    float pageBottom = ASFixedToFloat(cropBox.bottom);

    for (ASInt32 r = 0; r < numRuns; ++r) {
        // 1. Check graphic state per-run (transparency)
        if (opts.removeTransparentText) {
            PDEGraphicState gState;
            memset(&gState, 0, sizeof(gState));
            PDETextGetGState(text, kPDETextRun, r, &gState, sizeof(gState));

            if (gState.extGState != NULL) {
                ASFixed opFill = PDEExtGStateGetOpacityFill(gState.extGState);
                if (opFill == fixedZero || ASFixedToFloat(opFill) < 0.01f) {
                    outMatchedKeyword = "Opacity=0";
                    return true;
                }
            }
        }

        // 2. Check text state (render mode)
        PDETextState tState;
        memset(&tState, 0, sizeof(tState));
        PDETextGetTextState(text, kPDETextRun, r, &tState, sizeof(tState));

        if (opts.removeTransparentText && tState.renderMode == 3) {
            outMatchedKeyword = "RenderMode=3";
            return true;
        }

        // 3. Keyword matching
        if (opts.removeKeywordText) {
            ASInt32 len = PDETextGetText(text, kPDETextRun, r, NULL);
            if (len > 0) {
                std::vector<ASUns8> buf(len + 1, 0);
                PDETextGetText(text, kPDETextRun, r, buf.data());
                std::string s((char*)buf.data(), len);

                for (const auto &kw : opts.customKeywords) {
                    if (s.find(kw) != std::string::npos) {
                        outMatchedKeyword = kw;
                        return true;
                    }
                }

                // Bottom strip CamScanner text
                if (opts.removeBottomStrip && bBottom < pageBottom + 60.0f) {
                    if (s.find("\xC8\xAB\xC4\xDC\xCD\xF5") != std::string::npos ||  // 全能王 GBK
                        s.find("\xC9\xA8\xC3\xE8") != std::string::npos) {           // 扫描 GBK
                        outMatchedKeyword = "扫描全能王页脚文本";
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

bool WatermarkService::isWatermarkPath(PDEPath path, const CleanOptions &opts, const ASFixedRect &cropBox) {
    // PDEPath supports PDEElementGetGState
    PDEGraphicState gState;
    memset(&gState, 0, sizeof(gState));
    PDEElementGetGState((PDEElement)path, &gState, sizeof(gState));

    if (opts.removePatternFills && gState.fillColorSpec.space != NULL) {
        ASAtom spName = PDEColorSpaceGetName(gState.fillColorSpec.space);
        if (spName == ASAtomFromString("Pattern")) {
            return true;
        }
    }

    ASFixedRect bbox;
    PDEElementGetBBox((PDEElement)path, &bbox);

    float pw = ASFixedToFloat(cropBox.right - cropBox.left);
    float ph = ASFixedToFloat(cropBox.top - cropBox.bottom);
    float bw = ASFixedToFloat(bbox.right - bbox.left);
    float bh = ASFixedToFloat(bbox.top - bbox.bottom);
    float bBottom = ASFixedToFloat(bbox.bottom);
    float pageBottom = ASFixedToFloat(cropBox.bottom);

    // Bottom strip / divider line
    if (opts.removeBottomStrip && pw > 50.0f && ph > 50.0f) {
        if (bBottom < pageBottom + 65.0f && bh < 65.0f && bw > pw * 0.60f) {
            return true;
        }
    }

    // Large semi-transparent background wash
    if (opts.removePatternFills && gState.extGState != NULL && pw > 50.0f && ph > 50.0f) {
        float op = ASFixedToFloat(PDEExtGStateGetOpacityFill(gState.extGState));
        if (op > 0.001f && op < 0.35f && bw > pw * 0.40f && bh > ph * 0.25f) {
            return true;
        }
    }

    return false;
}

bool WatermarkService::isWatermarkImage(PDEImage img, const CleanOptions &opts, const ASFixedRect &cropBox) {
    if (!opts.removeBottomStrip) return false;

    ASFixedRect bbox;
    PDEElementGetBBox((PDEElement)img, &bbox);

    float bw = ASFixedToFloat(bbox.right - bbox.left);
    float bh = ASFixedToFloat(bbox.top - bbox.bottom);
    float bBottom = ASFixedToFloat(bbox.bottom);
    float pageBottom = ASFixedToFloat(cropBox.bottom);

    // CamScanner QR code: small square image at the very bottom
    if (bBottom < pageBottom + 65.0f && bw < 70.0f && bh < 70.0f && bw > 10.0f && bh > 10.0f) {
        return true;
    }

    return false;
}

PageInspectResult WatermarkService::inspectPage(PDDoc pddoc, ASInt32 pageIndex, const CleanOptions &opts) {
    PageInspectResult res;
    if (!pddoc) return res;

    PDPage page = PDDocAcquirePage(pddoc, pageIndex);
    if (!page) return res;

    ASFixedRect cropBox;
    PDPageGetCropBox(page, &cropBox);

    res.linkAnnotations = PDPageGetNumAnnots(page);

    PDEContent content = PDPageAcquirePDEContent(page, gExtensionID);
    if (content) {
        res.totalElements = PDEContentGetNumElems(content);
        for (ASInt32 i = 0; i < res.totalElements; ++i) {
            PDEElement elem = PDEContentGetElem(content, i);
            if (!elem) continue;

            ASInt32 type = PDEObjectGetType((PDEObject)elem);
            if (type == kPDEText) {
                res.textElements++;
                std::string matched;
                if (isWatermarkText((PDEText)elem, opts, cropBox, matched)) {
                    if (matched.find("RenderMode=3") != std::string::npos || matched.find("Opacity=0") != std::string::npos) {
                        res.transparentTextCount++;
                    } else {
                        res.keywordWatermarkCount++;
                        if (std::find(res.detectedKeywords.begin(), res.detectedKeywords.end(), matched) == res.detectedKeywords.end()) {
                            res.detectedKeywords.push_back(matched);
                        }
                    }
                }
            } else if (type == kPDEPath) {
                res.pathElements++;
                if (isWatermarkPath((PDEPath)elem, opts, cropBox)) {
                    res.patternFillCount++;
                }
            } else if (type == kPDEImage) {
                res.imageElements++;
            }
        }
        PDPageReleasePDEContent(page, gExtensionID);
    }

    PDPageRelease(page);
    return res;
}

} // namespace wipepdf
