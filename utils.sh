#!/bin/bash
set -e

export ALEXA_PATH=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
cd "$ALEXA_PATH"

export PLATFORM="esp32s3"
export FLASH_BAUD=2000000
export CONSOLE_BAUD=2000000

# Reuse willow:latest - has IDF 5.3.3 + ADF patches already applied
export DOCKER_IMAGE="willow:latest"
export DOCKER_NAME="alexa-build"

# Resolve real path of esp-adf (may be a symlink)
export ADF_REAL_PATH=$(realpath "$ALEXA_PATH/deps/esp-adf")
export ADF_PATH="$ADF_REAL_PATH"
export DIST_FILE="build/alexa-dist.bin"

if [ -f /.dockerenv ]; then
    export container="docker"
fi

check_port() {
    if [ ! $PORT ]; then
        echo "You need to define the PORT environment variable to do serial stuff - exiting"
        exit 1
    fi

    if [ ! -c $PORT ]; then
        echo "Cannot find configured port $PORT - exiting"
        exit 1
    fi

    if [ ! -w "$PORT" ]; then
        echo "You don't have permission to write to $PORT - exiting"
        echo "You need to either run this command with sudo or add yourself to the dialout group"
        exit 1
    fi
}

check_tio() {
    if ! command -v tio &> /dev/null; then
        echo "tio could not be found in path - you need to install it"
        echo "More information: https://github.com/tio/tio"
        exit 1
    fi
}

check_host() {
    if [ ! "$container" ]; then
        return
    fi

    echo "You need to run this command from the host - you are in the container"
    exit 1
}

do_term() {
    tio -b "$CONSOLE_BAUD" "$PORT"
}

mkdir -p flags

check_flag() {
    FLAG="$1"
    if [ ! -r flags/"$FLAG" ]; then
        echo "You need to run $FLAG first"
        exit 1
    fi
}

add_flag() {
    FLAG="$1"
    date > flags/"$FLAG"
}

remove_flag() {
    FLAG="$1"
    rm -f flags/"$FLAG"
}

usage() {
    echo "Usage: $0 <command>"
    echo ""
    echo "Commands:"
    echo "  setup       Initialize sdkconfig (run once before first config/build)"
    echo "  build       Build the firmware"
    echo "  config      Open menuconfig"
    echo "  clean       Clean build directory"
    echo "  docker      Enter Docker build container"
    echo "  flash       Flash full firmware to device (set PORT env var)"
    echo "  flash-app   Flash only app binary - faster, for code-only changes"
    echo "  monitor     Monitor serial output (set PORT env var)"
}

docker_run() {
    docker run -it --rm \
        -v "$ALEXA_PATH":/alexa \
        -v "$ADF_REAL_PATH":/esp-adf \
        -e ADF_PATH=/esp-adf \
        -w /alexa \
        --name "$DOCKER_NAME" \
        "$DOCKER_IMAGE" \
        bash
}

docker_setup() {
    docker run -it --rm \
        -v "$ALEXA_PATH":/alexa \
        -v "$ADF_REAL_PATH":/esp-adf \
        -e ADF_PATH=/esp-adf \
        -w /alexa \
        --name "$DOCKER_NAME" \
        "$DOCKER_IMAGE" \
        bash -c "./utils.sh setup"
}

docker_build() {
    docker run -it --rm \
        -v "$ALEXA_PATH":/alexa \
        -v "$ADF_REAL_PATH":/esp-adf \
        -e ADF_PATH=/esp-adf \
        -w /alexa \
        --name "$DOCKER_NAME" \
        "$DOCKER_IMAGE" \
        bash -c "./utils.sh build"
}

docker_config() {
    docker run -it --rm \
        -v "$ALEXA_PATH":/alexa \
        -v "$ADF_REAL_PATH":/esp-adf \
        -e ADF_PATH=/esp-adf \
        -w /alexa \
        --name "$DOCKER_NAME" \
        "$DOCKER_IMAGE" \
        bash -c "./utils.sh config"
}

build() {
    if [ -z "$container" ]; then
        docker_build
        return
    fi
    export ADF_PATH=/esp-adf
    idf.py build
}

config() {
    if [ -z "$container" ]; then
        docker_config
        return
    fi
    export ADF_PATH=/esp-adf
    idf.py menuconfig
}

case "$1" in
    setup)
        if [ -z "$container" ]; then
            docker_setup
        else
            export ADF_PATH=/esp-adf
            echo "Setting target to esp32s3..."
            idf.py set-target esp32s3
            echo "Setup done. You can now run: ./utils.sh config or ./utils.sh build"
        fi
        ;;
    build)
        build
        ;;
    config)
        config
        ;;
    clean)
        idf.py fullclean
        ;;
    docker)
        docker_run
        ;;
    flash)
        check_host
        check_port
        check_tio
        check_flag "erase-flash"
        cd "$ALEXA_PATH"/build
        esptool --chip "$PLATFORM" -p "$PORT" -b "$FLASH_BAUD" --before default-reset --after hard-reset write-flash \
            @flash_args
        do_term
        ;;
    flash-app)
        check_host
        check_port
        check_tio
        check_flag "erase-flash"
        esptool --chip "$PLATFORM" -p "$PORT" -b "$FLASH_BAUD" --before default-reset --after hard-reset write-flash \
            0x2d000 "$ALEXA_PATH/build/ota_data_initial.bin" \
            0x30000 "$ALEXA_PATH/build/alexa.bin"
        do_term
        ;;
    erase-flash)
        check_host
        check_port
        esptool.py --chip "$PLATFORM" -p "$PORT" erase_flash
        echo "Flash erased. You will need to reflash."
        add_flag "erase-flash"
        ;;
    monitor)
        check_host
        check_port
        check_tio
        do_term
        ;;
    *)
        usage
        ;;
esac
