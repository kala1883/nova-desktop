"""Isolated broker tests for publication, subscriptions and durable fan-out."""
import hashlib
import importlib
import ast
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'sync/server'))
broker_module = importlib.import_module('chunksync.broker')


class Workflow(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='nova-workflow-')
        self.project = Path(self.temporary.name)
        broker_module.PROJECT = self.project
        self.broker = broker_module.Broker(self.project / 'runtime/broker', initialize=True, reserve=0)
        self.nodes = {}

    def tearDown(self):
        self.temporary.cleanup()

    def register(self, node):
        result, _ = self.broker.handle({'action': 'register', 'automatic': True, 'node_id': node, 'name': node})
        self.nodes[result['device']] = node
        return result['device']

    def call(self, role, action, payload=b'', **fields):
        return self.broker.handle(dict(action=action, device=role, node_id=self.nodes[role], **fields), payload)[0]

    def bind(self, role):
        self.call(role, 'dataset_create', dataset='work', name='Work')
        self.call(role, 'bind', dataset='work', name='Work', path=f'C:\\{role}\\work')

    def publish(self, role, identifier, parent=None):
        content = b'versioned-content'
        digest = hashlib.sha256(content).hexdigest()
        manifest = dict(format='chunksync-v2', id=identifier, dataset='work', path='report.bin',
                        origin=role, parent=parent, kind='PUT', size=len(content), mtime_ns=1,
                        sha256=digest, chunks=[dict(hash=digest, size=len(content))])
        plan = self.call(role, 'prepare', native=True, manifest=manifest)
        for chunk in plan['needed']:
            self.call(role, 'put', content, job=identifier, hash=chunk['hash'], offset=0)
        self.call(role, 'commit', job=identifier)
        return plan, content

    def ack(self, role, plan):
        return self.call(role, 'ack', job=plan['id'], mhash=plan['mhash'])

    def test_fanout_and_late_subscription(self):
        a, b, c = [self.register(node) for node in ('laptop', 'desktop', 'workstation')]
        self.assertEqual((a, b, c), ('A', 'B', 'C'))
        self.assertEqual(self.register('laptop'), a)
        for role in (a, b, c):
            self.bind(role)
        plan, content = self.publish(a, 'version-one')
        self.ack(b, plan)
        self.assertGreater(self.call(a, 'status')['stored_bytes'], 0)
        self.ack(c, plan)
        self.assertEqual(self.call(a, 'status')['stored_bytes'], 0)
        d = self.register('late-device')
        self.bind(d)
        self.assertFalse(self.call(d, 'overview', dataset='work')['heads'][0]['available'])
        repairs = self.call(a, 'repairs', dataset='work')['jobs']
        self.assertEqual([m['id'] for m in repairs], ['version-one'])
        repair = self.call(a, 'repair_prepare', job='version-one')
        for chunk in repair['needed']:
            self.call(a, 'put', content, job='version-one', hash=chunk['hash'], offset=0)
        self.call(a, 'commit', job='version-one')
        self.assertTrue(self.call(d, 'overview', dataset='work')['heads'][0]['available'])
        self.ack(d, plan)
        self.assertEqual(self.call(a, 'status')['stored_bytes'], 0)
        status = self.call(d, 'status')
        self.assertEqual(status['datasets'][0]['publisher'], a)
        self.assertEqual(len(status['devices']), 4)

    def test_no_subscriber_keeps_initial_contents(self):
        a = self.register('publisher')
        self.bind(a)
        plan, _ = self.publish(a, 'initial-version')
        self.ack(a, plan)
        self.assertGreater(self.call(a, 'status')['stored_bytes'], 0)

    def test_registration_capacity_and_version_race(self):
        roles = [self.register(f'node-{i}') for i in range(16)]
        with self.assertRaises(broker_module.SyncError):
            self.register('over-capacity')
        self.assertEqual(len(self.call(roles[0], 'status')['devices']), 16)
        self.bind(roles[0])
        self.publish(roles[0], 'first')
        with self.assertRaises(broker_module.VersionConflict):
            self.publish(roles[0], 'stale-parent')
        self.assertEqual(self.call(roles[0], 'overview', dataset='work')['heads'][0]['manifest']['id'], 'first')


class Bootstrap(unittest.TestCase):
    def test_privilege_boundary_with_fake_package_manager(self):
        bash = None
        if os.name == 'nt':
            candidate = Path(os.environ.get('ProgramFiles', 'C:/Program Files')) / 'Git/bin/bash.exe'
            bash = str(candidate) if candidate.is_file() else None
        if not bash:
            bash = shutil.which('bash')
        if not bash:
            self.skipTest('A shell is needed for isolated bootstrap simulation')
        source = (Path(__file__).resolve().parents[1] / 'src/platform/ssh_client.c').read_text(encoding='utf-8')
        declaration = re.search(r'static const char setup\[\]=(.*?);\n    int rc;', source, re.S).group(1)
        command = ''.join(ast.literal_eval(token) for token in re.findall(r'"(?:\\.|[^"\\])*"', declaration))
        with tempfile.TemporaryDirectory(prefix='nova-bootstrap-') as directory:
            root = Path(directory)
            def script(name, body):
                path = root / name
                path.write_text('#!/bin/sh\n' + body + '\n', encoding='utf-8', newline='\n')
                path.chmod(0o700)
            script('id', 'echo "$NOVA_TEST_UID"')
            script('python3', 'if [ "$1" = -c ]; then test -f "$NOVA_TEST_INSTALLED"; else IFS= read -r line; printf "%s" "$line"; fi')
            script('apt-get', 'echo "$*" >> "$NOVA_TEST_CALLS"; : > "$NOVA_TEST_INSTALLED"')
            env = dict(os.environ, NOVA_TEST_UID='1000', NOVA_FAKE_BIN=str(root),
                       NOVA_TEST_INSTALLED=str(root / 'installed'), NOVA_TEST_CALLS=str(root / 'calls'))
            prefix = 'nova_test_bin=$(cygpath -u "$NOVA_FAKE_BIN"); ' if os.name == 'nt' else 'nova_test_bin="$NOVA_FAKE_BIN"; '
            wrapper = prefix + 'PATH="$nova_test_bin:/usr/bin:/bin"; NOVA_TEST_INSTALLED="$nova_test_bin/installed"; NOVA_TEST_CALLS="$nova_test_bin/calls"; export PATH NOVA_TEST_INSTALLED NOVA_TEST_CALLS; ' + command
            denied = subprocess.run([bash, '-c', wrapper], env=env, input=b'trusted-source\n', capture_output=True, timeout=15)
            self.assertEqual(denied.returncode, 126, denied.stderr)
            self.assertFalse((root / 'calls').exists())
            env['NOVA_TEST_UID'] = '0'
            installed = subprocess.run([bash, '-c', wrapper], env=env, input=b'trusted-source\n', capture_output=True, timeout=15)
            self.assertEqual(installed.returncode, 0, installed.stderr)
            self.assertEqual(installed.stdout, b'trusted-source')
            self.assertEqual((root / 'calls').read_text().splitlines(), ['update', 'install -y python3'])


if __name__ == '__main__':
    unittest.main()
