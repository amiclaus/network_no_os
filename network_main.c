/***************************************************************************//**
 *   @file   main.c
 *   @brief  Main file for aducm3029 platform of iio_demo project.
 *   @author RBolboac (ramona.bolboaca@analog.com)
********************************************************************************
 * Copyright 2022(c) Analog Devices, Inc.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Analog Devices, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *  - The use of this software may or may not infringe the patent rights
 *    of one or more patent holders.  This license does not release you
 *    from the requirement that you obtain separate licenses from these
 *    patent holders to use this software.
 *  - Use of the software either in source or binary form, must be run
 *    on or directly connected to an Analog Devices Inc. component.
 *
 * THIS SOFTWARE IS PROVIDED BY ANALOG DEVICES "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, NON-INFRINGEMENT,
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL ANALOG DEVICES BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, INTELLECTUAL PROPERTY RIGHTS, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

/******************************************************************************/
/***************************** Include Files **********************************/
/******************************************************************************/
#include "no_os_error.h"

#include <stdlib.h>
#include <stdio.h>
#include "parameters.h"
#include "no_os_uart.h"
#include "no_os_delay.h"
#include "no_os_timer.h"
#include "mqtt_client.h"

#include "maxim_uart.h"
#include "maxim_irq.h"
#include "maxim_timer.h"
#include "no_os_irq.h"
#include "no_os_error.h"

#include "wifi.h"
#include "tcp_socket.h"

// The default baudrate iio_app will use to print messages to console.
#define UART_BAUDRATE_DEFAULT	115200
#define PRINT_ERR_AND_RET(msg, ret) do {\
	printf("%s - Code: %d (-0x%x) \n", msg, ret, ret);\
	return ret;\
} while (0)



int32_t read_and_send(struct mqtt_desc *mqtt, int i)
{
	struct mqtt_message	msg;
	uint8_t			buff[100];
	uint32_t		len;

	/* Serialize data */
	len = sprintf(buff, "Data from device: %d", i);
	/* Send data to mqtt broker */
	msg = (struct mqtt_message) {
		.qos = MQTT_QOS0,
		.payload = buff,
		.len = len,
		.retained = false
	};
	return mqtt_publish(mqtt, MQTT_PUBLISH_TOPIC, &msg);
}

/***************************************************************************//**
 * @brief Main function execution for aducm3029 platform.
 *
 * @return ret - Result of the enabled examples execution.
*******************************************************************************/

void mqtt_message_handler(struct mqtt_message_data *msg)
{
	char	buff[101];
	int32_t	len;

	/* Message.payload don't have at the end '\0' so we have to add it. */
	len = msg->message.len > 100 ? 100 : msg->message.len;
	memcpy(buff, msg->message.payload, len);
	buff[len] = 0;

	printf("Topic:%s -- Payload: %s\n", msg->topic, buff);
}

