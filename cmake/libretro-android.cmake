# ---- Injection Dtock : cible libretro Android (armeabi-v7a) ----
# Ajouté à la fin du CMakeLists.txt de Butterscotch par le workflow.
# La cible `butterscotch` est déjà SHARED en PLATFORM=android et inclut
# src/android/*.c (dont platformLog). On ajoute seulement l'entrée libretro.
if(PLATFORM STREQUAL "android")
    target_sources(butterscotch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/libretro/main.c)
    target_include_directories(butterscotch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/libretro)
    set_target_properties(butterscotch PROPERTIES
        OUTPUT_NAME "butterscotch_libretro_android"
        PREFIX "")
endif()
