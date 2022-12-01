evdev = dependency('libevdev')
system_json = dependency('nlohmann_json', required: false)

if system_json.found()
  json = system_json
else
  json = subproject('json').get_variable('nlohmann_json_dep')
endif

ipc = shared_module('ipc',
    ['ipc.cpp', 'stipc.cpp'],
    include_directories: [wayfire_api_inc, wayfire_conf_inc, plugins_common_inc],
    dependencies: [wlroots, pixman, wfconfig, wftouch, json, evdev],
    install: true,
    install_dir: conf_data.get('PLUGIN_PATH'))
