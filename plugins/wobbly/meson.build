wobbly = shared_module('wobbly',
                       ['wobbly.cpp', 'wobbly.c'],
                       include_directories: [wayfire_api_inc, wayfire_conf_inc, plugins_common_inc],
                       dependencies: [wlroots, pixman, wfconfig],
                       install: true,
                       install_dir: join_paths(get_option('libdir'), 'wayfire'))

wobbly_inc = include_directories('.')
install_headers(['wayfire/plugins/wobbly/wobbly-signal.hpp'], subdir: 'wayfire/plugins/wobbly')
