# switch_demo（ESP32 / esp-idf）

## 结构

```
esp32/
  port/                     # ESP32 TKL（骨架，实现待补，见 port/README.md）
  examples/switch_demo/
    CMakeLists.txt          # esp-idf 工程入口（components/ 自动发现）
    components/
      tuyaopen-port/        # 包装组件：REQUIRES tuyaopen-core + add_subdirectory(../port)
    main/
      main.c                # 骨架（TKL 补齐前不调用 tuya API，保证可编译）
      idf_component.yml     # component manager 自动下载 core（GitHub）
```

core 经 `idf_component.yml` 自动拉取（仓库根即组件）：

```yaml
dependencies:
  tuyaopen-core:
    git: https://github.com/flyingcys/TuyaOpen-Core.git
```

core 根的 `CMakeLists.txt` 是 esp-idf 组件入口（`idf_component_register` 后
`include(tuyaopen-core.cmake)`）——include 语境不调 `project()`，core 无任何
esp-idf 特判。`components/tuyaopen-port` 的 `REQUIRES tuyaopen-core` 保证 core
先处理（port 向 `tuyaopen-headers` 自注入头路径的前提）。

## 构建（需要 esp-idf 环境）

```bash
. $HOME/esp/esp-idf/export.sh
idf.py set-target esp32
idf.py build               # 首次构建自动下载 core 到 managed_components/
idf.py -p /dev/ttyUSB0 flash monitor
```

## Kconfig 配置

core 自带 `Kconfig.projbuild`（esp-idf 自动收集进根菜单），`idf.py menuconfig`
中直接出现 "tuyaopen-core" 菜单，配置写入 `sdkconfig`：

```
idf.py menuconfig ──▶ sdkconfig ──▶ esp-idf 设 CONFIG_* CMake 变量
                                     └─▶ core 的 TuyaOpenOptions 归一化（y/1→ON）→ 宏注入
```

示例默认值在 `sdkconfig.defaults`（WIRED/QRCODE=y）。

## 说明

- `sdkconfig` 的 `CONFIG_*` 优先于一切内置默认（esp-idf 标准行为）
- 当前 esp32 port 的 TKL 实现待补（优先级清单见 `port/README.md`）
