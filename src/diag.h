/**
 * @file  diag.h
 * @brief USB CDC 诊断固件入口（PAD_DIAG_CDC）
 */

#ifndef __DIAG_H
#define __DIAG_H

/**
 * @brief 诊断模式主流程：初始化 CDC，循环打印运行状态与 PB14 状态。
 *        该函数不会返回。
 */
void diag_run(void) __attribute__((noreturn));

#endif /* __DIAG_H */
