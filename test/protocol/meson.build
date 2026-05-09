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

test_support_sources = [
    '../support/headless-core-harness.cpp',
    '../support/wayland-client-utils.cpp',
    '../support/wayland-layer-shell-client-bridge.c',
    '../support/wayland-layer-shell-client.cpp',
    '../support/wayland-xdg-client.cpp',
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

test('Xdg-shell test', xdg_shell_test)
test('Layer-shell test', layer_shell_test)
