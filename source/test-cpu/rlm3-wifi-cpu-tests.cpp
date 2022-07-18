#include "Test.hpp"
#include "rlm3-wifi.h"
#include "Mock.hpp"
#include "rlm3-gpio.h"
#include "rlm3-task.h"
#include "rlm3-uart.h"
#include <cstring>
#include <vector>
#include "logger.h"


static void DoTransmit(const char* expected_text)
{
	for (const char* cursor = expected_text; *cursor != 0; cursor++)
	{
		uint8_t actual = 0;
		ASSERT(RLM3_UART4_TransmitCallback(&actual));
		ASSERT(actual == *cursor);
	}
	uint8_t actual = 0;
	bool result = RLM3_UART4_TransmitCallback(&actual);
	ASSERT(!result);
}

static void DoRecieve(const char* text)
{
	for (const char* cursor = text; *cursor != 0; cursor++)
		RLM3_UART4_ReceiveCallback(*cursor);
}

static void ExpectSendCommand(const char* transmit, RLM3_Time timeout, const std::vector<const char*>& response)
{
	EXPECT(RLM3_GetCurrentTask())_AND_RETURN((void*)1);
	EXPECT(RLM3_UART4_EnsureTransmit());
	EXPECT(RLM3_Take());
	EXPECT(RLM3_Take())_AND_DO(DoTransmit(transmit));
	EXPECT(RLM3_GiveFromISR((void*)1));
	EXPECT(RLM3_GetCurrentTime())_AND_RETURN((RLM3_Time)5);
	EXPECT(RLM3_TakeUntil(5, timeout))_AND_RETURN(true);
	for (size_t i = 0; i < response.size(); i++)
	{
		EXPECT(RLM3_TakeUntil(5, timeout))_AND_DO(DoRecieve(response[i]))_AND_RETURN(true);
		EXPECT(RLM3_GiveFromISR((void*)1));
	}
}

static void ExpectSendCommand(const char* transmit, RLM3_Time timeout, const char* response)
{
	std::vector<const char*> v = { response };
	ExpectSendCommand(transmit, timeout, v);
}

static void ExpectInit()
{
	EXPECT(__HAL_RCC_GPIOG_CLK_ENABLE());
	EXPECT(HAL_GPIO_WritePin(GPIOG, WIFI_ENABLE_Pin | WIFI_BOOT_MODE_Pin | WIFI_RESET_Pin, GPIO_PIN_RESET));
	GPIO_InitTypeDef GPIO_InitStruct = { 0 };
	GPIO_InitStruct.Pin = WIFI_ENABLE_Pin | WIFI_BOOT_MODE_Pin | WIFI_RESET_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	EXPECT(HAL_GPIO_Init(GPIOG, &GPIO_InitStruct));
	EXPECT(RLM3_Delay(5));
	EXPECT(RLM3_UART4_Init(115200));
	EXPECT(HAL_GPIO_WritePin(GPIOG, WIFI_ENABLE_Pin, GPIO_PIN_SET));
	EXPECT(RLM3_Delay(50));
	EXPECT(HAL_GPIO_WritePin(GPIOG, WIFI_RESET_Pin, GPIO_PIN_SET));
	EXPECT(RLM3_Delay(5000));
	ExpectSendCommand("AT\r\n", 100, "AT\r\nOK\r");
	ExpectSendCommand("ATE0\r\n", 1000, "\nATE0\r\nOK\r");
	ExpectSendCommand("AT+CWAUTOCONN=0\r\n", 1000, "\nOK\r");
	ExpectSendCommand("AT+CIPMODE=0\r\n", 1000, "\nOK\r");
}

static void ExpectDeinit()
{
	EXPECT(HAL_GPIO_WritePin(GPIOG, WIFI_ENABLE_Pin | WIFI_BOOT_MODE_Pin | WIFI_RESET_Pin, GPIO_PIN_RESET));
	EXPECT(RLM3_UART4_Deinit());
	EXPECT(HAL_GPIO_DeInit(GPIOG, WIFI_ENABLE_Pin | WIFI_BOOT_MODE_Pin | WIFI_RESET_Pin));
}

TEST_CASE(RLM3_WIFI_Init_HappyCase)
{
	ExpectInit();

	RLM3_WIFI_Init();
}

TEST_CASE(RLM3_WIFI_IsInit_HappyCase)
{
	EXPECT(RLM3_UART4_IsInit())_AND_RETURN(false);
	EXPECT(RLM3_UART4_IsInit())_AND_RETURN(true);

	ASSERT(!RLM3_WIFI_IsInit());
	ASSERT(RLM3_WIFI_IsInit());
}

