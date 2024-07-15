subdir('common')

if get_option('custom_pch')
  cc = meson.get_compiler('cpp')

  pch_file = meson.current_source_dir() / 'pch/pch.hpp'
  api_headers = meson.project_source_root() / 'src/api'
  ipc_headers = meson.project_source_root() / 'plugins/ipc'
  proto_headers = meson.project_build_root() / 'proto/libwl_protos.a.p/'
  common_headers = meson.project_source_root() / 'plugins/common'
  wobbly_headers = meson.project_source_root() / 'plugins/wobbly'

  if cc.get_id() == 'clang'
    pch = custom_target('plugin_pch',
        input: pch_file,
        output: 'plugin_pch.hpp.pch',
        command: cc.cmd_array() + ['@INPUT@', '-c', '-o', '@OUTPUT@',
          '-std=c++17', '-pthread', '-fPIC',
          '-I' + api_headers,
          '-I' + ipc_headers,
          '-I/usr/include/pixman-1',
          '-I' + proto_headers,
          '-I' + common_headers,
          '-I' + wobbly_headers])

    plugin_pch_args = ['-include-pch', pch.full_path(), '-pthread']
    plugin_pch_dep = declare_dependency(sources: pch)
  elif cc.get_id() == 'gcc'
    error('TODO: CUSTOM PCH FOR GCC')
  else
    error('Unsupported compiler for custom pch: ' + cc.get_id())
  endif
else
  plugin_pch_args = []
  plugin_pch_dep = declare_dependency()
endif

subdir('ipc')
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
subdir('ipc-rules')
