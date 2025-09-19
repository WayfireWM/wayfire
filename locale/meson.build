share_dir = join_paths(get_option('prefix'), 'share')
wayfire_locale_dir = join_paths(share_dir, 'locale')

fs = import('fs')

mo_targets = []

po_files_raw = run_command('find', '.', '-name', '*.po', check : true).stdout().strip().split('\n')
po_files = files(po_files_raw)

foreach po_file : po_files
    language = fs.name(fs.parent(fs.parent(po_file)))
    mo_file = fs.stem(po_file) + '.mo'
    t = custom_target('mo-' + language + '-' + fs.stem(po_file),
        input : po_file,
        output : mo_file,
        command : ['msgfmt', '@INPUT@', '-o', '@OUTPUT@'],
        install : true,
        install_dir : join_paths(
            wayfire_locale_dir,
            language,
            'LC_MESSAGES'
        )
    )

    mo_targets += t
endforeach
