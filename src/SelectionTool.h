#pragma once
#include "PluginInit.h"

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
        
    // Hit testing logic
    static void HandleClick(AVPageView pageView, ASInt16 x, ASInt16 y);
};

} // namespace wipepdf
