#!/usr/bin/env python3
"""Run extracted shell packages with an isolated HOME and a fake Flatpak CLI."""
import hashlib
import importlib.util
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest

REPO = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / 'scripts'))
from package_linux import package_scripts


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="Friend's [Linux] & ")
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.payload = self.root / 'payload'
        files = {}
        for name in ('bin/64bit/obs-multi-replay.so', 'data/multi-replay-writer', 'data/locale/en-US.ini'):
            path = self.payload / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'\x7fELF\x02\x01' + bytes(12) + b'\x3e\x00fixture, never execute')
            path.chmod(0o755)
            files[name] = hashlib.sha256(path.read_bytes()).hexdigest()
        import re
        version = re.search(r'project\(obs-multi-replay VERSION ([\d.]+)', (REPO / 'CMakeLists.txt').read_text())[1]
        (self.payload / 'manifest.json').write_text(json.dumps(dict(
            target='flatpak', architecture='x86_64', integration_test=False, plugin_version=version,
            obs_version='32.2.2', app_commit='a' * 64, files=files,
            ffmpeg_abi=dict(avformat=62, avcodec=62, avutil=60))))
        self.archive = package_scripts(self.payload, self.root / 'out')
        self.package = self.root / 'extracted'
        self.package.mkdir()
        with tarfile.open(self.archive) as tar:
            for entry in tar:
                self.assertNotIn('..', pathlib.PurePosixPath(entry.name).parts)
                self.assertTrue(entry.isfile() or entry.isdir())
            tar.extractall(self.package)
        self.home = self.root / 'home'
        self.home.mkdir()
        self.config = self.home / '.var/app/com.obsproject.Studio/config/obs-studio'
        self.target = self.config / 'plugins/obs-multi-replay'
        self.target.mkdir(parents=True)
        (self.target / 'legacy.txt').write_text('old plugin')
        self.profile = self.config / 'basic/profiles/Test/basic.ini'
        self.profile.parent.mkdir(parents=True)
        self.profile.write_bytes(b'[MultiLengthReplay]\r\nSettings={"keep":true}\r\n')
        self.profile_hash = hashlib.sha256(self.profile.read_bytes()).hexdigest()
        self.preserved = {}
        for name, data in (('basic/scenes/Friends.json', b'{"sources":[],"hotkeys":{"keep":true}}'),
                           ('user.ini', b'[Hotkeys]\nKEEP=1\n'), ('recordings/keep.mkv', b'original recording')):
            path = self.config / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            self.preserved[path] = data
        self.other = self.target.parent / 'unrelated-plugin'
        self.other.mkdir()
        (self.other / 'keep.txt').write_text('other plugin')
        tools = self.root / 'bin'
        tools.mkdir()
        flatpak = tools / 'flatpak'
        flatpak.write_text('''#!/bin/sh
case "$1" in
list) printf 'com.obsproject.Studio\\tx86_64\\tstable\\tuser\\n' ;;
info) printf '%s\\n' "$FAKE_COMMIT" ;;
ps) if [ "${FAKE_OBS_RUNNING:-0}" = 1 ]; then printf 'com.obsproject.Studio\\n'; fi ;;
*) exit 1 ;;
esac
''')
        flatpak.chmod(0o755)
        self.env = dict(os.environ, HOME=str(self.home), PATH=str(tools) + os.pathsep + os.environ['PATH'], FAKE_COMMIT='a' * 64)
        self.env.pop('SUDO_USER', None)
        self.env.pop('SUDO_UID', None)

    def run_script(self, name='install.sh', expected=0, interactive=False, **env):
        command = ['/bin/sh', str(self.package / name)]
        if not interactive:
            command.append('--non-interactive')
        result = subprocess.run(command, input='n\n' if interactive else '', text=True,
                                capture_output=True, cwd=self.root, env=dict(self.env, **env), timeout=20)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        self.assertEqual(hashlib.sha256(self.profile.read_bytes()).hexdigest(), self.profile_hash)
        self.assertEqual((self.other / 'keep.txt').read_text(), 'other plugin')
        for path, data in self.preserved.items():
            self.assertEqual(path.read_bytes(), data)
        return result

    def test_archive_contract_and_modes(self):
        with tarfile.open(self.archive) as tar:
            roots = {pathlib.PurePosixPath(e.name).parts[0] for e in tar}
            self.assertEqual(roots, {'install.sh', 'uninstall.sh', 'README.txt', 'PluginFiles'})
            for name in ('install.sh', 'uninstall.sh', 'PluginFiles/payload/obs-multi-replay/data/multi-replay-writer'):
                self.assertTrue(tar.getmember(name).mode & 0o111)
                if name.endswith('.sh'):
                    self.assertNotIn(b'\r', tar.extractfile(name).read())

    def test_install_update_repair_and_logs(self):
        self.assertIn('Update', self.run_script().stdout)
        self.assertFalse((self.target / 'legacy.txt').exists())
        self.assertIn('Repair', self.run_script().stdout)
        shutil.rmtree(self.target)
        self.assertIn('Install version', self.run_script().stdout)
        self.assertTrue(list((self.package / 'PluginFiles/Logs').glob('*.log')))
        self.assertEqual({p.name for p in self.package.iterdir()}, {'install.sh', 'uninstall.sh', 'README.txt', 'PluginFiles'})

    def test_cancel_leaves_plugin(self):
        self.assertIn('Cancelled', self.run_script(interactive=True).stdout)
        self.assertTrue((self.target / 'legacy.txt').exists())

    def test_corruption_leaves_plugin_and_logs_error(self):
        path = self.package / 'PluginFiles/payload/obs-multi-replay/data/multi-replay-writer'
        path.write_text('corrupt')
        self.assertIn('Damaged package', self.run_script(expected=1).stderr)
        self.assertTrue((self.target / 'legacy.txt').exists())
        self.assertIn('Damaged package', next((self.package / 'PluginFiles/Logs').glob('*.log')).read_text())

    def test_obs_mismatch_and_running_guard(self):
        self.assertIn('Installed commit', self.run_script(expected=1, FAKE_COMMIT='b' * 64).stderr)
        self.assertIn('Close OBS', self.run_script(expected=1, FAKE_OBS_RUNNING='1').stderr)
        self.assertIn('Close OBS', self.run_script('uninstall.sh', expected=1, FAKE_OBS_RUNNING='1').stderr)
        self.assertTrue((self.target / 'legacy.txt').exists())

    def test_downgrade_refused(self):
        (self.target / 'manifest.json').write_text('{"plugin_version":"99.0.0"}')
        self.assertIn('Downgrading', self.run_script(expected=1).stderr)
        self.assertTrue((self.target / 'legacy.txt').exists())

    def test_relocated_package_removal_without_payload_or_obs_match(self):
        self.run_script()
        new = self.root / 'relocated package'
        self.package.rename(new)
        self.package = new
        shutil.rmtree(self.package / 'PluginFiles/payload')
        self.run_script('uninstall.sh', FAKE_COMMIT='b' * 64)
        self.assertFalse(self.target.exists())
        self.run_script('uninstall.sh')
        self.assertTrue(list((self.config / 'replay-plugin-backups').glob('*/plugin')))

    def test_missing_python_has_actionable_error(self):
        tools = self.root / 'no-python'
        tools.mkdir()
        for name in ('dirname', 'mkdir'):
            (tools / name).symlink_to(shutil.which(name))
        result = self.run_script(expected=1, PATH=str(tools))
        self.assertIn('Python 3.10', result.stdout)
        self.assertTrue((self.target / 'legacy.txt').exists())

    def test_sudo_refused(self):
        self.assertIn('without sudo', self.run_script(expected=1, SUDO_USER='test').stderr)
        self.assertTrue((self.target / 'legacy.txt').exists())

    def run_bundle(self, *arguments, expected=0, answer='', path=None, **environment):
        path = path or self.archive.with_name(self.archive.name.removesuffix('.tar.gz') + '.run')
        result = subprocess.run(['/bin/sh', str(path), *arguments], input=answer, text=True,
                                capture_output=True, cwd=self.root, env=dict(self.env, **environment), timeout=20)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        self.assertEqual(hashlib.sha256(self.profile.read_bytes()).hexdigest(), self.profile_hash)
        self.assertEqual((self.other / 'keep.txt').read_text(), 'other plugin')
        for path, data in self.preserved.items():
            self.assertEqual(path.read_bytes(), data)
        return result

    def test_single_file_install_repair_uninstall_and_persistent_logs(self):
        self.assertIn('Update', self.run_bundle('--non-interactive').stdout)
        self.assertIn('Repair', self.run_bundle('--non-interactive').stdout)
        self.run_bundle('--uninstall', '--non-interactive')
        self.run_bundle('--uninstall', '--non-interactive')
        self.assertFalse(self.target.exists())
        self.assertEqual(len(list((self.home / '.local/state/obs-multi-replay/logs').glob('*.log'))), 4)

    def test_single_file_prompt_retains_stdin_and_cancel_is_safe(self):
        self.assertIn('Cancelled', self.run_bundle(answer='n\n').stdout)
        self.assertTrue((self.target / 'legacy.txt').exists())
        self.assertIn('Complete', self.run_bundle(answer='y\n').stdout)

    def test_single_file_corruption_refused_before_plugin_change(self):
        path = self.archive.with_name(self.archive.name.removesuffix('.tar.gz') + '.run')
        with path.open('ab') as stream:
            stream.write(b'damage')
        self.assertIn('damaged', self.run_bundle('--non-interactive', expected=1).stderr)
        self.assertTrue((self.target / 'legacy.txt').exists())

    def test_single_file_mismatch_and_sudo_refused(self):
        self.assertIn('Installed commit', self.run_bundle('--non-interactive', expected=1, FAKE_COMMIT='b' * 64).stderr)
        self.assertIn('without sudo', self.run_bundle('--non-interactive', expected=1, SUDO_USER='test').stderr)
        self.assertTrue((self.target / 'legacy.txt').exists())

    def test_single_file_refuses_unsafe_archive(self):
        from package_linux import package_run
        malicious = self.root / 'unsafe.tar.gz'
        with tarfile.open(malicious, 'w:gz') as archive:
            entry = tarfile.TarInfo('../escape')
            archive.addfile(entry)
        self.assertIn('Unsafe', self.run_bundle('--non-interactive', expected=1, path=package_run(malicious)).stderr)
        self.assertFalse((self.root / 'escape').exists())
        self.assertTrue((self.target / 'legacy.txt').exists())


