wl_protocol_dir = wayland_protos.get_variable(pkgconfig: 'pkgdatadir')

wayland_scanner = find_program('wayland-scanner', native: true)

wayland_scanner_server = generator(
	wayland_scanner,
	output: '@BASENAME@-protocol.h',
	arguments: ['server-header', '@INPUT@', '@OUTPUT@'],
)

wayland_scanner_code = generator(
	wayland_scanner,
	output: '@BASENAME@-protocol.c',
	arguments: ['private-code', '@INPUT@', '@OUTPUT@'],
)

wayland_scanner_client = generator(
	wayland_scanner,
	output: '@BASENAME@-client-protocol.h',
	arguments: ['client-header', '@INPUT@', '@OUTPUT@'],
)

server_protocols = [
	[wl_protocol_dir, 'stable/xdg-shell/xdg-shell.xml'],
	[wl_protocol_dir, 'unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml'],
	[wl_protocol_dir, 'unstable/xdg-shell/xdg-shell-unstable-v6.xml'],
    [wl_protocol_dir, 'unstable/xdg-output/xdg-output-unstable-v1.xml'],
    [wl_protocol_dir, 'unstable/pointer-constraints/pointer-constraints-unstable-v1.xml'],
    [wl_protocol_dir, 'unstable/relative-pointer/relative-pointer-unstable-v1.xml'],
    [wl_protocol_dir, 'unstable/tablet/tablet-unstable-v2.xml'],
    [wl_protocol_dir, 'unstable/keyboard-shortcuts-inhibit/keyboard-shortcuts-inhibit-unstable-v1.xml'],
    [wl_protocol_dir, 'unstable/input-method/input-method-unstable-v1.xml'],
    'wayfire-shell-unstable-v2.xml',
    'gtk-shell.xml',
    'wlr-layer-shell-unstable-v1.xml',
    'wlr-output-power-management-unstable-v1.xml'
]

wl_protos_src = []
wl_protos_headers = []

foreach p : server_protocols
	xml = join_paths(p)
	wl_protos_src += wayland_scanner_code.process(xml)
	wl_protos_headers += wayland_scanner_server.process(xml)
endforeach

lib_wl_protos = static_library('wl_protos', wl_protos_src + wl_protos_headers,
	dependencies: [wayland_client]) # for the include directory

wf_protos = declare_dependency(
	link_with: lib_wl_protos,
	sources: wl_protos_headers,
)

# Install wayfire-shell protocol, so that other projects can find it
install_data('wayfire-shell-unstable-v2.xml', install_dir: join_paths(pkgdatadir, 'unstable'))
