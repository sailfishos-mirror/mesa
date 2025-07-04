#!/usr/bin/env bash
# shellcheck disable=SC2048
# shellcheck disable=SC2086 # we want word splitting
# shellcheck disable=SC2155 # mktemp usually not failing

shopt -s expand_aliases

function _x_store_state {
    if [[ "$-" == *"x"* ]]; then
      previous_state_x=1
    else
      previous_state_x=0
    fi
}
_x_store_state
alias x_store_state='{ _x_store_state; } >/dev/null 2>/dev/null'

function _x_off {
    x_store_state
    set +x
}
alias x_off='{ _x_off; } >/dev/null 2>/dev/null'

function _x_restore {
  [ $previous_state_x -eq 0 ] || set -x
}
alias x_restore='{ _x_restore; } >/dev/null 2>/dev/null'

function _error_msg() (
    x_off
    RED="\e[0;31m"
    ENDCOLOR="\e[0m"
    echo -e "${RED}$*${ENDCOLOR}"
)

# Some date binaries (like the BusyBox one) use -D instead of -d for date parsing.
# This fallback handles both cases: try GNU coreutils-style first, then fallback to BusyBox if it fails.
# The BusyBox fallback needs '|| true' because it returns an error status even when it outputs the time.
export JOB_START_S=$(
  date -u +"%s" -d "$CI_JOB_STARTED_AT" 2>/dev/null ||
  { date -u +"%s" -D "%Y-%m-%dT%H:%M:%SZ" "$CI_JOB_STARTED_AT" 2>/dev/null || true; }
)

function get_current_minsec {
    DATE_S=$(date -u +"%s")
    CURR_TIME=$((DATE_S-JOB_START_S))
    printf "%02d:%02d" $((CURR_TIME/60)) $((CURR_TIME%60))
}

function _build_section_start {
    local section_params=$1
    shift
    local section_name=$1
    CURRENT_SECTION=$section_name
    shift
    CYAN="\e[0;36m"
    ENDCOLOR="\e[0m"

    CURR_MINSEC=$(get_current_minsec)
    echo -e "\n\e[0Ksection_start:$(date +%s):$section_name$section_params\r\e[0K${CYAN}[${CURR_MINSEC}] $*${ENDCOLOR}\n"
    x_restore
}
alias build_section_start="x_off; _build_section_start"

function _section_start {
    build_section_start "[collapsed=true]" $*
    x_restore
}
alias section_start="x_off; _section_start"

function _uncollapsed_section_start {
    build_section_start "" $*
    x_restore
}
alias uncollapsed_section_start="x_off; _uncollapsed_section_start"

function _build_section_end {
    echo -e "\e[0Ksection_end:$(date +%s):$1\r\e[0K"
    CURRENT_SECTION=""
    x_restore
}
alias build_section_end="x_off; _build_section_end"

function _section_end {
    build_section_end $*
    x_restore
}
alias section_end="x_off; _section_end"

function _section_switch {
    if [ -n "${CURRENT_SECTION:-}" ]
    then
        build_section_end $CURRENT_SECTION
        x_off
    fi
    build_section_start "[collapsed=true]" $*
    x_restore
}
alias section_switch="x_off; _section_switch"

function _uncollapsed_section_switch {
    if [ -n "${CURRENT_SECTION:-}" ]
    then
        build_section_end $CURRENT_SECTION
        x_off
    fi
    build_section_start "" $*
    x_restore
}
alias uncollapsed_section_switch="x_off; _uncollapsed_section_switch"

export -f _x_store_state
export -f _x_off
export -f _x_restore
export -f get_current_minsec
export -f _build_section_start
export -f _section_start
export -f _build_section_end
export -f _section_end
export -f _section_switch
export -f _uncollapsed_section_switch
export -f _error_msg

# Freedesktop requirement (needed for Wayland)
[ -n "${XDG_RUNTIME_DIR:-}" ] || export XDG_RUNTIME_DIR="$(mktemp --tmpdir -d xdg-runtime-XXXXXX)"

