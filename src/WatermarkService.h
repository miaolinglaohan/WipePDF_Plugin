#pragma once
#include "PluginInit.h"
#include <string>
#include <vector>

namespace wipepdf {

struct TargetFingerprint {
    bool active = false;
    ASInt32 type = -1; // kPDEImage, kPDEText, kPDEPath

    // Element's center position relative to the page (0.0 ~ 1.0).
    // Used to avoid matching full-page background images (scanned pages)
    // when the user clicked a small watermark element.
    float relX = 0.5f;
    float relY = 0.5f;
    float relW = 0.0f; // element width as fraction of page width
    float relH = 0.0f; // element height as fraction of page height

    // When the picked element lives inside a Form XObject, this records the
    // Form's page transform so position matching can be done in page space.
    bool inForm = false;
    ASFixedMatrix formMatrix = { fixedOne, fixedZero, fixedZero, fixedOne, fixedZero, fixedZero };
    
    // For Image
    float width = 0;      // displayed width in points
    float height = 0;     // displayed height in points
    ASInt32 pixelWidth = 0;  // native pixel width (PDEImageGetAttrs)
    ASInt32 pixelHeight = 0; // native pixel height
    
    // For Path
    float bboxWidth = 0;
    float bboxHeight = 0;
    bool isPattern = false;
    
    // For Text
    std::string textContent;
};

struct CleanOptions {
    bool removeTransparentText = true;
    bool removePatternFills = true;
    bool removeKeywordText = true;
    bool removeLinks = true;
    bool removeBottomStrip = true;
    
    // For manual point-and-click target
    TargetFingerprint targetFingerprint;
    
    std::vector<std::string> customKeywords = {
        "http://", "https://", "www.", ".com", ".cn", ".net",
        "淘宝", "微信", "加群", "公众号", // 推广引流
        "水印", "盗版", "暴力", "破解",   // 版权提示
        "扫描", "备注",                   // CamScanner 页脚等
        "CamScanner"
    };
};

struct PageInspectResult {
    int totalElements = 0;
    int textElements = 0;
    int transparentTextCount = 0;
    int keywordWatermarkCount = 0;
    int pathElements = 0;
    int patternFillCount = 0;
    int imageElements = 0;
    int linkAnnotations = 0;
    std::vector<std::string> detectedKeywords;
};

struct CleanResult {
    int totalRemoved = 0;
    int removedLinks = 0;
    int removedTransparentText = 0;
    int removedKeywordText = 0;
    int removedPatternPaths = 0;
    int removedBottomStrips = 0;
    int removedImages = 0;
    int removedTargetFingers = 0;
    
    void add(const CleanResult& other) {
        totalRemoved += other.totalRemoved;
        removedLinks += other.removedLinks;
        removedTransparentText += other.removedTransparentText;
        removedKeywordText += other.removedKeywordText;
        removedPatternPaths += other.removedPatternPaths;
        removedBottomStrips += other.removedBottomStrips;
        removedImages += other.removedImages;
        removedTargetFingers += other.removedTargetFingers;
    }
};

class WatermarkService {
public:
    static CleanResult cleanActiveDocument(const CleanOptions &opts = CleanOptions());
    static CleanResult cleanDocument(PDDoc pddoc, const CleanOptions &opts = CleanOptions());
    // Count how many elements would be removed (no modification), used to
    // preview the deletion scope before the user confirms.
    static CleanResult countDocument(PDDoc pddoc, const CleanOptions &opts = CleanOptions());
    static PageInspectResult inspectPage(PDDoc pddoc, ASInt32 pageIndex, const CleanOptions &opts = CleanOptions());

private:
    static CleanResult cleanPageContent(PDPage page, PDEContent content, const CleanOptions &opts, const ASFixedRect &cropBox);
    static CleanResult countPageContent(PDEContent content, const CleanOptions &opts, const ASFixedRect &cropBox);
    static CleanResult cleanContainer(PDEElement container, const CleanOptions &opts, const ASFixedRect &cropBox);
    static CleanResult countContainer(PDEElement container, const CleanOptions &opts, const ASFixedRect &cropBox);
    static bool isWatermarkText(PDEText text, const CleanOptions &opts, const ASFixedRect &cropBox, std::string &outMatchedType, std::string &outMatchedKeyword);
    static bool isWatermarkPath(PDEPath path, const CleanOptions &opts, const ASFixedRect &cropBox, std::string &outMatchedType);
    static bool isWatermarkImage(PDEImage img, const CleanOptions &opts, const ASFixedRect &cropBox, std::string &outMatchedType);
    // True if the element is a marked-content Container tagged as a PDF
    // standard watermark (/Artifact + /Subtype /Watermark).
    static bool isWatermarkMarkedContainer(PDEElement elem);
    // Shared position guard used by all matchers.
    static bool fingerprintPositionMatches(const TargetFingerprint &fp, const ASFixedRect &bbox, const ASFixedRect &cropBox);
};

} // namespace wipepdf
