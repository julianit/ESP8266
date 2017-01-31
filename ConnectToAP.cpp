/*
	I used ESP8266 E12 Node MCU (AMICA)
	
	This example set the module in a station mode.
	Connect to an access point and print its IP Address as soon
	the ip address is assing. 

*/
#include <ets_sys.h>
#include <osapi.h>
#include "user_interface.h"
#include <os_type.h>
#include <gpio.h>
#include "espconn.h"
#include "driver/uart.h"

#define DELAY 1000 /* milliseconds */

static ETSTimer network_timer;
extern int ets_uart_printf(const char *fmt, ...);

/******************************************************************************
 * FunctionName : user_rf_cal_sector_set
 * Description  : SDK just reversed 4 sectors, used for rf init data and paramters.
 *                We add this function to force users to set rf cal sector, since
 *                we don't know which sector is free in user's application.
 *                sector map for last several sectors : ABBBCDDD
 *                A : rf cal
 *                B : at parameters
 *                C : rf init data
 *                D : sdk parameters
 * Parameters   : none
 * Returns      : rf cal sector
*******************************************************************************/

uint32 ICACHE_FLASH_ATTR user_rf_cal_sector_set(void)
{
    enum flash_size_map size_map = system_get_flash_size_map();
    uint32 rf_cal_sec = 0;

    switch (size_map) {
        case FLASH_SIZE_4M_MAP_256_256:
            rf_cal_sec = 128 - 8;
            break;

        case FLASH_SIZE_8M_MAP_512_512:
            rf_cal_sec = 256 - 5;
            break;

        case FLASH_SIZE_16M_MAP_512_512:
        case FLASH_SIZE_16M_MAP_1024_1024:
            rf_cal_sec = 512 - 5;
            break;

        case FLASH_SIZE_32M_MAP_512_512:
        case FLASH_SIZE_32M_MAP_1024_1024:
            rf_cal_sec = 1024 - 5;
            break;

        default:
            rf_cal_sec = 0;
            break;
    }

    return rf_cal_sec;
}

void ICACHE_FLASH_ATTR user_rf_pre_init(void)
{
}

/* ICACHE_FLASH_ATTR - instruction that force 
   to  save the function in the flash
   This functions check if the ESP is connected to the acces point.
   if it is connected we can see the IP Address on the UART terminal,
   otherwise we re-start a timer and call the function again.
   
   So the function is in a loop until the ESP connects to AP 
  */
void ICACHE_FLASH_ATTR network_check_ip(void)
{
    struct ip_info ipconfig;
	//Disarm the timer
   	os_timer_disarm(&network_timer);
    wifi_get_ip_info(STATION_IF, &ipconfig);
	//Check if the ESP is connected 
    if (wifi_station_get_connect_status() == STATION_GOT_IP && ipconfig.ip.addr != 0)
    {
		os_printf("IP found\n");
    }
    else
    {
		// If the ESP is not connected 
		os_printf("No IP found\n");
		//Disarm the timer
        os_timer_disarm(&network_timer);
        //Associate the timer event with a function. In our case we want to 
		// invoke the network_check_ip
		os_timer_setfn(&network_timer, (os_timer_func_t *)network_check_ip, NULL);
        //Start the timer
		os_timer_arm(&network_timer, DELAY, 0);
    }
}

void ICACHE_FLASH_ATTR user_init()
{
    // Set AP settings
    char ssid[32] = "AP-NAME"; // AP name  
    char password[64] = "AP-PASSWORD"; //AP Password
    struct station_config stationConf;
    //Set the ESP to station mode.
	wifi_set_opmode( STATION_MODE );
	//Copy the Access point name and password.
	os_memcpy(&stationConf.ssid, ssid, 32);
    os_memcpy(&stationConf.password, password, 32);
    //Set the WiFi configuration
	wifi_station_set_config(&stationConf);
    //Try to connect to the access point (AP)
	wifi_station_connect();
	
    network_check_ip();
}

