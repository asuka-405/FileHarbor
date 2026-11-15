# Storage Sense backend (new in the FileHarbor fork; not in upstream Nemo).
storage_daemon = configure_file(
  input: 'fileharbor-storage-daemon.in',
  output: 'fileharbor-storage-daemon',
  copy: true,
)
install_data(storage_daemon,
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x',
)

storage_conf = configuration_data()
storage_conf.set('bindir', join_paths(get_option('prefix'), get_option('bindir')))

configure_file(
  input: 'in.archdev.FileHarbor.StorageSense.service.in',
  output: 'in.archdev.FileHarbor.StorageSense.service',
  configuration: storage_conf,
  install_dir: join_paths(get_option('datadir'), 'dbus-1', 'services'),
)
configure_file(
  input: 'fileharbor-storage-sense.service.in',
  output: 'fileharbor-storage-sense.service',
  configuration: storage_conf,
  install_dir: join_paths(get_option('prefix'), 'lib', 'systemd', 'user'),
)
