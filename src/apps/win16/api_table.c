/* api_table.c -- GÉNÉRÉ par tools/win16/spec2c.py depuis les .spec de spec/ (winevdm, format de Wine) :
 * ne pas modifier à la main. */
#include "w16.h"

extern void api_A20Proc16 (struct w16call *c) __attribute__ ((weak));
extern void api_A20_Proc16 (struct w16call *c) __attribute__ ((weak));
extern void api_AbortDoc16 (struct w16call *c) __attribute__ ((weak));
extern void api_AbortPath16 (struct w16call *c) __attribute__ ((weak));
extern void api_AboutDlgProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_AccessResource16 (struct w16call *c) __attribute__ ((weak));
extern void api_AddAtom16 (struct w16call *c) __attribute__ ((weak));
extern void api_AddFontResource16 (struct w16call *c) __attribute__ ((weak));
extern void api_AdjustWindowRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_AdjustWindowRectEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_AllocCStoDSAlias16 (struct w16call *c) __attribute__ ((weak));
extern void api_AllocDStoCSAlias16 (struct w16call *c) __attribute__ ((weak));
extern void api_AllocLSThunkletCallback16 (struct w16call *c) __attribute__ ((weak));
extern void api_AllocLSThunkletCallbackEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_AllocLSThunkletSysthunk16 (struct w16call *c) __attribute__ ((weak));
extern void api_AllocResource16 (struct w16call *c) __attribute__ ((weak));
extern void api_AllocSLThunkletCallback16 (struct w16call *c) __attribute__ ((weak));
extern void api_AllocSLThunkletCallbackEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_AllocSLThunkletSysthunk16 (struct w16call *c) __attribute__ ((weak));
extern void api_AllocSelector16 (struct w16call *c) __attribute__ ((weak));
extern void api_AllocSelectorArray16 (struct w16call *c) __attribute__ ((weak));
extern void api_AnimatePalette16 (struct w16call *c) __attribute__ ((weak));
extern void api_AnsiLower16 (struct w16call *c) __attribute__ ((weak));
extern void api_AnsiLowerBuff16 (struct w16call *c) __attribute__ ((weak));
extern void api_AnsiNext16 (struct w16call *c) __attribute__ ((weak));
extern void api_AnsiPrev16 (struct w16call *c) __attribute__ ((weak));
extern void api_AnsiToOem16 (struct w16call *c) __attribute__ ((weak));
extern void api_AnsiToOemBuff16 (struct w16call *c) __attribute__ ((weak));
extern void api_AnsiUpper16 (struct w16call *c) __attribute__ ((weak));
extern void api_AnsiUpperBuff16 (struct w16call *c) __attribute__ ((weak));
extern void api_AnyPopup16 (struct w16call *c) __attribute__ ((weak));
extern void api_AppendMenu16 (struct w16call *c) __attribute__ ((weak));
extern void api_Arc16 (struct w16call *c) __attribute__ ((weak));
extern void api_ArrangeIconicWindows16 (struct w16call *c) __attribute__ ((weak));
extern void api_BeginDeferWindowPos16 (struct w16call *c) __attribute__ ((weak));
extern void api_BeginPaint16 (struct w16call *c) __attribute__ ((weak));
extern void api_BeginPath16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiBounds16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiCalcString16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiCalcStringEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiCalcTabString16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiCalcTabStringEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiCaretPelToString16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiCaretStringTopel16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiCreateString16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiCreateStringEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiCreateTabString16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiCreateTabStringEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiDeleteString16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiFontComplement16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiGetDefaults16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiGetNextGlyph16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiGetPrevGlyph16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiGetStringExtent16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiGlyphLength16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiGlyphOut16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiGlyphToPel16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiGlyphToString16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiIsStringRTL16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiJustifyString16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiKExtTextOut16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiLayout16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiLayoutEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiPelToGlyph16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiSetDefaults16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiSetKashida16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiSetStringTabs16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiShape16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiShapeEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiStringOut16 (struct w16call *c) __attribute__ ((weak));
extern void api_BiDiStringToGlyph16 (struct w16call *c) __attribute__ ((weak));
extern void api_BitBlt16 (struct w16call *c) __attribute__ ((weak));
extern void api_BringWindowToTop16 (struct w16call *c) __attribute__ ((weak));
extern void api_BuildCommDCB16 (struct w16call *c) __attribute__ ((weak));
extern void api_C16ThkSL (struct w16call *c) __attribute__ ((weak));
extern void api_C16ThkSL01 (struct w16call *c) __attribute__ ((weak));
extern void api_CBClientGlueSL (struct w16call *c) __attribute__ ((weak));
extern void api_CBClientThunkSL (struct w16call *c) __attribute__ ((weak));
extern void api_CBClientThunkSLEx (struct w16call *c) __attribute__ ((weak));
extern void api_CalcChildScroll16 (struct w16call *c) __attribute__ ((weak));
extern void api_CallMsgFilter16 (struct w16call *c) __attribute__ ((weak));
extern void api_CallMsgFilter32_16 (struct w16call *c) __attribute__ ((weak));
extern void api_CallNextHookEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_CallProc32W16 (struct w16call *c) __attribute__ ((weak));
extern void api_CallProcEx32W16 (struct w16call *c) __attribute__ ((weak));
extern void api_CallWindowProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_CascadeChildWindows16 (struct w16call *c) __attribute__ ((weak));
extern void api_Catch16 (struct w16call *c) __attribute__ ((weak));
extern void api_ChangeClipboardChain16 (struct w16call *c) __attribute__ ((weak));
extern void api_ChangeDialogTemplate16 (struct w16call *c) __attribute__ ((weak));
extern void api_ChangeDisplaySettings16 (struct w16call *c) __attribute__ ((weak));
extern void api_ChangeKeyboardCodePage16 (struct w16call *c) __attribute__ ((weak));
extern void api_ChangeKeyboardLanguage16 (struct w16call *c) __attribute__ ((weak));
extern void api_ChangeMenu16 (struct w16call *c) __attribute__ ((weak));
extern void api_CheckDlgButton16 (struct w16call *c) __attribute__ ((weak));
extern void api_CheckMenuItem16 (struct w16call *c) __attribute__ ((weak));
extern void api_CheckMenuRadioItem16 (struct w16call *c) __attribute__ ((weak));
extern void api_CheckRadioButton16 (struct w16call *c) __attribute__ ((weak));
extern void api_ChildWindowFromPoint16 (struct w16call *c) __attribute__ ((weak));
extern void api_ChildWindowFromPointEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_ChooseColor16 (struct w16call *c) __attribute__ ((weak));
extern void api_ChooseFont16 (struct w16call *c) __attribute__ ((weak));
extern void api_Chord16 (struct w16call *c) __attribute__ ((weak));
extern void api_ClassFirst16 (struct w16call *c) __attribute__ ((weak));
extern void api_ClassNext16 (struct w16call *c) __attribute__ ((weak));
extern void api_ClearCommBreak16 (struct w16call *c) __attribute__ ((weak));
extern void api_ClientToScreen16 (struct w16call *c) __attribute__ ((weak));
extern void api_ClipCursor16 (struct w16call *c) __attribute__ ((weak));
extern void api_CloseClipboard16 (struct w16call *c) __attribute__ ((weak));
extern void api_CloseComm16 (struct w16call *c) __attribute__ ((weak));
extern void api_CloseDriver16 (struct w16call *c) __attribute__ ((weak));
extern void api_CloseFigure16 (struct w16call *c) __attribute__ ((weak));
extern void api_CloseJob16 (struct w16call *c) __attribute__ ((weak));
extern void api_CloseMetaFile16 (struct w16call *c) __attribute__ ((weak));
extern void api_CloseSound16 (struct w16call *c) __attribute__ ((weak));
extern void api_CloseW32Handle (struct w16call *c) __attribute__ ((weak));
extern void api_CloseWindow16 (struct w16call *c) __attribute__ ((weak));
extern void api_ColorDlgProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_CombineRgn16 (struct w16call *c) __attribute__ ((weak));
extern void api_CommDlgExtendedError16 (struct w16call *c) __attribute__ ((weak));
extern void api_ControlPanelInfo16 (struct w16call *c) __attribute__ ((weak));
extern void api_ConvertDialog32To16 (struct w16call *c) __attribute__ ((weak));
extern void api_ConvertMenu32To16 (struct w16call *c) __attribute__ ((weak));
extern void api_ConvertToGlobalHandle16 (struct w16call *c) __attribute__ ((weak));
extern void api_Copy16 (struct w16call *c) __attribute__ ((weak));
extern void api_CopyCursor16 (struct w16call *c) __attribute__ ((weak));
extern void api_CopyIcon16 (struct w16call *c) __attribute__ ((weak));
extern void api_CopyImage16 (struct w16call *c) __attribute__ ((weak));
extern void api_CopyLZFile16 (struct w16call *c) __attribute__ ((weak));
extern void api_CopyMetaFile16 (struct w16call *c) __attribute__ ((weak));
extern void api_CopyRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_CountClipboardFormats16 (struct w16call *c) __attribute__ ((weak));
extern void api_CountVoiceNotes16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateBitmap16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateBitmapIndirect16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateBrushIndirect16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateCaret16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateCompatibleBitmap16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateCompatibleDC16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateCursor16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateCursorIconIndirect16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateDC16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateDIBPatternBrush16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateDIBSection16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateDIBitmap16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateDialog16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateDialogIndirect16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateDialogIndirectParam16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateDialogIndirectParamML16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateDialogParam16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateDialogParamML16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateDirectory16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateDiscardableBitmap16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateEllipticRgn16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateEllipticRgnIndirect16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateFont16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateFontIndirect16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateHalftonePalette16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateHatchBrush16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateIC16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateIcon16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateIconFromResourceEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateMenu16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateMetaFile16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreatePQ16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreatePalette16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreatePatternBrush16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreatePen16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreatePenIndirect16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreatePolyPolygonRgn16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreatePolygonRgn16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreatePopupMenu16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateRectRgn16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateRectRgnIndirect16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateRoundRectRgn16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateScalableFontResource16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateSolidBrush16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateSystemTimer16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateThread16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateUserBitmap16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateUserDiscardableBitmap16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateW32Event (struct w16call *c) __attribute__ ((weak));
extern void api_CreateWindow16 (struct w16call *c) __attribute__ ((weak));
extern void api_CreateWindowEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_DCHook16 (struct w16call *c) __attribute__ ((weak));
extern void api_DOS3Call (struct w16call *c) __attribute__ ((weak));
extern void api_DPtoLP16 (struct w16call *c) __attribute__ ((weak));
extern void api_Death16 (struct w16call *c) __attribute__ ((weak));
extern void api_DebugBreak16 (struct w16call *c) __attribute__ ((weak));
extern void api_DebugFillBuffer (struct w16call *c) __attribute__ ((weak));
extern void api_DefDlgProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_DefDriverProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_DefFrameProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_DefHookProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_DefMDIChildProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_DefWindowProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_DeferWindowPos16 (struct w16call *c) __attribute__ ((weak));
extern void api_DefineHandleTable16 (struct w16call *c) __attribute__ ((weak));
extern void api_DeleteAtom16 (struct w16call *c) __attribute__ ((weak));
extern void api_DeleteDC16 (struct w16call *c) __attribute__ ((weak));
extern void api_DeleteFile16 (struct w16call *c) __attribute__ ((weak));
extern void api_DeleteJob16 (struct w16call *c) __attribute__ ((weak));
extern void api_DeleteMenu16 (struct w16call *c) __attribute__ ((weak));
extern void api_DeleteMetaFile16 (struct w16call *c) __attribute__ ((weak));
extern void api_DeleteObject16 (struct w16call *c) __attribute__ ((weak));
extern void api_DeletePQ16 (struct w16call *c) __attribute__ ((weak));
extern void api_DestroyCaret16 (struct w16call *c) __attribute__ ((weak));
extern void api_DestroyCursor16 (struct w16call *c) __attribute__ ((weak));
extern void api_DestroyIcon16 (struct w16call *c) __attribute__ ((weak));
extern void api_DestroyIcon32 (struct w16call *c) __attribute__ ((weak));
extern void api_DestroyMenu16 (struct w16call *c) __attribute__ ((weak));
extern void api_DestroyWindow16 (struct w16call *c) __attribute__ ((weak));
extern void api_DiagOutput16 (struct w16call *c) __attribute__ ((weak));
extern void api_DiagQuery16 (struct w16call *c) __attribute__ ((weak));
extern void api_DialogBox16 (struct w16call *c) __attribute__ ((weak));
extern void api_DialogBoxIndirect16 (struct w16call *c) __attribute__ ((weak));
extern void api_DialogBoxIndirectParam16 (struct w16call *c) __attribute__ ((weak));
extern void api_DialogBoxIndirectParamML16 (struct w16call *c) __attribute__ ((weak));
extern void api_DialogBoxParam16 (struct w16call *c) __attribute__ ((weak));
extern void api_DirectResAlloc16 (struct w16call *c) __attribute__ ((weak));
extern void api_DirectedYield16 (struct w16call *c) __attribute__ ((weak));
extern void api_Disable16 (struct w16call *c) __attribute__ ((weak));
extern void api_DisableSystemTimers16 (struct w16call *c) __attribute__ ((weak));
extern void api_DispatchMessage16 (struct w16call *c) __attribute__ ((weak));
extern void api_DispatchMessage32_16 (struct w16call *c) __attribute__ ((weak));
extern void api_DlgDirList16 (struct w16call *c) __attribute__ ((weak));
extern void api_DlgDirListComboBox16 (struct w16call *c) __attribute__ ((weak));
extern void api_DlgDirSelect16 (struct w16call *c) __attribute__ ((weak));
extern void api_DlgDirSelectComboBox16 (struct w16call *c) __attribute__ ((weak));
extern void api_DlgDirSelectComboBoxEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_DlgDirSelectEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_DllEntryPoint (struct w16call *c) __attribute__ ((weak));
extern void api_DoBeep16 (struct w16call *c) __attribute__ ((weak));
extern void api_DoEnvironmentSubst16 (struct w16call *c) __attribute__ ((weak));
extern void api_DragAcceptFiles16 (struct w16call *c) __attribute__ ((weak));
extern void api_DragDetect16 (struct w16call *c) __attribute__ ((weak));
extern void api_DragFinish16 (struct w16call *c) __attribute__ ((weak));
extern void api_DragObject16 (struct w16call *c) __attribute__ ((weak));
extern void api_DragQueryFile16 (struct w16call *c) __attribute__ ((weak));
extern void api_DragQueryPoint16 (struct w16call *c) __attribute__ ((weak));
extern void api_DrawAnimatedRects16 (struct w16call *c) __attribute__ ((weak));
extern void api_DrawCaption16 (struct w16call *c) __attribute__ ((weak));
extern void api_DrawCaptionTemp16 (struct w16call *c) __attribute__ ((weak));
extern void api_DrawEdge16 (struct w16call *c) __attribute__ ((weak));
extern void api_DrawFocusRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_DrawFrameControl16 (struct w16call *c) __attribute__ ((weak));
extern void api_DrawIcon16 (struct w16call *c) __attribute__ ((weak));
extern void api_DrawIconEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_DrawMenuBar16 (struct w16call *c) __attribute__ ((weak));
extern void api_DrawState16 (struct w16call *c) __attribute__ ((weak));
extern void api_DrawText16 (struct w16call *c) __attribute__ ((weak));
extern void api_DriveType16 (struct w16call *c) __attribute__ ((weak));
extern void api_DriverCallback16 (struct w16call *c) __attribute__ ((weak));
extern void api_DriverProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_DrvClose16 (struct w16call *c) __attribute__ ((weak));
extern void api_DrvDefDriverProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_DrvGetModuleHandle16 (struct w16call *c) __attribute__ ((weak));
extern void api_DrvGetPrinterData16 (struct w16call *c) __attribute__ ((weak));
extern void api_DrvOpen16 (struct w16call *c) __attribute__ ((weak));
extern void api_DrvSendMessage16 (struct w16call *c) __attribute__ ((weak));
extern void api_DrvSetPrinterData16 (struct w16call *c) __attribute__ ((weak));
extern void api_DumpIcon16 (struct w16call *c) __attribute__ ((weak));
extern void api_Ellipse16 (struct w16call *c) __attribute__ ((weak));
extern void api_EmptyClipboard16 (struct w16call *c) __attribute__ ((weak));
extern void api_Enable16 (struct w16call *c) __attribute__ ((weak));
extern void api_EnableCommNotification16 (struct w16call *c) __attribute__ ((weak));
extern void api_EnableHardwareInput16 (struct w16call *c) __attribute__ ((weak));
extern void api_EnableMenuItem16 (struct w16call *c) __attribute__ ((weak));
extern void api_EnableScrollBar16 (struct w16call *c) __attribute__ ((weak));
extern void api_EnableSystemTimers16 (struct w16call *c) __attribute__ ((weak));
extern void api_EnableWindow16 (struct w16call *c) __attribute__ ((weak));
extern void api_EndDeferWindowPos16 (struct w16call *c) __attribute__ ((weak));
extern void api_EndDialog16 (struct w16call *c) __attribute__ ((weak));
extern void api_EndDoc16 (struct w16call *c) __attribute__ ((weak));
extern void api_EndMenu (struct w16call *c) __attribute__ ((weak));
extern void api_EndPage16 (struct w16call *c) __attribute__ ((weak));
extern void api_EndPaint16 (struct w16call *c) __attribute__ ((weak));
extern void api_EndPath16 (struct w16call *c) __attribute__ ((weak));
extern void api_EndSpoolPage16 (struct w16call *c) __attribute__ ((weak));
extern void api_EngineDeleteFont16 (struct w16call *c) __attribute__ ((weak));
extern void api_EngineEnumerateFont16 (struct w16call *c) __attribute__ ((weak));
extern void api_EngineGetCharWidth16 (struct w16call *c) __attribute__ ((weak));
extern void api_EngineGetGlyphBMP16 (struct w16call *c) __attribute__ ((weak));
extern void api_EngineMakeFontDir16 (struct w16call *c) __attribute__ ((weak));
extern void api_EngineRealizeFont16 (struct w16call *c) __attribute__ ((weak));
extern void api_EngineRealizeFontExt16 (struct w16call *c) __attribute__ ((weak));
extern void api_EngineSetFontContext16 (struct w16call *c) __attribute__ ((weak));
extern void api_EntryAddrProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_EnumChildWindows16 (struct w16call *c) __attribute__ ((weak));
extern void api_EnumClipboardFormats16 (struct w16call *c) __attribute__ ((weak));
extern void api_EnumDisplaySettings16 (struct w16call *c) __attribute__ ((weak));
extern void api_EnumFontFamilies16 (struct w16call *c) __attribute__ ((weak));
extern void api_EnumFontFamiliesEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_EnumFonts16 (struct w16call *c) __attribute__ ((weak));
extern void api_EnumMetaFile16 (struct w16call *c) __attribute__ ((weak));
extern void api_EnumObjects16 (struct w16call *c) __attribute__ ((weak));
extern void api_EnumProps16 (struct w16call *c) __attribute__ ((weak));
extern void api_EnumTaskWindows16 (struct w16call *c) __attribute__ ((weak));
extern void api_EnumWindows16 (struct w16call *c) __attribute__ ((weak));
extern void api_EqualRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_EqualRgn16 (struct w16call *c) __attribute__ ((weak));
extern void api_Escape16 (struct w16call *c) __attribute__ ((weak));
extern void api_EscapeCommFunction16 (struct w16call *c) __attribute__ ((weak));
extern void api_ExcludeClipRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_ExcludeUpdateRgn16 (struct w16call *c) __attribute__ ((weak));
extern void api_ExcludeVisRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_ExitKernel16 (struct w16call *c) __attribute__ ((weak));
extern void api_ExitProcess16 (struct w16call *c) __attribute__ ((weak));
extern void api_ExitWindows16 (struct w16call *c) __attribute__ ((weak));
extern void api_ExitWindowsExec16 (struct w16call *c) __attribute__ ((weak));
extern void api_ExtFloodFill16 (struct w16call *c) __attribute__ ((weak));
extern void api_ExtSelectClipRgn16 (struct w16call *c) __attribute__ ((weak));
extern void api_ExtTextOut16 (struct w16call *c) __attribute__ ((weak));
extern void api_ExtractAssociatedIcon16 (struct w16call *c) __attribute__ ((weak));
extern void api_ExtractIcon16 (struct w16call *c) __attribute__ ((weak));
extern void api_ExtractIconEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_ExtractPQ16 (struct w16call *c) __attribute__ ((weak));
extern void api_FarGetOwner16 (struct w16call *c) __attribute__ ((weak));
extern void api_FarSetOwner16 (struct w16call *c) __attribute__ ((weak));
extern void api_FastWindowFrame16 (struct w16call *c) __attribute__ ((weak));
extern void api_FatalAppExit16 (struct w16call *c) __attribute__ ((weak));
extern void api_FileCDR16 (struct w16call *c) __attribute__ ((weak));
extern void api_FileOpenDlgProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_FileSaveDlgProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_FillPath16 (struct w16call *c) __attribute__ ((weak));
extern void api_FillRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_FillRgn16 (struct w16call *c) __attribute__ ((weak));
extern void api_FillWindow16 (struct w16call *c) __attribute__ ((weak));
extern void api_FinalGdiInit16 (struct w16call *c) __attribute__ ((weak));
extern void api_FinalUserInit16 (struct w16call *c) __attribute__ ((weak));
extern void api_FindAtom16 (struct w16call *c) __attribute__ ((weak));
extern void api_FindClose16 (struct w16call *c) __attribute__ ((weak));
extern void api_FindEnvironmentString16 (struct w16call *c) __attribute__ ((weak));
extern void api_FindExecutable16 (struct w16call *c) __attribute__ ((weak));
extern void api_FindFirstFile16 (struct w16call *c) __attribute__ ((weak));
extern void api_FindLSThunkletCallback (struct w16call *c) __attribute__ ((weak));
extern void api_FindLanguageResource16 (struct w16call *c) __attribute__ ((weak));
extern void api_FindNextFile16 (struct w16call *c) __attribute__ ((weak));
extern void api_FindResource16 (struct w16call *c) __attribute__ ((weak));
extern void api_FindResourceEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_FindSLThunkletCallback (struct w16call *c) __attribute__ ((weak));
extern void api_FindText16 (struct w16call *c) __attribute__ ((weak));
extern void api_FindTextDlgProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_FindWindow16 (struct w16call *c) __attribute__ ((weak));
extern void api_FindWindowEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_FlashWindow16 (struct w16call *c) __attribute__ ((weak));
extern void api_FlattenPath16 (struct w16call *c) __attribute__ ((weak));
extern void api_FloodFill16 (struct w16call *c) __attribute__ ((weak));
extern void api_FlushComm16 (struct w16call *c) __attribute__ ((weak));
extern void api_FontFamilyEnumProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_FontStyleEnumProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_FormatCharDlgProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_FormatMessage16 (struct w16call *c) __attribute__ ((weak));
extern void api_FrameRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_FrameRgn16 (struct w16call *c) __attribute__ ((weak));
extern void api_FreeLibrary32W16 (struct w16call *c) __attribute__ ((weak));
extern void api_FreeLibrary32_16 (struct w16call *c) __attribute__ ((weak));
extern void api_FreeModule16 (struct w16call *c) __attribute__ ((weak));
extern void api_FreeProcInstance16 (struct w16call *c) __attribute__ ((weak));
extern void api_FreeResource16 (struct w16call *c) __attribute__ ((weak));
extern void api_FreeThunklet16 (struct w16call *c) __attribute__ ((weak));
extern void api_GDIRealizePalette16 (struct w16call *c) __attribute__ ((weak));
extern void api_GDISelectPalette16 (struct w16call *c) __attribute__ ((weak));
extern void api_GdiFreeResources16 (struct w16call *c) __attribute__ ((weak));
extern void api_GdiInit216 (struct w16call *c) __attribute__ ((weak));
extern void api_GdiMoveBitmap16 (struct w16call *c) __attribute__ ((weak));
extern void api_GdiSeeGdiDo16 (struct w16call *c) __attribute__ ((weak));
extern void api_GdiSignalProc (struct w16call *c) __attribute__ ((weak));
extern void api_GdiTaskTermination16 (struct w16call *c) __attribute__ ((weak));
extern void api_Get80x87SaveSize16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetActiveWindow16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetAppCodePage16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetAppCompatFlags16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetArcDirection16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetAspectRatioFilter16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetAspectRatioFilterEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetAsyncKeyState16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetAtomHandle16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetAtomName16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetBaseCodePage16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetBitmapBits16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetBitmapDimension16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetBitmapDimensionEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetBkColor16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetBkMode16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetBoundsRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetBrushOrg16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetBrushOrgEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetCapture16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetCaretBlinkTime16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetCaretPos16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetCharABCWidths16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetCharWidth16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetClassInfo16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetClassInfoEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetClassLong16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetClassName16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetClassWord16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetClientRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetClipBox16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetClipCursor16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetClipRgn16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetClipboardCodePage16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetClipboardData16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetClipboardFormatName16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetClipboardOwner16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetClipboardViewer16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetCodeHandle16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetCodeInfo16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetCodePageSystemFont16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetCommError16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetCommEventMask16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetCommState16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetControlBrush16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetCurLogFont16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetCurPID16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetCurrentDirectory16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetCurrentPDB16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetCurrentPosition16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetCurrentPositionEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetCurrentProcessId16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetCurrentThreadId16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetCursor16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetCursorPos16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetDC16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetDCEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetDCHook16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetDCOrg16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetDCState16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetDIBColorTable16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetDIBits16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetDOSEnvironment16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetDesktopHwnd16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetDesktopWindow16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetDeviceCaps16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetDialog32Size16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetDialogBaseUnits16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetDiskFreeSpace16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetDlgCtrlID16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetDlgItem16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetDlgItemInt16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetDlgItemText16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetDoubleClickTime16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetDriveType16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetDriverInfo16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetDriverModuleHandle16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetDummyModuleHandleDS16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetEnvironment16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetExePtr (struct w16call *c) __attribute__ ((weak));
extern void api_GetExeVersion16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetExpWinVer16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetExpandedName16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetFastQueue16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetFileAttributes16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetFileResource16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetFileResourceSize16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetFileTitle16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetFileVersionInfo16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetFileVersionInfoSize16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetFocus16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetFontAssocStatus16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetFontData16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetFontLanguageInfo16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetForegroundWindow16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetFreeMemInfo16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetFreeSpace16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetFreeSystemResources16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetGlyphOutline16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetHDCCharSet16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetHeapSpaces16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetIconID16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetIconInfo16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetInputState16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetInstanceData16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetInternalWindowPos16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetKBCodePage16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetKerningPairs16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetKeyNameText16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetKeyState16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetKeyboardLanguage16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetKeyboardLayoutName16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetKeyboardState16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetKeyboardType16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetLanguageName16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetLanguageResId16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetLastActivePopup16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetLastError16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetMapMode16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetMenu16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetMenu32Size16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetMenuCheckMarkDimensions (struct w16call *c) __attribute__ ((weak));
extern void api_GetMenuContextHelpId16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetMenuItemCount16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetMenuItemID16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetMenuItemRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetMenuState16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetMenuString16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetMessage16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetMessage32_16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetMessageExtraInfo (struct w16call *c) __attribute__ ((weak));
extern void api_GetMessagePos (struct w16call *c) __attribute__ ((weak));
extern void api_GetMessageTime (struct w16call *c) __attribute__ ((weak));
extern void api_GetMetaFile16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetMetaFileBits16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetModuleFileName16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetModuleFileName32_16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetModuleHandle32_16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetModuleName16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetModuleUsage16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetMouseEventProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetNearestColor16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetNearestPaletteIndex16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetNextDlgGroupItem16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetNextDlgTabItem16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetNextDriver16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetNextWindow16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetNumKeyboards16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetNumLanguages16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetNumTasks16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetObject16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetOpenClipboardWindow16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetOpenFileName16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetOutlineTextMetrics16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetPaletteEntries16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetParent16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetPath16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetPhysicalFontHandle16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetPixel16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetPolyFillMode16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetPriorityClipboardFormat16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetPrivateProfileInt16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetPrivateProfileSection16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetPrivateProfileSectionNames16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetPrivateProfileString16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetPrivateProfileStruct16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetProcAddress32W16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetProcAddress32_16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetProcessDefaultLayout16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetProcessDword (struct w16call *c) __attribute__ ((weak));
extern void api_GetProfileInt16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetProfileSection16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetProfileSectionNames16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetProfileString16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetProp16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetQueueStatus16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetROP216 (struct w16call *c) __attribute__ ((weak));
extern void api_GetRasterizerCaps16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetRegionData16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetRelAbs16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetRgnBox16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetSaveFileName16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetScrollInfo16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetScrollPos16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetScrollRange16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetSelectorBase (struct w16call *c) __attribute__ ((weak));
extern void api_GetSelectorLimit16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetSetKernelDOSProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetShellWindow16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetShortPathName16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetSpoolJob16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetStockObject16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetStretchBltMode16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetSubMenu16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetSysColor16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetSysColorBrush16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetSysModalWindow16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetSystemDebugState16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetSystemDirectory16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetSystemIconFont16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetSystemMSecCount16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetSystemMenu16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetSystemMetrics16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetSystemPaletteEntries16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetSystemPaletteUse16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetTEBSelectorFS16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetTabbedTextExtent16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetTaskDS16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetTaskQueue16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetTaskQueueDS16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetTaskQueueES16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetTempDrive (struct w16call *c) __attribute__ ((weak));
extern void api_GetTempFileName16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetTextAlign16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetTextCharacterExtra16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetTextCharset16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetTextCodePage16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetTextColor16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetTextExtent16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetTextExtentPoint16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetTextExtentRtoL16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetTextFace16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetTextMetrics16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetThreadQueue16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetThresholdEvent16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetThresholdStatus16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetTickCount (struct w16call *c) __attribute__ ((weak));
extern void api_GetTimerResolution16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetTopWindow16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetUnicodeMap16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetUpdateRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetUpdateRgn16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetVDMPointer32W16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetVersion16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetVersionEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetViewportExt16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetViewportExtEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetViewportOrg16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetViewportOrgEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetWinDebugInfo16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetWinFlags16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetWindow16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetWindowDC16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetWindowExt16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetWindowExtEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetWindowLong16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetWindowOrg16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetWindowOrgEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetWindowPlacement16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetWindowRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetWindowTask16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetWindowText16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetWindowTextLength16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetWindowWord16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetWindowsDirectory16 (struct w16call *c) __attribute__ ((weak));
extern void api_GetpWin16Lock16 (struct w16call *c) __attribute__ ((weak));
extern void api_GlobalAddAtom16 (struct w16call *c) __attribute__ ((weak));
extern void api_GlobalChangeLockCount16 (struct w16call *c) __attribute__ ((weak));
extern void api_GlobalCompact16 (struct w16call *c) __attribute__ ((weak));
extern void api_GlobalDOSAlloc16 (struct w16call *c) __attribute__ ((weak));
extern void api_GlobalDOSFree16 (struct w16call *c) __attribute__ ((weak));
extern void api_GlobalDeleteAtom16 (struct w16call *c) __attribute__ ((weak));
extern void api_GlobalEntryHandle16 (struct w16call *c) __attribute__ ((weak));
extern void api_GlobalEntryModule16 (struct w16call *c) __attribute__ ((weak));
extern void api_GlobalFindAtom16 (struct w16call *c) __attribute__ ((weak));
extern void api_GlobalFirst16 (struct w16call *c) __attribute__ ((weak));
extern void api_GlobalFix16 (struct w16call *c) __attribute__ ((weak));
extern void api_GlobalFreeAll16 (struct w16call *c) __attribute__ ((weak));
extern void api_GlobalGetAtomName16 (struct w16call *c) __attribute__ ((weak));
extern void api_GlobalHandle16 (struct w16call *c) __attribute__ ((weak));
extern void api_GlobalHandleNoRIP16 (struct w16call *c) __attribute__ ((weak));
extern void api_GlobalHandleToSel16 (struct w16call *c) __attribute__ ((weak));
extern void api_GlobalInfo16 (struct w16call *c) __attribute__ ((weak));
extern void api_GlobalLRUNewest16 (struct w16call *c) __attribute__ ((weak));
extern void api_GlobalLRUOldest16 (struct w16call *c) __attribute__ ((weak));
extern void api_GlobalMasterHandle16 (struct w16call *c) __attribute__ ((weak));
extern void api_GlobalNext16 (struct w16call *c) __attribute__ ((weak));
extern void api_GlobalNotify16 (struct w16call *c) __attribute__ ((weak));
extern void api_GlobalPageLock16 (struct w16call *c) __attribute__ ((weak));
extern void api_GlobalPageUnlock16 (struct w16call *c) __attribute__ ((weak));
extern void api_GlobalUnWire16 (struct w16call *c) __attribute__ ((weak));
extern void api_GlobalUnfix16 (struct w16call *c) __attribute__ ((weak));
extern void api_GlobalWire16 (struct w16call *c) __attribute__ ((weak));
extern void api_GrayString16 (struct w16call *c) __attribute__ ((weak));
extern void api_HandleParamError (struct w16call *c) __attribute__ ((weak));
extern void api_HasGPHandler16 (struct w16call *c) __attribute__ ((weak));
extern void api_HideCaret16 (struct w16call *c) __attribute__ ((weak));
extern void api_HiliteMenuItem16 (struct w16call *c) __attribute__ ((weak));
extern void api_IconSize16 (struct w16call *c) __attribute__ ((weak));
extern void api_InSendMessage16 (struct w16call *c) __attribute__ ((weak));
extern void api_InflateRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_InitApp16 (struct w16call *c) __attribute__ ((weak));
extern void api_InitAtomTable16 (struct w16call *c) __attribute__ ((weak));
extern void api_InitCBClient16 (struct w16call *c) __attribute__ ((weak));
extern void api_InitTask16 (struct w16call *c) __attribute__ ((weak));
extern void api_InitThreadInput16 (struct w16call *c) __attribute__ ((weak));
extern void api_Inquire16 (struct w16call *c) __attribute__ ((weak));
extern void api_InquireSystem16 (struct w16call *c) __attribute__ ((weak));
extern void api_InquireVisRgn16 (struct w16call *c) __attribute__ ((weak));
extern void api_InsertMenu16 (struct w16call *c) __attribute__ ((weak));
extern void api_InsertMenuItem16 (struct w16call *c) __attribute__ ((weak));
extern void api_InsertPQ16 (struct w16call *c) __attribute__ ((weak));
extern void api_InternalExtractIcon16 (struct w16call *c) __attribute__ ((weak));
extern void api_InterruptRegister16 (struct w16call *c) __attribute__ ((weak));
extern void api_InterruptUnRegister16 (struct w16call *c) __attribute__ ((weak));
extern void api_IntersectClipRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_IntersectRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_IntersectVisRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_InvalidateRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_InvalidateRgn16 (struct w16call *c) __attribute__ ((weak));
extern void api_InvertRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_InvertRgn16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsBadCodePtr16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsBadFlatReadWritePtr16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsBadHugeReadPtr16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsBadHugeWritePtr16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsBadReadPtr16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsBadStringPtr16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsBadWritePtr16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsCharAlphaA (struct w16call *c) __attribute__ ((weak));
extern void api_IsCharAlphaNumericA (struct w16call *c) __attribute__ ((weak));
extern void api_IsCharArabic16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsCharFrench16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsCharHebrew16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsCharLowerA (struct w16call *c) __attribute__ ((weak));
extern void api_IsCharUpperA (struct w16call *c) __attribute__ ((weak));
extern void api_IsChild16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsClipboardFormatAvailable16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsDBCSLeadByte (struct w16call *c) __attribute__ ((weak));
extern void api_IsDCCurrentPalette16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsDCDirty16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsDialogMessage16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsDlgButtonChecked16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsGDIObject16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsIconic16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsMenu16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsPeFormat16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsRectEmpty16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsRomFile16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsRomModule16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsSLThunklet16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsSharedSelector16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsTask16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsTaskLocked16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsUserIdle16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsValidMetaFile16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsWinOldApTask16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsWindow16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsWindowEnabled16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsWindowVisible16 (struct w16call *c) __attribute__ ((weak));
extern void api_IsZoomed16 (struct w16call *c) __attribute__ ((weak));
extern void api_KERNEL_490 (struct w16call *c) __attribute__ ((weak));
extern void api_KERNEL_AnsiLower16 (struct w16call *c) __attribute__ ((weak));
extern void api_KERNEL_AnsiNext16 (struct w16call *c) __attribute__ ((weak));
extern void api_KERNEL_AnsiPrev16 (struct w16call *c) __attribute__ ((weak));
extern void api_KERNEL_AnsiUpper16 (struct w16call *c) __attribute__ ((weak));
extern void api_KERNEL_DllEntryPoint (struct w16call *c) __attribute__ ((weak));
extern void api_KERNEL_lstrcmp16 (struct w16call *c) __attribute__ ((weak));
extern void api_KERNEL_nop (struct w16call *c) __attribute__ ((weak));
extern void api_KillSystemTimer16 (struct w16call *c) __attribute__ ((weak));
extern void api_KillTimer16 (struct w16call *c) __attribute__ ((weak));
extern void api_LPtoDP16 (struct w16call *c) __attribute__ ((weak));
extern void api_LZClose16 (struct w16call *c) __attribute__ ((weak));
extern void api_LZCopy16 (struct w16call *c) __attribute__ ((weak));
extern void api_LZDone16 (struct w16call *c) __attribute__ ((weak));
extern void api_LZInit16 (struct w16call *c) __attribute__ ((weak));
extern void api_LZOpenFile16 (struct w16call *c) __attribute__ ((weak));
extern void api_LZRead16 (struct w16call *c) __attribute__ ((weak));
extern void api_LZSeek16 (struct w16call *c) __attribute__ ((weak));
extern void api_LZStart16 (struct w16call *c) __attribute__ ((weak));
extern void api_LangToBestCodePage16 (struct w16call *c) __attribute__ ((weak));
extern void api_LimitEMSPages16 (struct w16call *c) __attribute__ ((weak));
extern void api_LineDDA16 (struct w16call *c) __attribute__ ((weak));
extern void api_LineTo16 (struct w16call *c) __attribute__ ((weak));
extern void api_LoadAccelerators16 (struct w16call *c) __attribute__ ((weak));
extern void api_LoadBitmap16 (struct w16call *c) __attribute__ ((weak));
extern void api_LoadCursor16 (struct w16call *c) __attribute__ ((weak));
extern void api_LoadCursorIconHandler16 (struct w16call *c) __attribute__ ((weak));
extern void api_LoadDIBCursorHandler16 (struct w16call *c) __attribute__ ((weak));
extern void api_LoadDIBIconHandler16 (struct w16call *c) __attribute__ ((weak));
extern void api_LoadIcon16 (struct w16call *c) __attribute__ ((weak));
extern void api_LoadIconHandler16 (struct w16call *c) __attribute__ ((weak));
extern void api_LoadImage16 (struct w16call *c) __attribute__ ((weak));
extern void api_LoadLanguageString16 (struct w16call *c) __attribute__ ((weak));
extern void api_LoadLibrary16 (struct w16call *c) __attribute__ ((weak));
extern void api_LoadLibrary32_16 (struct w16call *c) __attribute__ ((weak));
extern void api_LoadLibraryEx32W16 (struct w16call *c) __attribute__ ((weak));
extern void api_LoadMenu16 (struct w16call *c) __attribute__ ((weak));
extern void api_LoadMenuIndirect16 (struct w16call *c) __attribute__ ((weak));
extern void api_LoadModule16 (struct w16call *c) __attribute__ ((weak));
extern void api_LoadResource16 (struct w16call *c) __attribute__ ((weak));
extern void api_LoadString16 (struct w16call *c) __attribute__ ((weak));
extern void api_LoadSystemLanguageString16 (struct w16call *c) __attribute__ ((weak));
extern void api_Local32Alloc16 (struct w16call *c) __attribute__ ((weak));
extern void api_Local32First16 (struct w16call *c) __attribute__ ((weak));
extern void api_Local32Free16 (struct w16call *c) __attribute__ ((weak));
extern void api_Local32GetSegment16 (struct w16call *c) __attribute__ ((weak));
extern void api_Local32Info16 (struct w16call *c) __attribute__ ((weak));
extern void api_Local32Init16 (struct w16call *c) __attribute__ ((weak));
extern void api_Local32Next16 (struct w16call *c) __attribute__ ((weak));
extern void api_Local32ReAlloc16 (struct w16call *c) __attribute__ ((weak));
extern void api_Local32Size16 (struct w16call *c) __attribute__ ((weak));
extern void api_Local32Translate16 (struct w16call *c) __attribute__ ((weak));
extern void api_Local32ValidHandle16 (struct w16call *c) __attribute__ ((weak));
extern void api_LocalAlloc16 (struct w16call *c) __attribute__ ((weak));
extern void api_LocalCompact16 (struct w16call *c) __attribute__ ((weak));
extern void api_LocalCountFree16 (struct w16call *c) __attribute__ ((weak));
extern void api_LocalFirst16 (struct w16call *c) __attribute__ ((weak));
extern void api_LocalFlags16 (struct w16call *c) __attribute__ ((weak));
extern void api_LocalFree16 (struct w16call *c) __attribute__ ((weak));
extern void api_LocalHandle16 (struct w16call *c) __attribute__ ((weak));
extern void api_LocalHandleDelta16 (struct w16call *c) __attribute__ ((weak));
extern void api_LocalHeapSize16 (struct w16call *c) __attribute__ ((weak));
extern void api_LocalInfo16 (struct w16call *c) __attribute__ ((weak));
extern void api_LocalInit16 (struct w16call *c) __attribute__ ((weak));
extern void api_LocalLock16 (struct w16call *c) __attribute__ ((weak));
extern void api_LocalNext16 (struct w16call *c) __attribute__ ((weak));
extern void api_LocalNotify16 (struct w16call *c) __attribute__ ((weak));
extern void api_LocalReAlloc16 (struct w16call *c) __attribute__ ((weak));
extern void api_LocalShrink16 (struct w16call *c) __attribute__ ((weak));
extern void api_LocalSize16 (struct w16call *c) __attribute__ ((weak));
extern void api_LocalUnlock16 (struct w16call *c) __attribute__ ((weak));
extern void api_LockCurrentTask16 (struct w16call *c) __attribute__ ((weak));
extern void api_LockInput16 (struct w16call *c) __attribute__ ((weak));
extern void api_LockSegment16 (struct w16call *c) __attribute__ ((weak));
extern void api_LockWindowUpdate16 (struct w16call *c) __attribute__ ((weak));
extern void api_LogApiThk (struct w16call *c) __attribute__ ((weak));
extern void api_LogError16 (struct w16call *c) __attribute__ ((weak));
extern void api_LogParamError16 (struct w16call *c) __attribute__ ((weak));
extern void api_LongPtrAdd16 (struct w16call *c) __attribute__ ((weak));
extern void api_LookupIconIdFromDirectoryEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_LookupMenuHandle16 (struct w16call *c) __attribute__ ((weak));
extern void api_MMSYSTEM_LibMain (struct w16call *c) __attribute__ ((weak));
extern void api_MMSYSTEM_WEP (struct w16call *c) __attribute__ ((weak));
extern void api_MakeMenuRToL16 (struct w16call *c) __attribute__ ((weak));
extern void api_MakeObjectPrivate16 (struct w16call *c) __attribute__ ((weak));
extern void api_MakeProcInstance16 (struct w16call *c) __attribute__ ((weak));
extern void api_MakeScrollBarsRToL (struct w16call *c) __attribute__ ((weak));
extern void api_MapDialogRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_MapHInstLS16 (struct w16call *c) __attribute__ ((weak));
extern void api_MapHInstSL16 (struct w16call *c) __attribute__ ((weak));
extern void api_MapLS (struct w16call *c) __attribute__ ((weak));
extern void api_MapProcessHandle (struct w16call *c) __attribute__ ((weak));
extern void api_MapSL (struct w16call *c) __attribute__ ((weak));
extern void api_MapVirtualKey16 (struct w16call *c) __attribute__ ((weak));
extern void api_MapWindowPoints16 (struct w16call *c) __attribute__ ((weak));
extern void api_MemManInfo16 (struct w16call *c) __attribute__ ((weak));
extern void api_MemoryRead16 (struct w16call *c) __attribute__ ((weak));
extern void api_MemoryWrite16 (struct w16call *c) __attribute__ ((weak));
extern void api_MessageBeep16 (struct w16call *c) __attribute__ ((weak));
extern void api_MessageBox16 (struct w16call *c) __attribute__ ((weak));
extern void api_MessageBoxEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_MessageBoxIndirect16 (struct w16call *c) __attribute__ ((weak));
extern void api_MinPQ16 (struct w16call *c) __attribute__ ((weak));
extern void api_ModifyMenu16 (struct w16call *c) __attribute__ ((weak));
extern void api_ModuleFindHandle16 (struct w16call *c) __attribute__ ((weak));
extern void api_ModuleFindName16 (struct w16call *c) __attribute__ ((weak));
extern void api_ModuleFirst16 (struct w16call *c) __attribute__ ((weak));
extern void api_ModuleNext16 (struct w16call *c) __attribute__ ((weak));
extern void api_MoveTo16 (struct w16call *c) __attribute__ ((weak));
extern void api_MoveToEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_MoveWindow16 (struct w16call *c) __attribute__ ((weak));
extern void api_MsgWaitForMultipleObjects16 (struct w16call *c) __attribute__ ((weak));
extern void api_MulDiv16 (struct w16call *c) __attribute__ ((weak));
extern void api_MyAlloc16 (struct w16call *c) __attribute__ ((weak));
extern void api_NE_DefResourceHandler (struct w16call *c) __attribute__ ((weak));
extern void api_NetBIOSCall16 (struct w16call *c) __attribute__ ((weak));
extern void api_NotifyRegister16 (struct w16call *c) __attribute__ ((weak));
extern void api_NotifyUnRegister16 (struct w16call *c) __attribute__ ((weak));
extern void api_OemKeyScan16 (struct w16call *c) __attribute__ ((weak));
extern void api_OemToAnsi16 (struct w16call *c) __attribute__ ((weak));
extern void api_OemToAnsiBuff16 (struct w16call *c) __attribute__ ((weak));
extern void api_OffsetClipRgn16 (struct w16call *c) __attribute__ ((weak));
extern void api_OffsetRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_OffsetRgn16 (struct w16call *c) __attribute__ ((weak));
extern void api_OffsetViewportOrg16 (struct w16call *c) __attribute__ ((weak));
extern void api_OffsetViewportOrgEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_OffsetVisRgn16 (struct w16call *c) __attribute__ ((weak));
extern void api_OffsetWindowOrg16 (struct w16call *c) __attribute__ ((weak));
extern void api_OffsetWindowOrgEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_OldExitWindows16 (struct w16call *c) __attribute__ ((weak));
extern void api_OldYield16 (struct w16call *c) __attribute__ ((weak));
extern void api_OpenClipboard16 (struct w16call *c) __attribute__ ((weak));
extern void api_OpenComm16 (struct w16call *c) __attribute__ ((weak));
extern void api_OpenDriver16 (struct w16call *c) __attribute__ ((weak));
extern void api_OpenFile16 (struct w16call *c) __attribute__ ((weak));
extern void api_OpenIcon16 (struct w16call *c) __attribute__ ((weak));
extern void api_OpenJob16 (struct w16call *c) __attribute__ ((weak));
extern void api_OpenSound16 (struct w16call *c) __attribute__ ((weak));
extern void api_OutputDebugStr16 (struct w16call *c) __attribute__ ((weak));
extern void api_OutputDebugString16 (struct w16call *c) __attribute__ ((weak));
extern void api_PaintRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_PaintRgn16 (struct w16call *c) __attribute__ ((weak));
extern void api_PatBlt16 (struct w16call *c) __attribute__ ((weak));
extern void api_PatchCodeHandle16 (struct w16call *c) __attribute__ ((weak));
extern void api_PathToRegion16 (struct w16call *c) __attribute__ ((weak));
extern void api_PeekMessage16 (struct w16call *c) __attribute__ ((weak));
extern void api_PeekMessage32_16 (struct w16call *c) __attribute__ ((weak));
extern void api_Pie16 (struct w16call *c) __attribute__ ((weak));
extern void api_PlayMetaFile16 (struct w16call *c) __attribute__ ((weak));
extern void api_PlayMetaFileRecord16 (struct w16call *c) __attribute__ ((weak));
extern void api_PlaySound16 (struct w16call *c) __attribute__ ((weak));
extern void api_PolyBezier16 (struct w16call *c) __attribute__ ((weak));
extern void api_PolyBezierTo16 (struct w16call *c) __attribute__ ((weak));
extern void api_PolyPolygon16 (struct w16call *c) __attribute__ ((weak));
extern void api_Polygon16 (struct w16call *c) __attribute__ ((weak));
extern void api_Polyline16 (struct w16call *c) __attribute__ ((weak));
extern void api_PostAppMessage16 (struct w16call *c) __attribute__ ((weak));
extern void api_PostEvent16 (struct w16call *c) __attribute__ ((weak));
extern void api_PostMessage16 (struct w16call *c) __attribute__ ((weak));
extern void api_PostQuitMessage16 (struct w16call *c) __attribute__ ((weak));
extern void api_PrestoChangoSelector16 (struct w16call *c) __attribute__ ((weak));
extern void api_PrintDlg16 (struct w16call *c) __attribute__ ((weak));
extern void api_PrintDlgProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_PrintSetupDlgProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_PtInRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_PtInRegion16 (struct w16call *c) __attribute__ ((weak));
extern void api_PtVisible16 (struct w16call *c) __attribute__ ((weak));
extern void api_QueryAbort16 (struct w16call *c) __attribute__ ((weak));
extern void api_QueryCodePage16 (struct w16call *c) __attribute__ ((weak));
extern void api_QuerySendMessage16 (struct w16call *c) __attribute__ ((weak));
extern void api_RawDrawText16 (struct w16call *c) __attribute__ ((weak));
extern void api_RawExtTextOut16 (struct w16call *c) __attribute__ ((weak));
extern void api_RawGetTextExtent16 (struct w16call *c) __attribute__ ((weak));
extern void api_RawGetTextExtentEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_RawTextOut16 (struct w16call *c) __attribute__ ((weak));
extern void api_ReadComm16 (struct w16call *c) __attribute__ ((weak));
extern void api_RealizeDefaultPalette16 (struct w16call *c) __attribute__ ((weak));
extern void api_RealizePalette16 (struct w16call *c) __attribute__ ((weak));
extern void api_RectInRegion16 (struct w16call *c) __attribute__ ((weak));
extern void api_RectVisible16 (struct w16call *c) __attribute__ ((weak));
extern void api_Rectangle16 (struct w16call *c) __attribute__ ((weak));
extern void api_RedrawWindow16 (struct w16call *c) __attribute__ ((weak));
extern void api_RegCloseKey16 (struct w16call *c) __attribute__ ((weak));
extern void api_RegCreateKey16 (struct w16call *c) __attribute__ ((weak));
extern void api_RegDeleteKey16 (struct w16call *c) __attribute__ ((weak));
extern void api_RegDeleteValue16 (struct w16call *c) __attribute__ ((weak));
extern void api_RegEnumKey16 (struct w16call *c) __attribute__ ((weak));
extern void api_RegEnumValue16 (struct w16call *c) __attribute__ ((weak));
extern void api_RegFlushKey16 (struct w16call *c) __attribute__ ((weak));
extern void api_RegOpenKey16 (struct w16call *c) __attribute__ ((weak));
extern void api_RegQueryValue16 (struct w16call *c) __attribute__ ((weak));
extern void api_RegQueryValueEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_RegSetValue16 (struct w16call *c) __attribute__ ((weak));
extern void api_RegSetValueEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_RegisterCBClient16 (struct w16call *c) __attribute__ ((weak));
extern void api_RegisterClass16 (struct w16call *c) __attribute__ ((weak));
extern void api_RegisterClassEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_RegisterClipboardFormat16 (struct w16call *c) __attribute__ ((weak));
extern void api_RegisterServiceProcess16 (struct w16call *c) __attribute__ ((weak));
extern void api_RegisterShellHook16 (struct w16call *c) __attribute__ ((weak));
extern void api_RegisterWindowMessageA (struct w16call *c) __attribute__ ((weak));
extern void api_ReleaseCapture16 (struct w16call *c) __attribute__ ((weak));
extern void api_ReleaseDC16 (struct w16call *c) __attribute__ ((weak));
extern void api_RemoveDirectory16 (struct w16call *c) __attribute__ ((weak));
extern void api_RemoveFontResource16 (struct w16call *c) __attribute__ ((weak));
extern void api_RemoveMenu16 (struct w16call *c) __attribute__ ((weak));
extern void api_RemoveProp16 (struct w16call *c) __attribute__ ((weak));
extern void api_RepaintScreen16 (struct w16call *c) __attribute__ ((weak));
extern void api_ReplaceText16 (struct w16call *c) __attribute__ ((weak));
extern void api_ReplaceTextDlgProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_ReplyMessage16 (struct w16call *c) __attribute__ ((weak));
extern void api_ResetDC16 (struct w16call *c) __attribute__ ((weak));
extern void api_ResetDialogTemplate16 (struct w16call *c) __attribute__ ((weak));
extern void api_ResetW32Event (struct w16call *c) __attribute__ ((weak));
extern void api_ResizePalette16 (struct w16call *c) __attribute__ ((weak));
extern void api_Restore80x87State16 (struct w16call *c) __attribute__ ((weak));
extern void api_RestoreDC16 (struct w16call *c) __attribute__ ((weak));
extern void api_RestoreVisRgn16 (struct w16call *c) __attribute__ ((weak));
extern void api_Resurrection16 (struct w16call *c) __attribute__ ((weak));
extern void api_RoundRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_SHELL_DllEntryPoint (struct w16call *c) __attribute__ ((weak));
extern void api_SHFormatDrive16 (struct w16call *c) __attribute__ ((weak));
extern void api_SSConfirmSmallStack (struct w16call *c) __attribute__ ((weak));
extern void api_SSInit16 (struct w16call *c) __attribute__ ((weak));
extern void api_SYSTEM_KillSystemTimer (struct w16call *c) __attribute__ ((weak));
extern void api_Save80x87State16 (struct w16call *c) __attribute__ ((weak));
extern void api_SaveDC16 (struct w16call *c) __attribute__ ((weak));
extern void api_SaveVisRgn16 (struct w16call *c) __attribute__ ((weak));
extern void api_ScaleViewportExt16 (struct w16call *c) __attribute__ ((weak));
extern void api_ScaleViewportExtEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_ScaleWindowExt16 (struct w16call *c) __attribute__ ((weak));
extern void api_ScaleWindowExtEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_ScanLR16 (struct w16call *c) __attribute__ ((weak));
extern void api_ScreenSwitchEnable16 (struct w16call *c) __attribute__ ((weak));
extern void api_ScreenToClient16 (struct w16call *c) __attribute__ ((weak));
extern void api_ScrollChildren16 (struct w16call *c) __attribute__ ((weak));
extern void api_ScrollDC16 (struct w16call *c) __attribute__ ((weak));
extern void api_ScrollWindow16 (struct w16call *c) __attribute__ ((weak));
extern void api_ScrollWindowEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_SelectBitmap16 (struct w16call *c) __attribute__ ((weak));
extern void api_SelectClipPath16 (struct w16call *c) __attribute__ ((weak));
extern void api_SelectClipRgn16 (struct w16call *c) __attribute__ ((weak));
extern void api_SelectObject16 (struct w16call *c) __attribute__ ((weak));
extern void api_SelectPalette16 (struct w16call *c) __attribute__ ((weak));
extern void api_SelectVisRgn16 (struct w16call *c) __attribute__ ((weak));
extern void api_SelectorAccessRights16 (struct w16call *c) __attribute__ ((weak));
extern void api_SendDlgItemMessage16 (struct w16call *c) __attribute__ ((weak));
extern void api_SendDriverMessage16 (struct w16call *c) __attribute__ ((weak));
extern void api_SendMessage16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetAbortProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetActiveWindow16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetAppCodePage16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetArcDirection16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetBitmapBits16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetBitmapDimension16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetBitmapDimensionEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetBkColor16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetBkMode16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetBoundsRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetBrushOrg16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetCapture16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetCaretBlinkTime16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetCaretPos16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetClassLong16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetClassWord16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetClipboardData16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetClipboardViewer16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetCommBreak16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetCommEventMask16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetCommState16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetCurrentDirectory16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetCursor16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetCursorPos16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetDCHook16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetDCOrg16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetDCState16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetDIBColorTable16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetDIBits16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetDIBitsToDevice16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetDeskPattern16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetDeskWallPaper16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetDlgItemInt16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetDlgItemText16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetDlgItemTextEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetDoubleClickTime16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetEnvironment16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetErrorMode16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetEventHook16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetFastQueue16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetFileAttributes16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetFocus16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetForegroundWindow16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetHandleCount16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetHookFlags16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetInternalWindowPos16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetKeyboardLanguage16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetKeyboardState16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetLastError16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetLayout16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetMagicColors16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetMapMode16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetMapperFlags16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetMenu16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetMenuContextHelpId16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetMenuItemBitmaps16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetMessageQueue16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetMetaFileBits16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetMetaFileBitsBetter16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetObjectOwner16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetPaletteEntries16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetParent16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetPixel16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetPolyFillMode16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetPriority16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetProcessDefaultLayout16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetProcessDword (struct w16call *c) __attribute__ ((weak));
extern void api_SetProp16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetROP216 (struct w16call *c) __attribute__ ((weak));
extern void api_SetRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetRectEmpty16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetRectRgn16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetRelAbs16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetResourceHandler16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetScrollInfo16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetScrollPos16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetScrollRange16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetSelectorBase (struct w16call *c) __attribute__ ((weak));
extern void api_SetSelectorLimit16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetSigHandler16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetSolidBrush16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetSoundNoise16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetSpeed16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetStretchBltMode16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetSwapAreaSize16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetSysColors16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetSysModalWindow16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetSystemMenu16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetSystemPaletteUse16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetSystemTimer16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetTaskQueue16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetTaskSignalProc (struct w16call *c) __attribute__ ((weak));
extern void api_SetTextAlign16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetTextCharacterExtra16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetTextCodePage16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetTextColor16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetTextJustification16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetThreadQueue16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetThunkletCallbackGlue16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetTimer16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetViewportExt16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetViewportExtEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetViewportOrg16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetViewportOrgEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetVoiceAccent16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetVoiceEnvelope16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetVoiceNote16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetVoiceQueueSize16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetVoiceSound16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetVoiceThreshold16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetW32Event (struct w16call *c) __attribute__ ((weak));
extern void api_SetWinDebugInfo16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetWindowExt16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetWindowExtEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetWindowLong16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetWindowOrg16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetWindowOrgEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetWindowPlacement16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetWindowPos16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetWindowRgn16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetWindowText16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetWindowTextEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetWindowWord16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetWindowsHook16 (struct w16call *c) __attribute__ ((weak));
extern void api_SetWindowsHookEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_ShellAbout16 (struct w16call *c) __attribute__ ((weak));
extern void api_ShellExecute16 (struct w16call *c) __attribute__ ((weak));
extern void api_ShellHookProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_ShowCaret16 (struct w16call *c) __attribute__ ((weak));
extern void api_ShowCursor16 (struct w16call *c) __attribute__ ((weak));
extern void api_ShowOwnedPopups16 (struct w16call *c) __attribute__ ((weak));
extern void api_ShowScrollBar16 (struct w16call *c) __attribute__ ((weak));
extern void api_ShowWindow16 (struct w16call *c) __attribute__ ((weak));
extern void api_ShrinkGdiHeap16 (struct w16call *c) __attribute__ ((weak));
extern void api_SignalProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_SizePQ16 (struct w16call *c) __attribute__ ((weak));
extern void api_SizeofResource16 (struct w16call *c) __attribute__ ((weak));
extern void api_SpoolFile16 (struct w16call *c) __attribute__ ((weak));
extern void api_StackEnter16 (struct w16call *c) __attribute__ ((weak));
extern void api_StackLeave16 (struct w16call *c) __attribute__ ((weak));
extern void api_StackTraceCSIPFirst16 (struct w16call *c) __attribute__ ((weak));
extern void api_StackTraceFirst16 (struct w16call *c) __attribute__ ((weak));
extern void api_StackTraceNext16 (struct w16call *c) __attribute__ ((weak));
extern void api_StartDoc16 (struct w16call *c) __attribute__ ((weak));
extern void api_StartPage16 (struct w16call *c) __attribute__ ((weak));
extern void api_StartSound16 (struct w16call *c) __attribute__ ((weak));
extern void api_StartSpoolPage16 (struct w16call *c) __attribute__ ((weak));
extern void api_StopSound16 (struct w16call *c) __attribute__ ((weak));
extern void api_StretchBlt16 (struct w16call *c) __attribute__ ((weak));
extern void api_StretchDIBits16 (struct w16call *c) __attribute__ ((weak));
extern void api_StrokeAndFillPath16 (struct w16call *c) __attribute__ ((weak));
extern void api_StrokePath16 (struct w16call *c) __attribute__ ((weak));
extern void api_SubtractRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_SwapMouseButton16 (struct w16call *c) __attribute__ ((weak));
extern void api_SwitchStackBack16 (struct w16call *c) __attribute__ ((weak));
extern void api_SwitchStackTo16 (struct w16call *c) __attribute__ ((weak));
extern void api_SwitchToThisWindow16 (struct w16call *c) __attribute__ ((weak));
extern void api_SyncAllVoices16 (struct w16call *c) __attribute__ ((weak));
extern void api_SysErrorBox16 (struct w16call *c) __attribute__ ((weak));
extern void api_SystemHeapInfo16 (struct w16call *c) __attribute__ ((weak));
extern void api_SystemParametersInfo16 (struct w16call *c) __attribute__ ((weak));
extern void api_TabbedTextOut16 (struct w16call *c) __attribute__ ((weak));
extern void api_TaskFindHandle16 (struct w16call *c) __attribute__ ((weak));
extern void api_TaskFirst16 (struct w16call *c) __attribute__ ((weak));
extern void api_TaskGetCSIP16 (struct w16call *c) __attribute__ ((weak));
extern void api_TaskNext16 (struct w16call *c) __attribute__ ((weak));
extern void api_TaskSetCSIP16 (struct w16call *c) __attribute__ ((weak));
extern void api_TaskSwitch16 (struct w16call *c) __attribute__ ((weak));
extern void api_TerminateApp16 (struct w16call *c) __attribute__ ((weak));
extern void api_TextOut16 (struct w16call *c) __attribute__ ((weak));
extern void api_Throw16 (struct w16call *c) __attribute__ ((weak));
extern void api_ThunkConnect16 (struct w16call *c) __attribute__ ((weak));
extern void api_TileChildWindows16 (struct w16call *c) __attribute__ ((weak));
extern void api_TimerCount16 (struct w16call *c) __attribute__ ((weak));
extern void api_ToAscii16 (struct w16call *c) __attribute__ ((weak));
extern void api_ToolHelpHook16 (struct w16call *c) __attribute__ ((weak));
extern void api_TrackPopupMenu16 (struct w16call *c) __attribute__ ((weak));
extern void api_TranslateAccelerator16 (struct w16call *c) __attribute__ ((weak));
extern void api_TranslateMDISysAccel16 (struct w16call *c) __attribute__ ((weak));
extern void api_TranslateMessage16 (struct w16call *c) __attribute__ ((weak));
extern void api_TranslateMessage32_16 (struct w16call *c) __attribute__ ((weak));
extern void api_TransmitCommChar16 (struct w16call *c) __attribute__ ((weak));
extern void api_UTGlue16 (struct w16call *c) __attribute__ ((weak));
extern void api_UTInit16 (struct w16call *c) __attribute__ ((weak));
extern void api_UnMapLS (struct w16call *c) __attribute__ ((weak));
extern void api_UnRegisterCBClient16 (struct w16call *c) __attribute__ ((weak));
extern void api_UngetCommChar16 (struct w16call *c) __attribute__ ((weak));
extern void api_UnhookWindowsHook16 (struct w16call *c) __attribute__ ((weak));
extern void api_UnhookWindowsHookEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_UnicodeToAnsi16 (struct w16call *c) __attribute__ ((weak));
extern void api_UnionRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_UnlockSegment16 (struct w16call *c) __attribute__ ((weak));
extern void api_UnrealizeObject16 (struct w16call *c) __attribute__ ((weak));
extern void api_UnregisterClass16 (struct w16call *c) __attribute__ ((weak));
extern void api_UpdateColors16 (struct w16call *c) __attribute__ ((weak));
extern void api_UpdateWindow16 (struct w16call *c) __attribute__ ((weak));
extern void api_UserSeeUserDo16 (struct w16call *c) __attribute__ ((weak));
extern void api_UserSignalProc (struct w16call *c) __attribute__ ((weak));
extern void api_UserYield16 (struct w16call *c) __attribute__ ((weak));
extern void api_VWin32_BoostThreadGroup (struct w16call *c) __attribute__ ((weak));
extern void api_VWin32_BoostThreadStatic (struct w16call *c) __attribute__ ((weak));
extern void api_VWin32_EventCreate (struct w16call *c) __attribute__ ((weak));
extern void api_VWin32_EventDestroy (struct w16call *c) __attribute__ ((weak));
extern void api_VWin32_EventSet (struct w16call *c) __attribute__ ((weak));
extern void api_VWin32_EventWait (struct w16call *c) __attribute__ ((weak));
extern void api_ValidateRect16 (struct w16call *c) __attribute__ ((weak));
extern void api_ValidateRgn16 (struct w16call *c) __attribute__ ((weak));
extern void api_VerFindFile16 (struct w16call *c) __attribute__ ((weak));
extern void api_VerInstallFile16 (struct w16call *c) __attribute__ ((weak));
extern void api_VerLanguageName16 (struct w16call *c) __attribute__ ((weak));
extern void api_VerQueryValue16 (struct w16call *c) __attribute__ ((weak));
extern void api_VkKeyScan16 (struct w16call *c) __attribute__ ((weak));
extern void api_WIN16_FreeLibrary16 (struct w16call *c) __attribute__ ((weak));
extern void api_WIN16_FreeSelector16 (struct w16call *c) __attribute__ ((weak));
extern void api_WIN16_GetCurrentTask (struct w16call *c) __attribute__ ((weak));
extern void api_WIN16_GetExePtr (struct w16call *c) __attribute__ ((weak));
extern void api_WIN16_GetModuleHandle (struct w16call *c) __attribute__ ((weak));
extern void api_WIN16_GetProcAddress16 (struct w16call *c) __attribute__ ((weak));
extern void api_WIN16_GlobalAlloc16 (struct w16call *c) __attribute__ ((weak));
extern void api_WIN16_GlobalFlags16 (struct w16call *c) __attribute__ ((weak));
extern void api_WIN16_GlobalFree16 (struct w16call *c) __attribute__ ((weak));
extern void api_WIN16_GlobalLock16 (struct w16call *c) __attribute__ ((weak));
extern void api_WIN16_GlobalReAlloc16 (struct w16call *c) __attribute__ ((weak));
extern void api_WIN16_GlobalSize16 (struct w16call *c) __attribute__ ((weak));
extern void api_WIN16_GlobalUnlock16 (struct w16call *c) __attribute__ ((weak));
extern void api_WIN16_LockResource16 (struct w16call *c) __attribute__ ((weak));
extern void api_WIN16_OpenFile16 (struct w16call *c) __attribute__ ((weak));
extern void api_WIN16_hread (struct w16call *c) __attribute__ ((weak));
extern void api_WIN16_lread (struct w16call *c) __attribute__ ((weak));
extern void api_WIN32_OldYield16 (struct w16call *c) __attribute__ ((weak));
extern void api_WINE_mmThreadEntryPoint (struct w16call *c) __attribute__ ((weak));
extern void api_WMMMidiRunOnce16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetAbortJob16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetAddConnection16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetBrowseDialog16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetCancelConnection16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetCancelJob16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetCloseJob16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetConnectDialog16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetConnectionDialog16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetDeviceMode16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetDirectoryNotify16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetDisconnectDialog16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetErrorText16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetGetCaps16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetGetConnection16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetGetDirectoryType16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetGetError16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetGetErrorText16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetGetPropertyText16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetGetUser16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetHoldJob16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetLockQueueData16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetOpenJob16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetPropertyDialog16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetReleaseJob16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetRestoreConnection16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetSetJobCopies16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetUnlockQueueData16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetUnwatchQueue16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetViewQueueDialog16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetWatchQueue16 (struct w16call *c) __attribute__ ((weak));
extern void api_WNetWriteJob16 (struct w16call *c) __attribute__ ((weak));
extern void api_WOW16Call (struct w16call *c) __attribute__ ((weak));
extern void api_WaitEvent16 (struct w16call *c) __attribute__ ((weak));
extern void api_WaitForMultipleObjects16 (struct w16call *c) __attribute__ ((weak));
extern void api_WaitForMultipleObjectsEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_WaitForSingleObject16 (struct w16call *c) __attribute__ ((weak));
extern void api_WaitMessage16 (struct w16call *c) __attribute__ ((weak));
extern void api_WaitSoundState16 (struct w16call *c) __attribute__ ((weak));
extern void api_WidenPath16 (struct w16call *c) __attribute__ ((weak));
extern void api_WinExec16 (struct w16call *c) __attribute__ ((weak));
extern void api_WinHelp16 (struct w16call *c) __attribute__ ((weak));
extern void api_WindowFromDC16 (struct w16call *c) __attribute__ ((weak));
extern void api_WindowFromPoint16 (struct w16call *c) __attribute__ ((weak));
extern void api_WriteComm16 (struct w16call *c) __attribute__ ((weak));
extern void api_WriteDialog16 (struct w16call *c) __attribute__ ((weak));
extern void api_WriteOutProfiles16 (struct w16call *c) __attribute__ ((weak));
extern void api_WritePrivateProfileSection16 (struct w16call *c) __attribute__ ((weak));
extern void api_WritePrivateProfileString16 (struct w16call *c) __attribute__ ((weak));
extern void api_WritePrivateProfileStruct16 (struct w16call *c) __attribute__ ((weak));
extern void api_WriteProfileSection16 (struct w16call *c) __attribute__ ((weak));
extern void api_WriteProfileString16 (struct w16call *c) __attribute__ ((weak));
extern void api_WriteSpool16 (struct w16call *c) __attribute__ ((weak));
extern void api_Yield16 (struct w16call *c) __attribute__ ((weak));
extern void api__CheckNotSysLevel (struct w16call *c) __attribute__ ((weak));
extern void api__ConfirmSysLevel (struct w16call *c) __attribute__ ((weak));
extern void api__CreateSysLevel (struct w16call *c) __attribute__ ((weak));
extern void api__DebugOutput (struct w16call *c) __attribute__ ((weak));
extern void api__EnterSysLevel (struct w16call *c) __attribute__ ((weak));
extern void api__EnterWin16Lock (struct w16call *c) __attribute__ ((weak));
extern void api__LeaveSysLevel (struct w16call *c) __attribute__ ((weak));
extern void api__LeaveWin16Lock (struct w16call *c) __attribute__ ((weak));
extern void api___WinEm87Info (struct w16call *c) __attribute__ ((weak));
extern void api___WinEm87Restore (struct w16call *c) __attribute__ ((weak));
extern void api___WinEm87Save (struct w16call *c) __attribute__ ((weak));
extern void api___wine_call_wndproc (struct w16call *c) __attribute__ ((weak));
extern void api___wine_vxd_apm (struct w16call *c) __attribute__ ((weak));
extern void api___wine_vxd_comm (struct w16call *c) __attribute__ ((weak));
extern void api___wine_vxd_configmg (struct w16call *c) __attribute__ ((weak));
extern void api___wine_vxd_enable (struct w16call *c) __attribute__ ((weak));
extern void api___wine_vxd_pagefile (struct w16call *c) __attribute__ ((weak));
extern void api___wine_vxd_reboot (struct w16call *c) __attribute__ ((weak));
extern void api___wine_vxd_shell (struct w16call *c) __attribute__ ((weak));
extern void api___wine_vxd_timer (struct w16call *c) __attribute__ ((weak));
extern void api___wine_vxd_timerapi (struct w16call *c) __attribute__ ((weak));
extern void api___wine_vxd_vdd (struct w16call *c) __attribute__ ((weak));
extern void api___wine_vxd_vmd (struct w16call *c) __attribute__ ((weak));
extern void api___wine_vxd_vmm (struct w16call *c) __attribute__ ((weak));
extern void api___wine_vxd_vxdloader (struct w16call *c) __attribute__ ((weak));
extern void api___wine_vxd_win32s (struct w16call *c) __attribute__ ((weak));
extern void api__fpMath (struct w16call *c) __attribute__ ((weak));
extern void api__hwrite16 (struct w16call *c) __attribute__ ((weak));
extern void api__lclose16 (struct w16call *c) __attribute__ ((weak));
extern void api__lcreat16 (struct w16call *c) __attribute__ ((weak));
extern void api__llseek16 (struct w16call *c) __attribute__ ((weak));
extern void api__lopen16 (struct w16call *c) __attribute__ ((weak));
extern void api__lwrite16 (struct w16call *c) __attribute__ ((weak));
extern void api_auxGetDevCaps16 (struct w16call *c) __attribute__ ((weak));
extern void api_auxGetNumDevs16 (struct w16call *c) __attribute__ ((weak));
extern void api_auxGetVolume16 (struct w16call *c) __attribute__ ((weak));
extern void api_auxOutMessage16 (struct w16call *c) __attribute__ ((weak));
extern void api_auxSetVolume16 (struct w16call *c) __attribute__ ((weak));
extern void api_fpe_return (struct w16call *c) __attribute__ ((weak));
extern void api_hmemcpy16 (struct w16call *c) __attribute__ ((weak));
extern void api_joyGetDevCaps16 (struct w16call *c) __attribute__ ((weak));
extern void api_joyGetNumDevs16 (struct w16call *c) __attribute__ ((weak));
extern void api_joyGetPos16 (struct w16call *c) __attribute__ ((weak));
extern void api_joyGetPosEx16 (struct w16call *c) __attribute__ ((weak));
extern void api_joyGetThreshold16 (struct w16call *c) __attribute__ ((weak));
extern void api_joyReleaseCapture16 (struct w16call *c) __attribute__ ((weak));
extern void api_joySetCalibration16 (struct w16call *c) __attribute__ ((weak));
extern void api_joySetCapture16 (struct w16call *c) __attribute__ ((weak));
extern void api_joySetThreshold16 (struct w16call *c) __attribute__ ((weak));
extern void api_keybd_event16 (struct w16call *c) __attribute__ ((weak));
extern void api_lstrcat16 (struct w16call *c) __attribute__ ((weak));
extern void api_lstrcatn16 (struct w16call *c) __attribute__ ((weak));
extern void api_lstrcmpA (struct w16call *c) __attribute__ ((weak));
extern void api_lstrcmpiA (struct w16call *c) __attribute__ ((weak));
extern void api_lstrcpy16 (struct w16call *c) __attribute__ ((weak));
extern void api_lstrcpyn16 (struct w16call *c) __attribute__ ((weak));
extern void api_lstrlen16 (struct w16call *c) __attribute__ ((weak));
extern void api_mciDriverNotify16 (struct w16call *c) __attribute__ ((weak));
extern void api_mciDriverYield16 (struct w16call *c) __attribute__ ((weak));
extern void api_mciExecute16 (struct w16call *c) __attribute__ ((weak));
extern void api_mciFreeCommandResource16 (struct w16call *c) __attribute__ ((weak));
extern void api_mciGetCreatorTask16 (struct w16call *c) __attribute__ ((weak));
extern void api_mciGetDeviceID16 (struct w16call *c) __attribute__ ((weak));
extern void api_mciGetDeviceIDFromElementID16 (struct w16call *c) __attribute__ ((weak));
extern void api_mciGetDriverData16 (struct w16call *c) __attribute__ ((weak));
extern void api_mciGetErrorString16 (struct w16call *c) __attribute__ ((weak));
extern void api_mciGetYieldProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_mciLoadCommandResource16 (struct w16call *c) __attribute__ ((weak));
extern void api_mciSendCommand16 (struct w16call *c) __attribute__ ((weak));
extern void api_mciSendString16 (struct w16call *c) __attribute__ ((weak));
extern void api_mciSetDriverData16 (struct w16call *c) __attribute__ ((weak));
extern void api_mciSetYieldProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiInAddBuffer16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiInClose16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiInGetDevCaps16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiInGetID16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiInGetNumDevs16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiInMessage16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiInOpen16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiInPrepareHeader16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiInReset16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiInStart16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiInStop16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiInUnprepareHeader16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiOutCacheDrumPatches16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiOutCachePatches16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiOutClose16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiOutGetDevCaps16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiOutGetErrorText16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiOutGetID16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiOutGetNumDevs16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiOutGetVolume16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiOutLongMsg16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiOutMessage16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiOutOpen16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiOutPrepareHeader16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiOutReset16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiOutSetVolume16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiOutShortMsg16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiOutUnprepareHeader16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiStreamClose16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiStreamOpen16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiStreamOut16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiStreamPause16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiStreamPosition16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiStreamProperty16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiStreamRestart16 (struct w16call *c) __attribute__ ((weak));
extern void api_midiStreamStop16 (struct w16call *c) __attribute__ ((weak));
extern void api_mixerClose16 (struct w16call *c) __attribute__ ((weak));
extern void api_mixerGetControlDetails16 (struct w16call *c) __attribute__ ((weak));
extern void api_mixerGetDevCaps16 (struct w16call *c) __attribute__ ((weak));
extern void api_mixerGetID16 (struct w16call *c) __attribute__ ((weak));
extern void api_mixerGetLineControls16 (struct w16call *c) __attribute__ ((weak));
extern void api_mixerGetLineInfo16 (struct w16call *c) __attribute__ ((weak));
extern void api_mixerGetNumDevs16 (struct w16call *c) __attribute__ ((weak));
extern void api_mixerMessage16 (struct w16call *c) __attribute__ ((weak));
extern void api_mixerOpen16 (struct w16call *c) __attribute__ ((weak));
extern void api_mixerSetControlDetails16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmGetCurrentTask16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmShowMMCPLPropertySheet16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmTaskBlock16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmTaskCreate16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmTaskSignal16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmTaskYield16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmThreadBlock16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmThreadCreate16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmThreadGetTask16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmThreadIsCurrent16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmThreadIsValid16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmThreadSignal16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmioAdvance16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmioAscend16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmioClose16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmioCreateChunk16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmioDescend16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmioFlush16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmioGetInfo16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmioInstallIOProc16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmioOpen16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmioRead16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmioRename16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmioSeek16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmioSendMessage16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmioSetBuffer16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmioSetInfo16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmioStringToFOURCC16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmioWrite16 (struct w16call *c) __attribute__ ((weak));
extern void api_mmsystemGetVersion16 (struct w16call *c) __attribute__ ((weak));
extern void api_mouse_event16 (struct w16call *c) __attribute__ ((weak));
extern void api_next_intcb (struct w16call *c) __attribute__ ((weak));
extern void api_sndPlaySound16 (struct w16call *c) __attribute__ ((weak));
extern void api_stub_USER_486 (struct w16call *c) __attribute__ ((weak));
extern void api_stub_USER_488 (struct w16call *c) __attribute__ ((weak));
extern void api_stub_USER_491 (struct w16call *c) __attribute__ ((weak));
extern void api_stub_USER_493 (struct w16call *c) __attribute__ ((weak));
extern void api_stub_USER_494 (struct w16call *c) __attribute__ ((weak));
extern void api_stub_USER_495 (struct w16call *c) __attribute__ ((weak));
extern void api_stub_USER_496 (struct w16call *c) __attribute__ ((weak));
extern void api_timeBeginPeriod16 (struct w16call *c) __attribute__ ((weak));
extern void api_timeEndPeriod16 (struct w16call *c) __attribute__ ((weak));
extern void api_timeGetDevCaps16 (struct w16call *c) __attribute__ ((weak));
extern void api_timeGetSystemTime16 (struct w16call *c) __attribute__ ((weak));
extern void api_timeGetTime16 (struct w16call *c) __attribute__ ((weak));
extern void api_timeKillEvent16 (struct w16call *c) __attribute__ ((weak));
extern void api_timeSetEvent16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveInAddBuffer16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveInClose16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveInGetDevCaps16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveInGetID16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveInGetNumDevs16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveInGetPosition16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveInMessage16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveInOpen16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveInPrepareHeader16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveInReset16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveInStart16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveInStop16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveInUnprepareHeader16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveOutBreakLoop16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveOutClose16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveOutGetDevCaps16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveOutGetErrorText16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveOutGetID16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveOutGetNumDevs16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveOutGetPitch16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveOutGetPlaybackRate16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveOutGetPosition16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveOutGetVolume16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveOutMessage16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveOutOpen16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveOutPause16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveOutPrepareHeader16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveOutReset16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveOutRestart16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveOutSetPitch16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveOutSetPlaybackRate16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveOutSetVolume16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveOutUnprepareHeader16 (struct w16call *c) __attribute__ ((weak));
extern void api_waveOutWrite16 (struct w16call *c) __attribute__ ((weak));
extern void api_wsprintf16 (struct w16call *c) __attribute__ ((weak));
extern void api_wvsprintf16 (struct w16call *c) __attribute__ ((weak));

