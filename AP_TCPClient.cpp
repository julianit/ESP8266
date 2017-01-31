/*
	This is a bit more complex example
	I will proovide more information when I make the code more clean an concise.
    untill then:
	1. The ESP connects to access point.
	2. Create TCP client and tries to connect to TCP Server on port 5555
	3. Send message 
	4. Close connection and repeat from step 2
		
*/

#include <ets_sys.h>
#include <osapi.h>
#include "user_interface.h"
#include "user_config.h"
#include "espconn.h"
#include <os_type.h>
#include <gpio.h>
#include "espconn.h"
#include "driver/uart.h"

#define DELAY 1000 /* milliseconds */

typedef enum {
	WIFI_CONNECTING,
	WIFI_CONNECTING_ERROR,
	WIFI_CONNECTED,
	TCP_DISCONNECTED,
	TCP_CONNECTING,
	TCP_CONNECTING_ERROR,
	TCP_CONNECTED,
	TCP_SENDING_DATA_ERROR,
	TCP_SENT_DATA
} tConnState;

const char *sEspconnErr[] =
{
		"Ok",                    // ERR_OK          0
		"Out of memory error",   // ERR_MEM        -1
		"Buffer error",          // ERR_BUF        -2
		"Timeout",               // ERR_TIMEOUT    -3
		"Routing problem",       // ERR_RTE        -4
		"Operation in progress", // ERR_INPROGRESS -5
		"Illegal value",         // ERR_VAL        -6
		"Operation would block", // ERR_WOULDBLOCK -7
		"Connection aborted",    // ERR_ABRT       -8
		"Connection reset",      // ERR_RST        -9
		"Connection closed",     // ERR_CLSD       -10
		"Not connected",         // ERR_CONN       -11
		"Illegal argument",      // ERR_ARG        -12
		"Address in use",        // ERR_USE        -13
		"Low-level netif error", // ERR_IF         -14
		"Already connected"      // ERR_ISCONN     -15
};


//LOCAL os_timer_t hello_timer;
static ETSTimer network_timer;
static ETSTimer WiFiLinker;
static tConnState connState = WIFI_CONNECTING;
struct espconn Conn;
esp_tcp ConnTcp;
static char macaddr[6];
static unsigned char tcpReconCount;

static void platform_reconnect(struct espconn *);
extern int ets_uart_printf(const char *fmt, ...);
static void wifi_check_ip(void *arg);
int (*console_printf)(const char *fmt, ...) = ets_uart_printf;

static void ICACHE_FLASH_ATTR platform_reconnect(struct espconn *pespconn)
{
	#ifdef PLATFORM_DEBUG
	console_printf("platform_reconnect\r\n");
	#endif
	wifi_check_ip(NULL);
}

static void ICACHE_FLASH_ATTR tcpclient_sent_cb(void *arg)
{
	struct espconn *pespconn = arg;
	#ifdef PLATFORM_DEBUG
	ets_uart_printf("tcpclient_sent_cb\r\n");
	#endif
	#ifdef LWIP_DEBUG
	list_espconn_tcp(pespconn);
	list_lwip_tcp_psc();
	#endif
	espconn_disconnect(pespconn);
}

static void ICACHE_FLASH_ATTR tcpclient_discon_cb(void *arg)
{
	struct espconn *pespconn = arg;
	connState = TCP_DISCONNECTED;
	#ifdef PLATFORM_DEBUG
	ets_uart_printf("tcpclient_discon_cb\r\n");
	#endif
	if (pespconn == NULL)
	{
		#ifdef PLATFORM_DEBUG
		ets_uart_printf("tcpclient_discon_cb - conn is NULL!\r\n");
		#endif
		return;
	}
	#ifdef PLATFORM_DEBUG
	ets_uart_printf("Will reconnect in 2s...\r\n");
	#endif
	os_timer_disarm(&WiFiLinker);
	os_timer_setfn(&WiFiLinker, (os_timer_func_t *)platform_reconnect, pespconn);
	os_timer_arm(&WiFiLinker, 2000, 0);
}

static void ICACHE_FLASH_ATTR tcpclient_connect_cb(void *arg)
{
	struct espconn *pespconn = arg;
	tcpReconCount = 0;
	char payload[128];
	#ifdef PLATFORM_DEBUG
	//ets_uart_printf("tcpclient_connect_cb\r\n");
	os_sprintf("tcpclient_connect_cb\r\n");
	#endif
	espconn_regist_sentcb(pespconn, tcpclient_sent_cb);
	connState = TCP_CONNECTED;
	os_sprintf(payload, MACSTR ",%s", MAC2STR(macaddr), "ESP8266");
	sint8 espsent_status = espconn_sent(pespconn, payload, strlen(payload));
	if(espsent_status == ESPCONN_OK) {
		connState = TCP_SENT_DATA;
		#ifdef PLATFORM_DEBUG
		console_printf("Data sent, payload: %s\r\n", payload);
		#endif
	} else {
		connState = TCP_SENDING_DATA_ERROR;
		#ifdef PLATFORM_DEBUG
		console_printf("Error while sending data.\r\n");
		#endif
	}
	#ifdef LWIP_DEBUG
	list_espconn_tcp(pespconn);
	list_lwip_tcp_psc();
	#endif
}

