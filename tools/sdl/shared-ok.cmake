# shared-ok.cmake -- simplesquirrel declare une cible SHARED qui ne sera jamais construite (EXCLUDE_FROM_ALL) :
# CMake la refuserait sur un systeme statique.
set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS TRUE)
# (OpenALConfig.cmake oublie de chercher Threads)
find_package(Threads)