if [ -z "${RESULTS_DIR:-}" ]; then
	export RESULTS_DIR="${PWD%/}/results"
	if [ -e "${RESULTS_DIR}" ]; then
		rm -rf "${RESULTS_DIR}"
	fi
	mkdir -p "${RESULTS_DIR}"
fi

function error {
    # we force the following to be not in a section
    if [ -n "${CURRENT_SECTION:-}" ]; then
      section_end $CURRENT_SECTION
      x_off
    fi

    CURR_MINSEC=$(get_current_minsec)
    echo -e "\n"
    _error_msg "[$CURR_MINSEC] ERROR: $*"
    x_restore
}

function trap_err {
    x_off
    error ${CURRENT_SECTION:-'unknown-section'}: ret code: $*
}

# ------ Structured tagging
export _CI_TAG_CHECK_DIR="/mesa-ci-build-tag"

_ci_tag_from_name_to_var() {
    # Transforms MY_COMPONENT_TAG to my-component
    echo "${1%_TAG}" | tr '[:upper:]' '[:lower:]' | tr '_' '-'
}

_ci_tag_check() (
    x_off
    _declared_name="${1}"
    declare -n _declared="${_declared_name}"
    _calculated="${2}"
    local component_lower=$(_ci_tag_from_name_to_var "${_declared_name}")

    if [ -z "${_declared:-}" ]; then
        # Close the section
        error "Fatal error"
        _error_msg "Structured tag is not set: ${_declared_name}"
        _error_msg ""
        echo "If you are adding a new component, please run:"
        echo "bin/ci/update_tag.py --include ${component_lower}"
        echo "This will automatically update the YAML file for you."
        echo "Or manually edit .gitlab-ci/conditional-build-image-tags.yml to add the new"
        echo "component."
        error ""
        exit 2
    fi

    if [ "${_declared}" != "${_calculated}" ]; then
        # Close the section
        error "Fatal error"
        _error_msg "Mismatch in declared and calculated tags:"
        _error_msg "    ${_declared_name} from the YAML is \"${_declared}\""
        _error_msg "    ... but I think it should be \"${_calculated}\""
        _error_msg ""
        echo "Usually this happens when you change what you want to be built without also"
        echo "changing the YAML declaration. For example, you've bumped SKQP to version"
        echo "1.2.3, but you still have 'SKQP_VER: 1.2.2' in"
        echo ".gitlab-ci/conditional-build-image-tags.yml."
        echo ""
        echo "If you meant to change the component I'm talking about, please change the"
        echo "tag and resubmit. You can also run:"
        echo "bin/ci/update_tag.py --include ${component_lower}"
        echo "to update the tag automatically."
        echo ""
        echo "If you didn't mean to change the component, please ping @mesa/ci-helpers and we"
        echo "can help you figure out what's gone wrong."
        echo ""
        echo "But for now, I've got to fail this build. Sorry."
        exit 2
    fi
    x_restore
)

_ci_tag_check_build() {
    x_off
    if [ -n "${NEW_TAG_DRY_RUN:-}" ]; then
        echo "${2}"
        exit 0
    fi

    _ci_tag_check "${1}" "${2}"

    if [ -n "${CI_NOT_BUILDING_ANYTHING:-}" ]; then
        exit 0
    fi
    x_restore
}

get_tag_file() {
    x_off
    # If no tag name is provided, return the directory
    echo "${_CI_TAG_CHECK_DIR}/${1:-}"
    x_restore
}

_ci_tag_write() (
    set +x
    local tag_name="${1}"
    local tag_value="${2}"

    mkdir -p "${_CI_TAG_CHECK_DIR}"
    echo -n "${tag_value}" > "$(get_tag_file "${tag_name}")"
)

_ci_calculate_tag() {
    x_off
    # the args are files that can affect the build output
    mapfile -t extra_files < <(printf '%s\n' "$@")
    (
        for extra_file in "${extra_files[@]}"; do
            if [ ! -f "${extra_file}" ]; then
                error "File '${extra_file}' does not exist"
                exit 1
            fi
            cat "${extra_file}"
        done
    ) | md5sum | cut -d' ' -f1
    x_restore
}