TEST_CASE(RLM3_WIFI_Deinit_HappyCase)
{
	ExpectInit();
	ExpectDeinit();

	RLM3_WIFI_Init();
	RLM3_WIFI_Deinit();
}

TEST_CASE(RLM3_WIFI_GetVersion_HappyCase)
{
	ExpectInit();
	ExpectSendCommand("AT+GMR\r\n", 1000, "\nAT version:255.254.253.252-dev(blah)\r\nSDK version:v251.250.249.248-ge7acblah\r\ncompile time(xxxx)\r\nBin version:2.1.0(Mini)\r\n\r\nOK\r");
	ExpectDeinit();

	RLM3_WIFI_Init();
	uint32_t at_version = 0;
	uint32_t sdk_version = 0;
	ASSERT(RLM3_WIFI_GetVersion(&at_version, &sdk_version));
	ASSERT(at_version == 0xFFFEFDFC);
	ASSERT(sdk_version == 0xFBFAF9F8);
	RLM3_WIFI_Deinit();
}

TEST_CASE(RLM3_WIFI_GetVersion_Failure)
{
	ExpectInit();
	ExpectSendCommand("AT+GMR\r\n", 1000, "\nFAIL\r");
	ExpectDeinit();

	RLM3_WIFI_Init();
	uint32_t at_version = 0;
	uint32_t sdk_version = 0;
	ASSERT(!RLM3_WIFI_GetVersion(&at_version, &sdk_version));
	ASSERT(at_version == 0);
	ASSERT(sdk_version == 0);
	RLM3_WIFI_Deinit();
}

TEST_CASE(RLM3_WIFI_NetworkConnect_HappyCase)
{
	ExpectInit();
	ExpectSendCommand("AT+CWJAP_CUR=\"test-sid\",\"test-pwd\"\r\n", 30000, { "\nWIFI CONNECTED\r", "\nWIFI GOT IP\r", "\n\r\nOK\r" });
	ExpectDeinit();

	RLM3_WIFI_Init();
	ASSERT(!RLM3_WIFI_IsNetworkConnected());
	ASSERT(RLM3_WIFI_NetworkConnect("test-sid", "test-pwd"));
	ASSERT(RLM3_WIFI_IsNetworkConnected());
	RLM3_WIFI_Deinit();
}

TEST_CASE(RLM3_WIFI_NetworkConnect_Error)
{
	ExpectInit();
	ExpectSendCommand("AT+CWJAP_CUR=\"test-sid\",\"test-pwd\"\r\n", 30000, { "\nERROR\r" });
	ExpectDeinit();

	RLM3_WIFI_Init();
	ASSERT(!RLM3_WIFI_IsNetworkConnected());
	ASSERT(!RLM3_WIFI_NetworkConnect("test-sid", "test-pwd"));
	ASSERT(!RLM3_WIFI_IsNetworkConnected());
	RLM3_WIFI_Deinit();
}

TEST_CASE(RLM3_WIFI_NetworkDisconnect_HappyCase)
{
	ExpectInit();
	ExpectSendCommand("AT+CWJAP_CUR=\"test-sid\",\"test-pwd\"\r\n", 30000, { "\nWIFI CONNECTED\r", "\nWIFI GOT IP\r", "\n\r\nOK\r" });
	ExpectSendCommand("AT+CWQAP\r\n", 1000, { "\nWIFI DISCONNECT\r", "\nOK\r" });
	ExpectDeinit();

	RLM3_WIFI_Init();
	ASSERT(!RLM3_WIFI_IsNetworkConnected());
	RLM3_WIFI_NetworkConnect("test-sid", "test-pwd");
	ASSERT(RLM3_WIFI_IsNetworkConnected());
	RLM3_WIFI_NetworkDisconnect();
	ASSERT(!RLM3_WIFI_IsNetworkConnected());
	RLM3_WIFI_Deinit();
}

TEST_CASE(RLM3_WIFI_NetworkDisconnect_Failure)
{
	ExpectInit();
	ExpectSendCommand("AT+CWJAP_CUR=\"test-sid\",\"test-pwd\"\r\n", 30000, { "\nWIFI CONNECTED\r", "\nWIFI GOT IP\r", "\n\r\nOK\r" });
	ExpectSendCommand("AT+CWQAP\r\n", 1000, { "\nFAIL\r" });
	ExpectDeinit();

	RLM3_WIFI_Init();
	ASSERT(!RLM3_WIFI_IsNetworkConnected());
	RLM3_WIFI_NetworkConnect("test-sid", "test-pwd");
	ASSERT(RLM3_WIFI_IsNetworkConnected());
	RLM3_WIFI_NetworkDisconnect();
	ASSERT(RLM3_WIFI_IsNetworkConnected());
	RLM3_WIFI_Deinit();
}

