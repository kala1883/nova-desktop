"""Loopback-only SSH fixture for the native sync client; never uses user servers."""
import hashlib
import importlib
import logging
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading

import paramiko

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"sync/server"))
broker_module=importlib.import_module("chunksync.broker")
wire=importlib.import_module("chunksync.wire")
logging.getLogger("paramiko").setLevel(logging.CRITICAL)


def run(executable):
    with tempfile.TemporaryDirectory(prefix="nova-sync-fixture-") as temporary:
        root=Path(temporary)
        broker_module.PROJECT=root
        broker=broker_module.Broker(root/"runtime/broker",initialize=True,quota=32*2**20,reserve=0)
        key=paramiko.ECDSAKey.generate()
        fingerprint=hashlib.sha256(key.asbytes()).hexdigest()
        listener=socket.socket()
        listener.bind(("127.0.0.1",0))
        listener.listen()
        listener.settimeout(.2)
        stopped=threading.Event()
        upload_drop=threading.Event()
        download_drop=threading.Event()
        failures=[]
        transports=[]
        def protocol(channel):
            try:
                reader=channel.makefile("rb")
                writer=channel.makefile("wb")
                project_line=reader.readline()
                root_line=reader.readline()
                def decode(value):
                    assert value.endswith(b"\n") and (len(value)-1)%5==0
                    return bytes(int(value[i+2:i+5],8) for i in range(0,len(value)-1,5))
                assert decode(project_line)==b"/fixture project'"
                assert decode(root_line)==b"/fixture project'/runtime/broker"
                while not channel.closed:
                    request,payload,_=wire.read_message(reader)
                    try:
                        if request.get("action")=="prepare" and request["manifest"].get("kind")=="PUT":
                            manifest=request["manifest"]
                            content=(root/manifest["origin"]/'files'/manifest['path']).read_bytes()
                            gear=[int.from_bytes(hashlib.sha256(bytes([b])).digest()[:8],'little') for b in range(256)]
                            fingerprint=0;start=0;expected=[]
                            for at,byte in enumerate(content):
                                fingerprint=((fingerprint<<1)+gear[byte])&((1<<64)-1)
                                size=at+1-start
                                if size>=2*2**20 or (size>=128*1024 and fingerprint&((1<<19)-1)==0):
                                    block=content[start:at+1];expected.append({'hash':hashlib.sha256(block).hexdigest(),'size':len(block)});start=at+1;fingerprint=0
                            if start<len(content):
                                block=content[start:];expected.append({'hash':hashlib.sha256(block).hexdigest(),'size':len(block)})
                            assert expected==manifest['chunks'], 'native/Python Gear boundary mismatch'
                            assert hashlib.sha256(content).hexdigest()==manifest['sha256']
                        result,data=broker.handle(request,payload)
                        if request["action"]=="put" and payload and not upload_drop.is_set():
                            upload_drop.set();channel.close();return
                        wire.write_message(writer,{"ok":True,"result":result},data)
                        if request["action"]=="get" and data and not download_drop.is_set():
                            download_drop.set();channel.close();return
                    except Exception as error:
                        wire.write_message(writer,{"ok":False,"error":str(error)})
            except (EOFError,OSError):
                pass
            except BaseException as error:
                failures.append(repr(error))
            finally:
                channel.close()
        class Session(paramiko.ServerInterface):
            def check_auth_password(self,user,password):
                return paramiko.AUTH_SUCCESSFUL if (user,password)==("fixture","fixture-password") else paramiko.AUTH_FAILED
            def get_allowed_auths(self,user):
                return "password"
            def check_channel_request(self,kind,channel_id):
                return paramiko.OPEN_SUCCEEDED if kind=="session" else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED
            def check_channel_exec_request(self,channel,command):
                assert command.startswith(b"sh -c 'IFS= read -r nova_p")
                assert b"fixture" not in command  # All paths are stdin data.
                threading.Thread(target=protocol,args=(channel,),daemon=True).start()
                return True
        def connection(sock):
            transport=paramiko.Transport(sock)
            transports.append(transport)
            transport.add_server_key(key)
            transport.set_subsystem_handler("sftp",paramiko.SFTPServer,paramiko.SFTPServerInterface)
            try:
                transport.start_server(server=Session())
                while transport.is_active() and not stopped.wait(.1):
                    pass
            except (paramiko.SSHException,OSError):
                pass
            finally:
                transport.close()
        def accept():
            while not stopped.is_set():
                try:
                    sock,_=listener.accept()
                except socket.timeout:
                    continue
                except OSError:
                    break
                threading.Thread(target=connection,args=(sock,),daemon=True).start()
        thread=threading.Thread(target=accept,daemon=True)
        thread.start()
        try:
            result=subprocess.run([str(Path(executable).resolve()),"127.0.0.1",str(listener.getsockname()[1]),fingerprint,str(root)],timeout=180)
            assert result.returncode==0,result.returncode
            assert upload_drop.is_set() and download_drop.is_set()
            with broker.connect() as db:
                assert db.execute("SELECT count(*) FROM objects").fetchone()[0]==0
                assert db.execute("SELECT count(*) FROM jobs WHERE state!='DELIVERED'").fetchone()[0]==0
            assert not failures,failures
            for index in range(15):
                broker.handle({"action":"dataset_create","device":"A","node_id":"fixture-A","dataset":f"limit-{index}","name":f"Limit fixture {index}"})
            try:
                broker.handle({"action":"dataset_create","device":"A","node_id":"fixture-A","dataset":"over-limit","name":"Must be rejected"})
            except broker_module.SyncError:
                pass
            else:
                raise AssertionError("Broker exceeded the native manager's group bound")
            with broker.connect() as db:
                assert db.execute("SELECT count(*) FROM datasets").fetchone()[0]==16
            print("PASS broker group capacity refusal without truncation")
            print("PASS isolated native SSH synchronization fixture; no user credentials, folders or cloud endpoints used")
        finally:
            stopped.set()
            listener.close()
            for transport in transports:
                transport.close()
            thread.join(timeout=3)


if __name__=="__main__":
    run(sys.argv[1])
