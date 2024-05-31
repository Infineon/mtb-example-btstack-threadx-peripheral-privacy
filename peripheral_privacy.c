/******************************************************************************
* File Name:   peripheral_privacy.c
*
* Description: This is the source code for the Peripheral_Privacy Example
*              for ModusToolbox.
*
* Related Document: See README.md
*
*******************************************************************************
* Copyright 2021-2023, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/

/*******************************************************************************
*        Header Files
*******************************************************************************/
#include "wiced_bt_stack.h"
#include "cybsp.h"
#include "cy_retarget_io.h"
#include "cyabs_rtos.h"
#include <string.h>
#include "wiced_memory.h"
#include "cyhal.h"
#include "stdio.h"
#include "GeneratedSource/cycfg_gatt_db.h"
#include "GeneratedSource/cycfg_bt_settings.h"
#include "GeneratedSource/cycfg_gap.h"
#include "wiced_bt_dev.h"
#include "app_utils.h"
#include "mtb_kvstore_cat5.h"
#include <inttypes.h>
#include "peripheral_privacy.h"
#include "app_bt_bonding.h"

/*******************************************************************
 * Variable Definitions
 ******************************************************************/
typedef void(*pfn_free_buffer_t)            (uint8_t *);
static uint16_t                             connection_id = 0;
static wiced_bt_device_address_t            connected_bda;
static wiced_bt_ble_advert_mode_t *         p_adv_mode = NULL;

/* If true we will go into bonding mode. This will be set false if pre-existing bonding info is available */
static wiced_bool_t                         bond_mode = WICED_TRUE;

/* This is the index for the link keys,cccd and privacy mode of the host we are currently bonded to */
static uint8_t                              bondindex = 0;

static  cyhal_pwm_t                         adv_led_pwm;
bool                                        pairing_mode;

/* Configure GPIO interrupt */
cyhal_gpio_callback_data_t button_cb_struct;

/* enum for state machine*/
enum StateMachine
{
    IDLE_NO_DATA,
    IDLE_DATA,
    IDLE_PRIVACY_CHANGE,
    CONNECTED,
    BONDED
} state;

/* PWM Duty Cycle of LED's for different states */
enum
{
    LED_ON_DUTY_CYCLE = 100,
    LED_BLINKING_DUTY_CYCLE = 25,
    LED_OFF_DUTY_CYCLE = 0
} led_duty_cycles;

/*******************************************************************
 * Function Prototypes
 ******************************************************************/
/* Bluetooth LE functions*/

static wiced_bt_gatt_status_t   ble_app_gatt_event_handler    (wiced_bt_gatt_evt_t event,
                                                               wiced_bt_gatt_event_data_t *p_event_data);
static wiced_bt_gatt_status_t   ble_app_server_handler        (wiced_bt_gatt_attribute_request_t *p_data);
static wiced_bt_gatt_status_t   ble_app_bt_gatt_req_read_by_type_handler (uint16_t conn_id,
                                                                          wiced_bt_gatt_opcode_t opcode,
                                                                          wiced_bt_gatt_read_by_type_t *p_read_req,
                                                                          uint16_t len_requested);
static wiced_bt_gatt_status_t   ble_app_write_handler         (uint16_t conn_id,
                                                               wiced_bt_gatt_opcode_t opcode,
                                                               wiced_bt_gatt_write_req_t *p_write_req,
                                                               uint16_t len_req);
static wiced_bt_gatt_status_t   ble_app_set_value             (uint16_t attr_handle,
                                                               uint8_t *p_val,
                                                               uint16_t len);
static void                     ble_app_init                  (void);
static wiced_bt_gatt_status_t   ble_app_connect_handler       (wiced_bt_gatt_connection_status_t *p_conn_status);
static wiced_bt_gatt_status_t   ble_app_read_handler          (uint16_t conn_id,
                                                               wiced_bt_gatt_opcode_t opcode,
                                                               wiced_bt_gatt_read_t *p_read_req,
                                                               uint16_t len_req);
static gatt_db_lookup_table_t   *app_get_attribute            (uint16_t handle);

/*App feature and utility functions*/
static void                      directed_adv_handler          (uint8_t device_index);
static void                      privacy_mode_handler          (uint8_t device_index);



/*Buffer management functions*/
 void                             app_free_buffer                   (uint8_t *p_buf);
 void*                            app_alloc_buffer                  (int len);

/*******************************************************************************
 *                              FUNCTION DEFINITIONS
 ******************************************************************************/


/**
 * Function Name:
 * app_bt_management_callback
 *
 * Function Description:
 * @brief This is a Bluetooth stack management event handler function to receive events
 *        from BLE stack and process as per the application.
 *
 * @param  wiced_bt_management_evt_t event : BLE event code of one byte length
           wiced_bt_management_evt_data_t *p_event_data: Pointer to BLE management event
 *         structures
 *
 * @return wiced_result_t: Error code from WICED_RESULT_LIST or BT_RESULT_LIST
 *
 */

