#pragma once
#include "PluginInit.h"

namespace wipepdf {

struct CleanOptions {
    bool removeTransparentText = true;
    bool removePatternFills = true;
    bool removeLinks = true;
    bool removeBottomStrip = true;
};

class WatermarkService {
public:
    // Cleans the currently active document in Acrobat
    static int cleanActiveDocument(const CleanOptions &opts = CleanOptions());

    // Cleans a specific PDDoc handle
    static int cleanDocument(PDDoc pddoc, const CleanOptions &opts = CleanOptions());
};

} // namespace wipepdf
