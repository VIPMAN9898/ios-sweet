#!/bin/bash
# A5000 reuses the Sweet open-source ARM64 assembly and pmap configuration.
# Run before patch application; the later Sweet config patches update the
# symlink target, and XNU's config tool reads the same completed feature list.
set -euo pipefail

[[ "${MACHINE_CONFIG:-}" == A5000 ]] || exit 0
[[ -f config/MASTER.arm64.sweet ]] || {
    echo 'Missing SWEET master config required by A5000' >&2
    exit 1
}
if [[ -e config/MASTER.arm64.a5000 || -L config/MASTER.arm64.a5000 ]]; then
    echo 'Refusing to replace existing A5000 master config' >&2
    exit 1
fi
ln -s MASTER.arm64.sweet config/MASTER.arm64.a5000
echo 'A5000 uses the patched SWEET open-source ARM64 master config'