/* Bluetooth Management Event Handler */
wiced_result_t app_bt_management_callback(wiced_bt_management_evt_t event, wiced_bt_management_evt_data_t *p_event_data)
{
    cy_rslt_t rslt;
    wiced_bt_dev_status_t status = WICED_BT_SUCCESS;
    wiced_bt_device_address_t bda = {0};
    wiced_bt_dev_ble_pairing_info_t *p_ble_info = NULL;

    switch (event)
    {
    case BTM_ENABLED_EVT:
        /* Bluetooth Controller and Host Stack Enabled */
        if (WICED_BT_SUCCESS == p_event_data->enabled.status)
        {
            /* Clear out the bondinfo structure */
            memset(&bondinfo, 0, sizeof(bondinfo));
            wiced_bt_dev_read_local_addr(bda);
            printf("Local Bluetooth Address: ");
            print_bd_address(bda);
            /* Perform application-specific initialization */
            ble_app_init();
        }
        else
        {
            printf( "Bluetooth Disabled \n" );
        }
        break;

    case BTM_DISABLED_EVT:
        /* Bluetooth Controller and Host Stack Disabled */
        printf("Bluetooth Disabled \r\n");
        break;

    case BTM_USER_CONFIRMATION_REQUEST_EVT:
        printf("\n********************\n" );
        printf("* NUMERIC = %"PRIu32" *\r", p_event_data->user_confirmation_request.numeric_value );
        printf("\n********************\n" );
        printf("Press 'y' if the numeric values match on both devices or press 'n' if they do not.\n\n");
        memcpy(&(connected_bda), &(p_event_data->user_confirmation_request.bd_addr), sizeof(wiced_bt_device_address_t));
        break;

    case BTM_PASSKEY_NOTIFICATION_EVT:
        /* Print passkey to the screen so that the user can enter it. */
        printf( "**********************************************************************\n");
        printf( "Passkey Notification\n");
        printf("PassKey: %"PRIu32" \n", p_event_data->user_passkey_notification.passkey );
        printf( "**********************************************************************\n");
        /*for simplicity we are confirming the passkey, end users may want to implement their own input method*/
        wiced_bt_dev_confirm_req_reply(WICED_BT_SUCCESS, p_event_data->user_passkey_notification.bd_addr);
        break;

    case BTM_SECURITY_REQUEST_EVT:
        /* Security Request */
        /* Only grant if we are in bonding mode */
        if(TRUE == bond_mode)
        {
            printf("Security Request Granted \n");
            wiced_bt_ble_security_grant(p_event_data->security_request.bd_addr, WICED_SUCCESS);
        }
        else
        {
            printf("Security Request Denied - not in bonding mode \n");
        }
        break;

    case BTM_PAIRING_IO_CAPABILITIES_BLE_REQUEST_EVT:
        /* Request for Pairing IO Capabilities (BLE) */
        printf("BLE Pairing IO Capabilities Request\n");
        /* IO Capabilities on this Platform */
        p_event_data->pairing_io_capabilities_ble_request.local_io_cap = BTM_IO_CAPABILITIES_DISPLAY_AND_YES_NO_INPUT;
        p_event_data->pairing_io_capabilities_ble_request.auth_req = BTM_LE_AUTH_REQ_SC_MITM_BOND;
        p_event_data->pairing_io_capabilities_ble_request.init_keys = BTM_LE_KEY_PENC | BTM_LE_KEY_PID;
        break;

    case BTM_BLE_CONNECTION_PARAM_UPDATE:
        printf("Connection parameter update status:%d, Connection Interval: %d, Connection Latency: %d, Connection Timeout: %d\n",
                                        p_event_data->ble_connection_param_update.status,
                                        p_event_data->ble_connection_param_update.conn_interval,
                                        p_event_data->ble_connection_param_update.conn_latency,
                                        p_event_data->ble_connection_param_update.supervision_timeout);
        break;

    case BTM_PAIRING_COMPLETE_EVT:

        /* Pairing is Complete */
        p_ble_info = &p_event_data->pairing_complete.pairing_complete_info.ble;
        printf("Pairing Status %s \n", get_bt_smp_status_name(p_ble_info->reason));

        if ( WICED_BT_SUCCESS == p_ble_info->reason ) /* Bonding successful */
        {
            /* Update Number of bonded devices and next free slot in slot data*/
            rslt = app_bt_update_slot_data();

            /*Check if the data was updated successfully*/
            if (CY_RSLT_SUCCESS == rslt)
            {
                printf("Slot Data saved to Flash \r\n");
                printf("Successfully Bonded to: ");
                print_bd_address(p_event_data->pairing_complete.bd_addr);
            }
            else
            {
                printf("Flash Write Error \r\n");
            }

            bond_mode = FALSE; /* remember that the device is now bonded, so disable bonding */
            printf("Number of bonded devices: %d, Next free slot: %d, Number of slots free: %d\n",bondinfo.slot_data[NUM_BONDED], bondinfo.slot_data[NEXT_FREE_INDEX ]+1, (BOND_INDEX_MAX - bondinfo.slot_data[NUM_BONDED]));
        }
        else
        {
            printf("Bonding failed! \n");
        }
        break;

    case BTM_ENCRYPTION_STATUS_EVT:
        /* Encryption Status Change */
        printf("Encryption Status event for: ");
        print_bd_address(p_event_data->encryption_status.bd_addr);
        printf("Encryption Status event result: %d \n", p_event_data->encryption_status.result);
        /*Check and retreive the index of the bond data of the device that got connected*/
        /* This call will return BOND_INDEX_MAX if the device is not found*/
        bondindex = app_bt_find_device_in_flash(p_event_data->encryption_status.bd_addr);
        if(bondindex < BOND_INDEX_MAX)
        {
            app_bt_restore_bond_data();
            app_bt_restore_cccd();
            app_wicedbutton_mb1_client_char_config[0] = peer_cccd_data[bondindex]; /* Set CCCD value from the value that was previously saved in the NVRAM */
            printf("Bond info present in Flash for device: ");
            print_bd_address(p_event_data->encryption_status.bd_addr);
            state = BONDED;
        }
        else{
            printf("No Bond info present in Flash for device: ");
            print_bd_address(p_event_data->encryption_status.bd_addr);
            bondindex=0;
        }
        break;

    case BTM_PAIRED_DEVICE_LINK_KEYS_UPDATE_EVT:
        /* save device keys to Flash */
        printf( "Paired Device Key Update \r\n");
        rslt = app_bt_save_device_link_keys(&(p_event_data->paired_device_link_keys_update));
        if (CY_RSLT_SUCCESS == rslt)
        {
            printf("Successfully Bonded to ");
            print_bd_address(p_event_data->paired_device_link_keys_update.bd_addr);
        }
        else
        {
            printf("Failed to bond! \r\n");
        }
        break;

    case BTM_PAIRED_DEVICE_LINK_KEYS_REQUEST_EVT:
        /* Paired Device Link Keys Request */
        printf("Paired Device Link keys Request Event for device ");
        print_bd_address((uint8_t *)(p_event_data->paired_device_link_keys_request.bd_addr));
        /* Need to search to see if the BD_ADDR we are looking for is in Flash. If not, we return WICED_BT_ERROR and the stack */
        /* will generate keys and will then call BTM_PAIRED_DEVICE_LINK_KEYS_UPDATE_EVT so that they can be stored */
        status = WICED_BT_ERROR;  /* Assume the device won't be found. If it is, we will set this back to WICED_BT_SUCCESS */

        /* This call will return BOND_INDEX_MAX if the device is not found*/
        bondindex = app_bt_find_device_in_flash(p_event_data->paired_device_link_keys_request.bd_addr);
        if ( bondindex < BOND_INDEX_MAX)
        {
            /* Copy the keys to where the stack wants it */
            memcpy(&(p_event_data->paired_device_link_keys_request), &(bondinfo.link_keys[bondindex]), sizeof(wiced_bt_device_link_keys_t));
            status = WICED_BT_SUCCESS;
        }
        else
        {
            printf("Device Link Keys not found in the database! \n");
            bondindex=0;
        }

        break;

    case BTM_LOCAL_IDENTITY_KEYS_UPDATE_EVT: /* Update of local privacy keys - save to NVSRAM */
        /* Update of local privacy keys - save to Flash */
        printf( "Local Identity Key Update\n");
        rslt = app_bt_save_local_identity_key(p_event_data->local_identity_keys_update);
        if (CY_RSLT_SUCCESS != rslt)
        {
            status = WICED_BT_ERROR;
        }
        break;

    case BTM_LOCAL_IDENTITY_KEYS_REQUEST_EVT: /* Request for local privacy keys - read from Flash */
        app_kv_store_init();
        printf("Local Identity Key Request\r\n");
        /*Read Local Identity Resolution Keys*/
        rslt = app_bt_read_local_identity_keys();
        if(CY_RSLT_SUCCESS == rslt)
        {
            memcpy(&(p_event_data->local_identity_keys_request), &(identity_keys), sizeof(wiced_bt_local_identity_keys_t));
            print_array(&identity_keys, sizeof(wiced_bt_local_identity_keys_t));
            status = WICED_BT_SUCCESS;
        }
        else
        {
            status = WICED_BT_ERROR;
        }
        break;

    case BTM_BLE_ADVERT_STATE_CHANGED_EVT:
        /* Advertisement State Changed */
        p_adv_mode = &p_event_data->ble_advert_state_changed;
        led_task_communicator(*p_adv_mode);
        printf("Advertisement State Change: %d\r\n", *p_adv_mode);
        break;

    default:
        printf("Unhandled Bluetooth Management Event: 0x%x %s\n", event, get_btm_event_name(event));
        break;
    }

    return status;
}

