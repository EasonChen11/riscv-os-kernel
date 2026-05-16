#ifndef VIDEO_H
#define VIDEO_H

#define FB_WIDTH  1920
#define FB_HEIGHT 1080
#define FB_BPP    4               /* XRGB8888 = 4 byte/pixel */

// 兩條編譯路徑：
//   QEMU define（Makefile run target 加 -DQEMU）：FB_BASE 自選空閒 RAM，
//                                                  video_init 透過 fw_cfg 跟 ramfb 註冊
//   無 QEMU define（OrangePi RV2 build）：FB_BASE 使用 U-Boot 預設好的硬體 base，
//                                          video_init 不必做 fw_cfg
/* FB_BASE_PA = QEMU/板上 framebuffer 的物理位址（要告訴 QEMU/HW）。
 * FB_BASE_VA = kernel 透過 PAGE_OFFSET linear map 拿到的虛擬位址（要 store 到 *FB）。 */
#include "pgtable.h"
#ifdef QEMU
#define FB_BASE_PA  0x87000000UL
#else
#define FB_BASE_PA  0x7F700000UL
#endif
#define FB_BASE_VA  (PAGE_OFFSET + FB_BASE_PA)

void video_init(void);

// 把 width*height 的 XRGB8888 影像置中拷貝到 framebuffer，
// 只 flush 對應行的 D-cache 範圍（整片 8MB flush 太貴）。
void video_bmp_display(unsigned int* bmp_image, int width, int height);

#endif /* VIDEO_H */
