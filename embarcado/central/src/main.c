/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <errno.h>
#include <kernel.h>
#include <sys/printk.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <sys/byteorder.h>

#define CFLAG(flag) static atomic_t flag = (atomic_t)false
#define SFLAG(flag) (void)atomic_set(&flag, (atomic_t)true)
#define UFLAG(flag) (void)atomic_set(&flag, (atomic_t)false)
#define WFLAG(flag) \
	while (!(bool)atomic_get(&flag)) { \
		(void)k_sleep(K_MSEC(1)); \
	}

#define BT_ATT_FIRST_ATTRIBUTE_HANDLE           0x0001
#define BT_ATT_FIRST_ATTTRIBUTE_HANDLE __DEPRECATED_MACRO BT_ATT_FIRST_ATTRIBUTE_HANDLE
/* 0xffff is defined as the maximum, and thus last, valid attribute handle */
#define BT_ATT_LAST_ATTRIBUTE_HANDLE            0xffff
#define BT_ATT_LAST_ATTTRIBUTE_HANDLE __DEPRECATED_MACRO BT_ATT_LAST_ATTRIBUTE_HANDLE

static void start_scan(void);

static struct bt_conn *default_conn;

static struct bt_uuid_16 discover_uuid = BT_UUID_INIT_16(0);
static struct bt_gatt_discover_params discover_params;
static struct bt_gatt_subscribe_params subscribe_params;
static uint16_t notify_h;

static uint16_t chrc_h = 0x0001;
static struct bt_uuid_128 ble_upper = BT_UUID_INIT_128(
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0x00, 0x89, 0x67, 0x43, 0x21);
static struct bt_uuid_128 ble_receive = BT_UUID_INIT_128(
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0x01, 0x89, 0x67, 0x43, 0x21);
static struct bt_uuid_128 ble_notify = BT_UUID_INIT_128(
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0x02, 0x89, 0x67, 0x43, 0x21);

static struct bt_uuid *primary_uuid = &ble_upper.uuid;

static uint8_t notify_func(struct bt_conn *conn,
			   struct bt_gatt_subscribe_params *params,
			   const void *data, uint16_t length)
{
	
	printk("[NOTIFICATION] data %p length %u\n", data, length);
	uint8_t string[length+1];
	 for(int i = 0; i < length; i++)
        string[i] = *((char*)data+i);
    string[length] = '\0';
    printk("\nReceived data from peripheral: %s\n\n", string);
	data = "";

	return BT_GATT_ITER_CONTINUE;
}


static uint8_t discover(struct bt_conn *conn, const struct bt_gatt_attr *attr, struct bt_gatt_discover_params *params)
{
	int err;

	if (attr == NULL) {
		if (chrc_h == 0)
			printk("Did not discover long_chrc (%x)",chrc_h);
		(void)memset(params, 0, sizeof(*params));


		return BT_GATT_ITER_STOP;
	}

	printk("[ATTRIBUTE] handle %u\n", attr->handle);
	if (params->type == BT_GATT_DISCOVER_PRIMARY && bt_uuid_cmp(params->uuid, &ble_upper.uuid) == 0) 
    {
		printk("Found service\n");
		params->uuid = &ble_receive.uuid;
		params->start_handle = attr->handle + 1;
		params->type = BT_GATT_DISCOVER_CHARACTERISTIC;
		


		err = bt_gatt_discover(conn, params);
		if (err != 0)
			printk("Discover failed (err %d)\n", err);
		 
	} 

	else if (!bt_uuid_cmp(discover_params.uuid, &ble_receive.uuid)) {
        memcpy(&discover_uuid, &ble_notify.uuid, sizeof(discover_uuid));
        discover_params.uuid          = &ble_notify.uuid;
        discover_params.start_handle  = attr->handle + 1;
        discover_params.type          = BT_GATT_DISCOVER_CHARACTERISTIC;
		subscribe_params.value_handle  = bt_gatt_attr_value_handle(attr);
         
        chrc_h                 = bt_gatt_attr_value_handle(attr);
         
        err = bt_gatt_discover(conn, &discover_params);
        if (err) {
            printk("Discover failed (err %d).\n", err);
        }
    }
    else if (!bt_uuid_cmp(discover_params.uuid, &ble_notify.uuid)) {
        memcpy(&discover_uuid, BT_UUID_GATT_CCC, sizeof(discover_uuid));
        discover_params.uuid         = &discover_uuid.uuid;
        discover_params.start_handle = attr->handle + 1;
        discover_params.type         = BT_GATT_DISCOVER_DESCRIPTOR;
        subscribe_params.value_handle = bt_gatt_attr_value_handle(attr);
		notify_h = bt_gatt_attr_value_handle(attr);
		
        err = bt_gatt_discover(conn, &discover_params);
        if (err) {
            printk("Discover failed (err %d).\n", err);
        }
    }
   else {
        subscribe_params.notify     = notify_func;
        subscribe_params.value      = BT_GATT_CCC_NOTIFY;
        subscribe_params.ccc_handle = attr->handle;

        err = bt_gatt_subscribe(conn, &subscribe_params);
        if (err && err != -EALREADY) {
            printk("Subscribe failed (err %d)\n", err);
        } else {
            printk("[SUBSCRIBED]\n");
        }

        return BT_GATT_ITER_STOP;
    }

    return BT_GATT_ITER_STOP;
}