TEST_CASE(RLM3_WIFI_WaitNetworkConnected_HappyCase)
{
	ExpectInit();
	ExpectSendCommand("AT+CWJAP_CUR=\"test-sid\",\"test-pwd\"\r\n", 30000, "\n\r\nOK\r");
	EXPECT(RLM3_GetCurrentTime())_AND_RETURN((RLM3_Time)5);
	EXPECT(RLM3_GetCurrentTask())_AND_RETURN((void*)1);
	EXPECT(RLM3_TakeUntil(5, 15))_AND_RETURN(true);
	EXPECT(RLM3_TakeUntil(5, 15))_AND_DO(DoRecieve("\nWIFI CONNECTED\r"))_AND_RETURN(true);
	EXPECT(RLM3_GiveFromISR((void*)1));
	EXPECT(RLM3_TakeUntil(5, 15))_AND_DO(DoRecieve("\nWIFI GOT IP\r"))_AND_RETURN(true);
	EXPECT(RLM3_GiveFromISR((void*)1));
	ExpectDeinit();

	RLM3_WIFI_Init();
	ASSERT(!RLM3_WIFI_IsNetworkConnected());
	RLM3_WIFI_NetworkConnect("test-sid", "test-pwd");
	ASSERT(!RLM3_WIFI_IsNetworkConnected());
	ASSERT(RLM3_WIFI_WaitNetworkConnected(15));
	ASSERT(RLM3_WIFI_IsNetworkConnected());
	RLM3_WIFI_Deinit();
}

TEST_CASE(RLM3_WIFI_WaitNetworkConnected_Timeout)
{
	ExpectInit();
	ExpectSendCommand("AT+CWJAP_CUR=\"test-sid\",\"test-pwd\"\r\n", 30000, "\n\r\nOK\r");
	EXPECT(RLM3_GetCurrentTime())_AND_RETURN((RLM3_Time)5);
	EXPECT(RLM3_GetCurrentTask())_AND_RETURN((void*)1);
	EXPECT(RLM3_TakeUntil(5, 15))_AND_RETURN(false);
	ExpectDeinit();

	RLM3_WIFI_Init();
	ASSERT(!RLM3_WIFI_IsNetworkConnected());
	RLM3_WIFI_NetworkConnect("test-sid", "test-pwd");
	ASSERT(!RLM3_WIFI_IsNetworkConnected());
	ASSERT(!RLM3_WIFI_WaitNetworkConnected(15));
	ASSERT(!RLM3_WIFI_IsNetworkConnected());
	RLM3_WIFI_Deinit();
}

TEST_CASE(RLM3_WIFI_ServerConnect_HappyCase)
{
	ExpectInit();
	ExpectSendCommand("AT+CWJAP_CUR=\"test-sid\",\"test-pwd\"\r\n", 30000, { "\nWIFI CONNECTED\r", "\nWIFI GOT IP\r", "\n\r\nOK\r" });
	ExpectSendCommand("AT+CIPCLOSE\r\n", 10000, "\nFAIL\r");
	ExpectSendCommand("AT+CIPSTART=\"TCP\",\"test-server\",test-port\r\n", 20000, { "\nCONNECT\r", "\n\r\nOK\r" });
	ExpectDeinit();

	RLM3_WIFI_Init();
	ASSERT(!RLM3_WIFI_IsServerConnected());
	ASSERT(RLM3_WIFI_NetworkConnect("test-sid", "test-pwd"));
	ASSERT(!RLM3_WIFI_IsServerConnected());
	ASSERT(RLM3_WIFI_ServerConnect("test-server", "test-port"));
	ASSERT(RLM3_WIFI_IsServerConnected());
	RLM3_WIFI_Deinit();
}

TEST_CASE(RLM3_WIFI_ServerConnect_Fail)
{
	ExpectInit();
	ExpectSendCommand("AT+CWJAP_CUR=\"test-sid\",\"test-pwd\"\r\n", 30000, { "\nWIFI CONNECTED\r", "\nWIFI GOT IP\r", "\n\r\nOK\r" });
	ExpectSendCommand("AT+CIPCLOSE\r\n", 10000, "\nOK\r");
	ExpectSendCommand("AT+CIPSTART=\"TCP\",\"test-server\",test-port\r\n", 20000, "\n\r\nFAIL\r" );
	ExpectDeinit();

	RLM3_WIFI_Init();
	ASSERT(!RLM3_WIFI_IsServerConnected());
	ASSERT(RLM3_WIFI_NetworkConnect("test-sid", "test-pwd"));
	ASSERT(!RLM3_WIFI_IsServerConnected());
	ASSERT(!RLM3_WIFI_ServerConnect("test-server", "test-port"));
	ASSERT(!RLM3_WIFI_IsServerConnected());
	RLM3_WIFI_Deinit();
}

