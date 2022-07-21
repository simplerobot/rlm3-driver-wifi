#include "Test.hpp"
#include "rlm3-wifi.h"
#include "rlm3-gpio.h"
#include "rlm3-task.h"
#include "rlm3-uart.h"
#include "rlm3-sim.hpp"
#include <cstring>
#include <vector>
#include "logger.h"

#include "rlm3-base.h"


RLM3_Task g_client_thread = nullptr;
size_t g_recv_buffer_size = 0;
uint8_t g_recv_buffer_data[32];

extern void RLM3_WIFI_Receive_Callback(uint8_t data)
{
	if (g_recv_buffer_size < sizeof(g_recv_buffer_data))
		g_recv_buffer_data[g_recv_buffer_size] = data;
	g_recv_buffer_size++;
	RLM3_GiveFromISR(g_client_thread);
}

TEST_CASE(RLM3_WIFI_IsInit_Uninitialized)
{
	ASSERT(!RLM3_WIFI_IsInit());
}

TEST_CASE(RLM3_WIFI_Init_HappyCase)
{
	SIM_RLM3_UART4_Transmit("AT\r\n");
	SIM_RLM3_UART4_Receive("AT\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("ATE0\r\n");
	SIM_RLM3_UART4_Receive("ATE0\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWAUTOCONN=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPMODE=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");

	ASSERT(RLM3_WIFI_Init());

	ASSERT(RLM3_WIFI_IsInit());
	ASSERT(SIM_GPIO_IsClockEnabled(GPIOG));
	ASSERT(RLM3_UART4_IsInit());
	ASSERT(SIM_RLM3_UART4_GetBaudrate() == 115200);
	ASSERT(SIM_GPIO_GetMode(WIFI_ENABLE_GPIO_Port, WIFI_ENABLE_Pin) == GPIO_MODE_OUTPUT_PP);
	ASSERT(SIM_GPIO_GetMode(WIFI_BOOT_MODE_GPIO_Port, WIFI_BOOT_MODE_Pin) == GPIO_MODE_OUTPUT_PP);
	ASSERT(SIM_GPIO_GetMode(WIFI_RESET_GPIO_Port, WIFI_RESET_Pin) == GPIO_MODE_OUTPUT_PP);
	ASSERT(SIM_GPIO_Read(WIFI_ENABLE_GPIO_Port, WIFI_ENABLE_Pin));
	ASSERT(SIM_GPIO_Read(WIFI_BOOT_MODE_GPIO_Port, WIFI_BOOT_MODE_Pin));
	ASSERT(SIM_GPIO_Read(WIFI_RESET_GPIO_Port, WIFI_RESET_Pin));
}

TEST_CASE(RLM3_WIFI_Init_PingTimeout)
{
	SIM_RLM3_UART4_Transmit("AT\r\n");

	ASSERT(!RLM3_WIFI_Init());
}

TEST_CASE(RLM3_WIFI_Init_PingFailure)
{
	SIM_RLM3_UART4_Transmit("AT\r\n");
	SIM_RLM3_UART4_Receive("AT\r\nFAIL\r\n");

	ASSERT(!RLM3_WIFI_Init());
}

TEST_CASE(RLM3_WIFI_Init_EchoTimeout)
{
	SIM_RLM3_UART4_Transmit("AT\r\n");
	SIM_RLM3_UART4_Receive("AT\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("ATE0\r\n");

	ASSERT(!RLM3_WIFI_Init());
}

TEST_CASE(RLM3_WIFI_Init_EchoFailure)
{
	SIM_RLM3_UART4_Transmit("AT\r\n");
	SIM_RLM3_UART4_Receive("AT\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("ATE0\r\n");
	SIM_RLM3_UART4_Receive("ATE0\r\nFAIL\r\n");

	ASSERT(!RLM3_WIFI_Init());
}

TEST_CASE(RLM3_WIFI_Init_ManualConnectFailure)
{
	SIM_RLM3_UART4_Transmit("AT\r\n");
	SIM_RLM3_UART4_Receive("AT\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("ATE0\r\n");
	SIM_RLM3_UART4_Receive("ATE0\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWAUTOCONN=0\r\n");
	SIM_RLM3_UART4_Receive("FAIL\r\n");

	ASSERT(!RLM3_WIFI_Init());
}

TEST_CASE(RLM3_WIFI_Init_TransferModeFailure)
{
	SIM_RLM3_UART4_Transmit("AT\r\n");
	SIM_RLM3_UART4_Receive("AT\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("ATE0\r\n");
	SIM_RLM3_UART4_Receive("ATE0\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWAUTOCONN=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPMODE=0\r\n");
	SIM_RLM3_UART4_Receive("FAIL\r\n");

	ASSERT(!RLM3_WIFI_Init());
}

TEST_CASE(RLM3_WIFI_DeInit_HappyCase)
{
	SIM_RLM3_UART4_Transmit("AT\r\n");
	SIM_RLM3_UART4_Receive("AT\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("ATE0\r\n");
	SIM_RLM3_UART4_Receive("ATE0\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWAUTOCONN=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPMODE=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");

	RLM3_WIFI_Init();
	RLM3_WIFI_Deinit();
	ASSERT(!RLM3_WIFI_IsInit());
	ASSERT(SIM_GPIO_IsClockEnabled(GPIOG));
	ASSERT(!RLM3_UART4_IsInit());
	ASSERT(!SIM_GPIO_IsEnabled(WIFI_ENABLE_GPIO_Port, WIFI_ENABLE_Pin));
	ASSERT(!SIM_GPIO_IsEnabled(WIFI_BOOT_MODE_GPIO_Port, WIFI_BOOT_MODE_Pin));
	ASSERT(!SIM_GPIO_IsEnabled(WIFI_RESET_GPIO_Port, WIFI_RESET_Pin));
}

TEST_CASE(RLM3_WIFI_GetVersion_HappyCase)
{
	SIM_RLM3_UART4_Transmit("AT\r\n");
	SIM_RLM3_UART4_Receive("AT\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("ATE0\r\n");
	SIM_RLM3_UART4_Receive("ATE0\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWAUTOCONN=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPMODE=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+GMR\r\n");
	SIM_RLM3_UART4_Receive("AT version:255.254.253.252-dev(blah)\r\nSDK version:v251.250.249.248-ge7acblah\r\ncompile time(xxxx)\r\nBin version:2.1.0(Mini)\r\n\r\nOK\r\n");

	RLM3_WIFI_Init();

	uint32_t at_version = 0;
	uint32_t sdk_version = 0;
	ASSERT(RLM3_WIFI_GetVersion(&at_version, &sdk_version));

	ASSERT(at_version == 0xFFFEFDFC);
	ASSERT(sdk_version == 0xFBFAF9F8);
}

TEST_CASE(RLM3_WIFI_GetVersion_Timeout)
{
	SIM_RLM3_UART4_Transmit("AT\r\n");
	SIM_RLM3_UART4_Receive("AT\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("ATE0\r\n");
	SIM_RLM3_UART4_Receive("ATE0\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWAUTOCONN=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPMODE=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+GMR\r\n");

	RLM3_WIFI_Init();
	uint32_t at_version = 0;
	uint32_t sdk_version = 0;
	ASSERT(!RLM3_WIFI_GetVersion(&at_version, &sdk_version));
}

TEST_CASE(RLM3_WIFI_GetVersion_Failure)
{
	SIM_RLM3_UART4_Transmit("AT\r\n");
	SIM_RLM3_UART4_Receive("AT\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("ATE0\r\n");
	SIM_RLM3_UART4_Receive("ATE0\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWAUTOCONN=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPMODE=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+GMR\r\n");
	SIM_RLM3_UART4_Receive("FAIL\r\n");

	RLM3_WIFI_Init();
	uint32_t at_version = 0;
	uint32_t sdk_version = 0;
	ASSERT(!RLM3_WIFI_GetVersion(&at_version, &sdk_version));
}

TEST_CASE(RLM3_WIFI_NetworkConnect_HappyCase)
{
	SIM_RLM3_UART4_Transmit("AT\r\n");
	SIM_RLM3_UART4_Receive("AT\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("ATE0\r\n");
	SIM_RLM3_UART4_Receive("ATE0\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWAUTOCONN=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPMODE=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWJAP_CUR=\"test-sid\",\"test-pwd\"\r\n");
	SIM_AddDelay(10);
	SIM_RLM3_UART4_Receive("WIFI CONNECTED\r\n");
	SIM_AddDelay(10);
	SIM_RLM3_UART4_Receive("WIFI GOT IP\r\n");
	SIM_AddDelay(10);
	SIM_RLM3_UART4_Receive("OK\r\n");

	RLM3_WIFI_Init();
	ASSERT(!RLM3_WIFI_IsNetworkConnected());
	ASSERT(RLM3_WIFI_NetworkConnect("test-sid", "test-pwd"));
	ASSERT(RLM3_WIFI_IsNetworkConnected());
}

TEST_CASE(RLM3_WIFI_NetworkConnect_Error)
{
	SIM_RLM3_UART4_Transmit("AT\r\n");
	SIM_RLM3_UART4_Receive("AT\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("ATE0\r\n");
	SIM_RLM3_UART4_Receive("ATE0\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWAUTOCONN=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPMODE=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWJAP_CUR=\"test-sid\",\"test-pwd\"\r\n");
	SIM_AddDelay(10);
	SIM_RLM3_UART4_Receive("FAIL\r\n");

	RLM3_WIFI_Init();
	ASSERT(!RLM3_WIFI_IsNetworkConnected());
	ASSERT(!RLM3_WIFI_NetworkConnect("test-sid", "test-pwd"));
	ASSERT(!RLM3_WIFI_IsNetworkConnected());
}

TEST_CASE(RLM3_WIFI_NetworkDisconnect_HappyCase)
{
	SIM_RLM3_UART4_Transmit("AT\r\n");
	SIM_RLM3_UART4_Receive("AT\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("ATE0\r\n");
	SIM_RLM3_UART4_Receive("ATE0\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWAUTOCONN=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPMODE=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWJAP_CUR=\"test-sid\",\"test-pwd\"\r\n");
	SIM_RLM3_UART4_Receive("WIFI CONNECTED\r\n");
	SIM_RLM3_UART4_Receive("WIFI GOT IP\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWQAP\r\n");
	SIM_RLM3_UART4_Receive("WIFI DISCONNECT\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");

	RLM3_WIFI_Init();
	RLM3_WIFI_NetworkConnect("test-sid", "test-pwd");
	RLM3_WIFI_NetworkDisconnect();
	ASSERT(!RLM3_WIFI_IsNetworkConnected());
}

TEST_CASE(RLM3_WIFI_NetworkDisconnect_Failure)
{
	SIM_RLM3_UART4_Transmit("AT\r\n");
	SIM_RLM3_UART4_Receive("AT\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("ATE0\r\n");
	SIM_RLM3_UART4_Receive("ATE0\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWAUTOCONN=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPMODE=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWJAP_CUR=\"test-sid\",\"test-pwd\"\r\n");
	SIM_RLM3_UART4_Receive("WIFI CONNECTED\r\n");
	SIM_RLM3_UART4_Receive("WIFI GOT IP\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWQAP\r\n");
	SIM_RLM3_UART4_Receive("WIFI DISCONNECT\r\n");
	SIM_RLM3_UART4_Receive("FAIL\r\n");

	RLM3_WIFI_Init();
	RLM3_WIFI_NetworkConnect("test-sid", "test-pwd");
	RLM3_WIFI_NetworkDisconnect();
	ASSERT(!RLM3_WIFI_IsNetworkConnected());
}

TEST_CASE(RLM3_WIFI_NetworkDisconnect_NotConnected)
{
	SIM_RLM3_UART4_Transmit("AT\r\n");
	SIM_RLM3_UART4_Receive("AT\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("ATE0\r\n");
	SIM_RLM3_UART4_Receive("ATE0\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWAUTOCONN=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPMODE=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");

	RLM3_WIFI_Init();
	RLM3_WIFI_NetworkDisconnect();
	ASSERT(!RLM3_WIFI_IsNetworkConnected());
}

TEST_CASE(RLM3_WIFI_ServerConnect_HappyCase)
{
	SIM_RLM3_UART4_Transmit("AT\r\n");
	SIM_RLM3_UART4_Receive("AT\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("ATE0\r\n");
	SIM_RLM3_UART4_Receive("ATE0\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWAUTOCONN=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPMODE=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWJAP_CUR=\"test-sid\",\"test-pwd\"\r\n");
	SIM_RLM3_UART4_Receive("WIFI CONNECTED\r\n");
	SIM_RLM3_UART4_Receive("WIFI GOT IP\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPSTART=\"TCP\",\"test-server\",test-port\r\n");
	SIM_RLM3_UART4_Receive("CONNECT\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");

	RLM3_WIFI_Init();
	RLM3_WIFI_NetworkConnect("test-sid", "test-pwd");
	ASSERT(RLM3_WIFI_ServerConnect("test-server", "test-port"));
}

TEST_CASE(RLM3_WIFI_ServerConnect_Fail)
{
	SIM_RLM3_UART4_Transmit("AT\r\n");
	SIM_RLM3_UART4_Receive("AT\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("ATE0\r\n");
	SIM_RLM3_UART4_Receive("ATE0\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWAUTOCONN=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPMODE=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWJAP_CUR=\"test-sid\",\"test-pwd\"\r\n");
	SIM_RLM3_UART4_Receive("WIFI CONNECTED\r\n");
	SIM_RLM3_UART4_Receive("WIFI GOT IP\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPSTART=\"TCP\",\"test-server\",test-port\r\n");
	SIM_RLM3_UART4_Receive("FAIL\r\n");

	RLM3_WIFI_Init();
	RLM3_WIFI_NetworkConnect("test-sid", "test-pwd");
	ASSERT(!RLM3_WIFI_ServerConnect("test-server", "test-port"));
}

TEST_CASE(RLM3_WIFI_ServerDisconnect_HappyCase)
{
	SIM_RLM3_UART4_Transmit("AT\r\n");
	SIM_RLM3_UART4_Receive("AT\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("ATE0\r\n");
	SIM_RLM3_UART4_Receive("ATE0\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWAUTOCONN=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPMODE=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWJAP_CUR=\"test-sid\",\"test-pwd\"\r\n");
	SIM_RLM3_UART4_Receive("WIFI CONNECTED\r\n");
	SIM_RLM3_UART4_Receive("WIFI GOT IP\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPSTART=\"TCP\",\"test-server\",test-port\r\n");
	SIM_RLM3_UART4_Receive("CONNECT\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPCLOSE\r\n");
	SIM_RLM3_UART4_Receive("CLOSED\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");

	RLM3_WIFI_Init();
	RLM3_WIFI_NetworkConnect("test-sid", "test-pwd");
	RLM3_WIFI_ServerConnect("test-server", "test-port");
	RLM3_WIFI_ServerDisconnect();
	ASSERT(!RLM3_WIFI_IsServerConnected());
}

TEST_CASE(RLM3_WIFI_ServerDisconnect_Fail)
{
	SIM_RLM3_UART4_Transmit("AT\r\n");
	SIM_RLM3_UART4_Receive("AT\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("ATE0\r\n");
	SIM_RLM3_UART4_Receive("ATE0\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWAUTOCONN=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPMODE=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWJAP_CUR=\"test-sid\",\"test-pwd\"\r\n");
	SIM_RLM3_UART4_Receive("WIFI CONNECTED\r\n");
	SIM_RLM3_UART4_Receive("WIFI GOT IP\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPSTART=\"TCP\",\"test-server\",test-port\r\n");
	SIM_RLM3_UART4_Receive("CONNECT\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPCLOSE\r\n");
	SIM_RLM3_UART4_Receive("FAIL\r\n");

	RLM3_WIFI_Init();
	RLM3_WIFI_NetworkConnect("test-sid", "test-pwd");
	RLM3_WIFI_ServerConnect("test-server", "test-port");
	RLM3_WIFI_ServerDisconnect();
	ASSERT(RLM3_WIFI_IsServerConnected());
}

TEST_CASE(RLM3_WIFI_Transmit_HappyCase)
{
	uint8_t buffer[] = { 'a', 'b', 'c', 'd', 'c', 'b', 'a' };

	SIM_RLM3_UART4_Transmit("AT\r\n");
	SIM_RLM3_UART4_Receive("AT\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("ATE0\r\n");
	SIM_RLM3_UART4_Receive("ATE0\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWAUTOCONN=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPMODE=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWJAP_CUR=\"test-sid\",\"test-pwd\"\r\n");
	SIM_RLM3_UART4_Receive("WIFI CONNECTED\r\n");
	SIM_RLM3_UART4_Receive("WIFI GOT IP\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPSTART=\"TCP\",\"test-server\",test-port\r\n");
	SIM_RLM3_UART4_Receive("CONNECT\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPSEND=7\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Receive("> \r\n");
	SIM_RLM3_UART4_Transmit("abcdcba");
	SIM_RLM3_UART4_Receive("Recv 7 bytes\r\n");
	SIM_RLM3_UART4_Receive("SEND OK\r\n");

	RLM3_WIFI_Init();
	RLM3_WIFI_NetworkConnect("test-sid", "test-pwd");
	RLM3_WIFI_ServerConnect("test-server", "test-port");
	ASSERT(RLM3_WIFI_Transmit(buffer, sizeof(buffer)));
}

TEST_CASE(RLM3_WIFI_Transmit_Empty)
{
	uint8_t buffer[] = { 'a', 'b', 'c', 'd', 'c', 'b', 'a' };

	SIM_RLM3_UART4_Transmit("AT\r\n");
	SIM_RLM3_UART4_Receive("AT\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("ATE0\r\n");
	SIM_RLM3_UART4_Receive("ATE0\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWAUTOCONN=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPMODE=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWJAP_CUR=\"test-sid\",\"test-pwd\"\r\n");
	SIM_RLM3_UART4_Receive("WIFI CONNECTED\r\n");
	SIM_RLM3_UART4_Receive("WIFI GOT IP\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPSTART=\"TCP\",\"test-server\",test-port\r\n");
	SIM_RLM3_UART4_Receive("CONNECT\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");

	RLM3_WIFI_Init();
	RLM3_WIFI_NetworkConnect("test-sid", "test-pwd");
	RLM3_WIFI_ServerConnect("test-server", "test-port");
	ASSERT(!RLM3_WIFI_Transmit(buffer, 0));
}

TEST_CASE(RLM3_WIFI_Transmit_MaxSize)
{
	constexpr size_t BUFFER_SIZE = 1024;
	uint8_t buffer[BUFFER_SIZE + 1] = { 0 };
	for (size_t i = 0; i < BUFFER_SIZE; i++)
		buffer[i] = 'a';

	SIM_RLM3_UART4_Transmit("AT\r\n");
	SIM_RLM3_UART4_Receive("AT\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("ATE0\r\n");
	SIM_RLM3_UART4_Receive("ATE0\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWAUTOCONN=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPMODE=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWJAP_CUR=\"test-sid\",\"test-pwd\"\r\n");
	SIM_RLM3_UART4_Receive("WIFI CONNECTED\r\n");
	SIM_RLM3_UART4_Receive("WIFI GOT IP\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPSTART=\"TCP\",\"test-server\",test-port\r\n");
	SIM_RLM3_UART4_Receive("CONNECT\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPSEND=1024\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Receive("> \r\n");
	SIM_RLM3_UART4_Transmit((const char*)buffer);
	SIM_RLM3_UART4_Receive("Recv 1024 bytes\r\n");
	SIM_RLM3_UART4_Receive("SEND OK\r\n");

	RLM3_WIFI_Init();
	RLM3_WIFI_NetworkConnect("test-sid", "test-pwd");
	RLM3_WIFI_ServerConnect("test-server", "test-port");
	ASSERT(RLM3_WIFI_Transmit(buffer, BUFFER_SIZE));
}

TEST_CASE(RLM3_WIFI_Transmit_OverSize)
{
	constexpr size_t BUFFER_SIZE = 1025;
	uint8_t buffer[BUFFER_SIZE + 1] = { 0 };
	for (size_t i = 0; i < BUFFER_SIZE; i++)
		buffer[i] = 'a';

	SIM_RLM3_UART4_Transmit("AT\r\n");
	SIM_RLM3_UART4_Receive("AT\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("ATE0\r\n");
	SIM_RLM3_UART4_Receive("ATE0\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWAUTOCONN=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPMODE=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWJAP_CUR=\"test-sid\",\"test-pwd\"\r\n");
	SIM_RLM3_UART4_Receive("WIFI CONNECTED\r\n");
	SIM_RLM3_UART4_Receive("WIFI GOT IP\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPSTART=\"TCP\",\"test-server\",test-port\r\n");
	SIM_RLM3_UART4_Receive("CONNECT\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");

	RLM3_WIFI_Init();
	RLM3_WIFI_NetworkConnect("test-sid", "test-pwd");
	RLM3_WIFI_ServerConnect("test-server", "test-port");
	ASSERT(!RLM3_WIFI_Transmit(buffer, sizeof(buffer)));
}

TEST_CASE(RLM3_WIFI_Receive_HappyCase)
{
	SIM_RLM3_UART4_Transmit("AT\r\n");
	SIM_RLM3_UART4_Receive("AT\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("ATE0\r\n");
	SIM_RLM3_UART4_Receive("ATE0\r\nOK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWAUTOCONN=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPMODE=0\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CWJAP_CUR=\"test-sid\",\"test-pwd\"\r\n");
	SIM_RLM3_UART4_Receive("WIFI CONNECTED\r\n");
	SIM_RLM3_UART4_Receive("WIFI GOT IP\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_RLM3_UART4_Transmit("AT+CIPSTART=\"TCP\",\"test-server\",test-port\r\n");
	SIM_RLM3_UART4_Receive("CONNECT\r\n");
	SIM_RLM3_UART4_Receive("OK\r\n");
	SIM_AddDelay(100);
	SIM_RLM3_UART4_Receive("+IPD,5:abcde\r\n");

	g_client_thread = RLM3_GetCurrentTask();;
	g_recv_buffer_size = 0;

	RLM3_WIFI_Init();
	RLM3_WIFI_NetworkConnect("test-sid", "test-pwd");
	RLM3_WIFI_ServerConnect("test-server", "test-port");
	while (g_recv_buffer_size < 5)
		RLM3_Take();
	ASSERT(std::strncmp((const char*)g_recv_buffer_data, "abcde", 5) == 0);

	g_client_thread = nullptr;
}

