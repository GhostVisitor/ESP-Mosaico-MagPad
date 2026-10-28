/**
 * @file  uart_link.h
 * @brief 串口协议：A5 5A <mask_L> <mask_H> <xor>
 */

#ifndef __UART_LINK_H
#define __UART_LINK_H

#include "CONFIG.h"

void uart_link_init(void);
void uart_link_send_mask(uint16_t mask);

/**
 * @brief 低电告警帧：A5 5B <mv_L> <mv_H> <xor>
 *
 * 帧头第二字节是 0x5B 而不是按键帧的 0x5A，旧的上位机状态机会把它当成
 * 乱码自动丢弃（不会误判成按键），新上位机可以解析出电池 mV。
 */
void uart_link_send_battery(uint16_t mv);

#endif /* __UART_LINK_H */
