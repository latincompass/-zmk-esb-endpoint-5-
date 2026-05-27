# 本地构建指南

由于 GitHub Actions 的 Zephyr SDK 下载和 west update 频繁超时/失败，
建议在本地开发机上构建固件。

## 前置条件

安装 Zephyr 开发环境（仅需一次）：
```bash
# 1. 安装依赖
sudo apt install python3-pip cmake ninja-build wget xz-utils

# 2. 安装 west
pip3 install west

# 3. 下载 Zephyr SDK
cd /opt
wget https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.16.8/zephyr-sdk-0.16.8_linux-x86_64_minimal.tar.xz
tar xf zephyr-sdk-0.16.8_linux-x86_64_minimal.tar.xz
cd zephyr-sdk-0.16.8 && ./setup.sh -t arm-zephyr-eabi -h
```

## 构建接收器固件 (dongle)

```bash
mkdir -p /tmp/ws && cd /tmp/ws
cat > west.yml << 'EOF'
manifest:
  defaults: { remote: zephyr }
  remotes:
    - name: zephyr
      url-base: https://github.com/zephyrproject-rtos
  projects:
    - name: zephyr
      revision: v3.7.0
      west-commands: scripts/west-commands.yml
    - name: hal_nordic
      revision: master
      path: modules/hal/nordic
  self: { path: dongle }
EOF
west init -l .
west update --fetch=smart -o=--depth=1
rm -rf dongle && cp -r /path/to/this/repo/dongle dongle
cd dongle && west build -b nrf52833dk_nrf52833
# 输出: build/zephyr/zephyr.hex
```

## 构建键盘固件

```bash
mkdir -p /tmp/ws2 && cd /tmp/ws2
cat > west.yml << 'EOF'
manifest:
  defaults: { remote: zmkfirmware }
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
    - name: zephyr
      url-base: https://github.com/zephyrproject-rtos
  projects:
    - name: zmk
      revision: main
    - name: zephyr
      remote: zephyr
      revision: v3.7.0
      west-commands: scripts/west-commands.yml
    - name: hal_nordic
      remote: zephyr
      revision: master
      path: modules/hal/nordic
  self: { path: config }
EOF
west init -l .
west update --fetch=smart -o=--depth=1
cp -r /path/to/this/repo modules/esb-ep
echo "CONFIG_ZMK_ESB_ENDPOINT=y" > config/nrf52833dk_nrf52833.conf
cd zmk/app
west build -b nrf52833dk_nrf52833 -d /tmp/ws2/build \
  -- -DZMK_CONFIG=/tmp/ws2/config \
     -DZMK_EXTRA_MODULES=/tmp/ws2/modules/esb-ep
# 输出: /tmp/ws2/build/zephyr/zephyr.hex
```
