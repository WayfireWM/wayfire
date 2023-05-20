wayfire_sources = ['geometry.cpp',
                   'region.cpp',
                   'debug.cpp',

                   'core/output-layout.cpp',
                   'core/plugin-loader.cpp',
                   'core/matcher.cpp',
                   'core/object.cpp',
                   'core/opengl.cpp',
                   'core/plugin.cpp',
                   'core/scene.cpp',
                   'core/core.cpp',
                   'core/idle.cpp',
                   'core/img.cpp',
                   'core/wm.cpp',
                   'core/view-access-interface.cpp',

                   'core/transaction/transaction.cpp',
                   'core/transaction/transaction-manager.cpp',

                   'core/seat/pointing-device.cpp',
                   'core/seat/input-manager.cpp',
                   'core/seat/input-method-relay.cpp',
                   'core/seat/bindings-repository.cpp',
                   'core/seat/hotspot-manager.cpp',
                   'core/seat/drag-icon.cpp',
                   'core/seat/keyboard.cpp',
                   'core/seat/pointer.cpp',
                   'core/seat/cursor.cpp',
                   'core/seat/switch.cpp',
                   'core/seat/tablet.cpp',
                   'core/seat/touch.cpp',
                   'core/seat/seat.cpp',

                   'view/surface.cpp',
                   'view/subsurface.cpp',
                   'view/view.cpp',
                   'view/view-impl.cpp',
                   'view/xdg-shell.cpp',
                   'view/xwayland.cpp',
                   'view/layer-shell.cpp',
                   'view/view-3d.cpp',
                   'view/compositor-view.cpp',
                   'view/wlr-surface-node.cpp',

                   'output/output.cpp',
                   'output/render-manager.cpp',
                   'output/workspace-stream.cpp',
                   'output/workspace-impl.cpp']

wayfire_dependencies = [wayland_server, wlroots, xkbcommon, libinput,
                       pixman, drm, egl, glesv2, glm, wf_protos, libdl,
                       wfconfig, libinotify, backtrace, wfutils, xcb, wftouch]

if conf_data.get('BUILD_WITH_IMAGEIO')
    wayfire_dependencies += [jpeg, png]
endif

debug_arguments = []

addr2line = find_program('addr2line', required: false)
if addr2line.found()
  debug_arguments += ['-DHAS_ADDR2LINE=1']
else
  debug_arguments += ['-DHAS_ADDR2LINE=0']
endif


cxx_flags_asan = run_command('/bin/sh', '-c', 'echo $CXXFLAGS $CPPFLAGS | grep fsanitize')
if get_option('b_sanitize').contains('address') or cxx_flags_asan.returncode() == 0
  print_trace = false
  message('Address sanitizer enabled, disabling internal backtrace')
  message('Overriding print_trace build option')
endif

if print_trace
  debug_arguments += ['-DPRINT_TRACE']
endif

# First build a static library of all sources, so that it can be reused
# in tests
libwayfire_sta = static_library('libwayfire', wayfire_sources,
    dependencies: wayfire_dependencies,
    include_directories: [wayfire_conf_inc, wayfire_api_inc],
    cpp_args: debug_arguments,
    install: false)

libwayfire = declare_dependency(link_whole: libwayfire_sta,
    include_directories: [wayfire_conf_inc, wayfire_api_inc],
    dependencies: wayfire_dependencies)

tests_include_dirs = include_directories('.')

# Generate main executable
executable('wayfire', ['util.cpp', 'main.cpp'],
    dependencies: libwayfire,
    install: true,
    cpp_args: debug_arguments)

# Generate wf-plugin executable
executable('wf-plugin', ['util.cpp', 'wf-plugin.cpp'],
    dependencies: libwayfire,
    install: true,
    cpp_args: debug_arguments)

shared_module('default-config-backend', 'default-config-backend.cpp',
    dependencies: wayfire_dependencies,
    include_directories: [wayfire_conf_inc, wayfire_api_inc],
    cpp_args: debug_arguments,
    install_dir: conf_data.get('PLUGIN_PATH'),
    install: true)

install_subdir('api/wayfire',
    install_dir: get_option('includedir'))

public_api_requirements = [
    cairo,
    pango,
    pangocairo,
    wayland_server,
    pixman,
    # These might be subprojects so we need to pass them as strings
    'wlroots',
    'wf-config',
]

pkgconfig = import('pkgconfig')
pkgconfig.generate(
    version:      meson.project_version(),
    filebase:     meson.project_name(),
    name:         meson.project_name(),
    description: 'A Wayland Compositor',
    requires:     public_api_requirements,
    variables:    ['metadatadir=${prefix}/' + metadata_dir_suffix,
                   'sysconfdir=' + sysconfdir,
                   'plugindir=${libdir}/wayfire',
                   'icondir=${prefix}/share/wayfire/icons',
                   'pkgdatadir='+pkgdatadir]
    )