/**
 * Function Name:
 * ble_app_init
 *
 * Function Description:
 * @brief  Initialize the Application
 *
 * @param  None.
 *
 * @return None.
 */
void ble_app_init(void)
{
    /* These are needed for reading stored keys from Serial Flash */
    cy_rslt_t   rslt;

    /* Allow peer to pair */
    wiced_bt_set_pairable_mode(WICED_TRUE, 0);

    /* Set Advertisement Data */
    wiced_bt_ble_set_raw_advertisement_data(CY_BT_ADV_PACKET_DATA_SIZE,
    cy_bt_adv_packet_data);

    /* Register with stack to receive GATT callback */
    wiced_bt_gatt_register(ble_app_gatt_event_handler);

    /* Initialize GATT Database */
    wiced_bt_gatt_db_init(gatt_database, gatt_database_len, NULL);

    /* Read contents of Serial flash */
    rslt = app_bt_restore_bond_data();
    if (CY_RSLT_SUCCESS == rslt )
    {
        printf("Bond data successfully restored from flash!\r\n");
    }

    if (0 == bondinfo.slot_data[NUM_BONDED ] || BOND_INDEX_MAX < bondinfo.slot_data[NUM_BONDED])
    {
        /* Allow new devices to bond */
        bond_mode = TRUE;
        printf("No bonded Device Found,Starting Undirected Advertisement \r\n\r\n");
        /* Start Undirected LE Advertisements on device startup. */
        wiced_bt_start_advertisements(BTM_BLE_ADVERT_UNDIRECTED_HIGH, 0, NULL);
        /* Set current state to IDLE with no data*/
        state = IDLE_NO_DATA;
    }
    else
    {
        printf("Number of bonded devices: %d, Next free slot: %d, Number of slots free %d \r\n", bondinfo.slot_data[NUM_BONDED ],
                                                            bondinfo.slot_data[NEXT_FREE_INDEX] + 1, (BOND_INDEX_MAX - bondinfo.slot_data[NUM_BONDED]));
        printf("printing Bonded Device information: \r\n");
        /* New devices not allowed to bond, can be enabled by entering b on Terminal */
        bond_mode = FALSE;
        print_bond_data();

        /* Change state to IDLE with bond data present*/
        state = IDLE_DATA;
        /* Add devices to address resolution database*/
        app_bt_add_devices_to_address_resolution_db();

        /*Start Advertisements*/
        if (1 == bondinfo.slot_data[NUM_BONDED])
        {
            printf("\r\nOnly 1 Device Found,Starting directed Advertisement to: ");
            print_bd_address(bondinfo.link_keys[0].bd_addr);
            print_bd_address(bondinfo.link_keys[0].conn_addr);
            printf("Enter 'e' for starting undirected Advertisement to add new device\r\n");
            wiced_bt_start_advertisements(BTM_BLE_ADVERT_DIRECTED_HIGH, bondinfo.link_keys[0].key_data.ble_addr_type,
                                          bondinfo.link_keys[0].bd_addr);
        }
        else
        {
            printf("\r\nSelect the bonded Devices Found in below list to Start Directed Advertisement \r\n");
            print_device_selection_menu();
            printf("Enter slot number to start directed advertisement for that device \r\n");
            printf("Enter e for Starting undirected Advertisement to add new device \r\n");
            printf("************************** NOTE ***************************************************\r\n");
            printf("*ONCE THE SLOTS ARE FULL THE OLDEST DEVICE DATA WILL BE OVERWRITTEN FOR NEW DEVICE*\r\n");
            printf("***********************************************************************************\r\n");
        }
    }
}


/**
* Function Name:
* ble_app_gatt_event_handler
*
* Function Description:
* @brief This function handles GATT events from the BT stack.
*
* @param wiced_bt_gatt_evt_t event                   : BLE GATT event code of one byte length
*        wiced_bt_gatt_event_data_t *p_event_data    : Pointer to BLE GATT event structures
*
* @return wiced_bt_gatt_status_t: See possible status codes in wiced_bt_gatt_status_e in wiced_bt_gatt.h
*
**/
static wiced_bt_gatt_status_t ble_app_gatt_event_handler(wiced_bt_gatt_evt_t event,
                                                         wiced_bt_gatt_event_data_t *p_event_data)
{
    wiced_bt_gatt_status_t status = WICED_BT_GATT_ERROR;
    wiced_bt_gatt_attribute_request_t *p_attr_req = &p_event_data->attribute_request;
    /* Call the appropriate callback function based on the GATT event type, and pass the relevant event
     * parameters to the callback function */
    switch ( event )
    {
        case GATT_CONNECTION_STATUS_EVT:
            status = ble_app_connect_handler( &p_event_data->connection_status );
            break;

        case GATT_ATTRIBUTE_REQUEST_EVT:
            status = ble_app_server_handler( p_attr_req );
            break;
        case GATT_GET_RESPONSE_BUFFER_EVT: /* GATT buffer request, typically sized to max of bearer mtu - 1 */
            p_event_data->buffer_request.buffer.p_app_rsp_buffer =
            app_alloc_buffer(p_event_data->buffer_request.len_requested);
            p_event_data->buffer_request.buffer.p_app_ctxt = (void *)app_free_buffer;
            status = WICED_BT_GATT_SUCCESS;
            break;
        case GATT_APP_BUFFER_TRANSMITTED_EVT: /* GATT buffer transmitted event,  check \ref wiced_bt_gatt_buffer_transmitted_t*/
        {
            pfn_free_buffer_t pfn_free = (pfn_free_buffer_t)p_event_data->buffer_xmitted.p_app_ctxt;

            /* If the buffer is dynamic, the context will point to a function to free it. */
            if (pfn_free)
                pfn_free(p_event_data->buffer_xmitted.p_app_data);

            status = WICED_BT_GATT_SUCCESS;
        }
            break;


        default:
               status = WICED_BT_GATT_SUCCESS;
               break;
    }

    return status;
}

