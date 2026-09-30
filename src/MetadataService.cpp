#include "MetadataService.h"
#include "BackupService.h"
#include "WatermarkService.h"
#include <string>
#include <vector>

namespace wipepdf {

// A minimal, valid XMP packet with no descriptive properties. Replacing the
// document's XMP with this packet removes author, producer, dates, creator
// tool and any custom XMP fields, while keeping the metadata stream itself
// well-formed RDF/XML so Acrobat accepts it.
static const char *kEmptyXmpPacket =
    "<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>"
    "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">"
    "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
    "<rdf:Description rdf:about=\"\"/>"
    "</rdf:RDF>"
    "</x:xmpmeta>"
    "<?xpacket end=\"w\"?>";

// Collected keys of the Info dictionary. CosObjEnum must not mutate the dict
// while enumerating, so keys are gathered first and removed afterwards.
struct InfoKeyCollector {
    std::vector<ASAtom> keys;
};

static ACCB1 ASBool ACCB2 CollectInfoKeyProc(CosObj obj, CosObj value, void *clientData) {
    (void)value;
    InfoKeyCollector *collector = (InfoKeyCollector *)clientData;
    // For dictionaries, the first callback argument is the key object.
    ASInt32 type = CosObjGetType(obj);
    if (type == CosName) {
        collector->keys.push_back(CosNameValue(obj));
    } else if (type == CosString) {
        // Some producers use string keys; remember them via a synthesized atom
        // is not possible, so handle the common name-key case only.
    }
    return true;
}

MetadataCleanResult MetadataService::cleanActiveDocument() {
    AVDoc avDoc = AVAppGetActiveDoc();
    if (!avDoc) return MetadataCleanResult();
    PDDoc pdDoc = AVDocGetPDDoc(avDoc);
    if (!pdDoc) return MetadataCleanResult();
    return cleanDocument(pdDoc);
}

MetadataCleanResult MetadataService::cleanDocument(PDDoc pdDoc) {
    MetadataCleanResult res;
    if (!pdDoc) return res;

    CosDoc cosDoc = PDDocGetCosDoc(pdDoc);

    // 1. Replace the XMP metadata stream with an empty packet. This also
    //    clears the Info-dictionary entries that mirror XMP properties.
    //    PDDocSetXAPMetadata raises if the XML is ill-formed; the DURING/
    //    HANDLER guard keeps the plugin alive in that (unexpected) case.
    {
        std::wstring packet;
        packet.reserve(512);
        for (const char *p = kEmptyXmpPacket; *p; ++p) packet.push_back((wchar_t)(unsigned char)*p);

        ASText metaText = ASTextFromUnicode((const ASUTF16Val *)packet.c_str(), kUTF16HostEndian);
        if (metaText) {
            DURING
                PDDocSetXAPMetadata(pdDoc, metaText);
                res.xmpReplaced = true;
            HANDLER
                res.xmpReplaced = false;
            END_HANDLER
            ASTextDestroy(metaText);
        }
    }

    // 2. Fallback: if the XMP replacement did not go through, drop the
    //    catalog's /Metadata reference entirely so no stale metadata (author,
    //    producer, dates, camera/GPS, custom props) can survive.
    if (!res.xmpReplaced && cosDoc) {
        CosObj root = CosDocGetRoot(cosDoc);
        if (CosObjGetType(root) == CosDict) {
            ASAtom metaKey = ASAtomFromString("Metadata");
            CosObj metaVal = CosDictGet(root, metaKey);
            if (!CosObjEqual(metaVal, CosNewNull())) {
                CosDictRemove(root, metaKey);
            }
        }
    }

    // 3. Remove every remaining key from the legacy Info dictionary, so that
    //    custom keys and non-aliased entries are cleared as well.
    if (cosDoc) {
        CosObj infoDict = CosDocGetInfoDict(cosDoc);
        if (CosObjGetType(infoDict) == CosDict) {
            InfoKeyCollector collector;
            CosObjEnum(infoDict, CollectInfoKeyProc, &collector);
            for (ASAtom key : collector.keys) {
                CosDictRemove(infoDict, key);
                res.removedInfoKeys++;
            }
        }
    }

    // 4. Mark the document as needing save so Acrobat prompts on close
    //    instead of silently writing changes over the original file.
    PDDocSetFlags(pdDoc, PDDocNeedsSave);

    return res;
}

BatchMetadataResult MetadataService::cleanBatch(const std::vector<std::wstring> &filePaths) {
    BatchMetadataResult result;
    result.totalFiles = static_cast<int>(filePaths.size());
    if (filePaths.empty()) return result;

    for (const auto &fullPath : filePaths) {
        ASText diPath = ASTextFromUnicode((const ASUTF16Val *)fullPath.c_str(), kUTF16HostEndian);
        if (!diPath) {
            result.errors.push_back(fullPath + L"（无法解析文件路径）");
            continue;
        }

        ASPathName path = ASFileSysCreatePathFromDIPathText(ASGetDefaultFileSys(), diPath, NULL);
        ASTextDestroy(diPath);
        if (!path) {
            result.errors.push_back(fullPath + L"（无法创建系统文件路径）");
            continue;
        }

        PDDoc doc = NULL;
        DURING
            doc = PDDocOpen(path, ASGetDefaultFileSys(), NULL, false);
        HANDLER
            char errBuf[256] = {0};
            ASGetErrorString(ERRORCODE, errBuf, sizeof(errBuf));
            std::wstring wErr = Utf8ToWString(errBuf);
            result.errors.push_back(fullPath + (wErr.empty() ? L"（无法打开文档）" : L"（打开失败: " + wErr + L"）"));
            doc = NULL;
        END_HANDLER

        ASFileSysReleasePath(ASGetDefaultFileSys(), path);

        if (!doc) {
            continue;
        }

        // 1. Safe automatic backup to %TEMP%\WipePDF_Backups\ before any
        // alteration. Never touch the file when the backup cannot be created -
        // a physical overwrite without a restore point is not acceptable.
        std::wstring backupPath;
        DURING
            backupPath = BackupService::CreateBackup(doc);
        HANDLER
            char errBuf[256] = {0};
            ASGetErrorString(ERRORCODE, errBuf, sizeof(errBuf));
            std::wstring wErr = Utf8ToWString(errBuf);
            result.errors.push_back(fullPath + (wErr.empty() ? L"（备份创建失败，已跳过该文件）" : L"（备份创建失败: " + wErr + L"，已跳过）"));
            backupPath.clear();
        END_HANDLER

        if (backupPath.empty()) {
            DURING
                PDDocClose(doc);
            HANDLER
                ; // Swallow close-time errors - nothing to recover.
            END_HANDLER
            continue;
        }

        bool docSuccess = false;
        DURING
            // 2. Clear all metadata (XMP + Info dictionary)
            MetadataCleanResult mRes = cleanDocument(doc);

            // 3. Physical Linear Erasure: Save with full rewrite and garbage collection
            // to completely remove unreferenced metadata objects from the binary stream.
            // In-place save (NULL path): PDSaveCopy is for save-to-new-path only.
            PDDocSave(doc, (PDSaveFull | PDSaveCollectGarbage), NULL, ASGetDefaultFileSys(), NULL, NULL);

            result.totalRemovedInfoKeys += mRes.removedInfoKeys;
            if (mRes.xmpReplaced) {
                result.totalXmpReplaced++;
            }
            docSuccess = true;
        HANDLER
            char errBuf[256] = {0};
            ASGetErrorString(ERRORCODE, errBuf, sizeof(errBuf));
            std::wstring wErr = Utf8ToWString(errBuf);
            result.errors.push_back(fullPath + (wErr.empty() ? L"（保存或清除元数据失败）" : L"（处理失败: " + wErr + L"）"));
            docSuccess = false;
        END_HANDLER

        DURING
            PDDocClose(doc);
        HANDLER
            ; // Swallow close-time errors - the result is already recorded.
        END_HANDLER

        if (docSuccess) {
            result.successFiles++;
        }
    }

    return result;
}

} // namespace wipepdf
