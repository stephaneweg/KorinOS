//
// markdown.h -- le Markdown du lecteur d'aide : le texte coupe en blocs (titres, paragraphes, listes, citations, code,
// tableaux, images, traits), puis chaque texte en morceaux de style (gras, italique, code, liens). Pas de dessin ici :
// main.cpp met les blocs en page avec UIKit.
//
// Ce qui est lu : # a ###### ; listes « - », « * », « + », « 1. » (imbriquees de deux espaces) ; > citation ;
// ``` et ~~~ (code, la langue apres est ignoree) ; | tableaux | (deuxieme ligne de tirets : en-tete) ; ![texte](image)
// seul sur sa ligne ; --- ; **gras**, *italique*, `code`, [lien](cible), <https://...>, \ devant un signe ; deux
// espaces ou \ en fin de ligne : retour a la ligne.
//
#ifndef HELP_MARKDOWN_H
#define HELP_MARKDOWN_H

#include <string>
#include <vector>

namespace md {

enum Kind { PARA, HEAD, CODE, QUOTE, ITEM, HR, IMAGE, TABLE };

struct Block
{
	int kind = PARA;
	int level = 0;					// titre : 1..6 ; element de liste : profondeur 0..
	int indent = 0;					// paragraphe ou code dans une liste : son retrait (profondeur + 1)
	bool ordered = false;				// element numerote
	int number = 0;					// son numero
	std::string text;				// texte en Markdown (titre, paragraphe, element, citation, texte d'image)
	std::string code;				// code : ses lignes ; image : sa cible
	std::vector<std::vector<std::string>> rows;	// tableau : ses cellules (texte en Markdown)
	bool header = false;				// tableau : la premiere ligne est un en-tete
};

// styles d'un morceau de texte
enum { S_BOLD = 1, S_ITALIC = 2, S_CODE = 4, S_LINK = 8, S_BREAK = 16 };

struct Run
{
	std::string text;
	int style = 0;
	int link = -1;					// numero dans la liste des liens du texte
};

// le texte d'un fichier -> ses blocs
std::vector<Block> parse (const std::string &src);
// le texte d'un bloc -> ses morceaux ; les cibles des liens s'ajoutent a links
std::vector<Run> inlines (const std::string &s, std::vector<std::string> &links);
// le texte sans ses marques (titres de la table des matieres, recherche)
std::string plain (const std::string &s);
// l'ancre d'un titre, comme GitHub : minuscules, espaces -> « - », ponctuation retiree
std::string slug (const std::string &s);

}

#endif
