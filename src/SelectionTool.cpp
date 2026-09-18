#include "SelectionTool.h"
#include "WatermarkService.h"
#include <string>

namespace wipepdf {

AVTool SelectionTool::gSelectionTool = NULL;
AVTool SelectionTool::gPreviousTool = NULL;

void SelectionTool::RegisterTool() {
    gSelectionTool = (AVTool)ASmalloc(sizeof(AVToolRec));
    memset(gSelectionTool, 0, sizeof(AVToolRec));

    gSelectionTool->size = sizeof(AVToolRec);
    gSelectionTool->ComputeEnabled = ASCallbackCreateProto(AVComputeEnabledProc, &SelectionTool::ComputeEnabledProc);
    gSelectionTool->Activate = ASCallbackCreateProto(ActivateProcType, &SelectionTool::ActivateProc);
    gSelectionTool->Deactivate = ASCallbackCreateProto(DeactivateProcType, &SelectionTool::DeactivateProc);
    gSelectionTool->GetType = ASCallbackCreateProto(GetTypeProcType, &SelectionTool::GetTypeProc);
    gSelectionTool->DoClick = ASCallbackCreateProto(DoClickProcType, &SelectionTool::DoClickProc);
    }

void SelectionTool::UnregisterTool() {
    if (gSelectionTool) {
        ASfree(gSelectionTool);
        gSelectionTool = NULL;
    }
}

void SelectionTool::ActivateTool(AVTool prevTool) {
    gPreviousTool = prevTool;
    AVDoc avDoc = AVAppGetActiveDoc();
    if (avDoc) {
        AVAppSetActiveTool(gSelectionTool, false);
    }
}

ASBool ACCB1 SelectionTool::ComputeEnabledProc(void *data) {
    return true; 
}

void ACCB1 SelectionTool::ActivateProc(AVTool tool, ASBool persistent) {
}

void ACCB1 SelectionTool::DeactivateProc(AVTool tool) {
}

ASAtom ACCB1 SelectionTool::GetTypeProc(AVTool tool) {
    return ASAtomFromString("WipePDF:SelectionTool");
}


ASBool ACCB1 SelectionTool::DoClickProc(AVTool tool, AVPageView pageView, ASInt16 x, ASInt16 y, ASInt16 flags, ASInt16 clickNo) {
    HandleClick(pageView, x, y);
    
    // Revert to previous tool
    if (gPreviousTool) {
        AVAppSetActiveTool(gPreviousTool, false);
    } else {
        AVTool handTool = AVAppGetToolByName(ASAtomFromString("Hand"));
        if (handTool) {
            AVAppSetActiveTool(handTool, false);
        }
    }
    return true;
}

void SelectionTool::HandleClick(AVPageView pageView, ASInt16 x, ASInt16 y) {
    PDPage page = AVPageViewGetPage(pageView);
    if (!page) return;

    ASFixedPoint pagePt;
    AVPageViewDevicePointToPage(pageView, x, y, &pagePt);
    
    PDEContent content = PDPageAcquirePDEContent(page, gExtensionID);
    if (!content) return;

    ASInt32 numElems = PDEContentGetNumElems(content);
    PDEElement clickedElem = NULL;
    ASInt32 clickedType = -1;
    
    TargetFingerprint fp;

    for (ASInt32 i = numElems - 1; i >= 0; --i) {
        PDEElement elem = PDEContentGetElem(content, i);
        if (!elem) continue;

        ASFixedRect bbox;
        PDEElementGetBBox(elem, &bbox);

        if (pagePt.h >= bbox.left && pagePt.h <= bbox.right &&
            pagePt.v >= bbox.bottom && pagePt.v <= bbox.top) {
            
            clickedElem = elem;
            clickedType = PDEObjectGetType((PDEObject)elem);
            
            float bw = ASFixedToFloat(bbox.right - bbox.left);
            float bh = ASFixedToFloat(bbox.top - bbox.bottom);

            if (clickedType == kPDEImage) {
                fp.active = true;
                fp.type = kPDEImage;
                fp.width = bw;
                fp.height = bh;
                break;
            } else if (clickedType == kPDEPath) {
                fp.active = true;
                fp.type = kPDEPath;
                fp.bboxWidth = bw;
                fp.bboxHeight = bh;
                
                PDEGraphicState gState;
                memset(&gState, 0, sizeof(gState));
                PDEElementGetGState(elem, &gState, sizeof(gState));
                if (gState.fillColorSpec.space != NULL) {
                    ASAtom spName = PDEColorSpaceGetName(gState.fillColorSpec.space);
                    if (spName == ASAtomFromString("Pattern")) {
                        fp.isPattern = true;
                    }
                }
                break;
            } else if (clickedType == kPDEText) {
                fp.active = true;
                fp.type = kPDEText;
                
                ASInt32 len = PDETextGetText((PDEText)elem, kPDETextRun, 0, NULL);
                if (len > 0) {
                    std::string buf(len, '\0');
                    PDETextGetText((PDEText)elem, kPDETextRun, 0, (ASUns8*)&buf[0]);
                    fp.textContent = buf;
                }
                break;
            }
        }
    }
    
    PDPageReleasePDEContent(page, gExtensionID);

    if (fp.active) {
        std::wstring msg = L"找到目标元素特征：\n";
        if (fp.type == kPDEImage) {
            msg += L"  - 类型：图片\n  - 尺寸：" + std::to_wstring(fp.width) + L" x " + std::to_wstring(fp.height) + L"\n";
        } else if (fp.type == kPDEPath) {
            msg += L"  - 类型：矢量图形\n  - 尺寸：" + std::to_wstring(fp.bboxWidth) + L" x " + std::to_wstring(fp.bboxHeight) + L"\n";
            if (fp.isPattern) msg += L"  - 填充：图案填充\n";
        } else if (fp.type == kPDEText) {
            std::wstring wcontent(fp.textContent.begin(), fp.textContent.end());
            msg += L"  - 类型：文本\n  - 内容：" + wcontent + L"\n";
        }
        
        msg += L"\n是否在全篇文档中批量删除同款水印？";
        
        int res = MessageBoxW(NULL, msg.c_str(), L"确认删除同款水印", MB_YESNO | MB_ICONQUESTION);
        if (res == IDYES) {
            CleanOptions opts;
            opts.targetFingerprint = fp;
            opts.removeTransparentText = false;
            opts.removePatternFills = false;
            opts.removeKeywordText = false;
            opts.removeLinks = false;
            opts.removeBottomStrip = false;
            
            CleanResult cleanRes = WatermarkService::cleanActiveDocument(opts);
            
            std::wstring finishMsg = L"水印清理完成！\n\n共成功清除 " + std::to_wstring(cleanRes.totalRemoved) + L" 处同款水印元素。\n页面已即时刷新，您可以直接保存文档。";
            MessageBoxW(NULL, finishMsg.c_str(), L"WipePDF 水印清理成功", MB_OK | MB_ICONINFORMATION);
        }
    } else {
        MessageBoxW(NULL, L"未找到有效的水印元素。\n请确认点击的 PDF 元素存在且未被删除。", L"WipePDF 提示", MB_OK | MB_ICONWARNING);
    }
}

} // namespace wipepdf