static const struct w16_api api_kernel[] = {
	{ 1, 3, 0, "FatalExit", 0, 0, 0 },
	{ 2, 0, 1, "ExitKernel", "", api_ExitKernel16, 0 },
	{ 3, 0, 0, "GetVersion", "", api_GetVersion16, 0 },
	{ 4, 0, 1, "LocalInit", "www", api_LocalInit16, 0 },
	{ 5, 0, 1, "LocalAlloc", "ww", api_LocalAlloc16, 0 },
	{ 6, 0, 1, "LocalReAlloc", "www", api_LocalReAlloc16, 0 },
	{ 7, 0, 1, "LocalFree", "w", api_LocalFree16, 0 },
	{ 8, 0, 0, "LocalLock", "w", api_LocalLock16, 0 },
	{ 9, 0, 1, "LocalUnlock", "w", api_LocalUnlock16, 0 },
	{ 10, 0, 1, "LocalSize", "w", api_LocalSize16, 0 },
	{ 11, 0, 1, "LocalHandle", "w", api_LocalHandle16, 0 },
	{ 12, 0, 1, "LocalFlags", "w", api_LocalFlags16, 0 },
	{ 13, 0, 1, "LocalCompact", "w", api_LocalCompact16, 0 },
	{ 14, 0, 0, "LocalNotify", "l", api_LocalNotify16, 0 },
	{ 15, 1, 0, "GlobalAlloc", "wl", api_WIN16_GlobalAlloc16, 0 },
	{ 16, 0, 1, "GlobalReAlloc", "wlw", api_WIN16_GlobalReAlloc16, 0 },
	{ 17, 0, 0, "GlobalFree", "w", api_WIN16_GlobalFree16, 0 },
	{ 18, 0, 0, "GlobalLock", "w", api_WIN16_GlobalLock16, 0 },
	{ 19, 0, 0, "GlobalUnlock", "w", api_WIN16_GlobalUnlock16, 0 },
	{ 20, 0, 0, "GlobalSize", "w", api_WIN16_GlobalSize16, 0 },
	{ 21, 0, 0, "GlobalHandle", "w", api_GlobalHandle16, 0 },
	{ 22, 0, 0, "GlobalFlags", "w", api_WIN16_GlobalFlags16, 0 },
	{ 23, 0, 1, "LockSegment", "w", api_LockSegment16, 0 },
	{ 24, 0, 1, "UnlockSegment", "w", api_UnlockSegment16, 0 },
	{ 25, 0, 0, "GlobalCompact", "l", api_GlobalCompact16, 0 },
	{ 26, 0, 1, "GlobalFreeAll", "w", api_GlobalFreeAll16, 0 },
	{ 27, 0, 1, "GetModuleName", "wpw", api_GetModuleName16, 0 },
	{ 28, 0, 0, "GlobalMasterHandle", "", api_GlobalMasterHandle16, 0 },
	{ 29, 0, 1, "Yield", "", api_Yield16, 0 },
	{ 30, 0, 1, "WaitEvent", "w", api_WaitEvent16, 0 },
	{ 31, 0, 1, "PostEvent", "w", api_PostEvent16, 0 },
	{ 32, 0, 1, "SetPriority", "ws", api_SetPriority16, 0 },
	{ 33, 0, 1, "LockCurrentTask", "w", api_LockCurrentTask16, 0 },
	{ 34, 0, 1, "SetTaskQueue", "ww", api_SetTaskQueue16, 0 },
	{ 35, 0, 1, "GetTaskQueue", "w", api_GetTaskQueue16, 0 },
	{ 36, 0, 0, "GetCurrentTask", "", api_WIN16_GetCurrentTask, 0 },
	{ 37, 0, 0, "GetCurrentPDB", "", api_GetCurrentPDB16, 0 },
	{ 38, 0, 0, "SetTaskSignalProc", "wP", api_SetTaskSignalProc, 0 },
	{ 39, 3, 0, "SetTaskSwitchProc", 0, 0, 0 },
	{ 40, 3, 0, "SetTaskInterchange", 0, 0, 0 },
	{ 41, 0, 1, "EnableDos", "", api_KERNEL_nop, 0 },
	{ 42, 0, 1, "DisableDos", "", api_KERNEL_nop, 0 },
	{ 43, 3, 0, "IsScreenGrab", 0, 0, 0 },
	{ 44, 3, 0, "BuildPDB", 0, 0, 0 },
	{ 45, 0, 1, "LoadModule", "tp", api_LoadModule16, 0 },
	{ 46, 0, 1, "FreeModule", "w", api_FreeModule16, 0 },
	{ 47, 0, 0, "GetModuleHandle", "T", api_WIN16_GetModuleHandle, 0 },
	{ 48, 0, 1, "GetModuleUsage", "w", api_GetModuleUsage16, 0 },
	{ 49, 0, 1, "GetModuleFileName", "wps", api_GetModuleFileName16, 0 },
	{ 50, 0, 0, "GetProcAddress", "wt", api_WIN16_GetProcAddress16, 0 },
	{ 51, 0, 0, "MakeProcInstance", "Pw", api_MakeProcInstance16, 0 },
	{ 52, 0, 1, "FreeProcInstance", "P", api_FreeProcInstance16, 0 },
	{ 53, 3, 0, "CallProcInstance", 0, 0, 0 },
	{ 54, 0, 1, "GetInstanceData", "www", api_GetInstanceData16, 0 },
	{ 55, 1, 0, "Catch", "P", api_Catch16, 0 },
	{ 56, 1, 0, "Throw", "pw", api_Throw16, 0 },
	{ 57, 0, 1, "GetProfileInt", "tts", api_GetProfileInt16, 0 },
	{ 58, 0, 1, "GetProfileString", "tttpw", api_GetProfileString16, 0 },
	{ 59, 0, 1, "WriteProfileString", "ttt", api_WriteProfileString16, 0 },
	{ 60, 0, 1, "FindResource", "wtt", api_FindResource16, 0 },
	{ 61, 0, 1, "LoadResource", "ww", api_LoadResource16, 0 },
	{ 62, 0, 0, "LockResource", "w", api_WIN16_LockResource16, 0 },
	{ 63, 0, 1, "FreeResource", "w", api_FreeResource16, 0 },
	{ 64, 0, 1, "AccessResource", "ww", api_AccessResource16, 0 },
	{ 65, 0, 0, "SizeofResource", "ww", api_SizeofResource16, 0 },
	{ 66, 0, 1, "AllocResource", "wwl", api_AllocResource16, 0 },
	{ 67, 0, 0, "SetResourceHandler", "wtP", api_SetResourceHandler16, 0 },
	{ 68, 0, 1, "InitAtomTable", "w", api_InitAtomTable16, 0 },
	{ 69, 0, 1, "FindAtom", "t", api_FindAtom16, 0 },
	{ 70, 0, 1, "AddAtom", "t", api_AddAtom16, 0 },
	{ 71, 0, 1, "DeleteAtom", "w", api_DeleteAtom16, 0 },
	{ 72, 0, 1, "GetAtomName", "wpw", api_GetAtomName16, 0 },
	{ 73, 0, 1, "GetAtomHandle", "w", api_GetAtomHandle16, 0 },
	{ 74, 0, 1, "OpenFile", "tlw", api_WIN16_OpenFile16, 0 },
	{ 75, 3, 0, "OpenPathName", 0, 0, 0 },
	{ 76, 3, 0, "DeletePathName", 0, 0, 0 },
	{ 77, 0, 0, "Reserved1", "P", api_KERNEL_AnsiNext16, 0 },
	{ 78, 0, 0, "Reserved2", "PP", api_KERNEL_AnsiPrev16, 0 },
	{ 79, 0, 0, "Reserved3", "T", api_KERNEL_AnsiUpper16, 0 },
	{ 80, 0, 0, "Reserved4", "T", api_KERNEL_AnsiLower16, 0 },
	{ 81, 0, 1, "_lclose", "w", api__lclose16, 0 },
	{ 82, 0, 1, "_lread", "wPw", api_WIN16_lread, 0 },
	{ 83, 0, 1, "_lcreat", "tw", api__lcreat16, 0 },
	{ 84, 0, 0, "_llseek", "wlw", api__llseek16, 0 },
	{ 85, 0, 1, "_lopen", "tw", api__lopen16, 0 },
	{ 86, 0, 1, "_lwrite", "wpw", api__lwrite16, 0 },
	{ 87, 0, 1, "Reserved5", "tt", api_KERNEL_lstrcmp16, 0 },
	{ 88, 0, 0, "lstrcpy", "Pt", api_lstrcpy16, 0 },
	{ 89, 0, 0, "lstrcat", "Tt", api_lstrcat16, 0 },
	{ 90, 0, 1, "lstrlen", "t", api_lstrlen16, 0 },
	{ 91, 1, 0, "InitTask", "", api_InitTask16, 0 },
	{ 92, 0, 0, "GetTempDrive", "w", api_GetTempDrive, 0 },
	{ 93, 0, 0, "GetCodeHandle", "P", api_GetCodeHandle16, 0 },
	{ 94, 0, 1, "DefineHandleTable", "w", api_DefineHandleTable16, 0 },
	{ 95, 0, 1, "LoadLibrary", "t", api_LoadLibrary16, 0 },
	{ 96, 0, 0, "FreeLibrary", "w", api_WIN16_FreeLibrary16, 0 },
	{ 97, 0, 1, "GetTempFileName", "wtwp", api_GetTempFileName16, 0 },
	{ 98, 0, 1, "GetLastDiskChange", "", api_KERNEL_nop, 0 },
	{ 99, 3, 0, "GetLPErrMode", 0, 0, 0 },
	{ 100, 0, 1, "ValidateCodeSegments", "", api_KERNEL_nop, 0 },
	{ 101, 3, 0, "NoHookDosCall", 0, 0, 0 },
	{ 102, 1, 0, "DOS3Call", "", api_DOS3Call, 0 },
	{ 103, 1, 0, "NetBIOSCall", "", api_NetBIOSCall16, 0 },
	{ 104, 0, 1, "GetCodeInfo", "Pp", api_GetCodeInfo16, 0 },
	{ 105, 0, 1, "GetExeVersion", "", api_GetExeVersion16, 0 },
	{ 106, 0, 0, "SetSwapAreaSize", "w", api_SetSwapAreaSize16, 0 },
	{ 107, 0, 1, "SetErrorMode", "w", api_SetErrorMode16, 0 },
	{ 108, 0, 1, "SwitchStackTo", "www", api_SwitchStackTo16, 0 },
	{ 109, 1, 0, "SwitchStackBack", "www", api_SwitchStackBack16, 0 },
	{ 110, 0, 0, "PatchCodeHandle", "w", api_PatchCodeHandle16, 0 },
	{ 111, 0, 0, "GlobalWire", "w", api_GlobalWire16, 0 },
	{ 112, 0, 1, "GlobalUnWire", "w", api_GlobalUnWire16, 0 },
	{ 113, 4, 0, "__AHSHIFT", 0, 0, 3 },
	{ 114, 4, 0, "__AHINCR", 0, 0, 8 },
	{ 115, 1, 0, "OutputDebugString", "t", api_OutputDebugString16, 0 },
	{ 116, 3, 0, "InitLib", 0, 0, 0 },
	{ 117, 0, 1, "OldYield", "", api_OldYield16, 0 },
	{ 118, 0, 1, "GetTaskQueueDS", "", api_GetTaskQueueDS16, 0 },
	{ 119, 0, 1, "GetTaskQueueES", "", api_GetTaskQueueES16, 0 },
	{ 120, 3, 0, "UndefDynLink", 0, 0, 0 },
	{ 121, 0, 1, "LocalShrink", "ww", api_LocalShrink16, 0 },
	{ 122, 0, 1, "IsTaskLocked", "", api_IsTaskLocked16, 0 },
	{ 123, 0, 1, "KbdRst", "", api_KERNEL_nop, 0 },
	{ 124, 0, 1, "EnableKernel", "", api_KERNEL_nop, 0 },
	{ 125, 0, 1, "DisableKernel", "", api_KERNEL_nop, 0 },
	{ 126, 3, 0, "MemoryFreed", 0, 0, 0 },
	{ 127, 0, 1, "GetPrivateProfileInt", "ttst", api_GetPrivateProfileInt16, 0 },
	{ 128, 0, 1, "GetPrivateProfileString", "tttpwt", api_GetPrivateProfileString16, 0 },
	{ 129, 0, 1, "WritePrivateProfileString", "tttt", api_WritePrivateProfileString16, 0 },
	{ 130, 0, 0, "FileCDR", "p", api_FileCDR16, 0 },
	{ 131, 0, 0, "GetDOSEnvironment", "", api_GetDOSEnvironment16, 0 },
	{ 132, 0, 0, "GetWinFlags", "", api_GetWinFlags16, 0 },
	{ 133, 0, 1, "GetExePtr", "w", api_WIN16_GetExePtr, 0 },
	{ 134, 0, 1, "GetWindowsDirectory", "pw", api_GetWindowsDirectory16, 0 },
	{ 135, 0, 1, "GetSystemDirectory", "pw", api_GetSystemDirectory16, 0 },
	{ 136, 0, 1, "GetDriveType", "w", api_GetDriveType16, 0 },
	{ 137, 0, 1, "FatalAppExit", "wt", api_FatalAppExit16, 0 },
	{ 138, 0, 0, "GetHeapSpaces", "w", api_GetHeapSpaces16, 0 },
	{ 139, 3, 0, "DoSignal", 0, 0, 0 },
	{ 140, 0, 1, "SetSigHandler", "Pppww", api_SetSigHandler16, 0 },
	{ 141, 3, 0, "InitTask1", 0, 0, 0 },
	{ 142, 0, 1, "GetProfileSectionNames", "pw", api_GetProfileSectionNames16, 0 },
	{ 143, 0, 1, "GetPrivateProfileSectionNames", "pwt", api_GetPrivateProfileSectionNames16, 0 },
	{ 144, 0, 1, "CreateDirectory", "pp", api_CreateDirectory16, 0 },
	{ 145, 0, 1, "RemoveDirectory", "p", api_RemoveDirectory16, 0 },
	{ 146, 0, 1, "DeleteFile", "p", api_DeleteFile16, 0 },
	{ 147, 0, 1, "SetLastError", "l", api_SetLastError16, 0 },
	{ 148, 0, 0, "GetLastError", "", api_GetLastError16, 0 },
	{ 149, 0, 1, "GetVersionEx", "p", api_GetVersionEx16, 0 },
	{ 150, 0, 1, "DirectedYield", "w", api_DirectedYield16, 0 },
	{ 151, 3, 0, "WinOldApCall", 0, 0, 0 },
	{ 152, 0, 1, "GetNumTasks", "", api_GetNumTasks16, 0 },
	{ 154, 0, 1, "GlobalNotify", "P", api_GlobalNotify16, 0 },
	{ 155, 0, 1, "GetTaskDS", "", api_GetTaskDS16, 0 },
	{ 156, 0, 0, "LimitEMSPages", "l", api_LimitEMSPages16, 0 },
	{ 157, 0, 0, "GetCurPID", "l", api_GetCurPID16, 0 },
	{ 158, 0, 1, "IsWinOldApTask", "w", api_IsWinOldApTask16, 0 },
	{ 159, 0, 0, "GlobalHandleNoRIP", "w", api_GlobalHandleNoRIP16, 0 },
	{ 160, 3, 0, "EMSCopy", 0, 0, 0 },
	{ 161, 0, 1, "LocalCountFree", "", api_LocalCountFree16, 0 },
	{ 162, 0, 1, "LocalHeapSize", "", api_LocalHeapSize16, 0 },
	{ 163, 0, 1, "GlobalLRUOldest", "w", api_GlobalLRUOldest16, 0 },
	{ 164, 0, 1, "GlobalLRUNewest", "w", api_GlobalLRUNewest16, 0 },
	{ 165, 0, 1, "A20Proc", "w", api_A20Proc16, 0 },
	{ 166, 0, 1, "WinExec", "tw", api_WinExec16, 0 },
	{ 167, 0, 1, "GetExpWinVer", "w", api_GetExpWinVer16, 0 },
	{ 168, 0, 1, "DirectResAlloc", "www", api_DirectResAlloc16, 0 },
	{ 169, 0, 0, "GetFreeSpace", "w", api_GetFreeSpace16, 0 },
	{ 170, 0, 1, "AllocCStoDSAlias", "w", api_AllocCStoDSAlias16, 0 },
	{ 171, 0, 1, "AllocDStoCSAlias", "w", api_AllocDStoCSAlias16, 0 },
	{ 172, 0, 1, "AllocAlias", "w", api_AllocCStoDSAlias16, 0 },
	{ 173, 4, 0, "__ROMBIOS", 0, 0, 0 },
	{ 174, 4, 0, "__A000H", 0, 0, 0 },
	{ 175, 0, 1, "AllocSelector", "w", api_AllocSelector16, 0 },
	{ 176, 0, 1, "FreeSelector", "w", api_WIN16_FreeSelector16, 0 },
	{ 177, 0, 1, "PrestoChangoSelector", "ww", api_PrestoChangoSelector16, 0 },
	{ 178, 4, 0, "__WINFLAGS", 0, 0, 1043 },
	{ 179, 4, 0, "__D000H", 0, 0, 0 },
	{ 180, 0, 0, "LongPtrAdd", "ll", api_LongPtrAdd16, 0 },
	{ 181, 4, 0, "__B000H", 0, 0, 0 },
	{ 182, 4, 0, "__B800H", 0, 0, 0 },
	{ 183, 4, 0, "__0000H", 0, 0, 0 },
	{ 184, 0, 0, "GlobalDOSAlloc", "l", api_GlobalDOSAlloc16, 0 },
	{ 185, 0, 1, "GlobalDOSFree", "w", api_GlobalDOSFree16, 0 },
	{ 186, 0, 0, "GetSelectorBase", "w", api_GetSelectorBase, 0 },
	{ 187, 0, 1, "SetSelectorBase", "wl", api_SetSelectorBase, 0 },
	{ 188, 0, 0, "GetSelectorLimit", "w", api_GetSelectorLimit16, 0 },
	{ 189, 0, 1, "SetSelectorLimit", "wl", api_SetSelectorLimit16, 0 },
	{ 190, 4, 0, "__E000H", 0, 0, 0 },
	{ 191, 0, 1, "GlobalPageLock", "w", api_GlobalPageLock16, 0 },
	{ 192, 0, 1, "GlobalPageUnlock", "w", api_GlobalPageUnlock16, 0 },
	{ 193, 4, 0, "__0040H", 0, 0, 0 },
	{ 194, 4, 0, "__F000H", 0, 0, 0 },
	{ 195, 4, 0, "__C000H", 0, 0, 0 },
	{ 196, 0, 1, "SelectorAccessRights", "www", api_SelectorAccessRights16, 0 },
	{ 197, 0, 1, "GlobalFix", "w", api_GlobalFix16, 0 },
	{ 198, 0, 1, "GlobalUnfix", "w", api_GlobalUnfix16, 0 },
	{ 199, 0, 1, "SetHandleCount", "w", api_SetHandleCount16, 0 },
	{ 200, 0, 1, "ValidateFreeSpaces", "", api_KERNEL_nop, 0 },
	{ 201, 3, 0, "ReplaceInst", 0, 0, 0 },
	{ 202, 3, 0, "RegisterPtrace", 0, 0, 0 },
	{ 203, 1, 0, "DebugBreak", "", api_DebugBreak16, 0 },
	{ 204, 3, 0, "SwapRecording", 0, 0, 0 },
	{ 205, 3, 0, "CVWBreak", 0, 0, 0 },
	{ 206, 0, 1, "AllocSelectorArray", "w", api_AllocSelectorArray16, 0 },
	{ 207, 0, 1, "IsDBCSLeadByte", "w", api_IsDBCSLeadByte, 0 },
	{ 208, 0, 0, "K208", "wlll", api_Local32Init16, 0 },
	{ 209, 0, 0, "K209", "llwl", api_Local32Alloc16, 0 },
	{ 210, 0, 0, "K210", "llwll", api_Local32ReAlloc16, 0 },
	{ 211, 0, 0, "K211", "llw", api_Local32Free16, 0 },
	{ 213, 0, 0, "K213", "llww", api_Local32Translate16, 0 },
	{ 214, 0, 0, "K214", "llw", api_Local32Size16, 0 },
	{ 215, 0, 0, "K215", "lw", api_Local32ValidHandle16, 0 },
	{ 216, 0, 0, "RegEnumKey", "llpl", api_RegEnumKey16, 0 },
	{ 217, 0, 0, "RegOpenKey", "ltp", api_RegOpenKey16, 0 },
	{ 218, 0, 0, "RegCreateKey", "ltp", api_RegCreateKey16, 0 },
	{ 219, 0, 0, "RegDeleteKey", "lt", api_RegDeleteKey16, 0 },
	{ 220, 0, 0, "RegCloseKey", "l", api_RegCloseKey16, 0 },
	{ 221, 0, 0, "RegSetValue", "ltlpl", api_RegSetValue16, 0 },
	{ 222, 0, 0, "RegDeleteValue", "lt", api_RegDeleteValue16, 0 },
	{ 223, 0, 0, "RegEnumValue", "llpppppp", api_RegEnumValue16, 0 },
	{ 224, 0, 0, "RegQueryValue", "ltpp", api_RegQueryValue16, 0 },
	{ 225, 0, 0, "RegQueryValueEx", "ltpppp", api_RegQueryValueEx16, 0 },
	{ 226, 0, 0, "RegSetValueEx", "ltllpl", api_RegSetValueEx16, 0 },
	{ 227, 0, 0, "RegFlushKey", "l", api_RegFlushKey16, 0 },
	{ 228, 0, 1, "K228", "w", api_GetExePtr, 0 },
	{ 229, 0, 1, "K229", "l", api_Local32GetSegment16, 0 },
	{ 230, 0, 0, "GlobalSmartPageLock", "w", api_GlobalPageLock16, 0 },
	{ 231, 0, 0, "GlobalSmartPageUnlock", "w", api_GlobalPageUnlock16, 0 },
	{ 232, 3, 0, "RegLoadKey", 0, 0, 0 },
	{ 233, 3, 0, "RegUnloadKey", 0, 0, 0 },
	{ 234, 3, 0, "RegSaveKey", 0, 0, 0 },
	{ 235, 3, 0, "InvalidateNlsCache", 0, 0, 0 },
	{ 236, 3, 0, "GetProductName", 0, 0, 0 },
	{ 237, 0, 1, "K237", "", api_KERNEL_nop, 0 },
	{ 262, 3, 0, "WOWWaitForMsgAndEvent", 0, 0, 0 },
	{ 263, 3, 0, "WOWMsgBox", 0, 0, 0 },
	{ 273, 3, 0, "K273", 0, 0, 0 },
	{ 274, 0, 1, "GetShortPathName", "tpw", api_GetShortPathName16, 0 },
	{ 310, 0, 1, "LocalHandleDelta", "w", api_LocalHandleDelta16, 0 },
	{ 311, 0, 0, "GetSetKernelDOSProc", "p", api_GetSetKernelDOSProc16, 0 },
	{ 314, 3, 0, "DebugDefineSegment", 0, 0, 0 },
	{ 315, 0, 1, "WriteOutProfiles", "", api_WriteOutProfiles16, 0 },
	{ 316, 0, 0, "GetFreeMemInfo", "", api_GetFreeMemInfo16, 0 },
	{ 318, 3, 0, "FatalExitHook", 0, 0, 0 },
	{ 319, 3, 0, "FlushCachedFileHandle", 0, 0, 0 },
	{ 320, 0, 1, "IsTask", "w", api_IsTask16, 0 },
	{ 323, 0, 1, "IsRomModule", "w", api_IsRomModule16, 0 },
	{ 324, 0, 1, "LogError", "wp", api_LogError16, 0 },
	{ 325, 0, 1, "LogParamError", "wpp", api_LogParamError16, 0 },
	{ 326, 0, 1, "IsRomFile", "w", api_IsRomFile16, 0 },
	{ 327, 1, 0, "K327", "", api_HandleParamError, 0 },
	{ 328, 2, 1, "_DebugOutput", "wt", api__DebugOutput, 0 },
	{ 329, 0, 1, "K329", "tw", api_DebugFillBuffer, 0 },
	{ 332, 3, 0, "THHOOK", 0, 0, 0 },
	{ 334, 0, 1, "IsBadReadPtr", "Pw", api_IsBadReadPtr16, 0 },
	{ 335, 0, 1, "IsBadWritePtr", "Pw", api_IsBadWritePtr16, 0 },
	{ 336, 0, 1, "IsBadCodePtr", "P", api_IsBadCodePtr16, 0 },
	{ 337, 0, 1, "IsBadStringPtr", "Pw", api_IsBadStringPtr16, 0 },
	{ 338, 0, 1, "HasGPHandler", "P", api_HasGPHandler16, 0 },
	{ 339, 0, 1, "DiagQuery", "", api_DiagQuery16, 0 },
	{ 340, 0, 1, "DiagOutput", "t", api_DiagOutput16, 0 },
	{ 341, 0, 0, "ToolHelpHook", "p", api_ToolHelpHook16, 0 },
	{ 342, 3, 0, "__GP", 0, 0, 0 },
	{ 343, 3, 0, "RegisterWinOldApHook", 0, 0, 0 },
	{ 344, 3, 0, "GetWinOldApHooks", 0, 0, 0 },
	{ 345, 0, 1, "IsSharedSelector", "w", api_IsSharedSelector16, 0 },
	{ 346, 0, 1, "IsBadHugeReadPtr", "Pl", api_IsBadHugeReadPtr16, 0 },
	{ 347, 0, 1, "IsBadHugeWritePtr", "Pl", api_IsBadHugeWritePtr16, 0 },
	{ 348, 0, 1, "hmemcpy", "ppl", api_hmemcpy16, 0 },
	{ 349, 0, 0, "_hread", "wPl", api_WIN16_hread, 0 },
	{ 350, 0, 0, "_hwrite", "wpl", api__hwrite16, 0 },
	{ 351, 0, 1, "BUNNY_351", "", api_KERNEL_nop, 0 },
	{ 352, 0, 0, "lstrcatn", "Ttw", api_lstrcatn16, 0 },
	{ 353, 0, 0, "lstrcpyn", "Ptw", api_lstrcpyn16, 0 },
	{ 354, 0, 0, "GetAppCompatFlags", "w", api_GetAppCompatFlags16, 0 },
	{ 355, 0, 1, "GetWinDebugInfo", "pw", api_GetWinDebugInfo16, 0 },
	{ 356, 0, 1, "SetWinDebugInfo", "p", api_SetWinDebugInfo16, 0 },
	{ 357, 0, 0, "MapSL", "P", api_MapSL, 0 },
	{ 358, 0, 0, "MapLS", "l", api_MapLS, 0 },
	{ 359, 0, 0, "UnMapLS", "P", api_UnMapLS, 0 },
	{ 360, 0, 1, "OpenFileEx", "tpw", api_OpenFile16, 0 },
	{ 361, 0, 1, "PIGLET_361", "", api_KERNEL_nop, 0 },
	{ 362, 3, 0, "ThunkTerminateProcess", 0, 0, 0 },
	{ 365, 1, 0, "GlobalChangeLockCount", "ww", api_GlobalChangeLockCount16, 0 },
	{ 403, 0, 1, "FarSetOwner", "ww", api_FarSetOwner16, 0 },
	{ 404, 0, 1, "FarGetOwner", "w", api_FarGetOwner16, 0 },
	{ 406, 0, 1, "WritePrivateProfileStruct", "ttpwt", api_WritePrivateProfileStruct16, 0 },
	{ 407, 0, 1, "GetPrivateProfileStruct", "ttpwt", api_GetPrivateProfileStruct16, 0 },
	{ 408, 3, 0, "KERNEL_408", 0, 0, 0 },
	{ 409, 3, 0, "KERNEL_409", 0, 0, 0 },
	{ 410, 3, 0, "CreateProcessFromWinExec", 0, 0, 0 },
	{ 411, 0, 0, "GetCurrentDirectory", "lp", api_GetCurrentDirectory16, 0 },
	{ 412, 0, 1, "SetCurrentDirectory", "p", api_SetCurrentDirectory16, 0 },
	{ 413, 0, 1, "FindFirstFile", "pp", api_FindFirstFile16, 0 },
	{ 414, 0, 1, "FindNextFile", "wp", api_FindNextFile16, 0 },
	{ 415, 0, 1, "FindClose", "w", api_FindClose16, 0 },
	{ 416, 0, 1, "WritePrivateProfileSection", "ttt", api_WritePrivateProfileSection16, 0 },
	{ 417, 0, 1, "WriteProfileSection", "tt", api_WriteProfileSection16, 0 },
	{ 418, 0, 1, "GetPrivateProfileSection", "tpwt", api_GetPrivateProfileSection16, 0 },
	{ 419, 0, 1, "GetProfileSection", "tpw", api_GetProfileSection16, 0 },
	{ 420, 0, 0, "GetFileAttributes", "p", api_GetFileAttributes16, 0 },
	{ 421, 0, 1, "SetFileAttributes", "pl", api_SetFileAttributes16, 0 },
	{ 422, 0, 1, "GetDiskFreeSpace", "ppppp", api_GetDiskFreeSpace16, 0 },
	{ 423, 0, 1, "LogApiThk", "t", api_LogApiThk, 0 },
	{ 431, 0, 1, "IsPeFormat", "tw", api_IsPeFormat16, 0 },
	{ 432, 3, 0, "FileTimeToLocalFileTime", 0, 0, 0 },
	{ 434, 0, 1, "UnicodeToAnsi", "ppw", api_UnicodeToAnsi16, 0 },
	{ 435, 3, 0, "GetTaskFlags", 0, 0, 0 },
	{ 436, 0, 1, "_ConfirmSysLevel", "p", api__ConfirmSysLevel, 0 },
	{ 437, 0, 1, "_CheckNotSysLevel", "p", api__CheckNotSysLevel, 0 },
	{ 438, 0, 1, "_CreateSysLevel", "pl", api__CreateSysLevel, 0 },
	{ 439, 0, 1, "_EnterSysLevel", "p", api__EnterSysLevel, 0 },
	{ 440, 0, 1, "_LeaveSysLevel", "p", api__LeaveSysLevel, 0 },
	{ 441, 0, 0, "CreateThread16", "plPPlp", api_CreateThread16, 0 },
	{ 442, 0, 0, "VWin32_EventCreate", "", api_VWin32_EventCreate, 0 },
	{ 443, 0, 0, "VWin32_EventDestroy", "l", api_VWin32_EventDestroy, 0 },
	{ 444, 0, 1, "Local32Info", "pw", api_Local32Info16, 0 },
	{ 445, 0, 1, "Local32First", "pw", api_Local32First16, 0 },
	{ 446, 0, 1, "Local32Next", "p", api_Local32Next16, 0 },
	{ 447, 0, 1, "WIN32_OldYield", "", api_WIN32_OldYield16, 0 },
	{ 448, 3, 0, "KERNEL_448", 0, 0, 0 },
	{ 449, 0, 0, "GetpWin16Lock", "", api_GetpWin16Lock16, 0 },
	{ 450, 0, 0, "VWin32_EventWait", "l", api_VWin32_EventWait, 0 },
	{ 451, 0, 0, "VWin32_EventSet", "l", api_VWin32_EventSet, 0 },
	{ 452, 0, 0, "LoadLibrary32", "t", api_LoadLibrary32_16, 0 },
	{ 453, 0, 0, "GetProcAddress32", "lt", api_GetProcAddress32_16, 0 },
	{ 454, 4, 0, "__FLATCS", 0, 0, 0 },
	{ 455, 4, 0, "__FLATDS", 0, 0, 0 },
	{ 456, 0, 0, "DefResourceHandler", "www", api_NE_DefResourceHandler, 0 },
	{ 457, 0, 0, "CreateW32Event", "ll", api_CreateW32Event, 0 },
	{ 458, 0, 0, "SetW32Event", "l", api_SetW32Event, 0 },
	{ 459, 0, 0, "ResetW32Event", "l", api_ResetW32Event, 0 },
	{ 460, 0, 0, "WaitForSingleObject", "ll", api_WaitForSingleObject16, 0 },
	{ 461, 0, 0, "WaitForMultipleObjects", "lpll", api_WaitForMultipleObjects16, 0 },
	{ 462, 0, 0, "GetCurrentThreadId", "", api_GetCurrentThreadId16, 0 },
	{ 463, 0, 0, "SetThreadQueue", "lw", api_SetThreadQueue16, 0 },
	{ 464, 0, 0, "GetThreadQueue", "l", api_GetThreadQueue16, 0 },
	{ 465, 3, 0, "NukeProcess", 0, 0, 0 },
	{ 466, 0, 1, "ExitProcess", "w", api_ExitProcess16, 0 },
	{ 467, 3, 0, "WOACreateConsole", 0, 0, 0 },
	{ 468, 3, 0, "WOASpawnConApp", 0, 0, 0 },
	{ 469, 3, 0, "WOAGimmeTitle", 0, 0, 0 },
	{ 470, 3, 0, "WOADestroyConsole", 0, 0, 0 },
	{ 471, 0, 0, "GetCurrentProcessId", "", api_GetCurrentProcessId16, 0 },
	{ 472, 1, 0, "MapHInstLS", "", api_MapHInstLS16, 0 },
	{ 473, 1, 0, "MapHInstSL", "", api_MapHInstSL16, 0 },
	{ 474, 0, 0, "CloseW32Handle", "l", api_CloseW32Handle, 0 },
	{ 475, 0, 1, "GetTEBSelectorFS", "", api_GetTEBSelectorFS16, 0 },
	{ 476, 0, 0, "ConvertToGlobalHandle", "l", api_ConvertToGlobalHandle16, 0 },
	{ 477, 3, 0, "WOAFullScreen", 0, 0, 0 },
	{ 478, 3, 0, "WOATerminateProcess", 0, 0, 0 },
	{ 479, 0, 0, "KERNEL_479", "l", api_VWin32_EventSet, 0 },
	{ 480, 0, 1, "_EnterWin16Lock", "", api__EnterWin16Lock, 0 },
	{ 481, 0, 1, "_LeaveWin16Lock", "", api__LeaveWin16Lock, 0 },
	{ 482, 0, 0, "LoadSystemLibrary32", "t", api_LoadLibrary32_16, 0 },
	{ 483, 0, 0, "MapProcessHandle", "l", api_MapProcessHandle, 0 },
	{ 484, 0, 0, "SetProcessDword", "lsl", api_SetProcessDword, 0 },
	{ 485, 0, 0, "GetProcessDword", "ls", api_GetProcessDword, 0 },
	{ 486, 0, 0, "FreeLibrary32", "l", api_FreeLibrary32_16, 0 },
	{ 487, 0, 0, "GetModuleFileName32", "ltw", api_GetModuleFileName32_16, 0 },
	{ 488, 0, 0, "GetModuleHandle32", "t", api_GetModuleHandle32_16, 0 },
	{ 489, 3, 0, "KERNEL_489", 0, 0, 0 },
	{ 490, 0, 1, "KERNEL_490", "w", api_KERNEL_490, 0 },
	{ 491, 0, 0, "RegisterServiceProcess", "ll", api_RegisterServiceProcess16, 0 },
	{ 492, 3, 0, "WOAAbort", 0, 0, 0 },
	{ 493, 0, 1, "UTInit", "llll", api_UTInit16, 0 },
	{ 494, 3, 0, "KERNEL_494", 0, 0, 0 },
	{ 495, 0, 0, "WaitForMultipleObjectsEx", "lplll", api_WaitForMultipleObjectsEx16, 0 },
	{ 500, 2, 0, "WOW16Call", "www", api_WOW16Call, 0 },
	{ 501, 3, 0, "KDDBGOUT", 0, 0, 0 },
	{ 502, 3, 0, "WOWGETNEXTVDMCOMMAND", 0, 0, 0 },
	{ 503, 3, 0, "WOWREGISTERSHELLWINDOWHANDLE", 0, 0, 0 },
	{ 504, 3, 0, "WOWLOADMODULE", 0, 0, 0 },
	{ 505, 3, 0, "WOWQUERYPERFORMANCECOUNTER", 0, 0, 0 },
	{ 506, 3, 0, "WOWCURSORICONOP", 0, 0, 0 },
	{ 507, 3, 0, "WOWFAILEDEXEC", 0, 0, 0 },
	{ 508, 3, 0, "WOWCLOSECOMPORT", 0, 0, 0 },
	{ 511, 3, 0, "WOWKILLREMOTETASK", 0, 0, 0 },
	{ 512, 3, 0, "WOWQUERYDEBUG", 0, 0, 0 },
	{ 513, 0, 0, "LoadLibraryEx32W", "tll", api_LoadLibraryEx32W16, 0 },
	{ 514, 0, 0, "FreeLibrary32W", "l", api_FreeLibrary32W16, 0 },
	{ 515, 0, 0, "GetProcAddress32W", "lt", api_GetProcAddress32W16, 0 },
	{ 516, 0, 0, "GetVDMPointer32W", "Pw", api_GetVDMPointer32W16, 0 },
	{ 517, 2, 0, "CallProc32W", "lll", api_CallProc32W16, 0 },
	{ 518, 2, 0, "_CallProcEx32W", "lll", api_CallProcEx32W16, 0 },
	{ 519, 3, 0, "EXITKERNELTHUNK", 0, 0, 0 },
	{ 520, 4, 0, "__MOD_KERNEL", 0, 0, 4200 },
	{ 521, 4, 0, "__MOD_DKERNEL", 0, 0, 4201 },
	{ 522, 4, 0, "__MOD_USER", 0, 0, 4203 },
	{ 523, 4, 0, "__MOD_DUSER", 0, 0, 4204 },
	{ 524, 4, 0, "__MOD_GDI", 0, 0, 4205 },
	{ 525, 4, 0, "__MOD_DGDI", 0, 0, 4206 },
	{ 526, 4, 0, "__MOD_KEYBOARD", 0, 0, 4207 },
	{ 527, 4, 0, "__MOD_SOUND", 0, 0, 4208 },
	{ 528, 4, 0, "__MOD_SHELL", 0, 0, 4209 },
	{ 529, 4, 0, "__MOD_WINSOCK", 0, 0, 4210 },
	{ 530, 4, 0, "__MOD_TOOLHELP", 0, 0, 4211 },
	{ 531, 4, 0, "__MOD_MMEDIA", 0, 0, 4212 },
	{ 532, 4, 0, "__MOD_COMMDLG", 0, 0, 4213 },
	{ 533, 3, 0, "ConvertDDEHandleLS", 0, 0, 0 },
	{ 534, 3, 0, "ConvertDDEHandleSL", 0, 0, 0 },
	{ 535, 0, 0, "VWin32_BoostThreadGroup", "ll", api_VWin32_BoostThreadGroup, 0 },
	{ 536, 0, 0, "VWin32_BoostThreadStatic", "ll", api_VWin32_BoostThreadStatic, 0 },
	{ 537, 3, 0, "KERNEL_537", 0, 0, 0 },
	{ 538, 3, 0, "ThunkTheTemplateHandle", 0, 0, 0 },
	{ 540, 3, 0, "KERNEL_540", 0, 0, 0 },
	{ 541, 3, 0, "WOWSETEXITONLASTAPP", 0, 0, 0 },
	{ 542, 3, 0, "KERNEL_542", 0, 0, 0 },
	{ 543, 3, 0, "KERNEL_543", 0, 0, 0 },
	{ 544, 3, 0, "WOWSETCOMPATHANDLE", 0, 0, 0 },
	{ 560, 0, 0, "SetThunkletCallbackGlue", "lP", api_SetThunkletCallbackGlue16, 0 },
	{ 561, 0, 0, "AllocLSThunkletCallback", "Pl", api_AllocLSThunkletCallback16, 0 },
	{ 562, 0, 0, "AllocSLThunkletCallback", "ll", api_AllocSLThunkletCallback16, 0 },
	{ 563, 0, 0, "FindLSThunkletCallback", "Pl", api_FindLSThunkletCallback, 0 },
	{ 564, 0, 0, "FindSLThunkletCallback", "ll", api_FindSLThunkletCallback, 0 },
	{ 566, 3, 0, "KERNEL_566", 0, 0, 0 },
	{ 567, 0, 0, "AllocLSThunkletCallbackEx", "Plw", api_AllocLSThunkletCallbackEx16, 0 },
	{ 568, 0, 0, "AllocSLThunkletCallbackEx", "llw", api_AllocSLThunkletCallbackEx16, 0 },
	{ 600, 3, 0, "AllocCodeAlias", 0, 0, 0 },
	{ 601, 3, 0, "FreeCodeAlias", 0, 0, 0 },
	{ 602, 0, 1, "GetDummyModuleHandleDS", "", api_GetDummyModuleHandleDS16, 0 },
	{ 603, 3, 0, "KERNEL_603", 0, 0, 0 },
	{ 604, 1, 0, "CBClientGlueSL", "", api_CBClientGlueSL, 0 },
	{ 605, 0, 0, "AllocSLThunkletCallback_dup", "ll", api_AllocSLThunkletCallback16, 0 },
	{ 606, 0, 0, "AllocLSThunkletCallback_dup", "Pl", api_AllocLSThunkletCallback16, 0 },
	{ 607, 0, 0, "AllocLSThunkletSysthunk", "Pll", api_AllocLSThunkletSysthunk16, 0 },
	{ 608, 0, 0, "AllocSLThunkletSysthunk", "lPl", api_AllocSLThunkletSysthunk16, 0 },
	{ 609, 0, 0, "FindLSThunkletCallback_dup", "Pl", api_FindLSThunkletCallback, 0 },
	{ 610, 0, 0, "FindSLThunkletCallback_dup", "ll", api_FindSLThunkletCallback, 0 },
	{ 611, 0, 1, "FreeThunklet", "ll", api_FreeThunklet16, 0 },
	{ 612, 0, 1, "IsSLThunklet", "p", api_IsSLThunklet16, 0 },
	{ 613, 3, 0, "HugeMapLS", 0, 0, 0 },
	{ 614, 3, 0, "HugeUnMapLS", 0, 0, 0 },
	{ 615, 0, 1, "ConvertDialog32To16", "lll", api_ConvertDialog32To16, 0 },
	{ 616, 0, 1, "ConvertMenu32To16", "lll", api_ConvertMenu32To16, 0 },
	{ 617, 0, 1, "GetMenu32Size", "p", api_GetMenu32Size16, 0 },
	{ 618, 0, 1, "GetDialog32Size", "p", api_GetDialog32Size16, 0 },
	{ 619, 0, 1, "RegisterCBClient", "wPl", api_RegisterCBClient16, 0 },
	{ 620, 1, 0, "CBClientThunkSL", "", api_CBClientThunkSL, 0 },
	{ 621, 1, 0, "CBClientThunkSLEx", "", api_CBClientThunkSLEx, 0 },
	{ 622, 0, 1, "UnRegisterCBClient", "wPl", api_UnRegisterCBClient16, 0 },
	{ 623, 0, 1, "InitCBClient", "l", api_InitCBClient16, 0 },
	{ 624, 0, 0, "SetFastQueue", "ll", api_SetFastQueue16, 0 },
	{ 625, 0, 0, "GetFastQueue", "", api_GetFastQueue16, 0 },
	{ 626, 3, 0, "SmashEnvironment", 0, 0, 0 },
	{ 627, 0, 1, "IsBadFlatReadWritePtr", "Plw", api_IsBadFlatReadWritePtr16, 0 },
	{ 630, 1, 0, "C16ThkSL", "", api_C16ThkSL, 0 },
	{ 631, 1, 0, "C16ThkSL01", "", api_C16ThkSL01, 0 },
	{ 651, 0, 0, "ThunkConnect16", "ttwlptw", api_ThunkConnect16, 0 },
	{ 652, 3, 0, "IsThreadId", 0, 0, 0 },
	{ 653, 3, 0, "OkWithKernelToChangeUsers", 0, 0, 0 },
	{ 666, 0, 0, "UTGlue16", "plpl", api_UTGlue16, 0 },
	{ 667, 0, 0, "EntryAddrProc", "ww", api_EntryAddrProc16, 0 },
	{ 668, 0, 0, "MyAlloc", "www", api_MyAlloc16, 0 },
	{ 669, 0, 1, "DllEntryPoint", "lwwwlw", api_KERNEL_DllEntryPoint, 0 },
	{ 700, 0, 0, "SSInit", "", api_SSInit16, 0 },
	{ 701, 3, 0, "SSOnBigStack", 0, 0, 0 },
	{ 702, 3, 0, "SSCall", 0, 0, 0 },
	{ 703, 3, 0, "CallProc32WFix", 0, 0, 0 },
	{ 704, 1, 0, "SSConfirmSmallStack", "", api_SSConfirmSmallStack, 0 },
	{ 901, 1, 0, "__wine_vxd_vmm", "", api___wine_vxd_vmm, 0 },
	{ 905, 1, 0, "__wine_vxd_timer", "", api___wine_vxd_timer, 0 },
	{ 909, 1, 0, "__wine_vxd_reboot", "", api___wine_vxd_reboot, 0 },
	{ 910, 1, 0, "__wine_vxd_vdd", "", api___wine_vxd_vdd, 0 },
	{ 912, 1, 0, "__wine_vxd_vmd", "", api___wine_vxd_vmd, 0 },
	{ 914, 1, 0, "__wine_vxd_comm", "", api___wine_vxd_comm, 0 },
	{ 923, 1, 0, "__wine_vxd_shell", "", api___wine_vxd_shell, 0 },
	{ 933, 1, 0, "__wine_vxd_pagefile", "", api___wine_vxd_pagefile, 0 },
	{ 938, 1, 0, "__wine_vxd_apm", "", api___wine_vxd_apm, 0 },
	{ 939, 1, 0, "__wine_vxd_vxdloader", "", api___wine_vxd_vxdloader, 0 },
	{ 945, 1, 0, "__wine_vxd_win32s", "", api___wine_vxd_win32s, 0 },
	{ 951, 1, 0, "__wine_vxd_configmg", "", api___wine_vxd_configmg, 0 },
	{ 955, 1, 0, "__wine_vxd_enable", "", api___wine_vxd_enable, 0 },
	{ 1990, 1, 0, "__wine_vxd_timerapi", "", api___wine_vxd_timerapi, 0 },
};

