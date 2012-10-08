/*
 * =====================================================================================
 *
 *       Filename:  bps_gadget.c
 *
 *    Description: 
 *    	This file implements a usb gadget driver that will let BPS's userspace 
 *    	applications to talk to the printer over usb. The driver supports 
 *    	HS & FS modes.
 *    	There are 5 endpoints in the interface supported by the driver.
 *    		1. A pair of BULK-OUT & BULK-IN for transferring print data
 *    		2. A pair of BULK-OUT & BULK-IN for transferring config data
 *    		3. An interrupt channel for notifying data availability in the 
 *    		   above BULK-IN endpoints.
 *    	User space applications are responsible for reading & writing data 
 *    	through the bulk endpoints using the character device interface provided
 *    	by the driver. It is the responsibility of the user-space applications 
 *    	to act upon the data passed through these channels. The interrupt 
 *    	endpoint is used to reduce the h/w bus activity and there by save power. 
 *    	Though print channel has bi-directional support, errors occurring in 
 *    	print application are reported through config channel's BULK-IN endpoint. 
 *    	This has been done so because Brady doesn't want to change the existing 
 *    	printer drivers. In order to report print channel errors, an ioctl named 
 *    	IOCTL_BPS_SEND_DATA_ERROR has been provided. The driver provides 
 *    	character device interface for devices to communicate over it. For this,
 *    	it provides two device nodes (250,0 - data channel) & (250,1 - config 
 *    	channel). Only one process can open a device node at a time. The 
 *    	applications should open the devices in blocking mode as the driver would
 *		block the processes initiating write system call until the usb transfer
 *		completes (if a process opens a device in ASYNC mode and invokes the 
 *		write method, it will always fail). The driver provides 'polling' 
 *		support on the device nodes so that the applications that doesn't want 
 *		to block on the read/write system calls can know whether the device can
 *		be written or read in advance. The driver expects only one thread 
 *		to be operated per channel at any time.
 *
 *        Version:  0.2
 *        Created:  01/23/2010 20:58:53
 *       Compiler:  gcc
 *
 *         Author:  Sudheer Divakaran
 *        Company:  Wipro Technologies, Kochi
 *
 *        Revision History:
 *        	23-Jan-2010	- Sudheer -Initial implementation.
 *        	27-May-2010 - Sudheer
 *        			1. Added support for sending Zero Length Packets.
 *        			2. Added support for receiving Zero Length Packets.
 *        			3. Removed interrupt manager (automatic interrupt channel 
 *        			   notification) & added support for letting the applications to 
 *        			   send interrupt notification from user space. 
 *        			4. Added support for retrieving sku model & protocol 
 *        			   version from user space.
 *        			5. Added provision to specify the product id during driver loading.
 *			07-June-2010 - Sudheer
 * 					1. Optimized the endpoint buffer sizes.
 *			19-Nov-2010 - Sudheer
 * 					1. Implemented usb suspend & resume methods.
 *
 * =====================================================================================
 */

#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/device.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>


#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/kthread.h>


#include "epautoconf.c"

#include "bps_sku.h"
#include "bps_gadget.h"


#define USB_SPEC_SUPPORTED		(0x0200)


#define BULK_EP_MAX_PACKET_SIZE (512)

#ifndef BPS_X86_SIMULATION //default case
#define INTR_EP_MAX_PACKET_SIZE (8)
#define BPS_FS_INTR_INTERVAL	(255)
#define BPS_HS_INTR_INTERVAL	(255)
#else
#define INTR_EP_MAX_PACKET_SIZE BULK_EP_MAX_PACKET_SIZE
#endif

#define EP0_URB_BUFFER_SIZE 		(64)
#define BULK_OUT_URB_BUFFER_SIZE	(512)
#define BULK_IN_URB_BUFFER_SIZE		(2048)
#define INTR_URB_BUFFER_SIZE		(1)
#define DATA_ERR_URB_BUFFER_SIZE 	(64)


//Number of endpoints supported by the gadget.
#define BPS_EP_COUNT 			(5)

#define FALSE (0)
#define TRUE  (-1)

#define DEBUG_BPS_GADGET
#ifdef DEBUG_BPS_GADGET
	#define DPRINT(fmt,args...) printk(KERN_DEBUG fmt,##args);
	#define PRINT_ENTER_FN printk(KERN_DEBUG "IN: %s\n",__func__);
	#define PRINT_LEAVE_FN printk(KERN_DEBUG "OUT: %s\n",__func__);
#else
	#define DPRINT(fmt,args...)	do{}while(0);
	#define PRINT_ENTER_FN		do{}while(0);
	#define PRINT_LEAVE_FN 		do{}while(0);
#endif


/*enum for identifying the endpoints in the gadget.*/
enum ep_info
{
	EP0,
	BULKOUT1,
	BULKIN1,
	BULKOUT2,
	BULKIN2,
	INTRIN1,
	MAX_BPS_EP
};


#define FIRST_BPS_EP	BULKOUT1
#define DATA_OUT_EP 	BULKOUT1
#define DATA_IN_EP 		BULKIN1
#define CONFIG_OUT_EP 	BULKOUT2
#define CONFIG_IN_EP 	BULKIN2
#define INTR_IN_EP		INTRIN1


/* This structure holds together the components associated with 
 * a endpoint used in bps.
 */
struct bps_ep
{
	int ep_id;					
	/*endpoint id*/
	
	int queued;					
	/*whether the request is queued on the endpoint*/

	struct usb_ep		*p_ep;	
	/*The usb endpoint in use*/

	struct usb_request	*p_req;
	/*The usb request associated with the endpoint*/

	unsigned urb_buffer_size;
	/*The usb request's buffer size*/
};



/*This structure holds together the variables associated with the charcter 
 * device interface provided by the driver.
 */
struct bps_cdev
{
	unsigned channel;
	/*Channel/minor number associated with the device.*/

	unsigned opened;
	/*Whether the device is opened or not.*/

	spinlock_t lock;
	/*Lock for synchronizing device methods.*/
	
	uint32_t rx_bytes;
	/*Number of bytes used in the RX channel.*/
	
	struct bps_ep *p_inep;
	/*Host's IN endpoint*/

	struct bps_ep *p_outep;
	/*Host's OUT endpoint*/
	
	wait_queue_head_t wait;
	/*Wait queue used for suspending the process in the device methods.*/

	struct bps_ep intr_ep; 
};



/*This structure holds together the variables used by the gadget driver*/
static struct
{
	int config_set;	
	/*Whether usb configuration has been chosen.*/
	
	int suspended;
	/*Whether the device is suspended.*/

	int bound;		
	/*Whether the gadget driver is in bound state.*/
	
	spinlock_t lock;
	/*common lock used in the driver.*/

	dev_t cdev_no;	
	/*device number of the character interface.*/
	
	struct cdev dev;
	
	struct bps_cdev cdevs[BPS_CDEV_MINOR_COUNT];
	/*holds the information for supporting character device.*/
	
	struct bps_ep data_err_ep; 
	/*bps endpoint used for reporting print errors through config channel.*/

	struct bps_ep ep_list[BPS_EP_COUNT+1];
	/*the bps endpoints used in the driver.*/

}bps;



/***   BEGINGS the USB descriptors used by the driver  ***/

#define LANG_EN_US		(0x0409)

#define STRING_MANUFACTURER	(1)
#define STRING_PRODUCT		(2)
#define STRING_CONFIG		(3)
#define STRING_INTERFACE	(3) 
/*Same string is used for config & interface */


