#include "WatermarkService.h"
#include <algorithm>
#include <cmath>

namespace wipepdf {

static bool float_eq(float a, float b) {
    return std::fabs(a - b) < 2.0f; // 2 pt tolerance
}

CleanResult WatermarkService::cleanActiveDocument(const CleanOptions &opts) {
    AVDoc avDoc = AVAppGetActiveDoc();
    if (!avDoc) return CleanResult();
    PDDoc pdDoc = AVDocGetPDDoc(avDoc);
    if (!pdDoc) return CleanResult();

    return cleanDocument(pdDoc, opts);
}

CleanResult WatermarkService::cleanDocument(PDDoc pddoc, const CleanOptions &opts) {
    CleanResult totalRes;
    if (!pddoc) return totalRes;
    
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
                        totalRes.removedLinks++;
                        totalRes.totalRemoved++;
                    }
                }
            }
        }

        // 2. Clean PDE Elements
        PDEContent content = PDPageAcquirePDEContent(page, gExtensionID);
        if (content) {
            CleanResult res = cleanPageContent(page, content, opts, cropBox);
            if (res.totalRemoved > 0) {
                PDPageSetPDEContent(page, gExtensionID);
                PDPageNotifyContentsDidChange(page);
                totalRes.add(res);
            }
            PDPageReleasePDEContent(page, gExtensionID);
        }

        PDPageRelease(page);
    }

    return totalRes;
}

CleanResult WatermarkService::cleanPageContent(PDPage page, PDEContent content, const CleanOptions &opts, const ASFixedRect &cropBox) {
    CleanResult res;
    ASInt32 numElems = PDEContentGetNumElems(content);

    for (ASInt32 i = numElems - 1; i >= 0; --i) {
        PDEElement elem = PDEContentGetElem(content, i);
        if (!elem) continue;

        ASInt32 type = PDEObjectGetType((PDEObject)elem);

        if (type == kPDEText) {
            std::string matchType, kw;
            if (isWatermarkText((PDEText)elem, opts, cropBox, matchType, kw)) {
                PDEContentRemoveElem(content, i);
                res.totalRemoved++;
                if (matchType == "Target") res.removedTargetFingers++;
                else if (matchType == "Transparent") res.removedTransparentText++;
                else if (matchType == "Keyword") res.removedKeywordText++;
            }
        } else if (type == kPDEPath) {
            std::string matchType;
            if (isWatermarkPath((PDEPath)elem, opts, cropBox, matchType)) {
                PDEContentRemoveElem(content, i);
                res.totalRemoved++;
                if (matchType == "Target") res.removedTargetFingers++;
                else if (matchType == "Pattern") res.removedPatternPaths++;
            }
        } else if (type == kPDEImage) {
            std::string matchType;
            if (isWatermarkImage((PDEImage)elem, opts, cropBox, matchType)) {
                PDEContentRemoveElem(content, i);
                res.totalRemoved++;
                if (matchType == "Target") res.removedTargetFingers++;
                else res.removedImages++;
            }
        } else if (type == kPDEForm) {
            CleanResult formRes = cleanForm((PDEForm)elem, opts, cropBox);
            res.add(formRes);
        }
    }

    return res;
}

