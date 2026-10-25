# 弓箭轨迹 0.0.13 对应源码

拉弓时显示完整虚线轨迹和落点，支持林克时间及角色碰撞体预览。此包包含自制 Mod、预测对象适配器和原生指令重定位构建工具。

源码不包含原始游戏程序、生成的原生指令组件、第三方 Mod、运行快照、日志、编译器或缓存。构建时从使用者自己的输入中生成对应模块。

## 构建要求

使用 Python 3.12、Zig 0.14.1，以及独立安装的 `capstone`、`keystone-engine`。设置环境变量 `ARROW_ZIG` 为 Zig 可执行文件的绝对路径。

自行准备以下输入：

- `inputs/text.bin`：《王国之泪》1.4.3 main 解压后的 text 段，CRC32 `945314EA`。
- `inputs/host.image`：配套专用 WiiXLaunch 1664 KiB 宿主的解压映像。
- `inputs/ultracam/subsdk3`：UltraCam 3.5 RC1，CRC32 `3989F712`。

运行：

```text
python native/build_native_closure.py --runtime
python mod/build_candidate.py
```

输出为 `mod/build/candidate/Arrow Trajectory 1.4.3/romfs/WiiXLaunch/mods/arrow143.wxlm`，目标 CRC32 为 `56533D6A`；原生组件目标 CRC32 为 `9B19EF1A`。

生成的汇编、头文件和原生组件位于本地构建目录，不要格式化生成文件。使用其他版本输入时，构建检查会拒绝继续。

## 安装和兼容

先准备配套 WiiXLaunch 前置：`subsdk8` CRC32 `25968557`、`subsdk9` CRC32 `A63EC343`；使用装备目录时需配套 CRC32 `D3155777`。本版不能直接用于 `platform143-256-1280k` 公共前置。

关闭游戏后，将生成的 `Arrow Trajectory 1.4.3` 文件夹放入 Mod 目录，替换旧版本，再重启游戏。保留原模块以便回退。已适配的环境为 1.4.3 / Eden；其他环境尚未验证。

普通单发无附材使用原生计算组件；多发和附材仍用估算路径。角色碰撞体边界可能与外观不同；不预测目标未来移动。林克时间支持引导线和落点显示，实际命中精度尚未量化。

自制部分署名 wuruoye，使用 GPL-3.0。游戏原始指令与第三方程序不在自制代码授权范围内，权利说明见 `NOTICE.txt`。
