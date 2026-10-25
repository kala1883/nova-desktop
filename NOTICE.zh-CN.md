# 权益与声明

版权所有 © 2026 Jie。除下方列出的第三方材料外，NOVA Desktop 的源代码、
文档和项目资源均按 [MIT License](LICENSE) 开放使用。

## 名称与项目身份

MIT 协议允许复制、修改和再分发，但不代表修改版是官方版本，也不代表原作者
为修改版背书。分支或衍生版本应明确标注重要修改，避免用户误认其来源。

## 第三方材料

- 项目在 `third_party/sqlite` 中包含并静态链接 SQLite 3.53.4。SQLite 已贡献
  至公有领域；来源和文件哈希见 `third_party/sqlite/README.md`。
- libssh2 1.11.1 以 BSD-3-Clause 协议随源码提供并静态链接；OpenSSL libcrypto
  以 Apache-2.0 协议从构建工具链静态链接。完整许可证见随包提供的
  `THIRD_PARTY_NOTICES.txt`。
- libvterm 0.3.3 按 MIT 协议随源码提供并静态链接，用于原生 SSH 终端。
  来源与哈希见 `third_party/libvterm/README.md`，许可证及 wcwidth 声明随包提供。
- zlib 以 zlib 协议从工具链静态链接，用于同步载荷压缩；完整许可证随包提供。
- Windows、Win32、PowerShell 等名称是 Microsoft Corporation 的商标。
  NOVA Desktop 与 Microsoft 不存在隶属、合作或背书关系。
- 项目仅使用 Q-Dir 一词描述用户熟悉的多窗格文件管理工作流。NOVA Desktop
  为独立实现，与 Q-Dir 及其发布者不存在隶属、合作或背书关系。

## 用户内容与隐私

NOVA Desktop 在本地 `config/nova.json` 保存偏好、已保存任务、工作区元数据
和文件管理会话：仓库内运行使用仓库的 config 目录，独立发布包使用 exe 旁的 config 目录。
旧 AppData SQLite/INI 文件只在首次迁移时读取，原文件保留。
添加项目只记录路径，不会复制、移动或上传文件，也不会主张
用户文件的任何权益。项目不包含统计分析或广告 SDK。用户启用的同步会将内容
暂存到所选 SSH 服务器，接收确认后回收块；原生管理窗口负责登记、配对和自动传输。
身份、配对与块日志保存在被 Git 忽略的 local.json 和 config/sync/，本机历史块保留。
主 JSON 可以纳入 Git；查看或发布它会包含已保存的名称、路径与命令文本。
NOVA 仅为用户请求的 SSH 操作或启用的同步配对传输文件。用户明确发起的 SSH/SFTP 操作向所选服务器传输所选
文件并执行请求的命令；密码、私钥口令和私钥路径不写入 JSON，主机指纹保存在
当前用户的 Windows 凭据管理器。保存/连接会在该用户的本机凭据管理器中记住登录
密码/口令和私钥路径，不复制私钥内容；清除登录信息或移除连接会删除对应登录信息。
主机指纹仍独立保存。

英文版本见 [NOTICE.md](NOTICE.md)。

临时界面状态另外保存在被 Git 忽略的 config/local.json。
