#pragma once

#include "rlm3-base.h"
#include "rlm3-task.h"

#ifdef __cplusplus
extern "C" {
#endif


#define RLM3_WIFI_LINK_COUNT (5)


extern bool RLM3_WIFI_Init();
extern void RLM3_WIFI_Deinit();
extern bool RLM3_WIFI_IsInit();

extern bool RLM3_WIFI_GetVersion(uint32_t* at_version, uint32_t* sdk_version);

extern bool RLM3_WIFI_NetworkConnect(const char* ssid, const char* password);
extern void RLM3_WIFI_NetworkDisconnect();
extern bool RLM3_WIFI_IsNetworkConnected();

extern bool RLM3_WIFI_ServerConnect(size_t link_id, const char* server, const char* service);
extern void RLM3_WIFI_ServerDisconnect(size_t link_id);
extern bool RLM3_WIFI_IsServerConnected(size_t link_id);

extern bool RLM3_WIFI_LocalNetworkEnable(const char* ssid, const char* password, size_t max_clients, const char* ip_address, const char* service);
extern void RLM3_WIFI_LocalNetworkDisable();
extern bool RLM3_WIFI_IsLocalNetworkEnabled();

extern bool RLM3_WIFI_Transmit(size_t link_id, const uint8_t* data, size_t size);
extern void RLM3_WIFI_Receive_Callback(size_t link_id, uint8_t data);
extern void RLM3_WIFI_NetworkConnect_Callback(size_t link_id, bool local_connection);
extern void RLM3_WIFI_NetworkDisconnect_Callback(size_t link_id, bool local_connection);


#ifdef __cplusplus
}
#endif
