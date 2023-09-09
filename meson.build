project(
	'wayfire',
	'c',
	'cpp',
	version: '0.8.0',
	license: 'MIT',
	meson_version: '>=0.56.0',
	default_options: [
		'cpp_std=c++17',
		'c_std=c11',
		'warning_level=2',
		'werror=false',
	],
)

wayfire_api_inc  = include_directories('src/api')

wayland_server = dependency('wayland-server')
wayland_client = dependency('wayland-client')
wayland_cursor = dependency('wayland-cursor')
wayland_protos = dependency('wayland-protocols', version: '>=1.12')
cairo          = dependency('cairo')
pango          = dependency('pango')
pangocairo     = dependency('pangocairo')
drm            = dependency('libdrm')
egl            = dependency('egl')
glesv2         = dependency('glesv2')
glm            = dependency('glm')
libinput       = dependency('libinput', version: '>=1.7.0')
pixman         = dependency('pixman-1')
threads        = dependency('threads')
xkbcommon      = dependency('xkbcommon')
libdl          = meson.get_compiler('cpp').find_library('dl')

# We're not to use system wlroots: So we'll use the subproject
if get_option('use_system_wlroots').disabled()
	use_system_wlroots = false
	wlroots = subproject('wlroots', default_options : ['examples=false']).get_variable('wlroots')

elif get_option('use_system_wlroots').enabled()
	use_system_wlroots = true
	wlroots = dependency('wlroots', version: ['>=0.16.0', '<0.17.0'], required: true)

elif get_option('use_system_wlroots').auto()
	message( 'SEARCHING FOR WLROOTS' )
	wlroots = dependency('wlroots', version: ['>=0.16.0', '<0.17.0'], required: false)
	use_system_wlroots = true
	if not wlroots.found()
		use_system_wlroots = false
		wlroots = subproject('wlroots', default_options : ['examples=false']).get_variable('wlroots')
	endif
endif

# We're not to use system wlroots: So we'll use the subproject
if get_option('use_system_wfconfig').disabled()
	use_system_wfconfig = true
	wfconfig = subproject('wf-config').get_variable('wfconfig')

elif get_option('use_system_wfconfig').enabled()
	use_system_wfconfig = false
	wfconfig = dependency('wf-config', version: ['>=0.16.0', '<0.17.0'], required: true)

elif get_option('use_system_wfconfig').auto()
	wfconfig = dependency('wf-config', version: ['>=0.8.0', '<0.9.0'], required: false)
	use_system_wfconfig = true
	if not wlroots.found()
		use_system_wfconfig = false
		wfconfig = subproject('wf-config').get_variable('wfconfig')
	endif
endif

wfutils = subproject('wf-utils').get_variable('wfutils')
wftouch = subproject('wf-touch').get_variable('wftouch')

needs_libinotify = ['freebsd', 'dragonfly'].contains(host_machine.system())
libinotify       = dependency('libinotify', required: needs_libinotify)

jpeg = dependency('libjpeg', required: false)
png  = dependency('libpng',  required: false)

# backtrace() is in a separate library on FreeBSD and Linux with musl
backtrace = meson.get_compiler('cpp').find_library('execinfo', required: false)

conf_data = configuration_data()

version = '"@0@"'.format(meson.project_version())
git = find_program('git', native: true, required: false)
if git.found()
	git_commit = run_command([git, 'rev-parse', '--short', 'HEAD'], check: true)
	git_branch = run_command([git, 'rev-parse', '--abbrev-ref', 'HEAD'], check: true)
	if git_commit.returncode() == 0 and git_branch.returncode() == 0
		version = '"@0@-@1@ (" __DATE__ ", branch \'@2@\')"'.format(
			meson.project_version(),
			git_commit.stdout().strip(),
			git_branch.stdout().strip(),
		)
	endif
endif
add_project_arguments('-DWAYFIRE_VERSION=@0@'.format(version), language: 'cpp')
conf_data.set('WAYFIRE_VERSION', '-DWAYFIRE_VERSION=@0@'.format(version))

conf_data.set('INSTALL_PREFIX', get_option('prefix'))
conf_data.set('PLUGIN_PATH', join_paths(get_option('prefix'), get_option('libdir'), 'wayfire'))
metadata_dir_suffix = 'share/wayfire/metadata'
conf_data.set('PLUGIN_XML_DIR', join_paths(get_option('prefix'), metadata_dir_suffix))
sysconfdir = join_paths(get_option('prefix'), get_option('sysconfdir'))
conf_data.set('SYSCONFDIR', sysconfdir)
pkgdatadir = join_paths(get_option('prefix'), 'share', 'wayfire', 'protocols')

