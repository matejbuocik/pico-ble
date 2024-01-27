# Raspberry Pi Pico Bluetooth Low Energy

## How to compile
```bash
mkdir build
cd build
export PICO_SDK_PATH=~/pico/pico-sdk  # Or where pico-sdk is cloned
cmake ..
make
```

## How to run
Files `bt_server.uf2` and `bt_client.uf2` were created.  
Hold BOOTSEL button on Pi Pico while connecting it to the PC.  
Then copy one of the files to it.
