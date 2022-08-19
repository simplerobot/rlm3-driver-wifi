#include "rlm3-wifi.h"
#include "rlm3-uart.h"
#include "rlm3-gpio.h"
#include "rlm3-task.h"
#include "rlm3-string.h"
#include "logger.h"
#include "Assert.h"
#include <stdarg.h>


LOGGER_ZONE(WIFI);


#define MAX_SEND_COMMAND_ARGUMENTS (7)


typedef enum State
{
	STATE_INITIAL,
	STATE_INVALID,
	STATE_READ_DATA,
	STATE_IGNORE_NEXT_LINE,
	STATE_END,
	// The STATE_X states contain the text actually received with SPACE, COMMA, COLON, DASH, DOT, ANY, and NN tokens.
	STATE_X_A,
	STATE_X_ALREADY_SPACE_CONNECT,
	STATE_X_AT,
	STATE_X_AT_SPACE_version_COLON_NN,
	STATE_X_busy_SPACE,
	STATE_X_busy_SPACE_p_DOT_DOT_DOT,
	STATE_X_busy_SPACE_s_DOT_DOT_DOT,
	STATE_X_DNS_SPACE_Fail,
	STATE_X_ERROR,
	STATE_X_FAIL,
	STATE_X_NN,
	STATE_X_NN_COMMA,
	STATE_X_NN_COMMA_C,
	STATE_X_NN_COMMA_CLOSED,
	STATE_X_NN_COMMA_CONNECT,
	STATE_X_NN_COMMA_SEND_SPACE_OK,
	STATE_X_no_SPACE_ip,
	STATE_X_OK,
	STATE_X_PLUS,
	STATE_X_PLUS_C,
	STATE_X_PLUS_CWJAP_COLON,
	STATE_X_PLUS_IPD_COMMA_NN,
	STATE_X_PLUS_IPD_COMMA_NN_COMMA,
	STATE_X_Recv_SPACE_NN,
	STATE_X_Recv_SPACE_NN_SPACE_bytes,
	STATE_X_S,
	STATE_X_SDK_SPACE_version_COLON_NN,
	STATE_X_SEND_SPACE,
	STATE_X_SEND_SPACE_FAIL,
	STATE_X_SEND_SPACE_OK,
	STATE_X_WIFI_SPACE,
	STATE_X_WIFI_SPACE_CONNECTED,
	STATE_X_WIFI_SPACE_DISCONNECT,
	STATE_X_WIFI_SPACE_GOT_SPACE_IP,
} State;

#define FLAG(COMMAND) (1 << (COMMAND))

typedef enum Command
{
	COMMAND_OK,
	COMMAND_ERROR,
	COMMAND_FAIL,
	COMMAND_CONNECTION_TIMEOUT,
	COMMAND_CONNECTION_WRONG_PASSWORD,
	COMMAND_CONNECTION_MISSING_AP,
	COMMAND_CONNECTION_FAILED,
	COMMAND_SEND_OK,
	COMMAND_SEND_FAIL,
	COMMAND_GO_AHEAD,
	COMMAND_ALREADY_CONNECTED,
	COMMAND_WIFI_CONNECTED,
	COMMAND_WIFI_DISCONNECT,
	COMMAND_WIFI_GOT_IP,
	COMMAND_BYTES_RECEIVED,
	COMMAND_DNS_FAIL,
	COMMAND_CLOSED_BEGIN,
	COMMAND_CLOSED_END = COMMAND_CLOSED_BEGIN + RLM3_WIFI_LINK_COUNT - 1,
	COMMAND_CONNECT_BEGIN,
	COMMAND_CONNECT_END = COMMAND_CONNECT_BEGIN + RLM3_WIFI_LINK_COUNT - 1,
	COMMAND_COUNT
} Command;


static State g_state = STATE_INITIAL;
static const char* g_expected = NULL;

static volatile RLM3_Task g_client_thread = NULL;
static volatile uint32_t g_command_flags = 0;
static const char* volatile* g_transmit_data = NULL;
static volatile uint32_t g_raw_transmit_count = 0;

static bool g_is_local_network_enabled = false;

static volatile bool g_wifi_connected = false;
static volatile bool g_wifi_has_ip = false;
static volatile bool g_is_tcp_outgoing[RLM3_WIFI_LINK_COUNT] = { 0 };
static volatile bool g_tcp_connected[RLM3_WIFI_LINK_COUNT] = { 0 };
static volatile uint32_t g_segment_count = 0;