if get_option('default_config_backend') == 'default'
  conf_data.set('DEFAULT_CONFIG_BACKEND', join_paths(conf_data.get('PLUGIN_PATH'), 'libdefault-config-backend.so'))
else
  conf_data.set('DEFAULT_CONFIG_BACKEND', get_option('default_config_backend'))
endif

cpp = meson.get_compiler('cpp')

# needed to dlopen() plugins
# -E is for RTTI/dynamic_cast across plugins
add_project_link_arguments(['-rdynamic', '-Wl,-E'], language: 'cpp')

project_args = ['-DWLR_USE_UNSTABLE']
# Needed for dlclose to actually free plugin memory on gcc+glibc
if cpp.has_argument('-fno-gnu-unique')
  project_args += '-fno-gnu-unique'
endif
add_project_arguments(project_args, language: ['cpp', 'c'])

# Needed on some older compilers
if cpp.has_link_argument('-lc++fs')
  add_project_link_arguments(['-lc++fs'], language: 'cpp')
elif cpp.has_link_argument('-lc++experimental')
  add_project_link_arguments(['-lc++experimental'], language: 'cpp')
elif cpp.has_link_argument('-lstdc++fs')
  add_project_link_arguments(['-lstdc++fs'], language: 'cpp')
endif

if get_option('enable_gles32')
  cpp.check_header('GLES3/gl32.h', dependencies: glesv2, required: true)
  conf_data.set('USE_GLES32', true)
else
  conf_data.set('USE_GLES32', false)
endif

if png.found() and jpeg.found()
  conf_data.set('BUILD_WITH_IMAGEIO', true)
else
  conf_data.set('BUILD_WITH_IMAGEIO', false)
endif

wayfire_conf_inc = include_directories(['.'])

add_project_arguments(['-Wno-unused-parameter'], language: 'cpp')

have_xwayland = false
have_x11_backend = false
if use_system_wlroots
  have_xwayland = cpp.get_define('WLR_HAS_XWAYLAND', prefix: '#include <wlr/config.h>', dependencies: wlroots) == '1'
  have_x11_backend = cpp.get_define('WLR_HAS_X11_BACKEND', prefix: '#include <wlr/config.h>', dependencies: wlroots) == '1'
else
  have_xwayland = subproject('wlroots').get_variable('conf_data').get('WLR_HAS_XWAYLAND', false) == 1
  have_x11_backend = subproject('wlroots').get_variable('conf_data').get('WLR_HAS_X11_BACKEND', false) == 1
endif

if get_option('xwayland').enabled() and not have_xwayland
  error('Cannot enable Xwayland in wayfire: wlroots has been built without Xwayland support')
endif

if get_option('xwayland').enabled()
  have_xwayland = true
elif get_option('xwayland').disabled()
  have_xwayland = false
endif

if have_xwayland
  xcb = dependency('xcb')
  conf_data.set('WF_HAS_XWAYLAND', 1)
else
  xcb = declare_dependency() # dummy dep
  conf_data.set('WF_HAS_XWAYLAND', 0)
endif

if get_option('print_trace')
  print_trace = true
else
  print_trace = false
endif

add_project_arguments(['-DWF_USE_CONFIG_H'], language: ['cpp', 'c'])
configure_file(input: 'config.h.in',
               output: 'config.h',
               install: true,
               install_dir: join_paths('include', 'wayfire'),
               configuration: conf_data)

subdir('proto')
subdir('src')
subdir('man')
subdir('metadata')
subdir('plugins')

# Unit tests
doctest = dependency('doctest', required: get_option('tests'))
if doctest.found()
    subdir('test')
endif

install_data('wayfire.desktop', install_dir :
    join_paths(get_option('prefix'), 'share/wayland-sessions'))

summary = [
	'',
	'----------------',
	'wayfire @0@'.format(meson.project_version()),
	'',
    'system wfconfig: @0@'.format(use_system_wfconfig),
    ' system wlroots: @0@'.format(use_system_wlroots),
    '       xwayland: @0@'.format(have_xwayland),
    '    x11-backend: @0@'.format(have_x11_backend),
    '        imageio: @0@'.format(conf_data.get('BUILD_WITH_IMAGEIO')),
    '         gles32: @0@'.format(conf_data.get('USE_GLES32')),
    '    print trace: @0@'.format(print_trace),
    '     unit tests: @0@'.format(doctest.found()),
    '----------------',
    ''
]
message('\n'.join(summary))
