#pragma once
#include "PluginInit.h"
#include <string>
#include <vector>

namespace wipepdf {

struct CleanOptions {
    bool removeTransparentText = true;
    bool removePatternFills = true;
    bool removeKeywordText = true;
    bool removeLinks = true;
    bool removeBottomStrip = true;
    std::vector<std::string> customKeywords = {
        "http://", "https://", "www.", ".com", ".cn", ".net",
        "淘宝", "店铺", "微信", "公众号", "仅供", "水印",
        "防伪", "盗版", "免费下载", "扫码", "关注",
        "扫描全能王", "全能王", "CamScanner", "扫描创建"
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

class WatermarkService {
public:
    static int cleanActiveDocument(const CleanOptions &opts = CleanOptions());
    static int cleanDocument(PDDoc pddoc, const CleanOptions &opts = CleanOptions());
    static PageInspectResult inspectPage(PDDoc pddoc, ASInt32 pageIndex, const CleanOptions &opts = CleanOptions());

private:
    static int cleanPageContent(PDPage page, PDEContent content, const CleanOptions &opts, const ASFixedRect &cropBox);
    static int cleanForm(PDEForm form, const CleanOptions &opts, const ASFixedRect &cropBox);
    static bool isWatermarkText(PDEText text, const CleanOptions &opts, const ASFixedRect &cropBox, std::string &outMatchedKeyword);
    static bool isWatermarkPath(PDEPath path, const CleanOptions &opts, const ASFixedRect &cropBox);
    static bool isWatermarkImage(PDEImage img, const CleanOptions &opts, const ASFixedRect &cropBox);
};

} // namespace wipepdf
