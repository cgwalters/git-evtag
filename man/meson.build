# Copyright 2022 Simon McVittie
# SPDX-License-Identifier: MIT

manpages_xsl = 'http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl'
xsltproc = find_program('xsltproc', required : get_option('man'))
build_man_page = false

if xsltproc.found()
  if run_command([
    xsltproc, '--nonet', manpages_xsl,
  ], check : false).returncode() == 0
    message('Docbook XSL found, man page enabled')
    build_man_page = true
  elif get_option('man').enabled()
    error('Man page requested, but Docbook XSL stylesheets not found')
  else
    message('Docbook XSL not found, man page disabled automatically')
  endif
endif

if build_man_page
  custom_target(
    'git-evtag.1',
    output : 'git-evtag.1',
    input : 'git-evtag.xml',
    command : [
      xsltproc,
      '--nonet',
      '--stringparam', 'man.output.quietly', '1',
      '--stringparam', 'funcsynopsis.style', 'ansi',
      '--stringparam', 'man.th.extra1.suppress', '1',
      '--stringparam', 'man.authors.section.enabled', '0',
      '--stringparam', 'man.copyright.section.enabled', '0',
      '-o', '@OUTPUT@',
      manpages_xsl,
      '@INPUT@',
    ],
    install : true,
    install_dir : get_option('mandir') / 'man1',
  )
endif
