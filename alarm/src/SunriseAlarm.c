/*
 * @brief FreeRTOS Blinky example
 *
 * @note
 * Copyright(C) NXP Semiconductors, 2012
 * All rights reserved.
 *
 * @par
 * Software that is described herein is for illustrative purposes only
 * which provides customers with programming information regarding the
 * LPC products.  This software is supplied "AS IS" without any warranties of
 * any kind, and NXP Semiconductors and its licensor disclaim any and
 * all warranties, express or implied, including all implied warranties of
 * merchantability, fitness for a particular purpose and non-infringement of
 * intellectual property rights.  NXP Semiconductors assumes no responsibility
 * or liability for the use of the software, conveys no license or rights under any
 * patent, copyright, mask work right, or any other intellectual property rights in
 * or to any products. NXP Semiconductors reserves the right to make changes
 * in the software without notification. NXP Semiconductors also makes no
 * representation or warranty that such application will be suitable for the
 * specified use without further testing or modification.
 *
 * @par
 * Permission to use, copy, modify, and distribute this software and its
 * documentation is hereby granted, under NXP Semiconductors' and its
 * licensor's relevant copyrights in the software, without fee, provided that it
 * is used in conjunction with NXP Semiconductors microcontrollers.  This
 * copyright, permission, and disclaimer notice must appear in all copies of
 * this code.
 */

#include "board.h"

#define I2C_FASTPLUS_BIT     0
static int mode_poll;

static uint8_t iox_data[2];	/* PORT0 input port, PORT1 Output port */
static volatile uint32_t tick_cnt;

/* Transmit and receive ring buffers */
STATIC RINGBUFF_T txring, rxring;

/* Transmit and receive ring buffer sizes */
#define UART_SRB_SIZE 128	/* Send */
#define UART_RRB_SIZE 32	/* Receive */

/*****************************************************************************
 * Private types/enumerations/variables
 ****************************************************************************/

/*****************************************************************************
 * Public types/enumerations/variables
 ****************************************************************************/

/*****************************************************************************
 * Private functions
 ****************************************************************************/

/* State machine handler for I2C0 and I2C1 */
static void I2C_State_Handling(I2C_ID_T id)
{
	if (Chip_I2C_IsMasterActive(id)) {
		Chip_I2C_MasterStateHandler(id);
	}
	else {
		Chip_I2C_SlaveStateHandler(id);
	}
}

/**
 * @brief	I2C Interrupt Handler
 * @return	None
 */
void I2C_IRQHandler(void)
{
	I2C_State_Handling(I2C0);
}

/* Set I2C mode to polling/interrupt */
static void I2C_Set_Mode(I2C_ID_T id, int polling)
{
	if (!polling) {
		mode_poll &= ~(1 << id);
		Chip_I2C_SetMasterEventHandler(id, Chip_I2C_EventHandler);
		NVIC_EnableIRQ(I2C0_IRQn);
	}
	else {
		mode_poll |= 1 << id;
		NVIC_DisableIRQ(I2C0_IRQn);
		Chip_I2C_SetMasterEventHandler(id, Chip_I2C_EventHandlerPolling);
	}
}

void Setup_I2C(void)
{
	//By default, these pins are GPIO. Need to set the mux to enable
	Chip_IOCON_PinMuxSet(LPC_IOCON, IOCON_PIO0_4, IOCON_FUNC1 | I2C_FASTPLUS_BIT);
	Chip_IOCON_PinMuxSet(LPC_IOCON, IOCON_PIO0_5, IOCON_FUNC1 | I2C_FASTPLUS_BIT);

	Chip_SYSCTL_PeriphReset(RESET_I2C0);
	Chip_I2C_Init(I2C0);
	Chip_I2C_SetClockRate(I2C0, 100000);
	/* Set default mode to interrupt */
	I2C_Set_Mode(I2C0, 0);
}

void Setup_UART(void)
{
	Chip_IOCON_PinMuxSet(LPC_IOCON, IOCON_PIO1_6, (IOCON_FUNC1 | IOCON_MODE_INACT));/* RXD */
	Chip_IOCON_PinMuxSet(LPC_IOCON, IOCON_PIO1_7, (IOCON_FUNC1 | IOCON_MODE_INACT));/* TXD */

	Chip_UART_Init(LPC_USART);
	Chip_UART_SetBaud(LPC_USART, 115200);
	Chip_UART_ConfigData(LPC_USART, (UART_LCR_WLEN8 | UART_LCR_SBS_1BIT));
	Chip_UART_SetupFIFOS(LPC_USART, (UART_FCR_FIFO_EN | UART_FCR_TRG_LEV2));
	Chip_UART_TXEnable(LPC_USART);
}

