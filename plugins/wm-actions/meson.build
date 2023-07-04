wm_actions = shared_module('wm-actions',
                           ['wm-actions.cpp'],
                           include_directories: [wayfire_api_inc, wayfire_conf_inc],
                           dependencies: [wlroots, pixman, wfconfig, json],
                           install: true,
                           install_dir: join_paths(get_option('libdir'), 'wayfire'))
