#include "rlm3-wifi.h"
#include "rlm3-uart.h"
#include "rlm3-gpio.h"
#include "rlm3-task.h"
#include "rlm3-string.h"
#include "logger.h"
#include "Assert.h"
#include <stdarg.h>


LOGGER_ZONE(WIFI);


#define MAX_SEND_COMMAND_ARGUMENTS 5


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
	STATE_X_C,
	STATE_X_CLOSED,
	STATE_X_CONNECT,
	STATE_X_DNS_SPACE_Fail,
	STATE_X_ERROR,
	STATE_X_FAIL,
	STATE_X_NN,
	STATE_X_NN_COMMA_SEND_SPACE_OK,
	STATE_X_no_SPACE_ip,
	STATE_X_OK,
	STATE_X_PLUS,
	STATE_X_PLUS_C,
	STATE_X_PLUS_CWJAP_COLON,
	STATE_X_PLUS_IPD_COMMA_NN,
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


static const uint32_t COMMAND_OK						= 1 << 0;  // 0x000001
static const uint32_t COMMAND_ERROR						= 1 << 1;  // 0x000002
static const uint32_t COMMAND_FAIL						= 1 << 2;  // 0x000004
static const uint32_t COMMAND_CONNECTION_TIMEOUT		= 1 << 3;  // 0x000008
static const uint32_t COMMAND_CONNECTION_WRONG_PASSWORD	= 1 << 4;  // 0x000010
static const uint32_t COMMAND_CONNECTION_MISSING_AP		= 1 << 5;  // 0x000020
static const uint32_t COMMAND_CONNECTION_FAILED			= 1 << 6;  // 0x000040
static const uint32_t COMMAND_SEND_OK					= 1 << 7;  // 0x000080
static const uint32_t COMMAND_SEND_FAIL					= 1 << 8;  // 0x000100
static const uint32_t COMMAND_GO_AHEAD					= 1 << 9;  // 0x000200
static const uint32_t COMMAND_ALREADY_CONNECTED			= 1 << 10; // 0x000400
static const uint32_t COMMAND_WIFI_CONNECTED			= 1 << 11; // 0x000800
static const uint32_t COMMAND_WIFI_DISCONNECT			= 1 << 12; // 0x001000
static const uint32_t COMMAND_WIFI_GOT_IP				= 1 << 13; // 0x002000
static const uint32_t COMMAND_CLOSED					= 1 << 14; // 0x004000
static const uint32_t COMMAND_CONNECT					= 1 << 15; // 0x008000
static const uint32_t COMMAND_BYTES_RECEIVED			= 1 << 16; // 0x010000
static const uint32_t COMMAND_DNS_FAIL					= 1 << 17; // 0x020000


static State g_state = STATE_INITIAL;
static const char* g_expected = NULL;


static volatile RLM3_Task g_client_thread = NULL;
static volatile uint32_t g_commands = 0;
static const char* volatile* g_transmit_data = NULL;
static volatile uint32_t g_transmit_count = 0;

static volatile bool g_wifi_connected = false;
static volatile bool g_wifi_has_ip = false;
static volatile bool g_tcp_connected = false;
static volatile uint32_t g_segment_count = 0;

static uint8_t g_sub_version = 0;
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
	g_commands = 0;
	g_client_thread = RLM3_GetCurrentTask();
}

static void EndCommand()
{
	g_client_thread = NULL;
}

static bool WaitForResponse(const char* action, uint32_t timeout, uint32_t pass_commands, uint32_t fail_commands)
{
	RLM3_Time start_time = RLM3_GetCurrentTime();

	// Wait until the server notifies us of one of the monitored commands.
	uint32_t monitored_commands = pass_commands | fail_commands;
	while ((g_commands & monitored_commands) == 0 && RLM3_TakeUntil(start_time, timeout))
		;

	if ((g_commands & fail_commands) != 0)
	{
		LOG_WARN("Fail %s %x", action, (int)g_commands);
		return false;
	}

	if ((g_commands & pass_commands) == 0)
	{
		LOG_WARN("Timeout %s %x", action, (int)g_commands);
		return false;
	}

	return true;
}

static void SendRaw(const uint8_t* buffer, size_t size)
{
	if (size == 0)
		return;
	g_transmit_count = size;
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

	g_transmit_count = 0;
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
	bool result = WaitForResponse(action, timeout, COMMAND_OK, COMMAND_ERROR | COMMAND_FAIL);
	EndCommand();

	va_end(args);

	return result;
}

static void NotifyCommand(uint32_t command)
{
	g_commands |= command;
	RLM3_GiveFromISR(g_client_thread);
}

extern bool RLM3_WIFI_Init()
{
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
	g_tcp_connected = false;
	g_segment_count = 0;
	g_receive_length = 0;
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
		result = SendCommandStandard("manual_connect", 1000, "AT+CWAUTOCONN=0", NULL);
	if (result)
		result = SendCommandStandard("transfer_mode", 1000, "AT+CIPMODE=0", NULL);
	return result;
}

