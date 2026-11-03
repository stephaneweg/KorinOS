; printd.inc - protocole du service print (src/sys/printd, printd.h en est la copie en C++), interne a PrinterKit.
; Message de 512 octets (WM_*) ; le resultat dans WM_OP, les reponses dans les WM_A.. et WM_DATA.
;   PD_NEW      -> id ; donnees : chemin du fichier des pages
;   PD_SUBMIT   a id, b copies, c mono | qualite << 8, d pages ; donnees : imprimante 64, titre 80, papier 64, sortie 256 -> 1 / 0
;   PD_ABORT    a id
;   PD_SETUP    a orientation, b copies, c qualite, d mono ; donnees : imprimante 64, papier 64 -> 1 / 0 : papier inconnu
;               reponse : a copies, b qualite, c mono ; donnees : imprimante, papier, papier l, h, marges l, h, d, b (32 bits)
;   PD_PRINTER  a rang -> 1 ; donnees : PrintPrinter
;   PD_MEDIA    a rang ; donnees : imprimante -> 1 ; donnees : nom 64, libelle 40
;   PD_ADD      donnees : nom 64, adresse 160 -> 1 / 0 ; donnees : texte
;   PD_REMOVE, PD_DEFAULT  donnees : nom -> 1 / 0
;   PD_REFRESH  donnees : nom -> 1 / 0 ; donnees : texte
;   PD_SCAN     -> nombre (la liste reste chez le service)
;   PD_FOUND    a rang -> 1 ; donnees : PrintFound
;   PD_JOB      a rang -> 1 ; donnees : PrintJobInfo
;   PD_CANCEL   a id -> 1 / 0
;   PD_FORGET
;   PD_FONT     a style ; donnees : famille -> cle (< 0 : aucune) ; a style a fabriquer ; donnees : chemin
;   PD_MEASURE  a cle, b taille ; donnees : texte -> avance
;   PD_METRICS  a cle, b taille -> 1 ; a ascendante, b descendante, c interligne
PD_NEW		= 1
PD_SUBMIT	= 2
PD_ABORT	= 3
PD_SETUP	= 4
PD_PRINTER	= 5
PD_MEDIA	= 6
PD_ADD		= 7
PD_REMOVE	= 8
PD_DEFAULT	= 9
PD_REFRESH	= 10
PD_SCAN		= 11
PD_FOUND	= 12
PD_JOB		= 13
PD_CANCEL	= 14
PD_FORGET	= 15
PD_FONT		= 16
PD_MEASURE	= 17
PD_METRICS	= 18
