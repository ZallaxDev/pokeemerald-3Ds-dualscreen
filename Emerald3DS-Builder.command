#!/bin/bash
set -e
cd "$(dirname "$0")"
if ! ./tools/macos.sh builder "$@"; then
    echo "Consulta docs/MACOS.md. Pulsa Intro para cerrar."
    read -r reply
    exit 1
fi
