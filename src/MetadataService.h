#pragma once
#include "PluginInit.h"

namespace wipepdf {

struct MetadataCleanResult {
    int removedInfoKeys = 0;   // keys removed from the (legacy) Info dict
    bool xmpReplaced = false;  // XMP metadata stream replaced with empty packet
};

class MetadataService {
public:
    // Wipe all document metadata of the active document:
    //  - Replace the XMP metadata stream with a minimal empty packet.
    //  - Remove every key from the legacy Info dictionary.
    static MetadataCleanResult cleanActiveDocument();

    // Same, for an already opened PDDoc.
    static MetadataCleanResult cleanDocument(PDDoc pdDoc);
};

} // namespace wipepdf
