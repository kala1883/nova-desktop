# Security policy / 安全策略

Please do not disclose a suspected security vulnerability in a public issue.
Use GitHub's private vulnerability reporting feature on this repository. Include
the affected version, reproduction steps, and expected impact.

如发现疑似安全漏洞，请勿直接在公开 Issue 中披露。请使用本仓库 GitHub 的私密
漏洞报告功能，并注明受影响版本、复现步骤和预期影响。

Only the latest source revision is actively supported. This project is provided
without warranty under the MIT License; responsible reports are nevertheless
reviewed and addressed on a best-effort basis.

目前仅主动支持最新源码版本。本项目按 MIT 协议不附带任何保证，但维护者仍会尽力
审阅并处理负责任披露的安全问题。

File-manager SSH/SFTP connections are explicitly user-initiated. Verify host fingerprints
independently on first use; changed fingerprints block connections. Passwords and
private-key passphrases and key paths can be saved by Save/Connect in the current
user's Windows Credential Manager with local-machine persistence. Key contents
are not copied. Login records are bound to the stable server ID, host, port and
case-sensitive username; changed endpoints never silently reuse recalled secrets.
Forget login and profile removal delete that server's saved login. Shared JSON
and portable packages contain no login secrets or key paths. Trusted host pins
remain separate entries in the same user's Windows Credential Manager. Remote deletion is permanent and
requires confirmation. Cancellation or disconnect does not guarantee that a
remote command stopped; uncertain writes and commands must not be retried blindly.

文件管理器中的 SSH/SFTP 连接由用户明确发起，首次使用应独立核验主机指纹，指纹变化时阻止连接。
保存/连接会将密码、私钥口令与私钥路径保存到当前 Windows 用户的本机凭据管理器，
不复制私钥内容。登录信息绑定稳定服务器 ID、主机、端口及区分大小写的用户名，
连接目标改变时不会静默复用回填凭据。清除登录信息或移除服务器会删除对应本机登录信息。
共享 JSON 和便携包不含登录凭据或私钥路径；主机信任记录仍作为独立条目保存。
远程删除为需确认的永久删除；取消或断线不保证服务器命令停止，不应盲目重试结果未知的写操作或命令。

Native synchronization is an explicit user feature. Saving a pair sends binding
metadata only; Sync now or an enabled automatic pair transfers its files to the
selected SSH server. Node IDs, filenames, folder bindings and version metadata
are visible to that server's SSH account; chunks use SSH transport encryption,
not end-to-end encryption at rest. Use a trusted single-user server and restrict
its filesystem access. `config/local.json` and `config/sync/` are private state,
not portable enrollment data. Keep both devices' durable cache after server GC.
Incoming paths reject traversal/reparse points; hashes, lineage checks and
atomic reconstruction protect ordinary files but do not form a security boundary
against another process concurrently changing filesystem links. Received deletes
require opt-in and go through Windows Shell. See `sync/README.md` for limits.

原生同步由用户显式启用，自动传输只用于已启用的配对。服务器能读取内容块、文件名、
绑定路径与版本元数据；SSH 保护传输，不提供云端静态端到端加密。使用可信单用户
服务器并限制其目录权限。本机身份和块缓存不随包共享，不可复制 A 身份来登记 B。
路径检查拒绝穿越和重解析点，但不承诺抵抗其他进程并发替换文件系统链接的攻击。

The native deployment source is embedded in the EXE. Server paths are framed
stdin data, never shell syntax. Deployments refuse unknown nonempty directories
and preserve existing queues; migrations refuse nonempty destinations, validate
node identity/instance ancestry and freeze old writes before snapshot/cutover.
Old data is retained. A migration gives the selected SSH account code and queue
write access already authorized by its login; it does not grant OS elevation,
open ports or copy SSH secrets between computers. A trusted single-user server
remains required. Keep both servers accessible during recovery, and never remove
the local migration journal before resolving an interrupted cutover.
内置部署通过现有 SSH 权限运行，不自动提权、开端口或复制登录凭据。迁移检查身份与
版本链，先冻结旧端再提交切换，旧数据保留；断点未完成前保留两端和本机恢复记录。

Managed Linux project directories, migration staging and initialized/activated
queues use owner-only directory permissions (0700); unrelated directories are
refused before their permissions or contents change.
受管理的 Linux 项目、迁移暂存目录与初始化/启用的队列使用仅属主可访问的 0700 权限；
未知目录会在修改内容或权限之前拒绝处理。