CleanResult WatermarkService::cleanForm(PDEForm form, const CleanOptions &opts, const ASFixedRect &cropBox) {
    CleanResult res;
    PDEContent formContent = PDEFormGetContent(form);
    if (!formContent) return res;

    ASInt32 numElems = PDEContentGetNumElems(formContent);
    for (ASInt32 i = numElems - 1; i >= 0; --i) {
        PDEElement elem = PDEContentGetElem(formContent, i);
        if (!elem) continue;

        ASInt32 type = PDEObjectGetType((PDEObject)elem);
        if (type == kPDEText) {
            std::string matchType, kw;
            if (isWatermarkText((PDEText)elem, opts, cropBox, matchType, kw)) {
                PDEContentRemoveElem(formContent, i);
                res.totalRemoved++;
                if (matchType == "Target") res.removedTargetFingers++;
                else if (matchType == "Transparent") res.removedTransparentText++;
                else if (matchType == "Keyword") res.removedKeywordText++;
            }
        } else if (type == kPDEPath) {
            std::string matchType;
            if (isWatermarkPath((PDEPath)elem, opts, cropBox, matchType)) {
                PDEContentRemoveElem(formContent, i);
                res.totalRemoved++;
                if (matchType == "Target") res.removedTargetFingers++;
                else if (matchType == "Pattern") res.removedPatternPaths++;
            }
        } else if (type == kPDEImage) {
            std::string matchType;
            if (isWatermarkImage((PDEImage)elem, opts, cropBox, matchType)) {
                PDEContentRemoveElem(formContent, i);
                res.totalRemoved++;
                if (matchType == "Target") res.removedTargetFingers++;
                else res.removedImages++;
            }
        }
    }

    if (res.totalRemoved > 0) {
        PDEFormSetContent(form, formContent);
    }
    return res;
}

