#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting
# shellcheck disable=SC1091 # paths only become valid at runtime

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# ALPINE_X86_64_LAVA_TRIGGER_TAG

# If we run in the fork (not from mesa or Marge-bot), reuse mainline kernel and rootfs, if exist.
_check_artifact_path() {
	_url="https://${1}/${2}"
	if curl -s -o /dev/null -I -L -f --retry 4 --retry-delay 15 "${_url}"; then
		echo -n "${_url}"
	fi
}

get_path_to_artifact() {
	_mainline_artifact="$(_check_artifact_path ${BASE_SYSTEM_MAINLINE_HOST_PATH} ${1})"
	if [ -n "${_mainline_artifact}" ]; then
		echo -n "${_mainline_artifact}"
		return
	fi
	_fork_artifact="$(_check_artifact_path ${BASE_SYSTEM_FORK_HOST_PATH} ${1})"
	if [ -n "${_fork_artifact}" ]; then
		echo -n "${_fork_artifact}"
		return
	fi
	set +x
	error "Sorry, I couldn't find a viable built path for ${1} in either mainline or a fork." >&2
	echo "" >&2
	echo "If you're working on CI, this probably means that you're missing a dependency:" >&2
	echo "this job ran ahead of the job which was supposed to upload that artifact." >&2
	echo "" >&2
	echo "If you aren't working on CI, please ping @mesa/ci-helpers to see if we can help." >&2
	echo "" >&2
	echo "This job is going to fail, because I can't find the resources I need. Sorry." >&2
	set -x
	exit 1
}

. "${SCRIPTS_DIR}/setup-test-env.sh"

section_start prepare_rootfs "Preparing root filesystem"

set -ex

ROOTFS_URL="$(get_path_to_artifact lava-rootfs.tar.zst)"
[ $? != 1 ] || exit 1

rm -rf results
mkdir results

filter_env_vars > dut-env-vars.sh
# Set SCRIPTS_DIR to point to the Mesa install we download for the DUT
echo "export SCRIPTS_DIR='$CI_PROJECT_DIR/install'" >> dut-env-vars.sh

# Prepare env vars for upload.
section_switch variables "Environment variables passed through to device:"
cat dut-env-vars.sh

section_switch lava_submit "Submitting job for scheduling"

touch results/lava.log
tail -f results/lava.log &
# Ensure that we are printing the commands that are being executed,
# making it easier to debug the job in case it fails.
set -x

# List of optional overlays
LAVA_EXTRA_OVERLAYS=()
if [ -n "${LAVA_FIRMWARE:-}" ]; then
    for fw in $LAVA_FIRMWARE; do
        LAVA_EXTRA_OVERLAYS+=(
            - append-overlay
              --name=linux-firmware
              --url="https://${BASE_SYSTEM_HOST_PREFIX}/${FIRMWARE_REPO}/${fw}-${FIRMWARE_TAG}.tar"
              --path="/"
              --format=tar
        )
    done
fi
if [ -n "${HWCI_KERNEL_MODULES:-}" ]; then
	LAVA_EXTRA_OVERLAYS+=(
		- append-overlay
		  --name=kernel-modules
		  --url="${KERNEL_IMAGE_BASE}/${DEBIAN_ARCH}/modules.tar"
		  --path="/"
		  --format=tar
	)
fi

PYTHONPATH=/ /lava/lava_job_submitter.py \
	--farm "${FARM}" \
	--device-type "${DEVICE_TYPE}" \
	--boot-method "${BOOT_METHOD}" \
	--job-timeout-min $((CI_JOB_TIMEOUT/60 - 5)) \
	--dump-yaml \
	--pipeline-info "$CI_JOB_NAME: $CI_PIPELINE_URL on $CI_COMMIT_REF_NAME ${CI_NODE_INDEX}/${CI_NODE_TOTAL}" \
	--rootfs-url "${ROOTFS_URL}" \
	--kernel-url-prefix "${KERNEL_IMAGE_BASE}/${DEBIAN_ARCH}" \
	--dtb-filename "${DTB}" \
	--first-stage-init /lava/init-stage1.sh \
	--env-file dut-env-vars.sh \
	--jwt-file "${S3_JWT_FILE}" \
	--kernel-image-name "${KERNEL_IMAGE_NAME}" \
	--kernel-image-type "${KERNEL_IMAGE_TYPE}" \
	--visibility-group "${VISIBILITY_GROUP}" \
	--lava-tags "${LAVA_TAGS}" \
	--mesa-job-name "$CI_JOB_NAME" \
	--structured-log-file "results/lava_job_detail.json" \
	--ssh-client-image "${LAVA_SSH_CLIENT_IMAGE}" \
	--project-dir "${CI_PROJECT_DIR}" \
	--project-name "${CI_PROJECT_NAME}" \
	--starting-section "${CURRENT_SECTION}" \
	--job-submitted-at "${CI_JOB_STARTED_AT}" \
	- append-overlay \
		--name=mesa-build \
		--url="https://${PIPELINE_ARTIFACTS_BASE}/${S3_ARTIFACT_NAME:?}.tar.zst" \
		--compression=zstd \
		--path="${CI_PROJECT_DIR}" \
		--format=tar \
	"${LAVA_EXTRA_OVERLAYS[@]}" \
	- submit \
	>> results/lava.log
