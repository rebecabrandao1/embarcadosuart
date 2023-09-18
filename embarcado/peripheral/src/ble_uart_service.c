#include <ble_uart_service.h>



#define BLE_UART_SERVICE_TX_CHAR_OFFSET    3

static ble_uart_service_rx_callback rx_callback = NULL;

static struct bt_uuid_128 ble_uart_svc_uuid = BT_UUID_INIT_128(
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0x00, 0x89, 0x67, 0x43, 0x21);

static struct bt_uuid_128 ble_uart_rx_uuid = BT_UUID_INIT_128(
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0x01, 0x89, 0x67, 0x43, 0x21);

static struct bt_uuid_128 ble_uart_tx_uuid = BT_UUID_INIT_128(
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0x02, 0x89, 0x67, 0x43, 0x21);

static ssize_t ble_uart_rx_from_host (struct bt_conn *conn,
					 const struct bt_gatt_attr *attr,
					 const void *buf, uint16_t  len,
					 uint16_t   offset, uint8_t   flags) {
	(void)conn;
	(void)attr;
	(void)offset;
	(void)flags;

	uint8_t string[len+1];
    for(int i = 0; i < len;i++)
		string[i] = *((char*)buf+i);

    string[len] = '\0';
    printk("\nReceived data: %s\n",string);

    if(rx_callback) {
        rx_callback((const uint8_t *)buf,len);
    }

    return len;
}

static void ble_uart_ccc_changed(const struct bt_gatt_attr *attr, uint16_t  value)
{
	const bool notif_enabled = (value == BT_GATT_CCC_NOTIFY);
	
	printk("Notifications %s\n", notif_enabled ? "enabled" : "disabled");
}

static struct bt_gatt_attr ble_uart_attr_table[] = {
	BT_GATT_PRIMARY_SERVICE(&ble_uart_svc_uuid),
	BT_GATT_CHARACTERISTIC(&ble_uart_rx_uuid.uuid, BT_GATT_CHRC_WRITE_WITHOUT_RESP ,
			BT_GATT_PERM_WRITE, NULL, ble_uart_rx_from_host, NULL),
	BT_GATT_CHARACTERISTIC(&ble_uart_tx_uuid.uuid, BT_GATT_CHRC_NOTIFY,
			BT_GATT_PERM_NONE, NULL, NULL, NULL),
	BT_GATT_CCC(ble_uart_ccc_changed, BT_GATT_PERM_WRITE),
};

static struct bt_gatt_service ble_uart_service = BT_GATT_SERVICE(ble_uart_attr_table);


int ble_uart_service_register(const ble_uart_service_rx_callback callback) {
    rx_callback = callback;
	return 	bt_gatt_service_register(&ble_uart_service);
}

int service_transmit(const uint8_t  *buffer, size_t len) {

	if(!buffer || !len) {
		return -1;
	}
	printk("Sending notification\n");
    struct bt_conn *conn = ble_get_connection_ref();
	  uint8_t string[len+1];
    for(int i = 0; i < len;i++)
	{
        if(buffer[i] <= 'z' && buffer[i] >= 'a')
            string[i] = buffer[i]-('a'-'A');
        else
            string[i] = buffer[i];
    }
    string[len] = '\0';
    if(conn) {
       return ( bt_gatt_notify(conn,
                            &ble_uart_attr_table[BLE_UART_SERVICE_TX_CHAR_OFFSET],
                            string,
                            len));
    } else {
        return -1;
    }
}