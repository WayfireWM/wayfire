if get_option('custom_pch')
  # Add a workaround for https://github.com/mesonbuild/meson/issues/4350
  # We create a PCH ourselves and manage it for all plugins.
  pch_file = meson.current_source_dir() / 'pch/plugin_pch.hpp'
  custom_pch_flags = ['@INPUT@', '-c', '-o', '@OUTPUT@', '-MD', '-MF', '@DEPFILE@',
      '-std=' + get_option('cpp_std'), '-pthread', '-fPIC'] + get_option('cpp_args')

  # Meson enables this unconditionally on everything not-macos and non-msvc
  custom_pch_flags += ['-D_FILE_OFFSET_BITS=64']

  # Gather flags from the compiler ...
  if get_option('b_ndebug') == 'false' or (get_option('b_ndebug') == 'if-release' and
      get_option('buildtype') != 'release')
    custom_pch_flags += ['-D_GLIBCXX_ASSERTIONS=1']
  else
    custom_pch_flags += ['-DNDEBUG']
  endif

  custom_pch_flags += ['-O' + get_option('optimization')]
  if get_option('debug')
    custom_pch_flags += ['-g']
  endif

  if has_asan
    custom_pch_flags += ['-fsanitize=address']
  endif

  pch = custom_target('plugin_pch',
      input: pch_file,
      output: 'plugin_pch.hpp.gch',
      depfile: 'plugin_pch.hpp.d',
      command: cpp.cmd_array() + custom_pch_flags)

  fs = import('fs')
  if cpp.get_id() == 'clang'
    # In clang, everything is simple, we just tell the compiler which PCH file to use
    plugin_pch_arg = ['-include-pch', pch.full_path(), '-pthread']
  elif cpp.get_id() == 'gcc'
    # GCC requires that the .gch and the .hpp file are in the same dir.
    fs.copyfile(pch_file) # copy to build dir where .gch is found
    plugin_pch_arg = ['-I' + fs.parent(pch.full_path()), '-include', fs.name(pch_file), '-pthread', '-fpch-preprocess']
  else
    error('Unsupported compiler for custom pch: ' + cpp.get_id())
  endif

  plugin_pch_dep = declare_dependency(sources: pch, compile_args: plugin_pch_arg)
else
  plugin_pch_args = []
  plugin_pch_dep = declare_dependency()
endif

wobbly_inc = include_directories('wobbly/')
subdir('common')

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
