#include "WatermarkService.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace wipepdf {

static bool float_eq(float a, float b) {
    return std::fabs(a - b) < 2.0f; // 2 pt tolerance
}

// Transform a candidate element bbox into page space when the fingerprint was
// picked inside a Form XObject (candidate bbox is in Form-local coordinates).
// Returns false if no transformation applies (top-level element).
static bool PageSpaceBBox(const TargetFingerprint &fp, const ASFixedRect &bbox, ASFixedRect &out) {
    if (!fp.inForm) return false;
    ASFixedPoint tl = { bbox.left, bbox.top };
    ASFixedPoint br = { bbox.right, bbox.bottom };
    ASFixedPoint tl2, br2;
    ASFixedMatrixTransform(&tl2, (ASFixedMatrixP)&fp.formMatrix, (ASFixedPointP)&tl);
    ASFixedMatrixTransform(&br2, (ASFixedMatrixP)&fp.formMatrix, (ASFixedPointP)&br);
    out.left   = (tl2.h < br2.h) ? tl2.h : br2.h;
    out.right  = (tl2.h > br2.h) ? tl2.h : br2.h;
    out.bottom = (tl2.v < br2.v) ? tl2.v : br2.v;
    out.top    = (tl2.v > br2.v) ? tl2.v : br2.v;
    return true;
}

// Minimal GBK -> Unicode (CJK + common symbols) table covering the Chinese
// keywords this plugin searches for. Each entry is the GBK lead+trail byte
// pair and the corresponding Unicode code point.
struct GbkEntry { unsigned short gbk; unsigned short uni; };
static const GbkEntry kGbkTable[] = {
    // 淘宝 微信 加群 公众号
    {0xCCD4, 0x6DD8}, {0xB1A6, 0x5B9D},
    {0xCEA2, 0x5FAE}, {0xD0C5, 0x4FE1},
    {0xBCD3, 0x52A0}, {0xC8BA, 0x7FA4},
    {0xB9AB, 0x516C}, {0xD6DA, 0x4F17}, {0xBAC5, 0x53F7},
    // 水印 盗版 暴力 破解
    {0xCBAE, 0x6C34}, {0xD3A1, 0x5370},
    {0xB5C1, 0x76D7}, {0xB0E6, 0x7248},
    {0xB1A9, 0x66B4}, {0xC1A6, 0x529B},
    {0xC6C6, 0x7834}, {0xBDE2, 0x89E3},
    // 扫描 备注
    {0xC9A8, 0x626B}, {0xC3E8, 0x63CF},
    {0xB1B8, 0x5907}, {0xD7A2, 0x6CE8},
    // 全能王 (CamScanner footer)
    {0xC8AB, 0x5168}, {0xC4DC, 0x80FD}, {0xCDF5, 0x738B},
    // 链接 广告
    {0xC1B4, 0x94FE}, {0xBDD3, 0x63A5},
    {0xB9E3, 0x5E7F}, {0xB8E6, 0x544A},
    // 二维码 验证 电话 手机
    {0xB6FE, 0x4E8C}, {0xCEAC, 0x7EF4}, {0xC2EB, 0x7801},
    {0xD1E9, 0x9A8C}, {0xD6A4, 0x8BC1},
    {0xB5E7, 0x7535}, {0xBBB0, 0x8BDD},
    {0xCAD6, 0x624B}, {0xBBFA, 0x673A},
};
// Reverse lookup: Unicode code point -> GBK byte pair (from kGbkTable).
static unsigned short UniToGbk(unsigned short uni) {
    for (const auto &e : kGbkTable) {
        if (e.uni == uni) return e.gbk;
    }
    return 0;
}

// Decode a UTF-8 string into UTF-16 code units (native endianness).
static std::vector<unsigned short> Utf8ToUtf16(const std::string &s) {
    std::vector<unsigned short> out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i];
        unsigned int cp;
        if (c < 0x80) { cp = c; i += 1; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            cp = ((c & 0x1F) << 6) | ((unsigned char)s[i + 1] & 0x3F); i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
            cp = ((c & 0x0F) << 12) | (((unsigned char)s[i + 1] & 0x3F) << 6)
                 | ((unsigned char)s[i + 2] & 0x3F); i += 3;
        } else { cp = 0xFFFD; i += 1; }
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back((unsigned short)(0xD800 | (cp >> 10)));
            out.push_back((unsigned short)(0xDC00 | (cp & 0x3FF)));
        } else {
            out.push_back((unsigned short)cp);
        }
    }
    return out;
}

