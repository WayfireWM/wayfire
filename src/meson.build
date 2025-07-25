wayfire_sources = ['geometry.cpp',
                   'region.cpp',
                   'debug.cpp',
                   'util.cpp',
                   'json.cpp',
                   'render.cpp',

                   'core/window-manager.cpp',
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

                   'core/txn/transaction.cpp',
                   'core/txn/transaction-manager.cpp',

                   'core/seat/pointing-device.cpp',
                   'core/seat/input-manager.cpp',
                   'core/seat/input-method-relay.cpp',
                   'core/seat/input-method-popup.cpp',
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

                   'view/wlr-surface-controller.cpp',
                   'view/wlr-subsurface-controller.cpp',
                   'view/view.cpp',
                   'view/toplevel-view.cpp',
                   'view/view-impl.cpp',
                   'view/toplevel-node.cpp',
                   'view/xdg-shell.cpp',
                   'view/xdg-shell/xdg-toplevel.cpp',
                   'view/xdg-shell/xdg-toplevel-view.cpp',
                   'view/xwayland.cpp',
                   'view/xwayland/xwayland-toplevel-view.cpp',
                   'view/xwayland/xwayland-view-base.cpp',
                   'view/xwayland/xwayland-toplevel.cpp',
                   'view/xwayland/xwayland-helpers.cpp',
                   'view/layer-shell/layer-shell.cpp',
                   'view/layer-shell/layer-shell-node.cpp',
                   'view/view-3d.cpp',
                   'view/compositor-view.cpp',
                   'view/wlr-surface-node.cpp',
                   'view/translation-node.cpp',

                   'output/output.cpp',
                   'output/workarea.cpp',
                   'output/render-manager.cpp',
                   'output/workspace-stream.cpp',
                   'output/workspace-impl.cpp']

wayfire_dependencies = [wayland_server, wlroots, xkbcommon, libinput,
                       pixman, drm, egl, glesv2, glm, wf_protos, libdl,
                       wfconfig, libinotify, backtrace, wfutils, xcb,
                       wftouch, json, udev]

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


if has_asan
  print_trace = false
  debug_arguments += ['-DHAS_ASAN=1']
  message('Address sanitizer enabled, disabling internal backtrace')
  message('Overriding print_trace build option')
else
  debug_arguments += ['-DHAS_ASAN=0']
endif

if print_trace
  debug_arguments += ['-DPRINT_TRACE']
endif

# Generate information about Wayfire version
git = find_program('git', native: true, required: false)
git_commit_info = vcs_tag(
    input: 'version/git-commit.cpp.in',
    output: 'git-commit.cpp',
    replace_string: '@GIT_COMMIT@',
    command: git.found() ? [git, 'rev-parse', '--short', 'HEAD'] : ['echo', 'unknown'],
    fallback: 'unknown')

git_branch_info = vcs_tag(
    input: 'version/git-branch.cpp.in',
    output: 'git-branch.cpp',
    replace_string: '@GIT_BRANCH@',
    command: git.found() ? [git, 'rev-parse', '--abbrev-ref', 'HEAD'] : ['echo', 'unknown'],
    fallback: 'unknown')

# First build a static library of all sources, so that it can be reused
# in tests
libwayfire_sta = static_library('libwayfire', wayfire_sources,
    dependencies: wayfire_dependencies,
    include_directories: [wayfire_conf_inc, wayfire_api_inc],
    cpp_args: debug_arguments,
    install: false, cpp_pch: 'pch/pch.h')

libwayfire = declare_dependency(link_whole: libwayfire_sta,
    include_directories: [wayfire_conf_inc, wayfire_api_inc],
    dependencies: wayfire_dependencies)

tests_include_dirs = include_directories('.')

# Generate main executable
executable('wayfire', ['main.cpp', git_commit_info, git_branch_info],
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
    wlroots_dep_name,
    'wf-config',
]

if wlroots_features['gles2_renderer']
  public_api_requirements += glesv2
endif

if wlroots_features['vulkan_renderer']
  public_api_requirements += vulkan
endif

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

devenv = environment()
devenv.set('WAYFIRE_DEFAULT_CONFIG_BACKEND', meson.current_build_dir() + '/libdefault-config-backend.so')
meson.add_devenv(devenv)
