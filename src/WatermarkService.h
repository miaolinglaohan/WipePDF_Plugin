#pragma once
#include "PluginInit.h"
#include <string>
#include <vector>

namespace wipepdf {

struct TargetFingerprint {
    bool active = false;
    ASInt32 type = -1; // kPDEImage, kPDEText, kPDEPath
    
    // For Image
    float width = 0;
    float height = 0;
    
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
        "ÌÔ±¦", "Î¢ÐÅ", "¼ÓÈº", "¹«ÖÚºÅ", // 淘宝, 微信, 加群, 公众号 (GBK)
        "Ë®Ó¡", "µÁ°æ", "±©Á¦", "ÆÆ½â", // 水印, 盗版, 暴力, 破解
        "É¨Ãè", "±¸×¢", // 扫描, 备注
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
    int removedImages = 0;
    int removedTargetFingers = 0;
    
    void add(const CleanResult& other) {
        totalRemoved += other.totalRemoved;
        removedLinks += other.removedLinks;
        removedTransparentText += other.removedTransparentText;
        removedKeywordText += other.removedKeywordText;
        removedPatternPaths += other.removedPatternPaths;
        removedImages += other.removedImages;
        removedTargetFingers += other.removedTargetFingers;
    }
};

class WatermarkService {
public:
    static CleanResult cleanActiveDocument(const CleanOptions &opts = CleanOptions());
    static CleanResult cleanDocument(PDDoc pddoc, const CleanOptions &opts = CleanOptions());
    static PageInspectResult inspectPage(PDDoc pddoc, ASInt32 pageIndex, const CleanOptions &opts = CleanOptions());

private:
    static CleanResult cleanPageContent(PDPage page, PDEContent content, const CleanOptions &opts, const ASFixedRect &cropBox);
    static CleanResult cleanForm(PDEForm form, const CleanOptions &opts, const ASFixedRect &cropBox);
    static bool isWatermarkText(PDEText text, const CleanOptions &opts, const ASFixedRect &cropBox, std::string &outMatchedType, std::string &outMatchedKeyword);
    static bool isWatermarkPath(PDEPath path, const CleanOptions &opts, const ASFixedRect &cropBox, std::string &outMatchedType);
    static bool isWatermarkImage(PDEImage img, const CleanOptions &opts, const ASFixedRect &cropBox, std::string &outMatchedType);
};

} // namespace wipepdf
