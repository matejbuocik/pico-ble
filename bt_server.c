#include <stdio.h>
#include <string.h>

#include "btstack.h"

#include "pico/btstack_cyw43.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "temp.h"
#include "temp_service.h"


/* Beat interval in milliseconds */
#define BEAT 1000


/* Advertising packet payload
 * Great example https://docs.silabs.com/bluetooth/4.0/general/adv-and-scanning/bluetooth-adv-data-basics
 * Types defined in https://github.com/bluekitchen/btstack/blob/272986f17af35a67815ab20897f6c91e710322a4/src/bluetooth_data_types.h */
static uint8_t adv_payload[] = {
    /* Flags:
     * supports Bluetooth LE only in general discovery mode. Any nearby device can discover it by scanning.
     * Bluetooth Basic Rate/Enhanced Data Rate (BR/EDT) is not supported.*/
    0x02, BLUETOOTH_DATA_TYPE_FLAGS, 0x06,

    /* Name: My Pico Server */
    0x0F, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME, 'M', 'y', ' ', 'P', 'i', 'c', 'o', ' ', 'S', 'e', 'r', 'v', 'e', 'r',

    /* Services available 
     * - 0x181a Environmental Sensing se2rvice 
     * Numbers at https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Assigned_Numbers/out/en/Assigned_Numbers.pdf?v=1706353890835 */
    0x03, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS, 0x1a, 0x18,

    /* Temperature embedded into advertising packet */   
    0x03, BLUETOOTH_DATA_TYPE_MANUFACTURER_SPECIFIC_DATA, 0x00, 0x00, 
};
static const uint8_t adv_payload_length = sizeof(adv_payload);


/* Two byte value of temperature multiplied by 100 */
static uint16_t temperature;
/* Get temp, print it, update global var `temperature` and embed in advertising packet */
void get_temp_wrapper() {
    float temp = get_temp();
    printf("Temperature: %.2f°C\n", temp);
    temperature = temp * 100;

    // BLE works little endian
    adv_payload[adv_payload_length - 2] = (uint8_t) (temperature & 0xff);
    adv_payload[adv_payload_length - 1] = (uint8_t) (temperature >> 8);
}

/* Notifications callback */
static btstack_context_callback_registration_t temp_callback;
/* Handle to client connection */
static hci_con_handle_t connection;
/* Notifications enabled */
static bool notifications_on = false;

/* DB attribute database created by compile-gatt.py */
extern uint8_t const profile_data[];

/* Event packet handler */
static btstack_packet_callback_registration_t hci_event_callback_registration;


/* Function called on CAN SEND NOW event */
static void temp_can_send_now(void *ctx){
	hci_con_handle_t con_handle = (hci_con_handle_t) (uintptr_t) ctx;
	att_server_notify(con_handle, ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_TEMPERATURE_CELSIUS_01_VALUE_HANDLE, (uint8_t*)&temperature, sizeof(temperature));
}

/* Function called on a read of characteristic */
uint16_t att_read_callback(hci_con_handle_t con_handle, uint16_t att_handle, uint16_t offset, uint8_t *buffer, uint16_t buffer_size) {
    UNUSED(con_handle);

    /* Temperature_Celsius characteristic is being read */
    if (att_handle == ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_TEMPERATURE_CELSIUS_01_VALUE_HANDLE){
        /* Handle read */
        return att_read_callback_handle_blob((const uint8_t *)&temperature, sizeof(temperature), offset, buffer, buffer_size);
    }

    return 0;  // Number of bytes copied
}

/* Function called on a write of characteristic */
int att_write_callback(hci_con_handle_t con_handle, uint16_t att_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size) {
    UNUSED(buffer_size);
    UNUSED(offset);
    UNUSED(transaction_mode);
      
    /* Temperature_Celsius configuration characteristic is being written to */
    if (att_handle == ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_TEMPERATURE_CELSIUS_01_CLIENT_CONFIGURATION_HANDLE) {
        /* Read 16 bit little endian value from buffer from position 0 */
        /* Configuration of notification, set in https://github.com/bluekitchen/btstack/blob/272986f17af35a67815ab20897f6c91e710322a4/src/bluetooth.h#L834 */
        notifications_on = little_endian_read_16(buffer, 0) == GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION;

        if (notifications_on) {
            connection = con_handle;
            /* Request emission of ATT_EVENT_CAN_SEND_NOW as soon as possible */
            att_server_request_can_send_now_event(connection);
        }
    }

    return 0;  // Number of bytes copied 
}

/* Function called on an incoming HCI event */
static void event_handler(uint8_t packet_type, uint16_t channel, uint8_t *event, uint16_t size) {
    UNUSED(channel);
    UNUSED(packet_type);
    UNUSED(size);

    uint8_t event_type = hci_event_packet_get_type(event);
    if (event_type == BTSTACK_EVENT_STATE) {
        /* BTStack is up and working */
        if (btstack_event_state_get_state(event) != HCI_STATE_WORKING) {
            return;
        }

        get_temp_wrapper();

        bd_addr_t local_addr;
        gap_local_bd_addr(local_addr);
        printf("BTstack up and running on %s.\n", bd_addr_to_str(local_addr));

        bd_addr_t null_address;
        memset(null_address, 0, BD_ADDR_LEN);
        gap_advertisements_set_params(0x0030, 0x0030, 0, 0, null_address, 0x07, 0x00);

        assert(adv_payload_length < 32);  // Max size of the advertising payload
        gap_advertisements_set_data(adv_payload_length, adv_payload);
        gap_advertisements_enable(1);
    } else if (event_type == BTSTACK_EVENT_NR_CONNECTIONS_CHANGED) {
        uint8_t nr_connections = event[2];
        if (nr_connections == 1) {
            printf("Connection!\n");
        } else if (nr_connections == 0) {
            printf("Disconnected!\n");
            notifications_on = false;
        }
    }
}

/* Function called on beat */
static void beat(struct btstack_timer_source *bts) {
    /* Blink the LED */
    static bool led = true;
    led = !led;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led);

    /* Update temperature */
    get_temp_wrapper();

    /* Send notifications */
    if (notifications_on) {
        temp_callback.callback = &temp_can_send_now;
        temp_callback.context = (void*) (uintptr_t) connection;
        att_server_register_can_send_now_callback(&temp_callback, connection);
        /* Request emission of ATT_EVENT_CAN_SEND_NOW as soon as possible */
        att_server_request_can_send_now_event(connection);
    }

    /* Restart timer */
    btstack_run_loop_set_timer(bts, BEAT);
    btstack_run_loop_add_timer(bts);
}

int main() {
    /* setup Pico */
    stdio_init_all();

    /* setup Wireless chip */
    if (cyw43_arch_init()) {
        printf("Error - Initialize Wireless chip\n");
        return 1;
    }

    /* start temperature sensor */
    start_temp();

    /* initialize BTStack */
    l2cap_init();
    sm_init();

    /* setup server */
    att_server_init(profile_data, att_read_callback, att_write_callback);

    /* inform about BTstack state */
    hci_event_callback_registration.callback = &event_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    /* setup beats */
    static btstack_timer_source_t metronome;
    metronome.process = &beat;
    btstack_run_loop_set_timer(&metronome, BEAT);
    btstack_run_loop_add_timer(&metronome);

    /* unleash the beast (turn bluetooth on) */
    hci_power_control(HCI_POWER_ON);
    btstack_run_loop_execute();
}