static const struct w16_api api_user[] = {
	{ 1, 0, 1, "MessageBox", "wttw", api_MessageBox16, 0 },
	{ 2, 0, 1, "OldExitWindows", "", api_OldExitWindows16, 0 },
	{ 3, 3, 0, "EnableOEMLayer", 0, 0, 0 },
	{ 4, 3, 0, "DisableOEMLayer", 0, 0, 0 },
	{ 5, 0, 1, "InitApp", "w", api_InitApp16, 0 },
	{ 6, 0, 1, "PostQuitMessage", "w", api_PostQuitMessage16, 0 },
	{ 7, 0, 1, "ExitWindows", "lw", api_ExitWindows16, 0 },
	{ 10, 0, 1, "SetTimer", "wwwP", api_SetTimer16, 0 },
	{ 11, 0, 1, "SetSystemTimer", "wwwP", api_SetSystemTimer16, 0 },
	{ 12, 0, 1, "KillTimer", "ww", api_KillTimer16, 0 },
	{ 13, 0, 0, "GetTickCount", "", api_GetTickCount, 0 },
	{ 14, 0, 0, "GetTimerResolution", "", api_GetTimerResolution16, 0 },
	{ 15, 0, 0, "GetCurrentTime", "", api_GetTickCount, 0 },
	{ 16, 0, 1, "ClipCursor", "p", api_ClipCursor16, 0 },
	{ 17, 0, 1, "GetCursorPos", "p", api_GetCursorPos16, 0 },
	{ 18, 0, 1, "SetCapture", "w", api_SetCapture16, 0 },
	{ 19, 0, 1, "ReleaseCapture", "", api_ReleaseCapture16, 0 },
	{ 20, 0, 1, "SetDoubleClickTime", "w", api_SetDoubleClickTime16, 0 },
	{ 21, 0, 1, "GetDoubleClickTime", "", api_GetDoubleClickTime16, 0 },
	{ 22, 0, 1, "SetFocus", "w", api_SetFocus16, 0 },
	{ 23, 0, 1, "GetFocus", "", api_GetFocus16, 0 },
	{ 24, 0, 1, "RemoveProp", "wp", api_RemoveProp16, 0 },
	{ 25, 0, 1, "GetProp", "wt", api_GetProp16, 0 },
	{ 26, 0, 1, "SetProp", "wtw", api_SetProp16, 0 },
	{ 27, 0, 1, "EnumProps", "wP", api_EnumProps16, 0 },
	{ 28, 0, 1, "ClientToScreen", "wp", api_ClientToScreen16, 0 },
	{ 29, 0, 1, "ScreenToClient", "wp", api_ScreenToClient16, 0 },
	{ 30, 0, 1, "WindowFromPoint", "l", api_WindowFromPoint16, 0 },
	{ 31, 0, 1, "IsIconic", "w", api_IsIconic16, 0 },
	{ 32, 0, 1, "GetWindowRect", "wp", api_GetWindowRect16, 0 },
	{ 33, 0, 1, "GetClientRect", "wp", api_GetClientRect16, 0 },
	{ 34, 0, 1, "EnableWindow", "ww", api_EnableWindow16, 0 },
	{ 35, 0, 1, "IsWindowEnabled", "w", api_IsWindowEnabled16, 0 },
	{ 36, 0, 1, "GetWindowText", "wPw", api_GetWindowText16, 0 },
	{ 37, 0, 1, "SetWindowText", "wT", api_SetWindowText16, 0 },
	{ 38, 0, 1, "GetWindowTextLength", "w", api_GetWindowTextLength16, 0 },
	{ 39, 0, 1, "BeginPaint", "wp", api_BeginPaint16, 0 },
	{ 40, 0, 1, "EndPaint", "wp", api_EndPaint16, 0 },
	{ 41, 0, 1, "CreateWindow", "ttlsssswwwP", api_CreateWindow16, 0 },
	{ 42, 0, 1, "ShowWindow", "ww", api_ShowWindow16, 0 },
	{ 43, 0, 1, "CloseWindow", "w", api_CloseWindow16, 0 },
	{ 44, 0, 1, "OpenIcon", "w", api_OpenIcon16, 0 },
	{ 45, 0, 1, "BringWindowToTop", "w", api_BringWindowToTop16, 0 },
	{ 46, 0, 1, "GetParent", "w", api_GetParent16, 0 },
	{ 47, 0, 1, "IsWindow", "w", api_IsWindow16, 0 },
	{ 48, 0, 1, "IsChild", "ww", api_IsChild16, 0 },
	{ 49, 0, 1, "IsWindowVisible", "w", api_IsWindowVisible16, 0 },
	{ 50, 0, 1, "FindWindow", "tt", api_FindWindow16, 0 },
	{ 51, 3, 0, "BEAR51", 0, 0, 0 },
	{ 52, 0, 1, "AnyPopup", "", api_AnyPopup16, 0 },
	{ 53, 0, 1, "DestroyWindow", "w", api_DestroyWindow16, 0 },
	{ 54, 0, 1, "EnumWindows", "Pl", api_EnumWindows16, 0 },
	{ 55, 0, 1, "EnumChildWindows", "wPl", api_EnumChildWindows16, 0 },
	{ 56, 0, 1, "MoveWindow", "wwwwww", api_MoveWindow16, 0 },
	{ 57, 0, 1, "RegisterClass", "p", api_RegisterClass16, 0 },
	{ 58, 0, 1, "GetClassName", "wpw", api_GetClassName16, 0 },
	{ 59, 0, 1, "SetActiveWindow", "w", api_SetActiveWindow16, 0 },
	{ 60, 0, 1, "GetActiveWindow", "", api_GetActiveWindow16, 0 },
	{ 61, 0, 1, "ScrollWindow", "wsspp", api_ScrollWindow16, 0 },
	{ 62, 0, 1, "SetScrollPos", "wwsw", api_SetScrollPos16, 0 },
	{ 63, 0, 1, "GetScrollPos", "ww", api_GetScrollPos16, 0 },
	{ 64, 0, 1, "SetScrollRange", "wwssw", api_SetScrollRange16, 0 },
	{ 65, 0, 1, "GetScrollRange", "wwpp", api_GetScrollRange16, 0 },
	{ 66, 0, 1, "GetDC", "w", api_GetDC16, 0 },
	{ 67, 0, 1, "GetWindowDC", "w", api_GetWindowDC16, 0 },
	{ 68, 0, 1, "ReleaseDC", "ww", api_ReleaseDC16, 0 },
	{ 69, 0, 1, "SetCursor", "w", api_SetCursor16, 0 },
	{ 70, 0, 1, "SetCursorPos", "ww", api_SetCursorPos16, 0 },
	{ 71, 0, 1, "ShowCursor", "w", api_ShowCursor16, 0 },
	{ 72, 0, 1, "SetRect", "pssss", api_SetRect16, 0 },
	{ 73, 0, 1, "SetRectEmpty", "p", api_SetRectEmpty16, 0 },
	{ 74, 0, 1, "CopyRect", "pp", api_CopyRect16, 0 },
	{ 75, 0, 1, "IsRectEmpty", "p", api_IsRectEmpty16, 0 },
	{ 76, 0, 1, "PtInRect", "pl", api_PtInRect16, 0 },
	{ 77, 0, 1, "OffsetRect", "pss", api_OffsetRect16, 0 },
	{ 78, 0, 1, "InflateRect", "pss", api_InflateRect16, 0 },
	{ 79, 0, 1, "IntersectRect", "ppp", api_IntersectRect16, 0 },
	{ 80, 0, 1, "UnionRect", "ppp", api_UnionRect16, 0 },
	{ 81, 0, 1, "FillRect", "wpw", api_FillRect16, 0 },
	{ 82, 0, 1, "InvertRect", "wp", api_InvertRect16, 0 },
	{ 83, 0, 1, "FrameRect", "wpw", api_FrameRect16, 0 },
	{ 84, 0, 1, "DrawIcon", "wssw", api_DrawIcon16, 0 },
	{ 85, 0, 1, "DrawText", "wtspw", api_DrawText16, 0 },
	{ 86, 0, 0, "IconSize", "", api_IconSize16, 0 },
	{ 87, 0, 1, "DialogBox", "wtwP", api_DialogBox16, 0 },
	{ 88, 0, 1, "EndDialog", "ws", api_EndDialog16, 0 },
	{ 89, 0, 1, "CreateDialog", "wtwP", api_CreateDialog16, 0 },
	{ 90, 0, 1, "IsDialogMessage", "wP", api_IsDialogMessage16, 0 },
	{ 91, 0, 1, "GetDlgItem", "ww", api_GetDlgItem16, 0 },
	{ 92, 0, 1, "SetDlgItemText", "wwT", api_SetDlgItemText16, 0 },
	{ 93, 0, 1, "GetDlgItemText", "wwPw", api_GetDlgItemText16, 0 },
	{ 94, 0, 1, "SetDlgItemInt", "wwww", api_SetDlgItemInt16, 0 },
	{ 95, 0, 1, "GetDlgItemInt", "wspw", api_GetDlgItemInt16, 0 },
	{ 96, 0, 1, "CheckRadioButton", "wwww", api_CheckRadioButton16, 0 },
	{ 97, 0, 1, "CheckDlgButton", "www", api_CheckDlgButton16, 0 },
	{ 98, 0, 1, "IsDlgButtonChecked", "ww", api_IsDlgButtonChecked16, 0 },
	{ 99, 0, 1, "DlgDirSelect", "wpw", api_DlgDirSelect16, 0 },
	{ 100, 0, 1, "DlgDirList", "wtwww", api_DlgDirList16, 0 },
	{ 101, 0, 0, "SendDlgItemMessage", "wwwwl", api_SendDlgItemMessage16, 0 },
	{ 102, 0, 1, "AdjustWindowRect", "plw", api_AdjustWindowRect16, 0 },
	{ 103, 0, 1, "MapDialogRect", "wp", api_MapDialogRect16, 0 },
	{ 104, 0, 1, "MessageBeep", "w", api_MessageBeep16, 0 },
	{ 105, 0, 1, "FlashWindow", "ww", api_FlashWindow16, 0 },
	{ 106, 0, 1, "GetKeyState", "w", api_GetKeyState16, 0 },
	{ 107, 0, 0, "DefWindowProc", "wwwl", api_DefWindowProc16, 0 },
	{ 108, 0, 1, "GetMessage", "pwww", api_GetMessage16, 0 },
	{ 109, 0, 1, "PeekMessage", "pwwww", api_PeekMessage16, 0 },
	{ 110, 0, 1, "PostMessage", "wwwl", api_PostMessage16, 0 },
	{ 111, 0, 0, "SendMessage", "wwwl", api_SendMessage16, 0 },
	{ 112, 0, 1, "WaitMessage", "", api_WaitMessage16, 0 },
	{ 113, 0, 1, "TranslateMessage", "p", api_TranslateMessage16, 0 },
	{ 114, 0, 0, "DispatchMessage", "p", api_DispatchMessage16, 0 },
	{ 115, 0, 1, "ReplyMessage", "l", api_ReplyMessage16, 0 },
	{ 116, 0, 1, "PostAppMessage", "wwwl", api_PostAppMessage16, 0 },
	{ 117, 0, 1, "WindowFromDC", "w", api_WindowFromDC16, 0 },
	{ 118, 0, 1, "RegisterWindowMessage", "t", api_RegisterWindowMessageA, 0 },
	{ 119, 0, 0, "GetMessagePos", "", api_GetMessagePos, 0 },
	{ 120, 0, 0, "GetMessageTime", "", api_GetMessageTime, 0 },
	{ 121, 0, 0, "SetWindowsHook", "sP", api_SetWindowsHook16, 0 },
	{ 122, 0, 0, "CallWindowProc", "Pwwwl", api_CallWindowProc16, 0 },
	{ 123, 0, 1, "CallMsgFilter", "ps", api_CallMsgFilter16, 0 },
	{ 124, 0, 1, "UpdateWindow", "w", api_UpdateWindow16, 0 },
	{ 125, 0, 1, "InvalidateRect", "wpw", api_InvalidateRect16, 0 },
	{ 126, 0, 1, "InvalidateRgn", "www", api_InvalidateRgn16, 0 },
	{ 127, 0, 1, "ValidateRect", "wp", api_ValidateRect16, 0 },
	{ 128, 0, 1, "ValidateRgn", "ww", api_ValidateRgn16, 0 },
	{ 129, 0, 1, "GetClassWord", "ws", api_GetClassWord16, 0 },
	{ 130, 0, 1, "SetClassWord", "wsw", api_SetClassWord16, 0 },
	{ 131, 0, 0, "GetClassLong", "ws", api_GetClassLong16, 0 },
	{ 132, 0, 0, "SetClassLong", "wsl", api_SetClassLong16, 0 },
	{ 133, 0, 1, "GetWindowWord", "ws", api_GetWindowWord16, 0 },
	{ 134, 0, 1, "SetWindowWord", "wsw", api_SetWindowWord16, 0 },
	{ 135, 0, 0, "GetWindowLong", "ws", api_GetWindowLong16, 0 },
	{ 136, 0, 0, "SetWindowLong", "wsl", api_SetWindowLong16, 0 },
	{ 137, 0, 1, "OpenClipboard", "w", api_OpenClipboard16, 0 },
	{ 138, 0, 1, "CloseClipboard", "", api_CloseClipboard16, 0 },
	{ 139, 0, 1, "EmptyClipboard", "", api_EmptyClipboard16, 0 },
	{ 140, 0, 1, "GetClipboardOwner", "", api_GetClipboardOwner16, 0 },
	{ 141, 0, 1, "SetClipboardData", "ww", api_SetClipboardData16, 0 },
	{ 142, 0, 1, "GetClipboardData", "w", api_GetClipboardData16, 0 },
	{ 143, 0, 1, "CountClipboardFormats", "", api_CountClipboardFormats16, 0 },
	{ 144, 0, 1, "EnumClipboardFormats", "w", api_EnumClipboardFormats16, 0 },
	{ 145, 0, 1, "RegisterClipboardFormat", "t", api_RegisterClipboardFormat16, 0 },
	{ 146, 0, 1, "GetClipboardFormatName", "wps", api_GetClipboardFormatName16, 0 },
	{ 147, 0, 1, "SetClipboardViewer", "w", api_SetClipboardViewer16, 0 },
	{ 148, 0, 1, "GetClipboardViewer", "", api_GetClipboardViewer16, 0 },
	{ 149, 0, 1, "ChangeClipboardChain", "ww", api_ChangeClipboardChain16, 0 },
	{ 150, 0, 1, "LoadMenu", "wt", api_LoadMenu16, 0 },
	{ 151, 0, 1, "CreateMenu", "", api_CreateMenu16, 0 },
	{ 152, 0, 1, "DestroyMenu", "w", api_DestroyMenu16, 0 },
	{ 153, 0, 1, "ChangeMenu", "wwTww", api_ChangeMenu16, 0 },
	{ 154, 0, 1, "CheckMenuItem", "www", api_CheckMenuItem16, 0 },
	{ 155, 0, 1, "EnableMenuItem", "www", api_EnableMenuItem16, 0 },
	{ 156, 0, 1, "GetSystemMenu", "ww", api_GetSystemMenu16, 0 },
	{ 157, 0, 1, "GetMenu", "w", api_GetMenu16, 0 },
	{ 158, 0, 1, "SetMenu", "ww", api_SetMenu16, 0 },
	{ 159, 0, 1, "GetSubMenu", "ww", api_GetSubMenu16, 0 },
	{ 160, 0, 1, "DrawMenuBar", "w", api_DrawMenuBar16, 0 },
	{ 161, 0, 1, "GetMenuString", "wwpsw", api_GetMenuString16, 0 },
	{ 162, 0, 1, "HiliteMenuItem", "wwww", api_HiliteMenuItem16, 0 },
	{ 163, 0, 1, "CreateCaret", "wwww", api_CreateCaret16, 0 },
	{ 164, 0, 1, "DestroyCaret", "", api_DestroyCaret16, 0 },
	{ 165, 0, 1, "SetCaretPos", "ww", api_SetCaretPos16, 0 },
	{ 166, 0, 1, "HideCaret", "w", api_HideCaret16, 0 },
	{ 167, 0, 1, "ShowCaret", "w", api_ShowCaret16, 0 },
	{ 168, 0, 1, "SetCaretBlinkTime", "w", api_SetCaretBlinkTime16, 0 },
	{ 169, 0, 1, "GetCaretBlinkTime", "", api_GetCaretBlinkTime16, 0 },
	{ 170, 0, 1, "ArrangeIconicWindows", "w", api_ArrangeIconicWindows16, 0 },
	{ 171, 0, 1, "WinHelp", "wtwl", api_WinHelp16, 0 },
	{ 172, 0, 1, "SwitchToThisWindow", "ww", api_SwitchToThisWindow16, 0 },
	{ 173, 0, 1, "LoadCursor", "wt", api_LoadCursor16, 0 },
	{ 174, 0, 1, "LoadIcon", "wt", api_LoadIcon16, 0 },
	{ 175, 0, 1, "LoadBitmap", "wt", api_LoadBitmap16, 0 },
	{ 176, 0, 1, "LoadString", "wwps", api_LoadString16, 0 },
	{ 177, 0, 1, "LoadAccelerators", "wt", api_LoadAccelerators16, 0 },
	{ 178, 0, 1, "TranslateAccelerator", "wwp", api_TranslateAccelerator16, 0 },
	{ 179, 0, 1, "GetSystemMetrics", "s", api_GetSystemMetrics16, 0 },
	{ 180, 0, 0, "GetSysColor", "w", api_GetSysColor16, 0 },
	{ 181, 0, 1, "SetSysColors", "wpp", api_SetSysColors16, 0 },
	{ 182, 0, 1, "KillSystemTimer", "ww", api_KillSystemTimer16, 0 },
	{ 183, 0, 1, "GetCaretPos", "p", api_GetCaretPos16, 0 },
	{ 184, 0, 1, "QuerySendMessage", "wwwp", api_QuerySendMessage16, 0 },
	{ 185, 0, 1, "GrayString", "wwPPsssss", api_GrayString16, 0 },
	{ 186, 0, 1, "SwapMouseButton", "w", api_SwapMouseButton16, 0 },
	{ 187, 0, 1, "EndMenu", "", api_EndMenu, 0 },
	{ 188, 0, 1, "SetSysModalWindow", "w", api_SetSysModalWindow16, 0 },
	{ 189, 0, 1, "GetSysModalWindow", "", api_GetSysModalWindow16, 0 },
	{ 190, 0, 1, "GetUpdateRect", "wpw", api_GetUpdateRect16, 0 },
	{ 191, 0, 1, "ChildWindowFromPoint", "wl", api_ChildWindowFromPoint16, 0 },
	{ 192, 0, 1, "InSendMessage", "", api_InSendMessage16, 0 },
	{ 193, 0, 1, "IsClipboardFormatAvailable", "w", api_IsClipboardFormatAvailable16, 0 },
	{ 194, 0, 1, "DlgDirSelectComboBox", "wpw", api_DlgDirSelectComboBox16, 0 },
	{ 195, 0, 1, "DlgDirListComboBox", "wpwww", api_DlgDirListComboBox16, 0 },
	{ 196, 0, 0, "TabbedTextOut", "wsspssps", api_TabbedTextOut16, 0 },
	{ 197, 0, 0, "GetTabbedTextExtent", "wpwwp", api_GetTabbedTextExtent16, 0 },
	{ 198, 0, 1, "CascadeChildWindows", "ww", api_CascadeChildWindows16, 0 },
	{ 199, 0, 1, "TileChildWindows", "ww", api_TileChildWindows16, 0 },
	{ 200, 0, 1, "OpenComm", "tww", api_OpenComm16, 0 },
	{ 201, 0, 1, "SetCommState", "p", api_SetCommState16, 0 },
	{ 202, 0, 1, "GetCommState", "wp", api_GetCommState16, 0 },
	{ 203, 0, 1, "GetCommError", "wp", api_GetCommError16, 0 },
	{ 204, 0, 1, "ReadComm", "wpw", api_ReadComm16, 0 },
	{ 205, 0, 1, "WriteComm", "wpw", api_WriteComm16, 0 },
	{ 206, 0, 1, "TransmitCommChar", "ww", api_TransmitCommChar16, 0 },
	{ 207, 0, 1, "CloseComm", "w", api_CloseComm16, 0 },
	{ 208, 0, 0, "SetCommEventMask", "ww", api_SetCommEventMask16, 0 },
	{ 209, 0, 1, "GetCommEventMask", "ww", api_GetCommEventMask16, 0 },
	{ 210, 0, 1, "SetCommBreak", "w", api_SetCommBreak16, 0 },
	{ 211, 0, 1, "ClearCommBreak", "w", api_ClearCommBreak16, 0 },
	{ 212, 0, 1, "UngetCommChar", "ww", api_UngetCommChar16, 0 },
	{ 213, 0, 1, "BuildCommDCB", "pp", api_BuildCommDCB16, 0 },
	{ 214, 0, 0, "EscapeCommFunction", "ww", api_EscapeCommFunction16, 0 },
	{ 215, 0, 1, "FlushComm", "ww", api_FlushComm16, 0 },
	{ 216, 0, 0, "UserSeeUserDo", "wwww", api_UserSeeUserDo16, 0 },
	{ 217, 0, 1, "LookupMenuHandle", "ws", api_LookupMenuHandle16, 0 },
	{ 218, 0, 1, "DialogBoxIndirect", "wwwP", api_DialogBoxIndirect16, 0 },
	{ 219, 0, 1, "CreateDialogIndirect", "wPwP", api_CreateDialogIndirect16, 0 },
	{ 220, 0, 1, "LoadMenuIndirect", "P", api_LoadMenuIndirect16, 0 },
	{ 221, 0, 1, "ScrollDC", "wssppwp", api_ScrollDC16, 0 },
	{ 222, 0, 1, "GetKeyboardState", "p", api_GetKeyboardState16, 0 },
	{ 223, 0, 1, "SetKeyboardState", "p", api_SetKeyboardState16, 0 },
	{ 224, 0, 1, "GetWindowTask", "w", api_GetWindowTask16, 0 },
	{ 225, 0, 1, "EnumTaskWindows", "wPl", api_EnumTaskWindows16, 0 },
	{ 226, 0, 1, "LockInput", "www", api_LockInput16, 0 },
	{ 227, 0, 1, "GetNextDlgGroupItem", "www", api_GetNextDlgGroupItem16, 0 },
	{ 228, 0, 1, "GetNextDlgTabItem", "www", api_GetNextDlgTabItem16, 0 },
	{ 229, 0, 1, "GetTopWindow", "w", api_GetTopWindow16, 0 },
	{ 230, 0, 1, "GetNextWindow", "ww", api_GetNextWindow16, 0 },
	{ 231, 0, 1, "GetSystemDebugState", "", api_GetSystemDebugState16, 0 },
	{ 232, 0, 1, "SetWindowPos", "wwwwwww", api_SetWindowPos16, 0 },
	{ 233, 0, 1, "SetParent", "ww", api_SetParent16, 0 },
	{ 234, 0, 1, "UnhookWindowsHook", "sP", api_UnhookWindowsHook16, 0 },
	{ 235, 0, 0, "DefHookProc", "swlp", api_DefHookProc16, 0 },
	{ 236, 0, 1, "GetCapture", "", api_GetCapture16, 0 },
	{ 237, 0, 1, "GetUpdateRgn", "www", api_GetUpdateRgn16, 0 },
	{ 238, 0, 1, "ExcludeUpdateRgn", "ww", api_ExcludeUpdateRgn16, 0 },
	{ 239, 0, 1, "DialogBoxParam", "wtwPl", api_DialogBoxParam16, 0 },
	{ 240, 0, 1, "DialogBoxIndirectParam", "wwwPl", api_DialogBoxIndirectParam16, 0 },
	{ 241, 0, 1, "CreateDialogParam", "wtwPl", api_CreateDialogParam16, 0 },
	{ 242, 0, 1, "CreateDialogIndirectParam", "wPwPl", api_CreateDialogIndirectParam16, 0 },
	{ 243, 0, 0, "GetDialogBaseUnits", "", api_GetDialogBaseUnits16, 0 },
	{ 244, 0, 1, "EqualRect", "pp", api_EqualRect16, 0 },
	{ 245, 0, 1, "EnableCommNotification", "swss", api_EnableCommNotification16, 0 },
	{ 246, 0, 1, "ExitWindowsExec", "tt", api_ExitWindowsExec16, 0 },
	{ 247, 0, 1, "GetCursor", "", api_GetCursor16, 0 },
	{ 248, 0, 1, "GetOpenClipboardWindow", "", api_GetOpenClipboardWindow16, 0 },
	{ 249, 0, 1, "GetAsyncKeyState", "w", api_GetAsyncKeyState16, 0 },
	{ 250, 0, 1, "GetMenuState", "www", api_GetMenuState16, 0 },
	{ 251, 0, 0, "SendDriverMessage", "wwll", api_SendDriverMessage16, 0 },
	{ 252, 0, 1, "OpenDriver", "ttl", api_OpenDriver16, 0 },
	{ 253, 0, 0, "CloseDriver", "wll", api_CloseDriver16, 0 },
	{ 254, 0, 1, "GetDriverModuleHandle", "w", api_GetDriverModuleHandle16, 0 },
	{ 255, 0, 0, "DefDriverProc", "lwwll", api_DefDriverProc16, 0 },
	{ 256, 0, 1, "GetDriverInfo", "wp", api_GetDriverInfo16, 0 },
	{ 257, 0, 1, "GetNextDriver", "wl", api_GetNextDriver16, 0 },
	{ 258, 0, 1, "MapWindowPoints", "wwpw", api_MapWindowPoints16, 0 },
	{ 259, 0, 1, "BeginDeferWindowPos", "s", api_BeginDeferWindowPos16, 0 },
	{ 260, 0, 1, "DeferWindowPos", "wwwssssw", api_DeferWindowPos16, 0 },
	{ 261, 0, 1, "EndDeferWindowPos", "w", api_EndDeferWindowPos16, 0 },
	{ 262, 0, 1, "GetWindow", "ww", api_GetWindow16, 0 },
	{ 263, 0, 1, "GetMenuItemCount", "w", api_GetMenuItemCount16, 0 },
	{ 264, 0, 1, "GetMenuItemID", "ww", api_GetMenuItemID16, 0 },
	{ 265, 0, 1, "ShowOwnedPopups", "ww", api_ShowOwnedPopups16, 0 },
	{ 266, 0, 1, "SetMessageQueue", "w", api_SetMessageQueue16, 0 },
	{ 267, 0, 1, "ShowScrollBar", "www", api_ShowScrollBar16, 0 },
	{ 268, 0, 1, "GlobalAddAtom", "t", api_GlobalAddAtom16, 0 },
	{ 269, 0, 1, "GlobalDeleteAtom", "w", api_GlobalDeleteAtom16, 0 },
	{ 270, 0, 1, "GlobalFindAtom", "t", api_GlobalFindAtom16, 0 },
	{ 271, 0, 1, "GlobalGetAtomName", "wps", api_GlobalGetAtomName16, 0 },
	{ 272, 0, 1, "IsZoomed", "w", api_IsZoomed16, 0 },
	{ 273, 0, 1, "ControlPanelInfo", "wwt", api_ControlPanelInfo16, 0 },
	{ 274, 3, 0, "GetNextQueueWindow", 0, 0, 0 },
	{ 275, 0, 1, "RepaintScreen", "", api_RepaintScreen16, 0 },
	{ 276, 3, 0, "LockMyTask", 0, 0, 0 },
	{ 277, 0, 1, "GetDlgCtrlID", "w", api_GetDlgCtrlID16, 0 },
	{ 278, 0, 1, "GetDesktopHwnd", "", api_GetDesktopHwnd16, 0 },
	{ 279, 0, 1, "OldSetDeskPattern", "", api_SetDeskPattern16, 0 },
	{ 280, 0, 1, "SetSystemMenu", "ww", api_SetSystemMenu16, 0 },
	{ 281, 0, 1, "GetSysColorBrush", "w", api_GetSysColorBrush16, 0 },
	{ 282, 0, 1, "SelectPalette", "www", api_SelectPalette16, 0 },
	{ 283, 0, 1, "RealizePalette", "w", api_RealizePalette16, 0 },
	{ 284, 0, 1, "GetFreeSystemResources", "w", api_GetFreeSystemResources16, 0 },
	{ 285, 0, 1, "SetDeskWallPaper", "p", api_SetDeskWallPaper16, 0 },
	{ 286, 0, 1, "GetDesktopWindow", "", api_GetDesktopWindow16, 0 },
	{ 287, 0, 1, "GetLastActivePopup", "w", api_GetLastActivePopup16, 0 },
	{ 288, 0, 0, "GetMessageExtraInfo", "", api_GetMessageExtraInfo, 0 },
	{ 289, 1, 0, "keybd_event", "", api_keybd_event16, 0 },
	{ 290, 0, 1, "RedrawWindow", "wpww", api_RedrawWindow16, 0 },
	{ 291, 0, 0, "SetWindowsHookEx", "sPww", api_SetWindowsHookEx16, 0 },
	{ 292, 0, 1, "UnhookWindowsHookEx", "P", api_UnhookWindowsHookEx16, 0 },
	{ 293, 0, 0, "CallNextHookEx", "Pswl", api_CallNextHookEx16, 0 },
	{ 294, 0, 1, "LockWindowUpdate", "w", api_LockWindowUpdate16, 0 },
	{ 299, 1, 0, "mouse_event", "", api_mouse_event16, 0 },
	{ 300, 3, 0, "UnloadInstalledDrivers", 0, 0, 0 },
	{ 301, 3, 0, "EDITWNDPROC", 0, 0, 0 },
	{ 302, 3, 0, "STATICWNDPROC", 0, 0, 0 },
	{ 303, 3, 0, "BUTTONWNDPROC", 0, 0, 0 },
	{ 304, 3, 0, "SBWNDPROC", 0, 0, 0 },
	{ 305, 3, 0, "DESKTOPWNDPROC", 0, 0, 0 },
	{ 306, 3, 0, "MENUWNDPROC", 0, 0, 0 },
	{ 307, 3, 0, "LBOXCTLWNDPROC", 0, 0, 0 },
	{ 308, 0, 0, "DefDlgProc", "wwwl", api_DefDlgProc16, 0 },
	{ 309, 0, 1, "GetClipCursor", "p", api_GetClipCursor16, 0 },
	{ 314, 0, 1, "SignalProc", "wwwww", api_SignalProc16, 0 },
	{ 319, 0, 1, "ScrollWindowEx", "wssppwpw", api_ScrollWindowEx16, 0 },
	{ 320, 0, 1, "SysErrorBox", "ttwww", api_SysErrorBox16, 0 },
	{ 321, 0, 0, "SetEventHook", "P", api_SetEventHook16, 0 },
	{ 322, 3, 0, "WinOldAppHackOMatic", 0, 0, 0 },
	{ 323, 3, 0, "GetMessage2", 0, 0, 0 },
	{ 324, 0, 1, "FillWindow", "wwww", api_FillWindow16, 0 },
	{ 325, 0, 1, "PaintRect", "wwwwp", api_PaintRect16, 0 },
	{ 326, 0, 1, "GetControlBrush", "www", api_GetControlBrush16, 0 },
	{ 331, 0, 1, "EnableHardwareInput", "w", api_EnableHardwareInput16, 0 },
	{ 332, 0, 1, "UserYield", "", api_UserYield16, 0 },
	{ 333, 0, 1, "IsUserIdle", "", api_IsUserIdle16, 0 },
	{ 334, 0, 0, "GetQueueStatus", "w", api_GetQueueStatus16, 0 },
	{ 335, 0, 1, "GetInputState", "", api_GetInputState16, 0 },
	{ 336, 0, 1, "LoadCursorIconHandler", "www", api_LoadCursorIconHandler16, 0 },
	{ 337, 0, 0, "GetMouseEventProc", "", api_GetMouseEventProc16, 0 },
	{ 338, 3, 0, "ECGETDS", 0, 0, 0 },
	{ 343, 3, 0, "GetFilePortName", 0, 0, 0 },
	{ 344, 3, 0, "COMBOBOXCTLWNDPROC", 0, 0, 0 },
	{ 345, 3, 0, "BEAR345", 0, 0, 0 },
	{ 356, 0, 1, "LoadDIBCursorHandler", "www", api_LoadDIBCursorHandler16, 0 },
	{ 357, 0, 1, "LoadDIBIconHandler", "www", api_LoadDIBIconHandler16, 0 },
	{ 358, 0, 1, "IsMenu", "w", api_IsMenu16, 0 },
	{ 359, 0, 1, "GetDCEx", "wwl", api_GetDCEx16, 0 },
	{ 362, 0, 1, "DCHook", "wwll", api_DCHook16, 0 },
	{ 364, 0, 1, "LookupIconIdFromDirectoryEx", "pwwww", api_LookupIconIdFromDirectoryEx16, 0 },
	{ 368, 0, 1, "CopyIcon", "ww", api_CopyIcon16, 0 },
	{ 369, 0, 1, "CopyCursor", "ww", api_CopyCursor16, 0 },
	{ 370, 0, 1, "GetWindowPlacement", "wp", api_GetWindowPlacement16, 0 },
	{ 371, 0, 1, "SetWindowPlacement", "wp", api_SetWindowPlacement16, 0 },
	{ 372, 3, 0, "GetInternalIconHeader", 0, 0, 0 },
	{ 373, 0, 1, "SubtractRect", "ppp", api_SubtractRect16, 0 },
	{ 374, 0, 1, "DllEntryPoint", "lwwwlw", api_DllEntryPoint, 0 },
	{ 375, 3, 0, "DrawTextEx", 0, 0, 0 },
	{ 376, 3, 0, "SetMessageExtraInfo", 0, 0, 0 },
	{ 378, 3, 0, "SetPropEx", 0, 0, 0 },
	{ 379, 3, 0, "GetPropEx", 0, 0, 0 },
	{ 380, 3, 0, "RemovePropEx", 0, 0, 0 },
	{ 382, 3, 0, "SetWindowContextHelpID", 0, 0, 0 },
	{ 383, 3, 0, "GetWindowContextHelpID", 0, 0, 0 },
	{ 384, 0, 1, "SetMenuContextHelpId", "ww", api_SetMenuContextHelpId16, 0 },
	{ 385, 0, 1, "GetMenuContextHelpId", "w", api_GetMenuContextHelpId16, 0 },
	{ 389, 0, 0, "LoadImage", "wtwwww", api_LoadImage16, 0 },
	{ 390, 0, 1, "CopyImage", "wwwww", api_CopyImage16, 0 },
	{ 391, 0, 1, "SignalProc32", "lllw", api_UserSignalProc, 0 },
	{ 394, 0, 1, "DrawIconEx", "wwwwwwwww", api_DrawIconEx16, 0 },
	{ 395, 0, 1, "GetIconInfo", "wp", api_GetIconInfo16, 0 },
	{ 397, 0, 1, "RegisterClassEx", "p", api_RegisterClassEx16, 0 },
	{ 398, 0, 1, "GetClassInfoEx", "wTp", api_GetClassInfoEx16, 0 },
	{ 399, 0, 1, "ChildWindowFromPointEx", "wlw", api_ChildWindowFromPointEx16, 0 },
	{ 400, 0, 1, "FinalUserInit", "", api_FinalUserInit16, 0 },
	{ 402, 0, 1, "GetPriorityClipboardFormat", "ps", api_GetPriorityClipboardFormat16, 0 },
	{ 403, 0, 1, "UnregisterClass", "tw", api_UnregisterClass16, 0 },
	{ 404, 0, 1, "GetClassInfo", "wTp", api_GetClassInfo16, 0 },
	{ 406, 0, 1, "CreateCursor", "wwwwwpp", api_CreateCursor16, 0 },
	{ 407, 0, 1, "CreateIcon", "wwwwwpp", api_CreateIcon16, 0 },
	{ 408, 0, 1, "CreateCursorIconIndirect", "wppp", api_CreateCursorIconIndirect16, 0 },
	{ 409, 0, 1, "InitThreadInput", "ww", api_InitThreadInput16, 0 },
	{ 410, 0, 1, "InsertMenu", "wwwwP", api_InsertMenu16, 0 },
	{ 411, 0, 1, "AppendMenu", "wwwP", api_AppendMenu16, 0 },
	{ 412, 0, 1, "RemoveMenu", "www", api_RemoveMenu16, 0 },
	{ 413, 0, 1, "DeleteMenu", "www", api_DeleteMenu16, 0 },
	{ 414, 0, 1, "ModifyMenu", "wwwwP", api_ModifyMenu16, 0 },
	{ 415, 0, 1, "CreatePopupMenu", "", api_CreatePopupMenu16, 0 },
	{ 416, 0, 1, "TrackPopupMenu", "wwssswp", api_TrackPopupMenu16, 0 },
	{ 417, 0, 0, "GetMenuCheckMarkDimensions", "", api_GetMenuCheckMarkDimensions, 0 },
	{ 418, 0, 1, "SetMenuItemBitmaps", "wwwww", api_SetMenuItemBitmaps16, 0 },
	{ 420, 2, 1, "_wsprintf", "pt", api_wsprintf16, 0 },
	{ 421, 0, 1, "wvsprintf", "ptp", api_wvsprintf16, 0 },
	{ 422, 0, 1, "DlgDirSelectEx", "wpww", api_DlgDirSelectEx16, 0 },
	{ 423, 0, 1, "DlgDirSelectComboBoxEx", "wpww", api_DlgDirSelectComboBoxEx16, 0 },
	{ 427, 0, 1, "FindWindowEx", "wwtt", api_FindWindowEx16, 0 },
	{ 428, 3, 0, "TileWindows", 0, 0, 0 },
	{ 429, 3, 0, "CascadeWindows", 0, 0, 0 },
	{ 430, 0, 1, "lstrcmp", "tt", api_lstrcmpA, 0 },
	{ 431, 0, 0, "AnsiUpper", "T", api_AnsiUpper16, 0 },
	{ 432, 0, 0, "AnsiLower", "T", api_AnsiLower16, 0 },
	{ 433, 0, 1, "IsCharAlpha", "w", api_IsCharAlphaA, 0 },
	{ 434, 0, 1, "IsCharAlphaNumeric", "w", api_IsCharAlphaNumericA, 0 },
	{ 435, 0, 1, "IsCharUpper", "w", api_IsCharUpperA, 0 },
	{ 436, 0, 1, "IsCharLower", "w", api_IsCharLowerA, 0 },
	{ 437, 0, 1, "AnsiUpperBuff", "tw", api_AnsiUpperBuff16, 0 },
	{ 438, 0, 1, "AnsiLowerBuff", "tw", api_AnsiLowerBuff16, 0 },
	{ 441, 0, 1, "InsertMenuItem", "wwwp", api_InsertMenuItem16, 0 },
	{ 443, 3, 0, "GetMenuItemInfo", 0, 0, 0 },
	{ 445, 0, 0, "DefFrameProc", "wwwwl", api_DefFrameProc16, 0 },
	{ 446, 3, 0, "SetMenuItemInfo", 0, 0, 0 },
	{ 447, 0, 0, "DefMDIChildProc", "wwwl", api_DefMDIChildProc16, 0 },
	{ 448, 0, 1, "DrawAnimatedRects", "wwpp", api_DrawAnimatedRects16, 0 },
	{ 449, 0, 1, "DrawState", "wwPlwssssw", api_DrawState16, 0 },
	{ 450, 0, 1, "CreateIconFromResourceEx", "plwlwww", api_CreateIconFromResourceEx16, 0 },
	{ 451, 0, 1, "TranslateMDISysAccel", "wp", api_TranslateMDISysAccel16, 0 },
	{ 452, 0, 1, "CreateWindowEx", "lttlsssswwwP", api_CreateWindowEx16, 0 },
	{ 454, 0, 1, "AdjustWindowRectEx", "plwl", api_AdjustWindowRectEx16, 0 },
	{ 455, 0, 1, "GetIconID", "wl", api_GetIconID16, 0 },
	{ 456, 0, 1, "LoadIconHandler", "ww", api_LoadIconHandler16, 0 },
	{ 457, 0, 1, "DestroyIcon", "w", api_DestroyIcon16, 0 },
	{ 458, 0, 1, "DestroyCursor", "w", api_DestroyCursor16, 0 },
	{ 459, 0, 0, "DumpIcon", "Pppp", api_DumpIcon16, 0 },
	{ 460, 0, 1, "GetInternalWindowPos", "wpp", api_GetInternalWindowPos16, 0 },
	{ 461, 0, 1, "SetInternalWindowPos", "wwpp", api_SetInternalWindowPos16, 0 },
	{ 462, 0, 1, "CalcChildScroll", "ww", api_CalcChildScroll16, 0 },
	{ 463, 0, 1, "ScrollChildren", "wwwl", api_ScrollChildren16, 0 },
	{ 464, 0, 0, "DragObject", "wwwwww", api_DragObject16, 0 },
	{ 465, 0, 1, "DragDetect", "wl", api_DragDetect16, 0 },
	{ 466, 0, 1, "DrawFocusRect", "wp", api_DrawFocusRect16, 0 },
	{ 470, 3, 0, "StringFunc", 0, 0, 0 },
	{ 471, 0, 1, "lstrcmpi", "tt", api_lstrcmpiA, 0 },
	{ 472, 0, 0, "AnsiNext", "P", api_AnsiNext16, 0 },
	{ 473, 0, 0, "AnsiPrev", "PP", api_AnsiPrev16, 0 },
	{ 475, 0, 1, "SetScrollInfo", "wspw", api_SetScrollInfo16, 0 },
	{ 476, 0, 1, "GetScrollInfo", "wsp", api_GetScrollInfo16, 0 },
	{ 477, 0, 1, "GetKeyboardLayoutName", "p", api_GetKeyboardLayoutName16, 0 },
	{ 478, 3, 0, "LoadKeyboardLayout", 0, 0, 0 },
	{ 479, 3, 0, "MenuItemFromPoint", 0, 0, 0 },
	{ 480, 3, 0, "GetUserLocalObjType", 0, 0, 0 },
	{ 482, 0, 1, "EnableScrollBar", "www", api_EnableScrollBar16, 0 },
	{ 483, 0, 1, "SystemParametersInfo", "wwpw", api_SystemParametersInfo16, 0 },
	{ 485, 0, 1, "MakeMenuRToL", "ww", api_MakeMenuRToL16, 0 },
	{ 486, 0, 1, "RawGetTabbedTextExtent", "", api_stub_USER_486, 0 },
	{ 487, 0, 1, "RawDrawText", "wtspw", api_RawDrawText16, 0 },
	{ 488, 0, 1, "RawTabbedTextOut", "", api_stub_USER_488, 0 },
	{ 489, 0, 1, "GetKeyboardLanguage", "", api_GetKeyboardLanguage16, 0 },
	{ 490, 0, 1, "SetKeyboardLanguage", "ww", api_SetKeyboardLanguage16, 0 },
	{ 491, 0, 1, "BiDiGrayString", "", api_stub_USER_491, 0 },
	{ 492, 0, 1, "MakeScrollBarsRToL", "www", api_MakeScrollBarsRToL, 0 },
	{ 493, 0, 1, "IsCharBiDi", "", api_stub_USER_493, 0 },
	{ 494, 0, 1, "IsCharEnglish", "", api_stub_USER_494, 0 },
	{ 495, 0, 1, "LenFruser1", "", api_stub_USER_495, 0 },
	{ 496, 0, 1, "DrawTextRToL", "", api_stub_USER_496, 0 },
	{ 498, 3, 0, "BEAR498", 0, 0, 0 },
	{ 499, 0, 1, "WNetErrorText", "wpw", api_WNetErrorText16, 0 },
	{ 500, 3, 0, "FARCALLNETDRIVER", 0, 0, 0 },
	{ 501, 0, 1, "WNetOpenJob", "ppwp", api_WNetOpenJob16, 0 },
	{ 502, 0, 1, "WNetCloseJob", "wpp", api_WNetCloseJob16, 0 },
	{ 503, 0, 1, "WNetAbortJob", "pw", api_WNetAbortJob16, 0 },
	{ 504, 0, 1, "WNetHoldJob", "pw", api_WNetHoldJob16, 0 },
	{ 505, 0, 1, "WNetReleaseJob", "pw", api_WNetReleaseJob16, 0 },
	{ 506, 0, 1, "WNetCancelJob", "pw", api_WNetCancelJob16, 0 },
	{ 507, 0, 1, "WNetSetJobCopies", "pww", api_WNetSetJobCopies16, 0 },
	{ 508, 0, 1, "WNetWatchQueue", "wppw", api_WNetWatchQueue16, 0 },
	{ 509, 0, 1, "WNetUnwatchQueue", "t", api_WNetUnwatchQueue16, 0 },
	{ 510, 0, 1, "WNetLockQueueData", "ppp", api_WNetLockQueueData16, 0 },
	{ 511, 0, 1, "WNetUnlockQueueData", "p", api_WNetUnlockQueueData16, 0 },
	{ 512, 0, 1, "WNetGetConnection", "ppp", api_WNetGetConnection16, 0 },
	{ 513, 0, 1, "WNetGetCaps", "w", api_WNetGetCaps16, 0 },
	{ 514, 0, 1, "WNetDeviceMode", "w", api_WNetDeviceMode16, 0 },
	{ 515, 0, 1, "WNetBrowseDialog", "wwp", api_WNetBrowseDialog16, 0 },
	{ 516, 0, 1, "WNetGetUser", "pp", api_WNetGetUser16, 0 },
	{ 517, 0, 1, "WNetAddConnection", "ttt", api_WNetAddConnection16, 0 },
	{ 518, 0, 1, "WNetCancelConnection", "tw", api_WNetCancelConnection16, 0 },
	{ 519, 0, 1, "WNetGetError", "p", api_WNetGetError16, 0 },
	{ 520, 0, 1, "WNetGetErrorText", "wpp", api_WNetGetErrorText16, 0 },
	{ 521, 3, 0, "WNetEnable", 0, 0, 0 },
	{ 522, 3, 0, "WNetDisable", 0, 0, 0 },
	{ 523, 0, 1, "WNetRestoreConnection", "wp", api_WNetRestoreConnection16, 0 },
	{ 524, 0, 1, "WNetWriteJob", "wpp", api_WNetWriteJob16, 0 },
	{ 525, 0, 1, "WNetConnectDialog", "ww", api_WNetConnectDialog16, 0 },
	{ 526, 0, 1, "WNetDisconnectDialog", "ww", api_WNetDisconnectDialog16, 0 },
	{ 527, 0, 1, "WNetConnectionDialog", "ww", api_WNetConnectionDialog16, 0 },
	{ 528, 0, 1, "WNetViewQueueDialog", "wp", api_WNetViewQueueDialog16, 0 },
	{ 529, 0, 1, "WNetPropertyDialog", "wwwtw", api_WNetPropertyDialog16, 0 },
	{ 530, 0, 1, "WNetGetDirectoryType", "pp", api_WNetGetDirectoryType16, 0 },
	{ 531, 0, 1, "WNetDirectoryNotify", "wpw", api_WNetDirectoryNotify16, 0 },
	{ 532, 0, 1, "WNetGetPropertyText", "wwttww", api_WNetGetPropertyText16, 0 },
	{ 533, 3, 0, "WNetInitialize", 0, 0, 0 },
	{ 534, 3, 0, "WNetLogon", 0, 0, 0 },
	{ 535, 3, 0, "WOWWORDBREAKPROC", 0, 0, 0 },
	{ 537, 3, 0, "MOUSEEVENT", 0, 0, 0 },
	{ 538, 3, 0, "KEYBDEVENT", 0, 0, 0 },
	{ 595, 3, 0, "OLDEXITWINDOWS", 0, 0, 0 },
	{ 600, 0, 1, "GetShellWindow", "", api_GetShellWindow16, 0 },
	{ 601, 3, 0, "DoHotkeyStuff", 0, 0, 0 },
	{ 602, 3, 0, "SetCheckCursorTimer", 0, 0, 0 },
	{ 604, 3, 0, "BroadcastSystemMessage", 0, 0, 0 },
	{ 605, 3, 0, "HackTaskMonitor", 0, 0, 0 },
	{ 606, 0, 1, "FormatMessage", "lPwwpwp", api_FormatMessage16, 0 },
	{ 608, 0, 1, "GetForegroundWindow", "", api_GetForegroundWindow16, 0 },
	{ 609, 0, 1, "SetForegroundWindow", "w", api_SetForegroundWindow16, 0 },
	{ 610, 0, 1, "DestroyIcon32", "ww", api_DestroyIcon32, 0 },
	{ 620, 0, 0, "ChangeDisplaySettings", "pl", api_ChangeDisplaySettings16, 0 },
	{ 621, 0, 1, "EnumDisplaySettings", "tlp", api_EnumDisplaySettings16, 0 },
	{ 640, 0, 0, "MsgWaitForMultipleObjects", "lplll", api_MsgWaitForMultipleObjects16, 0 },
	{ 650, 3, 0, "ActivateKeyboardLayout", 0, 0, 0 },
	{ 651, 3, 0, "GetKeyboardLayout", 0, 0, 0 },
	{ 652, 3, 0, "GetKeyboardLayoutList", 0, 0, 0 },
	{ 654, 3, 0, "UnloadKeyboardLayout", 0, 0, 0 },
	{ 655, 3, 0, "PostPostedMessages", 0, 0, 0 },
	{ 656, 0, 1, "DrawFrameControl", "wpww", api_DrawFrameControl16, 0 },
	{ 657, 0, 1, "DrawCaptionTemp", "wwpwwpw", api_DrawCaptionTemp16, 0 },
	{ 658, 3, 0, "DispatchInput", 0, 0, 0 },
	{ 659, 0, 1, "DrawEdge", "wpww", api_DrawEdge16, 0 },
	{ 660, 0, 1, "DrawCaption", "wwpw", api_DrawCaption16, 0 },
	{ 661, 3, 0, "SetSysColorsTemp", 0, 0, 0 },
	{ 662, 3, 0, "DrawMenubarTemp", 0, 0, 0 },
	{ 663, 3, 0, "GetMenuDefaultItem", 0, 0, 0 },
	{ 664, 3, 0, "SetMenuDefaultItem", 0, 0, 0 },
	{ 665, 0, 1, "GetMenuItemRect", "wwwp", api_GetMenuItemRect16, 0 },
	{ 666, 0, 1, "CheckMenuRadioItem", "wwwww", api_CheckMenuRadioItem16, 0 },
	{ 667, 3, 0, "TrackPopupMenuEx", 0, 0, 0 },
	{ 668, 0, 1, "SetWindowRgn", "www", api_SetWindowRgn16, 0 },
	{ 669, 3, 0, "GetWindowRgn", 0, 0, 0 },
	{ 800, 3, 0, "CHOOSEFONT_CALLBACK16", 0, 0, 0 },
	{ 801, 3, 0, "FINDREPLACE_CALLBACK16", 0, 0, 0 },
	{ 802, 3, 0, "OPENFILENAME_CALLBACK16", 0, 0, 0 },
	{ 803, 3, 0, "PRINTDLG_CALLBACK16", 0, 0, 0 },
	{ 804, 3, 0, "CHOOSECOLOR_CALLBACK16", 0, 0, 0 },
	{ 819, 0, 1, "PeekMessage32", "pwwwww", api_PeekMessage32_16, 0 },
	{ 820, 0, 0, "GetMessage32", "pwwww", api_GetMessage32_16, 0 },
	{ 821, 0, 1, "TranslateMessage32", "pw", api_TranslateMessage32_16, 0 },
	{ 822, 0, 0, "DispatchMessage32", "pw", api_DispatchMessage32_16, 0 },
	{ 823, 0, 1, "CallMsgFilter32", "pww", api_CallMsgFilter32_16, 0 },
	{ 825, 3, 0, "PostMessage32", 0, 0, 0 },
	{ 826, 3, 0, "PostThreadMessage32", 0, 0, 0 },
	{ 827, 0, 1, "MessageBoxIndirect", "p", api_MessageBoxIndirect16, 0 },
	{ 851, 3, 0, "MsgThkConnectionDataLS", 0, 0, 0 },
	{ 853, 3, 0, "FT_USRFTHKTHKCONNECTIONDATA", 0, 0, 0 },
	{ 854, 3, 0, "FT__USRF2THKTHKCONNECTIONDATA", 0, 0, 0 },
	{ 855, 3, 0, "Usr32ThkConnectionDataSL", 0, 0, 0 },
	{ 890, 3, 0, "InstallIMT", 0, 0, 0 },
	{ 891, 3, 0, "UninstallIMT", 0, 0, 0 },
	{ 901, 0, 1, "FindResourceEx", "", api_FindResourceEx16, 0 },
	{ 902, 0, 1, "LoadSystemLanguageString", "wwpww", api_LoadSystemLanguageString16, 0 },
	{ 903, 0, 1, "GetClipboardCodePage", "www", api_GetClipboardCodePage16, 0 },
	{ 904, 0, 1, "ResetDialogTemplate", "", api_ResetDialogTemplate16, 0 },
	{ 905, 0, 1, "ChangeDialogTemplate", "", api_ChangeDialogTemplate16, 0 },
	{ 906, 0, 1, "GetNumLanguages", "", api_GetNumLanguages16, 0 },
	{ 907, 0, 1, "GetLanguageName", "wwpw", api_GetLanguageName16, 0 },
	{ 908, 0, 1, "GetLanguageResId", "", api_GetLanguageResId16, 0 },
	{ 909, 0, 1, "SetWindowTextEx", "wtw", api_SetWindowTextEx16, 0 },
	{ 910, 0, 1, "BiDiMessageBoxEx", "wttww", api_MessageBoxEx16, 0 },
	{ 911, 0, 1, "SetDlgItemTextEx", "wwtw", api_SetDlgItemTextEx16, 0 },
	{ 912, 0, 0, "ChangeKeyboardLanguage", "ww", api_ChangeKeyboardLanguage16, 0 },
	{ 913, 0, 1, "GetCodePageSystemFont", "ww", api_GetCodePageSystemFont16, 0 },
	{ 914, 0, 1, "QueryCodePage", "wwwl", api_QueryCodePage16, 0 },
	{ 915, 0, 0, "GetAppCodePage", "w", api_GetAppCodePage16, 0 },
	{ 916, 0, 1, "CreateDialogIndirectParamML", "wpwplwwtw", api_CreateDialogIndirectParamML16, 0 },
	{ 917, 0, 1, "CreateDialogParamML", "", api_CreateDialogParamML16, 0 },
	{ 918, 0, 1, "DialogBoxIndirectParamML", "wwwplwwtw", api_DialogBoxIndirectParamML16, 0 },
	{ 919, 0, 1, "LoadLanguageString", "wwwpw", api_LoadLanguageString16, 0 },
	{ 920, 0, 0, "SetAppCodePage", "wwww", api_SetAppCodePage16, 0 },
	{ 922, 0, 0, "GetBaseCodePage", "", api_GetBaseCodePage16, 0 },
	{ 923, 0, 1, "FindLanguageResource", "wttw", api_FindLanguageResource16, 0 },
	{ 924, 0, 0, "ChangeKeyboardCodePage", "ww", api_ChangeKeyboardCodePage16, 0 },
	{ 925, 0, 1, "LangToBestCodePage", "", api_LangToBestCodePage16, 0 },
	{ 926, 0, 1, "GetNumKeyboards", "", api_GetNumKeyboards16, 0 },
	{ 927, 0, 1, "IsCharHebrew", "w", api_IsCharHebrew16, 0 },
	{ 928, 0, 1, "IsCharFrench", "w", api_IsCharFrench16, 0 },
	{ 929, 0, 1, "IsCharArabic", "w", api_IsCharArabic16, 0 },
	{ 930, 0, 1, "MessageBoxEx", "wttww", api_MessageBoxEx16, 0 },
	{ 1000, 0, 1, "SetProcessDefaultLayout", "l", api_SetProcessDefaultLayout16, 0 },
	{ 1001, 0, 1, "GetProcessDefaultLayout", "p", api_GetProcessDefaultLayout16, 0 },
	{ 1010, 0, 0, "__wine_call_wndproc", "wwwll", api___wine_call_wndproc, 0 },
};

