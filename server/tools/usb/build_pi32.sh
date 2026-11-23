#!/usr/bin/env bash
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends python3-dev python3-venv python3-cryptography python3-cffi python3-yaml python3-setuptools python3-wheel git ca-certificates build-essential pkg-config libudev-dev autoconf automake libtool binutils zlib1g-dev
python3 -m venv --system-site-packages /tmp/picobrew-build-env
source /tmp/picobrew-build-env/bin/activate
# The upstream wheel set omits armv7. Compile the same wrapper and the
# exact libusb submodule recorded by its v1.0.30.1 release for this architecture.
git clone --quiet https://github.com/pyocd/libusb-package /tmp/picobrew-libusb
git -C /tmp/picobrew-libusb checkout --quiet 09711f4056efd55f5591ecfad1826c83b411c7c2
git -C /tmp/picobrew-libusb -c 'url.https://github.com/.insteadOf=git@github.com:' submodule update --init
test "$(git -C /tmp/picobrew-libusb/src/libusb rev-parse HEAD)" = 87a55632db62c9bdc58cd31d3ccfa673f1bb017f
python -m pip install /tmp/picobrew-libusb
python -m pip install PyInstaller==6.22.3 -r firmware/common/requirements-usb.txt -r firmware/pico-s-pro/modified/requirements-usb.txt
python -m unittest discover -s server/tools/usb -p 'test_*.py'
python server/tools/usb/build_worker.py --output artifacts
