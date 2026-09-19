#pragma once
#include "PluginInit.h"
#include "WatermarkService.h"

namespace wipepdf {

class SelectionTool {
public:
    static void RegisterTool();
    static void UnregisterTool();
    static void ActivateTool(AVTool prevTool);

private:
    static AVTool gSelectionTool;
    static AVTool gPreviousTool;

    // Callbacks
    static ASBool ACCB1 ComputeEnabledProc(void *data);
    static void ACCB1 ActivateProc(AVTool tool, ASBool persistent);
    static void ACCB1 DeactivateProc(AVTool tool);
    static ASAtom ACCB1 GetTypeProc(AVTool tool);
    static ASBool ACCB1 DoClickProc(AVTool tool, AVPageView pageView, ASInt16 x, ASInt16 y, ASInt16 flags, ASInt16 clickNo);
        
    // Hit testing logic. Returns true if the click hit a valid element and
    // the user confirmed deletion (so the caller can restore the prior tool).
    static bool HandleClick(AVPageView pageView, ASInt16 x, ASInt16 y);

    // Recursively hit-test inside a Form XObject or marked-content Container
    // (e.g. a /Artifact /Subtype /Watermark block). Child element bboxes are
    // transformed into page coordinates using the accumulated matrix, so a
    // watermark nested in a Form/Container is picked instead of the page
    // background. Returns true and fills `fp` if a pickable element was hit.
    static bool HitTestFormContent(PDEElement container, const ASFixedPoint &pagePt,
                                   const ASFixedMatrix *parentMatrix, TargetFingerprint &fp);

    // Extract a fingerprint from a Form/Container that was hit. The container's
    // page-space bbox is used for size/position; pixel/text attributes come
    // from the first pickable child found in its content (no matrix math).
    // Returns true if a pickable child was found.
    static bool ExtractContainerFingerprint(PDEElement container, const ASFixedRect &pageBBox,
                                            TargetFingerprint &fp);
};

} // namespace wipepdf
