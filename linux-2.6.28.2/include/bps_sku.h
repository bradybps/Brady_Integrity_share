/*
 * =====================================================================================
 *
 *       Filename:  bps_sku.h
 *
 *    Description:  
 *
 *        Version:  0.1
 *        Created:  02/28/10 10:20:34
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Sudheer Divakaran, 
 *        Company:  Wipro Technologies, Kochi
 *
 * =====================================================================================
 */

#ifndef __BPS_SKU_H__
#define __BPS_SKU_H__

#define BPS_SKU_WIFI_BT_ETH_01		(771)	//TI/Jorjin WG7310 WIFI+BT, 
							//LAN9220 Ethernet

#define BPS_SKU_WIFI_BT_01			(772)	//Reserved
#define BPS_SKU_BT_ONLY_01			(773)	//Rayson BTM330
#define BPS_SKU_ETH_ONLY_01			(774)	//Reserved

#define BPS_GPIO_SKU_WIFI_BT_ETH_01	(0)	
#define BPS_GPIO_SKU_WIFI_BT_01		(1)
#define BPS_GPIO_SKU_BT_ONLY_01		(2)
#define BPS_GPIO_SKU_ETH_ONLY_01	(3)

#define BPS_FIRST_SKU 	BPS_SKU_WIFI_BT_ETH_01
#define BPS_LAST_SKU	BPS_SKU_ETH_ONLY_01

#endif