static uint8_t g_number = 0;
static volatile uint32_t g_at_version = 0;
static volatile uint32_t g_sdk_version = 0;
static uint32_t g_receive_length = 0;

#ifdef TEST
static uint8_t g_invalid_buffer[32];
static uint32_t g_invalid_buffer_length = 0;
static State g_last_valid_state = STATE_INVALID;
static uint32_t g_invalid_count = 0;
static uint32_t g_error_count = 0;
#endif


static void BeginCommand()
{
	ASSERT(g_client_thread == NULL);
	g_command_flags = 0;
	g_client_thread = RLM3_GetCurrentTask();
}

static void EndCommand()
{
	g_client_thread = NULL;
}

static bool WaitForResponse(const char* action, uint32_t timeout, uint32_t pass_command_flags, uint32_t fail_command_flags)
{
	RLM3_Time start_time = RLM3_GetCurrentTime();

	// Wait until the server notifies us of one of the monitored commands.
	uint32_t monitored_command_flags = pass_command_flags | fail_command_flags;
	while ((g_command_flags & monitored_command_flags) == 0 && RLM3_TakeUntil(start_time, timeout))
		;

	if ((g_command_flags & fail_command_flags) != 0)
	{
		LOG_WARN("Fail %s %x", action, (int)g_command_flags);
		return false;
	}

	if ((g_command_flags & pass_command_flags) == 0)
	{
		LOG_WARN("Timeout %s %x", action, (int)g_command_flags);
		return false;
	}

	return true;
}

static void SendRaw(const uint8_t* buffer, size_t size)
{
	if (size == 0)
		return;
	g_raw_transmit_count = size;
	const char* data = (const char*)buffer;
	g_transmit_data = &data;
	RLM3_UART4_EnsureTransmit();
	while (g_transmit_data != NULL)
		RLM3_Take();
}

static void SendV(const char* action, va_list args)
{
	const char* command_data[MAX_SEND_COMMAND_ARGUMENTS + 2];
	size_t command_count = 0;

	const char* arg = va_arg(args, const char*);
	while (command_count < MAX_SEND_COMMAND_ARGUMENTS && arg != NULL)
	{
		if (*arg != 0)
			command_data[command_count++] = arg;
		arg = va_arg(args, const char*);
	}
	ASSERT(arg == NULL);
	command_data[command_count++] = "\r\n";
	command_data[command_count++] = NULL;

	g_raw_transmit_count = 0;
	g_transmit_data = command_data;
	RLM3_UART4_EnsureTransmit();
	while (g_transmit_data != NULL)
		RLM3_Take();
}

static void __attribute__((sentinel)) Send(const char* action, ...)
{
	va_list args;
	va_start(args, action);
	SendV(action, args);
	va_end(args);
}

static bool __attribute__((sentinel)) SendCommandStandard(const char* action, uint32_t timeout, ...)
{
	va_list args;
	va_start(args, timeout);

	BeginCommand();
	SendV(action, args);
	bool result = WaitForResponse(action, timeout, FLAG(COMMAND_OK), FLAG(COMMAND_ERROR) | FLAG(COMMAND_FAIL));
	EndCommand();

	va_end(args);

	return result;
}

static void NotifyCommand(Command command)
{
	g_command_flags |= FLAG(command);
	RLM3_GiveFromISR(g_client_thread);
}

static void NotifyConnectToServer(size_t link_id)
{
	if (link_id >= RLM3_WIFI_LINK_COUNT)
		return;
	g_tcp_connected[link_id] = true;
	NotifyCommand((Command)(COMMAND_CONNECT_BEGIN + link_id));
	RLM3_WIFI_NetworkConnect_Callback(link_id, !g_is_tcp_outgoing[g_number]);
}

static void NotifyDisconnectFromServer(size_t link_id)
{
	if (link_id >= RLM3_WIFI_LINK_COUNT)
		return;
	if (!g_tcp_connected[g_number])
		return;
	NotifyCommand((Command)(COMMAND_CLOSED_BEGIN + link_id));
	RLM3_WIFI_NetworkDisconnect_Callback(link_id, !g_is_tcp_outgoing[g_number]);
	g_is_tcp_outgoing[link_id] = false;
	g_tcp_connected[link_id] = false;
}

static void NotifyDisconnectFromAllServers()
{
	for (size_t i = 0; i < RLM3_WIFI_LINK_COUNT; i++)
		NotifyDisconnectFromServer(i);
}