// Encode a keyword (UTF-8) into a byte pattern for the given target encoding
// and search for it inside the raw text-run bytes. Returns true on a hit.
static bool KeywordHitEncoded(const std::string &raw, const std::string &utf8Keyword,
                              bool utf16, bool bigEndian) {
    std::vector<unsigned short> units = Utf8ToUtf16(utf8Keyword);
    std::string pat;
    if (!utf16) {
        for (unsigned short u : units) {
            unsigned short gbk = UniToGbk(u);
            if (gbk == 0) return false; // char not in table, cannot encode
            pat.push_back((char)(gbk >> 8));
            pat.push_back((char)(gbk & 0xFF));
        }
    } else {
        for (unsigned short u : units) {
            if (bigEndian) { pat.push_back((char)(u >> 8)); pat.push_back((char)(u & 0xFF)); }
            else           { pat.push_back((char)(u & 0xFF)); pat.push_back((char)(u >> 8)); }
        }
    }
    return raw.find(pat) != std::string::npos;
}

// Match a keyword against a PDF text-run's raw bytes. Keywords come in as
// standard UTF-8; the run bytes can be WinAnsi/Latin-1, GBK, UTF-16BE or
// UTF-16LE depending on how the producer embedded the text. ASCII/URL
// keywords match the raw bytes directly; Chinese keywords are searched as
// their GBK and UTF-16 byte patterns so encoding quirks in the PDF no longer
// break detection (previously the keyword list was double-encoded garbage
// that never matched anything).
static bool KeywordInRun(const std::string &raw, const std::string &utf8Keyword) {
    if (utf8Keyword.find_first_not_of(" -~") == std::string::npos)
        return raw.find(utf8Keyword) != std::string::npos;
    return KeywordHitEncoded(raw, utf8Keyword, false, false) ||
           KeywordHitEncoded(raw, utf8Keyword, true, true) ||
           KeywordHitEncoded(raw, utf8Keyword, true, false);
}

// Exact whole-run match between a stored fingerprint (raw bytes captured at
// click time) and a candidate run, tolerant of the encoding differences above.
static bool FingerprintExactMatch(const std::string &raw, const std::string &fpRaw) {
    if (raw == fpRaw) return true; // same encoding in practice
    // Also compare as UTF-16BE / UTF-16LE decoded sequences.
    auto norm16 = [](const std::string &s, bool bigEndian) {
        std::vector<unsigned short> v;
        size_t off = 0;
        if (s.size() >= 2 && (unsigned char)s[0] == 0xFE && (unsigned char)s[1] == 0xFF) { bigEndian = true; off = 2; }
        else if (s.size() >= 2 && (unsigned char)s[0] == 0xFF && (unsigned char)s[1] == 0xFE) { bigEndian = false; off = 2; }
        for (size_t i = off; i + 1 < s.size(); i += 2) {
            unsigned short u = bigEndian
                ? (unsigned short)(((unsigned char)s[i] << 8) | (unsigned char)s[i + 1])
                : (unsigned short)((unsigned char)s[i] | ((unsigned char)s[i + 1] << 8));
            v.push_back(u);
        }
        return v;
    };
    auto a = norm16(raw, true), b = norm16(fpRaw, true);
    if (a == b) return true;
    a = norm16(raw, false); b = norm16(fpRaw, false);
    if (a == b) return true;
    return false;
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

    // Mark the document as needing to be saved so Acrobat prompts on close
    // instead of silently auto-saving our modifications over the original.
    if (totalRes.totalRemoved > 0) {
        PDDocSetFlags(pddoc, PDDocNeedsSave);
    }

    return totalRes;
}

