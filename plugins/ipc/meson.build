evdev = dependency('libevdev')
system_json = dependency('nlohmann_json', required: false)

ipc_include_dirs = include_directories('.')

if system_json.found()
  json = system_json
else
  json = subproject('json').get_variable('nlohmann_json_dep')
endif

ipc = shared_module('ipc',
    ['ipc.cpp'],
    include_directories: [wayfire_api_inc, wayfire_conf_inc, plugins_common_inc],
    dependencies: [wlroots, pixman, wfconfig, wftouch, json, evdev],
    install: true,
    install_dir: conf_data.get('PLUGIN_PATH'))

stipc = shared_module('stipc',
    ['stipc.cpp'],
    include_directories: [wayfire_api_inc, wayfire_conf_inc, plugins_common_inc],
    dependencies: [wlroots, pixman, wfconfig, wftouch, json, evdev],
    install: true,
    install_dir: conf_data.get('PLUGIN_PATH'))

demoipc = shared_module('demo-ipc',
    ['demo-ipc.cpp'],
    include_directories: [wayfire_api_inc, wayfire_conf_inc, plugins_common_inc],
    dependencies: [wlroots, pixman, wfconfig, wftouch, json, evdev],
    install: true,
    install_dir: conf_data.get('PLUGIN_PATH'))

install_headers(['ipc-method-repository.hpp', 'ipc.hpp', 'ipc-helpers.hpp', 'ipc-activator.hpp'], subdir: 'wayfire/plugins/ipc')
