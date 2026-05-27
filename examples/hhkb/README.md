# HHKB Professional 2 配列示例 — nRF52833 + ESB 端点

本目录提供 **HHKB Professional 2 / Professional BT** 标准配列的 ZMK 配置示例。

## 配列图示

```
┌───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┐
│Esc│ 1 │ 2 │ 3 │ 4 │ 5 │ 6 │ 7 │ 8 │ 9 │ 0 │ - │ = │ \ │ `~│BSp│
│Pwr│F1 │F2 │F3 │F4 │F5 │F6 │F7 │F8 │F9 │F10│F11│F12│Ins│   │Del│
├───┼───┼───┼───┼───┼───┼───┼───┼───┼───┼───┼───┼───┼───┼───┤
│Tab│ Q │ W │ E │ R │ T │ Y │ U │ I │ O │ P │ [ │ ] │ \ │Del│
│Cps│   │   │   │   │   │   │   │PSc│Slk│Pau│   │   │   │Clr│
├───┼───┼───┼───┼───┼───┼───┼───┼───┼───┼───┼───┼───┼───┴───┘
│Ctl│ A │ S │ D │ F │ G │ H │ J │ K │ L │ ; │ ' │Return  │
│◇On│Vl+│Mut│Ejc│   │   │   │   │ * │ / │Hm │PgU│        │
├───┼───┼───┼───┼───┼───┼───┼───┼───┼───┼───┼───┼───┬───┘
│Sft│ Z │ X │ C │ V │ B │ N │ M │ , │ . │ / │Sft│Fn │
│   │   │   │   │   │   │   │   │ < │ > │ ? │   │   │
├───┼───┼───┼───┼───┴───┴───┴───┴───┼───┼───┼───┤
│Alt│Cmd│Opt│       Space(6.25u)     │Cmd│Alt│Opt│
└───┴───┴───┴───────────────────────┴───┴───┴───┘
```

## 文件说明

| 文件 | 说明 |
|------|------|
| `hhkb.keymap` | 三层 keymap（Default + Fn + System） |
| `hhkb_nrf52833.overlay` | 5x15 矩阵 DTS overlay + ESB 端点 |
| `hhkb_nrf52833.conf` | Kconfig 配置 |

## 使用说明

1. **修改 GPIO 引脚**：`hhkb_nrf52833.overlay` 中的 `row-gpios` 和 `col-gpios` 必须匹配你的实际 PCB 原理图
2. **复制文件**：将三个文件放入你的 ZMK 键盘配置项目（如 `zmk-config/config/`）
3. **添加模块**：在 `west.yml` 中添加此 ESB 模块作为依赖
4. **构建**：用 west 构建你的键盘固件

## 层说明

| 层 | 触发 | 功能 |
|----|------|------|
| Default | 无 | 标准 HHKB 配列 |
| Fn (Layer 1) | 按住 Fn | F1-F12、方向键、Home/End、PgUp/PgDn |
| System (Layer 2) | 可绑定 | 蓝牙设备切换等 |
