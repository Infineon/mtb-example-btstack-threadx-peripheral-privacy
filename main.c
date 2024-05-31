/******************************************************************************
* File Name:   main.c
*
* Description: This is the source code for the Peripheral_Privacy Example
*              for ModusToolbox.
*
* Related Document: See README.md
*
********************************************************************************
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
#include "cyhal.h"
#include "cy_retarget_io.h"
#include "cyabs_rtos.h"
#include "cycfg_bt_settings.h"
#include "peripheral_privacy.h"
#include "mtb_kvstore_cat5.h"
#include "app_bt_bonding.h"
#include "cyabs_rtos_impl.h"

#define BUTTON_TASK_STACK_SIZE                    (4096)
#define BUTTON_TASK_PRIORITY                      (CY_RTOS_PRIORITY_NORMAL)

#define UART_TASK_STACK_SIZE                    (4096)
#define UART_TASK_PRIORITY                      (CY_RTOS_PRIORITY_NORMAL)

#define LED_TASK_STACK_SIZE                    (4096)
#define LED_TASK_PRIORITY                      (CY_RTOS_PRIORITY_NORMAL)


/*******************************************************************
 * Variable Definitions
 ******************************************************************/

static uint64_t button_task_stack[BUTTON_TASK_STACK_SIZE/8];

static uint64_t uart_task_stack[UART_TASK_STACK_SIZE/8];

static uint64_t led_task_stack[LED_TASK_STACK_SIZE/8];

cy_thread_t button_task_pointer;

cy_thread_t uart_task_pointer;

cy_thread_t led_task_pointer;


/* Queue for communication between UART ISR and UART Task*/
cy_queue_t xUARTQueue;

/* Queue for communication with LED Task*/
cy_queue_t xLEDQueue;

/**
 * Function Name:
 * main
 *
 * Function Description:
 * @brief   Entry point to the application. Initialize transport configuration
 *          and register BLE management event callback. The actual application
 *          initialization will happen when stack reports that BT device is ready
 *
 * @param   None
 *
 * @return  int
 *
 */
int main()
{
    cy_rslt_t result;
    wiced_result_t wiced_result;

    /* Initialize the board support package */
    result = cybsp_init();

    /* Enable global interrupts */
    __enable_irq();

    cyhal_syspm_lock_deepsleep();

    /* Initialize retarget-io to use the debug UART port */
    cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX, CY_RETARGET_IO_BAUDRATE);

    printf("************* Peripheral Privacy App Start***** ************************\n");
    display_menu();

    if (CY_RSLT_SUCCESS != result)
    {
        printf("BSP Initialization has failed! \n");
        CY_ASSERT(0);
    }

    /* Create a queue capable of unsigned integer values.
       this is used for communicating between UART ISR and the UART task */
    result = cy_rtos_queue_init(&xUARTQueue,QUEUE_SIZE,sizeof(uint8_t) );

    /*Check the status of Queue creation*/
    if (result != CY_RSLT_SUCCESS)
    {
        printf("Failed to create Queue for communication between UART ISR and Task!!");
        CY_ASSERT(0);
    }

    /* Create a queue capable of unsigned integer values.
       this is used for communicating with LED task */

    result = cy_rtos_queue_init(&xLEDQueue,QUEUE_SIZE,sizeof(uint8_t));

    /*Check the status of Queue creation*/
    if (result != CY_RSLT_SUCCESS)
    {
        printf("Failed to create Queue for communication with LED Task!!");
        CY_ASSERT(0);
    }

    /* Configure the Button GPIO */
    key_button_app_init();

    result = cy_rtos_thread_create(&button_task_pointer,
             (cy_thread_entry_fn_t) &button_task,
                                           "button_task",
                                           &button_task_stack,
                                           BUTTON_TASK_STACK_SIZE,
                                           BUTTON_TASK_PRIORITY,
                                           0);
    if (result != CY_RSLT_SUCCESS)
    {
        printf("button_task creation failed \r\n");
        CY_ASSERT(0);
    }

    result = cy_rtos_thread_create(&uart_task_pointer,
             (cy_thread_entry_fn_t)&uart_task,
                                           "uart_task",
                                           &uart_task_stack,
                                           UART_TASK_STACK_SIZE,
                                           UART_TASK_PRIORITY,
                                           0);
    if (result != CY_RSLT_SUCCESS)
    {
        printf("uart_task creation failed \r\n");
        CY_ASSERT(0);
    }



    result = cy_rtos_thread_create(&led_task_pointer,
                                           (cy_thread_entry_fn_t)&app_led_control,
                                           "app_led_control_task",
                                          (void*) &led_task_stack,
                                           LED_TASK_STACK_SIZE,
                                           LED_TASK_PRIORITY,
                                           0);
    if (result != CY_RSLT_SUCCESS)
    {
        printf("BLE_task creation failed \r\n");
        CY_ASSERT(0);
    }


    /* Register a callback function and set it to fire for any received UART characters */
    cyhal_uart_register_callback(&cy_retarget_io_uart_obj, uart_interrupt_handler,NULL);
    cyhal_uart_enable_event(&cy_retarget_io_uart_obj, CYHAL_UART_IRQ_RX_NOT_EMPTY, INT_PRIORITY, TRUE);


    /* Register call back and configuration with stack */
    wiced_result = wiced_bt_stack_init(app_bt_management_callback, &wiced_bt_cfg_settings);

    /* Check if stack initialization was successful */
    if (WICED_BT_SUCCESS == wiced_result)
    {
        printf("Bluetooth Stack Initialization Successful \n");
    }
    else
    {
        printf("Bluetooth Stack Initialization failed!! \n");
        CY_ASSERT(0);
    }

}
