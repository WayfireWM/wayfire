subdir('common')

system_json = dependency('nlohmann_json', required: false)
if system_json.found()
  json = system_json
else
  json = subproject('json').get_variable('nlohmann_json_dep')
endif

if get_option('debug_ipc')
  subdir('ipc')
endif

subdir('protocols')
subdir('vswitch')
subdir('wobbly')
subdir('grid')
subdir('decor')
subdir('animate')
subdir('cube')
subdir('window-rules')
subdir('blur')
subdir('tile')
subdir('wm-actions')
subdir('scale')
subdir('single_plugins')