// Position/size guard shared by all fingerprint matchers. When the user
// manually picked a watermark, we require the candidate element to sit at a
// similar normalized position and occupy a similar relative size, so that
// full-page scanned backgrounds (which share pixel dimensions with a
// watermark image but cover the whole page) are never removed.
//
// `bbox` must already be in page space (callers transform Form-local bboxes
// to page space via PageSpaceBBox() before invoking this).
bool WatermarkService::fingerprintPositionMatches(const TargetFingerprint &fp, const ASFixedRect &bbox, const ASFixedRect &cropBox) {
    float pageW = ASFixedToFloat(cropBox.right - cropBox.left);
    float pageH = ASFixedToFloat(cropBox.top - cropBox.bottom);
    if (pageW <= 0.0f || pageH <= 0.0f) return true;

    float cx = ASFixedToFloat((bbox.left + bbox.right) / 2);
    float cy = ASFixedToFloat((bbox.bottom + bbox.top) / 2);
    float bw = ASFixedToFloat(bbox.right - bbox.left);
    float bh = ASFixedToFloat(bbox.top - bbox.bottom);

    float relX = cx / pageW;
    float relY = cy / pageH;
    float relW = bw / pageW;
    float relH = bh / pageH;

    // Center position must be within 12% of the page (in either axis) of the
    // picked element's normalized center.
    const float kPosTol = 0.12f;
    if (std::fabs(relX - fp.relX) > kPosTol) return false;
    if (std::fabs(relY - fp.relY) > kPosTol) return false;

    // Relative size must be within 25% (relative) of the picked element.
    const float kSizeTol = 0.25f;
    if (fp.relW > 0.0f && std::fabs(relW - fp.relW) > fp.relW * kSizeTol) return false;
    if (fp.relH > 0.0f && std::fabs(relH - fp.relH) > fp.relH * kSizeTol) return false;

    return true;
}

CleanResult WatermarkService::countDocument(PDDoc pddoc, const CleanOptions &opts) {
    CleanResult total;
    if (!pddoc) return total;

    ASInt32 numPages = PDDocGetNumPages(pddoc);
    for (ASInt32 p = 0; p < numPages; ++p) {
        PDPage page = PDDocAcquirePage(pddoc, p);
        if (!page) continue;

        ASFixedRect cropBox;
        PDPageGetCropBox(page, &cropBox);

        if (opts.removeLinks) {
            ASInt32 numAnnots = PDPageGetNumAnnots(page);
            for (ASInt32 a = 0; a < numAnnots; ++a) {
                PDAnnot annot = PDPageGetAnnot(page, a);
                if (PDAnnotIsValid(annot)) {
                    ASAtom subtype = PDAnnotGetSubtype(annot);
                    if (subtype == ASAtomFromString("Link")) {
                        total.removedLinks++;
                        total.totalRemoved++;
                    }
                }
            }
        }

        PDEContent content = PDPageAcquirePDEContent(page, gExtensionID);
        if (content) {
            CleanResult res = countPageContent(content, opts, cropBox);
            total.add(res);
            PDPageReleasePDEContent(page, gExtensionID);
        }

        PDPageRelease(page);
    }
    return total;
}

CleanResult WatermarkService::countPageContent(PDEContent content, const CleanOptions &opts, const ASFixedRect &cropBox) {
    CleanResult res;
    ASInt32 numElems = PDEContentGetNumElems(content);
    bool watermarkOnly = opts.targetFingerprint.active && opts.targetFingerprint.isWatermarkMarked;
    for (ASInt32 i = 0; i < numElems; ++i) {
        PDEElement elem = PDEContentGetElem(content, i);
        if (!elem) continue;

        ASInt32 type = PDEObjectGetType((PDEObject)elem);

        if (watermarkOnly) {
            if (type == kPDEContainer && isWatermarkMarkedContainer(elem)) {
                res.totalRemoved++;
                res.removedTargetFingers++;
            }
            continue;
        }

        if (type == kPDEText) {
            std::string matchType, kw;
            std::vector<ASInt32> runs = getWatermarkTextRuns((PDEText)elem, opts, cropBox, matchType, kw);
            if (!runs.empty()) {
                res.totalRemoved++;
                if (matchType == "Target") res.removedTargetFingers++;
                else if (matchType == "Transparent") res.removedTransparentText++;
                else if (matchType == "Keyword") res.removedKeywordText++;
            }
        } else if (type == kPDEPath) {
            std::string matchType;
            if (isWatermarkPath((PDEPath)elem, opts, cropBox, matchType)) {
                res.totalRemoved++;
                if (matchType == "Target") res.removedTargetFingers++;
                else if (matchType == "Pattern") res.removedPatternPaths++;
                else if (matchType == "BottomStrip") res.removedBottomStrips++;
            }
        } else if (type == kPDEImage) {
            std::string matchType;
            if (isWatermarkImage((PDEImage)elem, opts, cropBox, matchType)) {
                res.totalRemoved++;
                if (matchType == "Target") res.removedTargetFingers++;
                else if (matchType == "BottomStrip") res.removedBottomStrips++;
                else res.removedImages++;
            }
        } else if (type == kPDEForm || type == kPDEContainer) {
            if (type == kPDEContainer && isWatermarkMarkedContainer(elem)) {
                res.totalRemoved++;
                res.removedTargetFingers++;
                continue;
            }
            CleanResult formRes = countContainer(elem, opts, cropBox);
            res.add(formRes);
        }
    }
    return res;
}

