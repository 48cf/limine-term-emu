project('limine-term-emu', 'c', default_options: ['c_std=c11'])

sdl2 = dependency('sdl2')
c_files = files(
    'terminal/backends/framebuffer.c',
    'terminal/term.c',
    'sdlterm.c',
)

executable('limine-term', c_files, dependencies: [sdl2])