static void ICACHE_FLASH_ATTR tcpclient_recon_cb(void *arg, sint8 err)
{
	struct espconn *pespconn = arg;
	connState = TCP_DISCONNECTED;

	#ifdef PLATFORM_DEBUG
	console_printf("tcpclient_recon_cb\r\n");
	if (err != ESPCONN_OK)
		console_printf("Connection error: %d - %s\r\n", err, ((err>-16)&&(err<1))? sEspconnErr[-err] : "?");
    #endif
	#ifdef LWIP_DEBUG
	list_espconn_tcp(pespconn);
	list_lwip_tcp_psc();
	#endif

    if (++tcpReconCount >= 5)
    {
		connState = TCP_CONNECTING_ERROR;
		tcpReconCount = 0;
		#ifdef PLATFORM_DEBUG
		console_printf("tcpclient_recon_cb, 5 failed TCP attempts!\r\n");
		console_printf("Will reconnect in 10s...\r\n");
		#endif
		os_timer_disarm(&WiFiLinker);
		os_timer_setfn(&WiFiLinker, (os_timer_func_t *)platform_reconnect, pespconn);
		os_timer_arm(&WiFiLinker, 10000, 0);
    }
    else
    {
		#ifdef PLATFORM_DEBUG
		console_printf("Will reconnect in 2s...\r\n");
		#endif
		os_timer_disarm(&WiFiLinker);
		os_timer_setfn(&WiFiLinker, (os_timer_func_t *)platform_reconnect, pespconn);
		os_timer_arm(&WiFiLinker, 2000, 0);
	}
}

static void ICACHE_FLASH_ATTR senddata()
{
	os_timer_disarm(&WiFiLinker);
	char info[150];
	char tcpserverip[15];
	Conn.proto.tcp = &ConnTcp;
	Conn.type = ESPCONN_TCP;
	Conn.state = ESPCONN_NONE;
	os_sprintf(tcpserverip, "%s", TCPSERVERIP);
	uint32_t ip = ipaddr_addr(tcpserverip);
	os_memcpy(Conn.proto.tcp->remote_ip, &ip, 4);
	Conn.proto.tcp->local_port = espconn_port();
	Conn.proto.tcp->remote_port = TCPSERVERPORT;
	espconn_regist_connectcb(&Conn, tcpclient_connect_cb);
	espconn_regist_reconcb(&Conn, tcpclient_recon_cb);
	espconn_regist_disconcb(&Conn, tcpclient_discon_cb);
	#ifdef PLATFORM_DEBUG
	console_printf("Start espconn_connect to " IPSTR ":%d\r\n", IP2STR(Conn.proto.tcp->remote_ip), Conn.proto.tcp->remote_port);
	#endif
	#ifdef LWIP_DEBUG
	console_printf("LWIP_DEBUG: Start espconn_connect local port %u\r\n", Conn.proto.tcp->local_port);
	#endif
	sint8 espcon_status = espconn_connect(&Conn);
	#ifdef PLATFORM_DEBUG
	switch(espcon_status)
	{
		case ESPCONN_OK:
			console_printf("TCP created.\r\n");
			break;
		case ESPCONN_RTE:
			console_printf("Error connection, routing problem.\r\n");
			break;
		case ESPCONN_TIMEOUT:
			console_printf("Error connection, timeout.\r\n");
			break;
		default:
			console_printf("Connection error: %d\r\n", espcon_status);
	}
	#endif
	if(espcon_status != ESPCONN_OK) {
		#ifdef LWIP_DEBUG
		list_espconn_tcp(&Conn);
		list_lwip_tcp_psc();
		#endif
		os_timer_setfn(&WiFiLinker, (os_timer_func_t *)wifi_check_ip, NULL);
		os_timer_arm(&WiFiLinker, 1000, 0);
	}
}

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

void ICACHE_FLASH_ATTR wifi_check_ip(void *arg)
{
    struct ip_info ipconfig;
    os_timer_disarm(&network_timer);
    wifi_get_ip_info(STATION_IF, &ipconfig);
    if (wifi_station_get_connect_status() == STATION_GOT_IP && ipconfig.ip.addr != 0)
    {
        os_printf("IP found\n");
        senddata();
       // thethingsio_activate(ACTIVATION_CODE, get_token);
    }
    else
    {
        os_printf("No IP found\n");
        os_timer_disarm(&network_timer);
        os_timer_setfn(&network_timer, (os_timer_func_t *)wifi_check_ip, NULL);
        os_timer_arm(&network_timer, DELAY, 0);
    }
}

void ICACHE_FLASH_ATTR user_init()
{
    // Set AP settings
    char ssid[32] = WIFI_CLIENTSSID;			//"Access point name";
    char password[64] = WIFI_CLIENTPASSWORD;	//"Access pount password";
    struct station_config stationConf;
    wifi_set_opmode( STATION_MODE );
    os_memcpy(&stationConf.ssid, ssid, 32);
    os_memcpy(&stationConf.password, password, 32);
    wifi_station_set_config(&stationConf);
    wifi_station_connect();
    wifi_check_ip(NULL);

}

