"""Optional loopback-only protocol fixture. Requires Paramiko; never uses real servers.
Run: python tests/ssh_fixture.py build/ssh-client-test.exe
"""
import errno
import io
import logging
import base64
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path
import paramiko
from cryptography.hazmat.primitives.asymmetric import ed25519
from cryptography.hazmat.primitives.serialization import Encoding, PrivateFormat, PublicFormat, BestAvailableEncryption

class Session(paramiko.ServerInterface):
    allowed_key = b''
    def check_auth_password(self, username, password):
        return paramiko.AUTH_SUCCESSFUL if (username, password) == ('fixture', 'fixture-password') else paramiko.AUTH_FAILED
    def get_allowed_auths(self, username):
        return 'password,publickey'
    def check_auth_publickey(self, username, key):
        return paramiko.AUTH_SUCCESSFUL if username == 'fixture' and key.asbytes() == self.allowed_key else paramiko.AUTH_FAILED
    def check_channel_request(self, kind, chanid):
        return paramiko.OPEN_SUCCEEDED if kind == 'session' else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED
    def check_channel_exec_request(self, channel, command):
        expected = b'sh -c \'IFS= read -r nova_dir || exit 125; nova_dir=$(printf "%b_" "$nova_dir") || exit 125; nova_dir=${nova_dir%_}; CDPATH= cd "$nova_dir" || exit 125; exec sh -s\''
        if command != expected:
            return False
        def execute():
            try:
                data = b''
                while True:
                    part = channel.recv(8192)
                    if not part:
                        break
                    data += part
                encoded, script = data.split(b'\n', 1)
                assert len(encoded) % 5 == 0
                directory = bytes(int(encoded[i+2:i+5], 8) for i in range(0, len(encoded), 5))
                script = script.strip()
                if script == b'pwd':
                    channel.sendall(directory + b'\n')
                elif script == b'fail':
                    channel.sendall_stderr(b'fixture error\n')
                    channel.send_exit_status(7)
                    channel.close()
                    return
                elif script == b'large':
                    channel.sendall(b'x' * 90000)
                elif script == b'flood':
                    while not channel.closed:
                        channel.sendall(b'z' * 8192)
                    return
                elif script == b'wait':
                    for _ in range(100):
                        if channel.closed:
                            return
                        time.sleep(.05)
                else:
                    raise ValueError('Unexpected fixture command')
                channel.send_exit_status(0)
                channel.close()
            except (OSError, EOFError):
                pass
        threading.Thread(target=execute, daemon=True).start()
        return True

class FileSystem(paramiko.SFTPServerInterface):
    def __init__(self, server, *args, root, **kwargs):
        super().__init__(server, *args, **kwargs)
        self.root = Path(root).resolve()
    def path(self, path):
        result = (self.root / path.lstrip('/')).resolve()
        if result != self.root and self.root not in result.parents:
            raise PermissionError(errno.EACCES, 'Outside fixture')
        return result
    def list_folder(self, path):
        try:
            entries = []
            for child in self.path(path).iterdir():
                value = paramiko.SFTPAttributes.from_stat(child.lstat())
                value.filename = child.name
                entries.append(value)
            return entries
        except OSError as error:
            return paramiko.SFTPServer.convert_errno(error.errno)
    def stat(self, path):
        try:
            return paramiko.SFTPAttributes.from_stat(self.path(path).stat())
        except OSError as error:
            return paramiko.SFTPServer.convert_errno(error.errno)
    lstat = stat
    def canonicalize(self, path):
        return '/' + str(self.path(path).relative_to(self.root)).replace('\\', '/').strip('.')
    def open(self, path, flags, attr):
        try:
            descriptor = os.open(self.path(path), flags | getattr(os, 'O_BINARY', 0), 0o600)
            mode = 'r+b' if flags & os.O_RDWR else 'wb' if flags & os.O_WRONLY else 'rb'
            file = os.fdopen(descriptor, mode)
            handle = paramiko.SFTPHandle(flags)
            handle.readfile = file
            handle.writefile = file
            return handle
        except OSError as error:
            return paramiko.SFTPServer.convert_errno(error.errno)
    def mkdir(self, path, attr):
        return self.mutate(lambda: self.path(path).mkdir())
    def rmdir(self, path):
        return self.mutate(lambda: self.path(path).rmdir())
    def remove(self, path):
        return self.mutate(lambda: self.path(path).unlink())
    def rename(self, old, new):
        return self.mutate(lambda: self.path(old).rename(self.path(new)))
    @staticmethod
    def mutate(action):
        try:
            action()
            return paramiko.SFTP_OK
        except OSError as error:
            return paramiko.SFTPServer.convert_errno(error.errno)

def main():
    key = paramiko.RSAKey.generate(2048)
    transports = []
    with tempfile.TemporaryDirectory(prefix='nova-ssh-fixture-') as directory:
        log = io.StringIO()
        handler = logging.StreamHandler(log)
        logging.getLogger('paramiko').setLevel(logging.DEBUG)
        logging.getLogger('paramiko').addHandler(handler)
        remote = Path(directory, 'remote')
        local = Path(directory, 'local')
        remote.mkdir()
        local.mkdir()
        private_key = ed25519.Ed25519PrivateKey.generate()
        key_file = Path(directory, '临时密钥')
        key_file.write_bytes(private_key.private_bytes(Encoding.PEM, PrivateFormat.OpenSSH, BestAvailableEncryption(b'fixture-key-passphrase')))
        Session.allowed_key = base64.b64decode(private_key.public_key().public_bytes(Encoding.OpenSSH, PublicFormat.OpenSSH).split()[1])
        Path(remote, 'fixture.txt').write_text('isolated fixture', encoding='utf-8')
        with socket.socket() as listener, socket.socket() as refused:
            refused.bind(('127.0.0.1', 0))
            refused_port = refused.getsockname()[1]
            listener.bind(('127.0.0.1', 0))
            listener.listen(8)
            listener.settimeout(.2)
            port = listener.getsockname()[1]
            stopping = threading.Event()
            def serve_connection(sock):
                transport = paramiko.Transport(sock)
                transports.append(transport)
                transport.add_server_key(key)
                transport.set_subsystem_handler('sftp', paramiko.SFTPServer, FileSystem, root=remote)
                try:
                    transport.start_server(server=Session())
                    while transport.is_active() and not stopping.is_set():
                        time.sleep(.02)
                except (EOFError, OSError, paramiko.SSHException):
                    pass
                finally:
                    transport.close()
            def serve():
                while not stopping.is_set():
                    try:
                        client, _ = listener.accept()
                    except socket.timeout:
                        continue
                    threading.Thread(target=serve_connection, args=(client,), daemon=True).start()
            thread = threading.Thread(target=serve, daemon=True)
            thread.start()
            try:
                result = subprocess.run([str(Path(sys.argv[1]).resolve()), str(port), str(local), str(key_file), str(refused_port)], timeout=90).returncode
                if result:
                    print('\n'.join(log.getvalue().splitlines()[-24:]))
                return result
            finally:
                stopping.set()
                for transport in transports:
                    transport.close()
                thread.join(2)
if __name__ == '__main__':
    sys.exit(main())