static const struct w16_api api_gdi[] = {
	{ 1, 0, 0, "SetBkColor", "wl", api_SetBkColor16, 0 },
	{ 2, 0, 1, "SetBkMode", "ww", api_SetBkMode16, 0 },
	{ 3, 0, 1, "SetMapMode", "ww", api_SetMapMode16, 0 },
	{ 4, 0, 1, "SetROP2", "ww", api_SetROP216, 0 },
	{ 5, 0, 1, "SetRelAbs", "ww", api_SetRelAbs16, 0 },
	{ 6, 0, 1, "SetPolyFillMode", "ww", api_SetPolyFillMode16, 0 },
	{ 7, 0, 1, "SetStretchBltMode", "ww", api_SetStretchBltMode16, 0 },
	{ 8, 0, 1, "SetTextCharacterExtra", "ws", api_SetTextCharacterExtra16, 0 },
	{ 9, 0, 0, "SetTextColor", "wl", api_SetTextColor16, 0 },
	{ 10, 0, 1, "SetTextJustification", "wss", api_SetTextJustification16, 0 },
	{ 11, 0, 0, "SetWindowOrg", "wss", api_SetWindowOrg16, 0 },
	{ 12, 0, 0, "SetWindowExt", "wss", api_SetWindowExt16, 0 },
	{ 13, 0, 0, "SetViewportOrg", "wss", api_SetViewportOrg16, 0 },
	{ 14, 0, 0, "SetViewportExt", "wss", api_SetViewportExt16, 0 },
	{ 15, 0, 0, "OffsetWindowOrg", "wss", api_OffsetWindowOrg16, 0 },
	{ 16, 0, 0, "ScaleWindowExt", "wssss", api_ScaleWindowExt16, 0 },
	{ 17, 0, 0, "OffsetViewportOrg", "wss", api_OffsetViewportOrg16, 0 },
	{ 18, 0, 0, "ScaleViewportExt", "wssss", api_ScaleViewportExt16, 0 },
	{ 19, 0, 1, "LineTo", "wss", api_LineTo16, 0 },
	{ 20, 0, 0, "MoveTo", "wss", api_MoveTo16, 0 },
	{ 21, 0, 1, "ExcludeClipRect", "wssss", api_ExcludeClipRect16, 0 },
	{ 22, 0, 1, "IntersectClipRect", "wssss", api_IntersectClipRect16, 0 },
	{ 23, 0, 1, "Arc", "wssssssss", api_Arc16, 0 },
	{ 24, 0, 1, "Ellipse", "wssss", api_Ellipse16, 0 },
	{ 25, 0, 1, "FloodFill", "wssl", api_FloodFill16, 0 },
	{ 26, 0, 1, "Pie", "wssssssss", api_Pie16, 0 },
	{ 27, 0, 1, "Rectangle", "wssss", api_Rectangle16, 0 },
	{ 28, 0, 1, "RoundRect", "wssssss", api_RoundRect16, 0 },
	{ 29, 0, 1, "PatBlt", "wssssl", api_PatBlt16, 0 },
	{ 30, 0, 1, "SaveDC", "w", api_SaveDC16, 0 },
	{ 31, 0, 0, "SetPixel", "wssl", api_SetPixel16, 0 },
	{ 32, 0, 1, "OffsetClipRgn", "wss", api_OffsetClipRgn16, 0 },
	{ 33, 0, 1, "TextOut", "wsstw", api_TextOut16, 0 },
	{ 34, 0, 1, "BitBlt", "wsssswssl", api_BitBlt16, 0 },
	{ 35, 0, 1, "StretchBlt", "wsssswssssl", api_StretchBlt16, 0 },
	{ 36, 0, 1, "Polygon", "wpw", api_Polygon16, 0 },
	{ 37, 0, 1, "Polyline", "wpw", api_Polyline16, 0 },
	{ 38, 0, 0, "Escape", "wwwPp", api_Escape16, 0 },
	{ 39, 0, 1, "RestoreDC", "ws", api_RestoreDC16, 0 },
	{ 40, 0, 1, "FillRgn", "www", api_FillRgn16, 0 },
	{ 41, 0, 1, "FrameRgn", "wwwww", api_FrameRgn16, 0 },
	{ 42, 0, 1, "InvertRgn", "ww", api_InvertRgn16, 0 },
	{ 43, 0, 1, "PaintRgn", "ww", api_PaintRgn16, 0 },
	{ 44, 0, 1, "SelectClipRgn", "ww", api_SelectClipRgn16, 0 },
	{ 45, 0, 1, "SelectObject", "ww", api_SelectObject16, 0 },
	{ 46, 3, 0, "BITMAPBITS", 0, 0, 0 },
	{ 47, 0, 1, "CombineRgn", "wwws", api_CombineRgn16, 0 },
	{ 48, 0, 1, "CreateBitmap", "wwwwp", api_CreateBitmap16, 0 },
	{ 49, 0, 1, "CreateBitmapIndirect", "p", api_CreateBitmapIndirect16, 0 },
	{ 50, 0, 1, "CreateBrushIndirect", "p", api_CreateBrushIndirect16, 0 },
	{ 51, 0, 1, "CreateCompatibleBitmap", "www", api_CreateCompatibleBitmap16, 0 },
	{ 52, 0, 1, "CreateCompatibleDC", "w", api_CreateCompatibleDC16, 0 },
	{ 53, 0, 1, "CreateDC", "tttP", api_CreateDC16, 0 },
	{ 54, 0, 1, "CreateEllipticRgn", "ssss", api_CreateEllipticRgn16, 0 },
	{ 55, 0, 1, "CreateEllipticRgnIndirect", "p", api_CreateEllipticRgnIndirect16, 0 },
	{ 56, 0, 1, "CreateFont", "ssssswwwwwwwwt", api_CreateFont16, 0 },
	{ 57, 0, 1, "CreateFontIndirect", "p", api_CreateFontIndirect16, 0 },
	{ 58, 0, 1, "CreateHatchBrush", "wl", api_CreateHatchBrush16, 0 },
	{ 60, 0, 1, "CreatePatternBrush", "w", api_CreatePatternBrush16, 0 },
	{ 61, 0, 1, "CreatePen", "ssl", api_CreatePen16, 0 },
	{ 62, 0, 1, "CreatePenIndirect", "p", api_CreatePenIndirect16, 0 },
	{ 63, 0, 1, "CreatePolygonRgn", "pww", api_CreatePolygonRgn16, 0 },
	{ 64, 0, 1, "CreateRectRgn", "ssss", api_CreateRectRgn16, 0 },
	{ 65, 0, 1, "CreateRectRgnIndirect", "p", api_CreateRectRgnIndirect16, 0 },
	{ 66, 0, 1, "CreateSolidBrush", "l", api_CreateSolidBrush16, 0 },
	{ 67, 0, 1, "DPtoLP", "wps", api_DPtoLP16, 0 },
	{ 68, 0, 1, "DeleteDC", "w", api_DeleteDC16, 0 },
	{ 69, 0, 1, "DeleteObject", "w", api_DeleteObject16, 0 },
	{ 70, 0, 1, "EnumFonts", "wtPl", api_EnumFonts16, 0 },
	{ 71, 0, 1, "EnumObjects", "wwPl", api_EnumObjects16, 0 },
	{ 72, 0, 1, "EqualRgn", "ww", api_EqualRgn16, 0 },
	{ 73, 0, 1, "ExcludeVisRect", "wssss", api_ExcludeVisRect16, 0 },
	{ 74, 0, 0, "GetBitmapBits", "wlp", api_GetBitmapBits16, 0 },
	{ 75, 0, 0, "GetBkColor", "w", api_GetBkColor16, 0 },
	{ 76, 0, 1, "GetBkMode", "w", api_GetBkMode16, 0 },
	{ 77, 0, 1, "GetClipBox", "wp", api_GetClipBox16, 0 },
	{ 78, 0, 0, "GetCurrentPosition", "w", api_GetCurrentPosition16, 0 },
	{ 79, 0, 0, "GetDCOrg", "w", api_GetDCOrg16, 0 },
	{ 80, 0, 1, "GetDeviceCaps", "ws", api_GetDeviceCaps16, 0 },
	{ 81, 0, 1, "GetMapMode", "w", api_GetMapMode16, 0 },
	{ 82, 0, 1, "GetObject", "wsp", api_GetObject16, 0 },
	{ 83, 0, 0, "GetPixel", "wss", api_GetPixel16, 0 },
	{ 84, 0, 1, "GetPolyFillMode", "w", api_GetPolyFillMode16, 0 },
	{ 85, 0, 1, "GetROP2", "w", api_GetROP216, 0 },
	{ 86, 0, 1, "GetRelAbs", "w", api_GetRelAbs16, 0 },
	{ 87, 0, 1, "GetStockObject", "w", api_GetStockObject16, 0 },
	{ 88, 0, 1, "GetStretchBltMode", "w", api_GetStretchBltMode16, 0 },
	{ 89, 0, 1, "GetTextCharacterExtra", "w", api_GetTextCharacterExtra16, 0 },
	{ 90, 0, 0, "GetTextColor", "w", api_GetTextColor16, 0 },
	{ 91, 0, 0, "GetTextExtent", "wps", api_GetTextExtent16, 0 },
	{ 92, 0, 1, "GetTextFace", "wsp", api_GetTextFace16, 0 },
	{ 93, 0, 1, "GetTextMetrics", "wp", api_GetTextMetrics16, 0 },
	{ 94, 0, 0, "GetViewportExt", "w", api_GetViewportExt16, 0 },
	{ 95, 0, 0, "GetViewportOrg", "w", api_GetViewportOrg16, 0 },
	{ 96, 0, 0, "GetWindowExt", "w", api_GetWindowExt16, 0 },
	{ 97, 0, 0, "GetWindowOrg", "w", api_GetWindowOrg16, 0 },
	{ 98, 0, 1, "IntersectVisRect", "wssss", api_IntersectVisRect16, 0 },
	{ 99, 0, 1, "LPtoDP", "wps", api_LPtoDP16, 0 },
	{ 100, 0, 1, "LineDDA", "ssssPl", api_LineDDA16, 0 },
	{ 101, 0, 1, "OffsetRgn", "wss", api_OffsetRgn16, 0 },
	{ 102, 0, 1, "OffsetVisRgn", "wss", api_OffsetVisRgn16, 0 },
	{ 103, 0, 1, "PtVisible", "wss", api_PtVisible16, 0 },
	{ 104, 0, 1, "RectVisibleOld", "wp", api_RectVisible16, 0 },
	{ 105, 0, 1, "SelectVisRgn", "ww", api_SelectVisRgn16, 0 },
	{ 106, 0, 0, "SetBitmapBits", "wlp", api_SetBitmapBits16, 0 },
	{ 117, 0, 0, "SetDCOrg", "wss", api_SetDCOrg16, 0 },
	{ 118, 0, 1, "GetSystemIconFont", "", api_GetSystemIconFont16, 0 },
	{ 119, 0, 1, "AddFontResource", "t", api_AddFontResource16, 0 },
	{ 120, 3, 0, "GetContinuingTextExtent", 0, 0, 0 },
	{ 121, 0, 1, "Death", "w", api_Death16, 0 },
	{ 122, 0, 1, "Resurrection", "wwwwwww", api_Resurrection16, 0 },
	{ 123, 0, 1, "PlayMetaFile", "ww", api_PlayMetaFile16, 0 },
	{ 124, 0, 1, "GetMetaFile", "t", api_GetMetaFile16, 0 },
	{ 125, 0, 1, "CreateMetaFile", "t", api_CreateMetaFile16, 0 },
	{ 126, 0, 1, "CloseMetaFile", "w", api_CloseMetaFile16, 0 },
	{ 127, 0, 1, "DeleteMetaFile", "w", api_DeleteMetaFile16, 0 },
	{ 128, 0, 1, "MulDiv", "sss", api_MulDiv16, 0 },
	{ 129, 0, 1, "SaveVisRgn", "w", api_SaveVisRgn16, 0 },
	{ 130, 0, 1, "RestoreVisRgn", "w", api_RestoreVisRgn16, 0 },
	{ 131, 0, 1, "InquireVisRgn", "w", api_InquireVisRgn16, 0 },
	{ 132, 0, 1, "SetEnvironment", "ttw", api_SetEnvironment16, 0 },
	{ 133, 0, 1, "GetEnvironment", "ttw", api_GetEnvironment16, 0 },
	{ 134, 0, 1, "GetRgnBox", "wp", api_GetRgnBox16, 0 },
	{ 135, 0, 1, "ScanLR", "wwwlw", api_ScanLR16, 0 },
	{ 136, 0, 1, "RemoveFontResource", "t", api_RemoveFontResource16, 0 },
	{ 137, 3, 0, "GSV", 0, 0, 0 },
	{ 138, 3, 0, "DPXlate", 0, 0, 0 },
	{ 139, 3, 0, "SetWinViewExt", 0, 0, 0 },
	{ 140, 3, 0, "ScaleExt", 0, 0, 0 },
	{ 141, 3, 0, "WordSet", 0, 0, 0 },
	{ 142, 3, 0, "RectStuff", 0, 0, 0 },
	{ 143, 3, 0, "OffsetOrg", 0, 0, 0 },
	{ 144, 3, 0, "LockDC", 0, 0, 0 },
	{ 145, 3, 0, "UnlockDC", 0, 0, 0 },
	{ 146, 3, 0, "LockUnlock", 0, 0, 0 },
	{ 147, 3, 0, "GDI_FarFrame", 0, 0, 0 },
	{ 148, 0, 0, "SetBrushOrg", "wss", api_SetBrushOrg16, 0 },
	{ 149, 0, 0, "GetBrushOrg", "w", api_GetBrushOrg16, 0 },
	{ 150, 0, 1, "UnrealizeObject", "w", api_UnrealizeObject16, 0 },
	{ 151, 0, 1, "CopyMetaFile", "wt", api_CopyMetaFile16, 0 },
	{ 152, 3, 0, "GDIInitApp", 0, 0, 0 },
	{ 153, 0, 1, "CreateIC", "tttp", api_CreateIC16, 0 },
	{ 154, 0, 0, "GetNearestColor", "wl", api_GetNearestColor16, 0 },
	{ 155, 0, 1, "QueryAbort", "ww", api_QueryAbort16, 0 },
	{ 156, 0, 1, "CreateDiscardableBitmap", "www", api_CreateDiscardableBitmap16, 0 },
	{ 157, 3, 0, "CompatibleBitmap", 0, 0, 0 },
	{ 158, 3, 0, "EnumCallback", 0, 0, 0 },
	{ 159, 0, 1, "GetMetaFileBits", "w", api_GetMetaFileBits16, 0 },
	{ 160, 0, 1, "SetMetaFileBits", "w", api_SetMetaFileBits16, 0 },
	{ 161, 0, 1, "PtInRegion", "wss", api_PtInRegion16, 0 },
	{ 162, 0, 0, "GetBitmapDimension", "w", api_GetBitmapDimension16, 0 },
	{ 163, 0, 0, "SetBitmapDimension", "wss", api_SetBitmapDimension16, 0 },
	{ 164, 3, 0, "PixToLine", 0, 0, 0 },
	{ 169, 0, 1, "IsDCDirty", "wP", api_IsDCDirty16, 0 },
	{ 170, 3, 0, "SetDCStatus", 0, 0, 0 },
	{ 171, 3, 0, "LVBUNION", 0, 0, 0 },
	{ 172, 0, 1, "SetRectRgn", "wssss", api_SetRectRgn16, 0 },
	{ 173, 0, 1, "GetClipRgn", "w", api_GetClipRgn16, 0 },
	{ 174, 3, 0, "BLOAT", 0, 0, 0 },
	{ 175, 0, 1, "EnumMetaFile", "wwPl", api_EnumMetaFile16, 0 },
	{ 176, 0, 1, "PlayMetaFileRecord", "wppw", api_PlayMetaFileRecord16, 0 },
	{ 177, 3, 0, "RCOS", 0, 0, 0 },
	{ 178, 3, 0, "RSIN", 0, 0, 0 },
	{ 179, 0, 1, "GetDCState", "w", api_GetDCState16, 0 },
	{ 180, 0, 1, "SetDCState", "ww", api_SetDCState16, 0 },
	{ 181, 0, 1, "RectInRegionOld", "wp", api_RectInRegion16, 0 },
	{ 182, 3, 0, "REQUESTSEM", 0, 0, 0 },
	{ 183, 3, 0, "CLEARSEM", 0, 0, 0 },
	{ 184, 3, 0, "STUFFVISIBLE", 0, 0, 0 },
	{ 185, 3, 0, "STUFFINREGION", 0, 0, 0 },
	{ 186, 3, 0, "DELETEABOVELINEFONTS", 0, 0, 0 },
	{ 188, 3, 0, "GetTextExtentEx", 0, 0, 0 },
	{ 190, 0, 1, "SetDCHook", "wPl", api_SetDCHook16, 0 },
	{ 191, 0, 0, "GetDCHook", "wp", api_GetDCHook16, 0 },
	{ 192, 0, 1, "SetHookFlags", "ww", api_SetHookFlags16, 0 },
	{ 193, 0, 1, "SetBoundsRect", "wpw", api_SetBoundsRect16, 0 },
	{ 194, 0, 1, "GetBoundsRect", "wpw", api_GetBoundsRect16, 0 },
	{ 195, 0, 1, "SelectBitmap", "ww", api_SelectBitmap16, 0 },
	{ 196, 0, 1, "SetMetaFileBitsBetter", "w", api_SetMetaFileBitsBetter16, 0 },
	{ 201, 3, 0, "DMBITBLT", 0, 0, 0 },
	{ 202, 3, 0, "DMCOLORINFO", 0, 0, 0 },
	{ 206, 3, 0, "dmEnumDFonts", 0, 0, 0 },
	{ 207, 3, 0, "DMENUMOBJ", 0, 0, 0 },
	{ 208, 3, 0, "DMOUTPUT", 0, 0, 0 },
	{ 209, 3, 0, "DMPIXEL", 0, 0, 0 },
	{ 210, 3, 0, "dmRealizeObject", 0, 0, 0 },
	{ 211, 3, 0, "DMSTRBLT", 0, 0, 0 },
	{ 212, 3, 0, "DMSCANLR", 0, 0, 0 },
	{ 213, 3, 0, "BRUTE", 0, 0, 0 },
	{ 214, 3, 0, "DMEXTTEXTOUT", 0, 0, 0 },
	{ 215, 3, 0, "DMGETCHARWIDTH", 0, 0, 0 },
	{ 216, 3, 0, "DMSTRETCHBLT", 0, 0, 0 },
	{ 217, 3, 0, "DMDIBBITS", 0, 0, 0 },
	{ 218, 3, 0, "DMSTRETCHDIBITS", 0, 0, 0 },
	{ 219, 3, 0, "DMSETDIBTODEV", 0, 0, 0 },
	{ 220, 3, 0, "DMTRANSPOSE", 0, 0, 0 },
	{ 230, 0, 1, "CreatePQ", "w", api_CreatePQ16, 0 },
	{ 231, 0, 1, "MinPQ", "w", api_MinPQ16, 0 },
	{ 232, 0, 1, "ExtractPQ", "w", api_ExtractPQ16, 0 },
	{ 233, 0, 1, "InsertPQ", "www", api_InsertPQ16, 0 },
	{ 234, 0, 1, "SizePQ", "ww", api_SizePQ16, 0 },
	{ 235, 0, 1, "DeletePQ", "w", api_DeletePQ16, 0 },
	{ 240, 0, 1, "OpenJob", "ttw", api_OpenJob16, 0 },
	{ 241, 0, 1, "WriteSpool", "wpw", api_WriteSpool16, 0 },
	{ 242, 0, 1, "WriteDialog", "wtw", api_WriteDialog16, 0 },
	{ 243, 0, 1, "CloseJob", "w", api_CloseJob16, 0 },
	{ 244, 0, 1, "DeleteJob", "ww", api_DeleteJob16, 0 },
	{ 245, 0, 0, "GetSpoolJob", "wp", api_GetSpoolJob16, 0 },
	{ 246, 0, 1, "StartSpoolPage", "w", api_StartSpoolPage16, 0 },
	{ 247, 0, 1, "EndSpoolPage", "w", api_EndSpoolPage16, 0 },
	{ 248, 3, 0, "QueryJob", 0, 0, 0 },
	{ 250, 0, 1, "Copy", "ppw", api_Copy16, 0 },
	{ 253, 3, 0, "DeleteSpoolPage", 0, 0, 0 },
	{ 254, 0, 1, "SpoolFile", "pppp", api_SpoolFile16, 0 },
	{ 267, 3, 0, "StartDocPrintEra", 0, 0, 0 },
	{ 268, 3, 0, "StartPagePrinter", 0, 0, 0 },
	{ 269, 3, 0, "WritePrinter", 0, 0, 0 },
	{ 270, 3, 0, "EndPagePrinter", 0, 0, 0 },
	{ 271, 3, 0, "AbortPrinter", 0, 0, 0 },
	{ 272, 3, 0, "EndDocPrinter", 0, 0, 0 },
	{ 274, 3, 0, "ClosePrinter", 0, 0, 0 },
	{ 280, 3, 0, "GetRealDriverInfo", 0, 0, 0 },
	{ 281, 0, 0, "DrvSetPrinterData", "ttlpl", api_DrvSetPrinterData16, 0 },
	{ 282, 0, 0, "DrvGetPrinterData", "ttpplp", api_DrvGetPrinterData16, 0 },
	{ 299, 3, 0, "ENGINEGETCHARWIDTHEX", 0, 0, 0 },
	{ 300, 0, 0, "EngineEnumerateFont", "tPl", api_EngineEnumerateFont16, 0 },
	{ 301, 0, 1, "EngineDeleteFont", "p", api_EngineDeleteFont16, 0 },
	{ 302, 0, 0, "EngineRealizeFont", "ppp", api_EngineRealizeFont16, 0 },
	{ 303, 0, 1, "EngineGetCharWidth", "pwwp", api_EngineGetCharWidth16, 0 },
	{ 304, 0, 1, "EngineSetFontContext", "pw", api_EngineSetFontContext16, 0 },
	{ 305, 0, 1, "EngineGetGlyphBMP", "wpwwplp", api_EngineGetGlyphBMP16, 0 },
	{ 306, 0, 0, "EngineMakeFontDir", "wpp", api_EngineMakeFontDir16, 0 },
	{ 307, 0, 1, "GetCharABCWidths", "wwwp", api_GetCharABCWidths16, 0 },
	{ 308, 0, 1, "GetOutlineTextMetrics", "wwp", api_GetOutlineTextMetrics16, 0 },
	{ 309, 0, 0, "GetGlyphOutline", "wwwplpp", api_GetGlyphOutline16, 0 },
	{ 310, 0, 1, "CreateScalableFontResource", "wttt", api_CreateScalableFontResource16, 0 },
	{ 311, 0, 0, "GetFontData", "wllpl", api_GetFontData16, 0 },
	{ 312, 3, 0, "ConvertOutLineFontFile", 0, 0, 0 },
	{ 313, 0, 1, "GetRasterizerCaps", "pw", api_GetRasterizerCaps16, 0 },
	{ 314, 3, 0, "EngineExtTextOut", 0, 0, 0 },
	{ 315, 0, 0, "EngineRealizeFontExt", "llll", api_EngineRealizeFontExt16, 0 },
	{ 316, 3, 0, "EngineGetCharWidthStr", 0, 0, 0 },
	{ 317, 3, 0, "EngineGetGlyphBmpExt", 0, 0, 0 },
	{ 330, 0, 1, "EnumFontFamilies", "wtPl", api_EnumFontFamilies16, 0 },
	{ 332, 0, 1, "GetKerningPairs", "wwp", api_GetKerningPairs16, 0 },
	{ 345, 0, 1, "GetTextAlign", "w", api_GetTextAlign16, 0 },
	{ 346, 0, 1, "SetTextAlign", "ww", api_SetTextAlign16, 0 },
	{ 347, 3, 0, "MFDRAWTEXT", 0, 0, 0 },
	{ 348, 0, 1, "Chord", "wssssssss", api_Chord16, 0 },
	{ 349, 0, 0, "SetMapperFlags", "wl", api_SetMapperFlags16, 0 },
	{ 350, 0, 1, "GetCharWidth", "wwwp", api_GetCharWidth16, 0 },
	{ 351, 0, 1, "ExtTextOut", "wsswptwp", api_ExtTextOut16, 0 },
	{ 352, 0, 1, "GetPhysicalFontHandle", "w", api_GetPhysicalFontHandle16, 0 },
	{ 353, 0, 0, "GetAspectRatioFilter", "w", api_GetAspectRatioFilter16, 0 },
	{ 354, 0, 1, "ShrinkGDIHeap", "", api_ShrinkGdiHeap16, 0 },
	{ 355, 3, 0, "FTrapping0", 0, 0, 0 },
	{ 360, 0, 1, "CreatePalette", "p", api_CreatePalette16, 0 },
	{ 361, 0, 1, "GDISelectPalette", "www", api_GDISelectPalette16, 0 },
	{ 362, 0, 1, "GDIRealizePalette", "w", api_GDIRealizePalette16, 0 },
	{ 363, 0, 1, "GetPaletteEntries", "wwwp", api_GetPaletteEntries16, 0 },
	{ 364, 0, 1, "SetPaletteEntries", "wwwp", api_SetPaletteEntries16, 0 },
	{ 365, 0, 1, "RealizeDefaultPalette", "w", api_RealizeDefaultPalette16, 0 },
	{ 366, 0, 1, "UpdateColors", "w", api_UpdateColors16, 0 },
	{ 367, 0, 1, "AnimatePalette", "wwwp", api_AnimatePalette16, 0 },
	{ 368, 0, 1, "ResizePalette", "ww", api_ResizePalette16, 0 },
	{ 370, 0, 1, "GetNearestPaletteIndex", "wl", api_GetNearestPaletteIndex16, 0 },
	{ 372, 0, 1, "ExtFloodFill", "wsslw", api_ExtFloodFill16, 0 },
	{ 373, 0, 1, "SetSystemPaletteUse", "ww", api_SetSystemPaletteUse16, 0 },
	{ 374, 0, 1, "GetSystemPaletteUse", "w", api_GetSystemPaletteUse16, 0 },
	{ 375, 0, 1, "GetSystemPaletteEntries", "wwwp", api_GetSystemPaletteEntries16, 0 },
	{ 376, 0, 1, "ResetDC", "wp", api_ResetDC16, 0 },
	{ 377, 0, 1, "StartDoc", "wp", api_StartDoc16, 0 },
	{ 378, 0, 1, "EndDoc", "w", api_EndDoc16, 0 },
	{ 379, 0, 1, "StartPage", "w", api_StartPage16, 0 },
	{ 380, 0, 1, "EndPage", "w", api_EndPage16, 0 },
	{ 381, 0, 1, "SetAbortProc", "wP", api_SetAbortProc16, 0 },
	{ 382, 0, 1, "AbortDoc", "w", api_AbortDoc16, 0 },
	{ 400, 0, 1, "FastWindowFrame", "wpssl", api_FastWindowFrame16, 0 },
	{ 401, 0, 1, "GdiMoveBitmap", "w", api_GdiMoveBitmap16, 0 },
	{ 402, 3, 0, "GDIGETBITSGLOBAL", 0, 0, 0 },
	{ 403, 0, 1, "GdiInit2", "ww", api_GdiInit216, 0 },
	{ 404, 3, 0, "GetTTGlyphIndexMap", 0, 0, 0 },
	{ 405, 0, 1, "FinalGdiInit", "w", api_FinalGdiInit16, 0 },
	{ 406, 3, 0, "CREATEREALBITMAPINDIRECT", 0, 0, 0 },
	{ 407, 0, 1, "CreateUserBitmap", "wwwwp", api_CreateUserBitmap16, 0 },
	{ 408, 3, 0, "CREATEREALBITMAP", 0, 0, 0 },
	{ 409, 0, 1, "CreateUserDiscardableBitmap", "www", api_CreateUserDiscardableBitmap16, 0 },
	{ 410, 0, 1, "IsValidMetaFile", "w", api_IsValidMetaFile16, 0 },
	{ 411, 0, 1, "GetCurLogFont", "w", api_GetCurLogFont16, 0 },
	{ 412, 0, 1, "IsDCCurrentPalette", "w", api_IsDCCurrentPalette16, 0 },
	{ 439, 0, 1, "StretchDIBits", "wssssssssppwl", api_StretchDIBits16, 0 },
	{ 440, 0, 1, "SetDIBits", "wwwwppw", api_SetDIBits16, 0 },
	{ 441, 0, 1, "GetDIBits", "wwwwppw", api_GetDIBits16, 0 },
	{ 442, 0, 1, "CreateDIBitmap", "wplppw", api_CreateDIBitmap16, 0 },
	{ 443, 0, 1, "SetDIBitsToDevice", "wsssssswwppw", api_SetDIBitsToDevice16, 0 },
	{ 444, 0, 1, "CreateRoundRectRgn", "ssssss", api_CreateRoundRectRgn16, 0 },
	{ 445, 0, 1, "CreateDIBPatternBrush", "ww", api_CreateDIBPatternBrush16, 0 },
	{ 449, 3, 0, "DEVICECOLORMATCH", 0, 0, 0 },
	{ 450, 0, 1, "PolyPolygon", "wppw", api_PolyPolygon16, 0 },
	{ 451, 0, 1, "CreatePolyPolygonRgn", "ppww", api_CreatePolyPolygonRgn16, 0 },
	{ 452, 0, 0, "GdiSeeGdiDo", "wwww", api_GdiSeeGdiDo16, 0 },
	{ 460, 0, 1, "GdiTaskTermination", "w", api_GdiTaskTermination16, 0 },
	{ 461, 0, 1, "SetObjectOwner", "ww", api_SetObjectOwner16, 0 },
	{ 462, 0, 1, "IsGDIObject", "w", api_IsGDIObject16, 0 },
	{ 463, 0, 1, "MakeObjectPrivate", "ww", api_MakeObjectPrivate16, 0 },
	{ 464, 3, 0, "FIXUPBOGUSPUBLISHERMETAFILE", 0, 0, 0 },
	{ 465, 0, 1, "RectVisible", "wp", api_RectVisible16, 0 },
	{ 466, 0, 1, "RectInRegion", "wp", api_RectInRegion16, 0 },
	{ 467, 0, 1, "UnicodeToAnsi", "PP", api_UnicodeToAnsi16, 0 },
	{ 468, 0, 1, "GetBitmapDimensionEx", "wp", api_GetBitmapDimensionEx16, 0 },
	{ 469, 0, 1, "GetBrushOrgEx", "wp", api_GetBrushOrgEx16, 0 },
	{ 470, 0, 1, "GetCurrentPositionEx", "wp", api_GetCurrentPositionEx16, 0 },
	{ 471, 0, 1, "GetTextExtentPoint", "wpsp", api_GetTextExtentPoint16, 0 },
	{ 472, 0, 1, "GetViewportExtEx", "wp", api_GetViewportExtEx16, 0 },
	{ 473, 0, 1, "GetViewportOrgEx", "wp", api_GetViewportOrgEx16, 0 },
	{ 474, 0, 1, "GetWindowExtEx", "wp", api_GetWindowExtEx16, 0 },
	{ 475, 0, 1, "GetWindowOrgEx", "wp", api_GetWindowOrgEx16, 0 },
	{ 476, 0, 1, "OffsetViewportOrgEx", "wssp", api_OffsetViewportOrgEx16, 0 },
	{ 477, 0, 1, "OffsetWindowOrgEx", "wssp", api_OffsetWindowOrgEx16, 0 },
	{ 478, 0, 1, "SetBitmapDimensionEx", "wssp", api_SetBitmapDimensionEx16, 0 },
	{ 479, 0, 1, "SetViewportExtEx", "wssp", api_SetViewportExtEx16, 0 },
	{ 480, 0, 1, "SetViewportOrgEx", "wssp", api_SetViewportOrgEx16, 0 },
	{ 481, 0, 1, "SetWindowExtEx", "wssp", api_SetWindowExtEx16, 0 },
	{ 482, 0, 1, "SetWindowOrgEx", "wssp", api_SetWindowOrgEx16, 0 },
	{ 483, 0, 1, "MoveToEx", "wssp", api_MoveToEx16, 0 },
	{ 484, 0, 1, "ScaleViewportExtEx", "wssssp", api_ScaleViewportExtEx16, 0 },
	{ 485, 0, 1, "ScaleWindowExtEx", "wssssp", api_ScaleWindowExtEx16, 0 },
	{ 486, 0, 1, "GetAspectRatioFilterEx", "wp", api_GetAspectRatioFilterEx16, 0 },
	{ 488, 0, 1, "GetFontAssocStatus", "w", api_GetFontAssocStatus16, 0 },
	{ 489, 0, 1, "CreateDIBSection", "wpwpll", api_CreateDIBSection16, 0 },
	{ 490, 3, 0, "CloseEnhMetafile", 0, 0, 0 },
	{ 491, 3, 0, "CopyEnhMetafile", 0, 0, 0 },
	{ 492, 3, 0, "CreateEnhMetafile", 0, 0, 0 },
	{ 493, 3, 0, "DeleteEnhMetafile", 0, 0, 0 },
	{ 495, 3, 0, "GDIComment", 0, 0, 0 },
	{ 496, 3, 0, "GetEnhMetafile", 0, 0, 0 },
	{ 497, 3, 0, "GetEnhMetafileBits", 0, 0, 0 },
	{ 498, 3, 0, "GetEnhMetafileDescription", 0, 0, 0 },
	{ 499, 3, 0, "GetEnhMetafileHeader", 0, 0, 0 },
	{ 501, 3, 0, "GetEnhMetafilePaletteEntries", 0, 0, 0 },
	{ 502, 0, 1, "PolyBezier", "wpw", api_PolyBezier16, 0 },
	{ 503, 0, 1, "PolyBezierTo", "wpw", api_PolyBezierTo16, 0 },
	{ 504, 3, 0, "PlayEnhMetafileRecord", 0, 0, 0 },
	{ 505, 3, 0, "SetEnhMetafileBits", 0, 0, 0 },
	{ 506, 3, 0, "SetMetaRgn", 0, 0, 0 },
	{ 508, 0, 1, "ExtSelectClipRgn", "www", api_ExtSelectClipRgn16, 0 },
	{ 511, 0, 1, "AbortPath", "w", api_AbortPath16, 0 },
	{ 512, 0, 1, "BeginPath", "w", api_BeginPath16, 0 },
	{ 513, 0, 1, "CloseFigure", "w", api_CloseFigure16, 0 },
	{ 514, 0, 1, "EndPath", "w", api_EndPath16, 0 },
	{ 515, 0, 1, "FillPath", "w", api_FillPath16, 0 },
	{ 516, 0, 1, "FlattenPath", "w", api_FlattenPath16, 0 },
	{ 517, 0, 1, "GetPath", "wppw", api_GetPath16, 0 },
	{ 518, 0, 1, "PathToRegion", "w", api_PathToRegion16, 0 },
	{ 519, 0, 1, "SelectClipPath", "ww", api_SelectClipPath16, 0 },
	{ 520, 0, 1, "StrokeAndFillPath", "w", api_StrokeAndFillPath16, 0 },
	{ 521, 0, 1, "StrokePath", "w", api_StrokePath16, 0 },
	{ 522, 0, 1, "WidenPath", "w", api_WidenPath16, 0 },
	{ 523, 3, 0, "ExtCreatePen", 0, 0, 0 },
	{ 524, 0, 1, "GetArcDirection", "w", api_GetArcDirection16, 0 },
	{ 525, 0, 1, "SetArcDirection", "ww", api_SetArcDirection16, 0 },
	{ 526, 3, 0, "GetMiterLimit", 0, 0, 0 },
	{ 527, 3, 0, "SetMiterLimit", 0, 0, 0 },
	{ 528, 3, 0, "GDIParametersInfo", 0, 0, 0 },
	{ 529, 0, 1, "CreateHalftonePalette", "w", api_CreateHalftonePalette16, 0 },
	{ 530, 0, 1, "RawTextOut", "wsstw", api_RawTextOut16, 0 },
	{ 531, 0, 1, "RawExtTextOut", "wsswptwp", api_RawExtTextOut16, 0 },
	{ 532, 0, 1, "RawGetTextExtent", "wtw", api_RawGetTextExtent16, 0 },
	{ 533, 0, 1, "RawGetTextExtentEx", "", api_RawGetTextExtentEx16, 0 },
	{ 536, 0, 1, "BiDiLayout", "", api_BiDiLayout16, 0 },
	{ 537, 0, 1, "BiDiCreateTabString", "", api_BiDiCreateTabString16, 0 },
	{ 538, 0, 1, "BiDiCreateString", "", api_BiDiCreateString16, 0 },
	{ 539, 0, 1, "BiDiStringOut", "", api_BiDiStringOut16, 0 },
	{ 540, 0, 1, "BiDiGlyphOut", "wwwwtwww", api_BiDiGlyphOut16, 0 },
	{ 541, 0, 1, "BiDiJustifyString", "", api_BiDiJustifyString16, 0 },
	{ 542, 0, 1, "BiDiSetStringTabs", "", api_BiDiSetStringTabs16, 0 },
	{ 543, 0, 1, "BiDiGetStringExtent", "", api_BiDiGetStringExtent16, 0 },
	{ 544, 0, 1, "BiDiGetNextGlyph", "", api_BiDiGetNextGlyph16, 0 },
	{ 545, 0, 1, "BiDiGetPrevGlyph", "", api_BiDiGetPrevGlyph16, 0 },
	{ 546, 0, 1, "BiDiIsStringRTL", "", api_BiDiIsStringRTL16, 0 },
	{ 547, 0, 1, "BiDiGlyphLength", "", api_BiDiGlyphLength16, 0 },
	{ 548, 0, 1, "BiDiCaretStringToPel", "", api_BiDiCaretStringTopel16, 0 },
	{ 549, 0, 1, "BiDiCaretPelToString", "", api_BiDiCaretPelToString16, 0 },
	{ 550, 0, 1, "BiDiStringToGlyph", "", api_BiDiStringToGlyph16, 0 },
	{ 551, 0, 1, "BiDiGlyphToString", "", api_BiDiGlyphToString16, 0 },
	{ 552, 0, 1, "BiDiPelToGlyph", "", api_BiDiPelToGlyph16, 0 },
	{ 553, 0, 1, "BiDiGlyphToPel", "", api_BiDiGlyphToPel16, 0 },
	{ 554, 0, 1, "BiDiBounds", "", api_BiDiBounds16, 0 },
	{ 555, 0, 1, "BiDiDeleteString", "w", api_BiDiDeleteString16, 0 },
	{ 556, 0, 1, "BiDiSetDefaults", "", api_BiDiSetDefaults16, 0 },
	{ 558, 0, 1, "BiDiGetDefaults", "", api_BiDiGetDefaults16, 0 },
	{ 559, 0, 1, "BiDiCalcString", "wwpwwwww", api_BiDiCalcString16, 0 },
	{ 560, 0, 1, "BiDiShape", "wwwwwwwwwwwwwwwwwwww", api_BiDiShape16, 0 },
	{ 561, 0, 1, "BiDiFontComplement", "", api_BiDiFontComplement16, 0 },
	{ 563, 0, 1, "BiDiCalcTabString", "", api_BiDiCalcTabString16, 0 },
	{ 564, 0, 1, "BiDiSetKashida", "", api_BiDiSetKashida16, 0 },
	{ 565, 0, 1, "BiDiKExtTextOut", "", api_BiDiKExtTextOut16, 0 },
	{ 566, 0, 1, "BiDiShapeEx", "", api_BiDiShapeEx16, 0 },
	{ 569, 0, 1, "BiDiCreateStringEx", "wwwpwwwww", api_BiDiCreateStringEx16, 0 },
	{ 570, 0, 1, "GetUnicodeMap", "", api_GetUnicodeMap16, 0 },
	{ 571, 0, 1, "GetTextExtentRtoL", "", api_GetTextExtentRtoL16, 0 },
	{ 572, 0, 1, "GetHDCCharSet", "w", api_GetHDCCharSet16, 0 },
	{ 573, 0, 1, "BiDiLayoutEx", "pwwwpwwwwppwww", api_BiDiLayoutEx16, 0 },
	{ 574, 0, 1, "BiDiCreateTabStringEx", "", api_BiDiCreateTabStringEx16, 0 },
	{ 575, 0, 1, "BiDiCalcTabStringEx", "", api_BiDiCalcTabStringEx16, 0 },
	{ 576, 0, 1, "BiDiCalcStringEx", "", api_BiDiCalcStringEx16, 0 },
	{ 588, 0, 1, "SetTextCodePage", "ww", api_SetTextCodePage16, 0 },
	{ 589, 0, 1, "GetTextCodePage", "", api_GetTextCodePage16, 0 },
	{ 602, 0, 1, "SetDIBColorTable", "wwwp", api_SetDIBColorTable16, 0 },
	{ 603, 0, 1, "GetDIBColorTable", "wwwp", api_GetDIBColorTable16, 0 },
	{ 604, 0, 1, "SetSolidBrush", "wl", api_SetSolidBrush16, 0 },
	{ 605, 0, 1, "SysDeleteObject", "w", api_DeleteObject16, 0 },
	{ 606, 0, 1, "SetMagicColors", "wlw", api_SetMagicColors16, 0 },
	{ 607, 0, 0, "GetRegionData", "wlp", api_GetRegionData16, 0 },
	{ 608, 3, 0, "ExtCreateRegion", 0, 0, 0 },
	{ 609, 0, 1, "GdiFreeResources", "l", api_GdiFreeResources16, 0 },
	{ 610, 0, 1, "GdiSignalProc32", "lllw", api_GdiSignalProc, 0 },
	{ 611, 3, 0, "GetRandomRgn", 0, 0, 0 },
	{ 612, 0, 1, "GetTextCharset", "w", api_GetTextCharset16, 0 },
	{ 613, 0, 1, "EnumFontFamiliesEx", "wpPll", api_EnumFontFamiliesEx16, 0 },
	{ 614, 3, 0, "AddLpkToGDI", 0, 0, 0 },
	{ 615, 3, 0, "GetCharacterPlacement", 0, 0, 0 },
	{ 616, 0, 0, "GetFontLanguageInfo", "w", api_GetFontLanguageInfo16, 0 },
	{ 650, 3, 0, "BuildInverseTableDIB", 0, 0, 0 },
	{ 701, 3, 0, "GDITHKCONNECTIONDATALS", 0, 0, 0 },
	{ 702, 3, 0, "FT_GDIFTHKTHKCONNECTIONDATA", 0, 0, 0 },
	{ 703, 3, 0, "FDTHKCONNECTIONDATASL", 0, 0, 0 },
	{ 704, 3, 0, "ICMTHKCONNECTIONDATASL", 0, 0, 0 },
	{ 820, 3, 0, "ICMCreateTransform", 0, 0, 0 },
	{ 821, 3, 0, "ICMDeleteTransform", 0, 0, 0 },
	{ 822, 3, 0, "ICMTranslateRGB", 0, 0, 0 },
	{ 823, 3, 0, "ICMTranslateRGBs", 0, 0, 0 },
	{ 824, 3, 0, "ICMCheckColorsInGamut", 0, 0, 0 },
	{ 1000, 0, 1, "SetLayout", "wl", api_SetLayout16, 0 },
	{ 1001, 3, 0, "GetLayout", 0, 0, 0 },
	{ 5000, 0, 0, "DllEntryPoint", "lwwwlw", api_DllEntryPoint, 0 },
};

