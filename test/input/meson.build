keyboard_config_test = executable(
    'keyboard-config-test',
    'keyboard-config-test.cpp',
    '../support/headless-core-harness.cpp',
    dependencies: [doctest, libwayfire],
    cpp_args: [
        '-DTEST_METADATA_DIR="' + meson.project_source_root() + '/metadata"',
        '-DTEST_DEFAULTS_INI="' + meson.project_source_root() + '/wayfire.ini"',
    ],
    install: false)
test('Keyboard config test', keyboard_config_test)

pointer_scroll_speed_test = executable(
    'pointer-scroll-speed-test',
    'pointer-scroll-speed-test.cpp',
    '../support/headless-core-harness.cpp',
    dependencies: [doctest, libwayfire],
    cpp_args: [
        '-DTEST_METADATA_DIR="' + meson.project_source_root() + '/metadata"',
        '-DTEST_DEFAULTS_INI="' + meson.project_source_root() + '/wayfire.ini"',
    ],
    install: false)
test('Pointer scroll speed test', pointer_scroll_speed_test)
