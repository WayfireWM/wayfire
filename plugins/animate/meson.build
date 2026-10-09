dependencies = [wlroots, pixman, wfconfig]
animate_pch_deps = [plugin_pch_dep]

if get_option('enable_openmp')
   dependencies += [dependency('openmp')]
   # PCH does not have openmp enabled
   animate_pch_deps = []
endif

animate_shaders = []
if get_option('vulkan_effects') and wlroots_features['vulkan_renderer'] and vulkan.found()
  dependencies += vulkan
  animate_shaders += [
    vulkan_shader_headers.get('fire.vert'),
    vulkan_shader_headers.get('fire.frag'),
    vulkan_shader_headers.get('squeezimize.vert'),
    vulkan_shader_headers.get('squeezimize.frag'),
    vulkan_shader_headers.get('squeezimize-horiz.frag'),
  ]
endif

animiate = shared_module('animate',
                         ['animate.cpp',
                          'fire/particle.cpp',
                          'fire/fire.cpp'] + animate_shaders,
                         include_directories: [wayfire_api_inc, wayfire_conf_inc],
                         dependencies: dependencies + animate_pch_deps,
                         install: true,
                         install_dir: join_paths(get_option('libdir'), 'wayfire'))

install_headers(['animate.hpp'], subdir: 'wayfire/plugins/animate')
