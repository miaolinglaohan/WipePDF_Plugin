# WipePDF Acrobat Plugin (清印 - Acrobat 增强插件版)

基于 **Adobe Acrobat Core API** 原生开发的专业 PDF 顽固水印与透明超链清除插件。

---

## 一、为什么选择 Acrobat 插件版？

| 特性 | 独立版 (`WipePDF.exe`) | Acrobat 插件版 (`WipePDF.api`) |
| :--- | :--- | :--- |
| **文件体积** | ~27 MB（自带 MuPDF + Qt6） | **~300 KB - 600 KB**（极度轻量！） |
| **PDF 引擎** | 第三方开源引擎 (MuPDF) | **Adobe 官方原生引擎** (最强兼容性) |
| **交互体验** | 独立窗口预览、点选 | **无缝融入 Acrobat 菜单与工具栏** |
| **撤销/重做** | 自研单步撤销 | **原生支持 Acrobat Ctrl+Z 历史回退** |
| **排版保真度** | 依赖外部重写 | **原生 PDE 对象级修剪，100% 原始保真** |

---

## 二、功能特性

- **透明文字超链检测与消除**：识别 `ca == 0` 或半透明的诱导性超链文字，一键消除。
- **全页灰色 Pattern 纹理清除**：精准剔除页面背景平铺的花式背景水印。
- **底部推广条带清除**：定位页脚处的宣传网址和下载提示。
- **非破坏性编辑**：直接操作 `PDEContent` 图元树，只删水印图元，正文绝不挖空、不留白块。

---

## 三、快速构建与安装

### 1. 构建环境要求
- Windows 10 / 11 64-bit
- Visual Studio 2022 / BuildTools (已配置)
- CMake 3.20+ 与 Ninja

### 2. 一键编译
在项目根目录下运行：
```powershell
.\build.ps1 -Arch x64
```
编译成功后，将在 `build_x64/` 目录下生成 `WipePDF.api`（体积仅数百 KB）。

### 3. 安装到 Acrobat
将生成的 `WipePDF.api` 复制到 Adobe Acrobat 安装目录下的 `plug_ins` 文件夹中：
```
C:\Program Files\Adobe\Acrobat DC\Acrobat\plug_ins\WipePDF.api
```
或运行自带脚本：
```powershell
.\scripts\install_plugin.ps1
```

启动 Adobe Acrobat，即可在顶部菜单和编辑工具中看到 **WipePDF** 专属功能项！