static const struct w16_api api_mmsystem[] = {
	{ 1, 0, 0, "WEP", "wwwp", api_MMSYSTEM_WEP, 0 },
	{ 2, 0, 0, "sndPlaySound", "pw", api_sndPlaySound16, 0 },
	{ 3, 0, 0, "PlaySound", "pwl", api_PlaySound16, 0 },
	{ 4, 0, 0, "DllEntryPoint", "lwwwlw", api_MMSYSTEM_LibMain, 0 },
	{ 5, 0, 0, "mmsystemGetVersion", "", api_mmsystemGetVersion16, 0 },
	{ 6, 0, 0, "DriverProc", "lwwll", api_DriverProc16, 0 },
	{ 8, 0, 0, "WMMMidiRunOnce", "", api_WMMMidiRunOnce16, 0 },
	{ 30, 0, 1, "OutputDebugStr", "t", api_OutputDebugStr16, 0 },
	{ 31, 0, 0, "DriverCallback", "lwwwlll", api_DriverCallback16, 0 },
	{ 32, 0, 0, "StackEnter", "", api_StackEnter16, 0 },
	{ 33, 0, 0, "StackLeave", "", api_StackLeave16, 0 },
	{ 34, 3, 0, "MMDRVINSTALL", 0, 0, 0 },
	{ 101, 0, 0, "joyGetNumDevs", "", api_joyGetNumDevs16, 0 },
	{ 102, 0, 0, "joyGetDevCaps", "wpw", api_joyGetDevCaps16, 0 },
	{ 103, 0, 0, "joyGetPos", "wp", api_joyGetPos16, 0 },
	{ 104, 0, 0, "joyGetThreshold", "wp", api_joyGetThreshold16, 0 },
	{ 105, 0, 0, "joyReleaseCapture", "w", api_joyReleaseCapture16, 0 },
	{ 106, 0, 0, "joySetCapture", "wwww", api_joySetCapture16, 0 },
	{ 107, 0, 0, "joySetThreshold", "ww", api_joySetThreshold16, 0 },
	{ 109, 0, 0, "joySetCalibration", "w", api_joySetCalibration16, 0 },
	{ 110, 0, 0, "joyGetPosEx", "wp", api_joyGetPosEx16, 0 },
	{ 111, 3, 0, "JOYCONFIGCHANGED", 0, 0, 0 },
	{ 201, 0, 0, "midiOutGetNumDevs", "", api_midiOutGetNumDevs16, 0 },
	{ 202, 0, 0, "midiOutGetDevCaps", "wpw", api_midiOutGetDevCaps16, 0 },
	{ 203, 0, 0, "midiOutGetErrorText", "wpw", api_midiOutGetErrorText16, 0 },
	{ 204, 0, 0, "midiOutOpen", "pwlll", api_midiOutOpen16, 0 },
	{ 205, 0, 0, "midiOutClose", "w", api_midiOutClose16, 0 },
	{ 206, 0, 0, "midiOutPrepareHeader", "wPw", api_midiOutPrepareHeader16, 0 },
	{ 207, 0, 0, "midiOutUnprepareHeader", "wPw", api_midiOutUnprepareHeader16, 0 },
	{ 208, 0, 0, "midiOutShortMsg", "wl", api_midiOutShortMsg16, 0 },
	{ 209, 0, 0, "midiOutLongMsg", "wPw", api_midiOutLongMsg16, 0 },
	{ 210, 0, 0, "midiOutReset", "w", api_midiOutReset16, 0 },
	{ 211, 0, 0, "midiOutGetVolume", "wp", api_midiOutGetVolume16, 0 },
	{ 212, 0, 0, "midiOutSetVolume", "wl", api_midiOutSetVolume16, 0 },
	{ 213, 0, 0, "midiOutCachePatches", "wwpw", api_midiOutCachePatches16, 0 },
	{ 214, 0, 0, "midiOutCacheDrumPatches", "wwpw", api_midiOutCacheDrumPatches16, 0 },
	{ 215, 0, 0, "midiOutGetID", "wp", api_midiOutGetID16, 0 },
	{ 216, 0, 0, "midiOutMessage", "wwll", api_midiOutMessage16, 0 },
	{ 250, 0, 0, "midiStreamProperty", "wpl", api_midiStreamProperty16, 0 },
	{ 251, 0, 0, "midiStreamOpen", "ppllll", api_midiStreamOpen16, 0 },
	{ 252, 0, 0, "midiStreamClose", "w", api_midiStreamClose16, 0 },
	{ 253, 0, 0, "midiStreamPosition", "wpw", api_midiStreamPosition16, 0 },
	{ 254, 0, 0, "midiStreamOut", "wpw", api_midiStreamOut16, 0 },
	{ 255, 0, 0, "midiStreamPause", "w", api_midiStreamPause16, 0 },
	{ 256, 0, 0, "midiStreamRestart", "w", api_midiStreamRestart16, 0 },
	{ 257, 0, 0, "midiStreamStop", "w", api_midiStreamStop16, 0 },
	{ 301, 0, 0, "midiInGetNumDevs", "", api_midiInGetNumDevs16, 0 },
	{ 302, 0, 0, "midiInGetDevCaps", "wpw", api_midiInGetDevCaps16, 0 },
	{ 303, 0, 0, "midiInGetErrorText", "wpw", api_midiOutGetErrorText16, 0 },
	{ 304, 0, 0, "midiInOpen", "pwlll", api_midiInOpen16, 0 },
	{ 305, 0, 0, "midiInClose", "w", api_midiInClose16, 0 },
	{ 306, 0, 0, "midiInPrepareHeader", "wPw", api_midiInPrepareHeader16, 0 },
	{ 307, 0, 0, "midiInUnprepareHeader", "wPw", api_midiInUnprepareHeader16, 0 },
	{ 308, 0, 0, "midiInAddBuffer", "wPw", api_midiInAddBuffer16, 0 },
	{ 309, 0, 0, "midiInStart", "w", api_midiInStart16, 0 },
	{ 310, 0, 0, "midiInStop", "w", api_midiInStop16, 0 },
	{ 311, 0, 0, "midiInReset", "w", api_midiInReset16, 0 },
	{ 312, 0, 0, "midiInGetID", "wp", api_midiInGetID16, 0 },
	{ 313, 0, 0, "midiInMessage", "wwll", api_midiInMessage16, 0 },
	{ 350, 0, 0, "auxGetNumDevs", "", api_auxGetNumDevs16, 0 },
	{ 351, 0, 0, "auxGetDevCaps", "wpw", api_auxGetDevCaps16, 0 },
	{ 352, 0, 0, "auxGetVolume", "wp", api_auxGetVolume16, 0 },
	{ 353, 0, 0, "auxSetVolume", "wl", api_auxSetVolume16, 0 },
	{ 354, 0, 0, "auxOutMessage", "wwll", api_auxOutMessage16, 0 },
	{ 401, 0, 0, "waveOutGetNumDevs", "", api_waveOutGetNumDevs16, 0 },
	{ 402, 0, 0, "waveOutGetDevCaps", "wpw", api_waveOutGetDevCaps16, 0 },
	{ 403, 0, 0, "waveOutGetErrorText", "wpw", api_waveOutGetErrorText16, 0 },
	{ 404, 0, 0, "waveOutOpen", "pwplll", api_waveOutOpen16, 0 },
	{ 405, 0, 0, "waveOutClose", "w", api_waveOutClose16, 0 },
	{ 406, 0, 0, "waveOutPrepareHeader", "wPw", api_waveOutPrepareHeader16, 0 },
	{ 407, 0, 0, "waveOutUnprepareHeader", "wPw", api_waveOutUnprepareHeader16, 0 },
	{ 408, 0, 0, "waveOutWrite", "wPw", api_waveOutWrite16, 0 },
	{ 409, 0, 0, "waveOutPause", "w", api_waveOutPause16, 0 },
	{ 410, 0, 0, "waveOutRestart", "w", api_waveOutRestart16, 0 },
	{ 411, 0, 0, "waveOutReset", "w", api_waveOutReset16, 0 },
	{ 412, 0, 0, "waveOutGetPosition", "wpw", api_waveOutGetPosition16, 0 },
	{ 413, 0, 0, "waveOutGetPitch", "wp", api_waveOutGetPitch16, 0 },
	{ 414, 0, 0, "waveOutSetPitch", "wl", api_waveOutSetPitch16, 0 },
	{ 415, 0, 0, "waveOutGetVolume", "wp", api_waveOutGetVolume16, 0 },
	{ 416, 0, 0, "waveOutSetVolume", "wl", api_waveOutSetVolume16, 0 },
	{ 417, 0, 0, "waveOutGetPlaybackRate", "wp", api_waveOutGetPlaybackRate16, 0 },
	{ 418, 0, 0, "waveOutSetPlaybackRate", "wl", api_waveOutSetPlaybackRate16, 0 },
	{ 419, 0, 0, "waveOutBreakLoop", "w", api_waveOutBreakLoop16, 0 },
	{ 420, 0, 0, "waveOutGetID", "wp", api_waveOutGetID16, 0 },
	{ 421, 0, 0, "waveOutMessage", "wwll", api_waveOutMessage16, 0 },
	{ 501, 0, 0, "waveInGetNumDevs", "", api_waveInGetNumDevs16, 0 },
	{ 502, 0, 0, "waveInGetDevCaps", "wpw", api_waveInGetDevCaps16, 0 },
	{ 503, 0, 0, "waveInGetErrorText", "wpw", api_waveOutGetErrorText16, 0 },
	{ 504, 0, 0, "waveInOpen", "pwplll", api_waveInOpen16, 0 },
	{ 505, 0, 0, "waveInClose", "w", api_waveInClose16, 0 },
	{ 506, 0, 0, "waveInPrepareHeader", "wPw", api_waveInPrepareHeader16, 0 },
	{ 507, 0, 0, "waveInUnprepareHeader", "wPw", api_waveInUnprepareHeader16, 0 },
	{ 508, 0, 0, "waveInAddBuffer", "wPw", api_waveInAddBuffer16, 0 },
	{ 509, 0, 0, "waveInStart", "w", api_waveInStart16, 0 },
	{ 510, 0, 0, "waveInStop", "w", api_waveInStop16, 0 },
	{ 511, 0, 0, "waveInReset", "w", api_waveInReset16, 0 },
	{ 512, 0, 0, "waveInGetPosition", "wpw", api_waveInGetPosition16, 0 },
	{ 513, 0, 0, "waveInGetID", "wp", api_waveInGetID16, 0 },
	{ 514, 0, 0, "waveInMessage", "wwll", api_waveInMessage16, 0 },
	{ 601, 0, 0, "timeGetSystemTime", "pw", api_timeGetSystemTime16, 0 },
	{ 602, 0, 0, "timeSetEvent", "wwPlw", api_timeSetEvent16, 0 },
	{ 603, 0, 0, "timeKillEvent", "w", api_timeKillEvent16, 0 },
	{ 604, 0, 0, "timeGetDevCaps", "pw", api_timeGetDevCaps16, 0 },
	{ 605, 0, 0, "timeBeginPeriod", "w", api_timeBeginPeriod16, 0 },
	{ 606, 0, 0, "timeEndPeriod", "w", api_timeEndPeriod16, 0 },
	{ 607, 0, 0, "timeGetTime", "", api_timeGetTime16, 0 },
	{ 701, 0, 0, "mciSendCommand", "wwll", api_mciSendCommand16, 0 },
	{ 702, 0, 0, "mciSendString", "tpww", api_mciSendString16, 0 },
	{ 703, 0, 0, "mciGetDeviceID", "p", api_mciGetDeviceID16, 0 },
	{ 705, 0, 0, "mciLoadCommandResource", "wtw", api_mciLoadCommandResource16, 0 },
	{ 706, 0, 0, "mciGetErrorString", "lpw", api_mciGetErrorString16, 0 },
	{ 707, 0, 0, "mciSetDriverData", "wl", api_mciSetDriverData16, 0 },
	{ 708, 0, 0, "mciGetDriverData", "w", api_mciGetDriverData16, 0 },
	{ 710, 0, 0, "mciDriverYield", "w", api_mciDriverYield16, 0 },
	{ 711, 0, 0, "mciDriverNotify", "www", api_mciDriverNotify16, 0 },
	{ 712, 0, 0, "mciExecute", "p", api_mciExecute16, 0 },
	{ 713, 0, 0, "mciFreeCommandResource", "w", api_mciFreeCommandResource16, 0 },
	{ 714, 0, 0, "mciSetYieldProc", "wpl", api_mciSetYieldProc16, 0 },
	{ 715, 0, 0, "mciGetDeviceIDFromElementID", "lp", api_mciGetDeviceIDFromElementID16, 0 },
	{ 716, 0, 0, "mciGetYieldProc", "wp", api_mciGetYieldProc16, 0 },
	{ 717, 0, 0, "mciGetCreatorTask", "w", api_mciGetCreatorTask16, 0 },
	{ 800, 0, 0, "mixerGetNumDevs", "", api_mixerGetNumDevs16, 0 },
	{ 801, 0, 0, "mixerGetDevCaps", "wpw", api_mixerGetDevCaps16, 0 },
	{ 802, 0, 0, "mixerOpen", "pwlll", api_mixerOpen16, 0 },
	{ 803, 0, 0, "mixerClose", "w", api_mixerClose16, 0 },
	{ 804, 0, 0, "mixerMessage", "wwll", api_mixerMessage16, 0 },
	{ 805, 0, 0, "mixerGetLineInfo", "wpl", api_mixerGetLineInfo16, 0 },
	{ 806, 0, 0, "mixerGetID", "wpl", api_mixerGetID16, 0 },
	{ 807, 0, 0, "mixerGetLineControls", "wpl", api_mixerGetLineControls16, 0 },
	{ 808, 0, 0, "mixerGetControlDetails", "wpl", api_mixerGetControlDetails16, 0 },
	{ 809, 0, 0, "mixerSetControlDetails", "wpl", api_mixerSetControlDetails16, 0 },
	{ 900, 0, 0, "mmTaskCreate", "lpl", api_mmTaskCreate16, 0 },
	{ 902, 0, 0, "mmTaskBlock", "w", api_mmTaskBlock16, 0 },
	{ 903, 0, 0, "mmTaskSignal", "w", api_mmTaskSignal16, 0 },
	{ 904, 0, 1, "mmGetCurrentTask", "", api_mmGetCurrentTask16, 0 },
	{ 905, 0, 0, "mmTaskYield", "", api_mmTaskYield16, 0 },
	{ 1100, 0, 0, "DrvOpen", "ttl", api_DrvOpen16, 0 },
	{ 1101, 0, 0, "DrvClose", "wll", api_DrvClose16, 0 },
	{ 1102, 0, 0, "DrvSendMessage", "wwll", api_DrvSendMessage16, 0 },
	{ 1103, 0, 0, "DrvGetModuleHandle", "w", api_DrvGetModuleHandle16, 0 },
	{ 1104, 0, 0, "DrvDefDriverProc", "lwwll", api_DrvDefDriverProc16, 0 },
	{ 1120, 0, 0, "mmThreadCreate", "Ppll", api_mmThreadCreate16, 0 },
	{ 1121, 0, 0, "mmThreadSignal", "w", api_mmThreadSignal16, 0 },
	{ 1122, 0, 0, "mmThreadBlock", "w", api_mmThreadBlock16, 0 },
	{ 1123, 0, 0, "mmThreadIsCurrent", "w", api_mmThreadIsCurrent16, 0 },
	{ 1124, 0, 0, "mmThreadIsValid", "w", api_mmThreadIsValid16, 0 },
	{ 1125, 0, 0, "mmThreadGetTask", "w", api_mmThreadGetTask16, 0 },
	{ 1150, 0, 0, "mmShowMMCPLPropertySheet", "wttt", api_mmShowMMCPLPropertySheet16, 0 },
	{ 1210, 0, 0, "mmioOpen", "tpl", api_mmioOpen16, 0 },
	{ 1211, 0, 0, "mmioClose", "ww", api_mmioClose16, 0 },
	{ 1212, 0, 0, "mmioRead", "wpl", api_mmioRead16, 0 },
	{ 1213, 0, 0, "mmioWrite", "wpl", api_mmioWrite16, 0 },
	{ 1214, 0, 0, "mmioSeek", "wlw", api_mmioSeek16, 0 },
	{ 1215, 0, 0, "mmioGetInfo", "wpw", api_mmioGetInfo16, 0 },
	{ 1216, 0, 0, "mmioSetInfo", "wpw", api_mmioSetInfo16, 0 },
	{ 1217, 0, 0, "mmioSetBuffer", "wPlw", api_mmioSetBuffer16, 0 },
	{ 1218, 0, 0, "mmioFlush", "ww", api_mmioFlush16, 0 },
	{ 1219, 0, 0, "mmioAdvance", "wpw", api_mmioAdvance16, 0 },
	{ 1220, 0, 0, "mmioStringToFOURCC", "tw", api_mmioStringToFOURCC16, 0 },
	{ 1221, 0, 0, "mmioInstallIOProc", "lPl", api_mmioInstallIOProc16, 0 },
	{ 1222, 0, 0, "mmioSendMessage", "wwll", api_mmioSendMessage16, 0 },
	{ 1223, 0, 0, "mmioDescend", "wppw", api_mmioDescend16, 0 },
	{ 1224, 0, 0, "mmioAscend", "wpw", api_mmioAscend16, 0 },
	{ 1225, 0, 0, "mmioCreateChunk", "wpw", api_mmioCreateChunk16, 0 },
	{ 1226, 0, 0, "mmioRename", "pppl", api_mmioRename16, 0 },
	{ 2047, 0, 0, "__wine_mmThreadEntryPoint", "l", api_WINE_mmThreadEntryPoint, 0 },
};

