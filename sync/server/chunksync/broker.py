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
"""


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
        if initialize:
            with self.connect() as db:
                db.executescript(SCHEMA)

    def connect(self, write=False):
        return database(self.db, write=write, max_bytes=self.config["metadata_limit"])

    def authenticate(self, db, request):
        role = request.get("device")
        if role not in ("A", "B"):
            raise SyncError("请先登记为设备 A 或 B")
        found = db.execute("SELECT node_id FROM devices WHERE role=?", (role,)).fetchone()
        if not found or found["node_id"] != request.get("node_id"):
            raise SyncError("设备身份与登记记录不匹配，请登记或选择另一设备位")
        return role

    def register(self, request):
        role = request.get("device")
        node = checked_id(request.get("node_id"))
        name = request.get("name", "").strip()
        if role not in ("A", "B") or not 1 <= len(name) <= 100:
            raise SyncError("请填写设备名称和 A/B 设备位")
        with self.connect(True) as db:
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
                    raise SyncError("两端基线不一致，请先接收远端更改；本机内容已保留")
                other = "B" if role == "A" else "A"
                unique = {c["hash"]: c["size"] for c in manifest["chunks"]}
                needed = []
                extra = 0
                for chunk, size in unique.items():
                    available = db.execute("SELECT size FROM inventory WHERE device=? AND hash=?", (other,chunk)).fetchone()
                    obj = db.execute("SELECT size FROM objects WHERE hash=?", (chunk,)).fetchone()
                    if available and available[0] != size or obj and obj[0] != size:
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
            if not job or job["origin"] != role or job["state"] != "STAGING":
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
                    raise SyncError("发布前基线发生变化，请先处理两端冲突")
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
            if db.execute("SELECT count(*) FROM receipts WHERE job=?", (job["id"],)).fetchone()[0] == 2:
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
                return {"id":dataset,"name":name},b""
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
                        "datasets":[dict(r) for r in db.execute("SELECT * FROM datasets ORDER BY name")],
                        "bindings":[dict(r) for r in db.execute("SELECT * FROM bindings ORDER BY dataset,device")],
                        "jobs":{r[0]:r[1] for r in db.execute("SELECT state,count(*) FROM jobs GROUP BY state")},
                        "allocated_bytes":db.execute("SELECT coalesce(sum(size),0) FROM objects").fetchone()[0],
                        "stored_bytes":db.execute("SELECT coalesce(sum(progress),0) FROM objects").fetchone()[0],
                        "quota_bytes":self.config["quota"],"disk_available_bytes":shutil.disk_usage(self.root).free},b""
        raise SyncError("不支持的请求")