/**
 * Function Name:
 * ble_app_connect_handler
 *
 * Function Description:
 * @brief  This function handles the connection and disconnection events. It also
 *         stores the currently connected device information in hostinfo structure.
 *
 * @param   p_conn_status  contains information related to the connection/disconnection
 *          event.
 *
 * @return  wiced_bt_gatt_status_t: See possible status codes in wiced_bt_gatt_status_e
 *          in wiced_bt_gatt.h
 *
 */
wiced_bt_gatt_status_t ble_app_connect_handler( wiced_bt_gatt_connection_status_t *p_conn_status)
{
    wiced_bt_gatt_status_t status = WICED_BT_GATT_ERROR;

    if (NULL != p_conn_status)
    {
        if (p_conn_status->connected)
        {
            /* Device has connected */
#ifdef PSOC6_BLE
    /* Refer to the note in Document History section of Readme.md */
    if(pairing_mode == TRUE)
    {
        app_bt_add_devices_to_address_resolution_db();
        pairing_mode = FALSE;
    }
#endif
            printf("Connected : BD Addr: " );
            print_bd_address(p_conn_status->bd_addr);
            printf("Connection ID '%d'\n", p_conn_status->conn_id );

            /* Handling the connection by updating connection ID */
            connection_id = p_conn_status->conn_id;
            state = CONNECTED;
            led_task_communicator(BTM_BLE_ADVERT_OFF);
        }
        else
        {
            /* Device has disconnected */
            printf("\nDisconnected : BD Addr: " );
            print_bd_address(p_conn_status->bd_addr);
            printf("Connection ID '%d', Reason '%s'\n", p_conn_status->conn_id, get_bt_gatt_disconn_reason_name(p_conn_status->reason) );

            led_task_communicator(BTM_BLE_ADVERT_OFF);
            /* Handling the disconnection */
            connection_id = 0;
            /* Reset the CCCD value so that on a reconnect CCCD will be off */
            app_wicedbutton_mb1_client_char_config[0] = 0;

            if (bondinfo.slot_data[NUM_BONDED] > 0)
            {
                state = IDLE_DATA;
                print_device_selection_menu();
                printf("Enter slot number to start directed advertisement for that device \r\n");
                printf("Enter e for Starting undirected Advertisement to add new device \r\n");
                bond_mode = WICED_FALSE;
            }
            else
            {
                state = IDLE_NO_DATA;
                bond_mode = WICED_TRUE;
                wiced_bt_start_advertisements(BTM_BLE_ADVERT_UNDIRECTED_HIGH, 0, NULL);
            }
        }
        status = WICED_BT_GATT_SUCCESS;
    }

    return status;
}

/**
* Function Name:
* ble_app_server_handler
*
* Function Description:
* @brief   This function handles GATT server events from the BT stack.
*
* @param   wiced_bt_gatt_attribute_request_t *p_data: GATT request data structure
*
* @return  wiced_bt_gatt_status_t: See possible status codes in wiced_bt_gatt_status_e in
*          wiced_bt_gatt.h
*
*/
static wiced_bt_gatt_status_t ble_app_server_handler (wiced_bt_gatt_attribute_request_t *p_data)
{
    wiced_bt_gatt_status_t status = WICED_BT_GATT_ERROR;
    wiced_bt_gatt_write_req_t *p_write_request = &p_data->data.write_req;

    switch ( p_data->opcode )
    {
        case GATT_REQ_READ:
        case GATT_REQ_READ_BLOB:
            /* Attribute read request */
            status = ble_app_read_handler( p_data->conn_id, p_data->opcode,
                                          &p_data->data.read_req, p_data->len_requested);
            break;
        case GATT_REQ_READ_BY_TYPE:
            status = ble_app_bt_gatt_req_read_by_type_handler(p_data->conn_id,
                                                          p_data->opcode,
                                                         &p_data->data.read_by_type,
                                                          p_data->len_requested);
            break;
            /* Attribute write request */
        case GATT_REQ_WRITE:
        case GATT_CMD_WRITE:
        case GATT_CMD_SIGNED_WRITE:
             status = ble_app_write_handler(p_data->conn_id, p_data->opcode,
                                            &p_data->data.write_req,
                                            p_data->len_requested );

            if ((p_data->opcode == GATT_REQ_WRITE) && (status == WICED_BT_GATT_SUCCESS))
            {
                wiced_bt_gatt_server_send_write_rsp(p_data->conn_id, p_data->opcode,
                                                    p_write_request->handle);
            }
            else
            {
                wiced_bt_gatt_server_send_error_rsp(p_data->conn_id, p_data->opcode,
                                                    p_write_request->handle, status);
            }
               break;
        case GATT_REQ_MTU:
            /*Application calls wiced_bt_gatt_server_send_mtu_rsp() with desired mtu*/
            status = wiced_bt_gatt_server_send_mtu_rsp(p_data->conn_id,
                                                       p_data->data.remote_mtu,
                                                       wiced_bt_cfg_settings.p_ble_cfg->ble_max_rx_pdu_size);
            break;
        case GATT_HANDLE_VALUE_NOTIF:
             printf("Client received our notification\r\n");
             status = WICED_BT_GATT_SUCCESS;
             break;

        default:
            printf("Unhandled Event opcode:%d\r\n",p_data->opcode);
            break;
    }

    return status;
}

/**
* Function Name:
* ble_app_write_handler
*
* Function Description:
* @brief This function handles Write Requests received from the client device
*
* @param  conn_id       Connection ID
*         opcode        BLE GATT request type opcode
*         p_write_req   Pointer to BLE GATT write request
*         len_req       length of data requested
*
* @return wiced_bt_gatt_status_t: See possible status codes in wiced_bt_gatt_status_e in wiced_bt_gatt.h
*
*/
static wiced_bt_gatt_status_t ble_app_write_handler(uint16_t conn_id,
                                                    wiced_bt_gatt_opcode_t opcode,
                                                    wiced_bt_gatt_write_req_t *p_write_req,
                                                    uint16_t len_req)
{
    wiced_bt_gatt_status_t status = WICED_BT_GATT_INVALID_HANDLE;

    /* Attempt to perform the Write Request */
    status = ble_app_set_value(p_write_req->handle,
                                p_write_req->p_val,
                               p_write_req->val_len);

    if( WICED_BT_GATT_SUCCESS != status )
        {
            printf("WARNING: GATT set attr status 0x%x\n", status);
        }

        return (status);
}


