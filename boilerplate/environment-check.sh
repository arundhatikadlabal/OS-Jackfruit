#!/bin/bash
# environment-check.sh - Preflight check for OS-Jackfruit container runtime

PASS=0
FAIL=0

check() {
    local desc="$1"
    local result="$2"
    if [ "$result" = "ok" ]; then
        echo "[  OK  ] $desc"
        PASS=$((PASS + 1))
    else
        echo "[ FAIL ] $desc -- $result"
        FAIL=$((FAIL + 1))
    fi
}

echo "========================================"
echo " OS-Jackfruit Environment Check"
echo "========================================"
echo ""

# Check running as root
if [ "$(id -u)" -eq 0 ]; then
    check "Running as root" "ok"
else
    check "Running as root" "please run with sudo"
fi

# Check OS
if grep -qi "ubuntu" /etc/os-release 2>/dev/null; then
    VERSION=$(grep VERSION_ID /etc/os-release | cut -d= -f2 | tr -d '"')
    check "Ubuntu detected (version $VERSION)" "ok"
else
    check "Ubuntu detected" "not Ubuntu -- may have issues"
fi

# Check kernel version
KVER=$(uname -r)
check "Kernel version: $KVER" "ok"

# Check kernel headers installed
if [ -d "/lib/modules/$(uname -r)/build" ]; then
    check "Kernel headers present" "ok"
else
    check "Kernel headers present" "missing -- run: apt install linux-headers-$(uname -r)"
fi

# Check gcc
if command -v gcc &>/dev/null; then
    GCCVER=$(gcc --version | head -1)
    check "gcc found: $GCCVER" "ok"
else
    check "gcc found" "missing -- run: apt install build-essential"
fi

# Check make
if command -v make &>/dev/null; then
    check "make found" "ok"
else
    check "make found" "missing -- run: apt install build-essential"
fi

# Check clone/namespaces support
if [ -f /proc/self/ns/pid ]; then
    check "PID namespaces supported" "ok"
else
    check "PID namespaces supported" "not available"
fi

if [ -f /proc/self/ns/uts ]; then
    check "UTS namespaces supported" "ok"
else
    check "UTS namespaces supported" "not available"
fi

if [ -f /proc/self/ns/mnt ]; then
    check "Mount namespaces supported" "ok"
else
    check "Mount namespaces supported" "not available"
fi

# Check /dev/container_monitor (optional)
if [ -e /dev/container_monitor ]; then
    check "/dev/container_monitor exists (module loaded)" "ok"
else
    echo "[ INFO ] /dev/container_monitor not found -- load monitor.ko before running"
fi

# Check unshare available
if command -v unshare &>/dev/null; then
    check "unshare utility found" "ok"
else
    check "unshare utility found" "missing"
fi

# Check chroot
if command -v chroot &>/dev/null; then
    check "chroot utility found" "ok"
else
    check "chroot utility found" "missing"
fi

# Check WSL (warn)
if grep -qi "microsoft" /proc/version 2>/dev/null; then
    echo "[ WARN ] Running inside WSL -- kernel module loading will NOT work"
else
    check "Not running inside WSL" "ok"
fi

# Check Secure Boot
if command -v mokutil &>/dev/null; then
    SB=$(mokutil --sb-state 2>/dev/null)
    if echo "$SB" | grep -qi "disabled"; then
        check "Secure Boot disabled" "ok"
    else
        echo "[ WARN ] Secure Boot may be enabled -- module loading may fail"
    fi
else
    echo "[ INFO ] mokutil not found -- verify Secure Boot is OFF in VM settings"
fi

echo ""
echo "========================================"
echo " Results: $PASS passed, $FAIL failed"
echo "========================================"

if [ "$FAIL" -eq 0 ]; then
    echo " Environment looks good. You can proceed."
    exit 0
else
    echo " Fix the above issues before building."
    exit 1
fi