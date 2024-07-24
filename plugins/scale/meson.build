all_include_dirs = [wayfire_api_inc, wayfire_conf_inc, plugins_common_inc, vswitch_inc, wobbly_inc, include_directories('.')]
all_deps = [wlroots, pixman, wfconfig, wftouch, cairo, pango, pangocairo, json, plugin_pch_dep]

shared_module('scale', ['scale.cpp', 'scale-title-overlay.cpp'],
        include_directories: all_include_dirs,
        dependencies: all_deps,
        link_with: [move_drag_interface],
        install: true,
        install_dir: conf_data.get('PLUGIN_PATH'))

shared_module('scale-title-filter', 'scale-title-filter.cpp',
        include_directories: all_include_dirs,
        dependencies: all_deps,
        install: true,
        install_dir: conf_data.get('PLUGIN_PATH'))

install_headers(['wayfire/plugins/scale-signal.hpp'], subdir: 'wayfire/plugins')