ci_tag_build_time_check() {
    # Get the caller script and hash its contents plus the extra files
    x_off
    local tag_name="${1}"
    local build_script_file="build-$(_ci_tag_from_name_to_var "${tag_name}").sh"
    local build_script=".gitlab-ci/container/${build_script_file}"
    shift
    # now $@ has the extra files
    local calculated_tag=$(_ci_calculate_tag "${build_script}" "$@")

    _ci_tag_check_build "${tag_name}" "${calculated_tag}"
    _ci_tag_write "${tag_name}" "${calculated_tag}"
    x_restore
}

ci_tag_test_time_check() {
    x_off
    local tag_file=$(get_tag_file "${1}")
    if [ ! -f "${tag_file}" ]; then
        _error_msg "Structured tag file ${tag_file} does not exist"
        _error_msg "Please run the ci_tag_build_time_check first and rebuild the image/rootfs"
        exit 2
    fi
    _ci_tag_check "${1}" "$(cat "${tag_file}")"
    x_restore
}

# Export all functions
export -f _ci_calculate_tag
export -f _ci_tag_check
export -f _ci_tag_check_build
export -f _ci_tag_from_name_to_var
export -f _ci_tag_write
export -f ci_tag_build_time_check
export -f ci_tag_test_time_check
export -f get_tag_file

# Structured tagging ------

curl-with-retry() {
    curl --fail --location --retry-connrefused --retry 4 --retry-delay 15 "$@"
}
export -f curl-with-retry

function find_s3_project_artifact() {
    x_off
    local artifact_path="$1"

    for project in "${FDO_UPSTREAM_REPO}" "${CI_PROJECT_PATH}"; do
        local full_path="${FDO_HTTP_CACHE_URI:-}${S3_BASE_PATH}/${project}/${artifact_path}"
        if curl-with-retry -s --head "https://${full_path}" >/dev/null; then
            echo "https://${full_path}"
            x_restore
            return 0
        fi
    done

    x_restore
    return 1
}
export -f find_s3_project_artifact

export -f error
export -f trap_err

function filter_env_vars() {
    x_off
    if [[ -n "${S3_JWT:-}" ]]; then
        echo >&2 "Fatal: S3_JWT is set. This should have been cleared at this point."
        return 1
    fi

    local exclude_vars=(
        # GitLab tokens/passwords
        CI_JOB_TOKEN
        CI_DEPLOY_USER
        CI_DEPLOY_PASSWORD
        CI_DEPENDENCY_PROXY_PASSWORD
        CI_REGISTRY_PASSWORD
        CI_REPOSITORY_URL

        # Too long and unnecessary GitLab variables
        CI_COMMIT_DESCRIPTION
        CI_COMMIT_MESSAGE
        CI_MERGE_REQUEST_DESCRIPTION

        # Shell-managed variables
        _
        HOME
        HOSTNAME
        OLDPWD
        PATH
        PWD
        TERM
        XDG_RUNTIME_DIR
    )

    env -0 | sort -z | while IFS= read -r -d '' line; do
        [[ "$line" == *=* ]] || continue
        local varname="${line%%=*}"

        # Skip certain Mesa-specific variables
        if echo "$varname" | grep -qxE "$CI_EXCLUDE_ENV_VAR_REGEX"; then
            echo >&2 "${FUNCNAME[0]}: $varname is not passed to the DUT as it matches the pattern in CI_EXCLUDE_ENV_VAR_REGEX"
            continue
        fi
        # Skip excluded or invalid names
        if printf '%s\n' "${exclude_vars[@]}" | grep -qxF "$varname"; then
            echo >&2 "${FUNCNAME[0]}: $varname is not passed to the DUT as it is a variable listed for exclusion in ${FUNCNAME[0]}"
            continue
        fi
        # Skip shell function exports
        if [[ "$varname" == BASH_FUNC_* ]] || [[ ! "$varname" =~ ^[a-zA-Z_][a-zA-Z0-9_]*$ ]]; then
            continue
        fi

        echo "${!varname@A}"
    done
    x_restore
}
export -f filter_env_vars

set -E
trap 'trap_err $?' ERR
