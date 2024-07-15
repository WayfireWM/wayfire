blur_base = shared_library('wayfire-blur-base',
     ['blur-base.cpp', 'box.cpp', 'gaussian.cpp', 'kawase.cpp', 'bokeh.cpp'],
     include_directories: [wayfire_api_inc, wayfire_conf_inc],
     dependencies: [wlroots, pixman, wfconfig, plugin_pch_dep],
     override_options: ['b_lundef=false'],
     install: true)
install_headers(['blur.hpp'], subdir: 'wayfire/plugins/blur')

blur = shared_module('blur', ['blur.cpp'],
     link_with: blur_base,
     include_directories: [wayfire_api_inc, wayfire_conf_inc],
     dependencies: [wlroots, pixman, wfconfig, plugin_pch_dep],
     install: true, install_dir: join_paths(get_option('libdir'), 'wayfire'))