static const struct w16_api api_commdlg[] = {
	{ 1, 0, 1, "GetOpenFileName", "P", api_GetOpenFileName16, 0 },
	{ 2, 0, 1, "GetSaveFileName", "P", api_GetSaveFileName16, 0 },
	{ 5, 0, 1, "ChooseColor", "p", api_ChooseColor16, 0 },
	{ 6, 0, 0, "FileOpenDlgProc", "wwwl", api_FileOpenDlgProc16, 0 },
	{ 7, 0, 0, "FileSaveDlgProc", "wwwl", api_FileSaveDlgProc16, 0 },
	{ 8, 0, 0, "ColorDlgProc", "wwwl", api_ColorDlgProc16, 0 },
	{ 11, 0, 1, "FindText", "P", api_FindText16, 0 },
	{ 12, 0, 1, "ReplaceText", "P", api_ReplaceText16, 0 },
	{ 13, 0, 0, "FindTextDlgProc", "wwwl", api_FindTextDlgProc16, 0 },
	{ 14, 0, 0, "ReplaceTextDlgProc", "wwwl", api_ReplaceTextDlgProc16, 0 },
	{ 15, 0, 1, "ChooseFont", "P", api_ChooseFont16, 0 },
	{ 16, 0, 1, "FormatCharDlgProc", "wwwl", api_FormatCharDlgProc16, 0 },
	{ 18, 0, 1, "FontStyleEnumProc", "ppwl", api_FontStyleEnumProc16, 0 },
	{ 19, 0, 1, "FontFamilyEnumProc", "ppwl", api_FontFamilyEnumProc16, 0 },
	{ 20, 0, 1, "PrintDlg", "P", api_PrintDlg16, 0 },
	{ 21, 0, 0, "PrintDlgProc", "wwwl", api_PrintDlgProc16, 0 },
	{ 22, 0, 0, "PrintSetupDlgProc", "wwwl", api_PrintSetupDlgProc16, 0 },
	{ 26, 0, 0, "CommDlgExtendedError", "", api_CommDlgExtendedError16, 0 },
	{ 27, 0, 1, "GetFileTitle", "tpw", api_GetFileTitle16, 0 },
};

