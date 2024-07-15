all_include_dirs = [wayfire_api_inc, wayfire_conf_inc, plugins_common_inc]
all_deps = [wlroots, pixman, wfconfig, wftouch, json, plugin_pch_dep]

shared_module('ipc-rules', ['ipc-rules.cpp'],
        include_directories: all_include_dirs,
        dependencies: all_deps,
        install: true,
        install_dir: conf_data.get('PLUGIN_PATH'))

install_headers(['ipc-rules-common.hpp'], subdir: 'wayfire/plugins/ipc')
