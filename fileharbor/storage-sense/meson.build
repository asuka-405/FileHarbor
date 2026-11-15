# Storage Sense backend (new in the FileHarbor fork; not in upstream Nemo).
storage_daemon = configure_file(
  input: 'nemo-storage-daemon.in',
  output: 'nemo-storage-daemon',
  copy: true,
)
install_data(storage_daemon,
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x',
)

storage_conf = configuration_data()
storage_conf.set('bindir', join_paths(get_option('prefix'), get_option('bindir')))

configure_file(
  input: 'org.nemo.StorageSense.service.in',
  output: 'org.nemo.StorageSense.service',
  configuration: storage_conf,
  install_dir: join_paths(get_option('datadir'), 'dbus-1', 'services'),
)
configure_file(
  input: 'nemo-storage-sense.service.in',
  output: 'nemo-storage-sense.service',
  configuration: storage_conf,
  install_dir: join_paths(get_option('prefix'), 'lib', 'systemd', 'user'),
)