static const struct w16_api api_keyboard[] = {
	{ 1, 0, 1, "Inquire", "p", api_Inquire16, 0 },
	{ 2, 0, 1, "Enable", "Pp", api_Enable16, 0 },
	{ 3, 0, 1, "Disable", "", api_Disable16, 0 },
	{ 4, 0, 1, "ToAscii", "wwppw", api_ToAscii16, 0 },
	{ 5, 0, 1, "AnsiToOem", "tp", api_AnsiToOem16, 0 },
	{ 6, 0, 1, "OemToAnsi", "tp", api_OemToAnsi16, 0 },
	{ 7, 0, 1, "SetSpeed", "w", api_SetSpeed16, 0 },
	{ 9, 3, 0, "INQUIREEX", 0, 0, 0 },
	{ 10, 3, 0, "TOASCIIEX", 0, 0, 0 },
	{ 11, 3, 0, "VKKEYSCANEX", 0, 0, 0 },
	{ 12, 3, 0, "MAPVIRTUALKEYEX", 0, 0, 0 },
	{ 13, 3, 0, "NEWTABLEEX", 0, 0, 0 },
	{ 14, 3, 0, "__EXPORTEDSTUB", 0, 0, 0 },
	{ 100, 0, 0, "ScreenSwitchEnable", "w", api_ScreenSwitchEnable16, 0 },
	{ 128, 0, 0, "OemKeyScan", "w", api_OemKeyScan16, 0 },
	{ 129, 0, 1, "VkKeyScan", "w", api_VkKeyScan16, 0 },
	{ 130, 0, 1, "GetKeyboardType", "w", api_GetKeyboardType16, 0 },
	{ 131, 0, 1, "MapVirtualKey", "ww", api_MapVirtualKey16, 0 },
	{ 132, 0, 1, "GetKBCodePage", "", api_GetKBCodePage16, 0 },
	{ 133, 0, 1, "GetKeyNameText", "lpw", api_GetKeyNameText16, 0 },
	{ 134, 0, 1, "AnsiToOemBuff", "ppw", api_AnsiToOemBuff16, 0 },
	{ 135, 0, 1, "OemToAnsiBuff", "ppw", api_OemToAnsiBuff16, 0 },
};