CleanResult WatermarkService::countContainer(PDEElement container, const CleanOptions &opts, const ASFixedRect &cropBox) {
    CleanResult res;
    ASInt32 ctype = PDEObjectGetType((PDEObject)container);
    PDEContent inner = NULL;
    if (ctype == kPDEForm) {
        inner = PDEFormGetContent((PDEForm)container);
    } else if (ctype == kPDEContainer) {
        inner = PDEContainerGetContent((PDEContainer)container);
    }
    if (!inner) return res;
    res = countPageContent(inner, opts, cropBox);
    return res;
}

CleanResult WatermarkService::cleanPageContent(PDPage page, PDEContent content, const CleanOptions &opts, const ASFixedRect &cropBox) {
    CleanResult res;
    ASInt32 numElems = PDEContentGetNumElems(content);
    // When the user point-picked a PDF-standard watermark container, delete
    // ONLY such containers and skip every other element - the safest possible
    // "remove the same watermark everywhere" semantics.
    bool watermarkOnly = opts.targetFingerprint.active && opts.targetFingerprint.isWatermarkMarked;

    for (ASInt32 i = numElems - 1; i >= 0; --i) {
        PDEElement elem = PDEContentGetElem(content, i);
        if (!elem) continue;

        ASInt32 type = PDEObjectGetType((PDEObject)elem);

        if (watermarkOnly) {
            // In watermark-only mode, only marked containers are removed.
            if (type == kPDEContainer && isWatermarkMarkedContainer(elem)) {
                PDEContentRemoveElem(content, i);
                res.totalRemoved++;
                res.removedTargetFingers++;
            }
            continue;
        }

        if (type == kPDEText) {
            std::string matchType, kw;
            std::vector<ASInt32> runs = getWatermarkTextRuns((PDEText)elem, opts, cropBox, matchType, kw);
            if (!runs.empty()) {
                // R01: Safely isolate and remove only the matched text runs.
                for (auto it = runs.rbegin(); it != runs.rend(); ++it) {
                    PDETextRemove((PDEText)elem, kPDETextRun, *it, 1);
                }
                // If the entire text element is now empty, remove it completely.
                if (PDETextGetNumRuns((PDEText)elem) == 0) {
                    PDEContentRemoveElem(content, i);
                }
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
                else if (matchType == "BottomStrip") res.removedBottomStrips++;
            }
        } else if (type == kPDEImage) {
            std::string matchType;
            if (isWatermarkImage((PDEImage)elem, opts, cropBox, matchType)) {
                PDEContentRemoveElem(content, i);
                res.totalRemoved++;
                if (matchType == "Target") res.removedTargetFingers++;
                else if (matchType == "BottomStrip") res.removedBottomStrips++;
                else res.removedImages++;
            }
        } else if (type == kPDEForm || type == kPDEContainer) {
            // A marked-content container tagged as a PDF standard watermark
            // (e.g. /Artifact /Subtype /Watermark) is removed as a whole.
            if (type == kPDEContainer && isWatermarkMarkedContainer(elem)) {
                PDEContentRemoveElem(content, i);
                res.totalRemoved++;
                res.removedTargetFingers++;
                continue;
            }
            CleanResult formRes = cleanContainer((PDEElement)elem, opts, cropBox);
            res.add(formRes);
            // If the container's content was emptied by cleaning, remove the
            // now useless container element entirely from the page.
            PDEContent inner = (type == kPDEForm)
                ? PDEFormGetContent((PDEForm)elem)
                : PDEContainerGetContent((PDEContainer)elem);
            if (inner && PDEContentGetNumElems(inner) == 0) {
                PDEContentRemoveElem(content, i);
            }
        }
    }

    return res;
}

