# Copyright 2022 Simon McVittie
# SPDX-License-Identifier: MIT

project(
  'git-evtag',
  'c',
  version : '2022.1',
  meson_version : '>=0.50.0',
  default_options : [
    'warning_level=2',
  ],
)

cc = meson.get_compiler('c')
add_project_arguments('-D_GNU_SOURCE', language : 'c')
common_include_directories = include_directories('.')

add_project_arguments(
  cc.get_supported_arguments([
    '-Werror=aggregate-return',
    '-Werror=declaration-after-statement',
    '-Werror=format-security',
    '-Werror=format=2',
    '-Werror=implicit-function-declaration',
    '-Werror=init-self',
    '-Werror=missing-include-dirs',
    '-Werror=missing-prototypes',
    '-Werror=pointer-arith',

    '-Wstrict-prototypes',
  ]),
  language : 'c',
)

foreach no_warning : ['missing-field-initializers', 'unused-parameter']
  add_project_arguments(
    cc.get_supported_arguments([
      '-Wno-' + no_warning,
      '-Wno-error=' + no_warning,
    ]),
    language : 'c',
  )
endforeach

# These can be replaced with meson.project_*_root() with a Meson 0.56
# dependency
project_build_root = meson.current_build_dir()
project_source_root = meson.current_source_dir()

glib_dep = dependency('gio-2.0', required : true)
libgit_glib_dep = dependency('libgit2', required : true)

cdata = configuration_data()
cdata.set_quoted(
  'DATADIR',
  get_option('prefix') / get_option('datadir'),
)
cdata.set_quoted(
  'LIBEXECDIR',
  get_option('prefix') / get_option('libexecdir'),
)
cdata.set_quoted(
  'LOCALEDIR',
  get_option('prefix') / get_option('datadir') / 'locale',
)
cdata.set_quoted(
  'PACKAGE_STRING',
  '@0@ @1@'.format(meson.project_name(), meson.project_version()),
)
cdata.set_quoted(
  'DEFAULT_EDITOR',
  get_option('default_editor'),
)

foreach function : ['git_libgit2_init']
  if cc.has_function(
    function,
    dependencies : libgit_glib_dep,
    prefix : '#include <git2.h>',
  )
    cdata.set('HAVE_' + function.to_upper().underscorify(), 1)
  endif
endforeach

configure_file(
  output : 'config.h',
  configuration : cdata,
)

install_symlinks = []

subdir('src')
subdir('man')
subdir('rust')
subdir('tests')

foreach symlink : install_symlinks
  if meson.version().version_compare('>=0.61.0') and not symlink['pointing_to'].startswith('/')
    install_symlink(
      symlink['link_name'],
      install_dir : symlink['install_dir'],
      pointing_to : symlink['pointing_to'],
    )
  else
    meson.add_install_script(
      'packaging/meson-compat-install-symlink.sh',
      symlink['link_name'],
      symlink['install_dir'],
      symlink['pointing_to'],
    )
  endif
endforeach
