
################################################################################

desktopFiles = [
  'nemo-autorun-software.desktop',
  'nemo-autostart.desktop',
  'in.archdev.FileHarbor.desktop',
]

install_data(desktopFiles,
  install_dir: join_paths(get_option('datadir'), 'applications')
)

################################################################################
dataConf = configuration_data()
dataConf.set('bindir', join_paths(get_option('prefix'), get_option('bindir')))

dbusFiles = [
  configure_file(
    input : 'in.archdev.FileHarbor.service.in',
    output: 'in.archdev.FileHarbor.service',
    configuration: dataConf,
  ),

  configure_file(
    input : 'in.archdev.FileHarbor.FileManager1.service.in',
    output: 'in.archdev.FileHarbor.FileManager1.service',
    configuration: dataConf,
  ),
]

install_data(dbusFiles,
  install_dir: join_paths(get_option('datadir'), 'dbus-1', 'services')
)

################################################################################

# nemo.xml is generated from nemo.xml.in by ../generate_additional_file
install_data('nemo.xml',
  install_dir: join_paths(get_option('datadir'), 'mime', 'packages')
)

################################################################################
dataConf = configuration_data()
dataConf.set('bindir', join_paths(get_option('prefix'), get_option('bindir')))

rootPolicyFile = configure_file(
  input : 'org.nemo.root.policy.in',
  output: 'org.nemo.root.policy',
  configuration: dataConf,
)

install_data(rootPolicyFile,
  install_dir: join_paths(get_option('datadir'), 'polkit-1', 'actions')
)

foreach lang_file: ['nemo_action.lang', 'nemo_search_helper.lang']
  foreach sv_dir: ['gtksourceview-2.0', 'gtksourceview-3.0', 'gtksourceview-4']
    install_data(lang_file,
      install_dir: join_paths(get_option('datadir'), sv_dir, 'language-specs')
    )
  endforeach
endforeach

################################################################################

install_data(
  files('themes/cosmic.ini', 'themes/cosmic-dark.ini', 'themes/cosmic-light.ini', 'themes/classic.ini'),
  install_dir: join_paths(nemoDataPath, 'themes')   # FileHarbor themes
)
install_data(
  files('shell-integration/integration.sh', 'shell-integration/ssh-wrapper.sh',
        'shell-integration/remote-bootstrap.sh.in', 'shell-integration/bash-rc'),
  install_dir: join_paths(nemoDataPath, 'shell-integration')     # FileHarbor terminal integration
)
install_data(
  files('shell-integration/zsh/.zshenv', 'shell-integration/zsh/.zprofile',
        'shell-integration/zsh/.zshrc', 'shell-integration/zsh/.zlogin'),
  install_dir: join_paths(nemoDataPath, 'shell-integration', 'zsh')
)
install_data(files('../fileharbor/docs/THEMING.md', '../fileharbor/docs/EXTENSIONS.md'), install_dir: join_paths(nemoDataPath, 'docs'))

subdir('icons')
