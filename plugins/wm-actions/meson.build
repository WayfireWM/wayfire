wm_actions = shared_module('wm-actions',
                           ['wm-actions.cpp'],
                           include_directories: [wayfire_api_inc, wayfire_conf_inc, plugins_common_inc],
                           dependencies: [wlroots, pixman, wfconfig, json, plugin_pch_dep],
                           install: true,
                           install_dir: join_paths(get_option('libdir'), 'wayfire'))

install_headers( [ 'wm-actions-signals.hpp' ], subdir: 'wayfire/plugins' )