CleanResult WatermarkService::cleanContainer(PDEElement container, const CleanOptions &opts, const ASFixedRect &cropBox) {
    CleanResult res;
    ASInt32 ctype = PDEObjectGetType((PDEObject)container);
    PDEContent inner = NULL;
    if (ctype == kPDEForm) {
        inner = PDEFormGetContent((PDEForm)container);
    } else if (ctype == kPDEContainer) {
        inner = PDEContainerGetContent((PDEContainer)container);
    }
    if (!inner) return res;

    ASInt32 numElems = PDEContentGetNumElems(inner);
    for (ASInt32 i = numElems - 1; i >= 0; --i) {
        PDEElement elem = PDEContentGetElem(inner, i);
        if (!elem) continue;

        ASInt32 type = PDEObjectGetType((PDEObject)elem);
        if (type == kPDEText) {
            std::string matchType, kw;
            std::vector<ASInt32> runs = getWatermarkTextRuns((PDEText)elem, opts, cropBox, matchType, kw);
            if (!runs.empty()) {
                for (auto it = runs.rbegin(); it != runs.rend(); ++it) {
                    PDETextRemove((PDEText)elem, kPDETextRun, *it, 1);
                }
                if (PDETextGetNumRuns((PDEText)elem) == 0) {
                    PDEContentRemoveElem(inner, i);
                }
                res.totalRemoved++;
                if (matchType == "Target") res.removedTargetFingers++;
                else if (matchType == "Transparent") res.removedTransparentText++;
                else if (matchType == "Keyword") res.removedKeywordText++;
            }
        } else if (type == kPDEPath) {
            std::string matchType;
            if (isWatermarkPath((PDEPath)elem, opts, cropBox, matchType)) {
                PDEContentRemoveElem(inner, i);
                res.totalRemoved++;
                if (matchType == "Target") res.removedTargetFingers++;
                else if (matchType == "Pattern") res.removedPatternPaths++;
                else if (matchType == "BottomStrip") res.removedBottomStrips++;
            }
        } else if (type == kPDEImage) {
            std::string matchType;
            if (isWatermarkImage((PDEImage)elem, opts, cropBox, matchType)) {
                PDEContentRemoveElem(inner, i);
                res.totalRemoved++;
                if (matchType == "Target") res.removedTargetFingers++;
                else if (matchType == "BottomStrip") res.removedBottomStrips++;
                else res.removedImages++;
            }
        } else if (type == kPDEForm || type == kPDEContainer) {
            CleanResult nested = cleanContainer(elem, opts, cropBox);
            res.add(nested);
            PDEContent nestedInner = (type == kPDEForm)
                ? PDEFormGetContent((PDEForm)elem)
                : PDEContainerGetContent((PDEContainer)elem);
            if (nestedInner && PDEContentGetNumElems(nestedInner) == 0) {
                PDEContentRemoveElem(inner, i);
            }
        }
    }

    if (res.totalRemoved > 0) {
        if (ctype == kPDEForm) {
            PDEFormSetContent((PDEForm)container, inner);
        } else {
            PDEContainerSetContent((PDEContainer)container, inner);
        }
    }
    return res;
}

