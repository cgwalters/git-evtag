#!/bin/sh
# Copyright 2022 Simon McVittie
# SPDX-License-Identifier: MIT

# Compatibility shim for installing symlinks with Meson < 0.61

set -eu

case "$2" in
  (/*)
    ln -fns "$3" "$DESTDIR/$2/$1"
    ;;
  (*)
    ln -fns "$3" "$MESON_INSTALL_DESTDIR_PREFIX/$2/$1"
    ;;
esac
