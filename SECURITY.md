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

SSH/SFTP connections are explicitly user-initiated. Verify host fingerprints
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

SSH/SFTP 连接由用户明确发起，首次使用应独立核验主机指纹，指纹变化时阻止连接。
保存/连接会将密码、私钥口令与私钥路径保存到当前 Windows 用户的本机凭据管理器，
不复制私钥内容。登录信息绑定稳定服务器 ID、主机、端口及区分大小写的用户名，
连接目标改变时不会静默复用回填凭据。清除登录信息或移除服务器会删除对应本机登录信息。
共享 JSON 和便携包不含登录凭据或私钥路径；主机信任记录仍作为独立条目保存。
远程删除为需确认的永久删除；取消或断线不保证服务器命令停止，不应盲目重试结果未知的写操作或命令。
