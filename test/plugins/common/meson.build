input_grab_test = executable(
    'input-grab-test',
    'input-grab-test.cpp',
    '../../support/headless-core-harness.cpp',
    dependencies: [doctest, libwayfire],
    include_directories: [plugins_common_inc],
    cpp_args: [
        '-DTEST_METADATA_DIR="' + meson.project_source_root() + '/metadata"',
        '-DTEST_DEFAULTS_INI="' + meson.project_source_root() + '/wayfire.ini"',
    ],
    install: false)

test('Input grab test', input_grab_test)
