/******************************************************************************
* File Name: peripheral_privacy.h
*
* Description: This file is the public interface of peripheral_privacy.h
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

#ifndef __PERIPHERAL_PRIVACY_H_
#define __PERIPHERAL_PRIVACY_H_

/*******************************************************************************
*        Header Files
*******************************************************************************/
#include "wiced_bt_dev.h"
#include "cyabs_rtos.h"
#include "cyhal.h"
#include "wiced_bt_ble.h"
/*******************************************************************************
*        Macro Definitions
*******************************************************************************/

/* PWM frequency of LED's in Hertz when blinking */
#define ADV_LED_PWM_FREQUENCY               (1)
#define DIRECTED_ADV_LED_PWM_FREQUENCY      (3)
#define BUTTON_INTERRUPT_PRIORITY           (3u)
#define INT_PRIORITY                        (3u)

/* Queue size for LED and UART tasks*/
#define QUEUE_SIZE                          (1)

/*******************************************************************************
 * Variables
 ******************************************************************************/
extern cy_thread_t button_task_pointer;

extern cy_thread_t uart_task_pointer;

extern cy_thread_t led_task_pointer;
extern cy_queue_t xLEDQueue;
extern cy_queue_t xUARTQueue;

#define portMAX_DELAY              0xffffffffUL

/*******************************************************************************
*        Function Prototypes
*******************************************************************************/
/*App feature and utility functions*/
void   display_menu                (void);
void   app_kv_store_init           (void);

/*Button interrupt configuration and handling functions*/
void   key_button_app_init         (void);
void button_interrupt_handler      (void *handler_arg,
                                    cyhal_gpio_event_t event);
void   button_task                 (cy_thread_arg_t pvParameters);

/*LED state handlers*/
void   led_task_communicator       (wiced_bt_ble_advert_mode_t CurrAdvState);
void   app_led_control             (cy_thread_arg_t pvParameters);

/*UART interrupt handler*/
void   uart_interrupt_handler      (void *handler_arg,
                                    cyhal_uart_event_t event);
void   uart_task                   (cy_thread_arg_t pvParameters);

/* Callback function for Bluetooth stack management events */
wiced_bt_dev_status_t  app_bt_management_callback  (wiced_bt_management_evt_t event,
                                                    wiced_bt_management_evt_data_t *p_event_data);

#endif /*__PERIPHERAL_PRIVACY_H_*/

/* [] END OF FILE */