/**
* Function Name:
* ble_app_read_handler
*
* Function Description:
* @brief  This function handles Read Requests received from the client device
*
* @param conn_id       Connection ID
*        opcode        BLE GATT request type opcode
*        p_read_req    Pointer to read request containing the handle to read
*        len_req       length of data requested
*
* @return wiced_bt_gatt_status_t: See possible status codes in wiced_bt_gatt_status_e in
*         wiced_bt_gatt.h
*
**/
static wiced_bt_gatt_status_t ble_app_read_handler( uint16_t conn_id,
                                                    wiced_bt_gatt_opcode_t opcode,
                                                    wiced_bt_gatt_read_t *p_read_req,
                                                    uint16_t len_req)
{

    gatt_db_lookup_table_t  *puAttribute;
    int          attr_len_to_copy;
    uint8_t     *from;
    int          to_send;

    puAttribute = app_get_attribute(p_read_req->handle);
    if ( NULL == puAttribute )
    {
        wiced_bt_gatt_server_send_error_rsp(conn_id, opcode, p_read_req->handle,
                                            WICED_BT_GATT_INVALID_HANDLE);
        return WICED_BT_GATT_INVALID_HANDLE;
    }
        attr_len_to_copy = puAttribute->cur_len;
        if (p_read_req->offset >= puAttribute->cur_len)
        {
             wiced_bt_gatt_server_send_error_rsp(conn_id, opcode, p_read_req->handle,
                                                 WICED_BT_GATT_INVALID_OFFSET);
             return WICED_BT_GATT_INVALID_OFFSET;
         }
    to_send = MIN(len_req, attr_len_to_copy - p_read_req->offset);
    from = ((uint8_t *)puAttribute->p_data) + p_read_req->offset;

    return wiced_bt_gatt_server_send_read_handle_rsp(conn_id, opcode, to_send, from, NULL); /* No need for context, as buff not allocated */;
}

/**
* Function Name:
* ble_app_set_value
*
* Function Description:
* @brief  This function handles writing to the attribute handle in the GATT database using the
*          data passed from the BT stack. The value to write is stored in a buffer
*          whose starting address is passed as one of the function parameters
*
* @param  attr_handle  GATT attribute handle
*         p_val        Pointer to BLE GATT write request value
*         len          length of GATT write request
*
*
* @return  wiced_bt_gatt_status_t: See possible status codes in wiced_bt_gatt_status_e in
*          wiced_bt_gatt.h
*
*/
static wiced_bt_gatt_status_t ble_app_set_value(uint16_t attr_handle,
                                                uint8_t *p_val,
                                                uint16_t len)
{
    int i = 0;
    wiced_bool_t isHandleInTable = WICED_FALSE;
    wiced_bool_t validLen = WICED_FALSE;
    wiced_bt_gatt_status_t res = WICED_BT_GATT_INVALID_HANDLE;
    cy_rslt_t rslt = CY_RSLT_SUCCESS;
    uint16_t cccd=0;

    // Check for a matching handle entry
    for (i = 0; i < app_gatt_db_ext_attr_tbl_size; i++)
    {
        if (app_gatt_db_ext_attr_tbl[i].handle == attr_handle)
        {
            // Detected a matching handle in external lookup table
            isHandleInTable = WICED_TRUE;
            // Verify that size constraints have been met
            validLen = (app_gatt_db_ext_attr_tbl[i].max_len >= len);
            if (validLen)
            {
                // Value fits within the supplied buffer; copy over the value
                app_gatt_db_ext_attr_tbl[i].cur_len = len;
                memcpy(app_gatt_db_ext_attr_tbl[i].p_data, p_val, len);
                res = WICED_BT_GATT_SUCCESS;

                switch (attr_handle)
                {
                case HDLD_WICEDBUTTON_MB1_CLIENT_CHAR_CONFIG:
                    if (len != 2)
                    {
                        return WICED_BT_GATT_INVALID_ATTR_LEN;
                    }

                    /* Update CCCD Value in Flash */
                    cccd = (p_val[0] | (p_val[1]<<8));
                    rslt = app_bt_update_cccd(cccd, bondindex);
                    if (CY_RSLT_SUCCESS != rslt)
                    {
                        printf("Failed to update CCCD Value in Flash! \r\n");
                    }
                    else{
                        printf("CCCD value updated in Flash! \r\n");
                    }
                    break;
                default:
                    printf("Write is not supported \r\n");
                }
            }
            else
            {
                // Value to write does not meet size constraints
                res = WICED_BT_GATT_INVALID_ATTR_LEN;
            }
            break;
        }
    }

    if (!isHandleInTable)
    {
        switch (attr_handle)
        {
        default:
            // The write operation was not performed for the indicated handle
            printf("Write Request to Invalid Handle: 0x%x\r\n", attr_handle);
            res = WICED_BT_GATT_WRITE_NOT_PERMIT;
            break;
        }
    }

    return res;
}

/**
 * Function Name:
 * ble_app_bt_gatt_req_read_by_type_handler
 *
 * Function Description:
 * @brief  Process read-by-type request from peer device
 *
 * @param conn_id       Connection ID
 *        opcode        BLE GATT request type opcode
 *        p_read_req    Pointer to read request containing the handle to read
 *        len_requested length of data requested
 *
 * @return wiced_bt_gatt_status_t  BLE GATT status
 */
static wiced_bt_gatt_status_t ble_app_bt_gatt_req_read_by_type_handler(uint16_t conn_id,
                                                                   wiced_bt_gatt_opcode_t opcode,
                                                                   wiced_bt_gatt_read_by_type_t *p_read_req,
                                                                   uint16_t len_requested)
{
    gatt_db_lookup_table_t *puAttribute;
    uint16_t last_handle = 0;
    uint16_t attr_handle = p_read_req->s_handle;
    uint8_t *p_rsp = app_alloc_buffer(len_requested);
    uint8_t pair_len = 0;
    int used = 0;

    if (p_rsp == NULL)
    {
        printf("No memory, len_requested: %d!!\r\n",len_requested);
        wiced_bt_gatt_server_send_error_rsp(conn_id, opcode, attr_handle, WICED_BT_GATT_INSUF_RESOURCE);
        return WICED_BT_GATT_INSUF_RESOURCE;
    }

    /* Read by type returns all attributes of the specified type, between the start and end handles */
    while (WICED_TRUE)
    {
        last_handle = attr_handle;
        attr_handle = wiced_bt_gatt_find_handle_by_type(attr_handle, p_read_req->e_handle,
                                                        &p_read_req->uuid);
        if (attr_handle == 0)
            break;

        if ((puAttribute = app_get_attribute(attr_handle)) == NULL)
        {
            printf("found type but no attribute for %d \r\n",last_handle);
            wiced_bt_gatt_server_send_error_rsp(conn_id, opcode, p_read_req->s_handle,
                                                WICED_BT_GATT_ERR_UNLIKELY);
            app_free_buffer(p_rsp);
            return WICED_BT_GATT_INVALID_HANDLE;
        }

        {
            int filled = wiced_bt_gatt_put_read_by_type_rsp_in_stream(p_rsp + used, len_requested - used, &pair_len,
                                                                attr_handle, puAttribute->cur_len, puAttribute->p_data);
            if (filled == 0)
            {
                break;
            }
            used += filled;
        }

        /* Increment starting handle for next search to one past current */
        attr_handle++;
    }

    if (used == 0)
    {
       printf("attr not found  start_handle: 0x%04x  end_handle: 0x%04x  Type: 0x%04x\r\n",
               p_read_req->s_handle, p_read_req->e_handle, p_read_req->uuid.uu.uuid16);
        wiced_bt_gatt_server_send_error_rsp(conn_id, opcode, p_read_req->s_handle, WICED_BT_GATT_INVALID_HANDLE);
        app_free_buffer(p_rsp);
        return WICED_BT_GATT_INVALID_HANDLE;
    }

    /* Send the response */
    return wiced_bt_gatt_server_send_read_by_type_rsp(conn_id, opcode, pair_len, used, p_rsp, (void *)app_free_buffer);
}