int main()
{
	int ret = -EINVAL;
	int i = 0;
	int status;
	const struct no_os_irq_platform_ops *platform_irq_ops = &max_irq_ops;

	struct no_os_irq_init_param irq_init_param = {
		.irq_ctrl_id = INTC_DEVICE_ID,
		.platform_ops = platform_irq_ops,
		.extra = NULL
	};

	struct max_uart_init_param ip = {
		.flow = UART_FLOW_DIS
	};

	struct no_os_irq_ctrl_desc *irq_desc;
	status = no_os_irq_ctrl_init(&irq_desc, &irq_init_param);
	if (status < 0)
		return status;

	status = no_os_irq_global_enable(irq_desc);
	if (status < 0)
		return status;


	struct no_os_uart_init_param luart_par = {
		.device_id = UART_DEVICE_ID,
		/* TODO: remove this ifdef when asynchrounous rx is implemented on every platform. */
		.irq_id = UART_IRQ_ID,
		.asynchronous_rx = true,
		.baud_rate = UART_BAUDRATE_DEFAULT,
		.size = NO_OS_UART_CS_8,
		.parity = NO_OS_UART_PAR_NO,
		.stop = NO_OS_UART_STOP_1_BIT,
		.extra = &ip
	};
	struct no_os_uart_desc *uart_desc;

	status = no_os_uart_init(&uart_desc, &luart_par);
	if (status < 0)
		return status;

	static struct tcp_socket_init_param socket_param;

	static struct wifi_desc *wifi;
	struct wifi_init_param wifi_param = {
		.irq_desc = irq_desc,
		.uart_desc = uart_desc,
		.uart_irq_conf = uart_desc,
		.uart_irq_id = UART_IRQ_ID
	};
	status = wifi_init(&wifi, &wifi_param);
	if (status < 0)
		return status;

	status = wifi_connect(wifi, WIFI_SSID, WIFI_PWD);
	if (status < 0)
		return status;

	char buff[100];
	wifi_get_ip(wifi, buff, 100);
	printf("Tinyiiod ip is: %s\n", buff);

	wifi_get_network_interface(wifi, &socket_param.net);

	socket_param.max_buff_size = 0;

	static struct tcp_socket_desc	*sock;
	status = socket_init(&sock, &socket_param);
		if (NO_OS_IS_ERR_VALUE(status))
			PRINT_ERR_AND_RET("Error socket_init", status);

	struct socket_address		mqtt_broker_addr;
	/* Connect socket to mqtt borker server */
	mqtt_broker_addr = (struct socket_address) {
		.addr = SERVER_ADDR,
		.port = SERVER_PORT
	};
	status = socket_connect(sock, &mqtt_broker_addr);
	if (NO_OS_IS_ERR_VALUE(status))
		PRINT_ERR_AND_RET("Error socket_connect", status);

	printf("Connection with \"%s\" established\n", SERVER_ADDR);

	static uint8_t			send_buff[BUFF_LEN];
	static uint8_t			read_buff[BUFF_LEN];
	struct mqtt_init_param		mqtt_init_param;
	/* Initialize mqtt descriptor */
	mqtt_init_param = (struct mqtt_init_param) {
		.timer_id = TIMER_ID,
		.extra_timer_init_param = &max_timer_ops,
		.sock = sock,
		.command_timeout_ms = MQTT_CONFIG_CMD_TIMEOUT,
		.send_buff = send_buff,
		.read_buff = read_buff,
		.send_buff_size = BUFF_LEN,
		.read_buff_size = BUFF_LEN,
		.message_handler = mqtt_message_handler
	};
	struct mqtt_desc	*mqtt;

	status = mqtt_init(&mqtt, &mqtt_init_param);
	if (NO_OS_IS_ERR_VALUE(status))
		PRINT_ERR_AND_RET("Error mqtt_init", status);

	struct mqtt_connect_config	conn_config;
	/* Mqtt configuration */
	/* Connect to mqtt broker */
	conn_config = (struct mqtt_connect_config) {
		.version = MQTT_CONFIG_VERSION,
		.keep_alive_ms = MQTT_CONFIG_KEEP_ALIVE,
		.client_name = MQTT_CONFIG_CLIENT_NAME,
		.username = MQTT_CONFIG_CLI_USER,
		.password = MQTT_CONFIG_CLI_PASS
	};


	status = mqtt_connect(mqtt, &conn_config, NULL);
	if (NO_OS_IS_ERR_VALUE(status))
		PRINT_ERR_AND_RET("Error mqtt_connect", status);

	printf("Connected to mqtt broker\n");

	/* Subscribe for a topic */
	status = mqtt_subscribe(mqtt, MQTT_SUBSCRIBE_TOPIC, MQTT_QOS0, NULL);
	if (NO_OS_IS_ERR_VALUE(status))
		PRINT_ERR_AND_RET("Error mqtt_subscribe", status);
	printf("Subscribed to topic: %s\n", MQTT_SUBSCRIBE_TOPIC);

	while (true) {
		status = read_and_send(mqtt, i);
		if (NO_OS_IS_ERR_VALUE(status))
			PRINT_ERR_AND_RET("Error read_and_send", status);
		printf("Data sent to broker\n");

		/* Dispatch new mqtt mesages if any during SCAN_SENSOR_TIME */
		status = mqtt_yield(mqtt, SCAN_SENSOR_TIME);
		if (NO_OS_IS_ERR_VALUE(status))
			PRINT_ERR_AND_RET("Error mqtt_yield", status);
		i++;
	}


	return 0;
}
