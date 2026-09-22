#!/usr/bin/bash

set -euo pipefail

cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