TEST_CASE(RLM3_WIFI_ServerDisconnect_HappyCase)
{
	ExpectInit();
	ExpectSendCommand("AT+CWJAP_CUR=\"test-sid\",\"test-pwd\"\r\n", 30000, { "\nWIFI CONNECTED\r", "\nWIFI GOT IP\r", "\n\r\nOK\r" });
	ExpectSendCommand("AT+CIPCLOSE\r\n", 10000, "\nFAIL\r");
	ExpectSendCommand("AT+CIPSTART=\"TCP\",\"test-server\",test-port\r\n", 20000, { "\nCONNECT\r", "\n\r\nOK\r" });
	ExpectSendCommand("AT+CIPCLOSE\r\n", 10000, { "\nCLOSED\r", "\nOK\r" });
	ExpectDeinit();

	RLM3_WIFI_Init();
	ASSERT(RLM3_WIFI_NetworkConnect("test-sid", "test-pwd"));
	ASSERT(RLM3_WIFI_ServerConnect("test-server", "test-port"));
	RLM3_WIFI_ServerDisconnect();
	ASSERT(!RLM3_WIFI_IsServerConnected());
	RLM3_WIFI_Deinit();
}

TEST_CASE(RLM3_WIFI_ServerDisconnect_Fail)
{
	ExpectInit();
	ExpectSendCommand("AT+CWJAP_CUR=\"test-sid\",\"test-pwd\"\r\n", 30000, { "\nWIFI CONNECTED\r", "\nWIFI GOT IP\r", "\n\r\nOK\r" });
	ExpectSendCommand("AT+CIPCLOSE\r\n", 10000, "\nFAIL\r");
	ExpectSendCommand("AT+CIPSTART=\"TCP\",\"test-server\",test-port\r\n", 20000, { "\nCONNECT\r", "\n\r\nOK\r" });
	ExpectSendCommand("AT+CIPCLOSE\r\n", 10000, "\nFAIL\r");
	ExpectDeinit();

	RLM3_WIFI_Init();
	ASSERT(RLM3_WIFI_NetworkConnect("test-sid", "test-pwd"));
	ASSERT(RLM3_WIFI_ServerConnect("test-server", "test-port"));
	RLM3_WIFI_ServerDisconnect();
	ASSERT(RLM3_WIFI_IsServerConnected());
	RLM3_WIFI_Deinit();
}

TEST_CASE(RLM3_WIFI_WaitServerConnected_HappyCase)
{
	ExpectInit();
	ExpectSendCommand("AT+CWJAP_CUR=\"test-sid\",\"test-pwd\"\r\n", 30000, { "\nWIFI CONNECTED\r", "\nWIFI GOT IP\r", "\n\r\nOK\r" });
	ExpectSendCommand("AT+CIPCLOSE\r\n", 10000, "\nFAIL\r");
	ExpectSendCommand("AT+CIPSTART=\"TCP\",\"test-server\",test-port\r\n", 20000, "\n\r\nOK\r");
	EXPECT(RLM3_GetCurrentTime())_AND_RETURN((RLM3_Time)5);
	EXPECT(RLM3_GetCurrentTask())_AND_RETURN((void*)1);
	EXPECT(RLM3_TakeUntil(5, 15))_AND_RETURN(true);
	EXPECT(RLM3_TakeUntil(5, 15))_AND_DO(DoRecieve("\nCONNECT\r"))_AND_RETURN(true);
	EXPECT(RLM3_GiveFromISR((void*)1));
	ExpectDeinit();

	RLM3_WIFI_Init();
	ASSERT(RLM3_WIFI_NetworkConnect("test-sid", "test-pwd"));
	ASSERT(RLM3_WIFI_ServerConnect("test-server", "test-port"));
	ASSERT(RLM3_WIFI_WaitServerConnected(15));
	RLM3_WIFI_Deinit();
}

