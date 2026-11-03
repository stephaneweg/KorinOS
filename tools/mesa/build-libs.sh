#!/bin/sh
# build-libs : les bibliotheques de Mesa pour ARMPI. Meson ne les construit que pour une cible finale :
# on les nomme une a une, et l'edition de liens se fait ici (build-armgl.sh), en statique.
#     sh tools/mesa/build-libs.sh
set -eu
. "$(dirname "$0")/env.sh"
cd "$OUT/build"
ninja -k 0 -j "$JOBS" src/mesa/libmesa.a src/gallium/auxiliary/libgallium.a src/compiler/libcompiler.a \
	src/compiler/nir/libnir.a src/compiler/glsl/libglsl.a src/compiler/glsl/libglsl_util.a \
	src/compiler/glsl/glcpp/libglcpp.a src/compiler/spirv/libvtn.a src/util/libmesa_util.a src/util/libmesa_util_simd.a \
	src/util/libxmlconfig.a src/util/libparson.a src/util/blake3/libblake3.a src/c11/impl/libmesa_util_c11.a \
	src/broadcom/cle/libbroadcom_cle.a src/broadcom/compiler/libbroadcom_compiler.a \
	src/broadcom/qpu/libbroadcom_qpu.a src/broadcom/libbroadcom_v3d.a src/broadcom/libbroadcom-v42.a \
	src/broadcom/libbroadcom-v71.a src/broadcom/libv3d_neon.a src/broadcom/perfcntrs/libbroadcom_perfcntrs.a \
	src/broadcom/perfcntrs/libv3d-perfcntrs-v42.a src/broadcom/perfcntrs/libv3d-perfcntrs-v71.a \
	src/gallium/drivers/v3d/libv3d.a src/gallium/drivers/v3d/libv3d-v42.a src/gallium/drivers/v3d/libv3d-v71.a \
	src/mesa/glapi/shared-glapi/libglapi.a src/mesa/glapi/glapi/libglapi_bridge.a \
	src/gallium/auxiliary/libgalliumvl_stub.a
