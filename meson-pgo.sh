#!/bin/sh

# Requirements:
#
# * all: build directory must not exist; it is created by the script
# * all: LC_CTYPE must be set to an UTF-8 locale
# * full*: must have at least one font installed
# * full-headless-sway: Sway 1.6.2 must be installed


set -e

usage_and_die() {
    echo "Usage: ${0} none|partial|full|full-headless-sway|[auto] <source-dir> <build-dir>"
    exit 1
}

[ ${#} -ge 3 ] || usage_and_die

mode=${1}
source_dir=$(realpath "${2}")
build_dir=$(realpath "${3}")
shift 3

# meson will complain if source dir is invalid

if [ -d "${build_dir}" ]; then
    echo "${build_dir}: build directory already exists"
    exit 1
fi

compiler=other
do_pgo=no

CFLAGS="${CFLAGS} -O3"

case $(${CC-cc} --version) in
    *GCC*)
        compiler=gcc
        do_pgo=yes
        ;;

    *clang*)
        compiler=clang

        if command -v llvm-profdata > /dev/null; then
            do_pgo=yes
            CFLAGS="${CFLAGS} -Wno-ignored-optimization-argument"
        fi
        ;;
esac

case ${mode} in
    partial|full|full-headless-sway)
    ;;

    none)
        do_pgo=no
        ;;

    auto)
        if command -v sway > /dev/null; then
            mode=full-headless-sway
        elif [ -n "${WAYLAND_DISPLAY+x}" ]; then
            mode=full
        else
            mode=partial
        fi
        ;;

    *)
        usage_and_die
        ;;
esac

# echo "source: ${source_dir}"
# echo "build: ${build_dir}"
# echo "compiler: ${compiler}"
# echo "mode: ${mode}"

export CFLAGS
meson "${@}" "${build_dir}"

if [ ${do_pgo} = yes ]; then
    find . -name "*.gcda" -delete
    meson configure "${build_dir}" -Db_pgo=generate
    ninja -C "${build_dir}"

    # If fcft/tllist are subprojects, we need to ensure their tests
    # have been executed, or we’ll get “profile count data file not
    # found” errors.
    ninja -C "${build_dir}" test

    script_options="--scroll --scroll-region --colors-regular --colors-bright --colors-256 --colors-rgb --attr-bold --attr-italic --attr-underline --sixel"

    tmp_file=$(mktemp)
    pwd=$(pwd)

    cleanup() {
        rm -f "${tmp_file}"
        cd "${pwd}"
    }
    trap cleanup EXIT INT HUP TERM

    cd "${build_dir}"
    case ${mode} in
        full)
            ./footclient --version
            ./foot \
             --config=/dev/null \
             --term=xterm \
             sh -c "${source_dir}/scripts/generate-alt-random-writes.py ${script_options} ${tmp_file} && cat ${tmp_file}"
            ;;

        full-headless-sway)
            ./footclient --version

            runtime_dir=$(mktemp -d)
            sway_conf=$(mktemp)

            echo "exec ${build_dir}/foot -o tweak.render-timer=log --config=/dev/null --term=xterm sh -c \"${source_dir}/scripts/generate-alt-random-writes.py ${script_options} ${tmp_file} && cat ${tmp_file}\" && swaymsg exit" > "${sway_conf}"
            export XDG_RUNTIME_DIR=${runtime_dir}
            export WLR_BACKENDS=headless

            sway -c "${sway_conf}"

            rm "${sway_conf}"
            rm -rf "${runtime_dir}"
            ;;

        partial)
            ./footclient --version
            ./foot --version
            "${source_dir}"/scripts/generate-alt-random-writes.py \
                --rows=67 \
                --cols=135 \
                ${script_options} \
                "${tmp_file}"
            ./pgo "${tmp_file}"
            ;;
    esac

    cd "${pwd}"
    rm "${tmp_file}"

    if [ ${compiler} = clang ]; then
        llvm-profdata \
            merge \
            "${build_dir}"/default_*.profraw \
            --output="${build_dir}"/default.profdata
    fi

    meson configure "${build_dir}" -Db_pgo=use
fi
