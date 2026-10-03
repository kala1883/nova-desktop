# 权益与声明

版权所有 © 2026 Jie。除下方列出的第三方材料外，NOVA Desktop 的源代码、
文档和项目资源均按 [MIT License](LICENSE) 开放使用。

## 名称与项目身份

MIT 协议允许复制、修改和再分发，但不代表修改版是官方版本，也不代表原作者
为修改版背书。分支或衍生版本应明确标注重要修改，避免用户误认其来源。

## 第三方材料

- 项目在 `third_party/sqlite` 中包含并静态链接 SQLite 3.53.4。SQLite 已贡献
  至公有领域；来源和文件哈希见 `third_party/sqlite/README.md`。
- Windows、Win32、PowerShell 等名称是 Microsoft Corporation 的商标。
  NOVA Desktop 与 Microsoft 不存在隶属、合作或背书关系。
- 项目仅使用 Q-Dir 一词描述用户熟悉的多窗格文件管理工作流。NOVA Desktop
  为独立实现，与 Q-Dir 及其发布者不存在隶属、合作或背书关系。

## 用户内容与隐私

NOVA Desktop 在本地 `config/nova.json` 保存偏好、已保存任务、工作区元数据
和文件管理会话：仓库内运行使用仓库的 config 目录，独立发布包使用 exe 旁的 config 目录。
旧 AppData SQLite/INI 文件只在首次迁移时读取，原文件保留。
添加项目只记录路径，不会复制、移动或上传文件，也不会主张
用户文件的任何权益。项目不包含统计分析、广告 SDK 或云同步服务。
主 JSON 可以纳入 Git；查看或发布它会包含已保存的名称、路径与命令文本。
NOVA 本身不上传这些数据。

英文版本见 [NOTICE.md](NOTICE.md)。

临时界面状态另外保存在被 Git 忽略的 config/local.json。
