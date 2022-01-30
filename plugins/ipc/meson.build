json = subproject('json').get_variable('nlohmann_json_dep')
evdev = dependency('libevdev')


ipc = shared_module('ipc',
    ['ipc.cpp', 'stipc.cpp'],
    include_directories: [wayfire_api_inc, plugins_common_inc],
    dependencies: [wlroots, pixman, wfconfig, wftouch, json, evdev],
    install: true,
    install_dir: conf_data.get('PLUGIN_PATH'))
