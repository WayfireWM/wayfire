wobbly_c_model = static_library('wobbly-c-model', ['wobbly.c'], install: false)

wobbly = shared_module('wobbly',
                       ['wobbly.cpp'],
                       include_directories: [wayfire_api_inc, wayfire_conf_inc, plugins_common_inc],
                       dependencies: [wlroots, pixman, wfconfig, plugin_pch_dep],
                       link_with: wobbly_c_model,
                       install: true,
                       install_dir: join_paths(get_option('libdir'), 'wayfire'))

install_headers(['wayfire/plugins/wobbly/wobbly-signal.hpp'], subdir: 'wayfire/plugins/wobbly')
