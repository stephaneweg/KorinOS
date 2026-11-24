# host-fix.cmake : inclus après project() par la configuration de Blender pour l'hôte (hosttools.sh,
# CMAKE_PROJECT_INCLUDE). La libtiff statique exporte une dépendance à CMath::CMath (sa libm), une cible que seul son
# propre FindCMath définit : on la déclare ici.
if(NOT TARGET CMath::CMath)
  add_library(CMath::CMath INTERFACE IMPORTED)
  set_target_properties(CMath::CMath PROPERTIES INTERFACE_LINK_LIBRARIES m)
endif()
