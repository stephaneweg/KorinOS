---
name: armpi-3ds
description: "n3dsemu : coeur 3DS d'Onyx sur EmuKit, ARM11 recompile ici, PICA200 sur Mesa ; dans main, deploye sur le Pi (3/11), titre de Zelda a 36 images/s sur le GPU, app en fenetre pas encore lancee"
metadata:
  node_type: memory
  type: project
  originSessionId: 69ca27f9-8ea5-4b8f-873e-170734524b97
  modified: 2026-11-02T22:02:48.106Z
---

Dans main depuis le 3/11/2026 (6529bf5), rien de pousse. Doc : docs/EMULATEURS.md, section 3DS. `src/apps/n3dsemu` : coeur C++ d'Onyx, `n3ds_arm.cpp` + `n3ds_jit.cpp` (ARM11, tick compte avant l'instruction), `n3ds_gl.cpp` + `n3ds_glsl.cpp` (GLSL par configuration), `n3ds_kern.S`. EmuKit a gagne `emu_rom_by_path` (ROM de plusieurs Go lue a la demande).

Prouve : Zelda ALBW et Mario 3D Land, 600 images, identiques a Dynarmic pas a pas, interprete et recompilateur. Rendu GL contre logiciel par llvmpipe (dump + `n3ds_replay`). Sur le Pi par le banc `run_n3ds` sans fenetre : GPU sans erreur, titre de Zelda 26 a 33 ms par image. Un vrai niveau n'est pas mesure.

**Why:** l'utilisateur a demande fusion et deploiement ; ne rien ecrire sur SD1: pendant les essais (`--nosave`). L'app deployee (SD0:/apps/n3dsemu.app, emukit.so, gamelib) attend un redemarrage du Pi : le noyau garde un UIKit ancien, `kit absent ou trop ancien : uikit`, nesemu pareil.

**How to apply:** banc dans WSL : `tools/emu/build_n3ds_bench.sh`, `check_n3ds.sh`, N3DS_TRACE_RANGE dans les deux pour la premiere instruction qui differe. Les ROM : `C:\Temp\ARMPI\SD1\Roms` (pas C:\Temp\Roms), Ocarina 3D chiffree. Piege : Dynarmic laisse la FPCR de l'invite dans les SVC. GLSL : un tableau `u[021]` est octal. Voir [[armpi-mesa]], [[armpi-emulateurs]].