TEST_CASE(RLM3_WIFI_WaitServerConnected_Timeout)
{
	ExpectInit();
	ExpectSendCommand("AT+CWJAP_CUR=\"test-sid\",\"test-pwd\"\r\n", 30000, { "\nWIFI CONNECTED\r", "\nWIFI GOT IP\r", "\n\r\nOK\r" });
	ExpectSendCommand("AT+CIPCLOSE\r\n", 10000, "\nFAIL\r");
	ExpectSendCommand("AT+CIPSTART=\"TCP\",\"test-server\",test-port\r\n", 20000, "\n\r\nOK\r");
	EXPECT(RLM3_GetCurrentTime())_AND_RETURN((RLM3_Time)5);
	EXPECT(RLM3_GetCurrentTask())_AND_RETURN((void*)1);
	EXPECT(RLM3_TakeUntil(5, 15))_AND_RETURN(false);
	ExpectDeinit();

	RLM3_WIFI_Init();
	ASSERT(RLM3_WIFI_NetworkConnect("test-sid", "test-pwd"));
	ASSERT(RLM3_WIFI_ServerConnect("test-server", "test-port"));
	ASSERT(!RLM3_WIFI_WaitServerConnected(15));
	RLM3_WIFI_Deinit();
}

TEST_CASE(RLM3_WIFI_Transmit_HappyCase)
{
	ExpectInit();
	ExpectSendCommand("AT+CWJAP_CUR=\"test-sid\",\"test-pwd\"\r\n", 30000, { "\nWIFI CONNECTED\r", "\nWIFI GOT IP\r", "\n\r\nOK\r" });
	ExpectSendCommand("AT+CIPCLOSE\r\n", 10000, "\nFAIL\r");
	ExpectSendCommand("AT+CIPSTART=\"TCP\",\"test-server\",test-port\r\n", 20000, { "\nCONNECT\r", "\n\r\nOK\r" });
	ExpectSendCommand("AT+CIPSEND=7\r\n", 10000, { "\nOK\r" });
	EXPECT(RLM3_GetCurrentTime())_AND_RETURN((RLM3_Time)5);
	EXPECT(RLM3_TakeUntil(5, 10000))_AND_DO(DoRecieve("\n>"))_AND_RETURN(true);
	EXPECT(RLM3_GiveFromISR((void*)1));
	EXPECT(RLM3_UART4_EnsureTransmit());
	EXPECT(RLM3_Take())_AND_DO(DoTransmit("abcdcba"));
	EXPECT(RLM3_GiveFromISR((void*)1));
	EXPECT(RLM3_GetCurrentTime())_AND_RETURN((RLM3_Time)5);
	EXPECT(RLM3_TakeUntil(5, 10000))_AND_DO(DoRecieve("\r\nRecv 7 bytes\r"))_AND_RETURN(true);
	EXPECT(RLM3_GiveFromISR((void*)1));
	EXPECT(RLM3_GetCurrentTime())_AND_RETURN((RLM3_Time)5);
	EXPECT(RLM3_TakeUntil(5, 10000))_AND_DO(DoRecieve("\r\nSEND OK\r"))_AND_RETURN(true);
	EXPECT(RLM3_GiveFromISR((void*)1));
	ExpectDeinit();

	RLM3_WIFI_Init();
	ASSERT(RLM3_WIFI_NetworkConnect("test-sid", "test-pwd"));
	ASSERT(RLM3_WIFI_ServerConnect("test-server", "test-port"));
	const uint8_t buffer[] = { 'a', 'b', 'c', 'd', 'c', 'b', 'a' };
	ASSERT(RLM3_WIFI_Transmit(buffer, 7));
	RLM3_WIFI_Deinit();
}




/*

	BeginCommand();
	Send("AT+CIPSEND=", size_str, NULL);

	bool result = true;
	if (result)
		result = WaitForResponse("transmit_a", 10000, COMMAND_OK, COMMAND_ERROR | COMMAND_FAIL);
	if (result)
		result = WaitForResponse("transmit_b", 10000, COMMAND_GO_AHEAD, COMMAND_ERROR | COMMAND_FAIL);
	if (result)
		SendRaw(data, size);
	if (result)
		result = WaitForResponse("transmit_c", 10000, COMMAND_BYTES_RECEIVED, COMMAND_ERROR | COMMAND_FAIL);
	if (result)
		result = WaitForResponse("transmit_d", 10000, COMMAND_OK, COMMAND_ERROR | COMMAND_FAIL | COMMAND_SEND_FAIL);
	EndCommand();

	return result;

	Max size
	Too Big
	Fail in multiple steps.


extern bool RLM3_WIFI_Transmit(const uint8_t* data, size_t size);
extern void RLM3_WIFI_Receive_Callback(uint8_t data);

 */
