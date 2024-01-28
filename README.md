# Raspberry Pi Pico W Bluetooth Low Energy

## BT Server
- Gets temperature data from Pico's onboard thermometer
- Provides Generic Attribute Profile (GATT)
Environmental Sensing service with Temperature Celsius characteristic,
enabling clients to connect to it (connection is exclusive)
- If the client has enabled notifications, they are being sent every second
- When no client is connected, acts as a peripheral role
in Generic Access Profile (GAP) and periodically sends out advertising packets that have the temperature embedded.

## BT Client
- Scans devices within reach and looks for Environmental sensing service
being provided
- When found, it connects to the server and looks for 
Temperature Celsius characteristic
- When found, it subscribes for notifications.

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

## Listen to Pico
Prints from Pico can be listened to on the terminal
```bash
sudo minicom -b 115200 -o -D /dev/ttyACM1
```

## Resources
- [Getting started with Pico](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf)
- [Pico C SDK Documentation](https://www.raspberrypi.com/documentation/pico-sdk/)
- [Pico W Bluetooth](https://datasheets.raspberrypi.com/picow/connecting-to-the-internet-with-pico-w.pdf)
- [BTStack Documentation](https://bluekitchen-gmbh.com/btstack)
- [BTStack Repo](https://github.com/bluekitchen/btstack)