/**
* Function Name:
* led_task_communicator
*
* Function Description:
* @brief   This function handles the communication to the led task by pushing current
*          advertisement state to the LED queue which is processed by app_led_control
*
* @param  *CurrAdvState:Current Advertisement State
*
* @return  None
*
*/
void led_task_communicator(wiced_bt_ble_advert_mode_t CurrAdvState)
{

    /* Post the Current Advertisement State */


    if( CY_RSLT_SUCCESS != cy_rtos_queue_put( &xLEDQueue, &CurrAdvState,10))
    {
        printf("Failed to queue up Current Advertisement State!");
    }


}

/**
 * Function Name:
 * app_led_control
 *
 * Function Description:
 * @brief  This Function to toggle led state depending on the state of advertisement.
 *         1. Advertisement ON (Undirected):   slow Blinking led(T = 1 sec)
 *         2. Advertisement ON (Directed):     fast Blinking led(T = 200 msec)
 *         3. Advertisement OFF, Connected:    LED ON
 *         4. Advertisement OFF, Timed out:    LED OFF
 *
 * @param    void *pvParameters:                Not used
 *
 * @return  None.
 */

void app_led_control(cy_thread_arg_t arg)
{
    cy_rslt_t rslt;
    wiced_bt_ble_advert_mode_t CurrAdvState;

    /* Initialize the PWM used for Advertising LED */
    rslt = cyhal_pwm_init(&adv_led_pwm, CYBSP_USER_LED1, NULL);

    /* PWM init failed. Stop program execution */
    if (rslt != CY_RSLT_SUCCESS)
    {
        printf("Advertisement LED PWM Initialization has failed! \n");
        CY_ASSERT(0);
    }
    while (1)
    {
         if (CY_RSLT_SUCCESS == cy_rtos_queue_get(&xLEDQueue, &(CurrAdvState), portMAX_DELAY))
        {
            switch (CurrAdvState)
            {
            case BTM_BLE_ADVERT_OFF:
                if (0 != connection_id)
                {
                    rslt = cyhal_pwm_set_duty_cycle(&adv_led_pwm, LED_OFF_DUTY_CYCLE, ADV_LED_PWM_FREQUENCY);
                }
                else
                {
                    rslt = cyhal_pwm_set_duty_cycle(&adv_led_pwm, LED_ON_DUTY_CYCLE, ADV_LED_PWM_FREQUENCY);
                }
                break;

            case BTM_BLE_ADVERT_DIRECTED_HIGH:
            case BTM_BLE_ADVERT_DIRECTED_LOW:
                rslt = cyhal_pwm_set_duty_cycle(&adv_led_pwm, LED_BLINKING_DUTY_CYCLE, DIRECTED_ADV_LED_PWM_FREQUENCY);
                break;

            case BTM_BLE_ADVERT_UNDIRECTED_HIGH:
            case BTM_BLE_ADVERT_UNDIRECTED_LOW:
                rslt = cyhal_pwm_set_duty_cycle(&adv_led_pwm, LED_BLINKING_DUTY_CYCLE, ADV_LED_PWM_FREQUENCY);
                break;

            default:
                break;
            }
        }

        /* Check if update to PWM parameters is successful*/
        if (CY_RSLT_SUCCESS != rslt)
        {
            printf("Failed to set duty cycle parameters!!");
        }

        rslt = cyhal_pwm_start(&adv_led_pwm);

        /* Check if PWM started successfully */
        if (CY_RSLT_SUCCESS != rslt)
        {
            printf("Failed to start PWM !!");
        }
    }
}

/**
 * Function Name:
 * key_button_app_init
 *
 * Function Description:
 * @brief  This function configures the button for the interrupts.
 *
 * @param  None
 *
 * @return None
 *
 */
/**
* Function Name:
* key_button_app_init
*
* Function Description:
* @brief This function configures the button for the interrupts.
*
* @param None
*
* @return None
*
*/
void key_button_app_init(void)
{
    /* Initialize GPIO for button interrupt*/
    cy_rslt_t rslt = cyhal_gpio_init(CYBSP_USER_BTN, CYHAL_GPIO_DIR_INPUT, CYHAL_GPIO_DRIVE_PULLUP, CYBSP_BTN_OFF);
    /* GPIO init failed. Stop program execution */
    if (CY_RSLT_SUCCESS != rslt)
    {
        printf("Button GPIO init failed! \n");
        CY_ASSERT(0);
    }
    /* Initialize the structure with values. Add these lines just before initializing button */
    button_cb_struct.callback = button_interrupt_handler;
    button_cb_struct.callback_arg = NULL;
    button_cb_struct.pin = NC;
    button_cb_struct.next = NULL;
    /* Configure GPIO interrupt. */
    cyhal_gpio_register_callback(CYBSP_USER_BTN, &button_cb_struct);
    cyhal_gpio_enable_event(CYBSP_USER_BTN, CYHAL_GPIO_IRQ_FALL, BUTTON_INTERRUPT_PRIORITY, true);
}

/**
* Function Name:
* button_interrupt_handler
*
* Function Description:
* @brief This interrupt handler enables or disables GATT notifications upon button
* press.
*
* @param void *handler_arg: Not used
* cyhal_gpio_event_t event: Not used
*
* @return None
*
*/
void button_interrupt_handler(void *handler_arg, cyhal_gpio_event_t event)
{
    cy_rtos_thread_set_notification (&button_task_pointer);
}

/**
* Function Name:
* button_task
*
* Function Description:
* @brief   This task starts Bluetooth LE advertisment on first button press and enables
*          or disables notifications from the server upon successive button presses.
*
* @param  void *pvParameters:  Not used
*
* @return None
*
*/
void button_task(cy_thread_arg_t arg)
{
    for (;;)
    {
        cy_rtos_thread_wait_notification(portMAX_DELAY);
        /* Increment the button value to register the button press */
        app_wicedbutton_mb1[0]++;
        /* If the connection is up and if the client wants notifications, send updated button press value */
        if (connection_id != 0)
        {
            if (app_wicedbutton_mb1_client_char_config[0] & GATT_CLIENT_CONFIG_NOTIFICATION)
            {
                wiced_bt_gatt_server_send_notification(connection_id, HDLC_WICEDBUTTON_MB1_VALUE,
                                                app_wicedbutton_mb1_len, app_wicedbutton_mb1, NULL);
                printf("Send Notification: sending Button value\r\n");
            }
            else
            {
                printf("Notifications are Disabled\r\n");
            }
        }
        else
        {
            printf("Connection is not Up \r\n");
        }
    }
}