std::vector<ASInt32> WatermarkService::getWatermarkTextRuns(PDEText text, const CleanOptions &opts, const ASFixedRect &cropBox, std::string &outMatchedType, std::string &outMatchedKeyword) {
    std::vector<ASInt32> matchedRuns;
    ASInt32 numRuns = PDETextGetNumRuns(text);
    if (numRuns <= 0) return matchedRuns;
    
    ASFixedRect bbox;
    PDEElementGetBBox((PDEElement)text, &bbox);
    float bBottom = ASFixedToFloat(bbox.bottom);
    float pageBottom = ASFixedToFloat(cropBox.bottom);

    // NOTE: a geometry-only "large overlay" rule was tried but removed: a big
    // text bbox can be the whole page's body text merged into one PDE text
    // element, so deleting it would destroy content. Overlay watermarks are
    // handled precisely via the manual point-and-click path instead (human
    // confirms what is deleted), keeping one-click clean conservative.

    // Check fingerprint
    if (opts.targetFingerprint.active && opts.targetFingerprint.type == kPDEText) {
        for (ASInt32 r = 0; r < numRuns; ++r) {
            ASInt32 len = PDETextGetText(text, kPDETextRun, r, NULL);
            if (len > 0) {
                std::vector<ASUns8> buf(len + 1, 0);
                PDETextGetText(text, kPDETextRun, r, buf.data());
                std::string s((char*)buf.data(), len);
                if (FingerprintExactMatch(s, opts.targetFingerprint.textContent)) {
                    outMatchedType = "Target";
                    matchedRuns.push_back(r);
                }
            }
        }
        if (!matchedRuns.empty()) return matchedRuns;
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
                    matchedRuns.push_back(r);
                    continue;
                }
            }
        }

        PDETextState tState;
        memset(&tState, 0, sizeof(tState));
        PDETextGetTextState(text, kPDETextRun, r, &tState, sizeof(tState));

        if (opts.removeTransparentText && tState.renderMode == 3) {
            outMatchedType = "Transparent";
            outMatchedKeyword = "RenderMode=3";
            matchedRuns.push_back(r);
                    continue;
        }

        if (opts.removeKeywordText) {
            ASInt32 len = PDETextGetText(text, kPDETextRun, r, NULL);
            if (len > 0) {
                std::vector<ASUns8> buf(len + 1, 0);
                PDETextGetText(text, kPDETextRun, r, buf.data());
                std::string s((char*)buf.data(), len);
                float fontSize = ASFixedToFloat(tState.fontSize);

                // A run whose bytes are all ASCII holds real characters.
                // CID/subset-font glyph codes contain high bytes; URL keyword
                // matching on those would match random glyph patterns and
                // delete real content, so URL matching is ASCII-only.
                bool asciiOnly = true;
                for (unsigned char c : s) {
                    if (c >= 0x80) { asciiOnly = false; break; }
                }

                // URL / scanner-brand watermark keywords. These strongly
                // indicate a watermark; require a minimum size so real body
                // text URLs (small, inline) are not removed.
                bool isUrlLike = (s.find("http") != std::string::npos ||
                                  s.find("www.") != std::string::npos ||
                                  s.find(".com") != std::string::npos ||
                                  s.find(".cn") != std::string::npos);
                if (asciiOnly && isUrlLike && fontSize >= 16.0f) {
                    outMatchedType = "Keyword";
                    outMatchedKeyword = "URL Watermark";
                    matchedRuns.push_back(r);
                    continue;
                }
                // KeywordInRun matches the exact GBK/UTF-16 byte patterns of
                // the keywords, so genuine Chinese keywords (test1) match and
                // random CID glyph codes virtually never do. ASCII keywords
                // ("www.", ".com") are only matched on ASCII runs so glyph
                // codes cannot accidentally hit those short patterns.
                for (const auto &kw : opts.watermarkKeywords) {
                    bool kwAscii = (kw.find_first_not_of(" -~") == std::string::npos);
                    if (kwAscii && !asciiOnly) continue;
                    if (KeywordInRun(s, kw)) {
                        outMatchedType = "Keyword";
                        outMatchedKeyword = kw;
                        matchedRuns.push_back(r);
                    continue;
                    }
                }

                for (const auto &kw : opts.customKeywords) {
                    bool kwAscii = (kw.find_first_not_of(" -~") == std::string::npos);
                    if (kwAscii && !asciiOnly) continue;
                    if (KeywordInRun(s, kw)) {
                        outMatchedType = "Keyword";
                        outMatchedKeyword = kw;
                        matchedRuns.push_back(r);
                    continue;
                    }
                }

                // CamScanner-style footer line at the page bottom. Gated by
                // removeBottomStrip but only fires on the brand keyword, so it
                // cannot remove normal bottom text.
                if (opts.removeBottomStrip && bBottom < pageBottom + 60.0f) {
                    if (KeywordInRun(s, "全能王") ||
                        KeywordInRun(s, "扫描")) {
                        outMatchedType = "Keyword";
                        outMatchedKeyword = "CamScanner Footer";
                        matchedRuns.push_back(r);
                    continue;
                    }
                }
            }
        }
    }

    return matchedRuns;
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
        ASFixedRect pageBBox = bbox;
        PageSpaceBBox(opts.targetFingerprint, bbox, pageBBox);
        float pbw = ASFixedToFloat(pageBBox.right - pageBBox.left);
        float pbh = ASFixedToFloat(pageBBox.top - pageBBox.bottom);
        if (float_eq(pbw, opts.targetFingerprint.bboxWidth) && 
            float_eq(pbh, opts.targetFingerprint.bboxHeight) &&
            isPattern == opts.targetFingerprint.isPattern &&
            fingerprintPositionMatches(opts.targetFingerprint, pageBBox, cropBox)) {
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
        // 底部通栏广告条：贴近页面底部（PDF 坐标 y 小）、高度很小、宽度占页宽过半。
        if (bBottom < pageBottom + 65.0f && bh < 65.0f && bw > pw * 0.60f) {
            outMatchedType = "BottomStrip";
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

// Returns true if the given element is a marked-content Container tagged as a
// PDF standard watermark: MCTag == /Artifact (or /Watermark) with a property
// dict whose /Subtype is /Watermark. This is the most reliable watermark
// signal (used by CamScanner and other scanners) and does not depend on
// geometry heuristics.
bool WatermarkService::isWatermarkMarkedContainer(PDEElement elem) {
    if (PDEObjectGetType((PDEObject)elem) != kPDEContainer) return false;
    PDEContainer c = (PDEContainer)elem;

    ASAtom tag = PDEContainerGetMCTag(c);
    ASAtom artifact = ASAtomFromString("Artifact");
    ASAtom wmSubtype = ASAtomFromString("Watermark");

    // Container must be an /Artifact (or /Watermark) marked-content block.
    bool tagOk = (tag == artifact) || (tag == wmSubtype);
    if (!tagOk) return false;

    // Check the marked-content property dict for /Subtype /Watermark.
    CosObj dict;
    ASBool isInline = false;
    if (!PDEContainerGetDict(c, &dict, &isInline)) return false;
    if (CosObjGetType(dict) != CosDict) return false;

    CosObj subtype = CosDictGet(dict, ASAtomFromString("Subtype"));
    if (CosObjGetType(subtype) != CosName) return false;
    ASAtom st = CosNameValue(subtype);
    return (st == wmSubtype);
}

bool WatermarkService::isWatermarkImage(PDEImage img, const CleanOptions &opts, const ASFixedRect &cropBox, std::string &outMatchedType) {
    ASFixedRect bbox;
    PDEElementGetBBox((PDEElement)img, &bbox);
    float bw = ASFixedToFloat(bbox.right - bbox.left);
    float bh = ASFixedToFloat(bbox.top - bbox.bottom);

    // Native pixel dimensions are a more stable "same image" fingerprint than
    // the on-page display size, which can vary with scaling between pages.
    ASInt32 pixW = 0, pixH = 0;
    {
        PDEImageAttrs attrs;
        memset(&attrs, 0, sizeof(attrs));
        PDEImageGetAttrs(img, &attrs, sizeof(attrs));
        pixW = attrs.width;
        pixH = attrs.height;
    }

    // Fingerprint check
    if (opts.targetFingerprint.active && opts.targetFingerprint.type == kPDEImage) {
        // For a pick inside a Form, candidate bbox is in Form-local space;
        // transform to page space so size and position compare correctly.
        ASFixedRect pageBBox = bbox;
        PageSpaceBBox(opts.targetFingerprint, bbox, pageBBox);
        float pbw = ASFixedToFloat(pageBBox.right - pageBBox.left);
        float pbh = ASFixedToFloat(pageBBox.top - pageBBox.bottom);

        bool sizeMatch = (opts.targetFingerprint.pixelWidth > 0 && opts.targetFingerprint.pixelHeight > 0)
                             ? (pixW == opts.targetFingerprint.pixelWidth && pixH == opts.targetFingerprint.pixelHeight)
                             : (float_eq(pbw, opts.targetFingerprint.width) &&
                                float_eq(pbh, opts.targetFingerprint.height));
        if (sizeMatch && fingerprintPositionMatches(opts.targetFingerprint, pageBBox, cropBox)) {
            outMatchedType = "Target";
            return true;
        }
    }

    // Built in checks
    if (!opts.removeBottomStrip) return false;

    float bBottom = ASFixedToFloat(bbox.bottom);
    float pageBottom = ASFixedToFloat(cropBox.bottom);

    // Small image hugging the page bottom edge (PDF coords: bottom = low y).
    if (bBottom < pageBottom + 65.0f && bw < 70.0f && bh < 70.0f && bw > 10.0f && bh > 10.0f) {
        outMatchedType = "BottomStrip";
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
                std::vector<ASInt32> runs = getWatermarkTextRuns((PDEText)elem, opts, cropBox, matchedType, kw);
                if (!runs.empty()) {
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
