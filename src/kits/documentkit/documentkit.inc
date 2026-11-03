; documentkit.inc - constantes publiques de DocumentKit (documentkit.abi, docs/KITS.md)

; --- erreurs ------------------------------------------------------------------
DKE_NOTFOUND	= -2		; attribut, relation, caractere absent
DKE_BADF	= -9		; poignee inconnue
DKE_NOMEM	= -12		; plus de poignee (64 lecteurs ouverts par process)
DKE_INVAL	= -22
DKE_CORRUPT	= -74		; UTF-8 invalide

; --- dk_xml_next ---------------------------------------------------------------
DKX_EOF		= 0
DKX_START	= 1
DKX_END		= 2
DKX_TEXT	= 3

; --- unites (dk_len_*) ---------------------------------------------------------
DKU_EMU		= 0		; 1/914400 pouce
DKU_TWIP	= 1		; 1/20 point
DKU_HMM		= 2		; 1/100 mm
DKU_PT		= 3
DKU_IN		= 4
DKU_CM		= 5
DKU_MM		= 6
DKU_PC		= 7		; pica : 12 points
DKU_PX		= 8		; 1/96 pouce
DKU_COUNT	= 9

; --- dk_color_write ------------------------------------------------------------
DKC_HASH	= 1		; #RRGGBB
DKC_LOWER	= 2		; rrggbb
DKC_ARGB	= 4		; FFRRGGBB

; --- dk_csv_next ---------------------------------------------------------------
DKV_FIELD	= 1		; un champ
DKV_EOL		= 2		; le dernier de son enregistrement
DKV_QUOTED	= 4		; il etait entre guillemets (vide mais present)

; --- dk_doc_kind ---------------------------------------------------------------
DKK_UNKNOWN	= 0
DKK_DOCX	= 1
DKK_XLSX	= 2
DKK_PPTX	= 3
DKK_ODT		= 4
DKK_ODS		= 5
DKK_ODP		= 6
DKK_RTF		= 7
DKK_ZIP		= 8		; un ZIP d'aucun format connu (ou mimetype compresse : le lire, dk_doc_kind_mime)
DKK_OLE		= 9		; ancien format Office (.doc, .xls, .ppt) : non pris en charge

; --- dk_rel_get ----------------------------------------------------------------
DKR_ID		= 0
DKR_TYPE	= 1		; dernier composant du type (slideMaster)
DKR_TARGET	= 2		; brute : dk_part_path la resout
DKR_MODE	= 3		; External, ou vide

; --- dk_env_text ---------------------------------------------------------------
DKS_XML_DECL	= 0		; <?xml ... standalone="yes"?> et saut de ligne
DKS_RELS_OPEN	= 1		; <Relationships xmlns=...>
DKS_RELS_CLOSE	= 2
DKS_TYPES_OPEN	= 3		; <Types xmlns=...> et les Default de rels et xml
DKS_TYPES_CLOSE	= 4
DKS_MANIFEST_OPEN = 5		; declaration, <manifest:manifest xmlns:manifest=... manifest:version="1.3">
DKS_MANIFEST_CLOSE = 6
DKS_COUNT	= 7