/**
* Function Name:
* uart_interrupt_handler
*
* Function Description:
* @brief  This function handles the UART interrupts and pushes the input to the
*         UART queue for processing by uart_task
*
* @param  void *handler_arg:                 Not used
*         cyhal_uart_event_t event:          Not used
*
* @return  None
*
*/
void uart_interrupt_handler(void *handler_arg, cyhal_uart_event_t event)
{
    (void) handler_arg;
    uint8_t readbyte;

    /* Read one byte from the buffer with a 100ms timeout */
    cyhal_uart_getc(&cy_retarget_io_uart_obj , &readbyte, 100);

    /* Post the byte. */
    cy_rtos_queue_put(&xUARTQueue, &readbyte, 0);
}

/**
* Function Name:
* uart_task
*
* Function Description:
* @brief  This function runs the UART task which processes the received commands via
*         Terminal.
*
* @param  void *pvParameters   : Not used
*
* @return None
*
*/

void uart_task(cy_thread_arg_t arg)
{
    uint8_t readbyte = 0;
    cy_rslt_t rslt = CY_RSLT_SUCCESS;
    uint8_t count = 0;
    uint8_t device_index = 0;

    for(;;)
    {
         if (CY_RSLT_SUCCESS == cy_rtos_queue_get( &xUARTQueue, &(readbyte), portMAX_DELAY))
        {
            /* Extract Device Index for use wherever required*/
            device_index = readbyte - '0';
            switch (readbyte)
            {
            case '1':
                if ((IDLE_DATA == state) && (1 <= bondinfo.slot_data[NUM_BONDED]))
                {
                    directed_adv_handler(device_index);
                }
                else if ((IDLE_PRIVACY_CHANGE == state) && (1 <= bondinfo.slot_data[NUM_BONDED]))
                {
                    privacy_mode_handler(device_index);
                    /*once privacy mode is changed go back to idle data state*/
                    state = IDLE_DATA;
                }
                else
                    printf("Invalid Operation\r\n");
                break;

            case '2':
                if ((IDLE_DATA == state) && (2 <= bondinfo.slot_data[NUM_BONDED]))
                {
                    directed_adv_handler(device_index);
                }
                else if ((IDLE_PRIVACY_CHANGE == state) && (2 <= bondinfo.slot_data[NUM_BONDED]))
                {
                    privacy_mode_handler(device_index);
                    /*once privacy mode is changed go back to idle data state*/
                    state = IDLE_DATA;
                }
                else
                    printf("Invalid Operation\r\n");
                break;

            case '3':
                if ((IDLE_DATA == state) && (3 <= bondinfo.slot_data[NUM_BONDED]))
                {
                    directed_adv_handler(device_index);
                }
                else if ((IDLE_PRIVACY_CHANGE == state) && (3 <= bondinfo.slot_data[NUM_BONDED]))
                {
                    privacy_mode_handler(device_index);
                    /*once privacy mode is changed go back to idle data state*/
                    state = IDLE_DATA;
                }
                else
                    printf("Invalid Operation\r\n");
                break;

            case '4':
                if ((IDLE_DATA == state) && (4 == bondinfo.slot_data[NUM_BONDED]))
                {
                    directed_adv_handler(device_index);
                }
                else if ((IDLE_PRIVACY_CHANGE == state) && (4 == bondinfo.slot_data[NUM_BONDED]))
                {
                    privacy_mode_handler(device_index);
                    /*once privacy mode is changed go back to idle data state*/
                    state = IDLE_DATA;
                }
                else
                    printf("Invalid Operation\r\n");
                break;

            case 'd':
                if (IDLE_DATA == state && BTM_BLE_ADVERT_DIRECTED_LOW != *p_adv_mode && BTM_BLE_ADVERT_DIRECTED_HIGH != *p_adv_mode)
                {
                    /* Put into bonding mode  */
                    bond_mode = TRUE;
                    rslt = app_bt_delete_bond_info();
                    if( CY_RSLT_SUCCESS == rslt)
                    {
                        printf( "Erased Flash!\n");
                    }
                    else
                    {
                        printf("Flash Write Error!\n");
                    }
                    wiced_bt_start_advertisements(BTM_BLE_ADVERT_UNDIRECTED_HIGH, 0, NULL);

                    /* Change state to Idle and no data */
                    state = IDLE_NO_DATA;
                }
                else if (IDLE_NO_DATA == state)
                    printf("No bond data present \r\n");
                else
                    printf("This option is not available when device is in connected or bonded state or its doing directed advertisement!!\r\n");

                break;

            case 'e':
                printf("************************** NOTE ***************************************************\r\n");
                printf("*ONCE THE SLOTS ARE FULL THE OLDEST DEVICE DATA WILL BE OVERWRITTEN FOR NEW DEVICE*\r\n");
                printf("***********************************************************************************\r\n");
                if (!((CONNECTED == state) || (BONDED == state)))
                {
                    if (bond_mode == WICED_FALSE) /* Enter bond mode */
                    {
                        /* Check to see if we need to erase one of the existing devices */
                        if (bondinfo.slot_data[NUM_BONDED ] == BOND_INDEX_MAX)
                        {
                            printf("Bonding slots full removing the oldest device \r\n");

                            /* Remove oldest device from the bonded device list */
                            wiced_result_t result = app_bt_delete_device_info(bondinfo.slot_data[NEXT_FREE_INDEX]);
                            if (WICED_BT_SUCCESS != result)
                            {
                                printf("error deleting device bond data!");
                            }
                            /* Reduce number of bonded devices by one */
                            bondinfo.slot_data[NUM_BONDED]--;

                            /*Update bond information in Flash*/
                             rslt = app_bt_update_bond_data();
                            if (CY_RSLT_SUCCESS == rslt)
                            {
                                printf("Removed host: ");
                                print_bd_address((uint8_t *)&bondinfo.link_keys[bondinfo.slot_data[NEXT_FREE_INDEX]].bd_addr);
                            }
                            else
                            {
                                printf("Flash Write Error, Cannot delete device!\n");
                            }
                        }

                        /* Put into bonding mode  */
                        bond_mode = WICED_TRUE;
                        printf("Bonding Mode Entered\r\n");
#ifdef PSOC6_BLE
/* This is a workaround for the issue mentioned in the Notes section under Document History in Readme.md
 * It allows the PSoC 6 Bluetooth LE device to connect to a new peer device even if PSoC 6 Bluetooth LE
 * has bonded with other devices previously. If there is a need to connect to a new device, clear the
 * controller address resolution list, start advertisement to connect with any new device, add the
 * old devices back to controller address resolution list immediately after connection. */
                pairing_mode = TRUE;
                wiced_result_t result = wiced_bt_ble_address_resolution_list_clear_and_disable();
                if(WICED_BT_SUCCESS == result)
                {
                    printf("Address resolution list cleared successfully \n");
                }
                else
                {
                    printf("Failed to clear address resolution list \n");
                }
#endif

                        /* restart the advertisements in Bonding Mode */
                        wiced_bt_start_advertisements(BTM_BLE_ADVERT_UNDIRECTED_HIGH, 0, NULL);
                    }
                    else /* Exit bonding mode */
                    {
                        bond_mode = WICED_FALSE;
                        printf("Bonding Mode Exited\r\n");
                    }
                }
                else
                    printf("This option is not available when device is in connected or bonded state!!");
                break;

            case 'h':
                display_menu();
                break;

            case 'l':
                printf("Number of bonded devices: %d, Next free slot: %d, Number of free slot: %d \r\n", bondinfo.slot_data[NUM_BONDED], bondinfo.slot_data[NEXT_FREE_INDEX] + 1, (BOND_INDEX_MAX - bondinfo.slot_data[NUM_BONDED]));
                for (count = 0; count < bondinfo.slot_data[NUM_BONDED]; count++)
                {
                    printf("Host %d: ", count+1);
                    print_bd_address(bondinfo.link_keys[count].bd_addr);
                }
                break;
            case 'p':
                /* If current state is bonded toggle current device privacy mode  else
                * print all devices and ask user for device to toggle Privacy mode*/
                if (BONDED == state)
                {
                    privacy_mode_handler(bondindex);
                }
                else
                {
                    state = IDLE_PRIVACY_CHANGE;
                    printf("Select the bonded Devices Found in below list to toggle current privacy mode \r\n\r\n");
                    print_device_selection_menu();
                    printf("\r\nEnter the slot number of the device to change privacy mode: \r\n");
                }
                break;

            case 'y':
                /*Useful if using numeric comparison for pairing*/
                wiced_bt_dev_confirm_req_reply(WICED_BT_SUCCESS,connected_bda);
                printf("Numeric Values are Matching!!\n");
                break;

            case 'n':
                /*Useful if using numeric comparison for pairing*/
                wiced_bt_dev_confirm_req_reply(WICED_BT_ERROR, connected_bda);
                printf("Numeric Values Don't Match\n");
                break;

            case 'r':

                if (CONNECTED != state && BONDED != state && BTM_BLE_ADVERT_DIRECTED_LOW != *p_adv_mode && BTM_BLE_ADVERT_DIRECTED_HIGH != *p_adv_mode)
                {
                    /*Reset Kv-store library, this will clear the flash*/
                    rslt = mtb_kvstore_reset(&kvstore_obj);
                    if (CY_RSLT_SUCCESS == rslt)
                    {
                        printf("successfully reset kv-store library, Please reset the device to generate new Keys!\r\n");
                    }
                    else
                    {
                        printf("failed to reset kv-store libray\r\n");
                    }
                    /*Clear bondinfo structure*/
                    memset(&bondinfo, 0, sizeof(bondinfo));
                    wiced_bt_start_advertisements(BTM_BLE_ADVERT_UNDIRECTED_HIGH, 0, NULL);
                    /* Change state to Idle and no data */
                    state = IDLE_NO_DATA;
                    /* Put into bonding mode  */
                    bond_mode = TRUE;
                }
                break;

            default:
                printf("Invalid Input\r\n");
            }
        }
    }
}

