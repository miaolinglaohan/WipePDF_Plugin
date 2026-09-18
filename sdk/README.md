# Adobe Acrobat SDK 配置说明

本项目支持两种编译模式：

### 1. 独立/模拟模式 (Mock Mode)
- 无须安装 Acrobat SDK 即可直接编译出动态库原型并进行单元测试。

### 2. 官方 Acrobat SDK 完整模式 (Official SDK Mode)
- 访问 Adobe 官方开发者网站下载 Acrobat SDK (包含 `Headers/` 目录)：
  https://opensource.adobe.com/dc-acrobat-sdk-docs/acrobatsdk/
- 将 SDK 解压后，将其中的 `Headers` 文件夹放置在当前 `sdk/` 目录下（或设置环境变量 `ACROBAT_SDK_DIR` 指向 SDK 路径）：
  ```
  sdk/
    └── Headers/
          ├── PIHeaders.h
          └── SDK/
                └── PIMain.c
  ```
- 重新运行 `.\build.ps1` 即可自动链接完整 Acrobat Core API。
