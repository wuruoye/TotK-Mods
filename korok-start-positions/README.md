# 呀哈哈起点坐标补丁 1.0.0

适用于《塞尔达传说 王国之泪》1.4.3。将 800 个普通呀哈哈和 100 组搬运任务的地图标记定位到开始操作的位置。

追花指向第一朵花，限时光圈和跳跃挑战指向起点树桩，搬运任务指向需要搬运的呀哈哈，滚石指向原始石头，移动光点指向机关设定的初始路线节点，风车射击指向风车，通天术指向下方触发区域。补石、供奉和积木拼图等按各自操作对象定位；多个等效入口保留一个固定入口。

## 需要准备

- 原版 1.4.3 RomFS 文件夹，包含 `Banc/MainField/HiddenKorok/MainField.hiddenkorok.byml.zs` 和 `Pack/ZsDic.pack.zs`。游戏文件由使用者自行提供。
- 能显示未完成呀哈哈、并使用原生呀哈哈地图坐标表的地图显示 Mod。本补丁只调整坐标，不单独解锁未完成标记，也不包含地图显示模块或 WiiXLaunch 前置。
- 运行生成工具需要 Windows 10/11 x64 和 .NET Framework 4.8，无需安装 Python，无需自行编译。

## 安装

1. 下载并解压 `korok-start-positions-totk143-v1.0.0-windows.zip`，保留 EXE、DLL 和许可文件在同一目录。
2. 双击 `Korok-Start-Setup.exe`，选择原版 1.4.3 RomFS，再选择一个空文件夹作为生成位置，点击“生成 Mod 文件夹”。输入文件保持原样。
3. 保存并关闭游戏，将生成的 `Korok Start Positions 1.4.3` 文件夹复制到游戏的 Mod 目录，并在模拟器的游戏属性中启用。
4. 重启游戏，打开地图。已有呀哈哈完成标志继续由地图显示 Mod 使用。

如果已有 Mod 覆盖同名地图坐标表，需要先处理冲突，不要同时启用两份坐标表。其他 Mod 如只改世界 Actor、地图布局或显示代码，仍需按其兼容说明组合使用。

## 范围与限制

本补丁不修改世界机关、NPC、存档结构和奖励。移动光点与搬运任务使用固定的原始入口，不跟随运行中的目标移动。供奉、补石等任务在部分完成后仍显示同一个固定入口。

同名坐标表必须与本补丁兼容。已核对的组合为 1.4.3、Map Markers 1.2.3 和 Map Vertical Guidance 0.3.3；其他地图显示 Mod、其他游戏版本及其他模拟器未逐一确认。实际设备和全部机关的长期游玩、存读档仍有待进一步验证。

生成的文件可复制到支持该 RomFS 覆盖格式的环境；生成工具本身仅支持 Windows x64。卸载时保存并关闭游戏，再移除 `Korok Start Positions 1.4.3` 文件夹。

## 源码与许可

自制生成工具采用 MIT，署名 wuruoye。Zstandard 1.5.7 组件采用 BSD，通知随包保留。原始游戏内容及第三方地图显示 Mod 的权利归各自权利人；本包不授予其再分发权限。

源码包包含 `Setup.cs`、`starts.patch` 和 `build.ps1`。在 Windows PowerShell 中运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Output .\build\Korok-Start-Setup.exe
```

将同版本安装包中的 `libzstd.dll` 放在生成的 EXE 旁即可运行。Zstandard 上游源码见 [Zstandard 1.5.7](https://github.com/facebook/zstd/tree/v1.5.7)。

生成工具也支持命令行：

```text
Korok-Start-Setup.exe --generate <RomFS文件夹> <空输出文件夹>
```
