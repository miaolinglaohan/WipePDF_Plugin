#pragma once
#include "PluginInit.h"

#include <string>
#include <vector>

namespace wipepdf {

struct MetadataCleanResult {
    int removedInfoKeys = 0;   // keys removed from the (legacy) Info dict
    bool xmpReplaced = false;  // XMP metadata stream replaced with empty packet
};

struct BatchMetadataResult {
    int totalFiles = 0;
    int successFiles = 0;
    int totalRemovedInfoKeys = 0;
    int totalXmpReplaced = 0;
    std::vector<std::wstring> errors;
};

class MetadataService {
public:
    // Wipe all document metadata of the active document:
    //  - Replace the XMP metadata stream with a minimal empty packet.
    //  - Remove every key from the legacy Info dictionary.
    static MetadataCleanResult cleanActiveDocument();

    // Same, for an already opened PDDoc.
    static MetadataCleanResult cleanDocument(PDDoc pdDoc);

    // Batch clean metadata across multiple PDF documents given by absolute file paths.
    // Each document is backed up before modification and saved with PDSaveFull (physical overwrite).
    static BatchMetadataResult cleanBatch(const std::vector<std::wstring> &filePaths);
};

} // namespace wipepdf