/**
 * Function Name:
 * display_menu
 *
 * Function Description:
 * @brief  Function to print the Menu.
 *
 * @param  None
 *
 * @return None
 *
 */
void display_menu(void)
{
    printf("************************** MENU ****************************************\r\n");
    printf("**1) Press 'l' to check for no of bonded devices and next empty slot  **\r\n");
    printf("**2) Press 'd' to erase all the bond data present in flash            **\r\n");
    printf("**3) Press 'e' to enter the bonding mode and add devices to bond list **\r\n");
    printf("**4) Enter slot number to start directed advertisement for that device**\r\n");
    printf("**5) Press 'p' to change privacy mode of bonded device                **\r\n");
    printf("**6) Press 'h' any time in application to print the menu              **\r\n");
    printf("**7) Press 'r' to reset kv-store (delete bond data and local IRK)     **\r\n");
    printf("***********************************************************************\r\n");
}

/**
 * Function Name: directed_adv_handler
 *
 * Function Description:
 * @brief   Directed advertisement Handler.
 *
 * @param   device_index : Index of the device stored in the device list to start directed
 *                         advertisement to.
 *
 * @return  None
 *
 */
void directed_adv_handler(uint8_t device_index)
{
    wiced_result_t result;

    wiced_bt_start_advertisements(BTM_BLE_ADVERT_OFF, 0, NULL);
    printf("Starting directed Advertisement for ");
    print_bd_address(bondinfo.link_keys[device_index - 1].bd_addr);
    printf("Enter e for Starting undirected Advertisement to add new device\r\n");
    result = wiced_bt_start_advertisements(BTM_BLE_ADVERT_DIRECTED_HIGH, bondinfo.link_keys[device_index-1].key_data.ble_addr_type, bondinfo.link_keys[device_index-1].bd_addr);
    if (WICED_BT_SUCCESS != result)
    {
        printf("failed to start directed advertisement! \n");
    }
}

/**
 * Function Name:
 * void privacy_mode_handler
 *
 * Function Description:
 * @brief   Handles request for change of privacy mode of bonded devices.
 *
 * @param   device_index   Index of the device stored in the device list to change privacy
 *                         mode.
 *
 * @return  None
 *
 */
void privacy_mode_handler(uint8_t device_index)
{
    cy_rslt_t rslt;
    bondinfo.privacy_mode[device_index] ^= 1;
    printf("Privacy Mode for device %d changed to (0 for Network, 1 for Device) :  %d \r\n", device_index, bondinfo.privacy_mode[device_index]);
    rslt = mtb_kvstore_write_numeric_key(&kvstore_obj,bond_data, (uint8_t *)&bondinfo, sizeof(bondinfo),true);
    if(CY_RSLT_SUCCESS != rslt)
    {
        printf("Failed to update ");
    }
    wiced_bt_ble_set_privacy_mode(bondinfo.link_keys[device_index].bd_addr,
                                  bondinfo.link_keys[device_index].key_data.ble_addr_type ,
                                  bondinfo.privacy_mode[device_index]);
}



/**
* Function Name:
* app_get_attribute
*
* Function Description:
* @brief   This function searches through the GATT DB to point to the attribute
*          corresponding to the given handle
*
* @param   uint16_t handle: Handle to search for in the GATT DB
*
* @return  gatt_db_lookup_table_t *: Pointer to the correct attribute in the GATT DB
*
*/
static gatt_db_lookup_table_t *app_get_attribute(uint16_t handle)
{
    /* Search for the given handle in the GATT DB and return the pointer to the
    correct attribute */
    uint8_t array_index = 0;

    for (array_index = 0; array_index < app_gatt_db_ext_attr_tbl_size; array_index++)
    {
        if (app_gatt_db_ext_attr_tbl[array_index].handle == handle)
        {
            return (&app_gatt_db_ext_attr_tbl[array_index]);
        }
    }
    return NULL;
}
