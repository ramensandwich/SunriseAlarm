/*
 * @brief SunriseAlarm
 *
 */

#include "board.h"
#include "secrets.h"
#include <stdlib.h>
#include <string.h>

#define I2C_FASTPLUS_BIT     0
/* UART ring buffer sizes */
#define UART_SRB_SIZE 128	/* Send */
#define UART_RRB_SIZE 256	/* Receive */
#define UART_BUF_SIZE 256

static int mode_poll;

/* Transmit and receive ring buffers */
STATIC RINGBUFF_T txring, rxring;

static uint8_t rxbuff[UART_RRB_SIZE], txbuff[UART_SRB_SIZE];


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

/**
 * @brief	Sets up the board I2C registers and interrupts
 * 			Changes the board J2-40 and J2-41 pins from GPIO to UART
 * @return	Nothing
 */
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

/**
 * @brief	Sets up the board UART registers and interrupts
 * 			Changes the board J2-9 and J2-10 pins from GPIO to UART
 * @return	Nothing
 */
void Setup_UART(void)
{
	Chip_IOCON_PinMuxSet(LPC_IOCON, IOCON_PIO1_6, (IOCON_FUNC1 | IOCON_MODE_INACT));/* RXD */
	Chip_IOCON_PinMuxSet(LPC_IOCON, IOCON_PIO1_7, (IOCON_FUNC1 | IOCON_MODE_INACT));/* TXD */

	Chip_UART_Init(LPC_USART);
	Chip_UART_SetBaud(LPC_USART, 115200);
	Chip_UART_ConfigData(LPC_USART, (UART_LCR_WLEN8 | UART_LCR_SBS_1BIT));
	Chip_UART_SetupFIFOS(LPC_USART, (UART_FCR_FIFO_EN | UART_FCR_TRG_LEV2));
	Chip_UART_TXEnable(LPC_USART);

	/* Before using the ring buffers, initialize them using the ring
	   buffer init function */
	RingBuffer_Init(&rxring, rxbuff, 1, UART_RRB_SIZE);
	RingBuffer_Init(&txring, txbuff, 1, UART_SRB_SIZE);

	/* Enable receive data and line status interrupt */
	Chip_UART_IntEnable(LPC_USART, (UART_IER_RBRINT | UART_IER_RLSINT));

	/* preemption = 1, sub-priority = 1 */
	NVIC_SetPriority(UART0_IRQn, 1);
	NVIC_EnableIRQ(UART0_IRQn);
}

/**
 * @brief	Sends through UART using ring buffers
 * @return	Number of bytes sent
 */
int Send_UART(char* message, int message_length)
{
	//TODO: Don't hardcode buffer size
	//TODO: Validate that message_length is less than buffer size
	char buf[48] = {0};

	for (int i = 0; i < message_length; i++)
	{
		buf[i] = message[i];
	}
	buf[message_length] = '\r';
	buf[message_length + 1] = '\n';

	int retCount = 0;

	retCount = Chip_UART_SendRB(LPC_USART, &txring, buf, message_length+2);

	//After sending the data, listen back for the echo so the command will execute
	//The ESP8266 does not execute commands until after it finishes echoing
	//Note: There is a bug(?) that the device echoes back our sent command with an extra '\r'.
	//Need to account for this when expecting bytes back
	int res = 1;
	char readChar = 0;
	int count = 0;
	memset(buf, 0, 48);
	int newline_read_count = 0;
	//Echo format is [command]\r\r\n\r\n
	while (newline_read_count < 2)
	{
		res = Chip_UART_ReadRB(LPC_USART, &rxring, &readChar, 1);
		buf[count] = readChar;
		if (res != 0) count++;
		if (readChar == '\n') newline_read_count++;
	}

	return retCount;
}

/**
 * @brief	Reads from the UART using ring buffers
 * @return	number of bytes read
 */
int Read_UART(char* buf, int max_bytes, bool is_wifi)
{
	int count = 0;
	int res = 1;
	int expected_newlines = 1;
	if (is_wifi) expected_newlines = 3;
	int counted_newlines = 0;
	while ((res != 0 || counted_newlines < expected_newlines) && count < max_bytes)
	{
		res = Chip_UART_ReadRB(LPC_USART, &rxring, &(buf[count]), 1);
		if (buf[count] == '\n') counted_newlines++;
		if (res != 0) count++;
	}

	//TODO: Return -1 if the chip replies with an error

	return count;
}

/**
 * @brief	UART interrupt handler using ring buffers
 * @return	Nothing
 */
void UART_IRQHandler(void)
{
	/* Want to handle any errors? Do it here. */

	/* Use default ring buffer handler. Override this with your own
	   code if you need more capability. */
	Chip_UART_IRQRBHandler(LPC_USART, &rxring, &txring);
}

/**
 * @brief	main routine for SunriseAlarm
 * @return	Nothing, function should not exit
 */
int main(void)
{
	static I2C_XFER_T xfer;
	uint8_t buf[17] = {0};
	uint8_t UARTBuf[UART_BUF_SIZE] = {0};

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
	memset(UARTBuf, 0, UART_BUF_SIZE);
	Read_UART(UARTBuf, UART_BUF_SIZE, false);

	//Reset the chip and its settings
	res = Send_UART("AT+RST", 6);
	memset(UARTBuf, 0, UART_BUF_SIZE);
	res = Read_UART(UARTBuf, UART_BUF_SIZE, false);

	//Check chip connection status
	res = Send_UART("AT", 2);
	memset(UARTBuf, 0, UART_BUF_SIZE);
	res = Read_UART(UARTBuf, UART_BUF_SIZE, false);

	//Set to station mode
	res = Send_UART("AT+CWMODE=3", 11);
	memset(UARTBuf, 0, UART_BUF_SIZE);
	res = Read_UART(UARTBuf, UART_BUF_SIZE, false);

	//Connect to access point
	char* APInfo = APSTRING;
	res = Send_UART(APInfo, strlen(APInfo));
	memset(UARTBuf, 0, UART_BUF_SIZE);
	//If already connected to AP, it will disconnect, then reconnect
	res = Read_UART(UARTBuf, UART_BUF_SIZE, true);

	//Create a connection to the nist NTP server
	res = Send_UART("AT+CIPSTART=\"UDP\",\"time.nist.gov\",123", 37);
	memset(UARTBuf, 0, UART_BUF_SIZE);
	res = Read_UART(UARTBuf, UART_BUF_SIZE, false);

	//Send request byte to the nist NTP server
	//AT+CIPSEND=48
	res = Send_UART("AT+CIPSEND=3", 13);
	//0b11100011
	UARTBuf[0] = 0xE3;
	res = Send_UART(UARTBuf, 1);

	memset(UARTBuf, 0, UART_BUF_SIZE);
	res = Read_UART(UARTBuf, UART_BUF_SIZE, false);


	//Note: NTP epoch is 1 January 1900, different than UNIX epoch

	while(1);

	/* Should never arrive here */
	return 1;
}
