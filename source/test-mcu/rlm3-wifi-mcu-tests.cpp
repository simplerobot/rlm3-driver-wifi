#include "Test.hpp"
#include "rlm3-wifi.h"
#include "logger.h"

#include <initializer_list>


LOGGER_ZONE(TEST);


TEST_CASE(RLM3_WIFI_Lifecycle)
{
	ASSERT(!RLM3_WIFI_IsInit());
	RLM3_WIFI_Init();
	ASSERT(RLM3_WIFI_IsInit());
	RLM3_WIFI_Deinit();
	ASSERT(!RLM3_WIFI_IsInit());
}

TEST_CASE(RLM3_WIFI_GetVersion)
{
	RLM3_WIFI_Init();
	uint32_t at_version = 0;
	uint32_t sdk_version = 0;
	ASSERT(RLM3_WIFI_GetVersion(&at_version, &sdk_version));
	LOG_ALWAYS("WIFI AT_VERSION: %lX SDK_VERSION: %lX", at_version, sdk_version);
	ASSERT(at_version != 0);
	ASSERT(sdk_version != 0);
	RLM3_WIFI_Deinit();
}

TEST_CASE(RLM3_WIFI_NetworkConnect)
{
	ASSERT(!RLM3_WIFI_IsNetworkConnected());
	RLM3_WIFI_Init();
	ASSERT(!RLM3_WIFI_IsNetworkConnected());
	RLM3_WIFI_NetworkConnect("simplerobots", "gKFAED2xrf258vEp");
	ASSERT(RLM3_WIFI_IsNetworkConnected());
	RLM3_WIFI_Deinit();
}
