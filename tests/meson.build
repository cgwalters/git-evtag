# Copyright 2022 Simon McVittie
# SPDX-License-Identifier: MIT

insttestdir = get_option('libexecdir') / 'installed-tests'
insttestmetadir = get_option('datadir') / 'installed-tests' / meson.project_name()

test_scripts = [
  'test-basic.sh',
]

test_env = environment()
test_env.prepend('PATH', project_build_root / 'src')
test_env.set('G_TEST_BUILDDIR', project_build_root)
test_env.set('G_TEST_SRCDIR', project_source_root)

foreach test_script : test_scripts
  test(
    test_script,
    files('tap-test'),
    args : [files(test_script)],
    env : test_env,
    protocol : 'tap',
  )

  if get_option('install_tests')
    configure_file(
      input : 'tap.test.in',
      output : test_script + '.test',
      configuration : {
        'basename' : test_script,
        'insttestdir' : get_option('prefix') / insttestdir,
      },
      install_dir : insttestmetadir,
    )
  endif
endforeach

if get_option('install_tests')
  install_data(
    'libtest.sh',
    install_dir : insttestdir,
  )
  install_data(
    test_scripts + [
      project_source_root / 'src/git-evtag-compute-py',
    ],
    install_dir : insttestdir,
    install_mode : 'rwxr-xr-x',
  )
endif

subdir('gpghome')
