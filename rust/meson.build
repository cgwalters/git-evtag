# Copyright 2022 Simon McVittie
# SPDX-License-Identifier: MIT

cargo = find_program('cargo', required : get_option('build_rust_version'))

if cargo.found()
  custom_target(
    'git-rustevtag',
    input : ['Cargo.toml', 'src/main.rs'],
    output : 'git-rustevtag',
    command : [
      'sh', '-euc',
      '"$1" build && cp target/debug/git-evtag "$2"',
      'sh',
      cargo,
      '@OUTPUT0@',
    ],
  )
endif
