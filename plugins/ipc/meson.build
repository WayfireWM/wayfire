evdev = dependency('libevdev')

ipc_include_dirs = include_directories('.')

ipc = shared_module('ipc',
    ['ipc.cpp'],
    include_directories: [wayfire_api_inc, wayfire_conf_inc, plugins_common_inc],
    dependencies: [wlroots, pixman, wfconfig, wftouch, json, evdev, plugin_pch_dep],
    cpp_args: plugin_pch_args,
    install: true,
    install_dir: conf_data.get('PLUGIN_PATH'))

stipc = shared_module('stipc',
    ['stipc.cpp'],
    include_directories: [wayfire_api_inc, wayfire_conf_inc, plugins_common_inc],
    dependencies: [wlroots, pixman, wfconfig, wftouch, json, evdev, plugin_pch_dep],
    cpp_args: plugin_pch_args,
    install: true,
    install_dir: conf_data.get('PLUGIN_PATH'))

demoipc = shared_module('demo-ipc',
    ['demo-ipc.cpp'],
    include_directories: [wayfire_api_inc, wayfire_conf_inc, plugins_common_inc],
    dependencies: [wlroots, pixman, wfconfig, wftouch, json, evdev, plugin_pch_dep],
    cpp_args: plugin_pch_args,
    install: true,
    install_dir: conf_data.get('PLUGIN_PATH'))

install_headers(['ipc-method-repository.hpp', 'ipc-helpers.hpp', 'ipc-activator.hpp'], subdir: 'wayfire/plugins/ipc')
