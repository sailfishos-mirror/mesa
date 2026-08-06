#!/usr/bin/env bash
# This Fossilize tool verifies that compilers don't rely on Vulkan enabled
# features that aren't recorded by Fossilize. This is important to make sure
# that precompiled shaders via Fossilize enable the same level of features. To
# achieve that, it verifies that the global pipeline key
# (VK_KHR_pipeline_binary) is invariant.

set -ex

INSTALL=$(realpath -s "$PWD"/install)
ARCH=$(uname -m)

export VK_DRIVER_FILES="$INSTALL"/share/vulkan/icd.d/"$VK_DRIVER"_icd."$ARCH".json

export LD_PRELOAD=$PWD/install/lib/libamdgpu_noop_drm_shim.so

# TODO: Get a list of available drm-shim GPUs instead of hardcoding this.
families=("PITCAIRN"
          "BONAIRE"
          "POLARIS10"
          "VEGA10"
          "NAVI10"
          "NAVI21"
          "VANGOGH"
          "NAVI31"
          "NAVI33"
          "STRIX1"
          "GFX1201")
for family in "${families[@]}"
do
    export AMDGPU_GPU_ID=$family
    fossilize-validate-cache-roundtrip --pipeline-binary-key
done
