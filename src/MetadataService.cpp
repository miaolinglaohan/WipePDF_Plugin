#include "MetadataService.h"
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

} // namespace wipepdf