int Send_UART(char* message, int message_length)
{
	//TODO: Don't hardcode buffer size
	char buf[48];

	for (int i = 0; i < message_length; i++)
	{
		buf[i] = message[i];
	}
	buf[message_length] = '\r';
	buf[message_length + 1] = '\n';

	return Chip_UART_SendBlocking(LPC_USART, buf, message_length+2);

}

/*****************************************************************************
 * Public functions
 ****************************************************************************/

/**
 * @brief	main routine for FreeRTOS blinky example
 * @return	Nothing, function should not exit
 */
int main(void)
{
	static I2C_XFER_T xfer;
	uint8_t buf[17] = {0};
	uint8_t UARTBuf[48] = {0};

	SystemCoreClockUpdate();
	Board_Init();

	Setup_I2C();
	Setup_UART();

	//Data is sent little-endian (lowest bytes first)
	buf[1] = 0x3F;
	buf[3] = 0x06;
	buf[7] = 0x5B;

	xfer.slaveAddr = 0x70;
	xfer.rxBuff = 0;
	xfer.txBuff = 0;
	xfer.txSz = 0;
	xfer.rxSz = 0;

	//turn on oscillator
	buf[0] = 0x21;
	xfer.txSz = 1;
	xfer.txBuff = buf;
	int res = 0;
	res = Chip_I2C_MasterSend(I2C0, xfer.slaveAddr, xfer.txBuff, xfer.txSz);

	//Turn on the screen;
	buf[0] = 0x81;
	xfer.txSz = 1;
	res = Chip_I2C_MasterSend(I2C0, xfer.slaveAddr, xfer.txBuff, xfer.txSz);

	//Set brightness to 15;
	buf[0] = 0xEF;
	xfer.txSz = 1;
	res = Chip_I2C_MasterSend(I2C0, xfer.slaveAddr, xfer.txBuff, xfer.txSz);

	//Send the entire buffer to set the display
	//1 byte header plus 8 2 byte bitmaps
	xfer.txSz = 17;
	buf[0] = 0;
	xfer.txBuff = buf;
	res = Chip_I2C_MasterSend(I2C0, xfer.slaveAddr, xfer.txBuff, xfer.txSz);

	//Check chip connection status
	res = Send_UART("AT", 2);
//	UARTBuf[0] = 'A';
//	UARTBuf[1] = 'T';
//	UARTBuf[2] = '\r';
//	UARTBuf[3] = '\n';
//	res = Chip_UART_SendBlocking(LPC_USART, UARTBuf, 48);
	//Expecting 'OK'
	//Don't forget that the device echos back what we send. TODO: Disable that?
	//Note: There is a bug(?) that the device echoes back our sent command with an extra '\r'.
	//Need to account for this when expecting bytes back
	//msglen + 5 (\r\r\n\r\n) + 2 (OK) + 2 (\r\n)
	//Seems like we run out of buffer space or something. Only able to read 15 or 16 bytes
	//before bytes get dropped.
	//TODO: Use ring buffer instead?
	res = Chip_UART_Read(LPC_USART, UARTBuf, 48);
	while (res != 0)
	{
		res = Chip_UART_Read(LPC_USART, UARTBuf, 48);
	}

	res = Send_UART("AT+RST", 6);
	res = Chip_UART_Read(LPC_USART, UARTBuf, 48);
	while (res != 0)
	{
		res = Chip_UART_Read(LPC_USART, UARTBuf, 48);
	}

	res = Send_UART("AT", 2);
	res = Chip_UART_Read(LPC_USART, UARTBuf, 48);
	while (res != 0)
	{
		res = Chip_UART_Read(LPC_USART, UARTBuf, 48);
	}

	res = Send_UART("AT+CWJAP=\"SSID\",\"PASSWORD\"", 32);
	res = Chip_UART_Read(LPC_USART, UARTBuf, 48);
	while (res != 0)
	{
		res = Chip_UART_Read(LPC_USART, UARTBuf, 48);
	}

	res = Send_UART("AT+CIPSTART=\"UDP\",\"time.nist.gov\",123", 37);
	res = Chip_UART_Read(LPC_USART, UARTBuf, 48);

	//Connect to WIFI
	//TODO: Don't commit secrets
	//AT+CWJAP="SSID","PASSWORD"
	Chip_UART_SendBlocking(LPC_USART, UARTBuf, 2);
	//Expecting 'OK'
	Chip_UART_ReadBlocking(LPC_USART, UARTBuf, 2);

	//AT+CWMODE=3\r\n

	//Create a connection to the nist NTP server
	//AT+CIPSTART="UDP","time.nist.gov",123

	//Send request byte to the nist NTP server
	//AT+CIPSEND=48

	//0b11100011
	UARTBuf[0] = 0xE3;



	//Note: NTP epoch is 1 January 1900, different than UNIX epoch

	while(1);

	/* Should never arrive here */
	return 1;
}
