# FileHarbor built-in tools (developer tools, image/video/music/markdown). New in the fork.
install_subdir('fhtools',
  install_dir: join_paths(nemoDataPath, 'tools'),
  exclude_directories: ['__pycache__'],
)
install_data('fileharbor-tools',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x',
)
