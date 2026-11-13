#!/usr/bin/env bash
# Shared argument parser; sourced by build.sh and run_tests.sh.

normalize_bool() {
    case "${1^^}" in
        ON|TRUE|YES|1) printf 'ON\n' ;;
        OFF|FALSE|NO|0) printf 'OFF\n' ;;
        *) echo "Expected a boolean value, got: $1" >&2; return 2 ;;
    esac
}

parse_build_options() {
    local mode=$1 default_release=$2 default_testing=$3
    shift 3
    RELEASE=$(normalize_bool "${RELEASE:-$default_release}") || return 2
    ENABLE_TESTING=$(normalize_bool "${ENABLE_TESTING:-$default_testing}") || return 2
    ENABLE_ASAN=$(normalize_bool "${ENABLE_ASAN:-OFF}") || return 2
    ENABLE_TSAN=$(normalize_bool "${ENABLE_TSAN:-OFF}") || return 2
    CTEST_OPTIONS=()
    local argument assignment key value ctest_only=OFF
    for argument in "$@"; do
        if [[ "$mode" == tests && "$argument" == -- && "$ctest_only" == OFF ]]; then
            ctest_only=ON
            continue
        fi
        if [[ "$ctest_only" == ON ]]; then
            CTEST_OPTIONS+=("$argument")
            continue
        fi
        assignment=${argument#-D}
        if [[ "$assignment" == *=* ]]; then
            key=${assignment%%=*}
            value=${assignment#*=}
            case "$key" in
                RELEASE|ENABLE_TESTING|ENABLE_ASAN|ENABLE_TSAN)
                    value=$(normalize_bool "$value") || return 2
                    printf -v "$key" '%s' "$value"
                    ;;
                *) echo "Unknown build option: $key" >&2; return 2 ;;
            esac
        elif [[ "$argument" == --help || "$argument" == -h ]]; then
            echo 'Options: [-D]RELEASE=ON|OFF [-D]ENABLE_TESTING=ON|OFF'
            echo '         [-D]ENABLE_ASAN=ON|OFF [-D]ENABLE_TSAN=ON|OFF'
            echo 'Options can also be passed as environment variables.'
            echo "Defaults: RELEASE=$default_release, ENABLE_TESTING=$default_testing, ENABLE_ASAN=OFF, ENABLE_TSAN=OFF"
            echo 'ASan and TSan require separate builds. run_tests.sh also accepts CTest arguments.'
            exit 0
        elif [[ "$mode" == tests ]]; then
            CTEST_OPTIONS+=("$argument")
        else
            echo "Unknown argument: $argument" >&2
            return 2
        fi
    done
    if [[ "$ENABLE_ASAN" == ON && "$ENABLE_TSAN" == ON ]]; then
        echo 'ENABLE_ASAN and ENABLE_TSAN are mutually exclusive' >&2
        return 2
    fi
    if [[ "$mode" == tests && "$ENABLE_TESTING" == OFF ]]; then
        echo 'run_tests.sh requires ENABLE_TESTING=ON; use build.sh to build without tests' >&2
        return 2
    fi
    CMAKE_OPTIONS=(
        "-DRELEASE=$RELEASE"
        "-DENABLE_TESTING=$ENABLE_TESTING"
        "-DENABLE_ASAN=$ENABLE_ASAN"
        "-DENABLE_TSAN=$ENABLE_TSAN"
    )
}
