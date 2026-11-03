# filekit.bi -- FileKit of Onyx for BASIC, as far as ARMPI's runtime makes it (src/apps/basic/baskits.h).
kit filekit 7
file_size 0 l s fk_file_size path
free 1 v p fk_free p
human_size 2 v ipi fk_human_size bytes,out,cap
load 3 i sLI fk_load path,out,out_n
remove 4 i s fk_remove path
tree_size 5 l sII fk_tree_size path,files,folders
copy 6 i sscp fk_copy src,dst,cb,user
