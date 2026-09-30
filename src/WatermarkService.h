#pragma once
#include "PluginInit.h"
#include <string>
#include <vector>

namespace wipepdf {

// Convert a UTF-8 byte string (keywords, PDF text runs) into a wide string
// for display. Widening byte-by-byte garbles any non-ASCII content.
inline std::wstring Utf8ToWString(const std::string &s) {
    if (s.empty()) return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring out(len > 0 ? len : 0, L'\0');
    if (len > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], len);
    return out;
}

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

    // True when the picked element is a marked-content Container tagged as a
    // PDF standard watermark (/Artifact /Subtype /Watermark). Deletion then
    // removes such containers directly instead of matching by geometry.
    bool isWatermarkMarked = false;
    
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
    // Default rules: CONSERVATIVE for one-click clean - only highly reliable,
    // content-safe heuristics are on. Riskier geometry heuristics (bottom
    // strips, pattern fills), link annotations and transparent text default
    // OFF so real content is never removed; the manual point-and-click flow
    // is the precise, human-verified path.
    bool removeTransparentText = false; // RenderMode=3 / Opacity=0 (risky to OCR, OFF)
    bool removeLinks = false;          // ALL Link annots incl. TOC/cross-refs (OFF)
    bool removeKeywordText = true;     // URL/brand keyword text w/ size guard
    bool removeBottomStrip = false;    // bottom-edge strips - OFF (risky)
    bool removePatternFills = false;   // pattern fills - OFF (risky)
    
    // For manual point-and-click target
    TargetFingerprint targetFingerprint;
    
    // URL / scanner-brand keywords that strongly indicate a watermark even at
    // small size (matched with a font-size guard inside isWatermarkText).
    std::vector<std::string> watermarkKeywords = {
        "http://", "https://", "www.", ".com", ".cn", ".net",
        "扫描全能王", "CamScanner", "免费注册", "biaozhun"
    };
    
    // Promotion/drainage words that are unlikely to appear in normal body
    // text. Keep this list narrow: every entry here can remove the whole
    // matching text run anywhere in the document.
    std::vector<std::string> customKeywords = {
        "淘宝", "微信", "加群", "公众号", // 推广引流
        "盗版",                          // 版权提示
    };
};

struct WatermarkCandidate {
    ASInt32 pageIndex = 0;
    ASFixedRect bbox = {0,0,0,0};      // Page-space bbox for view highlight
    ASFixedRect localBBox = {0,0,0,0}; // Container-local bbox for precise deletion
    bool selected = true;
    std::string matchType;
    std::string matchKeyword;
    ASInt32 elemType = -1;
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
    int removedOverlays = 0;      // full-page / large-light overlay text
    int removedPatternPaths = 0;
    int removedBottomStrips = 0;
    int removedImages = 0;
    int removedTargetFingers = 0;

    // Path of the pre-modification backup copy in %TEMP% (empty when the
    // backup could not be created). Informational; add() does not merge it.
    std::wstring backupPath;

    void add(const CleanResult& other) {
        totalRemoved += other.totalRemoved;
        removedLinks += other.removedLinks;
        removedTransparentText += other.removedTransparentText;
        removedKeywordText += other.removedKeywordText;
        removedOverlays += other.removedOverlays;
        removedPatternPaths += other.removedPatternPaths;
        removedBottomStrips += other.removedBottomStrips;
        removedImages += other.removedImages;
        removedTargetFingers += other.removedTargetFingers;
    }
};

struct WhiteoutResult {
    int pagesPatched = 0;
    std::wstring backupPath;
    bool success = false;
};

class WatermarkService {
public:
    // Save a full copy of the document to %TEMP%\WipePDF_backup_<tick>.pdf.
    // Returns the backup path, or an empty string when the backup could not
    // be created (path resolution or save failure).
    static std::wstring BackupDocumentToTemp(PDDoc pddoc);

    static CleanResult cleanActiveDocument(const CleanOptions &opts = CleanOptions());
    static CleanResult cleanDocument(PDDoc pddoc, const CleanOptions &opts = CleanOptions());
    static std::vector<WatermarkCandidate> scanDocument(PDDoc pddoc, const CleanOptions &opts = CleanOptions());
    static CleanResult executePlan(PDDoc pddoc, const std::vector<WatermarkCandidate>& plan, const CleanOptions &opts = CleanOptions());
    // Option A: Lossless vector whiteout patch applied over fused watermarks
    static WhiteoutResult applyWhiteoutPatch(PDDoc pddoc, const ASFixedRect& targetPageRect, ASInt32 refPageIndex, bool applyAllPages);
    static PageInspectResult inspectPage(PDDoc pddoc, ASInt32 pageIndex, const CleanOptions &opts = CleanOptions());
    // True if the element is a marked-content Container tagged as a PDF
    // standard watermark (/Artifact + /Subtype /Watermark).
    static bool isWatermarkMarkedContainer(PDEElement elem);

private:
    static CleanResult cleanPageContent(PDPage page, PDEContent content, const CleanOptions &opts, const ASFixedRect &cropBox);
    static CleanResult cleanContainer(PDEElement container, const CleanOptions &opts, const ASFixedRect &cropBox);
    static void scanContainer(ASInt32 pageIndex, PDEElement container, const CleanOptions &opts, const ASFixedRect &cropBox, std::vector<WatermarkCandidate>& outCandidates);
    static CleanResult executeContainerPlan(ASInt32 pageIndex, PDEElement container, const std::vector<WatermarkCandidate>& plan, const CleanOptions &opts, const ASFixedRect &cropBox);

    static std::vector<ASInt32> getWatermarkTextRuns(PDEText text, const CleanOptions &opts, const ASFixedRect &cropBox, std::string &outMatchedType, std::string &outMatchedKeyword);
    static bool isWatermarkPath(PDEPath path, const CleanOptions &opts, const ASFixedRect &cropBox, std::string &outMatchedType);
    static bool isWatermarkImage(PDEImage img, const CleanOptions &opts, const ASFixedRect &cropBox, std::string &outMatchedType);
    // Shared position guard used by all matchers.
    static bool fingerprintPositionMatches(const TargetFingerprint &fp, const ASFixedRect &bbox, const ASFixedRect &cropBox);
};

} // namespace wipepdf