extern bool RLM3_WIFI_Init()
{
	ASSERT(COMMAND_COUNT < 32);

	if (RLM3_UART4_IsInit())
		RLM3_UART4_Deinit();

	__HAL_RCC_GPIOG_CLK_ENABLE();

	HAL_GPIO_WritePin(GPIOG, WIFI_ENABLE_Pin | WIFI_BOOT_MODE_Pin | WIFI_RESET_Pin, GPIO_PIN_RESET);

	GPIO_InitTypeDef GPIO_InitStruct = { 0 };
	GPIO_InitStruct.Pin = WIFI_ENABLE_Pin | WIFI_BOOT_MODE_Pin | WIFI_RESET_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

	g_transmit_data = NULL;
	g_state = STATE_INITIAL;
	g_expected = NULL;
	g_wifi_has_ip = false;
	g_wifi_connected = false;
	for (size_t i = 0; i < RLM3_WIFI_LINK_COUNT; i++)
	{
		g_is_tcp_outgoing[i] = false;
		g_tcp_connected[i] = false;
	}
	g_segment_count = 0;
	g_receive_length = 0;
	g_client_thread = NULL;
	g_is_local_network_enabled = false;

#ifdef TEST
	g_invalid_buffer_length = 0;
	g_last_valid_state = STATE_INVALID;
	g_invalid_count = 0;
	g_error_count = 0;
#endif

	HAL_GPIO_WritePin(GPIOG, WIFI_BOOT_MODE_Pin, GPIO_PIN_SET);
	HAL_GPIO_WritePin(GPIOG, WIFI_RESET_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(GPIOG, WIFI_ENABLE_Pin, GPIO_PIN_SET);
	RLM3_Delay(10);

	HAL_GPIO_WritePin(GPIOG, WIFI_RESET_Pin, GPIO_PIN_SET);
	RLM3_Delay(990);

	RLM3_UART4_Init(115200);

	bool result = true;
	if (result)
		result = SendCommandStandard("ping", 100, "AT", NULL);
	if (result)
		result = SendCommandStandard("disable_echo", 1000, "ATE0", NULL);
	if (result)
		result = SendCommandStandard("transfer_mode", 1000, "AT+CIPMODE=0", NULL);
	if (result)
		result = SendCommandStandard("multiple_connections", 1000, "AT+CIPMUX=1", NULL);
	if (result)
		result = SendCommandStandard("wifi_mode", 1000, "AT+CWMODE_CUR=1", NULL);
	if (result)
		result = SendCommandStandard("manual_connect", 1000, "AT+CWAUTOCONN=0", NULL);

	return result;
}

extern void RLM3_WIFI_Deinit()
{
	RLM3_UART4_Deinit();

	NotifyDisconnectFromAllServers();

	HAL_GPIO_WritePin(GPIOG, WIFI_ENABLE_Pin | WIFI_BOOT_MODE_Pin | WIFI_RESET_Pin, GPIO_PIN_RESET);
	HAL_GPIO_DeInit(GPIOG, WIFI_ENABLE_Pin | WIFI_BOOT_MODE_Pin | WIFI_RESET_Pin);

#ifdef TEST
	LOG_ALWAYS("Invalid %d Error %d", (int)g_invalid_count, (int)g_error_count);
#endif
}

extern bool RLM3_WIFI_IsInit()
{
	return RLM3_UART4_IsInit();
}

extern bool RLM3_WIFI_GetVersion(uint32_t* at_version, uint32_t* sdk_version)
{
	if (!SendCommandStandard("get_version", 1000, "AT+GMR", NULL))
		return false;
	*at_version = g_at_version;
	*sdk_version = g_sdk_version;
	return true;
}

extern bool RLM3_WIFI_NetworkConnect(const char* ssid, const char* password)
{
	ASSERT(RLM3_WIFI_IsInit());

	RLM3_WIFI_NetworkDisconnect();

	BeginCommand();

	bool result = true;
	if (result)
		Send("network_connect_a", "AT+CWJAP_CUR=\"", ssid, "\",\"", password, "\"", NULL);
	if (result)
		result = WaitForResponse("network_connect_b", 30000, FLAG(COMMAND_OK), FLAG(COMMAND_ERROR) | FLAG(COMMAND_FAIL));
	if (result)
		result = WaitForResponse("network_connect_c", 30000, FLAG(COMMAND_WIFI_CONNECTED), FLAG(COMMAND_CONNECTION_TIMEOUT) | FLAG(COMMAND_CONNECTION_WRONG_PASSWORD) | FLAG(COMMAND_CONNECTION_MISSING_AP) | FLAG(COMMAND_CONNECTION_FAILED) | FLAG(COMMAND_ALREADY_CONNECTED));
	if (result)
		result = WaitForResponse("network_connect_d", 30000, FLAG(COMMAND_WIFI_GOT_IP), FLAG(COMMAND_CONNECTION_TIMEOUT) | FLAG(COMMAND_CONNECTION_WRONG_PASSWORD) | FLAG(COMMAND_CONNECTION_MISSING_AP) | FLAG(COMMAND_CONNECTION_FAILED) | FLAG(COMMAND_ALREADY_CONNECTED));
	EndCommand();

	return result;
}

extern void RLM3_WIFI_NetworkDisconnect()
{
	BeginCommand();

	if (g_wifi_connected)
	{
		bool result = true;
		if (result)
			Send("network_disconnect_a", "AT+CWQAP", NULL);
		if (result)
			result = WaitForResponse("network_disconnect_b", 1000, FLAG(COMMAND_OK), FLAG(COMMAND_ERROR) | FLAG(COMMAND_FAIL));
		if (result)
			WaitForResponse("network_disconnect_c", 10000, FLAG(COMMAND_WIFI_DISCONNECT), 0);
	}

	EndCommand();
}

extern bool RLM3_WIFI_IsNetworkConnected()
{
	return g_wifi_connected && g_wifi_has_ip;
}

extern bool RLM3_WIFI_ServerConnect(size_t link_id, const char* server, const char* service)
{
	if (link_id >= RLM3_WIFI_LINK_COUNT)
		return false;

	RLM3_WIFI_ServerDisconnect(link_id);

	BeginCommand();

	char link_id_str[2] = { 0 };
	link_id_str[0] = '0' + link_id;

	g_is_tcp_outgoing[link_id] = true;

	bool result = true;
	if (result)
		Send("tcp_connect_a", "AT+CIPSTART=", link_id_str ,",\"TCP\",\"", server, "\",", service, NULL);
	if (result)
		result = WaitForResponse("tcp_connect_b", 30000, FLAG(COMMAND_OK), FLAG(COMMAND_ERROR) | FLAG(COMMAND_FAIL));
	if (result)
		result = WaitForResponse("tcp_connect_c", 30000, FLAG(COMMAND_CONNECT_BEGIN + link_id), FLAG(COMMAND_CONNECTION_TIMEOUT) | FLAG(COMMAND_CONNECTION_WRONG_PASSWORD) | FLAG(COMMAND_CONNECTION_MISSING_AP) | FLAG(COMMAND_CONNECTION_FAILED) | FLAG(COMMAND_WIFI_DISCONNECT) | FLAG(COMMAND_CLOSED_BEGIN + link_id) | FLAG(COMMAND_DNS_FAIL));

	EndCommand();

	return result;
}

extern void RLM3_WIFI_ServerDisconnect(size_t link_id)
{
	if (link_id >= RLM3_WIFI_LINK_COUNT)
		return;

	BeginCommand();

	if (g_tcp_connected[link_id])
	{
		char link_id_str[2] = { 0 };
		link_id_str[0] = '0' + link_id;

		bool result = true;
		if (result)
			Send("tcp_disconnect_a", "AT+CIPCLOSE=", link_id_str, NULL);
		if (result)
			result = WaitForResponse("tcp_disconnect_b", 1000, FLAG(COMMAND_OK), FLAG(COMMAND_ERROR) | FLAG(COMMAND_FAIL));
		if (result)
			result = WaitForResponse("tcp_disconnect_c", 1000, FLAG(COMMAND_CLOSED_BEGIN + link_id), 0);
	}

	EndCommand();
}

extern bool RLM3_WIFI_IsServerConnected(size_t link_id)
{
	if (link_id >= RLM3_WIFI_LINK_COUNT)
		return false;

	return g_tcp_connected[link_id];
}

extern bool RLM3_WIFI_LocalNetworkEnable(const char* ssid, const char* password, size_t max_clients, const char* ip_address, const char* service)
{
	if (max_clients > RLM3_WIFI_LINK_COUNT)
		max_clients = RLM3_WIFI_LINK_COUNT;
	char max_clients_str[2];
	RLM3_Format(max_clients_str, sizeof(max_clients_str), "%u", (unsigned int)max_clients);

	bool result = true;

	if (result)
		result = SendCommandStandard("wifi_mode", 1000, "AT+CWMODE_CUR=3", NULL);
	if (result)
		result = SendCommandStandard("ap_ip", 1000, "AT+CIPAP_CUR=\"", ip_address, "\"", NULL);
	if (result)
		result = SendCommandStandard("ap_set", 1000, "AT+CWSAP_CUR=\"", ssid, "\",\"", password, "\",1,3,", max_clients_str, ",0", NULL); // Channel 1, Encryption using WPA2_PSK, SSID broadcast
	if (result)
		result = SendCommandStandard("server", 1000, "AT+CIPSERVER=1,", service, NULL);

	g_is_local_network_enabled = result;

	return result;
}

extern void RLM3_WIFI_LocalNetworkDisable()
{
	bool result = true;

	if (result)
		result = SendCommandStandard("server", 1000, "AT+CIPSERVER=0", NULL);
	if (result)
		result = SendCommandStandard("wifi_mode", 1000, "AT+CWMODE_CUR=1", NULL);

	g_is_local_network_enabled = false;
}

extern bool RLM3_WIFI_IsLocalNetworkEnabled()
{
	return g_is_local_network_enabled;
}

extern bool RLM3_WIFI_Transmit(size_t link_id, const uint8_t* data, size_t size)
{
	if (link_id >= RLM3_WIFI_LINK_COUNT)
		return false;

	// We only support small blocks for now.
	if (0 >= size || size > 1024)
		return false;
	char size_str[5];
	RLM3_Format(size_str, sizeof(size_str), "%u", (unsigned int)size);
	char link_id_str[2] = { 0 };
	link_id_str[0] = '0' + link_id;

	BeginCommand();
	Send("transmit_a", "AT+CIPSEND=", link_id_str, ",", size_str, NULL);

	bool result = true;
	if (result)
		result = WaitForResponse("transmit_b", 10000, FLAG(COMMAND_OK), FLAG(COMMAND_ERROR) | FLAG(COMMAND_FAIL));
	if (result)
		result = WaitForResponse("transmit_c", 10000, FLAG(COMMAND_GO_AHEAD), FLAG(COMMAND_ERROR) | FLAG(COMMAND_FAIL));
	if (result)
		SendRaw(data, size);
	if (result)
		result = WaitForResponse("transmit_d", 10000, FLAG(COMMAND_BYTES_RECEIVED), FLAG(COMMAND_ERROR) | FLAG(COMMAND_FAIL));
	if (result)
		result = WaitForResponse("transmit_e", 10000, FLAG(COMMAND_SEND_OK), FLAG(COMMAND_ERROR) | FLAG(COMMAND_FAIL) | FLAG(COMMAND_SEND_FAIL));
	EndCommand();

	return result;
}

extern void RLM3_UART4_ReceiveCallback(uint8_t x)
{
	if (IS_LOG_TRACE() && x != '\r')
		RLM3_DebugOutputFromISR(x);

	// If we are expecting something specific, make sure that's what we get.
	if (g_expected != NULL)
	{
		uint8_t expected = *(g_expected++);
		if (x != expected)
		{
			LOG_ERROR("Expect %x '%c' Actual %x '%c' State %d", expected, expected, x, (x >= 0x20 && x <= 0x7F) ? x : '?', g_state);
			g_expected = NULL;
			g_state = STATE_INVALID;
		}
		else if (*g_expected == 0)
		{
			g_expected = NULL;
		}
		return;
	}

	State next = STATE_INVALID;
	switch (g_state)
	{
	case STATE_INVALID:
		// Recover once we see a '\n' or a '\r'.
		if (x == '\r' || x == '\n') { next = STATE_INITIAL; }
		break;

	case STATE_END:
		next = STATE_END;
		if (x == '\n') { next = STATE_INITIAL; }
		break;

	case STATE_IGNORE_NEXT_LINE:
		next = STATE_IGNORE_NEXT_LINE;
		if (x == '\n') { next = STATE_END; }
		break;

	case STATE_READ_DATA:
		RLM3_WIFI_Receive_Callback(g_number, x);
		next = STATE_READ_DATA;
		if (--g_receive_length == 0)
			next = STATE_INITIAL;
		break;

	case STATE_INITIAL:
		if (x == ' ' || x == '\r' || x == '\n' || x == 0xff || x == 0xfe) { next = STATE_INITIAL; }
		if (x == '+') { next = STATE_X_PLUS; }
		if (x == '>') { next = STATE_INITIAL; NotifyCommand(COMMAND_GO_AHEAD); }
		if (x == 'A') { next = STATE_X_A; }
		if (x == 'B') { next = STATE_END; g_expected = "in version"; }
		if (x == 'b') { next = STATE_X_busy_SPACE; g_expected = "usy "; }
		if (x == 'c') { next = STATE_END; g_expected = "ompile time"; }
		if (x == 'D') { next = STATE_X_DNS_SPACE_Fail; g_expected = "NS Fail"; }
		if (x == 'E') { next = STATE_X_ERROR; g_expected = "RROR"; }
		if (x == 'F') { next = STATE_X_FAIL; g_expected = "AIL"; }
		if (x == 'n') { next = STATE_X_no_SPACE_ip; g_expected = "o ip"; }
		if (x == 'O') { next = STATE_X_OK; g_expected = "K"; }
		if (x == 'R') { next = STATE_X_Recv_SPACE_NN; g_expected = "ecv "; }
		if (x == 'S') { next = STATE_X_S; }
		if (x == 'W') { next = STATE_X_WIFI_SPACE; g_expected = "IFI "; }
		if (x >= '0' && x <= '9') { next = STATE_X_NN; g_number = x - '0'; }
		break;

	case STATE_X_PLUS:
		if (x == 'I') { next = STATE_X_PLUS_IPD_COMMA_NN; g_expected = "PD,"; g_number = 0; g_receive_length = 0; }
		if (x == 'C') { next = STATE_X_PLUS_C; }
		break;

	case STATE_X_A:
		if (x == 'T') { next = STATE_X_AT; }
		if (x == 'L') { next = STATE_X_ALREADY_SPACE_CONNECT; g_expected = "READY CONNECT"; }
		if (x == 'i') { next = STATE_IGNORE_NEXT_LINE; g_expected = "-Thinker"; }
		break;

	case STATE_X_AT:
		next = STATE_END;
		if (x == ' ') { next = STATE_X_AT_SPACE_version_COLON_NN; g_expected = "version:"; g_at_version = 0; g_number = 0; }
		break;

	case STATE_X_AT_SPACE_version_COLON_NN:
		if (x >= '0' && x <= '9') { next = STATE_X_AT_SPACE_version_COLON_NN; g_number = 10 * g_number + x - '0'; }
		if (x == 'v') { next = STATE_X_AT_SPACE_version_COLON_NN; }
		if (x == '.') { next = STATE_X_AT_SPACE_version_COLON_NN; }
		if (x == '(' || x == '-' || x == '\r') { next = STATE_END; }
		if (x == '.' || x == '(' || x == '-' || x == '\r') { g_at_version = (g_at_version << 8) | g_number; g_number = 0; }
		break;

	case STATE_X_ALREADY_SPACE_CONNECT:
		if (x == '\r') { next = STATE_END; NotifyCommand(COMMAND_ALREADY_CONNECTED); }
		break;

	case STATE_X_busy_SPACE:
		if (x == 's') { next = STATE_X_busy_SPACE_s_DOT_DOT_DOT; g_expected = "..."; }
		if (x == 'p') { next = STATE_X_busy_SPACE_p_DOT_DOT_DOT; g_expected = "..."; }
		break;

	case STATE_X_busy_SPACE_s_DOT_DOT_DOT:
		LOG_INFO("Busy %d Segments", (int)g_segment_count);
		if (x == '\r') { next = STATE_END; }
		break;

	case STATE_X_busy_SPACE_p_DOT_DOT_DOT:
		LOG_INFO("Busy With Command");
		if (x == '\r') { next = STATE_END; }
		break;

	case STATE_X_DNS_SPACE_Fail:
		if (x == '\r') { next = STATE_END; NotifyCommand(COMMAND_DNS_FAIL); }
		break;

	case STATE_X_ERROR:
		if (x == '\r') { next = STATE_END; NotifyCommand(COMMAND_ERROR); }
		break;

	case STATE_X_FAIL:
		if (x == '\r') { next = STATE_END; NotifyCommand(COMMAND_FAIL); }
		break;

	case STATE_X_no_SPACE_ip:
		if (x == '\r') { next = STATE_END; g_wifi_has_ip = false; NotifyDisconnectFromAllServers(); }
		break;

	case STATE_X_OK:
		if (x == '\r') { next = STATE_END; NotifyCommand(COMMAND_OK); }
		break;

	case STATE_X_Recv_SPACE_NN:
		if (x >= '0' && x <= '9') { next = STATE_X_Recv_SPACE_NN; }
		if (x == ' ') { next = STATE_X_Recv_SPACE_NN_SPACE_bytes; g_expected = "bytes"; }
		break;

	case STATE_X_Recv_SPACE_NN_SPACE_bytes:
		if (x == '\r') { next = STATE_END; g_segment_count++; NotifyCommand(COMMAND_BYTES_RECEIVED); }
		break;

	case STATE_X_S:
		if (x == 'E') { next = STATE_X_SEND_SPACE; g_expected = "ND "; }
		if (x == 'D') { next = STATE_X_SDK_SPACE_version_COLON_NN; g_expected = "K version:"; g_sdk_version = 0; g_number = 0; }
		if (x == 'T') { next = STATE_END; g_expected = "ATUS:"; }
		break;

	case STATE_X_SEND_SPACE:
		if (x == 'O') { next = STATE_X_SEND_SPACE_OK; g_expected = "K"; }
		if (x == 'F') { next = STATE_X_SEND_SPACE_FAIL; g_expected = "AIL"; }
		break;

	case STATE_X_SEND_SPACE_OK:
		if (x == '\r') { next = STATE_END; NotifyCommand(COMMAND_SEND_OK); }
		break;

	case STATE_X_SEND_SPACE_FAIL:
		if (x == '\r') { next = STATE_END; NotifyCommand(COMMAND_SEND_FAIL); }
		break;

	case STATE_X_SDK_SPACE_version_COLON_NN:
		if (x >= '0' && x <= '9') { next = STATE_X_SDK_SPACE_version_COLON_NN; g_number = 10 * g_number + x - '0'; }
		if (x == 'v') { next = STATE_X_SDK_SPACE_version_COLON_NN; }
		if (x == '.') { next = STATE_X_SDK_SPACE_version_COLON_NN; }
		if (x == '(' || x == '-') { next = STATE_END; }
		if (x == '\r') { next = STATE_END; }
		if (x == '.' || x == '(' || x == '-' || x == '\r') { g_sdk_version = (g_sdk_version << 8) | g_number; g_number = 0; }
		break;

	case STATE_X_PLUS_IPD_COMMA_NN:
		if (x >= '0' && x <= '9') { next = STATE_X_PLUS_IPD_COMMA_NN; g_number = 10 * g_number + x - '0'; }
		if (x == ',') { next = STATE_X_PLUS_IPD_COMMA_NN_COMMA; }
		break;

	case STATE_X_PLUS_IPD_COMMA_NN_COMMA:
		if (x >= '0' && x <= '9') { next = STATE_X_PLUS_IPD_COMMA_NN_COMMA; g_receive_length = 10 * g_receive_length + x - '0'; }
		if (x == ':') { next = STATE_READ_DATA; }
		break;

	case STATE_X_PLUS_C:
		if (x == 'I') { next = STATE_END; }
		if (x == 'W') { next = STATE_X_PLUS_CWJAP_COLON; g_expected = "JAP:"; }
		break;

	case STATE_X_PLUS_CWJAP_COLON:
		if (x == '1') NotifyCommand(COMMAND_CONNECTION_TIMEOUT);
		else if (x == '2') NotifyCommand(COMMAND_CONNECTION_WRONG_PASSWORD);
		else if (x == '3') NotifyCommand(COMMAND_CONNECTION_MISSING_AP);
		else if (x == '4') NotifyCommand(COMMAND_CONNECTION_FAILED);
		else NotifyCommand(COMMAND_CONNECTION_TIMEOUT);
		next = STATE_END;
		break;

	case STATE_X_WIFI_SPACE:
		if (x == 'C') { next = STATE_X_WIFI_SPACE_CONNECTED; g_expected = "ONNECTED"; }
		if (x == 'D') { next = STATE_X_WIFI_SPACE_DISCONNECT; g_expected = "ISCONNECT"; }
		if (x == 'G') { next = STATE_X_WIFI_SPACE_GOT_SPACE_IP; g_expected = "OT IP"; }
		break;

	case STATE_X_WIFI_SPACE_CONNECTED:
		if (x == '\r') { next = STATE_END; g_wifi_connected = true; NotifyCommand(COMMAND_WIFI_CONNECTED); }
		break;

	case STATE_X_WIFI_SPACE_DISCONNECT:
		if (x == '\r') { next = STATE_END; g_wifi_connected = false; g_wifi_has_ip = false; NotifyDisconnectFromAllServers(); NotifyCommand(COMMAND_WIFI_DISCONNECT); }
		break;

	case STATE_X_WIFI_SPACE_GOT_SPACE_IP:
		if (x == '\r') { next = STATE_END; g_wifi_has_ip = true; NotifyCommand(COMMAND_WIFI_GOT_IP); }
		break;

	case STATE_X_NN:
		if (x >= '0' && x <= '9') { next = STATE_X_NN; g_number = 10 * g_number + x - '0'; }
		if (x == ',') { next = STATE_X_NN_COMMA; }
		break;

	case STATE_X_NN_COMMA:
		if (x == 'C') { next = STATE_X_NN_COMMA_C; }
		if (x == 'S') { next = STATE_X_NN_COMMA_SEND_SPACE_OK; g_expected = "END OK"; }
		break;

	case STATE_X_NN_COMMA_C:
		if (x == 'L') { next = STATE_X_NN_COMMA_CLOSED; g_expected = "OSED"; }
		if (x == 'O') { next = STATE_X_NN_COMMA_CONNECT; g_expected = "NNECT"; }
		break;

	case STATE_X_NN_COMMA_CLOSED:
		if (x == '\r') { next = STATE_END; NotifyDisconnectFromServer(g_number); }
		break;

	case STATE_X_NN_COMMA_CONNECT:
		if (x == '\r') { next = STATE_END; NotifyConnectToServer(g_number); }
		break;

	case STATE_X_NN_COMMA_SEND_SPACE_OK:
		if (x == '\r') { next = STATE_END; g_segment_count--; }
		break;
	}

#ifdef TEST
	if (g_invalid_buffer_length > 0)
	{
		if (next != STATE_INVALID || g_invalid_buffer_length + 2 >= sizeof(g_invalid_buffer))
		{
			g_invalid_buffer[g_invalid_buffer_length++] = 0;
			LOG_ERROR("Invalid State %d '%s'", g_last_valid_state, g_invalid_buffer);
			g_invalid_buffer_length = 0;
		}
	}

	if (next != STATE_INVALID)
		g_last_valid_state = next;
	else
		g_invalid_buffer[g_invalid_buffer_length++] = x;

	if (next == STATE_INVALID)
		g_invalid_count++;
#endif

	g_state = next;
}

extern bool RLM3_UART4_TransmitCallback(uint8_t* data_to_send)
{
	if (g_transmit_data == NULL)
		return false;

	// Send this byte and move to the next.
	uint8_t x = **g_transmit_data;
	*data_to_send = x;
	(*g_transmit_data)++;

	if (IS_LOG_TRACE() && x != '\r')
		RLM3_DebugOutputFromISR(x);

	// If this is binary data, we only have one buffer and we are done when it is sent.
	if (g_raw_transmit_count > 0)
	{
		// Move onto the next byte.
		if (--g_raw_transmit_count == 0)
		{
			g_transmit_data = NULL;
			RLM3_GiveFromISR(g_client_thread);
		}
	}
	else
	{
		// Otherwise, this string is done when we reach a nul character and all strings are done once we reach a NULL string.
		if (**g_transmit_data == 0 && *(++g_transmit_data) == NULL)
		{
			g_transmit_data = NULL;
			RLM3_GiveFromISR(g_client_thread);
		}
	}

	return true;
}

extern void RLM3_UART4_ErrorCallback(uint32_t status_flags)
{
	LOG_WARN("UART Error %x", (int)status_flags);
	g_state = STATE_INVALID;
#ifdef TEST
	g_error_count++;
#endif
}

extern __attribute__((weak)) void RLM3_WIFI_Receive_Callback(size_t link_id, uint8_t data)
{
	// DO NOT MODIFIY THIS FUNCTION.  Override it by declaring a non-weak version in your project files.
}

extern __attribute__((weak)) void RLM3_WIFI_NetworkConnect_Callback(size_t link_id, bool local_connection)
{
	// DO NOT MODIFIY THIS FUNCTION.  Override it by declaring a non-weak version in your project files.
}

extern __attribute__((weak)) void RLM3_WIFI_NetworkDisconnect_Callback(size_t link_id, bool local_connection)
{
	// DO NOT MODIFIY THIS FUNCTION.  Override it by declaring a non-weak version in your project files.
}

