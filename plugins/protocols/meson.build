protocol_plugins = [
  'foreign-toplevel', 'gtk-shell', 'wayfire-shell', 'xdg-activation', 'shortcuts-inhibit',
  'input-method-v1', 'session-lock'
]

all_include_dirs = [wayfire_api_inc, wayfire_conf_inc, plugins_common_inc, ipc_include_dirs]
all_deps = [wlroots, pixman, wfconfig, wf_protos, json, cairo, pango, pangocairo, plugin_pch_dep]

foreach plugin : protocol_plugins
  shared_module(plugin, plugin + '.cpp',
      include_directories: all_include_dirs,
      dependencies: all_deps,
      install: true,
      install_dir: conf_data.get('PLUGIN_PATH'))
endforeach

install_headers(['input-method-v1.hpp'], subdir: 'wayfire/plugins/input-method-v1/')