static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	char dev[BT_ADDR_LE_STR_LEN];
	int err;

	bt_addr_le_to_str(addr, dev, sizeof(dev));
	printk("[DEVICE]: %s, AD evt type %u, AD data len %u, RSSI %i\n",
	       dev, type, ad->len, rssi);

	/* We're only interested in connectable events */
	if (type == BT_GAP_ADV_TYPE_ADV_IND ||
	    type == BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {
		return;
	}

	err = bt_le_scan_stop();
			if (err) {
				printk("Stop LE scan failed (err %d)\n", err);
				return;
			}
	
	err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
				BT_LE_CONN_PARAM_DEFAULT, &default_conn);
	if (err) 
    {
		start_scan();
	}
}

static void start_scan(void)
{
	int err;

	/* Use active scanning and disable duplicate filtering to handle any
	 * devices that might update their advertising data at runtime. */
	struct bt_le_scan_param scan_param = {
		.type       = BT_LE_SCAN_TYPE_ACTIVE,
		.options    = BT_LE_SCAN_OPT_NONE,
		.interval   = BT_GAP_SCAN_FAST_INTERVAL,
		.window     = BT_GAP_SCAN_FAST_WINDOW,
	};

	err = bt_le_scan_start(&scan_param, device_found);
	if (err) {
		printk("Scanning failed to start (err %d)\n", err);
		return;
	}

	printk("Scanning successfully started\n");
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	char addr[BT_ADDR_LE_STR_LEN];
	int err;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (conn_err) {
		printk("Failed to connect to %s (%u)\n", addr, conn_err);

		bt_conn_unref(default_conn);
		default_conn = NULL;

		start_scan();
		return;
	}

	printk("Connected: %s\n", addr);

}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Disconnected: %s (reason 0x%02x)\n", addr, reason);

	if (default_conn != conn) {
		return;
	}

	bt_conn_unref(default_conn);
	default_conn = NULL;

	start_scan();
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};


void gatt_disc(struct bt_conn *conn)
{
	int err;

	printk("Discovering services and characteristics\n");
	//static struct bt_gatt_discover_params discover_params;

	
	discover_params.uuid = primary_uuid;
	discover_params.func = discover;
	discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	discover_params.type = BT_GATT_DISCOVER_PRIMARY;

	err = bt_gatt_discover(conn, &discover_params);
	if (err) {
		printk("Discover failed(err %d)\n", err);
		return;
	}
}

static void gatt_write_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_write_params *params)
{
	if (err != BT_ATT_ERR_SUCCESS)
		printk("Write failed: 0x%02X\n", err);
	(void)memset(params, 0, sizeof(*params));
	//SFLAG(cwrite_f);
}

static void gatt_write(struct bt_conn *conn, uint16_t handle, char* chrc_data)
{
	static struct bt_gatt_write_params write_params;
	int err;

	if (handle == chrc_h)
    {
		printk("Writing to chrc\n");
		write_params.data = (const void *)chrc_data;
		write_params.length = strlen(chrc_data);
	}

	write_params.func = gatt_write_cb;
	write_params.handle = handle;
	

	err = bt_gatt_write(conn, &write_params);
	if (err != 0)
		printk("bt_gatt_write failed: %d\n", err);

	
	printk("write success\n");
}

int write_cmd(struct bt_conn *conn)
{
	
	k_sleep(K_SECONDS(0.5));
	console_getline_init();
    while(true)
    {
        printk("Entry ->:");
        char *s = console_getline();

        printk("Received entry -> : %s\n", s);
		gatt_write(conn, chrc_h, s);
    }
	
}

void  start(uint32_t count)
{


	default_conn = NULL;
	
	start_scan();
	//last_write_rate = 0U;

	while (true) {
		struct bt_conn *conn = NULL;

		if (default_conn) {
			/* Get a connection reference to ensure that a
			 * reference is maintained in case disconnected
			 * callback is called while we perform GATT Write
			 * command.
			 */
			conn = bt_conn_ref(default_conn);
		}

		if (conn) {
			printk("Connected \n");
			gatt_disc(conn);
			k_sleep(K_SECONDS(0.5));
			(void)write_cmd(conn);
			bt_conn_unref(conn);

			if (count) {
				count--;
				if (!count) {
					break;
				}
			}

			k_yield();
		} else {
			k_sleep(K_SECONDS(1));
		}
	}

	return ;
}

int main(void)
{
	int err;
	err = bt_enable(NULL);

	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}
	

	printk("Bluetooth initialized\n");
	(void)start(0U);
	
	return 0;
}