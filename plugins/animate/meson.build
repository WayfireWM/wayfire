dependencies = [wlroots, pixman, wfconfig]
animate_pch_deps = [plugin_pch_dep]

if get_option('enable_openmp')
   dependencies += [dependency('openmp')]
   # PCH does not have openmp enabled
   animate_pch_deps = []
endif

animiate = shared_module('animate',
                         ['animate.cpp',
                          'fire/particle.cpp',
                          'fire/fire.cpp'],
                         include_directories: [wayfire_api_inc, wayfire_conf_inc],
                         dependencies: dependencies + animate_pch_deps,
                         install: true,
                         install_dir: join_paths(get_option('libdir'), 'wayfire'))

install_headers(['animate.hpp'], subdir: 'wayfire/plugins/animate')
