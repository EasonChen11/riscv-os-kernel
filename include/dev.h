#ifndef DEV_H
#define DEV_H

/* 各裝置 driver 的註冊入口：回傳 register_dev 配給的 device id。 */
int uart_dev_init(void);   /* Adv1：/dev/uart */
int fb_dev_init(void);     /* Adv2：/dev/fb */

#endif /* DEV_H */