extern void RLM3_WIFI_Deinit()
{
	RLM3_UART4_Deinit();

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
	RLM3_WIFI_NetworkDisconnect();

	BeginCommand();

	bool result = true;
	if (result)
		Send("network_connect_a", "AT+CWJAP_CUR=\"", ssid, "\",\"", password, "\"", NULL);
	if (result)
		result = WaitForResponse("network_connect_b", 30000, COMMAND_OK, COMMAND_ERROR | COMMAND_FAIL);
	if (result)
		result = WaitForResponse("network_connect_c", 30000, COMMAND_WIFI_CONNECTED, COMMAND_CONNECTION_TIMEOUT | COMMAND_CONNECTION_WRONG_PASSWORD | COMMAND_CONNECTION_MISSING_AP | COMMAND_CONNECTION_FAILED | COMMAND_ALREADY_CONNECTED | COMMAND_WIFI_DISCONNECT);
	if (result)
		result = WaitForResponse("network_connect_d", 30000, COMMAND_WIFI_GOT_IP, COMMAND_CONNECTION_TIMEOUT | COMMAND_CONNECTION_WRONG_PASSWORD | COMMAND_CONNECTION_MISSING_AP | COMMAND_CONNECTION_FAILED | COMMAND_ALREADY_CONNECTED | COMMAND_WIFI_DISCONNECT);
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
			result = WaitForResponse("network_disconnect_b", 1000, COMMAND_OK, COMMAND_ERROR | COMMAND_FAIL);
		if (result)
			WaitForResponse("network_disconnect_c", 1000, COMMAND_WIFI_DISCONNECT, 0);
	}

	EndCommand();
}

extern bool RLM3_WIFI_IsNetworkConnected()
{
	return g_wifi_connected && g_wifi_has_ip;
}

extern bool RLM3_WIFI_ServerConnect(const char* server, const char* service)
{
	RLM3_WIFI_ServerDisconnect();

	BeginCommand();

	bool result = true;
	if (result)
		Send("tcp_connect_a", "AT+CIPSTART=\"TCP\",\"", server, "\",", service, NULL);
	if (result)
		result = WaitForResponse("tcp_connect_b", 30000, COMMAND_OK, COMMAND_ERROR | COMMAND_FAIL);
	if (result)
		result = WaitForResponse("tcp_connect_c", 30000, COMMAND_CONNECT, COMMAND_CONNECTION_TIMEOUT | COMMAND_CONNECTION_WRONG_PASSWORD | COMMAND_CONNECTION_MISSING_AP | COMMAND_CONNECTION_FAILED | COMMAND_WIFI_DISCONNECT | COMMAND_CLOSED | COMMAND_DNS_FAIL);

	EndCommand();

	return result;
}

extern void RLM3_WIFI_ServerDisconnect()
{
	BeginCommand();

	if (g_tcp_connected)
	{
		bool result = true;
		if (result)
			Send("tcp_disconnect_a", "AT+CIPCLOSE", NULL);
		if (result)
			result = WaitForResponse("tcp_disconnect_b", 1000, COMMAND_OK, COMMAND_ERROR | COMMAND_FAIL);
		if (result)
			result = WaitForResponse("tcp_disconnect_c", 1000, COMMAND_CLOSED, 0);
	}

	EndCommand();
}

extern bool RLM3_WIFI_IsServerConnected()
{
	return g_tcp_connected;
}

