/*
 * =====================================================================================
 *
 *       Filename:  bps_sysconfig.h
 *
 *    Description:  sysconfig_table
 *
 *        Version:  1.0
 *        Created:  06/21/10 19:51:02
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Sudheer Divakaran, 
 *        Company:  Wipro Technologies, Kochi
 *
 * =====================================================================================
 */

#ifndef __BPS_SYS_CONFIG_H__
#define __BPS_SYS_CONFIG_H__

#define BPS_SYSCONFIG_ID (0x8A5B1B42)

#define BPS_SYS_CONFIG_VERSION	(0x1)


#ifdef EVK_BOARD

#define SYSCONFIG_START_OFFSET_IN_K	 (512)			
#define DEFAULT_KERNEL1_OFFSET_IN_K  (640)
#define DEFAULT_KERNEL2_OFFSET_IN_K  (2080)

#define BOOTLOADER1_START_OFFSET_IN_K (128)
#define BOOTLOADER1_SIZE_IN_K 		  (128)
#define BOOTLOADER2_START_OFFSET_IN_K (256)
#define BOOTLOADER2_SIZE_IN_K 		BOOTLOADER1_SIZE_IN_K 	

#define SYSCONFIG_SIZE_IN_K			(128)

#define BPS_BREC_OFFSET_IN_K		(384)
#define BPS_BREC_SIZE_IN_K			(128)

#else

#define SYSCONFIG_START_OFFSET_IN_K	 (192)			
#define DEFAULT_KERNEL1_OFFSET_IN_K  (320)
#define DEFAULT_KERNEL2_OFFSET_IN_K  (2080)

#define BOOTLOADER1_START_OFFSET_IN_K (16)
#define BOOTLOADER1_SIZE_IN_K 		  (80)
#define BOOTLOADER2_START_OFFSET_IN_K (96)
#define BOOTLOADER2_SIZE_IN_K 		BOOTLOADER1_SIZE_IN_K 	

#define SYSCONFIG_SIZE_IN_K			(128)

#define BPS_BREC_OFFSET_IN_K		(176)
#define BPS_BREC_SIZE_IN_K			(16)



#endif



#define BOOT_LOADER1_PARTITION_NO	(0)
#define BOOT_LOADER2_PARTITION_NO	(1)
#define BOOT_ENV_PARTITION_NO		(2)

#define SYSCONFIG_PARTITION_NO		(3)

#define KERNEL1_PARTITION_NO		(4)
#define KERNEL2_PARTITION_NO		(5)

#define CONFIGFS_PARTITION_NO		(6)

#define ROOTFS1_PARTITION_NO		(7)
#define ROOTFS2_PARTITION_NO		(8)
#define SAFEFS_PARTITION_NO			ROOTFS2_PARTITION_NO
#define LOG_PARTITION_NO			(9)

#define ALLOWED_BAD_BLOCKS_IN_ROOTFS1		(10)
#define ALLOWED_BAD_BLOCKS_IN_ROOTFS2		(1)
#define ALLOWED_BAD_BLOCKS_IN_CONFIGFS		(1)
#define ALLOWED_BAD_BLOCKS_IN_SYSCONFIG		(2)


#define KERNEL_SIZE_IN_K			(1760)
#define DEFAULT_KERNEL  			(1)
#define ALTERNATE_KERNEL			(2)
#define DEFAULT_ROOTFS  			(ROOTFS1_PARTITION_NO)



struct bps_sys_config
{
	uint32_t id;	//0x8A5B1B42
	/*unique id for locating sys_config table within nand*/

	uint32_t version;
	/*sysconfig table version */

	uint32_t checksum;

	uint32_t kernel1_offset;
	/*offset of kernel1 image (in KB) within nand*/

	uint32_t kernel2_offset;
	/*offset of kernel2 image (in KB) within nand*/

	char active_kernel;
	/*active kernel within the two copies*/

	char active_rootfs;
	/*rootfs to be used during next reboot*/

	char boot_strapped;
	/*denotes whether the initial flashing has been done*/


	char padding1; //To make the structure memory aligned.

	/*NOTE: All future member additions should be after this!!*/

}__attribute__((packed));



#define BPS_DEFAULT_SYS_CONFIG {BPS_SYSCONFIG_ID, BPS_SYS_CONFIG_VERSION, 0, \
					DEFAULT_KERNEL1_OFFSET_IN_K, DEFAULT_KERNEL2_OFFSET_IN_K,\
   					DEFAULT_KERNEL, DEFAULT_ROOTFS,0}
	
#define sysconfig_crc(p) ((uint32_t)(p->id + p->version + p->kernel1_offset +\
		   					p->kernel2_offset + p->active_kernel + p->active_rootfs +\
		   					p->boot_strapped))



#define KERNEL_MAGIC_BOOT_SUCCESS (0x1234ABCD)
#define MAX_BOOT_RECOVERY_ATTEMPTS (10)

struct bps_last_boot_info
{
	uint32_t id;	//0x8A5B1B42
	/*unique id for locating bps_last_boot_info table*/

	uint32_t last_kernel;

	uint32_t recovery_attempts;
	uint32_t crc;
}__attribute__((packed));


#define boot_info_crc(p) ((uint32_t)(p->id + p->last_kernel + p->recovery_attempts))

#endif
