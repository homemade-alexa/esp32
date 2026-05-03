#!/bin/bash
set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
PROJECT_DIR="$SCRIPT_DIR/.."

python3 "$PROJECT_DIR/deps/esp-adf/esp-idf/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py" \
    generate "$SCRIPT_DIR/nvs.csv" "$PROJECT_DIR/build/alexa_nvs.bin" 0x24000

esptool --chip esp32s3 -p "$PORT" erase-region 0x9000 0x24000
esptool --chip esp32s3 -p "$PORT" write-flash 0x9000 "$PROJECT_DIR/build/alexa_nvs.bin"
