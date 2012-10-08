/*
 * =====================================================================================
 *
 *       Filename:  bps_gadget.h
 *
 *    Description:  
 *
 *        Version:  0.1
 *        Created:  01/23/2010 20:59:22
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Sudheer Divakaran
 *        Company:  Wipro Technologies, Kochi
 *
 * =====================================================================================
 */

#ifndef __BPS_GADGET_H__
#define __BPS_GADGET_H__


#define VENDOR_ID_BRADY			(0x0E2E) 

#define BPS_DEVICE_SUB_CLASS	(0)
#define BPS_DEVICE_PROTOCOL		(1)

#define BPS_INTERFACE_SUBCLASS	(0)
#define BPS_INTERFACE_PROTOCOL	(0)



#define BPS_CDEV_MAJOR_NO		(250)
#define BPS_CDEV_MINOR_COUNT 	(2)
#define DATA_CHANNEL			(0)
#define CONFIG_CHANNEL			(1)



#define DATA_CHANNEL_INTR_VALUE		(1)
#define CONFIG_CHANNEL_INTR_VALUE	(2)

struct bps_data_error
{
	const char* p_buffer;
	size_t len;
}__attribute__((packed));
/*The applications should pass this structure to the ioctl (defined below) 
 * for reporting data channel errors over config channel.
 */


#define BPS_IOCTL_MAGIC 'B'

#define IOCTL_BPS_GET_PACKET_SIZE	_IOR(BPS_IOCTL_MAGIC, 0, unsigned int)
/*To retrieve the maximum number of bytes a bulk endpoint can transfer*/

#define IOCTL_BPS_GET_SKU_MODEL    _IOR(BPS_IOCTL_MAGIC, 1, unsigned int)
/*To retrieve the SKU model*/

#define IOCTL_BPS_GET_PROTOCOL_VERSION  _IOR(BPS_IOCTL_MAGIC, 2, unsigned int)
/*To retrieve the protocol version supported by the gadegt driver.*/

#define IOCTL_BPS_SEND_DATA_ERROR	_IOW(BPS_IOCTL_MAGIC, 3, struct bps_data_error)
/*To transfer data channel errors through config channel*/

#define IOCTL_BPS_SEND_ZLP		    _IOW(BPS_IOCTL_MAGIC, 4, unsigned int)
/*To send a Zero Length Packet through the channel*/

#define IOCTL_BPS_SEND_INTR_NOTIFICATION    _IOW(BPS_IOCTL_MAGIC, 5, unsigned int)
/*To send an interrupt notification through the interrupt channel*/

#define IOCTL_BPS_IS_SUSPENDED    _IOR(BPS_IOCTL_MAGIC, 6, unsigned int)
/*To check whether the device is suspended*/

#endif
