project('login-startup', 'c', version: '0.2.4', default_options: ['c_std=c11', 'warning_level=3'])
gio = dependency('gio-unix-2.0', version: '>=2.74', include_type: 'system')
x11 = dependency('x11', required: true, include_type: 'system')
gtk = dependency('gtk4', version: '>=4.16', include_type: 'system')
adw = dependency('libadwaita-1', version: '>=1.6', include_type: 'system')
core_dependencies = [gio]
if x11.found()
  core_dependencies += [x11]
  add_project_arguments('-DHAVE_X11', language: 'c')
endif
core = static_library('startup-core', 'src/core.c', dependencies: core_dependencies)
app = executable('login-startup', ['src/main.c', 'src/ui.c'], link_with: core, dependencies: [gio, gtk, adw], install: true)
tests = executable('test-core', 'tests/test-core.c', link_with: core, include_directories: include_directories('src'), dependencies: gio)
test('core', tests)
test('watchdog-integration', find_program('tests/watchdog-integration.sh'), args: [app.full_path()], timeout: 150)
test('installed-deb-application-scan', app, args: ['--list-installed-apps'], timeout: 45)
install_data('data/io.github.local.LoginStartup.desktop', install_dir: get_option('datadir') / 'applications')
install_data('data/io.github.local.LoginStartup.svg', install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps')
