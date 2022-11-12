# Copyright 2022 Simon McVittie
# SPDX-License-Identifier: MIT

if get_option('install_tests')
  install_data(
    'key1.asc',
    'key2.asc',
    'key3.asc',
    'secring.gpg',
    'trustdb.gpg',
    install_dir : insttestdir / 'gpghome',
  )

  install_symlinks += {
    'link_name' : 'pubring.gpg',
    'install_dir' : insttestdir / 'gpghome',
    'pointing_to' : 'trusted/pubring.gpg',
  }
endif

subdir('trusted')
