protocol_plugins = [
  'foreign-toplevel',
]

all_include_dirs = [wayfire_api_inc, wayfire_conf_inc, plugins_common_inc]
all_deps = [wlroots, pixman, wfconfig]

foreach plugin : protocol_plugins
  shared_module(plugin, plugin + '.cpp',
      include_directories: all_include_dirs,
      dependencies: all_deps,
      install: true,
      install_dir: conf_data.get('PLUGIN_PATH'))
endforeach