bool WatermarkService::isWatermarkText(PDEText text, const CleanOptions &opts, const ASFixedRect &cropBox, std::string &outMatchedType, std::string &outMatchedKeyword) {
    ASInt32 numRuns = PDETextGetNumRuns(text);
    if (numRuns <= 0) return false;
    
    ASFixedRect bbox;
    PDEElementGetBBox((PDEElement)text, &bbox);
    float bBottom = ASFixedToFloat(bbox.bottom);
    float pageBottom = ASFixedToFloat(cropBox.bottom);
    
    // Check fingerprint
    if (opts.targetFingerprint.active && opts.targetFingerprint.type == kPDEText) {
        for (ASInt32 r = 0; r < numRuns; ++r) {
            ASInt32 len = PDETextGetText(text, kPDETextRun, r, NULL);
            if (len > 0) {
                std::vector<ASUns8> buf(len + 1, 0);
                PDETextGetText(text, kPDETextRun, r, buf.data());
                std::string s((char*)buf.data(), len);
                if (s == opts.targetFingerprint.textContent) {
                    outMatchedType = "Target";
                    return true;
                }
            }
        }
    }
    
    // Check built-in rules
    for (ASInt32 r = 0; r < numRuns; ++r) {
        if (opts.removeTransparentText) {
            PDEGraphicState gState;
            memset(&gState, 0, sizeof(gState));
            PDETextGetGState(text, kPDETextRun, r, &gState, sizeof(gState));

            if (gState.extGState != NULL) {
                ASFixed opFill = PDEExtGStateGetOpacityFill(gState.extGState);
                if (opFill == fixedZero || ASFixedToFloat(opFill) < 0.01f) {
                    outMatchedType = "Transparent";
                    outMatchedKeyword = "Opacity=0";
                    return true;
                }
            }
        }

        PDETextState tState;
        memset(&tState, 0, sizeof(tState));
        PDETextGetTextState(text, kPDETextRun, r, &tState, sizeof(tState));

        if (opts.removeTransparentText && tState.renderMode == 3) {
            outMatchedType = "Transparent";
            outMatchedKeyword = "RenderMode=3";
            return true;
        }

        if (opts.removeKeywordText) {
            ASInt32 len = PDETextGetText(text, kPDETextRun, r, NULL);
            if (len > 0) {
                std::vector<ASUns8> buf(len + 1, 0);
                PDETextGetText(text, kPDETextRun, r, buf.data());
                std::string s((char*)buf.data(), len);

                for (const auto &kw : opts.customKeywords) {
                    if (s.find(kw) != std::string::npos) {
                        outMatchedType = "Keyword";
                        outMatchedKeyword = kw;
                        return true;
                    }
                }

                if (opts.removeBottomStrip && bBottom < pageBottom + 60.0f) {
                    if (s.find("È«ÄÜÍõ") != std::string::npos ||  
                        s.find("É¨Ãè") != std::string::npos) {           
                        outMatchedType = "Keyword";
                        outMatchedKeyword = "CamScanner Footer";
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

bool WatermarkService::isWatermarkPath(PDEPath path, const CleanOptions &opts, const ASFixedRect &cropBox, std::string &outMatchedType) {
    PDEGraphicState gState;
    memset(&gState, 0, sizeof(gState));
    PDEElementGetGState((PDEElement)path, &gState, sizeof(gState));

    bool isPattern = false;
    if (gState.fillColorSpec.space != NULL) {
        ASAtom spName = PDEColorSpaceGetName(gState.fillColorSpec.space);
        if (spName == ASAtomFromString("Pattern")) {
            isPattern = true;
        }
    }

    ASFixedRect bbox;
    PDEElementGetBBox((PDEElement)path, &bbox);
    float bw = ASFixedToFloat(bbox.right - bbox.left);
    float bh = ASFixedToFloat(bbox.top - bbox.bottom);

    // Fingerprint check
    if (opts.targetFingerprint.active && opts.targetFingerprint.type == kPDEPath) {
        if (float_eq(bw, opts.targetFingerprint.bboxWidth) && 
            float_eq(bh, opts.targetFingerprint.bboxHeight) &&
            isPattern == opts.targetFingerprint.isPattern) {
            outMatchedType = "Target";
            return true;
        }
    }

    // Built-in checks
    if (opts.removePatternFills && isPattern) {
        outMatchedType = "Pattern";
        return true;
    }

    float pw = ASFixedToFloat(cropBox.right - cropBox.left);
    float ph = ASFixedToFloat(cropBox.top - cropBox.bottom);
    float bBottom = ASFixedToFloat(bbox.bottom);
    float pageBottom = ASFixedToFloat(cropBox.bottom);

    if (opts.removeBottomStrip && pw > 50.0f && ph > 50.0f) {
        if (bBottom < pageBottom + 65.0f && bh < 65.0f && bw > pw * 0.60f) {
            outMatchedType = "Pattern";
            return true;
        }
    }

    if (opts.removePatternFills && gState.extGState != NULL && pw > 50.0f && ph > 50.0f) {
        float op = ASFixedToFloat(PDEExtGStateGetOpacityFill(gState.extGState));
        if (op > 0.001f && op < 0.35f && bw > pw * 0.40f && bh > ph * 0.25f) {
            outMatchedType = "Pattern";
            return true;
        }
    }

    return false;
}

bool WatermarkService::isWatermarkImage(PDEImage img, const CleanOptions &opts, const ASFixedRect &cropBox, std::string &outMatchedType) {
    ASFixedRect bbox;
    PDEElementGetBBox((PDEElement)img, &bbox);
    float bw = ASFixedToFloat(bbox.right - bbox.left);
    float bh = ASFixedToFloat(bbox.top - bbox.bottom);

    // Fingerprint check
    if (opts.targetFingerprint.active && opts.targetFingerprint.type == kPDEImage) {
        if (float_eq(bw, opts.targetFingerprint.width) && 
            float_eq(bh, opts.targetFingerprint.height)) {
            outMatchedType = "Target";
            return true;
        }
    }

    // Built in checks
    if (!opts.removeBottomStrip) return false;

    float bBottom = ASFixedToFloat(bbox.bottom);
    float pageBottom = ASFixedToFloat(cropBox.bottom);

    if (bBottom < pageBottom + 65.0f && bw < 70.0f && bh < 70.0f && bw > 10.0f && bh > 10.0f) {
        outMatchedType = "Pattern";
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
                std::string matchedType, kw;
                if (isWatermarkText((PDEText)elem, opts, cropBox, matchedType, kw)) {
                    if (matchedType == "Transparent") {
                        res.transparentTextCount++;
                    } else if (matchedType == "Keyword") {
                        res.keywordWatermarkCount++;
                        if (std::find(res.detectedKeywords.begin(), res.detectedKeywords.end(), kw) == res.detectedKeywords.end()) {
                            res.detectedKeywords.push_back(kw);
                        }
                    }
                }
            } else if (type == kPDEPath) {
                res.pathElements++;
                std::string matchedType;
                if (isWatermarkPath((PDEPath)elem, opts, cropBox, matchedType)) {
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
