project('limine-term-emu', 'c', default_options: ['c_std=c11'])

sdl2 = dependency('sdl2')
c_files = files(
    'terminal/backends/framebuffer.c',
    'terminal/term.c',
    'sdlterm.c',
)

pkgdatadir = join_paths(get_option('datadir'), 'limine-term-emu')

install_subdir('assets', install_dir: join_paths([pkgdatadir]))

conf = configuration_data()

conf.set_quoted('SOURCE_ROOT', meson.source_root())
conf.set_quoted('PACKAGE_DATADIR', join_paths(get_option('prefix'), pkgdatadir))

configure_file(
  output: 'config.h',
  configuration: conf
)

executable('limine-term', c_files, dependencies: [sdl2], install: true)
