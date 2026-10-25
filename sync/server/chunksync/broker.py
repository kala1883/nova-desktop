import json
import os
from pathlib import Path
import shutil
import time
from .control import active_queue, control

from .common import (PROJECT, FORMAT, MAX_CHUNK, PACKET, SyncError, atomic_json,
                     canonical, checked_hash, checked_id, database, fsync_dir,
                     hash_file, linked, manifest_id, relative_key)

SCHEMA = """
CREATE TABLE IF NOT EXISTS devices(role TEXT PRIMARY KEY,node_id TEXT NOT NULL,name TEXT NOT NULL,last_seen REAL NOT NULL);
CREATE TABLE IF NOT EXISTS datasets(id TEXT PRIMARY KEY,name TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS bindings(dataset TEXT NOT NULL,device TEXT NOT NULL,path TEXT NOT NULL,name TEXT NOT NULL,updated REAL NOT NULL,PRIMARY KEY(dataset,device));
CREATE TABLE IF NOT EXISTS jobs(seq INTEGER PRIMARY KEY AUTOINCREMENT,id TEXT UNIQUE NOT NULL,dataset TEXT NOT NULL,
 path_key TEXT NOT NULL,origin TEXT NOT NULL,manifest TEXT NOT NULL,mhash TEXT NOT NULL,state TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS heads(dataset TEXT NOT NULL,path_key TEXT NOT NULL,job TEXT NOT NULL,PRIMARY KEY(dataset,path_key));
CREATE TABLE IF NOT EXISTS objects(hash TEXT PRIMARY KEY,size INTEGER NOT NULL,complete INTEGER NOT NULL DEFAULT 0,progress INTEGER NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS job_chunks(job TEXT NOT NULL,hash TEXT NOT NULL,size INTEGER NOT NULL,PRIMARY KEY(job,hash));
CREATE TABLE IF NOT EXISTS needs(job TEXT NOT NULL,hash TEXT NOT NULL,PRIMARY KEY(job,hash));
CREATE TABLE IF NOT EXISTS inventory(device TEXT NOT NULL,hash TEXT NOT NULL,size INTEGER NOT NULL,PRIMARY KEY(device,hash));
CREATE TABLE IF NOT EXISTS receipts(job TEXT NOT NULL,device TEXT NOT NULL,mhash TEXT NOT NULL,PRIMARY KEY(job,device));
CREATE INDEX IF NOT EXISTS jobs_state ON jobs(state,seq);
CREATE INDEX IF NOT EXISTS chunks_hash ON job_chunks(hash);
CREATE INDEX IF NOT EXISTS jobs_path ON jobs(dataset,path_key,seq);
CREATE TABLE IF NOT EXISTS publishers(dataset TEXT PRIMARY KEY,device TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS recipients(job TEXT NOT NULL,device TEXT NOT NULL,PRIMARY KEY(job,device));
"""

DEVICE_ROLES = tuple(chr(ord('A') + i) for i in range(16))


class VersionConflict(SyncError):
    code = 'conflict'


