#include <stdio.h>

#include "btstack.h"

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"


/* Beat interval in milliseconds */
#define BEAT 1000


/* I am receiving notifications */
static bool notifications_connected = false;
/* Handle to notification listener */
static gatt_client_notification_t notification_listener;
/* Handle to server connection */
static hci_con_handle_t connection;

/* HCI event packet handler */
static btstack_packet_callback_registration_t hci_event_callback_registration;

/* State machine */
typedef enum {
    OFF,
    SCANNING,
    CONNECTING,
    GETTING_SERVICE,
    GETTING_CHARACTERISTIC,
    CONNECTING_NOTIFICATIONS,
    CONNECTED
} state_t;
static state_t state = OFF;

/* Stored Service */
static gatt_client_service_t service;
/* Stored Characteristic */
static gatt_client_characteristic_t characteristic;


/* Function called on an incoming GATT event */
static void gatt_handler(uint8_t packet_type, uint16_t channel, uint8_t *event, uint16_t size) {
    UNUSED(packet_type);
    UNUSED(channel);
    UNUSED(size);

    uint8_t event_type = hci_event_packet_get_type(event);

    if (state == GETTING_SERVICE && event_type == GATT_EVENT_SERVICE_QUERY_RESULT) {
        /* Service discovered, store it (there should be exactly one env service) */
        printf("Store service.\n");
        gatt_event_service_query_result_get_service(event, &service);

    } else if (state == GETTING_SERVICE && event_type == GATT_EVENT_QUERY_COMPLETE) {
        /* End of service discovery, discover ORG_BLUETOOTH_CHARACTERISTIC_TEMPERATURE_CELSIUS characteristic */
        printf("Discover ORG_BLUETOOTH_CHARACTERISTIC_TEMPERATURE_CELSIUS characteristic.\n");
        state = GETTING_CHARACTERISTIC;
        gatt_client_discover_characteristics_for_service_by_uuid16(gatt_handler, connection, &service, ORG_BLUETOOTH_CHARACTERISTIC_TEMPERATURE_CELSIUS);

    } else if (state == GETTING_CHARACTERISTIC && event_type == GATT_EVENT_CHARACTERISTIC_QUERY_RESULT) {
        /* Characteristic discovered, store it (there should be exactly one temp characteristic) */
        printf("Store characteristic.\n");
        gatt_event_characteristic_query_result_get_characteristic(event, &characteristic);

    } else if (state == GETTING_CHARACTERISTIC && event_type == GATT_EVENT_QUERY_COMPLETE) {
        /* End of characteristic discovery, register for notifications */
        printf("Register for notifications.\n");
        notifications_connected = true;
        state = CONNECTING_NOTIFICATIONS;
        gatt_client_listen_for_characteristic_value_updates(&notification_listener, gatt_handler, connection, &characteristic);
        gatt_client_write_client_characteristic_configuration(gatt_handler, connection, &characteristic, GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION);

    } else if (state == CONNECTING_NOTIFICATIONS && event_type == GATT_EVENT_QUERY_COMPLETE) {
        /* Notifications connected */
        printf("Notifications connected.\n");
        state = CONNECTED;

    } else if (state == CONNECTED && event_type == GATT_EVENT_NOTIFICATION) {
        /* Notification received */
        const uint8_t *val = gatt_event_notification_get_value(event);
        int length = gatt_event_notification_get_value_length(event);

        if (length == 2) {
            float temp = little_endian_read_16(val, 0) / 100.0;
            printf("Temperature: %.2f°C\n", temp);
        } else {
            printf("Cannot read temperature (length %d).\n", length);
        }
    }
}

/* Function called on an incoming HCI event */
static void event_handler(uint8_t packet_type, uint16_t channel, uint8_t *event, uint16_t size) {
    UNUSED(size);
    UNUSED(packet_type);
    UNUSED(channel);

    uint8_t event_type = hci_event_packet_get_type(event);
    if (event_type == BTSTACK_EVENT_STATE) {
        if (btstack_event_state_get_state(event) != HCI_STATE_WORKING) {
            state = OFF;
            return;
        } else {
            /* BTStack is up and working */
            bd_addr_t local_addr;
            gap_local_bd_addr(local_addr);
            printf("BTstack up and running on %s.\n", bd_addr_to_str(local_addr));

            /* Start scanning */
            printf("Starting scan.\n");
            state = SCANNING;
            gap_set_scan_parameters(0, 0x0030, 0x0030);
            gap_start_scan();
        }

    } else if (event_type == GAP_EVENT_ADVERTISING_REPORT
               && state == SCANNING) {
        /* Got advertising packet */

        /* Discover if this server contains environmental service */
        const uint8_t *data = gap_event_advertising_report_get_data(event);
        uint8_t length = gap_event_advertising_report_get_data_length(event);
        ad_context_t context;

        for (ad_iterator_init(&context, length, data); ad_iterator_has_more(&context) ;ad_iterator_next(&context)) {
            uint8_t data_type = ad_iterator_get_data_type(&context);
            uint8_t data_size = ad_iterator_get_data_len(&context);
            const uint8_t *data = ad_iterator_get_data(&context);

            if (data_type == BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS) {
                for (int i = 0; i < data_size; i += 2) {
                    uint16_t service = little_endian_read_16(data, i);
                    if (service == ORG_BLUETOOTH_SERVICE_ENVIRONMENTAL_SENSING) {
                        goto env_service_found;
                    }
                }
            }
        }
        /* ORG_BLUETOOTH_SERVICE_ENVIRONMENTAL_SENSING was not found, return */
        return;

    env_service_found:
        /* Get server address and type */
        bd_addr_t server_addr;
        gap_event_advertising_report_get_address(event, server_addr);
        bd_addr_type_t server_addr_type = gap_event_advertising_report_get_address_type(event);

        /* Connect to the server */
        gap_stop_scan();
        state = CONNECTING;
        printf("Connecting to server: %s.\n", bd_addr_to_str(server_addr));
        gap_connect(server_addr, server_addr_type);

    } else if (event_type == HCI_EVENT_LE_META
               && state == CONNECTING
               && hci_event_le_meta_get_subevent_code(event) == HCI_SUBEVENT_LE_CONNECTION_COMPLETE) {
        /* Connected to the server */
        connection = hci_subevent_le_connection_complete_get_connection_handle(event);
        printf("Connected.\n");

        /* Discover ORG_BLUETOOTH_SERVICE_ENVIRONMENTAL_SENSING service */
        printf("Discover ORG_BLUETOOTH_SERVICE_ENVIRONMENTAL_SENSING service.\n");
        state = GETTING_SERVICE;
        gatt_client_discover_primary_services_by_uuid16(gatt_handler, connection, ORG_BLUETOOTH_SERVICE_ENVIRONMENTAL_SENSING);

    } else if (event_type == HCI_EVENT_DISCONNECTION_COMPLETE) {
        /* Disconnected */
        printf("Disconnected.\n");
        connection = HCI_CON_HANDLE_INVALID;
        notifications_connected = false;

        /* Start scanning */
        printf("Starting scan.\n");
        state = SCANNING;
        gap_set_scan_parameters(0, 0x0030, 0x0030);
        gap_start_scan();
    }
}

/* Function called on beat */
static void beat(struct btstack_timer_source *bts) {
    /* Blink the LED */
    static bool led = true;
    led = notifications_connected ? true : !led;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led);

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

    sleep_ms(3000);

    /* initialize BTStack */
    l2cap_init();
    sm_init();

    /* setup client */
    gatt_client_init();

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
