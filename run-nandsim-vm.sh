#!/usr/bin/env bash

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

if [ ! -x "$BUSYBOX" ]; then
    echo "BusyBox missing!"
    exit 1
fi

# verify it's a real BusyBox binary
if ! "$BUSYBOX" --help 2>/dev/null | grep -q "BusyBox"; then
    echo "BusyBox present but not functional"
    exit 1
fi

# verify busybox is statically compiled
if ! file "$BUSYBOX" | grep -q "statically linked"; then
    echo "BusyBox is not statically compiled!"
    exit 1
fi

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
find_module_and_dependencies() {
    MODSRC="/lib/modules/$MODVER"
    MODDST="$ROOT/lib/modules/$MODVER"

    modprobe --show-depends $1 > /dev/null
    if [ $? -ne 0 ]; then
        echo "Error: failed to find $1!" >&2
        return 1
    fi

    mods=$(modprobe --show-depends $1 | awk '{print $2}')
    for m in $mods; do

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
}

echo "[*] Resolving nandsim dependency tree..."
find_module_and_dependencies "nandsim"

echo "[*] Adding VirtIO net driver if possible..."
find_module_and_dependencies "virtio_net"

# -----------------------------
# init script
# -----------------------------
cat > "$ROOT/init" <<EOF
#!/bin/sh

try_setup_network() {
    ip link set lo up

    modprobe virtio_net

    # Check if the exit status of the previous command is NOT 0
    if [ $? -ne 0 ]; then
        echo "Error: modprobe virtio_net failed!" >&2
        return 1
    fi

    ip link set eth0 up
    udhcpc -i eth0 -s /udhcpc.script
    telnetd -l /bin/sh

    echo "Network should be up now..."
}

mount -t devtmpfs devtmpfs /dev
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mkdir -p /dev/pts
mount -t devpts none /dev/pts

echo "[*] Running depmod..."
depmod -b /lib/modules $MODVER

echo "[*] Loading nandsim..."
modprobe nandsim first_id_byte=0x20 second_id_byte=0xaa third_id_byte=0x00 \
fourth_id_byte=0x15 pagesize=2048 oobpagesize=64 eraseblock_size=131072 || echo "nandsim failed"

echo "[*] Loading networking... (virtio-net, as best effort)"
try_setup_network


echo "[*] Ready"
echo "[*] Run poweroff -f to shutdown!"
exec /bin/sh
EOF

chmod +x "$ROOT/init"

cp -v $PWD/user/qemu_vm.udhcpc.script $ROOT/udhcpc.script
chmod +x "$ROOT/udhcpc.script"

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
    -m 4096 \
    -smp 4 \
    -kernel "$KERNEL" \
    -initrd "$WORK/initramfs.cpio.gz" \
    -append "console=ttyS0 rdinit=/init nokaslr" \
    -virtfs local,path="$PWD/build",mount_tag=build,security_model=none,id=buildfs \
    -nographic \
    -netdev user,id=net0,hostfwd=tcp::2222-:23 \
    -device virtio-net-pci,netdev=net0
