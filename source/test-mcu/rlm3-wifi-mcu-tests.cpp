#include "Test.hpp"
#include "rlm3-wifi.h"
#include "rlm3-task.h"
#include "logger.h"
#include <cstring>
#include <initializer_list>


LOGGER_ZONE(TEST);


static size_t g_recv_count = 0;
static uint8_t g_recv_buffer[32];

extern void RLM3_WIFI_Receive_Callback(uint8_t data)
{
	if (g_recv_count < sizeof(g_recv_buffer))
		g_recv_buffer[g_recv_count] = data;
	g_recv_count++;
}

TEST_CASE(RLM3_WIFI_Lifecycle)
{
	ASSERT(!RLM3_WIFI_IsInit());
	ASSERT(RLM3_WIFI_Init());
	ASSERT(RLM3_WIFI_IsInit());
	RLM3_WIFI_Deinit();
	ASSERT(!RLM3_WIFI_IsInit());
}

TEST_CASE(RLM3_WIFI_GetVersion_HappyCase)
{
	ASSERT(RLM3_WIFI_Init());
	uint32_t at_version = 0;
	uint32_t sdk_version = 0;
	ASSERT(RLM3_WIFI_GetVersion(&at_version, &sdk_version));
	LOG_ALWAYS("WIFI AT_VERSION: %lX SDK_VERSION: %lX", at_version, sdk_version);
	ASSERT(at_version != 0);
	ASSERT(sdk_version != 0);
	RLM3_WIFI_Deinit();
}

TEST_CASE(RLM3_WIFI_NetworkConnect_HappyCase)
{
	ASSERT(!RLM3_WIFI_IsNetworkConnected());
	ASSERT(RLM3_WIFI_Init());
	ASSERT(!RLM3_WIFI_IsNetworkConnected());
	ASSERT(RLM3_WIFI_NetworkConnect("simplerobots", "gKFAED2xrf258vEp"));
	ASSERT(RLM3_WIFI_IsNetworkConnected());
	RLM3_WIFI_NetworkDisconnect();
	ASSERT(!RLM3_WIFI_IsNetworkConnected());
	RLM3_WIFI_Deinit();
}

TEST_CASE(RLM3_WIFI_ServerConnect_HappyCase)
{
	ASSERT(RLM3_WIFI_Init());
	ASSERT(RLM3_WIFI_NetworkConnect("simplerobots", "gKFAED2xrf258vEp"));
	ASSERT(!RLM3_WIFI_IsServerConnected());
	ASSERT(RLM3_WIFI_ServerConnect("www.google.com", "80"));
	ASSERT(RLM3_WIFI_IsServerConnected());
	RLM3_WIFI_ServerDisconnect();
	ASSERT(!RLM3_WIFI_IsServerConnected());
	RLM3_WIFI_Deinit();
}

TEST_CASE(RLM3_WIFI_SendReceive)
{
	ASSERT(RLM3_WIFI_Init());
	ASSERT(RLM3_WIFI_NetworkConnect("simplerobots", "gKFAED2xrf258vEp"));
	ASSERT(!RLM3_WIFI_IsServerConnected());
	ASSERT(RLM3_WIFI_ServerConnect("www.google.com", "80"));
	ASSERT(RLM3_WIFI_IsServerConnected());
	g_recv_count = 0;
	const char* command = "GET /\r\n";
	RLM3_WIFI_Transmit((const uint8_t*)command, std::strlen(command));
	RLM3_Delay(1000);
	RLM3_WIFI_ServerDisconnect();
	ASSERT(!RLM3_WIFI_IsServerConnected());
	RLM3_WIFI_Deinit();

	ASSERT(g_recv_count > 1024);
	ASSERT(std::strncmp((char*)g_recv_buffer, "HTTP/1.0 200 OK", 15) == 0);
}

TEST_SETUP(WIFI_LOGGING)
{
	logger_set_level("WIFI", LOGGER_LEVEL_TRACE);
}
