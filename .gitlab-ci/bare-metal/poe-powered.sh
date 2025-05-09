#!/bin/bash
# shellcheck disable=SC1091
# shellcheck disable=SC2034
# shellcheck disable=SC2059
# shellcheck disable=SC2086 # we want word splitting

. "$SCRIPTS_DIR"/setup-test-env.sh

# Boot script for devices attached to a PoE switch, using NFS for the root
# filesystem.

# We're run from the root of the repo, make a helper var for our paths
BM=$CI_PROJECT_DIR/install/bare-metal
CI_COMMON=$CI_PROJECT_DIR/install/common
CI_INSTALL=$CI_PROJECT_DIR/install

# Runner config checks
if [ -z "$BM_SERIAL" ]; then
  echo "Must set BM_SERIAL in your gitlab-runner config.toml [[runners]] environment"
  echo "This is the serial port to listen the device."
  exit 1
fi

if [ -z "$BM_POE_ADDRESS" ]; then
  echo "Must set BM_POE_ADDRESS in your gitlab-runner config.toml [[runners]] environment"
  echo "This is the PoE switch address to connect for powering up/down devices."
  exit 1
fi

if [ -z "$BM_POE_INTERFACE" ]; then
  echo "Must set BM_POE_INTERFACE in your gitlab-runner config.toml [[runners]] environment"
  echo "This is the PoE switch interface where the device is connected."
  exit 1
fi

if [ -z "$BM_POWERUP" ]; then
  echo "Must set BM_POWERUP in your gitlab-runner config.toml [[runners]] environment"
  echo "This is a shell script that should power up the device and begin its boot sequence."
  exit 1
fi

if [ -z "$BM_POWERDOWN" ]; then
  echo "Must set BM_POWERDOWN in your gitlab-runner config.toml [[runners]] environment"
  echo "This is a shell script that should power off the device."
  exit 1
fi

if [ ! -d /nfs ]; then
  echo "NFS rootfs directory needs to be mounted at /nfs by the gitlab runner"
  exit 1
fi

if [ ! -d /tftp ]; then
  echo "TFTP directory for this board needs to be mounted at /tftp by the gitlab runner"
  exit 1
fi

# job config checks
if [ -z "$BM_ROOTFS" ]; then
  echo "Must set BM_ROOTFS to your board's rootfs directory in the job's variables"
  exit 1
fi

if [ -z "$BM_BOOTFS" ] && { [ -z "$BM_KERNEL" ] || [ -z "$BM_DTB" ]; } ; then
  echo "Must set /boot files for the TFTP boot in the job's variables or set kernel and dtb"
  exit 1
fi

if [ -z "$BM_CMDLINE" ]; then
  echo "Must set BM_CMDLINE to your board's kernel command line arguments"
  exit 1
fi

section_start prepare_rootfs "Preparing rootfs components"

set -ex

date +'%F %T'

# Clear out any previous run's artifacts.
rm -rf results/
mkdir -p results

# Create the rootfs in the NFS directory.  rm to make sure it's in a pristine
# state, since it's volume-mounted on the host.
rsync -a --delete $BM_ROOTFS/ /nfs/

date +'%F %T'

# If BM_BOOTFS is an URL, download it
if echo $BM_BOOTFS | grep -q http; then
  curl -L --retry 4 -f --retry-all-errors --retry-delay 60 \
    "${FDO_HTTP_CACHE_URI:-}$BM_BOOTFS" -o /tmp/bootfs.tar
  BM_BOOTFS=/tmp/bootfs.tar
fi

date +'%F %T'

# If BM_BOOTFS is a file, assume it is a tarball and uncompress it
if [ -f "${BM_BOOTFS}" ]; then
  mkdir -p /tmp/bootfs
  tar xf $BM_BOOTFS -C /tmp/bootfs
  BM_BOOTFS=/tmp/bootfs
fi

date +'%F %T'

# Install kernel modules (it could be either in /lib/modules or
# /usr/lib/modules, but we want to install in the latter)
if [ -n "${BM_BOOTFS}" ]; then
  [ -d $BM_BOOTFS/usr/lib/modules ] && rsync -a $BM_BOOTFS/usr/lib/modules/ /nfs/usr/lib/modules/
  [ -d $BM_BOOTFS/lib/modules ] && rsync -a $BM_BOOTFS/lib/modules/ /nfs/lib/modules/
else
  echo "No modules!"
fi


date +'%F %T'

# Install kernel image + bootloader files
if [ -z "$BM_BOOTFS" ]; then
  mv "${BM_KERNEL}" "${BM_DTB}.dtb" /tftp/
else  # BM_BOOTFS
  rsync -aL --delete $BM_BOOTFS/boot/ /tftp/
fi

date +'%F %T'

# Create the rootfs in the NFS directory
. $BM/rootfs-setup.sh /nfs

date +'%F %T'

echo "$BM_CMDLINE" > /tftp/cmdline.txt

# Add some options in config.txt, if defined
if [ -n "$BM_BOOTCONFIG" ]; then
  printf "$BM_BOOTCONFIG" >> /tftp/config.txt
fi

section_end prepare_rootfs

set +e
STRUCTURED_LOG_FILE=results/job_detail.json
python3 $CI_INSTALL/custom_logger.py ${STRUCTURED_LOG_FILE} --update dut_job_type "${DEVICE_TYPE}"
python3 $CI_INSTALL/custom_logger.py ${STRUCTURED_LOG_FILE} --update farm "${FARM}"
ATTEMPTS=3
first_attempt=True
while [ $((ATTEMPTS--)) -gt 0 ]; do
  section_start dut_boot "Booting hardware device ..."
  python3 $CI_INSTALL/custom_logger.py ${STRUCTURED_LOG_FILE} --create-dut-job dut_name "${CI_RUNNER_DESCRIPTION}"
  # Update subtime time to CI_JOB_STARTED_AT only for the first run
  if [ "$first_attempt" = "True" ]; then
    python3 $CI_INSTALL/custom_logger.py ${STRUCTURED_LOG_FILE} --update-dut-time submit "${CI_JOB_STARTED_AT}"
  else
    python3 $CI_INSTALL/custom_logger.py ${STRUCTURED_LOG_FILE} --update-dut-time submit
  fi
  python3 $BM/poe_run.py \
          --dev="$BM_SERIAL" \
          --powerup="$BM_POWERUP" \
          --powerdown="$BM_POWERDOWN" \
          --boot-timeout-seconds ${BOOT_PHASE_TIMEOUT_SECONDS:-300} \
          --test-timeout-minutes ${TEST_PHASE_TIMEOUT_MINUTES:-$((CI_JOB_TIMEOUT/60 - ${TEST_SETUP_AND_UPLOAD_MARGIN_MINUTES:-5}))}
  ret=$?

  if [ $ret -eq 2 ]; then
    python3 $CI_INSTALL/custom_logger.py ${STRUCTURED_LOG_FILE} --close-dut-job
    first_attempt=False
    error "Device failed to boot; will retry"
  else
    # We're no longer in dut_boot by this point
    unset CURRENT_SECTION
    ATTEMPTS=0
  fi
done

section_start dut_cleanup "Cleaning up after job"
python3 $CI_INSTALL/custom_logger.py ${STRUCTURED_LOG_FILE} --close-dut-job
python3 $CI_INSTALL/custom_logger.py ${STRUCTURED_LOG_FILE} --close
set -e

date +'%F %T'

# Bring artifacts back from the NFS dir to the build dir where gitlab-runner
# will look for them.
cp -Rp /nfs/results/. results/

date +'%F %T'
section_end dut_cleanup

exit $ret