class Broker:
    def __init__(self, root, initialize=False, quota=15 * 2**30, reserve=5 * 2**30):
        self.root = Path(root).absolute()
        allowed = (PROJECT / "runtime").resolve()
        if self.root.resolve() == allowed or not self.root.resolve().is_relative_to(allowed):
            raise SyncError("服务端数据必须位于项目 runtime 的子目录")
        for path in (self.root, *self.root.parents):
            if linked(path):
                raise SyncError("服务端目录不允许链接或 junction")
            if path == PROJECT:
                break
        config = self.root / "broker.json"
        if not config.exists():
            if not initialize:
                raise SyncError("服务端尚未初始化")
            if self.root.exists() and any(self.root.iterdir()):
                raise SyncError("拒绝初始化非空未知目录")
            self.root.mkdir(parents=True, exist_ok=True)
            if os.name != "nt":
                self.root.chmod(0o700)
            atomic_json(config, {"format": FORMAT, "quota": int(quota), "reserve": int(reserve), "metadata_limit": 256 * 2**20})
        self.config = json.loads(config.read_text(encoding="utf-8"))
        if self.config.get("format") != FORMAT:
            raise SyncError("服务端版本不兼容")
        self.chunks = self.root / "chunks"
        self.uploads = self.root / "uploads"
        self.chunks.mkdir(exist_ok=True)
        self.uploads.mkdir(exist_ok=True)
        if linked(self.chunks) or linked(self.uploads):
            raise SyncError("服务端内容目录不能是链接")
        self.db = self.root / "queue.sqlite3"
        # Additive upgrade: preserve legacy identities, versions and receipts.
        self.upgraded = False
        state_file = self.root / '.nova-control.json'
        if initialize and (not state_file.exists() or control(self.root)['phase'] == 'ACTIVE'):
            self.upgrade()

    def upgrade(self):
        with self.connect() as db:
            db.executescript("BEGIN IMMEDIATE;" + SCHEMA + """
                INSERT OR IGNORE INTO publishers SELECT dataset,origin FROM jobs j
                  WHERE seq=(SELECT min(seq) FROM jobs k WHERE k.dataset=j.dataset);
                INSERT OR IGNORE INTO recipients SELECT id,origin FROM jobs;
                INSERT OR IGNORE INTO recipients SELECT j.id,b.device FROM jobs j JOIN bindings b ON b.dataset=j.dataset;
                COMMIT;
            """)
        self.upgraded = True

    def connect(self, write=False):
        return database(self.db, write=write, max_bytes=self.config["metadata_limit"])

    def authenticate(self, db, request):
        role = request.get("device")
        if role not in DEVICE_ROLES:
            raise SyncError("请先注册本设备 / Register this device first")
        found = db.execute("SELECT node_id FROM devices WHERE role=?", (role,)).fetchone()
        if not found or found["node_id"] != request.get("node_id"):
            raise SyncError("设备身份与登记记录不匹配，请登记或选择另一设备位")
        return role

    def register(self, request):
        role = request.get("device")
        node = checked_id(request.get("node_id"))
        name = request.get("name", "").strip()
        if (request.get("automatic") is not True and role not in DEVICE_ROLES) or not 1 <= len(name) <= 100:
            raise SyncError("请填写设备名称 / Enter a device name")
        with self.connect(True) as db:
            if request.get("automatic") is True:
                retained = db.execute("SELECT role FROM devices WHERE node_id=?", (node,)).fetchone()
                occupied = {r[0] for r in db.execute("SELECT role FROM devices")}
                role = retained[0] if retained else next((r for r in DEVICE_ROLES if r not in occupied), None)
            if role not in DEVICE_ROLES:
                raise SyncError("设备已达 16 台上限 / Device limit (16) reached")
            existing = db.execute("SELECT * FROM devices WHERE role=?", (role,)).fetchone()
            if existing and existing["node_id"] != node:
                raise SyncError(f"设备 {role} 已被另一安装登记；请选择另一设备位")
            db.execute("INSERT OR REPLACE INTO devices VALUES (?,?,?,?)", (role, node, name, time.time()))
        return {"registered": True, "device": role, "name": name}

    @staticmethod
    def validate_manifest(manifest):
        if not isinstance(manifest, dict) or manifest.get("format") != FORMAT:
            raise SyncError("清单格式错误")
        checked_id(manifest.get("id"))
        checked_id(manifest.get("dataset"))
        if manifest.get("path_key") != relative_key(manifest.get("path")):
            raise SyncError("清单路径索引不匹配")
        if manifest.get("parent") is not None:
            checked_id(manifest["parent"])
        if manifest.get("kind") not in ("PUT", "DELETE"):
            raise SyncError("操作类型错误")
        if manifest.get("kind") == "DELETE":
            if manifest.get("chunks") != [] or manifest.get("size") != 0 or manifest.get("sha256") is not None:
                raise SyncError("删除标记不应携带内容")
            return
        checked_hash(manifest.get("sha256"))
        chunks = manifest.get("chunks")
        if not isinstance(chunks, list) or len(chunks) > 100000:
            raise SyncError("单文件分块清单超限")
        for chunk in chunks:
            checked_hash(chunk.get("hash"))
            if type(chunk.get("size")) is not int or not 0 < chunk["size"] <= MAX_CHUNK:
                raise SyncError("内容块大小错误")
        if type(manifest.get("size")) is not int or sum(c["size"] for c in chunks) != manifest["size"]:
            raise SyncError("文件总大小校验失败")

    def prepare(self, request):
        manifest = request.get("manifest")
        if request.get("native") is True and isinstance(manifest,dict) and "path_key" not in manifest:
            manifest={**manifest,"path_key":relative_key(manifest.get("path"))}
        self.validate_manifest(manifest)
        encoded = canonical(manifest)
        digest = manifest_id(manifest)
        with self.connect(True) as db:
            role = self.authenticate(db, request)
            if manifest.get("origin") != role:
                raise SyncError("不能发布另一设备的版本")
            if not db.execute("SELECT 1 FROM datasets WHERE id=?", (manifest["dataset"],)).fetchone():
                raise SyncError("同步组不存在")
            found = db.execute("SELECT * FROM jobs WHERE id=?", (manifest["id"],)).fetchone()
            if found:
                if found["manifest"] != encoded:
                    raise SyncError("操作 ID 与已有清单冲突")
            else:
                head = db.execute("SELECT job FROM heads WHERE dataset=? AND path_key=?", (manifest["dataset"],manifest["path_key"])).fetchone()
                if (head[0] if head else None) != manifest["parent"]:
                    raise VersionConflict("版本已变化，请先拉取或选择保留的版本 / Version changed; pull or choose a version")
                others = [r[0] for r in db.execute("SELECT device FROM bindings WHERE dataset=? AND device!=?", (manifest['dataset'], role))]
                unique = {c["hash"]: c["size"] for c in manifest["chunks"]}
                needed = []
                extra = 0
                for chunk, size in unique.items():
                    available = bool(others) and all(db.execute("SELECT 1 FROM inventory WHERE device=? AND hash=? AND size=?", (other,chunk,size)).fetchone() for other in others)
                    obj = db.execute("SELECT size FROM objects WHERE hash=?", (chunk,)).fetchone()
                    if obj and obj[0] != size:
                        raise SyncError("内容哈希的大小记录不一致")
                    if not available:
                        needed.append(chunk)
                        if not obj:
                            extra += size
                allocated = db.execute("SELECT coalesce(sum(size),0) FROM objects").fetchone()[0]
                if allocated + extra > self.config["quota"]:
                    raise SyncError("云端暂存配额不足，未上传版本保留在本机，可稍后续传")
                if shutil.disk_usage(self.root).free < extra + self.config["reserve"]:
                    raise SyncError("云端实际空闲空间不足以保留安全余量")
                db.execute("INSERT INTO jobs(id,dataset,path_key,origin,manifest,mhash,state) VALUES (?,?,?,?,?,?,?)",
                           (manifest["id"],manifest["dataset"],manifest["path_key"],role,encoded,digest,"STAGING"))
                db.execute("INSERT INTO recipients VALUES (?,?)", (manifest['id'],role))
                for other in others:
                    db.execute("INSERT OR IGNORE INTO recipients VALUES (?,?)", (manifest['id'],other))
                for chunk, size in unique.items():
                    db.execute("INSERT INTO job_chunks VALUES (?,?,?)", (manifest["id"],chunk,size))
                    if chunk in needed:
                        db.execute("INSERT OR IGNORE INTO objects(hash,size) VALUES (?,?)", (chunk,size))
                        db.execute("INSERT INTO needs VALUES (?,?)", (manifest["id"],chunk))
            needs = [dict(r) for r in db.execute("SELECT o.hash,o.size,o.complete,o.progress FROM needs n JOIN objects o ON n.hash=o.hash WHERE n.job=?", (manifest["id"],))]
            state = db.execute("SELECT state FROM jobs WHERE id=?", (manifest["id"],)).fetchone()[0]
        needed_hashes = {r["hash"] for r in needs}
        result={"id": manifest["id"], "mhash": digest, "state": state, "needed": needs,
                "receiver_reused_bytes": sum(c["size"] for c in manifest["chunks"] if c["hash"] not in needed_hashes)}
        if request.get("native") is True:
            result["manifest"]=manifest
        return result

    def object_offset(self, db, digest, size):
        target = self.chunks / digest
        if linked(target):
            raise SyncError("无效的服务端对象类型")
        if target.exists():
            if hash_file(target) != (digest,size):
                raise SyncError("云端块校验失败，暂不确认该任务")
            db.execute("UPDATE objects SET complete=1,progress=size WHERE hash=?", (digest,))
            return size
        partial = self.uploads / (digest + ".part")
        if linked(partial):
            raise SyncError("无效的暂存类型")
        offset = partial.stat().st_size if partial.exists() else 0
        if offset > size:
            raise SyncError("断点大小超出内容块")
        db.execute("UPDATE objects SET complete=0,progress=? WHERE hash=?", (offset,digest))
        return offset

    def upload(self, request, payload):
        digest = checked_hash(request.get("hash"))
        with self.connect(True) as db:
            role = self.authenticate(db,request)
            job = db.execute("SELECT * FROM jobs WHERE id=?", (request.get("job"),)).fetchone()
            if not job or job["origin"] != role or job["state"] not in ("STAGING", "READY"):
                raise SyncError("上传任务不在可写状态")
            obj = db.execute("SELECT o.* FROM objects o JOIN needs n ON n.hash=o.hash WHERE n.job=? AND o.hash=?", (job["id"],digest)).fetchone()
            if not obj:
                raise SyncError("该任务不需要上传此内容块")
            offset = self.object_offset(db,digest,obj["size"])
            if not payload:
                return {"offset":offset,"complete":offset == obj["size"]}
            if type(request.get("offset")) is not int or request["offset"] != offset:
                return {"offset":offset,"complete":offset == obj["size"],"retry":True}
            if offset + len(payload) > obj["size"] or len(payload) > PACKET:
                raise SyncError("上传超出块边界")
            if shutil.disk_usage(self.root).free < len(payload) + self.config["reserve"]:
                raise SyncError("云端磁盘安全余量不足，断点已保留")
            partial = self.uploads / (digest + ".part")
            with partial.open("ab") as stream:
                stream.write(payload)
                stream.flush()
                os.fsync(stream.fileno())
            offset += len(payload)
            if offset == obj["size"]:
                if hash_file(partial) != (digest,obj["size"]):
                    partial.unlink()
                    db.execute("UPDATE objects SET progress=0,complete=0 WHERE hash=?", (digest,))
                    return {"offset":0,"complete":False,"retry":True,"hash_mismatch":True}
                os.replace(partial,self.chunks / digest)
                fsync_dir(self.chunks)
            db.execute("UPDATE objects SET progress=?,complete=? WHERE hash=?", (offset,int(offset == obj["size"]),digest))
        return {"offset":offset,"complete":offset == obj["size"]}

    def commit(self, request):
        with self.connect(True) as db:
            role = self.authenticate(db,request)
            job = db.execute("SELECT * FROM jobs WHERE id=?", (request.get("job"),)).fetchone()
            if not job or job["origin"] != role:
                raise SyncError("未知的源端任务")
            manifest = json.loads(job["manifest"])
            if job["state"] == "STAGING":
                head = db.execute("SELECT job FROM heads WHERE dataset=? AND path_key=?", (job["dataset"],job["path_key"])).fetchone()
                if (head[0] if head else None) != manifest["parent"]:
                    raise VersionConflict("发布前版本已变化 / Version changed before publication")
                for obj in db.execute("SELECT o.* FROM needs n JOIN objects o ON n.hash=o.hash WHERE n.job=?", (job["id"],)):
                    if self.object_offset(db,obj["hash"],obj["size"]) != obj["size"]:
                        raise SyncError("仍有内容块尚未完成上传")
                db.execute("UPDATE jobs SET state='READY' WHERE id=?", (job["id"],))
                db.execute("INSERT OR REPLACE INTO heads VALUES (?,?,?)", (job["dataset"],job["path_key"],job["id"]))
                for chunk in manifest["chunks"]:
                    db.execute("INSERT OR IGNORE INTO inventory VALUES (?,?,?)", (role,chunk["hash"],chunk["size"]))
                db.execute("INSERT OR IGNORE INTO receipts VALUES (?,?,?)", (job["id"],role,job["mhash"]))
        return {"published":True,"id":job["id"],"mhash":job["mhash"]}

    def acknowledge(self, request):
        with self.connect(True) as db:
            role = self.authenticate(db,request)
            job = db.execute("SELECT * FROM jobs WHERE id=?", (request.get("job"),)).fetchone()
            if not job or job["state"] not in ("READY","DELIVERED") or job["mhash"] != request.get("mhash"):
                raise SyncError("版本确认与任务清单不匹配")
            for chunk in json.loads(job["manifest"])["chunks"]:
                db.execute("INSERT OR IGNORE INTO inventory VALUES (?,?,?)", (role,chunk["hash"],chunk["size"]))
            db.execute("INSERT OR IGNORE INTO receipts VALUES (?,?,?)", (job["id"],role,job["mhash"]))
            if (db.execute("SELECT count(*) FROM recipients WHERE job=?", (job['id'],)).fetchone()[0] >= 2 and
                not db.execute("SELECT 1 FROM recipients t WHERE t.job=? AND NOT EXISTS(SELECT 1 FROM receipts r WHERE r.job=t.job AND r.device=t.device)", (job['id'],)).fetchone()):
                db.execute("UPDATE jobs SET state='DELIVERED' WHERE id=?", (job["id"],))
        # Receipts MUST commit before filesystem GC in a separate transaction.
        return {"acknowledged":True,"gc":self.gc()}

    def gc(self):
        removed = 0
        with self.connect(True) as db:
            obsolete = db.execute("SELECT o.hash FROM objects o WHERE NOT EXISTS (SELECT 1 FROM job_chunks c JOIN jobs j ON j.id=c.job WHERE c.hash=o.hash AND j.state!='DELIVERED')").fetchall()
            for row in obsolete:
                for path in (self.chunks / row[0],self.uploads / (row[0]+".part")):
                    if path.exists():
                        if linked(path) or not path.is_file():
                            raise SyncError("拒绝清理非普通内容对象")
                        removed += path.stat().st_size
                        path.unlink()
                db.execute("DELETE FROM objects WHERE hash=?", (row[0],))
            fsync_dir(self.chunks)
        return {"removed_bytes":removed}

    def handle(self, request, payload=b""):
        with active_queue(self.root):
            if not self.upgraded:
                self.upgrade()
            return self._handle(request, payload)

    def _handle(self, request, payload=b""):
        action = request.get("action")
        if action == "ping":
            with self.connect() as db:
                return {"version":FORMAT,"quota":self.config["quota"],
                        "devices":[dict(r) for r in db.execute("SELECT * FROM devices ORDER BY role")],
                        "datasets":[dict(r) for r in db.execute("SELECT * FROM datasets ORDER BY name")]}, b""
        if action == "register":
            return self.register(request), b""
        if action == "prepare":
            return self.prepare(request), b""
        if action == "put":
            return self.upload(request,payload), b""
        if action == "commit":
            return self.commit(request), b""
        if action == "ack":
            return self.acknowledge(request), b""
        with self.connect(action in ("dataset_create","bind","unbind","status")) as db:
            role = self.authenticate(db,request)
            if action in ("bind","unbind"):
                dataset=checked_id(request.get("dataset"))
                if not db.execute("SELECT 1 FROM datasets WHERE id=?",(dataset,)).fetchone():
                    raise SyncError("同步组不存在 / Synchronization group not found")
                if action=="unbind":
                    db.execute("DELETE FROM bindings WHERE dataset=? AND device=?",(dataset,role))
                else:
                    path=request.get("path")
                    name=request.get("name","")
                    if not isinstance(path,str) or not 1<=len(path)<2048 or '\0' in path or not isinstance(name,str) or len(name)>120:
                        raise SyncError("目录绑定格式错误 / Invalid folder binding")
                    db.execute("INSERT OR REPLACE INTO bindings VALUES (?,?,?,?,?)",(dataset,role,path,name,time.time()))
                    db.execute("INSERT OR IGNORE INTO recipients SELECT id,? FROM jobs WHERE dataset=?", (role,dataset))
                    db.execute("UPDATE jobs SET state='READY' WHERE dataset=? AND state='DELIVERED' AND NOT EXISTS(SELECT 1 FROM receipts WHERE job=jobs.id AND device=?)", (dataset,role))
                return {"bound":action=="bind","files_deleted":False},b""
            if action == "dataset_create":
                dataset = checked_id(request.get("dataset"))
                name = request.get("name","").strip()
                if not 1 <= len(name) <= 120:
                    raise SyncError("请填写同步组名称")
                old = db.execute("SELECT name FROM datasets WHERE id=?",(dataset,)).fetchone()
                if old and old[0] != name:
                    raise SyncError("同步组标识已经存在")
                if not old and db.execute("SELECT count(*) FROM datasets").fetchone()[0] >= 16:
                    raise SyncError("同步组已达 16 个上限，现有数据保留 / Synchronization group limit (16) reached; existing data preserved")
                db.execute("INSERT OR IGNORE INTO datasets VALUES (?,?)",(dataset,name))
                db.execute("INSERT OR IGNORE INTO publishers VALUES (?,?)", (dataset,role))
                return {"id":dataset,"name":name},b""
            if action == "overview":
                dataset = checked_id(request.get('dataset'))
                rows = db.execute("SELECT j.id,j.manifest,j.mhash,EXISTS(SELECT 1 FROM receipts r WHERE r.job=j.id AND r.device=?) AS received FROM heads h JOIN jobs j ON j.id=h.job WHERE h.dataset=? ORDER BY j.path_key", (role,dataset)).fetchall()
                return {"heads":[{"manifest":json.loads(r['manifest']),"mhash":r['mhash'],"received":bool(r['received']),
                                  "available":not db.execute("SELECT 1 FROM jobs j JOIN job_chunks c ON c.job=j.id WHERE j.dataset=? AND j.path_key=? AND j.state='READY' AND NOT EXISTS(SELECT 1 FROM receipts r WHERE r.job=j.id AND r.device=?) AND NOT EXISTS(SELECT 1 FROM objects o WHERE o.hash=c.hash AND o.complete=1) AND NOT EXISTS(SELECT 1 FROM inventory i WHERE i.device=? AND i.hash=c.hash)", (dataset,json.loads(r['manifest'])['path_key'],role,role)).fetchone()} for r in rows]},b''
            if action == "repairs":
                dataset = checked_id(request.get('dataset'))
                rows = db.execute("SELECT DISTINCT j.manifest FROM jobs j JOIN job_chunks c ON c.job=j.id WHERE j.dataset=? AND j.origin=? AND j.state='READY' AND NOT EXISTS(SELECT 1 FROM objects o WHERE o.hash=c.hash AND o.complete=1) AND EXISTS(SELECT 1 FROM recipients t WHERE t.job=j.id AND t.device!=j.origin AND NOT EXISTS(SELECT 1 FROM receipts r WHERE r.job=t.job AND r.device=t.device) AND NOT EXISTS(SELECT 1 FROM inventory i WHERE i.device=t.device AND i.hash=c.hash))", (dataset,role)).fetchall()
                # Rehydrate late subscribers from durable source caches, without a new version.
                return {"jobs":[json.loads(r[0]) for r in rows]},b''
            if action == "repair_prepare":
                job = db.execute("SELECT * FROM jobs WHERE id=?", (checked_id(request.get('job')),)).fetchone()
                if not job or job['origin'] != role or job['state'] != 'READY':
                    raise SyncError('无法修复此版本 / Cannot repair this version')
                chunks = list(db.execute("SELECT c.hash,c.size FROM job_chunks c WHERE c.job=? AND EXISTS(SELECT 1 FROM recipients t WHERE t.job=c.job AND t.device!=? AND NOT EXISTS(SELECT 1 FROM receipts r WHERE r.job=t.job AND r.device=t.device) AND NOT EXISTS(SELECT 1 FROM inventory i WHERE i.device=t.device AND i.hash=c.hash))", (job['id'],role)))
                extra = sum(c['size'] for c in chunks if not db.execute('SELECT 1 FROM objects WHERE hash=?', (c['hash'],)).fetchone())
                allocated = db.execute('SELECT coalesce(sum(size),0) FROM objects').fetchone()[0]
                if allocated + extra > self.config['quota'] or shutil.disk_usage(self.root).free < extra + self.config['reserve']:
                    raise SyncError('暂存空间不足 / Insufficient staging space')
                for c in chunks:
                    db.execute('INSERT OR IGNORE INTO objects(hash,size) VALUES (?,?)', (c['hash'],c['size']))
                    db.execute('INSERT OR IGNORE INTO needs VALUES (?,?)', (job['id'],c['hash']))
                return {"id":job['id'],"manifest":json.loads(job['manifest']),"state":"STAGING","needed":[dict(r) for r in db.execute('SELECT o.* FROM needs n JOIN objects o ON o.hash=n.hash WHERE n.job=?', (job['id'],))]},b''
            if action == "pending":
                dataset = checked_id(request.get("dataset"))
                rows = db.execute("SELECT j.manifest,j.mhash FROM jobs j WHERE j.dataset=? AND j.state='READY' AND NOT EXISTS(SELECT 1 FROM receipts r WHERE r.job=j.id AND r.device=?) ORDER BY j.seq",(dataset,role)).fetchall()
                return {"jobs":[{"manifest":json.loads(r[0]),"mhash":r[1]} for r in rows]},b""
            if action == "get":
                digest = checked_hash(request.get("hash"))
                job = db.execute("SELECT * FROM jobs WHERE id=?",(request.get("job"),)).fetchone()
                if not job or job["state"] != "READY" or not db.execute("SELECT 1 FROM job_chunks WHERE job=? AND hash=?",(job["id"],digest)).fetchone():
                    raise SyncError("无法读取该任务的内容块")
                path = self.chunks / digest
                if not path.is_file() or linked(path):
                    raise SyncError("所需块未在云端：请保留本机基线缓存，或在源设备上线后修复")
                offset = request.get("offset",0)
                if type(offset) is not int or not 0 <= offset <= path.stat().st_size:
                    raise SyncError("下载断点无效")
                with path.open("rb") as stream:
                    stream.seek(offset)
                    data = stream.read(PACKET)
                return {"offset":offset,"size":path.stat().st_size}, data
            if action == "status":
                db.execute("UPDATE devices SET last_seen=? WHERE role=?",(time.time(),role))
                return {"control":control(self.root) if (self.root/'.nova-control.json').exists() else None,
                        "devices":[dict(r) for r in db.execute("SELECT * FROM devices ORDER BY role")],
                        "datasets":[dict(r) for r in db.execute("SELECT d.*,p.device AS publisher FROM datasets d LEFT JOIN publishers p ON p.dataset=d.id ORDER BY name")],
                        "bindings":[dict(r) for r in db.execute("SELECT * FROM bindings ORDER BY dataset,device")],
                        "jobs":{r[0]:r[1] for r in db.execute("SELECT state,count(*) FROM jobs GROUP BY state")},
                        "allocated_bytes":db.execute("SELECT coalesce(sum(size),0) FROM objects").fetchone()[0],
                        "stored_bytes":db.execute("SELECT coalesce(sum(progress),0) FROM objects").fetchone()[0],
                        "quota_bytes":self.config["quota"],"disk_available_bytes":shutil.disk_usage(self.root).free},b""
        raise SyncError("不支持的请求")
