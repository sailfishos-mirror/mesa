#!/bin/bash

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_TEST_VK_TAG
set -eux

# This script sets up the wine components and the wine prefix that will be used
# when running wine.  All of this gets bundled up into a tarball that's
# conditionally overlaid on LAVA runners when we need wine (container-based jobs
# will have the wine contents in them).  Everything that's included in the
# WINE_S3_ARTIFACT (other than the debian wine packages) must go in this script,
# because that determines the cached hash it's uploaded under for LAVA runners.

section_start wine "Setting up Wine"

# Do a very early check to make sure the tag is correct without the need of
# setting up the environment variables locally
ci_tag_build_time_check "WINE_TAG"

export WINEPREFIX="/wineprefix"
export WINEDEBUG="-all"

# We don't want crash dialogs
cat >crashdialog.reg <<EOF
Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Software\Wine\WineDbg]
"ShowCrashDialog"=dword:00000000

EOF

# Set the wine prefix and disable the crash dialog
wine regedit crashdialog.reg
rm crashdialog.reg

# An immediate wine command may fail with: "${WINEPREFIX}: Not a
# valid wine prefix."  and that is just spit because of checking
# the existance of the system.reg file, which fails.  Just giving
# it a bit more of time for it to be created solves the problem
# ...
while ! test -f  "${WINEPREFIX}/system.reg"; do sleep 1; done

section_end wine


# Archive and upload wine for use as a LAVA overlay, if the archive doesn't exist yet
WINE_S3_ARTIFACT="wine.tar.zst"
ARTIFACT_PATH="${DATA_STORAGE_PATH}/wine/${DEBIAN_TEST_VK_TAG}-${WINE_TAG}/${CI_JOB_NAME}/${WINE_S3_ARTIFACT}"
if FOUND_ARTIFACT_URL="$(find_s3_project_artifact "${ARTIFACT_PATH}")"; then
  echo "Found wine at: ${FOUND_ARTIFACT_URL}, skipping upload"
else
  echo "Uploaded wine not found, reuploading..."
  tar --zstd -cf "$WINE_S3_ARTIFACT" -C / \
    "${WINEPREFIX#/}" \
    /usr/lib/*/wine
  ci-fairy s3cp --token-file "${S3_JWT_FILE}" "$WINE_S3_ARTIFACT" \
    "https://${S3_BASE_PATH}/${CI_PROJECT_PATH}/${ARTIFACT_PATH}"
  rm "$WINE_S3_ARTIFACT"
fi
