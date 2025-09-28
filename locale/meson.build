share_dir = join_paths(get_option('prefix'), 'share')
wayfire_locale_dir = join_paths(share_dir, 'locale')

fs = import('fs')

po_files_raw = run_command('find', '.', '-name', '*.po', check : true).stdout().strip().split('\n')
po_files = files(po_files_raw)

foreach po_file : po_files
    language = fs.name(fs.parent(fs.parent(po_file)))
    install_path = join_paths(wayfire_locale_dir, language, 'LC_MESSAGES')
    mo_dir = join_paths(meson.project_build_root(), 'locale', language, 'LC_MESSAGES')
    mo_file = fs.stem(po_file) + '.mo'
    mo_path = join_paths(mo_dir, mo_file)
    run_command('mkdir', '-p', mo_dir, check : true)
    run_command('msgfmt', po_file, '-o', mo_path, check : true)
    install_data(mo_path, install_dir : install_path)
endforeach
