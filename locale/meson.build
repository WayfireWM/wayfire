share_dir = join_paths(get_option('prefix'), 'share')
wayfire_locale_dir = join_paths(share_dir, 'locale')

fs = import('fs')

po_files_raw = run_command('find', '.', '-name', '*.po', check : true).stdout().strip().split('\n')
po_files = files(po_files_raw)

foreach po_file : po_files
    language = fs.name(fs.parent(fs.parent(po_file)))
    install_path = join_paths(
        wayfire_locale_dir,
        language,
        'LC_MESSAGES'
    )
    run_command('mkdir', '-p', meson.project_build_root() + '/locale/' + language + '/LC_MESSAGES', check : true)
    run_command('msgfmt', po_file, '-o', meson.project_build_root() + '/locale/' + language + '/LC_MESSAGES/' + fs.stem(po_file) + '.mo', check : true)
    install_data(
        meson.project_build_root() + '/locale/' + language + '/LC_MESSAGES/' + fs.stem(po_file) + '.mo',
        install_dir : install_path
    )
endforeach
