"""Development-only AT-SPI checks against the real logged-in GNOME window.

The installed application is C; this script is not included in the deb.
File chooser keyboard entry is driven separately through the VM console.
"""
import pathlib
import sys
import time
import subprocess
import pyatspi

root = pathlib.Path.home() / 'login-startup'
marker = root / 'work/gui-marker.txt'

def nodes(node):
    yield node
    for child in node:
        yield from nodes(child)

def app():
    return next(a for a in pyatspi.Registry.getDesktop(0) if a.name == 'login-startup')

def find(name, role=None):
    application = app()
    dialogs = [n for n in nodes(application) if n.getRoleName() == 'alert' and n.name == '删除这条启动配置？']
    scope = dialogs[-1] if dialogs and role == 'button' else application
    for node in nodes(scope):
        if node.name == name and (role is None or node.getRoleName() == role):
            return node
    raise AssertionError(f'Control not found: {name} ({role})')

def click(name, role='button'):
    control = find(name, role)
    actions = control.queryAction()
    assert actions.nActions > 0, name
    assert actions.doAction(0), name
    time.sleep(.35)

def content(name):
    return find(name, 'text').queryText().getText(0, -1)

def set_text(name, value):
    assert find(name, 'text').queryEditableText().setTextContents(value)
    time.sleep(.15)

def row_named(name):
    return find(name, 'list item')

def row_click(name, action, role='button'):
    row = row_named(name)
    node = next(n for n in nodes(row) if n.name == action and n.getRoleName() == role)
    assert node.queryAction().doAction(0)
    time.sleep(.4)

phase = sys.argv[1]
if phase == 'recover':
    while any(n.getRoleName() == 'alert' and n.name == '删除这条启动配置？' for n in nodes(app())):
        click('取消')
    row_click('界面验收已修改', '删除配置')
    click('删除配置')
    assert not any(n.name == '界面验收已修改' and n.getRoleName() == 'list item' for n in nodes(app()))
    marker.unlink(missing_ok=True)
    click('清空 / 取消编辑')
    print('PASS: test-only fixture recovered after ambiguous automation selector')
elif phase == 'choose':
    assert not marker.exists(), 'GUI fixture marker must not pre-exist'
    click('选择文件')
    print('PASS: native file chooser opened from real GTK button')
elif phase == 'exercise':
    assert content('文件路径') == str(root / 'tests/gui-probe.sh')
    assert not marker.exists(), 'Selecting a file must not execute it'
    set_text('显示名称', '界面验收（临时）')
    click('添加自启动')
    row_named('界面验收（临时）')
    assert not marker.exists(), 'Adding must not execute the file'
    row_click('界面验收（临时）', '立即试运行')
    for _ in range(50):
        if marker.exists():
            break
        time.sleep(.1)
    assert marker.read_text().splitlines() == ['GUI_TRIAL']
    row_click('界面验收（临时）', '编辑')
    set_text('显示名称', '界面验收已修改')
    click('保存修改')
    row_named('界面验收已修改')
    assert marker.read_text().splitlines() == ['GUI_TRIAL']
    row_click('界面验收已修改', '登录后自动启动', 'switch')
    row_named('界面验收已修改')
    row_click('界面验收已修改', '删除配置')
    click('取消')
    row_named('界面验收已修改')
    row_click('界面验收已修改', '删除配置')
    click('删除配置')
    assert not any(n.name == '界面验收已修改' and n.getRoleName() == 'list item' for n in nodes(app()))
    assert (root / 'tests/gui-probe.sh').exists()
    assert marker.read_text().splitlines() == ['GUI_TRIAL']
    print('PASS: real GNOME file selection/no-run/add/trial/edit/toggle/delete-cancel/delete-confirm/source-preserved')
elif phase == 'failure':
    identifier = subprocess.check_output(['/usr/bin/login-startup', '--add', str(root / 'tests/gui-fail.sh'), '--name', '异常退出验收'], text=True).strip()
    click('刷新启动项列表')
    row_click('异常退出验收', '立即试运行')
    for _ in range(30):
        labels = [n.name for n in nodes(app()) if n.getRoleName() == 'label']
        if any('试运行进程未正常结束' in label and '23' in label for label in labels):
            break
        time.sleep(.1)
    else:
        raise AssertionError('No real UI feedback for exit 23')
    row_click('异常退出验收', '删除配置')
    click('删除配置')
    assert (root / 'tests/gui-fail.sh').exists()
    print('PASS: real GUI asynchronous nonzero exit 23 feedback and fixture cleanup')
else:
    raise SystemExit('Expected choose or exercise')
