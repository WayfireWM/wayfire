xdg_shell_client_xml = join_paths(wl_protocol_dir, 'stable/xdg-shell/xdg-shell.xml')

xdg_shell_client_header = custom_target(
    'xdg-shell-client-header',
    input: xdg_shell_client_xml,
    output: 'xdg-shell-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])

layer_shell_client_xml = join_paths(meson.project_source_root(),
    'proto/wlr-layer-shell-unstable-v1.xml')

layer_shell_client_header = custom_target(
    'wlr-layer-shell-client-header',
    input: layer_shell_client_xml,
    output: 'wlr-layer-shell-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])

viewporter_client_xml = join_paths(wl_protocol_dir, 'stable/viewporter/viewporter.xml')

viewporter_client_header = custom_target(
    'viewporter-client-header',
    input: viewporter_client_xml,
    output: 'viewporter-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])

viewporter_client_code = custom_target(
    'viewporter-client-code',
    input: viewporter_client_xml,
    output: 'viewporter-client-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

fractional_scale_client_xml = join_paths(wl_protocol_dir,
    'staging/fractional-scale/fractional-scale-v1.xml')

fractional_scale_client_header = custom_target(
    'fractional-scale-client-header',
    input: fractional_scale_client_xml,
    output: 'fractional-scale-v1-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])

fractional_scale_client_code = custom_target(
    'fractional-scale-client-code',
    input: fractional_scale_client_xml,
    output: 'fractional-scale-v1-client-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

test_support_sources = [
    '../support/headless-core-harness.cpp',
    '../support/wayland-client-utils.cpp',
    '../support/wayland-layer-shell-client-bridge.c',
    '../support/wayland-layer-shell-client.cpp',
    '../support/wayland-xdg-client.cpp',
    fractional_scale_client_header,
    fractional_scale_client_code,
    viewporter_client_header,
    viewporter_client_code,
    layer_shell_client_header,
    xdg_shell_client_header,
]

xdg_shell_test = executable(
    'xdg-shell-test',
    'xdg-shell-test.cpp',
    test_support_sources,
    dependencies: [doctest, libwayfire, wayland_client],
    cpp_args: [
        '-DTEST_METADATA_DIR="' + meson.project_source_root() + '/metadata"',
        '-DTEST_DEFAULTS_INI="' + meson.project_source_root() + '/wayfire.ini"',
    ],
    install: false)

layer_shell_test = executable(
    'layer-shell-test',
    'layer-shell-test.cpp',
    test_support_sources,
    dependencies: [doctest, libwayfire, wayland_client],
    cpp_args: [
        '-DTEST_METADATA_DIR="' + meson.project_source_root() + '/metadata"',
        '-DTEST_DEFAULTS_INI="' + meson.project_source_root() + '/wayfire.ini"',
    ],
    install: false)

scaling_test = executable(
    'scaling-test',
    'scaling-test.cpp',
    test_support_sources,
    dependencies: [doctest, libwayfire, wayland_client],
    cpp_args: [
        '-DTEST_METADATA_DIR="' + meson.project_source_root() + '/metadata"',
        '-DTEST_DEFAULTS_INI="' + meson.project_source_root() + '/wayfire.ini"',
    ],
    install: false)

# This test uses a different value for a workaround option. Keep it in a separate
# executable because static option_wrapper_t instances cache options per process.
native_buffer_size_scaling_test = executable(
    'native-buffer-size-scaling-test',
    'native-buffer-size-scaling-test.cpp',
    test_support_sources,
    dependencies: [doctest, libwayfire, wayland_client],
    cpp_args: [
        '-DTEST_METADATA_DIR="' + meson.project_source_root() + '/metadata"',
        '-DTEST_DEFAULTS_INI="' + meson.project_source_root() + '/wayfire.ini"',
    ],
    install: false)

test('Xdg-shell test', xdg_shell_test)
test('Layer-shell test', layer_shell_test)
test('Scaling test', scaling_test)
test('Native buffer size scaling test', native_buffer_size_scaling_test)

if not get_option('xwayland').disabled() and wlroots_features['xwayland']
    xwayland_test = executable(
        'xwayland-test',
        'xwayland-test.cpp',
        test_support_sources,
        dependencies: [doctest, libwayfire, wayland_client, xcb],
        cpp_args: [
            '-DTEST_METADATA_DIR="' + meson.project_source_root() + '/metadata"',
            '-DTEST_DEFAULTS_INI="' + meson.project_source_root() + '/wayfire.ini"',
        ],
        install: false)

    test('Xwayland test', xwayland_test)
endif
