#ifndef __COMIP_FB_H
#define __COMIP_FB_H

/*color code*/
#define COLOR_CODE_16BIT_CONFIG1	0	//PACKET RGB565
#define	COLOR_CODE_16BIT_CONFIG2	1	//UNPACKET RGB565
#define	COLOR_CODE_16BIT_CONFIG3	2	//UNPACKET RGB565
#define	COLOR_CODE_18BIT_CONFIG1	3	//PACKET RGB666
#define	COLOR_CODE_18BIT_CONFIG2	4	//UNPACKET RGB666
#define	COLOR_CODE_24BIT		5	//PACKET RGB888
#define	COLOR_CODE_MAX			6

/*command code*/
#define	DCS_CMD				02
#define	GEN_CMD				03
#define	SW_PACK0			04
#define	SW_PACK1			05
#define	SW_PACK2			06
#define	LW_PACK				07
#define	SHUTDOWN_SW_PACK		08

/* DSI mode flags */

/* video mode */
#define MIPI_DSI_MODE_VIDEO     BIT(0)
/* video burst mode */
#define MIPI_DSI_MODE_VIDEO_BURST   BIT(1)
/* video pulse mode */
#define MIPI_DSI_MODE_VIDEO_SYNC_PULSE  BIT(2)
/* enable auto vertical count mode */
#define MIPI_DSI_MODE_VIDEO_AUTO_VERT   BIT(3)
/* enable hsync-end packets in vsync-pulse and v-porch area */
#define MIPI_DSI_MODE_VIDEO_HSE     BIT(4)
/* disable hfront-porch area */
#define MIPI_DSI_MODE_VIDEO_HFP     BIT(5)
/* disable hback-porch area */
#define MIPI_DSI_MODE_VIDEO_HBP     BIT(6)
/* disable hsync-active area */
#define MIPI_DSI_MODE_VIDEO_HSA     BIT(7)
/* flush display FIFO on vsync pulse */
#define MIPI_DSI_MODE_VSYNC_FLUSH   BIT(8)
/* disable EoT packets in HS mode */
#define MIPI_DSI_MODE_EOT_PACKET    BIT(9)
/* device supports non-continuous clock behavior (DSI spec 5.6.1) */
#define MIPI_DSI_CLOCK_NON_CONTINUOUS   BIT(10)
/* transmit data in low power */
#define MIPI_DSI_MODE_LPM       BIT(11)

#define MIPI_DSI_FMT_RGB888 0
#define MIPI_DSI_FMT_RGB666 1
#define MIPI_DSI_FMT_RGB666_PACKED 2
#define MIPI_DSI_FMT_RGB565 3

#endif