static unsigned int sku;
module_param(sku, int, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(sku, "BPS sku model");

// static strings, in UTF-8
static struct usb_string gadget_strings [] = 
{
	{ STRING_MANUFACTURER	, "Brady",},
	{ STRING_PRODUCT		, "BPS",},
	{ STRING_CONFIG			, "default",},
	{}
};

static struct usb_gadget_strings gadget_string_table = 
{
	.language	= LANG_EN_US,
	.strings	= gadget_strings,
};


static struct usb_device_descriptor bps_device_descriptor = 
{
	.bLength 			=	USB_DT_DEVICE_SIZE,
	.bDescriptorType 	=	USB_DT_DEVICE,
	.bcdUSB 			=	__constant_cpu_to_le16(USB_SPEC_SUPPORTED),
	.bDeviceClass 		=	USB_CLASS_VENDOR_SPEC,
	.idVendor 			=	__constant_cpu_to_le16(VENDOR_ID_BRADY),
/*	.idProduct 			=	__constant_cpu_to_le16(PRODUCT_ID_BPS),*/
	.iManufacturer 		=	STRING_MANUFACTURER,
	.iProduct 			=	STRING_PRODUCT,
	.bNumConfigurations =	1,
};



static struct usb_qualifier_descriptor bps_dev_qualifier = {
	.bLength 			=	sizeof bps_dev_qualifier,
	.bDescriptorType 	=	USB_DT_DEVICE_QUALIFIER,
	.bcdUSB 			=	__constant_cpu_to_le16(USB_SPEC_SUPPORTED),
	.bDeviceClass 		=	USB_CLASS_PER_INTERFACE,
	.bDeviceSubClass	=	BPS_DEVICE_SUB_CLASS,
	.bDeviceProtocol	=	BPS_DEVICE_PROTOCOL,
	.bNumConfigurations =	1,
};



// There is only one usb configuration.
static struct usb_config_descriptor bps_config_descriptor = {
	.bLength 			=	sizeof bps_config_descriptor,
	.bDescriptorType 	=	USB_DT_CONFIG,
	// wTotalLength computed by usb_gadget_config_buf()
	.bNumInterfaces 	=	1,
	.bConfigurationValue=	1,
	.iConfiguration 	=	STRING_CONFIG,
	.bmAttributes 		=	USB_CONFIG_ATT_ONE | USB_CONFIG_ATT_SELFPOWER,
	.bMaxPower 			=	CONFIG_USB_GADGET_VBUS_DRAW / 2,
	
	//TODO: Remove the self power attribute and compute the power value 
	//based on the final board's requirement.
};

static struct usb_interface_descriptor bps_default_interface = {
	.bLength 			=	sizeof bps_default_interface,
	.bDescriptorType 	=	USB_DT_INTERFACE,
	.bInterfaceNumber 	=	0,
	.bNumEndpoints 		=	BPS_EP_COUNT,
	.bInterfaceClass 	=	USB_CLASS_VENDOR_SPEC,
	.bInterfaceSubClass =	BPS_INTERFACE_SUBCLASS,
	.bInterfaceProtocol =	BPS_INTERFACE_PROTOCOL,
	.iInterface 		=	STRING_INTERFACE
};



/*** BEGINS FULL SPEED ENDPOINT DESCRIPTORS   ***/

static struct usb_endpoint_descriptor fs_bulkout1_ep_desc = {
	.bLength 			=	USB_DT_ENDPOINT_SIZE,
	.bDescriptorType 	=	USB_DT_ENDPOINT,
	.bEndpointAddress 	=	USB_DIR_OUT,
	.bmAttributes 		=	USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor fs_bulkin1_ep_desc = {
	.bLength 			=	USB_DT_ENDPOINT_SIZE,
	.bDescriptorType 	=	USB_DT_ENDPOINT,
	.bEndpointAddress 	=	USB_DIR_IN,
	.bmAttributes 		=	USB_ENDPOINT_XFER_BULK,
};


static struct usb_endpoint_descriptor fs_bulkout2_ep_desc = {
	.bLength 			=	USB_DT_ENDPOINT_SIZE,
	.bDescriptorType 	=	USB_DT_ENDPOINT,
	.bEndpointAddress 	=	USB_DIR_OUT,
	.bmAttributes 		=	USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor fs_bulkin2_ep_desc = {
	.bLength 			=	USB_DT_ENDPOINT_SIZE,
	.bDescriptorType 	=	USB_DT_ENDPOINT,
	.bEndpointAddress 	=	USB_DIR_IN,
	.bmAttributes 		=	USB_ENDPOINT_XFER_BULK,
};



static struct usb_endpoint_descriptor fs_intrin1_ep_desc = {
	.bLength 			=	USB_DT_ENDPOINT_SIZE,
	.bDescriptorType 	=	USB_DT_ENDPOINT,
	.bEndpointAddress 	=	USB_DIR_IN,
#ifndef BPS_X86_SIMULATION
	.bmAttributes 		=	USB_ENDPOINT_XFER_INT,
	.bInterval			=	BPS_FS_INTR_INTERVAL,
#else
	.bmAttributes 		=	USB_ENDPOINT_XFER_BULK,
#endif
	.wMaxPacketSize 	=	cpu_to_le16(INTR_EP_MAX_PACKET_SIZE)

};
/*** ENDS FULL SPEED ENDPOINT DESCRIPTORS   ***/

/* gadget's FS function */
static const struct usb_descriptor_header* fs_gadget_function[] = {
	(struct usb_descriptor_header *) &bps_default_interface,
	(struct usb_descriptor_header *) &fs_bulkout1_ep_desc,
	(struct usb_descriptor_header *) &fs_bulkin1_ep_desc,
	(struct usb_descriptor_header *) &fs_bulkout2_ep_desc,
	(struct usb_descriptor_header *) &fs_bulkin2_ep_desc,
	(struct usb_descriptor_header *) &fs_intrin1_ep_desc,
	NULL
};


/*** BEGINS HIGH SPEED ENDPOINT DESCRIPTORS   ***/
static struct usb_endpoint_descriptor hs_bulkout1_ep_desc = {
	.bLength 			=	USB_DT_ENDPOINT_SIZE,
	.bDescriptorType 	=	USB_DT_ENDPOINT,
	.bEndpointAddress 	=	USB_DIR_OUT,
	.bmAttributes 		=	USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize 	=	cpu_to_le16(BULK_EP_MAX_PACKET_SIZE)
};

static struct usb_endpoint_descriptor hs_bulkin1_ep_desc = {
	.bLength 			=	USB_DT_ENDPOINT_SIZE,
	.bDescriptorType 	=	USB_DT_ENDPOINT,
	.bmAttributes 		=	USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize 	=	cpu_to_le16(BULK_EP_MAX_PACKET_SIZE)
};

static struct usb_endpoint_descriptor hs_bulkout2_ep_desc = {
	.bLength 			=	USB_DT_ENDPOINT_SIZE,
	.bDescriptorType 	=	USB_DT_ENDPOINT,
	.bEndpointAddress 	=	USB_DIR_OUT,
	.bmAttributes 		=	USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize 	=	cpu_to_le16(BULK_EP_MAX_PACKET_SIZE)
};

static struct usb_endpoint_descriptor hs_bulkin2_ep_desc = {
	.bLength 			=	USB_DT_ENDPOINT_SIZE,
	.bDescriptorType 	=	USB_DT_ENDPOINT,
	.bmAttributes 		=	USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize 	=	cpu_to_le16(BULK_EP_MAX_PACKET_SIZE)
};


static struct usb_endpoint_descriptor hs_intrin1_ep_desc = {
	.bLength 			=	USB_DT_ENDPOINT_SIZE,
	.bDescriptorType 	=	USB_DT_ENDPOINT,
	.bEndpointAddress 	=	USB_DIR_IN,
#ifndef BPS_X86_SIMULATION
	.bmAttributes 		=	USB_ENDPOINT_XFER_INT,
	.bInterval			=	BPS_HS_INTR_INTERVAL,
#else
	.bmAttributes 		=	USB_ENDPOINT_XFER_BULK,
#endif
	.wMaxPacketSize 	=	cpu_to_le16(INTR_EP_MAX_PACKET_SIZE)
};
/*** ENDS HIGH SPEED ENDPOINT DESCRIPTORS   ***/





/* gadget's HS function */
static const struct usb_descriptor_header *hs_gadget_function[] = {
	(struct usb_descriptor_header *) &bps_default_interface,
	(struct usb_descriptor_header *) &hs_bulkout1_ep_desc,
	(struct usb_descriptor_header *) &hs_bulkin1_ep_desc,
	(struct usb_descriptor_header *) &hs_bulkout2_ep_desc,
	(struct usb_descriptor_header *) &hs_bulkin2_ep_desc,
	(struct usb_descriptor_header *) &hs_intrin1_ep_desc,
	NULL
};
/***   ENDS the USB descriptors used by the driver  ***/


/**
 * alloc_ep_request - allocates an endpoint request on the specified endpoint
 * @p_ep:the endpoint associated with the request
 * @length:the number of bytes to be allocated
 *
 * returns the newly allocated request if the function succeeds; NULL otherwise 
 */
static struct usb_request* alloc_ep_request(struct usb_ep *p_ep,
											unsigned length)
{
	struct usb_request *p_req;

	p_req = usb_ep_alloc_request(p_ep,GFP_ATOMIC);
	
	if (p_req) {

		p_req->length = length;
		p_req->buf = kmalloc(length,GFP_ATOMIC);

		if (NULL==p_req->buf) {
			usb_ep_free_request(p_ep, p_req);
			p_req = NULL;
		}
	}

	return p_req;
}



/**
 * dequeue_request - dequeues an endpoint request from an endpoint
 * @p_bpsep:the bps endpoint associated with the request
 *
 * if the request is active, it will be dequed and the endpoint 
 * fifo will be flushed.
 */
static void dequeue_request(struct bps_ep *p_bpsep)
{
	if (p_bpsep && p_bpsep->p_ep && p_bpsep->p_req) {

		if (p_bpsep->queued) {
			usb_ep_dequeue(p_bpsep->p_ep,p_bpsep->p_req);
			usb_ep_fifo_flush(p_bpsep->p_ep);
		}

	}
}


/**
 * free_usb_request - releases the resources associated with an endpoint.
 * @p_bpsep:the bps endpoint associated with the request
 *
 * dequeues the request associated with the bps endpoint and frees the memory 
 * associated with the request
 */
static void free_usb_request(struct bps_ep *p_bpsep)
{
	if (p_bpsep && p_bpsep->p_ep && p_bpsep->p_req) {

		dequeue_request(p_bpsep);

		kfree(p_bpsep->p_req->buf);
		usb_ep_free_request(p_bpsep->p_ep, p_bpsep->p_req);
		
		p_bpsep->p_req = NULL;
	}
}


/**
 * free_all_requests - releases the resources associated all bps usb requests.
 *
 * iterates through the endpoint list and invokes the function calls to free 
 * the resources associated with them
 */
static void free_all_requests(void)
{
	int i;
	for (i=0; i < BPS_CDEV_MINOR_COUNT; i++) {

		free_usb_request(&bps.cdevs[i].intr_ep);
	}

	free_usb_request(&bps.data_err_ep);
	
	for (i=EP0;i<MAX_BPS_EP;i++) {

		free_usb_request(&bps.ep_list[i]);
	}
}


/**
 * disable_bps_eps - disables the endpoints used by the bps endpoints.
 *
 * dequeues all requests on the endpoints and disables them.
 */
static void disable_bps_eps(void)
{
	int i;

	dequeue_request(&bps.data_err_ep);

	for (i = 0; i < BPS_CDEV_MINOR_COUNT; i++) {
		dequeue_request(&bps.cdevs[i].intr_ep);
	}


	for (i=FIRST_BPS_EP;i<MAX_BPS_EP;i++) {

		if (bps.ep_list[i].p_ep) {

			dequeue_request(&bps.ep_list[i]);
			usb_ep_disable(bps.ep_list[i].p_ep);
		}
	}
}



/**
 * ep0_complete - completion handler for ep0 request
 * @p_ep:endpoint associated with the request
 * @p_req:usb request associated with the request
 *
 * the usb request completion handler for the ep0 request
 */
static void ep0_complete(struct usb_ep *p_ep, struct usb_request *p_req)
{
	bps.ep_list[EP0].queued = FALSE;
	if (p_req->status)
		printk(KERN_ERR "ep0_urb failed:%d\n",p_req->status);
}


/**
 * data_err_urb_complete - completion handler for data channel error request
 * @p_ep:endpoint associated with the request
 * @p_req:usb request associated with the request
 *
 * completion handler for data channel error request. Wakesup the process 
 * waiting for the transfer completion and logs if any error happens.
 */
static void data_err_urb_complete(struct usb_ep *p_ep, 
									struct usb_request *p_req)
{
	struct bps_cdev *p_dev = &bps.cdevs[DATA_CHANNEL];
	int status = p_req->status;
	
	spin_lock(&p_dev->lock);
	bps.data_err_ep.queued = FALSE;
	wake_up(&p_dev->wait);
	spin_unlock(&p_dev->lock);

	if (status)
		printk(KERN_ERR "data_err_urb failed:%d\n",status);

}


/**
 * intr_urb_complete - completion handler for interrupt channel error request
 * @p_ep:endpoint associated with the request
 * @p_req:usb request associated with the request
 *
 * completion handler for interrupt channel request. Notifies the interrupt 
 * thread about the transfer completion and logs if any error happens.
 */
static void intr_urb_complete(struct usb_ep *p_ep, struct usb_request *p_req)
{
	struct bps_cdev *p_dev = p_req->context;
	p_dev->intr_ep.queued = FALSE;
	wake_up(&p_dev->wait);
	if (p_req->status)
		printk(KERN_ERR "intr_urb failed:%d\n",p_req->status);

}


/**
 * bulk_urb_complete - completion handler for bulk endpoint requests
 * @p_ep:endpoint associated with the request
 * @p_req:usb request associated with the request
 *
 * completion handler for bulk endpoint requests. Notifies the process 
 * waiting for the transfer completion and logs if any error happens. If the 
 * usb request fails for BULK-OUT requests, re-submits the requests if the 
 * corresponding process is still active.
 */
static void bulk_urb_complete(struct usb_ep *p_ep,
								struct usb_request *p_req)
{
	struct bps_cdev *p_dev;
	struct bps_ep *p_bpsep=container_of(p_ep->driver_data,struct bps_ep,p_ep);
	int status = p_req->status;
	int re_submission_failed = FALSE;

	/*printk(KERN_DEBUG "%s:status=%d\n",__func__,p_req->status);*/

	if (DATA_OUT_EP == p_bpsep->ep_id || DATA_IN_EP == p_bpsep->ep_id)
		p_dev = &bps.cdevs[DATA_CHANNEL];
	else
		p_dev = &bps.cdevs[CONFIG_CHANNEL];

	spin_lock(&p_dev->lock);

	if (p_req->status) {
		
		if (p_dev->opened && bps.config_set && (FALSE == bps.suspended) && 
				(DATA_OUT_EP == p_bpsep->ep_id || 
				 CONFIG_OUT_EP == p_bpsep->ep_id)) {

			/*The request type is BULK-OUT and the process is still active, 
			 * so resubmit it
			 */
			DPRINT("Resubmitting urb\n")

			if (!usb_ep_queue(p_ep,p_req,GFP_ATOMIC)) {

				spin_unlock(&p_dev->lock);
				return;

			} else {

				re_submission_failed=TRUE;
			}
		}
	}

	p_bpsep->queued = FALSE;
	wake_up(&p_dev->wait);
	spin_unlock(&p_dev->lock);
	
	if (status)
		printk(KERN_ERR "bulk_urb failed:%d\n",status);

	if (re_submission_failed)
		printk(KERN_ERR "urb resubmission failed\n");
}



/**
 * reset_config - completion handler for bulk endpoint requests
 * @p_gadget: gadget associated with the request
 *
 * disables all endpoints and wakesup the interrupt endpoint thread 
 */
static void reset_config(struct usb_gadget *p_gadget)
{
	bps.config_set = FALSE;
	disable_bps_eps();
}


/**
 * select_ep_descriptor - selects the endpoint descriptor based on host's speed
 * @p_gadget: gadget associated with the request
 * @p_fs_desc: full-speed endpoint descriptor.
 * @p_hs_desc: high-speed endpoint descriptor.
 *
 * Selects the appropriate endpoint descriptor based on host's speed.
 *
 * Returns high-speed/full-speed endpoint descriptor based on host's speed.
 */
static struct usb_endpoint_descriptor *select_ep_descriptor(
									struct usb_gadget *p_gadget,
									struct usb_endpoint_descriptor *p_fs_desc,
									struct usb_endpoint_descriptor *p_hs_desc)
{
	if (USB_SPEED_HIGH == p_gadget->speed)
		return p_hs_desc;
	
	return p_fs_desc;
}



/**
 * save_urb_lengths - saves the usb request lengths.
 *
 * Iterates through all the endpoints and saves the associated usb request's 
 * length for future use, mainly for the write function.
 */
static void save_urb_lengths(void)
{
	int i;
	for (i = FIRST_BPS_EP; i < MAX_BPS_EP; i++) {
		if (INTR_IN_EP != i) {
			if (BULKOUT1 == i || BULKOUT2 == i) {
				bps.ep_list[i].p_req->length = 
					min(((unsigned)BULK_OUT_URB_BUFFER_SIZE),((unsigned)bps.ep_list[i].p_ep->maxpacket));
			}
			bps.ep_list[i].urb_buffer_size = bps.ep_list[i].p_req->length;
		}
	}

	bps.data_err_ep.urb_buffer_size = bps.data_err_ep.p_req->length;

	for (i = 0; i < BPS_CDEV_MINOR_COUNT; i++) {
		bps.cdevs[i].intr_ep.urb_buffer_size = bps.cdevs[i].intr_ep.p_req->length;
	}	
}


/**
 * set_config - sets gadget function and enables the endpoints
 * @p_gadget: gadget associated with the request
 *
 * Resets the configuration, enables the endpoints, sets the usb 
 * request lengths and wakes up the interrupt thread.
 *
 * Returns 0 on success; error code otherwise.
 */
static int set_config(struct usb_gadget *p_gadget)
{
	int i;
	int retval = 0;

	reset_config(p_gadget);
	
	for (i = FIRST_BPS_EP; i < MAX_BPS_EP; i++) {
		if (usb_ep_enable(bps.ep_list[i].p_ep,
			select_ep_descriptor(p_gadget,
				(struct usb_endpoint_descriptor*)fs_gadget_function[i],
				(struct usb_endpoint_descriptor*)hs_gadget_function[i]))) {
			printk(KERN_ERR "Can't enable ep%d\n",i);
			retval = -ENODEV;
			break;
		}

	}

	if (!retval) {
		save_urb_lengths();
		bps.config_set = TRUE;
	}

	return retval;
}


/**
 * allocate_endpoints - Allocates all the endpoints need by the driver.
 * @p_gadget: gadget associated with the request
 *
 * Allocates all the endpoints need by the driver 
 *
 * Returns 0 on success; error code otherwise.
 */
static int allocate_endpoints(struct usb_gadget *p_gadget)
{
	int i;
	int retval = 0;

	bps.ep_list[EP0].p_ep = p_gadget->ep0;

	for (i = FIRST_BPS_EP; i < MAX_BPS_EP; i++) {

		bps.ep_list[i].ep_id = i;
		bps.ep_list[i].p_ep = usb_ep_autoconfig(p_gadget,
				(struct usb_endpoint_descriptor *)fs_gadget_function[i]);
		
		if (bps.ep_list[i].p_ep) {
			bps.ep_list[i].p_ep->driver_data = &bps.ep_list[i].p_ep;
		} else {
			retval = -ENODEV;
			printk(KERN_ERR "EP allocation failed\n");
			break;
		}
	}

	return retval;	
}


/**
 * allocate_extra_ep_requests - Allocates the usb requests need by the driver.
 * @p_gadget: gadget associated with the request
 *
 * Allocates all the endpoints need by the driver 
 *
 * Returns 0 on success; error code otherwise.
 */
static int allocate_extra_usb_requests(struct usb_gadget *p_gadget)
{

	int i;
	struct bps_ep *p_bpsep;
	int retval = 0;

	bps.data_err_ep.p_ep = bps.ep_list[CONFIG_IN_EP].p_ep;
	bps.data_err_ep.p_req = alloc_ep_request(bps.data_err_ep.p_ep,
												DATA_ERR_URB_BUFFER_SIZE);
	if (bps.data_err_ep.p_req) {
		bps.data_err_ep.p_req->complete = data_err_urb_complete;
		
		for (i = 0; i < BPS_CDEV_MINOR_COUNT; i++) {
			p_bpsep = &bps.cdevs[i].intr_ep;
			p_bpsep->p_ep = bps.ep_list[INTR_IN_EP].p_ep;
			p_bpsep->p_req = alloc_ep_request(p_bpsep->p_ep,
												INTR_URB_BUFFER_SIZE);

			if (NULL == p_bpsep->p_req) {
				retval = -ENOMEM;
				break;
			} else {
				p_bpsep->p_req->complete = intr_urb_complete;
				p_bpsep->p_req->context = &bps.cdevs[i];
			}
		}

	} else {
		retval = -ENOMEM;
	}

	return retval;
}


/**
 * allocate_endpoint_requests - Allocates the usb requests need by the driver.
 * @p_gadget: gadget associated with the request
 *
 * Allocates all the endpoints needed by the driver 
 *
 * Returns 0 on success; error code otherwise.
 */
static int allocate_endpoint_requests(struct usb_gadget *p_gadget)
{
	int buffer_size,i;
	int retval = 0;

	for (i = EP0; i < MAX_BPS_EP; i++) {
		if (EP0 == i)
			buffer_size = EP0_URB_BUFFER_SIZE;
		else if (INTRIN1 == i)
			continue;
		else if (BULKOUT1 == i || BULKOUT2 == i)
			buffer_size = BULK_OUT_URB_BUFFER_SIZE;
		else
			buffer_size = BULK_IN_URB_BUFFER_SIZE;
			
		bps.ep_list[i].p_req = alloc_ep_request(bps.ep_list[i].p_ep,
															buffer_size);
		if (bps.ep_list[i].p_req) {
			if (INTR_IN_EP != i) {
				bps.ep_list[i].p_req->complete = bulk_urb_complete;
			}
		} else {
			retval = -ENOMEM;
			break;
		}
	}

	if (!retval) {
		retval = allocate_extra_usb_requests(p_gadget);
	}

	if (retval) {
		printk(KERN_ERR "Unable to allocate ep request\n");
		free_all_requests();
	}

	return retval;
}


/**
 * bind_gadget - Associtates the gadget driver with the device driver.
 * @p_gadget: gadget associated with the request
 *
 * Creates interrupt handler thread, allocates endpoints and 
 * usb requests. Kills the interrupt thread if anything goes wrong.
 *
 * Returns 0 on success; error code otherwise.
 */
static __init int bind_gadget(struct usb_gadget *p_gadget)
{
	int i;
	int retval = -ENODEV;
	PRINT_ENTER_FN
	
	bps.bound = TRUE;

	//TODO: Remove selfpowered feature from the production driver
	usb_gadget_set_selfpowered(p_gadget);

	usb_ep_autoconfig_reset(p_gadget);

	if (!(retval = allocate_endpoints(p_gadget))) {

		if (!(retval = allocate_endpoint_requests(p_gadget))) {
			
			save_urb_lengths();
			
			for(i = FIRST_BPS_EP; i < MAX_BPS_EP; i++) {
				((struct usb_endpoint_descriptor*)
				 hs_gadget_function[i])->bEndpointAddress =
				((struct usb_endpoint_descriptor*)
				 fs_gadget_function[i])->bEndpointAddress;

			}

			bps_device_descriptor.bMaxPacketSize0 = 
									bps.ep_list[EP0].p_ep->maxpacket;

		}
	}

	
	if (retval) {
		bps.bound = FALSE;
	}

	return retval;
}



/**
 * unbind_gadget - Disconnects the gadget driver from the device driver.
 * @p_gadget: gadget associated with the request
 *
 * Kills the interrupt handler thread and frees all the usb requests 
 */
static void unbind_gadget(struct usb_gadget *p_gadget)
{
	PRINT_ENTER_FN
	
	bps.bound = FALSE;
	
	free_all_requests();
}


/**
 * populate_config_buf - Copies the gadget function details to the ep0 buffer.
 * @p_gadget: gadget associated with the request
 * @p_buffer: ep0 buffer associated with the request
 * @type: descriptor type to be copied
 *
 * Chooses the gadget function depending on the device speed and copies it 
 * to the ep0 buffer.
 *
 * Returns the number of bytes to be sent to the host on success; 
 * error code otherwise.
 */
static int populate_config_buf(struct usb_gadget *p_gadget,
								u8 *p_buffer, u8 type, unsigned index)
{
	int len;
	const struct usb_descriptor_header **p_function;

	if (index > 0)
		return -EINVAL;

	if (USB_SPEED_HIGH == p_gadget->speed)
		p_function = hs_gadget_function;
	else
		p_function = fs_gadget_function;


	len = usb_gadget_config_buf(&bps_config_descriptor, 
					p_buffer, bps.ep_list[EP0].p_ep->maxpacket, p_function);
	if (len > 0) {
		((struct usb_config_descriptor *) p_buffer)->bDescriptorType = type;
	}

	return len;
}



/**
 * setup_gadget - handles the ep0 requests during the setup phase.
 * @p_gadget: gadget associated with the request
 * @p_ctrl: usb control request received from the host.
 *
 * Responds to the setup commands that will be sent by the host during 
 * the setup phase.
 *
 * Returns the number of bytes to be sent to the host on success; 
 * error code otherwise.
 */
static int setup_gadget(struct usb_gadget *p_gadget,
						const struct usb_ctrlrequest *p_ctrl)
{
	int retval = -EOPNOTSUPP;
	//u16 wIndex = le16_to_cpu(p_ctrl->wIndex);
	u16 wValue = le16_to_cpu(p_ctrl->wValue);
	u16 wLength = le16_to_cpu(p_ctrl->wLength);
	struct usb_request *p_req = bps.ep_list[EP0].p_req;

	p_req->complete = ep0_complete;

	

	switch (p_ctrl->bRequestType & USB_TYPE_MASK) {
	
	case USB_TYPE_STANDARD:
			switch (p_ctrl->bRequest) {

			case USB_REQ_GET_DESCRIPTOR:
				switch (wValue >> 8) {
				case USB_DT_DEVICE:
					retval = min(wLength, 
								(u16)sizeof(bps_device_descriptor));

					memcpy(p_req->buf,
							&bps_device_descriptor, retval);

					break;
				case USB_DT_CONFIG:
					retval = populate_config_buf(p_gadget,
												p_req->buf,
												wValue >> 8,
												wValue & 0xff);

					if (retval >= 0) {
						retval = min(wLength, (u16)retval);
					}
					break;
				
				case USB_DT_STRING:
					retval = usb_gadget_get_string(&gadget_string_table,
													wValue & 0xff,
													p_req->buf);
					if (retval >= 0) {
						retval = min(wLength,(u16)retval);
					}
					break;
				case USB_DT_DEVICE_QUALIFIER:
					if (!p_gadget->is_dualspeed) {
						break;
					}
					retval = sizeof bps_dev_qualifier;
					memcpy(p_req->buf, &bps_dev_qualifier, retval);

					break;
				default:
					;

				}
				break;
			case USB_REQ_SET_CONFIGURATION:
				retval = set_config(p_gadget);
				break;
			case USB_REQ_GET_CONFIGURATION:
				break;
			case USB_REQ_SET_INTERFACE:
				//We've only one interface. So, set config 
				//function will do
				retval = 0;
				break;
			case USB_REQ_GET_INTERFACE:
				break;
			}
			break;

	default:
		break;
	}

	if (retval >= 0) {
		p_req->length = retval;
		p_req->zero = retval < wLength;
		retval = usb_ep_queue(bps.ep_list[EP0].p_ep,
								p_req,GFP_ATOMIC);
		if (!retval)
			bps.ep_list[EP0].queued = TRUE;
	}

	return retval;
}

/**
 * disconnect_gadget - handles the host disconnection event.
 * @p_gadget: gadget associated with the request
 *
 * Handles the host disconnection event by reseting the chosen 
 * configuration.
 *
 */
static void disconnect_gadget(struct usb_gadget *p_gadget)
{
	PRINT_ENTER_FN
	reset_config(p_gadget);
}

/**
 * queue_out_request - queues a request on a BULK-OUT endpoint
 * @p_dev: bps character device associated with the request
 * @claim_lock: whether or not to obtain the lock associated with 
 *				the device.
 *
 * Obtains the lock associated with the bps character device and 
 * queues the BULK-OUT request associated with the corresponding 
 * endpoint.
 * 
 *
 * Returns 0 on success; error code otherwise.
 */
static int queue_out_request(struct bps_cdev *p_dev, int claim_lock)
{
	unsigned long flags;
	int retval = 0;

	if (claim_lock)
		spin_lock_irqsave(&p_dev->lock,flags);
	
	if (FALSE == bps.config_set || TRUE == bps.suspended || 
										FALSE == p_dev->opened) {
		retval = -EINVAL;
	} else {

		if (FALSE == p_dev->p_outep->queued) {
			p_dev->rx_bytes = 0;
			p_dev->p_outep->p_req->actual = 0;

			if (usb_ep_queue(p_dev->p_outep->p_ep,
								p_dev->p_outep->p_req, GFP_ATOMIC)) {
				printk(KERN_ERR "ep_queue failed\n");
				retval = -EIO;
			} else {
				p_dev->p_outep->queued = TRUE;
			}
		
		} else {
			DPRINT("duplicate queuing request\n")
		}
	}

	if (claim_lock)
		spin_unlock_irqrestore(&p_dev->lock,flags);

	return retval;
}

/**
 * suspend_gadget - handles the host's suspend command.
 * @p_gadget: gadget associated with the request
 *
 * Handles the host's suspend command by dequeuing all urbs. 
 */
static void suspend_gadget(struct usb_gadget* p_gadget)
{
	int i;
	unsigned long flags;
	PRINT_ENTER_FN
	
	spin_lock_irqsave(&bps.lock,flags);
	bps.suspended = TRUE;
	
	dequeue_request(&bps.data_err_ep);

	for (i = 0; i < BPS_CDEV_MINOR_COUNT; i++) {
		dequeue_request(&bps.cdevs[i].intr_ep);
	}

	for (i=FIRST_BPS_EP;i<MAX_BPS_EP;i++) {
		if (bps.ep_list[i].p_ep) {
			dequeue_request(&bps.ep_list[i]);
		}
	}

	spin_unlock_irqrestore(&bps.lock,flags);
}


/**
 * resume_gadget - handles the host's resume command.
 * @p_gadget: gadget associated with the request
 *
 * Handles the host's resume command.
 */
static void	resume_gadget(struct usb_gadget* p_gadget)
{
	unsigned long flags;
	int i;
	PRINT_ENTER_FN
	
	spin_lock_irqsave(&bps.lock,flags);
	bps.suspended = FALSE;
	for (i = 0; i < BPS_CDEV_MINOR_COUNT; i++) {
		queue_out_request(&bps.cdevs[i],TRUE);
	}
	spin_unlock_irqrestore(&bps.lock,flags);
}





/**
 * bps_open - implements the character device's 'open' syscall handler
 * @p_inode: character device's inode* associated with the request.
 * @p_file: character device's file* associated with the request.
 *
 * Blocks two processes from simultaneously opening the bps chacter device 
 * node and initializes data structures associated with the bps character 
 * device.
 *
 * Returns 0 on success; error code if the device id already in use or 
 * 				if the host hasn't chosen usb configuration of the gadget.
 */
static int bps_open(struct inode *p_inode, struct file *p_file)
{
	unsigned long flags;
	int retval=0;
	
	spin_lock_irqsave(&bps.lock,flags);
	
	if (TRUE == bps.config_set) {
		
		int channel = iminor(p_inode);
		
		if (bps.cdevs[channel].opened) {
			retval = -EBUSY;
		} else {
			struct bps_cdev* p_dev = &bps.cdevs[channel];
			p_file->private_data = p_dev;

			p_dev->channel = channel;
			p_dev->rx_bytes = 0;
			p_dev->p_outep->p_req->actual = 0;
			p_dev->opened = TRUE;

			queue_out_request(p_dev,FALSE);
		}

	} else {
		retval = -EIO;
	}
	
	spin_unlock_irqrestore(&bps.lock,flags);
	
	return retval;
}


/**
 * bps_release - implements the character device's 'close' syscall handler 
 * @p_inode: character device's inode* associated with the request.
 * @p_file: character device's file* associated with the request.
 *
 * Dequeues all usb requests associated with the bps device's endpoints 
 * and resets the device's data structure members.
 *
 * Always returns 0 
 */
static int bps_release(struct inode *p_inode, struct file *p_file)
{
	unsigned long flags;
	
	struct bps_cdev *p_dev = p_file->private_data;

	spin_lock_irqsave(&bps.lock,flags);
	
	p_dev->opened = FALSE;
	
	dequeue_request(&p_dev->intr_ep);
	dequeue_request(p_dev->p_inep);
	dequeue_request(p_dev->p_outep);

	if (DATA_CHANNEL == p_dev->channel) 
		dequeue_request(&bps.data_err_ep);

	p_file->private_data = NULL;
	
	spin_unlock_irqrestore(&bps.lock,flags);

	return 0;
}

/**
 * bps_read - implements the character device's 'read' syscall handler 
 * @p_file: character device's file* associated with the request.
 * @p_user: userspace buffer associated with the request.
 * @len: length of the userspace buffer associated with the request.
 * @p_offset: offset of the file associated with the request.
 *
 * Returns error code if the parameters and invalid or if the system call 
 * is in ASYNC mode and there is no data already available or if the host 
 * hasn't chosen the device configuration. If the system call is a 
 * blocking one and there is data already available to be read, then 
 * returns it immediately otherwise suspends the process until hosts sends
 * something or the usb request fails.
 *
 * Returns the number of bytes successfully copied to the user buffer 
 *  or error code otherwise
 */
static ssize_t bps_read(struct file *p_file, char __user *p_user,
						size_t len, loff_t *p_offset)
{
	ssize_t retval = 0;
	struct bps_cdev* p_dev = p_file->private_data;
	PRINT_ENTER_FN
	if (NULL==p_user || 0==len) {
		retval = -EINVAL;
	} else {
		unsigned long flags;
		struct bps_ep *p_bpsep = p_dev->p_outep;

restart_read:
		spin_lock_irqsave(&p_dev->lock,flags);

		if (!bps.config_set || bps.suspended) {
			retval = -ECOMM;
			spin_unlock_irqrestore(&p_dev->lock,flags);
		} else {
check_queued_request:			
			if (TRUE == p_bpsep->queued) {
				
				spin_unlock_irqrestore(&p_dev->lock,flags);
				
				if (p_file->f_flags & O_NONBLOCK) {
					retval = -EAGAIN;
				} else {
					if(wait_event_interruptible(p_dev->wait,
									FALSE==p_bpsep->queued)) {
						retval = -EINTR;
					} else if(p_bpsep->p_req->status) {
					//endpoint request failed.
						goto restart_read;
					}
				}

			} else {
				
				BUG_ON(p_dev->rx_bytes > p_bpsep->p_req->actual);

				if (p_bpsep->p_req->status ||  
				  ((p_dev->rx_bytes == p_bpsep->p_req->actual) && 
				   p_bpsep->p_req->actual)) {

					if (queue_out_request(p_dev,FALSE)) {
						retval = -EIO;
					} else { //queuing success.
						goto check_queued_request;
					}
				}
				spin_unlock_irqrestore(&p_dev->lock,flags);
			}

			if (!retval) {

				const char *p_buffer = ((const char*)p_bpsep->p_req->buf) + 
														p_dev->rx_bytes;
				uint32_t bytes_to_copy = min(len,
						(size_t)(p_bpsep->p_req->actual - p_dev->rx_bytes));

				BUG_ON(p_dev->rx_bytes > p_bpsep->p_req->actual);

				if (copy_to_user(p_user, p_buffer, bytes_to_copy))
					retval = -EFAULT;
				else
					retval = bytes_to_copy;

				p_dev->rx_bytes += bytes_to_copy;

				//We've consumed all the bytes sent by the host.
				//So, queue another request.
				if (p_dev->rx_bytes == p_bpsep->p_req->actual)
					queue_out_request(p_dev,TRUE);
			}
		}
	}
	PRINT_LEAVE_FN
	return retval;
}


/**
 * is_write_channel_busy - returns whether or not the device's IN 
 * 							endpoint is busy.
 *
 * @p_dev: the bps character device associated with the request.
 *
 * Checks and returns whether the device' IN endpoint has some 
 * request queued in it.
 *
 * Returns 0 if the there is no request queued on the IN endpoint, 
 * TRUE otherwise.
 */
static int is_write_channel_busy(struct bps_cdev *p_dev)
{
	if (DATA_CHANNEL == p_dev->channel) {
		return (p_dev->p_inep->queued | bps.data_err_ep.queued);
	} else {
		return p_dev->p_inep->queued;
	}
}


/**
 * bps_write_impl - does the real writing on the usb IN endpoint 
 * @p_file: character device's file* associated with the request.
 * @p_user: userspace buffer associated with the request.
 * @len: length of the userspace buffer associated with the request.
 * @p_bpsep: the bps endpoint associated with the request.
 *
 * Common method for doing write on the BPS channels and for reporting errors 
 * occurring in 'data channel' through 'config channel' (as per Brady's 
 * requirement). Returns an error code if the parameters and invalid or if 
 * the system call is in ASYNC mode or if the host hasn't chosen the device 
 * configuration. If the system call is a blocking one then suspends the 
 * process until the entire data is transferred to the host or the usb request
 * fails.
 *
 * Returns the number of bytes successfully sent to the host or an error code. 
 */
static ssize_t bps_write_impl(struct file *p_file, const char __user *p_user,
								size_t len, struct bps_ep *p_bpsep)
{
	ssize_t retval = 0;
	struct bps_cdev* p_dev = p_file->private_data;

	PRINT_ENTER_FN
	if (NULL == p_user || 0 == len)
		retval = -EINVAL;
	else if (p_file->f_flags & O_NONBLOCK)
		retval = -EWOULDBLOCK;
	else {
		size_t bytes_to_tx;
		unsigned long flags;
		
		while (len) {

			bytes_to_tx = min(p_bpsep->urb_buffer_size,len);
			
			if (copy_from_user(p_bpsep->p_req->buf,
									p_user, bytes_to_tx)) {
				retval = -EFAULT;
				break;
			} else {
				spin_lock_irqsave(&p_dev->lock,flags);
				if (FALSE == bps.config_set || TRUE == bps.suspended) {
					retval = -ECOMM;
					spin_unlock_irqrestore(&p_dev->lock,flags);
					break;
				} else {
					p_bpsep->p_req->length = bytes_to_tx;
					
					if(usb_ep_queue(p_bpsep->p_ep,
									p_bpsep->p_req, GFP_ATOMIC)) {
						retval = -EIO;
						spin_unlock_irqrestore(&p_dev->lock, flags);
						printk(KERN_ERR "ep_queue failed\n");
						break;
					} else { //queuing success
						p_bpsep->queued = TRUE;
						spin_unlock_irqrestore(&p_dev->lock, flags);
						
						wait_event(p_dev->wait, FALSE == is_write_channel_busy(p_dev));

						if (0 == p_bpsep->p_req->status) {
							//Transfer success
							len    -= p_bpsep->p_req->actual;
							p_user += p_bpsep->p_req->actual;
							retval += p_bpsep->p_req->actual;
						} else {
							retval = -EIO;
							break;
						}
					}
				}//else if (FALSE == bps.config_set || TRUE == bps.suspended)

			}//else copy_from_user

		}//end while
	}
	
	PRINT_LEAVE_FN
	return retval;
}

/**
 * bps_write - implements the character device's 'write' syscall handler 
 * @p_file: character device's file* associated with the request.
 * @p_user: userspace buffer associated with the request.
 * @len: length of the userspace buffer associated with the request.
 * @p_offset: offset of the file associated with the request.
 *
 * Invokes the method 'bps_write_impl' for doing the real writing on to 
 * the host and returns bytes transferred.
 *
 * Returns the number of bytes successfully sent to the host or an error code.
 */
static ssize_t bps_write(struct file *p_file, const char __user *p_user,
						size_t len, loff_t *p_offset)
{
	ssize_t retval;
	struct bps_cdev *p_dev = p_file->private_data;

	struct bps_ep *p_bpsep = p_dev->p_inep;

	PRINT_ENTER_FN
	retval =  bps_write_impl(p_file, p_user, len, p_bpsep);
	PRINT_LEAVE_FN

	return retval;
}


/**
 * send_zlp - Sends a ZLP packet through the channel. 
 * @p_file: character device's file* associated with the request.
 *
 * Responsible for handling the IOCTL_BPS_SEND_ZLP ioctl requests and sending
 * Zero Length Packet through the channel.
 *
 * Returns 0 on success, an error code otherwise.
 */
static ssize_t send_zlp(struct file *p_file)
{
	ssize_t retval = 0;
	struct bps_cdev* p_dev = p_file->private_data;
	struct bps_ep *p_bpsep = p_dev->p_inep;
	
	PRINT_ENTER_FN

	if (p_file->f_flags & O_NONBLOCK)
		retval = -EWOULDBLOCK;
	else {
		unsigned long flags;

		spin_lock_irqsave(&p_dev->lock,flags);
		if (FALSE == bps.config_set || TRUE == bps.suspended) {
			retval = -ECOMM;
			spin_unlock_irqrestore(&p_dev->lock,flags);
		} else {
			p_bpsep->p_req->length = 0;
			p_bpsep->p_req->zero = 1;

			if(usb_ep_queue(p_bpsep->p_ep,
							p_bpsep->p_req, GFP_ATOMIC)) {
				retval = -EIO;
				spin_unlock_irqrestore(&p_dev->lock, flags);
				printk(KERN_ERR "ep_queue failed\n");
			} else { //queuing success
				p_bpsep->queued = TRUE;
				spin_unlock_irqrestore(&p_dev->lock, flags);
				
				wait_event(p_dev->wait,
						FALSE == is_write_channel_busy(p_dev));
				if (0 != p_bpsep->p_req->status) {
					retval = -EIO;
				}
			}

			p_bpsep->p_req->zero = 0;

		}//else if (FALSE == bps.config_set || TRUE == bps.suspended)
	}
	
	PRINT_LEAVE_FN
	return retval;
}

/**
 * send_intr_notification - Sends an interrupt packet through the intr channel. 
 * @p_file: character device's file* associated with the request.
 * @value: Interrupt value to be sent.
 *
 * Sends an interrupt packet through the intr channel.
 *
 * Returns 0 on success, an error code otherwise.
 */
static ssize_t send_intr_notification(struct file *p_file,unsigned long value)
{
	ssize_t retval = 0;
	struct bps_cdev* p_dev = p_file->private_data;
	struct bps_ep *p_bpsep = &p_dev->intr_ep;

	PRINT_ENTER_FN
	if (DATA_CHANNEL_INTR_VALUE != value && 
			CONFIG_CHANNEL_INTR_VALUE != value)
		return -EINVAL;

	if (p_file->f_flags & O_NONBLOCK)
		retval = -EWOULDBLOCK;
	else {
		unsigned long flags;
		((char *)p_dev->intr_ep.p_req->buf)[0] = value;

		spin_lock_irqsave(&p_dev->lock,flags);
		if (FALSE == bps.config_set || TRUE == bps.suspended) {
			retval = -ECOMM;
			spin_unlock_irqrestore(&p_dev->lock,flags);
		} else {

			if(usb_ep_queue(p_bpsep->p_ep,
							p_bpsep->p_req, GFP_ATOMIC)) {
				retval = -EIO;
				spin_unlock_irqrestore(&p_dev->lock, flags);
				printk(KERN_ERR "ep_queue failed\n");
			} else { //queuing success
				p_bpsep->queued = TRUE;
				spin_unlock_irqrestore(&p_dev->lock, flags);
				
				wait_event(p_dev->wait, FALSE==p_bpsep->queued);
				if (0 != p_bpsep->p_req->status) {
					retval = -EIO;
				}
			}

		}//else if (FALSE == bps.config_set || TRUE == bps.suspended)
	}
	PRINT_LEAVE_FN
	return retval;
}



/**
 * send_data_error - handler for  IOCTL_BPS_SEND_DATA_ERROR
 * @p_file: character device's file* associated with the request.
 * @ioarg: ioctl argument passed by the process. Should be a 
 * 			struct bps_data_error.
 *
 * Invokes the method 'bps_write_impl' for doing the real writing on to 
 * the host and returns the bytes transferred.
 *
 * Returns the number of bytes successfully sent to the host or an error code.
 */
static ssize_t send_data_error(struct file *p_file,unsigned long ioarg)
{
	struct bps_data_error data_error;
	struct bps_cdev *p_dev = p_file->private_data;
	PRINT_ENTER_FN
	if (CONFIG_CHANNEL == p_dev->channel)
		return -EOPNOTSUPP;
	else {
		if (copy_from_user(&data_error, (const void __user*)ioarg,
												sizeof(data_error)))
			return -EFAULT;
		else {
			return bps_write_impl(p_file,data_error.p_buffer,
									data_error.len, &bps.data_err_ep);
		}
	}
}


/**
 * bps_poll - implements the character device's 'poll' syscall handler
 * @p_file: character device's file* associated with the request.
 * @p_table: poll table associated with the request.
 *
 * Returns whether or not the device can be read, written or there is 
 * any exception associated with the device.
 *
 * Returns 0 or a combination of bitwise ORed POLLIN POLLOUT POLLERR.
 */
static unsigned int bps_poll(struct file *p_file,
	   						struct poll_table_struct *p_table)
{
	unsigned long flags;
	unsigned int mask = 0;
	struct bps_cdev *p_dev = p_file->private_data;
	///struct bps_ep* p_write_ep=p_dev->p_inep; //Host's IN
	struct bps_ep *p_read_ep = p_dev->p_outep; //Host's OUT

	poll_wait(p_file, &p_dev->wait, p_table);
	
	spin_lock_irqsave(&p_dev->lock, flags);
	
	if (FALSE == bps.config_set || TRUE == bps.suspended)
		mask = POLLERR;
	else {

		if ((FALSE == p_read_ep->queued) && 
				(0 == p_read_ep->p_req->status)) {
			mask |= POLLIN;
		}

		///if(FALSE==p_write_ep->queued)
		if (FALSE == is_write_channel_busy(p_dev)) {
			mask |= POLLOUT;
		}
	}

	spin_unlock_irqrestore(&p_dev->lock,flags);

	return mask;
}


/**
 * bps_ioctl - implements the character device's 'ioctl' syscall handler
 * @p_file: character device's file* associated with the request.
 * @iocmd:  ioctl command associated with the request.
 * @ioarg:  ioctl argument associated with the request.
 *
 * 
 *
 * Returns a +ve value on success or an error code otherwise.
 */
static long bps_ioctl(struct file *p_file,
						unsigned int iocmd,unsigned long ioarg)
{
	struct bps_cdev *p_dev = p_file->private_data;

	switch(iocmd) {
	case IOCTL_BPS_GET_PACKET_SIZE:
		/*Return the maximum number of bytes 
		 *supported by the BULK endpoints
		 */
		return p_dev->p_inep->p_ep->maxpacket;

	case IOCTL_BPS_GET_SKU_MODEL:
		return sku;

	case IOCTL_BPS_GET_PROTOCOL_VERSION:
		return bps_default_interface.bInterfaceProtocol;

	case IOCTL_BPS_SEND_DATA_ERROR:
		/*For reporting 'data channel' errors through config 
		 *channel as per Brady's requirement
		 */
		return send_data_error(p_file,ioarg);
	
	case IOCTL_BPS_SEND_ZLP:
		return send_zlp(p_file);

	case IOCTL_BPS_SEND_INTR_NOTIFICATION:
		return send_intr_notification(p_file,ioarg);

	case IOCTL_BPS_IS_SUSPENDED:
		return (TRUE == bps.suspended);


	}

	return -EOPNOTSUPP;
}



static const char DESCRIPTION[] = "BPS gadget";
static const char MODULE_NAME[] = "bpsgadget";



/*Gadget driver structure to be registered with the usb core*/
static struct usb_gadget_driver bps_gadget_driver = {
	.speed		= USB_SPEED_HIGH,
	.function	= (char *)DESCRIPTION,
	.bind		= bind_gadget,
	.unbind		= unbind_gadget,
	.setup		= setup_gadget,
	.disconnect	= disconnect_gadget,
	.suspend	= suspend_gadget,
	.resume		= resume_gadget,
	.driver		= {
		.name		= (char *) MODULE_NAME,
		.owner		= THIS_MODULE,
	},
};

/*fops sturucture for the gadget driver's character driver interface.*/
static struct file_operations bps_fops = {
	.owner 	 = THIS_MODULE,
	.open	 = bps_open,
	.release = bps_release,
	.read	 = bps_read,
	.write	 = bps_write,
	.poll	 = bps_poll,
	.unlocked_ioctl = bps_ioctl,
};


/**
 * register_bps_chr_iface - registers the character device 
 * 							interface for the gadget driver 
 *
 * Returns 0 on success; error code otherwise.
 */

static __init int register_bps_chr_iface(void)
{
	bps.cdev_no = MKDEV(BPS_CDEV_MAJOR_NO,0);
	
	if (!register_chrdev_region(bps.cdev_no,
				BPS_CDEV_MINOR_COUNT,MODULE_NAME)) {
		cdev_init(&bps.dev,&bps_fops);
		
		bps.dev.owner = THIS_MODULE;

		if (!cdev_add(&bps.dev,bps.cdev_no,
					BPS_CDEV_MINOR_COUNT)) {
			return 0;
		} else {
			unregister_chrdev_region(bps.cdev_no,BPS_CDEV_MINOR_COUNT);
		}
	}

	return -ENOMEM;
}


/**
 * unregister_bps_chr_iface - unregisters the character device 
 * 							interface associated with the gadget driver 
 *
 */
static __exit void unregister_bps_chr_iface(void)
{
	cdev_del(&bps.dev);
	unregister_chrdev_region(bps.cdev_no,BPS_CDEV_MINOR_COUNT);
}

/**
 * on_load - module entry point
 *
 * Initializes data structures in the driver, registers the gadget driver 
 * with the usb core and registers the character device interface for the 
 * gadget driver.
 *
 * Returns 0 on success; an error code otherwise.
 */
static int __init on_load(void)
{
	if(sku < BPS_FIRST_SKU || sku > BPS_LAST_SKU) {
		printk(KERN_ERR "bps:Invalid SKU\n");
		return -EINVAL;
	}

	bps.config_set = FALSE;
	bps.suspended = FALSE;
	bps_device_descriptor.idProduct = __constant_cpu_to_le16(sku);

	spin_lock_init(&bps.lock);
	spin_lock_init(&bps.cdevs[DATA_CHANNEL].lock);
	spin_lock_init(&bps.cdevs[CONFIG_CHANNEL].lock);

	init_waitqueue_head(&bps.cdevs[DATA_CHANNEL].wait);
	init_waitqueue_head(&bps.cdevs[CONFIG_CHANNEL].wait);

	
	bps.cdevs[DATA_CHANNEL].p_inep = &bps.ep_list[DATA_IN_EP];
	bps.cdevs[DATA_CHANNEL].p_outep = &bps.ep_list[DATA_OUT_EP];

	bps.cdevs[CONFIG_CHANNEL].p_inep = &bps.ep_list[CONFIG_IN_EP];
	bps.cdevs[CONFIG_CHANNEL].p_outep = &bps.ep_list[CONFIG_OUT_EP];


	if (!usb_gadget_register_driver(&bps_gadget_driver)) {
		if (!register_bps_chr_iface())
			return 0;
		else
			usb_gadget_unregister_driver(&bps_gadget_driver);
	}

	return -ENOMEM;
}

/**
 * on_unload - module exit point
 *
 * Unregisters the gadget driver and the  character device interface for the 
 * gadget driver.
 */
static void __exit on_unload(void)
{
	unregister_bps_chr_iface();
	usb_gadget_unregister_driver(&bps_gadget_driver);
}


MODULE_LICENSE("GPL");
module_init(on_load);
module_exit(on_unload);
