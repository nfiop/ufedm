#!/usr/bin/env bash
set -euo pipefail

KERNEL="${KERNEL:-/boot/vmlinuz-$(uname -r)}"
MODVER="$(uname -r)"
BUSYBOX="${BUSYBOX:-$(command -v busybox)}"

WORK="$(mktemp -d)"
ROOT="$WORK/root"

cleanup() {
    rm -rf "$WORK"
}
trap cleanup EXIT

echo "[*] Building minimal nandsim initramfs"

mkdir -p \
    "$ROOT"/{bin,sbin,etc,proc,sys,dev,tmp,root,host} \
    "$ROOT/lib/modules/$MODVER"

if [[ ! -d "$PWD/build" ]]; then
    echo "[!] build/ directory missing on host"
    exit 1
fi

if [[ -z "$(ls -A "$PWD/build")" ]]; then
    echo "[!] build/ directory is empty"
    exit 1
fi

echo "[*] Copying build/ into initramfs..."
cp -a "$PWD/build" "$ROOT/build"

# -----------------------------
# BusyBox
# -----------------------------
cp "$BUSYBOX" "$ROOT/bin/busybox"

(
cd "$ROOT/bin"
for b in sh mount mkdir ls cat insmod modprobe dmesg echo sleep uname; do
    ln -sf busybox "$b"
done
)

# -----------------------------
# Resolve dependency closure
# (correct: real file list from modprobe)
# -----------------------------
echo "[*] Resolving nandsim dependency tree..."

mods=$(modprobe --show-depends nandsim | awk '{print $2}')

MODSRC="/lib/modules/$MODVER"
MODDST="$ROOT/lib/modules/$MODVER"

for m in $mods; do
    # # normalize name → path
    # m="${m%.zst}"
    # m="${m%.xz}"
    # m="${m##*/}"

    path=$(modinfo -n "$m" 2>/dev/null || true)

    if [[ -z "$path" ]]; then
        echo "[!] missing module: $m"
        continue
    fi

    rel="${path#$MODSRC/}"
    out="$MODDST/$rel"

    mkdir -p "$(dirname "$out")"

    case "$path" in
        *.zst)
            echo "[+] decompress $rel"
            unzstd -c "$path" > "${out%.zst}"
            ;;
        *.xz)
            echo "[+] decompress $rel"
            xz -dc "$path" > "${out%.xz}"
            ;;
        *)
            cp "$path" "$out"
            ;;
    esac
done

# -----------------------------
# init script
# -----------------------------
cat > "$ROOT/init" <<EOF
#!/bin/sh

mount -t devtmpfs devtmpfs /dev
mount -t proc proc /proc
mount -t sysfs sysfs /sys

echo "[*] Running depmod..."
depmod -b /lib/modules $MODVER

echo "[*] Loading nandsim..."
modprobe nandsim first_id_byte=0x20 second_id_byte=0xaa third_id_byte=0x00 \
fourth_id_byte=0x15 pagesize=2048 oobpagesize=64 eraseblock_size=131072 || echo "nandsim failed"

echo "[*] Ready"
echo "[*] Run poweroff -f to shutdown!"
exec /bin/sh
EOF

chmod +x "$ROOT/init"

# -----------------------------
# build initramfs
# -----------------------------
echo "[*] packing initramfs..."
(
cd "$ROOT"
find . -print0 \
| cpio --null -o --format=newc 2>/dev/null \
| gzip -9 > "$WORK/initramfs.cpio.gz"
)

# -----------------------------
# boot QEMU
# -----------------------------
echo "[*] booting..."

exec qemu-system-x86_64 \
    -enable-kvm \
    -cpu host \
    -m 2048 \
    -smp 2 \
    -kernel "$KERNEL" \
    -initrd "$WORK/initramfs.cpio.gz" \
    -append "console=ttyS0 rdinit=/init nokaslr" \
    -virtfs local,path="$PWD/build",mount_tag=build,security_model=none,id=buildfs \
    -nographic
