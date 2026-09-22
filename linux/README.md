# tuyaopen-core linux 目标

## 构建

```bash
cd examples/switch_demo
cmake -B build
cmake --build build -j$(nproc)
./build/switch_demo
```

凭据可选：`-DTUYA_PRODUCT_ID=.. -DTUYA_OPENSDK_UUID=.. -DTUYA_OPENSDK_AUTHKEY=..`，
不传则用 `src/tuya_config.h` 的 `#ifndef` 默认值或 `tuya_config_secrets.h`。

## Kconfig / menuconfig 配置（可选）

```bash
pip3 install --user kconfiglib        # 一次性
cd <本目录>

KCONFIG_CONFIG=./.config menuconfig   # 交互配置
KCONFIG_CONFIG=./.config olddefconfig # 全默认生成
```

开关符号来自 `Kconfig`（rsource 子模块 `tuyaopen-core/Kconfig`，与 CMake 开关
一一对应、默认值一致）。生成的 `.config` 会被示例工程自动读取
（`CONFIG_xxx=y` 归一化为 ON），链路：

```
menuconfig → .config → demo CMake 读取 → core 的 tuyaopen-core.cmake 注入宏
```

优先级：**`-D` 命令行 > `.config` > 工程内置默认**。
注意：一旦存在 `.config`，一切以它为准（工程内置默认不再兜底），
删除 `.config` 可回到内置默认。`.config` 不入库（.gitignore）。

不开 menuconfig 的手工等价物：`-DCONFIG_ENABLE_WIFI=ON`，
或直接改 `port/include/common/tuya_kconfig.h` 里注释的开关（`-D` 优先）。

## 更新 core 版本

```bash
cd tuyaopen-core && git fetch && git checkout <tag或commit> && cd ..
git add tuyaopen-core && git commit -m "bump tuyaopen-core"
```
