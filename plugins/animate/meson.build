dependencies = [wlroots, pixman, wfconfig]

if get_option('enable_openmp')
   dependencies += [dependency('openmp')]
endif

animiate = shared_module('animate',
                         ['animate.cpp',
                          'fire/particle.cpp',
                          'fire/fire.cpp'],
                         include_directories: [wayfire_api_inc, wayfire_conf_inc],
                         dependencies: dependencies,
                         install: true,
                         install_dir: join_paths(get_option('libdir'), 'wayfire'))

install_headers(['animate.hpp'], subdir: 'wayfire/plugins/animate')
