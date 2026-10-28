/**
 * @file  uart_link.c
 * @brief 串口链路：UART1 (PA8=RXD1 / PA9=TXD1)，19200 8N1
 *        波特率实际取自 board.h 的 BOARD_UART_BAUDRATE
 *
 * 帧格式（与实测一致）：
 *   A5 5A <mask_L> <mask_H> <checksum>
 *   checksum = A5 ^ 5A ^ mask_L ^ mask_H
 *
 * 按键状态变化时发送一帧；上电初始化后也会发送一帧当前状态。
 */

#include "uart_link.h"
#include "board.h"

void uart_link_init(void)
{
    /* 先把 TXD 拉高，避免初始化瞬间出现低电平毛刺 */
    GPIOA_SetBits(BOARD_UART_TX_PIN);
    GPIOA_ModeCfg(BOARD_UART_TX_PIN, GPIO_ModeOut_PP_5mA);
    GPIOA_ModeCfg(BOARD_UART_RX_PIN, GPIO_ModeIN_PU);

    UART1_DefInit();
    UART1_BaudRateCfg(BOARD_UART_BAUDRATE);

    /* 清掉可能残留的接收数据，本固件目前只发送不解析 */
    UART1_CLR_RXFIFO();
}

void uart_link_send_mask(uint16_t mask)
{
    uint8_t frame[5];

    frame[0] = 0xA5;
    frame[1] = 0x5A;
    frame[2] = (uint8_t)(mask & 0xFF);
    frame[3] = (uint8_t)((mask >> 8) & 0x0F); /* HI 只用低 4 位 */
    frame[4] = (uint8_t)(frame[0] ^ frame[1] ^ frame[2] ^ frame[3]);

    UART1_SendString(frame, sizeof(frame));
}

void uart_link_send_battery(uint16_t mv)
{
    uint8_t frame[5];

    frame[0] = 0xA5;
    frame[1] = 0x5B; /* 与按键帧 0x5A 区分 */
    frame[2] = (uint8_t)(mv & 0xFF);
    frame[3] = (uint8_t)((mv >> 8) & 0xFF);
    frame[4] = (uint8_t)(frame[0] ^ frame[1] ^ frame[2] ^ frame[3]);

    UART1_SendString(frame, sizeof(frame));
}