extern bool RLM3_WIFI_Transmit(const uint8_t* data, size_t size)
{
	// We only support small blocks for now.
	if (0 >= size || size > 1024)
		return false;
	char size_str[5];
	if (!RLM3_UIntToString(size, size_str, sizeof(size_str)))
		return false;

	BeginCommand();
	Send("transmit_a", "AT+CIPSEND=", size_str, NULL);

	bool result = true;
	if (result)
		result = WaitForResponse("transmit_b", 10000, COMMAND_OK, COMMAND_ERROR | COMMAND_FAIL);
	if (result)
		result = WaitForResponse("transmit_c", 10000, COMMAND_GO_AHEAD, COMMAND_ERROR | COMMAND_FAIL);
	if (result)
		SendRaw(data, size);
	if (result)
		result = WaitForResponse("transmit_d", 10000, COMMAND_BYTES_RECEIVED, COMMAND_ERROR | COMMAND_FAIL);
	if (result)
		result = WaitForResponse("transmit_e", 10000, COMMAND_SEND_OK, COMMAND_ERROR | COMMAND_FAIL | COMMAND_SEND_FAIL);
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
		RLM3_WIFI_Receive_Callback(x);
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
		if (x == 'C') { next = STATE_X_C; }
		if (x == 'D') { next = STATE_X_DNS_SPACE_Fail; g_expected = "NS Fail"; }
		if (x == 'E') { next = STATE_X_ERROR; g_expected = "RROR"; }
		if (x == 'F') { next = STATE_X_FAIL; g_expected = "AIL"; }
		if (x == 'n') { next = STATE_X_no_SPACE_ip; g_expected = "o ip"; }
		if (x == 'O') { next = STATE_X_OK; g_expected = "K"; }
		if (x == 'R') { next = STATE_X_Recv_SPACE_NN; g_expected = "ecv "; }
		if (x == 'S') { next = STATE_X_S; }
		if (x == 'W') { next = STATE_X_WIFI_SPACE; g_expected = "IFI "; }
		if (x >= '0' && x <= '9') { next = STATE_X_NN; }
		break;

	case STATE_X_PLUS:
		if (x == 'I') { next = STATE_X_PLUS_IPD_COMMA_NN; g_expected = "PD,"; g_receive_length = 0; }
		if (x == 'C') { next = STATE_X_PLUS_C; }
		break;

	case STATE_X_A:
		if (x == 'T') { next = STATE_X_AT; }
		if (x == 'L') { next = STATE_X_ALREADY_SPACE_CONNECT; g_expected = "READY CONNECT"; }
		if (x == 'i') { next = STATE_IGNORE_NEXT_LINE; g_expected = "-Thinker"; }
		break;

	case STATE_X_AT:
		next = STATE_END;
		if (x == ' ') { next = STATE_X_AT_SPACE_version_COLON_NN; g_expected = "version:"; g_at_version = 0; g_sub_version = 0; }
		break;

	case STATE_X_AT_SPACE_version_COLON_NN:
		if (x >= '0' && x <= '9') { next = STATE_X_AT_SPACE_version_COLON_NN; g_sub_version = 10 * g_sub_version + x - '0'; }
		if (x == 'v') { next = STATE_X_AT_SPACE_version_COLON_NN; }
		if (x == '.') { next = STATE_X_AT_SPACE_version_COLON_NN; }
		if (x == '(' || x == '-' || x == '\r') { next = STATE_END; }
		if (x == '.' || x == '(' || x == '-' || x == '\r') { g_at_version = (g_at_version << 8) | g_sub_version; g_sub_version = 0; }
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

	case STATE_X_C:
		if (x == 'L') { next = STATE_X_CLOSED; g_expected = "OSED"; }
		if (x == 'O') { next = STATE_X_CONNECT; g_expected = "NNECT"; }
		break;

	case STATE_X_CLOSED:
		if (x == '\r') { next = STATE_END; g_tcp_connected = false; NotifyCommand(COMMAND_CLOSED); }
		break;

	case STATE_X_CONNECT:
		if (x == '\r') { next = STATE_END; g_tcp_connected = true; NotifyCommand(COMMAND_CONNECT); }
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
		if (x == '\r') { next = STATE_END; g_wifi_has_ip = false; g_tcp_connected = false; }
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
		if (x == 'D') { next = STATE_X_SDK_SPACE_version_COLON_NN; g_expected = "K version:"; g_sdk_version = 0; g_sub_version = 0; }
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
		if (x >= '0' && x <= '9') { next = STATE_X_SDK_SPACE_version_COLON_NN; g_sub_version = 10 * g_sub_version + x - '0'; }
		if (x == 'v') { next = STATE_X_SDK_SPACE_version_COLON_NN; }
		if (x == '.') { next = STATE_X_SDK_SPACE_version_COLON_NN; }
		if (x == '(' || x == '-') { next = STATE_END; }
		if (x == '\r') { next = STATE_END; }
		if (x == '.' || x == '(' || x == '-' || x == '\r') { g_sdk_version = (g_sdk_version << 8) | g_sub_version; g_sub_version = 0; }
		break;

	case STATE_X_PLUS_IPD_COMMA_NN:
		if (x >= '0' && x <= '9') { next = STATE_X_PLUS_IPD_COMMA_NN; g_receive_length = 10 * g_receive_length + x - '0'; }
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
		if (x == '\r') { next = STATE_END; g_wifi_connected = false; g_wifi_has_ip = false; g_tcp_connected = false; NotifyCommand(COMMAND_WIFI_DISCONNECT); }
		break;

	case STATE_X_WIFI_SPACE_GOT_SPACE_IP:
		if (x == '\r') { next = STATE_END; g_wifi_has_ip = true; NotifyCommand(COMMAND_WIFI_GOT_IP); }
		break;

	case STATE_X_NN:
		if (x >= '0' && x <= '9') { next = STATE_X_NN; }
		if (x == ',') { next = STATE_X_NN_COMMA_SEND_SPACE_OK; g_expected = "SEND OK"; }
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
	if (g_transmit_count > 0)
	{
		// Move onto the next byte.
		if (--g_transmit_count == 0)
		{
			g_transmit_data = NULL;
			NotifyCommand(0);
		}
	}
	else
	{
		// Otherwise, this string is done when we reach a nul character and all strings are done once we reach a NULL string.
		if (**g_transmit_data == 0 && *(++g_transmit_data) == NULL)
		{
			g_transmit_data = NULL;
			NotifyCommand(0);
		}
	}

	return true;
}

extern void RLM3_UART4_ErrorCallback(uint32_t status_flags)
{
#ifdef TEST
//	LOG_ERROR("UART Error %x", (int)status_flags);
	g_error_count++;
#endif
}

extern __attribute__((weak)) void RLM3_WIFI_Receive_Callback(uint8_t data)
{
	// DO NOT MODIFIY THIS FUNCTION.  Override it by declaring a non-weak version in your project files.
}

