# Contributing / 参与贡献

Issues and pull requests are welcome. Please keep changes focused, explain the
user-visible behavior, and include test coverage when practical.

欢迎提交 Issue 和 Pull Request。请尽量保持改动聚焦，说明用户可见行为，并在
可行时补充测试。

Before opening a pull request / 提交 PR 前：

```powershell
.\build.bat
.\tests\run.bat
.\tests\storage.bat
.\tests\files.bat
```

Use UTF-8 for documentation and UTF-16-aware Win32 APIs for user-visible text.
New UI copy should provide both Simplified Chinese and English translations.

文档使用 UTF-8；用户可见文本使用支持 UTF-16 的 Win32 API。新增界面文案应同时
提供简体中文和英文版本。