def check_production_archive(path):
    # Validate the actual build, then run its launchers with isolated user files.
    spec = importlib.util.spec_from_file_location('release', REPO / 'scripts/release-assets.py')
    release = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(release)
    release.validate_archive(path)
    with tempfile.TemporaryDirectory(prefix="Replay production's [package] ") as directory:
        root = pathlib.Path(directory)
        extracted = root / 'extracted'
        extracted.mkdir()
        with tarfile.open(path) as archive:
            archive.extractall(extracted)
        home = root / 'home'
        home.mkdir()
        config = home / '.var/app/com.obsproject.Studio/config/obs-studio'
        profile = config / 'basic/profiles/Test/basic.ini'
        profile.parent.mkdir(parents=True)
        original = b'[MultiLengthReplay]\nSettings={"preserve":true}\n'
        profile.write_bytes(original)
        other = config / 'plugins/other-plugin.txt'
        other.parent.mkdir(parents=True)
        other.write_text('preserve')
        env = dict(os.environ, HOME=str(home), XDG_STATE_HOME=str(home / '.local/state'),
                   XDG_DATA_HOME=os.environ.get('XDG_DATA_HOME', str(pathlib.Path.home() / '.local/share')))
        env.pop('SUDO_USER', None)
        env.pop('SUDO_UID', None)
        target = config / 'plugins/obs-multi-replay'
        def run(name):
            subprocess.run(['/bin/sh', str(extracted / name), '--non-interactive'], env=env, check=True, timeout=60)
            assert profile.read_bytes() == original
            assert other.read_text() == 'preserve'
        run('install.sh')
        import replay_installer as core
        core.verify_package(target)
        run('install.sh')
        shutil.rmtree(extracted / 'PluginFiles/payload')
        run('uninstall.sh')
        run('uninstall.sh')
        assert not target.exists()
        bundle = path.with_name(path.name.removesuffix('.tar.gz') + '.run')
        for arguments in ([], [], ['--uninstall'], ['--uninstall']):
            subprocess.run(['/bin/sh', str(bundle), '--non-interactive', *arguments], env=env, check=True, timeout=60)
            assert profile.read_bytes() == original
            assert other.read_text() == 'preserve'
        assert not target.exists()
        assert len(list((home / '.local/state/obs-multi-replay/logs').glob('*.log'))) == 4
        assert {p.name for p in extracted.iterdir()} == {'install.sh', 'uninstall.sh', 'README.txt', 'PluginFiles'}
        print('PASS: production tar and single-file installers, install/repair/removal, real Flatpak compatibility, payload hashes and isolated configuration')


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--production-archive':
        check_production_archive(pathlib.Path(sys.argv[2]))
    else:
        unittest.main()