static const struct w16_api api_toolhelp[] = {
	{ 50, 0, 1, "GlobalHandleToSel", "w", api_GlobalHandleToSel16, 0 },
	{ 51, 0, 1, "GlobalFirst", "pw", api_GlobalFirst16, 0 },
	{ 52, 0, 1, "GlobalNext", "pw", api_GlobalNext16, 0 },
	{ 53, 0, 1, "GlobalInfo", "p", api_GlobalInfo16, 0 },
	{ 54, 0, 1, "GlobalEntryHandle", "pw", api_GlobalEntryHandle16, 0 },
	{ 55, 0, 1, "GlobalEntryModule", "pww", api_GlobalEntryModule16, 0 },
	{ 56, 0, 1, "LocalInfo", "pw", api_LocalInfo16, 0 },
	{ 57, 0, 1, "LocalFirst", "pw", api_LocalFirst16, 0 },
	{ 58, 0, 1, "LocalNext", "p", api_LocalNext16, 0 },
	{ 59, 0, 1, "ModuleFirst", "p", api_ModuleFirst16, 0 },
	{ 60, 0, 1, "ModuleNext", "p", api_ModuleNext16, 0 },
	{ 61, 0, 1, "ModuleFindName", "pp", api_ModuleFindName16, 0 },
	{ 62, 0, 1, "ModuleFindHandle", "pw", api_ModuleFindHandle16, 0 },
	{ 63, 0, 1, "TaskFirst", "p", api_TaskFirst16, 0 },
	{ 64, 0, 1, "TaskNext", "p", api_TaskNext16, 0 },
	{ 65, 0, 1, "TaskFindHandle", "pw", api_TaskFindHandle16, 0 },
	{ 66, 0, 1, "StackTraceFirst", "pw", api_StackTraceFirst16, 0 },
	{ 67, 0, 1, "StackTraceCSIPFirst", "pwwww", api_StackTraceCSIPFirst16, 0 },
	{ 68, 0, 1, "StackTraceNext", "p", api_StackTraceNext16, 0 },
	{ 69, 0, 1, "ClassFirst", "p", api_ClassFirst16, 0 },
	{ 70, 0, 1, "ClassNext", "p", api_ClassNext16, 0 },
	{ 71, 0, 1, "SystemHeapInfo", "p", api_SystemHeapInfo16, 0 },
	{ 72, 0, 1, "MemManInfo", "p", api_MemManInfo16, 0 },
	{ 73, 0, 1, "NotifyRegister", "wPw", api_NotifyRegister16, 0 },
	{ 74, 0, 1, "NotifyUnRegister", "w", api_NotifyUnRegister16, 0 },
	{ 75, 0, 1, "InterruptRegister", "wP", api_InterruptRegister16, 0 },
	{ 76, 0, 1, "InterruptUnRegister", "w", api_InterruptUnRegister16, 0 },
	{ 77, 0, 1, "TerminateApp", "ww", api_TerminateApp16, 0 },
	{ 78, 0, 0, "MemoryRead", "wlpl", api_MemoryRead16, 0 },
	{ 79, 0, 0, "MemoryWrite", "wlpl", api_MemoryWrite16, 0 },
	{ 80, 0, 1, "TimerCount", "p", api_TimerCount16, 0 },
	{ 81, 0, 1, "TaskSetCSIP", "www", api_TaskSetCSIP16, 0 },
	{ 82, 0, 0, "TaskGetCSIP", "w", api_TaskGetCSIP16, 0 },
	{ 83, 0, 1, "TaskSwitch", "wP", api_TaskSwitch16, 0 },
	{ 84, 0, 1, "Local32Info", "pw", api_Local32Info16, 0 },
	{ 85, 0, 1, "Local32First", "pw", api_Local32First16, 0 },
	{ 86, 0, 1, "Local32Next", "p", api_Local32Next16, 0 },
	{ 100, 1, 0, "next_intcb", "", api_next_intcb, 0 },
};

static const struct w16_api api_win87em[] = {
	{ 1, 1, 0, "__fpMath", "", api__fpMath, 0 },
	{ 3, 0, 1, "__Win87EmInfo", "pw", api___WinEm87Info, 0 },
	{ 4, 0, 1, "__Win87EmRestore", "pw", api___WinEm87Restore, 0 },
	{ 5, 0, 1, "__Win87EmSave", "pw", api___WinEm87Save, 0 },
	{ 100, 1, 0, "fpe_return", "", api_fpe_return, 0 },
};

static const struct w16_api api_shell[] = {
	{ 1, 0, 0, "RegOpenKey", "ltp", api_RegOpenKey16, 0 },
	{ 2, 0, 0, "RegCreateKey", "ltp", api_RegCreateKey16, 0 },
	{ 3, 0, 0, "RegCloseKey", "l", api_RegCloseKey16, 0 },
	{ 4, 0, 0, "RegDeleteKey", "lt", api_RegDeleteKey16, 0 },
	{ 5, 0, 0, "RegSetValue", "ltltl", api_RegSetValue16, 0 },
	{ 6, 0, 0, "RegQueryValue", "ltpp", api_RegQueryValue16, 0 },
	{ 7, 0, 0, "RegEnumKey", "llpl", api_RegEnumKey16, 0 },
	{ 9, 0, 1, "DragAcceptFiles", "ww", api_DragAcceptFiles16, 0 },
	{ 11, 0, 1, "DragQueryFile", "wsps", api_DragQueryFile16, 0 },
	{ 12, 0, 1, "DragFinish", "w", api_DragFinish16, 0 },
	{ 13, 0, 1, "DragQueryPoint", "wp", api_DragQueryPoint16, 0 },
	{ 20, 0, 1, "ShellExecute", "wtttts", api_ShellExecute16, 0 },
	{ 21, 0, 1, "FindExecutable", "ttp", api_FindExecutable16, 0 },
	{ 22, 0, 1, "ShellAbout", "wppw", api_ShellAbout16, 0 },
	{ 33, 0, 1, "AboutDlgProc", "wwwl", api_AboutDlgProc16, 0 },
	{ 34, 0, 1, "ExtractIcon", "wts", api_ExtractIcon16, 0 },
	{ 36, 0, 1, "ExtractAssociatedIcon", "wpp", api_ExtractAssociatedIcon16, 0 },
	{ 37, 0, 0, "DoEnvironmentSubst", "pw", api_DoEnvironmentSubst16, 0 },
	{ 38, 0, 0, "FindEnvironmentString", "p", api_FindEnvironmentString16, 0 },
	{ 39, 0, 1, "InternalExtractIcon", "wpsw", api_InternalExtractIcon16, 0 },
	{ 40, 0, 1, "ExtractIconEx", "twppw", api_ExtractIconEx16, 0 },
	{ 101, 0, 0, "DllEntryPoint", "lwwwlw", api_SHELL_DllEntryPoint, 0 },
	{ 102, 0, 1, "RegisterShellHook", "ww", api_RegisterShellHook16, 0 },
	{ 103, 0, 0, "ShellHookProc", "wwl", api_ShellHookProc16, 0 },
	{ 157, 3, 0, "RESTARTDIALOG", 0, 0, 0 },
	{ 262, 0, 1, "DriveType", "l", api_DriveType16, 0 },
	{ 400, 0, 0, "SHFormatDrive", "wwww", api_SHFormatDrive16, 0 },
	{ 401, 3, 0, "SHCHECKDRIVE", 0, 0, 0 },
};

static const struct w16_api api_ver[] = {
	{ 2, 0, 0, "GetFileResourceSize", "tttp", api_GetFileResourceSize16, 0 },
	{ 3, 0, 0, "GetFileResource", "tttllp", api_GetFileResource16, 0 },
	{ 6, 0, 0, "GetFileVersionInfoSize", "tp", api_GetFileVersionInfoSize16, 0 },
	{ 7, 0, 0, "GetFileVersionInfo", "tllp", api_GetFileVersionInfo16, 0 },
	{ 8, 0, 0, "VerFindFile", "wtttpppp", api_VerFindFile16, 0 },
	{ 9, 0, 0, "VerInstallFile", "wtttttpp", api_VerInstallFile16, 0 },
	{ 10, 0, 0, "VerLanguageName", "wpw", api_VerLanguageName16, 0 },
	{ 11, 0, 0, "VerQueryValue", "Ptpp", api_VerQueryValue16, 0 },
	{ 20, 3, 0, "GETFILEVERSIONINFORAW", 0, 0, 0 },
};

static const struct w16_api api_lzexpand[] = {
	{ 1, 0, 0, "LZCopy", "ww", api_LZCopy16, 0 },
	{ 2, 0, 1, "LZOpenFile", "tpw", api_LZOpenFile16, 0 },
	{ 3, 0, 1, "LZInit", "w", api_LZInit16, 0 },
	{ 4, 0, 0, "LZSeek", "wlw", api_LZSeek16, 0 },
	{ 5, 0, 1, "LZRead", "wpw", api_LZRead16, 0 },
	{ 6, 0, 1, "LZClose", "w", api_LZClose16, 0 },
	{ 7, 0, 1, "LZStart", "", api_LZStart16, 0 },
	{ 8, 0, 0, "CopyLZFile", "ww", api_CopyLZFile16, 0 },
	{ 9, 0, 1, "LZDone", "", api_LZDone16, 0 },
	{ 10, 0, 1, "GetExpandedName", "tp", api_GetExpandedName16, 0 },
};

static const struct w16_api api_system[] = {
	{ 1, 0, 0, "InquireSystem", "ww", api_InquireSystem16, 0 },
	{ 2, 0, 1, "CreateSystemTimer", "wP", api_CreateSystemTimer16, 0 },
	{ 3, 0, 1, "KillSystemTimer", "w", api_SYSTEM_KillSystemTimer, 0 },
	{ 4, 0, 1, "EnableSystemTimers", "", api_EnableSystemTimers16, 0 },
	{ 5, 0, 1, "DisableSystemTimers", "", api_DisableSystemTimers16, 0 },
	{ 6, 0, 0, "GetSystemMSecCount", "", api_GetSystemMSecCount16, 0 },
	{ 7, 0, 1, "Get80x87SaveSize", "", api_Get80x87SaveSize16, 0 },
	{ 8, 0, 1, "Save80x87State", "p", api_Save80x87State16, 0 },
	{ 9, 0, 1, "Restore80x87State", "p", api_Restore80x87State16, 0 },
	{ 13, 3, 0, "INQUIRELONGINTS", 0, 0, 0 },
	{ 20, 0, 1, "A20_Proc", "w", api_A20_Proc16, 0 },
};

static const struct w16_api api_sound[] = {
	{ 1, 0, 1, "OpenSound", "", api_OpenSound16, 0 },
	{ 2, 0, 1, "CloseSound", "", api_CloseSound16, 0 },
	{ 3, 0, 1, "SetVoiceQueueSize", "ww", api_SetVoiceQueueSize16, 0 },
	{ 4, 0, 1, "SetVoiceNote", "wwww", api_SetVoiceNote16, 0 },
	{ 5, 0, 1, "SetVoiceAccent", "wwwww", api_SetVoiceAccent16, 0 },
	{ 6, 0, 1, "SetVoiceEnvelope", "www", api_SetVoiceEnvelope16, 0 },
	{ 7, 0, 1, "SetSoundNoise", "ww", api_SetSoundNoise16, 0 },
	{ 8, 0, 1, "SetVoiceSound", "wlw", api_SetVoiceSound16, 0 },
	{ 9, 0, 1, "StartSound", "", api_StartSound16, 0 },
	{ 10, 0, 1, "StopSound", "", api_StopSound16, 0 },
	{ 11, 0, 1, "WaitSoundState", "w", api_WaitSoundState16, 0 },
	{ 12, 0, 1, "SyncAllVoices", "", api_SyncAllVoices16, 0 },
	{ 13, 0, 1, "CountVoiceNotes", "w", api_CountVoiceNotes16, 0 },
	{ 14, 0, 0, "GetThresholdEvent", "", api_GetThresholdEvent16, 0 },
	{ 15, 0, 1, "GetThresholdStatus", "", api_GetThresholdStatus16, 0 },
	{ 16, 0, 1, "SetVoiceThreshold", "ww", api_SetVoiceThreshold16, 0 },
	{ 17, 0, 1, "DoBeep", "", api_DoBeep16, 0 },
	{ 18, 3, 0, "MYOPENSOUND", 0, 0, 0 },
};

const struct w16_module w16_modules[] = {
	{ "KERNEL", api_kernel, 475 },
	{ "USER", api_user, 556 },
	{ "GDI", api_gdi, 436 },
	{ "MMSYSTEM", api_mmsystem, 166 },
	{ "COMMDLG", api_commdlg, 19 },
	{ "KEYBOARD", api_keyboard, 22 },
	{ "TOOLHELP", api_toolhelp, 38 },
	{ "WIN87EM", api_win87em, 5 },
	{ "SHELL", api_shell, 28 },
	{ "VER", api_ver, 9 },
	{ "LZEXPAND", api_lzexpand, 10 },
	{ "SYSTEM", api_system, 11 },
	{ "SOUND", api_sound, 18 },
	{ 0, 0, 0 }
};
