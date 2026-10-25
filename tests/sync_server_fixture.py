"""Actual bootstrap subprocesses behind loopback-only Paramiko, with loss injection.

No saved credentials, real server, user workspace or installed Python is needed
by the product. Python/Paramiko are isolated test dependencies only.
"""
import base64
import hashlib
import json
import logging
from pathlib import Path
import socket
import sqlite3
import subprocess
import sys
import tempfile
import threading
import importlib

import paramiko

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "sync/server"))
wire = importlib.import_module("chunksync.wire")
logging.getLogger("paramiko").setLevel(logging.CRITICAL)


def run(executable):
    with tempfile.TemporaryDirectory(prefix="nova server '") as temporary:
        root = Path(temporary)
        occupied = root / "occupied"
        occupied.mkdir()
        (occupied / "keep.txt").write_text("unrelated user data", encoding="utf-8")
        key = paramiko.ECDSAKey.generate()
        pin = hashlib.sha256(key.asbytes()).hexdigest()
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        listener.settimeout(.2)
        stop = threading.Event()
        transports = []
        normal_drop = threading.Event()
        migration_drop = threading.Event()
        commit_drop = threading.Event()
        rollback_drop = threading.Event()
        partial_jobs = set()
        failures = []

        def mapped(value):
            for remote, local in (("/fixture source'", "source"), ("/fixture destination'", "destination"), ("/fixture rollback'", "rollback"), ("/fixture occupied'", "occupied")):
                if value == remote:
                    return root / local
                if value == remote + "/runtime/broker":
                    return root / local / "runtime/broker"
            raise AssertionError("unexpected fixture path: " + value)

        def session(channel, command):
            process = None
            try:
                reader, writer = channel.makefile("rb"), channel.makefile("wb")
                admin = command.startswith(b"python3 -B -u -c 'import sys,base64;")
                if admin:
                    encoded = reader.readline()
                    source = base64.b64decode(encoded)
                    assert source == (ROOT / "sync/server/chunksync/control.py").read_bytes(), "bootstrap must be embedded source"
                    process = subprocess.Popen([sys.executable, "-B", "-u", "-c", command.decode().split("-c '", 1)[1][:-1]], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                    process.stdin.write(encoded)
                    process.stdin.flush()
                else:
                    assert command.startswith(b"sh -c 'IFS= read -r nova_p")
                    def decode(line):
                        assert line.endswith(b"\n") and (len(line)-1) % 5 == 0
                        return bytes(int(line[i+2:i+5], 8) for i in range(0, len(line)-1, 5)).decode()
                    project, queue = mapped(decode(reader.readline())), mapped(decode(reader.readline()))
                    assert project.resolve().is_relative_to(root) and queue.resolve().is_relative_to(root)
                    process = subprocess.Popen([sys.executable, "-B", "-u", str(project / "sync_server.py"), "--root", str(queue)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                while not channel.closed:
                    request, payload, _ = wire.read_message(reader)
                    if admin:
                        request["project"] = str(mapped(request["project"]))
                        request["root"] = str(mapped(request["root"]))
                    if request.get("action") == "prepare" and request["manifest"]["path"] == "partial.bin":
                        partial_jobs.add(request["manifest"]["id"])
                    wire.write_message(process.stdin, request, payload)
                    response, data, _ = wire.read_message(process.stdout)
                    if admin and request.get("action") == "import_put" and Path(request["project"]).name == "rollback" and payload and not rollback_drop.is_set():
                        rollback_drop.set()
                        channel.close()
                        return
                    if not admin and request.get("action") == "put" and request.get("job") in partial_jobs and payload and not normal_drop.is_set():
                        normal_drop.set()
                        channel.close()
                        return
                    if admin and request.get("action") == "import_put" and request.get("size", 0) > 128*1024 and payload and not migration_drop.is_set():
                        migration_drop.set()
                        channel.close()
                        return
                    if admin and request.get("action") == "export_commit" and response.get("ok") and not commit_drop.is_set():
                        commit_drop.set()
                        channel.close()
                        return
                    wire.write_message(writer, response, data)
            except (EOFError, OSError):
                if process and process.poll() not in (None, 0):
                    failures.append(process.stderr.read().decode(errors="replace")[-3000:])
            except BaseException as error:
                failures.append(repr(error))
            finally:
                if process:
                    process.stdin.close()
                    try:
                        process.wait(10)
                    except subprocess.TimeoutExpired:
                        process.kill()  # Only this disposable fixture child.
                        process.wait()
                    if process.returncode:
                        failures.append(process.stderr.read().decode(errors="replace")[-3000:])
                channel.close()

        class SSH(paramiko.ServerInterface):
            def check_auth_password(self, user, password):
                return paramiko.AUTH_SUCCESSFUL if (user, password) == ("fixture", "fixture-password") else paramiko.AUTH_FAILED
            def get_allowed_auths(self, user):
                return "password"
            def check_channel_request(self, kind, channel_id):
                return paramiko.OPEN_SUCCEEDED if kind == "session" else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED
            def check_channel_exec_request(self, channel, command):
                threading.Thread(target=session, args=(channel, command), daemon=True).start()
                return True

        def connection(sock):
            transport = paramiko.Transport(sock)
            transports.append(transport)
            transport.add_server_key(key)
            transport.set_subsystem_handler("sftp", paramiko.SFTPServer, paramiko.SFTPServerInterface)
            try:
                transport.start_server(server=SSH())
                while transport.is_active() and not stop.wait(.1):
                    pass
            except (paramiko.SSHException, OSError):
                pass
            finally:
                transport.close()

        def accept():
            while not stop.is_set():
                try:
                    sock, _ = listener.accept()
                except socket.timeout:
                    continue
                except OSError:
                    break
                threading.Thread(target=connection, args=(sock,), daemon=True).start()

        thread = threading.Thread(target=accept, daemon=True)
        thread.start()
        try:
            result = subprocess.run([str(Path(executable).resolve()), str(listener.getsockname()[1]), pin, str(root)], timeout=360)
            assert not failures, failures
            assert result.returncode == 0, result.returncode
            assert normal_drop.is_set() and migration_drop.is_set() and commit_drop.is_set()
            assert rollback_drop.is_set()
            assert (occupied / "keep.txt").read_text(encoding="utf-8") == "unrelated user data"
            assert not (occupied / ".chunksync-project.json").exists()
            source = root / "source/runtime/broker"
            target = root / "destination/runtime/broker"
            assert json.loads((source / ".nova-control.json").read_text())["phase"] == "REDIRECT"
            assert json.loads((target / ".nova-control.json").read_text())["phase"] == "ACTIVE"
            with sqlite3.connect(source / "queue.sqlite3") as old, sqlite3.connect(target / "queue.sqlite3") as new:
                assert old.execute("SELECT count(*) FROM devices").fetchone()[0] == new.execute("SELECT count(*) FROM devices").fetchone()[0] == 2
                assert set(old.execute("SELECT id,manifest,mhash FROM jobs")) <= set(new.execute("SELECT id,manifest,mhash FROM jobs"))
                assert new.execute("SELECT count(*) FROM jobs WHERE state!='DELIVERED'").fetchone()[0] == 0
                assert new.execute("SELECT count(*) FROM objects").fetchone()[0] == 0
            old.close()
            new.close()
            print("PASS loopback server administration: real embedded bootstrap subprocesses, no manual deployment script, full record preservation and migration safety")
        finally:
            stop.set()
            listener.close()
            for transport in transports:
                transport.close()
            thread.join(3)


if __name__ == "__main__":
    run(sys.argv[1])
