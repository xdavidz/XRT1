/*
 * A GEM style (optionally CMA backed) device manager for ZynQ based
 * OpenCL accelerators.
 *
 * Copyright (C) 2016-2019 Xilinx, Inc. All rights reserved.
 *
 * Authors:
 *    Sonal Santan <sonal.santan@xilinx.com>
 *    Jan Stephan  <j.stephan@hzdr.de>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/fpga/fpga-mgr.h>
#include "zocl_drv.h"
#include "xclbin.h"
#include "sched_exec.h"

static int zocl_fpga_mgr_load(struct drm_device *ddev, char *data, int size);

void
dz_dump(char *data, int size)
{
	int i, len = 40;

	
	if (len > size)
		len = size;

	printk("size %d first %d hex\n", size, len);
	for (i = 0; i < len; i++)
		printk(KERN_CONT"%02x", data[i]);

	printk("size %d last %d hex\n", size, len);
	for (i = size - len; i < size; i++)
		printk(KERN_CONT"%02x", data[i]);

	DZ_DEBUG("out");
}

#if defined(XCLBIN_DOWNLOAD)
/**
 * Bitstream header information.
 */
struct {
	unsigned int HeaderLength;     /* Length of header in 32 bit words */
	unsigned int BitstreamLength;  /* Length of bitstream to read in bytes*/
	unsigned char *DesignName;     /* Design name get from bitstream */
	unsigned char *PartName;       /* Part name read from bitstream */
	unsigned char *Date;           /* Date read from bitstream header */
	unsigned char *Time;           /* Bitstream creation time*/
	unsigned int MagicLength;      /* Length of the magic numbers*/
} XHwIcap_Bit_Header;

/* Used for parsing bitstream header */
#define XHI_EVEN_MAGIC_BYTE     0x0f
#define XHI_ODD_MAGIC_BYTE      0xf0

/* Extra mode for IDLE */
#define XHI_OP_IDLE  -1
#define XHI_BIT_HEADER_FAILURE -1

/* The imaginary module length register */
#define XHI_MLR                  15

#define DMA_HWICAP_BITFILE_BUFFER_SIZE 1024
#define BITFILE_BUFFER_SIZE DMA_HWICAP_BITFILE_BUFFER_SIZE

static int bitstream_parse_header(const unsigned char *Data, unsigned int Size,
				  XHwIcap_Bit_Header *Header)
{
	unsigned int i;
	unsigned int len;
	unsigned int tmp;
	unsigned int idx;

	/* Start Index at start of bitstream */
	idx = 0;

	/* Initialize HeaderLength.  If header returned early inidicates
	 * failure.
	 */
	Header->Headerlength = XHI_BIT_HEADER_FAILURE;

	/* Get "Magic" length */
	Header->Magiclength = Data[idx++];
	Header->Magiclength = (Header->Magiclength << 8) | Data[idx++];

	/* Read in "magic" */
	for (i = 0; i < Header->Magiclength - 1; i++) {
		tmp = Data[idx++];
		if (i%2 == 0 && tmp != XHI_EVEN_MAGIC_BYTE)
			return -1;   /* INVALID_FILE_HEADER_ERROR */

		if (i%2 == 1 && tmp != XHI_ODD_MAGIC_BYTE)
			return -1;   /* INVALID_FILE_HEADER_ERROR */

	}

	/* Read null end of magic data. */
	tmp = Data[idx++];

	/* Read 0x01 (short) */
	tmp = Data[idx++];
	tmp = (tmp << 8) | Data[idx++];

	/* Check the "0x01" half word */
	if (tmp != 0x01)
		return -1;	 /* INVALID_FILE_HEADER_ERROR */

	/* Read 'a' */
	tmp = Data[idx++];
	if (tmp != 'a')
		return -1;	  /* INVALID_FILE_HEADER_ERROR	*/

	/* Get Design Name length */
	len = Data[idx++];
	len = (len << 8) | Data[idx++];

	/* allocate space for design name and final null character. */
	Header->DesignName = kmalloc(len, GFP_KERNEL);

	/* Read in Design Name */
	for (i = 0; i < len; i++)
		Header->DesignName[i] = Data[idx++];

	if (Header->DesignName[len-1] != '\0')
		return -1;

	/* Read 'b' */
	tmp = Data[idx++];
	if (tmp != 'b')
		return -1;	/* INVALID_FILE_HEADER_ERROR */


	/* Get Part Name length */
	len = Data[idx++];
	len = (len << 8) | Data[idx++];

	/* allocate space for part name and final null character. */
	Header->PartName = kmalloc(len, GFP_KERNEL);

	/* Read in part name */
	for (i = 0; i < len; i++)
		Header->PartName[i] = Data[idx++];

	if (Header->PartName[len-1] != '\0')
		return -1;

	/* Read 'c' */
	tmp = Data[idx++];
	if (tmp != 'c')
		return -1;	/* INVALID_FILE_HEADER_ERROR */


	/* Get date length */
	len = Data[idx++];
	len = (len << 8) | Data[idx++];

	/* allocate space for date and final null character. */
	Header->Date = kmalloc(len, GFP_KERNEL);

	/* Read in date name */
	for (i = 0; i < len; i++)
		Header->Date[i] = Data[idx++];

	if (Header->Date[len - 1] != '\0')
		return -1;

	/* Read 'd' */
	tmp = Data[idx++];
	if (tmp != 'd')
		return -1;	/* INVALID_FILE_HEADER_ERROR  */

	/* Get time length */
	len = Data[idx++];
	len = (len << 8) | Data[idx++];

	/* allocate space for time and final null character. */
	Header->Time = kmalloc(len, GFP_KERNEL);

	/* Read in time name */
	for (i = 0; i < len; i++)
		Header->Time[i] = Data[idx++];

	if (Header->Time[len - 1] != '\0')
		return -1;

	/* Read 'e' */
	tmp = Data[idx++];
	if (tmp != 'e')
		return -1;	/* INVALID_FILE_HEADER_ERROR */

	/* Get byte length of bitstream */
	Header->BitstreamLength = Data[idx++];
	Header->BitstreamLength = (Header->BitstreamLength << 8) | Data[idx++];
	Header->BitstreamLength = (Header->BitstreamLength << 8) | Data[idx++];
	Header->BitstreamLength = (Header->BitstreamLength << 8) | Data[idx++];
	Header->HeaderLength = idx;

	DRM_INFO("Design %s: Part %s: Timestamp %s %s: Raw data size 0x%x\n",
			Header->DesignName, Header->PartName, Header->Time,
			Header->Date, Header->BitstreamLength);

	return 0;
}

static int zocl_pcap_download(struct drm_zocl_dev *zdev,
			      const void __user *bit_buf, unsigned long length)
{
	XHwIcap_Bit_Header bit_header;
	char *buffer = NULL;
	char *data = NULL;
	unsigned int i;
	char temp;
	int err;

	memset(&bit_header, 0, sizeof(bit_header));
	buffer = kmalloc(BITFILE_BUFFER_SIZE, GFP_KERNEL);

	if (!buffer) {
		err = -ENOMEM;
		goto free_buffers;
	}

	if (copy_from_user(buffer, bit_buf, BITFILE_BUFFER_SIZE)) {
		err = -EFAULT;
		goto free_buffers;
	}

	if (bitstream_parse_header(buffer, BITFILE_BUFFER_SIZE, &bit_header)) {
		err = -EINVAL;
		goto free_buffers;
	}

	if ((bit_header.HeaderLength + bit_header.BitstreamLength) > length) {
		err = -EINVAL;
		goto free_buffers;
	}

	bit_buf += bit_header.HeaderLength;

	data = vmalloc(bit_header.BitstreamLength);
	if (!data) {
		err = -ENOMEM;
		goto free_buffers;
	}

	if (copy_from_user(data, bit_buf, bit_header.BitstreamLength)) {
		err = -EFAULT;
		goto free_buffers;
	}

#if 1
	for (i = 0; i < bit_header.BitstreamLength ; i = i+4) {
		temp = data[i];
		data[i] = data[i+3];
		data[i+3] = temp;

		temp = data[i+1];
		data[i+1] = data[i+2];
		data[i+2] = temp;
	}
#endif
	err = zocl_fpga_mgr_load(zdev->ddev, data, bit_header.BitstreamLength);

free_buffers:
	kfree(buffer);
	kfree(bit_header.DesignName);
	kfree(bit_header.PartName);
	kfree(bit_header.Date);
	kfree(bit_header.Time);
	vfree(data);
	return err;
}

int zocl_pcap_download_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *filp)
{

	struct xclBin bin_obj;
	char __user *buffer;
	struct drm_zocl_dev *zdev = dev->dev_private;
	const struct drm_zocl_pcap_download *args = data;
	uint64_t primary_fw_off;
	uint64_t primary_fw_len;

	if (copy_from_user(&bin_obj, args->xclbin, sizeof(struct xclBin)))
		return -EFAULT;
	if (memcmp(bin_obj.m_magic, "xclbin0", 8))
		return -EINVAL;

	primary_fw_off = bin_obj.m_primaryFirmwareOffset;
	primary_fw_len = bin_obj.m_primaryFirmwareLength;
	if ((primary_fw_off + primary_fw_len) > bin_obj.m_length)
		return -EINVAL;

	if (bin_obj.m_secondaryFirmwareLength)
		return -EINVAL;

	buffer = (char __user *)args->xclbin;

	if (!ZOCL_ACCESS_OK(VERIFY_READ, buffer, bin_obj.m_length))
		return -EFAULT;

	buffer += primary_fw_off;

	return zocl_pcap_download(zdev, buffer, primary_fw_len);
}
#endif //XCLBIN_DOWNLOAD

static int
zocl_fpga_mgr_load(struct drm_device *ddev, char *data, int size)
{
	struct drm_zocl_dev *zdev = ddev->dev_private;
	struct device *dev = ddev->dev;
	struct fpga_manager *fpga_mgr = zdev->fpga_mgr;
	struct fpga_image_info *info;
	int err = 0;

	DZ_DUMP(data, size);

	info = fpga_image_info_alloc(dev);
	if (!info)
		return -ENOMEM;

	info->flags = FPGA_MGR_PARTIAL_RECONFIG;
	info->buf = data;
	info->count = size;

	err = fpga_mgr_load(fpga_mgr, info);
	if (err == 0)
		DZ_DEBUG("SUCCEEDED: %d", err);
	else
		DZ_DEBUG("FAILED: %d", err);

	fpga_image_info_free(info);

	return err;
}

/*XXX To be REMOVED, test only */
#if 0
int zocl_pdi_download_ioctl(struct drm_device *dev, void *buf,
			     struct drm_file *filp)
{
	struct drm_zocl_pcap_download *args = buf;
	char pdi_size[32] = "";
	int size = 0;
	char __user *buffer;
	char *data = NULL;
	int err = 0;

	DZ_DEBUG("pdi_size %ld", sizeof(pdi_size));

	/**
	 * Trick: first 32 is the size of the pdi bit stream
	 */
	buffer = (char __user *)args->xclbin;
	if (copy_from_user(pdi_size, buffer, sizeof(pdi_size)))
		return -EFAULT;

	if (kstrtoint(pdi_size, 0, &size) != 0)
		return -EINVAL;

	DZ_DEBUG("atoi size %d\n", size);
	
	// now we just copyin all pdi data into the kernel
	buffer += 32;

	data = vmalloc(size);
	if (!data) {
		err = -ENOMEM;
		goto out;
	}

	if (copy_from_user(data, buffer, size)) {
		err = -EFAULT;
		goto out;
	}
	
	err = zocl_fpga_mgr_load(dev, data, size);

out:
	if (data)
		vfree(data);

	return err;
}
#endif

char *kind_to_string(enum axlf_section_kind kind)
{
	switch (kind) {
	case 0:  return "BITSTREAM";
	case 1:  return "CLEARING_BITSTREAM";
	case 2:  return "EMBEDDED_METADATA";
	case 3:  return "FIRMWARE";
	case 4:  return "DEBUG_DATA";
	case 5:  return "SCHED_FIRMWARE";
	case 6:  return "MEM_TOPOLOGY";
	case 7:  return "CONNECTIVITY";
	case 8:  return "IP_LAYOUT";
	case 9:  return "DEBUG_IP_LAYOUT";
	case 10: return "DESIGN_CHECK_POINT";
	case 11: return "CLOCK_FREQ_TOPOLOGY";
	case 12: return "MCS";
	case 13: return "BMC";
	case 14: return "BUILD_METADATA";
	case 15: return "KEYVALUE_METADATA";
	case 16: return "USER_METADATA";
	case 17: return "DNA_CERTIFICATE";
	case 18: return "PDI";
	case 19: return "BITSTREAM_PARTIAL_PDI";
	case 20: return "DTC";
	case 21: return "EMULATION_DATA";
	case 22: return "SYSTEM_METADATA";
	default: return "UNKNOWN";
	}
}

/* should be obsoleted after mailbox implememted */
static struct axlf_section_header *
get_axlf_section(struct axlf *top, enum axlf_section_kind kind)
{
	int i = 0;

	DRM_INFO("Finding %s section header", kind_to_string(kind));
	for (i = 0; i < top->m_header.m_numSections; i++) {
		if (top->m_sections[i].m_sectionKind == kind)
			return &top->m_sections[i];
	}
	DRM_INFO("AXLF section %s header not found", kind_to_string(kind));
	return NULL;
}

int
zocl_check_section(struct axlf_section_header *header, uint64_t xclbin_len,
		enum axlf_section_kind kind)
{
	uint64_t offset;
	uint64_t size;

	DRM_INFO("Section %s details:", kind_to_string(kind));
	DRM_INFO("  offset = 0x%llx", header->m_sectionOffset);
	DRM_INFO("  size = 0x%llx", header->m_sectionSize);

	offset = header->m_sectionOffset;
	size = header->m_sectionSize;
	if (offset + size > xclbin_len) {
		DRM_ERROR("Section %s extends beyond xclbin boundary 0x%llx\n",
				kind_to_string(kind), xclbin_len);
		return -EINVAL;
	}
	return 0;
}

static int
zocl_section_info(enum axlf_section_kind kind, struct axlf *axlf_full,
	uint64_t *offset, uint64_t *size)
{
	struct axlf_section_header *memHeader = NULL;
	uint64_t xclbin_len;
	int err = 0;

	memHeader = get_axlf_section(axlf_full, kind);
	if (!memHeader)
		return -ENODEV;

	xclbin_len = axlf_full->m_header.m_length;
	err = zocl_check_section(memHeader, xclbin_len, kind);
	if (err)
		return err;

	*offset = memHeader->m_sectionOffset;
	*size = memHeader->m_sectionSize;

	return 0;
}

int
zocl_offsetof_sect(enum axlf_section_kind kind, void *sect,
		struct axlf *axlf_full, char __user *xclbin_ptr)
{
	uint64_t offset;
	uint64_t size;
	void **sect_tmp = (void *)sect;
	int err = 0;

	err = zocl_section_info(kind, axlf_full, &offset, &size);
	if (err)
		return 0;

	*sect_tmp = &xclbin_ptr[offset];

	return size;
}

int
zocl_read_sect(enum axlf_section_kind kind, void *sect,
		struct axlf *axlf_full, char __user *xclbin_ptr)
{
	uint64_t offset;
	uint64_t size;
	void **sect_tmp = (void *)sect;
	int err = 0;

	err = zocl_section_info(kind, axlf_full, &offset, &size);
	if (err)
		return 0;

	*sect_tmp = vmalloc(size);
	err = copy_from_user(*sect_tmp, &xclbin_ptr[offset], size);
	if (err) {
		vfree(*sect_tmp);
		sect = NULL;
		return err;
	}

	return size;
}

/* Record all of the hardware address apertures in the XCLBIN
 * This could be used to verify if the configure command set wrong CU base
 * address and allow user map one of the aperture to user space.
 *
 * The xclbin doesn't contain IP size. Use hardcoding size for now.
 */
int
zocl_update_apertures(struct drm_zocl_dev *zdev)
{
	struct ip_data *ip;
	struct debug_ip_data *dbg_ip;
	struct addr_aperture *apt;
	int total = 0;
	int i;

	/* Update aperture should only happen when loading xclbin */
	kfree(zdev->apertures);
	zdev->num_apts = 0;

	if (zdev->ip)
		total += zdev->ip->m_count;

	if (zdev->debug_ip)
		total += zdev->debug_ip->m_count;

	/* If this happened, the xclbin is super bad */
	if (total <= 0) {
		DRM_ERROR("Invalid number of apertures\n");
		return -EINVAL;
	}

	apt = kcalloc(total, sizeof(struct addr_aperture), GFP_KERNEL);
	if (!apt) {
		DRM_ERROR("Out of memory\n");
		return -ENOMEM;
	}

	if (zdev->ip) {
		for (i = 0; i < zdev->ip->m_count; ++i) {
			ip = &zdev->ip->m_ip_data[i];
			apt[zdev->num_apts].addr = ip->m_base_address;
			apt[zdev->num_apts].size = CU_SIZE;
			zdev->num_apts++;
		}
	}

	if (zdev->debug_ip) {
		for (i = 0; i < zdev->debug_ip->m_count; ++i) {
			dbg_ip = &zdev->debug_ip->m_debug_ip_data[i];
			apt[zdev->num_apts].addr = dbg_ip->m_base_address;
			if (dbg_ip->m_type == AXI_MONITOR_FIFO_LITE
			    || dbg_ip->m_type == AXI_MONITOR_FIFO_FULL)
				/* FIFO_LITE has 4KB and FIFO FULL has 8KB
				 * address range. Use both 8K is okay.
				 */
				apt[zdev->num_apts].size = _8KB;
			else
				/* Others debug IPs have 64KB address range*/
				apt[zdev->num_apts].size = _64KB;
			zdev->num_apts++;
		}
	}

	zdev->apertures = apt;

	return 0;
}

int
zocl_load_pdi(struct drm_device *ddev, void *data)
{
	struct drm_zocl_dev *zdev = ddev->dev_private;
	struct axlf *axlf = data;
	struct axlf *axlf_head;
	char *xclbin = NULL;
	char *pdi_buffer = NULL;
	size_t size_of_header;
	size_t num_of_sections;
	uint64_t size = 0;
	int ret = 0;

	/*
	if (copy_from_user(&axlf_head, axlf_obj->xclbin, sizeof(struct axlf)))
		return -EFAULT;
	*/
	DZ_DEBUG("xclbin 0x%llx, size %ld", (uint64_t)axlf, sizeof(struct axlf));
	axlf_head = axlf;
	
	//memcpy(&axlf_head, axlf, sizeof (struct axlf));
	DZ_DEBUG("%s", axlf_head->m_magic);

	if (memcmp(axlf_head->m_magic, "xclbin2", 8)) {
		DRM_INFO("Invalid xclbin magic string.");
		return -EINVAL;
	}

	/* Check unique ID */
	if (axlf_head->m_uniqueId == zdev->unique_id_last_bitstream) {
		DRM_INFO("The XCLBIN already loaded. Don't need to reload.");
		return ret;
	}

	write_lock(&zdev->attr_rwlock);

	/* Get full axlf header */
	size_of_header = sizeof(struct axlf_section_header);
	num_of_sections = axlf_head->m_header.m_numSections-1;
	xclbin = (char __user *)axlf;
	ret = !ZOCL_ACCESS_OK(VERIFY_READ, xclbin, axlf_head->m_header.m_length);
	if (ret) {
		ret = -EFAULT;
		goto out;
	}

	DZ_DEBUG("before loading PDI");
	size = zocl_offsetof_sect(PDI, &pdi_buffer, axlf, xclbin);
	if (size > 0) {
		DZ_DEBUG("size %lld", size);
		ret = zocl_fpga_mgr_load(ddev, pdi_buffer, size);
	}
	zdev->unique_id_last_bitstream = axlf_head->m_uniqueId;

out:
	write_unlock(&zdev->attr_rwlock);

	return ret;
}

int
zocl_read_axlf_ioctl(struct drm_device *ddev, void *data, struct drm_file *filp)
{
	struct drm_zocl_axlf *axlf_obj = data;
	struct drm_zocl_dev *zdev = ddev->dev_private;
	struct axlf axlf_head;
	struct axlf *axlf;
	long axlf_size;
	char __user *xclbin = NULL;
	size_t size_of_header;
	size_t num_of_sections;
	uint64_t size = 0;
	int ret = 0;
	char *pdi_buffer = NULL;

	if (copy_from_user(&axlf_head, axlf_obj->za_xclbin_ptr, sizeof(struct axlf))) {
		DRM_WARN("copy_from_user failed for za_xclbin_ptr");
		return -EFAULT;
	}

	if (memcmp(axlf_head.m_magic, "xclbin2", 8)) {
		DRM_WARN("xclbin magic is invalid %s", axlf_head.m_magic);
		return -EINVAL;
	}

	/* Check unique ID */
	if (axlf_head.m_uniqueId == zdev->unique_id_last_bitstream) {
		DRM_INFO("The XCLBIN already loaded. Don't need to reload.");
		return ret;
	}
	write_lock(&zdev->attr_rwlock);

	zocl_free_sections(zdev);

	/* Get full axlf header */
	size_of_header = sizeof(struct axlf_section_header);
	num_of_sections = axlf_head.m_header.m_numSections-1;
	axlf_size = sizeof(struct axlf) + size_of_header * num_of_sections;
	axlf = vmalloc(axlf_size);
	if (!axlf) {
		write_unlock(&zdev->attr_rwlock);
		return -ENOMEM;
	}

	if (copy_from_user(axlf, axlf_obj->za_xclbin_ptr, axlf_size)) {
		ret = -EFAULT;
		goto out0;
	}

	xclbin = (char __user *)axlf_obj->za_xclbin_ptr;
	ret = !ZOCL_ACCESS_OK(VERIFY_READ, xclbin, axlf_head.m_header.m_length);
	if (ret) {
		ret = -EFAULT;
		goto out0;
	}

	DZ_DEBUG("flags %d", axlf_obj->za_flags);
	if (axlf_obj->za_flags & DRM_ZOCL_AXLF_FLAGS_PDI_LOAD) {
		/* If PDI section is available, load PDI */
		size = zocl_read_sect(PDI, &pdi_buffer, axlf, xclbin);
		if (size > 0) {
			DZ_DEBUG("found PDI size: %lld", size);
			ret = zocl_fpga_mgr_load(ddev, pdi_buffer, size);
			if (ret)
				goto out0;
		} else if (size < 0) {
			goto out0;
		}
	}

	/* Populating IP_LAYOUT sections */
	/* zocl_read_sect return size of section when successfully find it */
	size = zocl_read_sect(IP_LAYOUT, &zdev->ip, axlf, xclbin);
	if (size <= 0) {
		if (size != 0)
			goto out0;
	} else if (sizeof_section(zdev->ip, m_ip_data) != size) {
		ret = -EINVAL;
		goto out0;
	}

	/* Populating DEBUG_IP_LAYOUT sections */
	size = zocl_read_sect(DEBUG_IP_LAYOUT, &zdev->debug_ip, axlf, xclbin);
	if (size <= 0) {
		if (size != 0)
			goto out0;
	} else if (sizeof_section(zdev->debug_ip, m_debug_ip_data) != size) {
		ret = -EINVAL;
		goto out0;
	}

	ret = zocl_update_apertures(zdev);
	if (ret)
		goto out0;

	/* Populating CONNECTIVITY sections */
	size = zocl_read_sect(CONNECTIVITY, &zdev->connectivity, axlf, xclbin);
	if (size <= 0) {
		if (size != 0)
			goto out0;
	} else if (sizeof_section(zdev->connectivity, m_connection) != size) {
		ret = -EINVAL;
		goto out0;
	}

	/* Populating MEM_TOPOLOGY sections */
	size = zocl_read_sect(MEM_TOPOLOGY, &zdev->topology, axlf, xclbin);
	if (size <= 0) {
		if (size != 0)
			goto out0;
	} else if (sizeof_section(zdev->topology, m_mem_data) != size) {
		ret = -EINVAL;
		goto out0;
	}

	zocl_init_mem(zdev, zdev->topology);

	zdev->unique_id_last_bitstream = axlf_head.m_uniqueId;

out0:
	write_unlock(&zdev->attr_rwlock);
	if (size < 0)
		ret = size;
	vfree(axlf);
	return ret;
}

/* IOCTL to get CU index in aperture list
 * used for recognizing BO and CU in mmap
 */
int
zocl_info_cu_ioctl(struct drm_device *dev, void *data, struct drm_file *filp)
{
	struct drm_zocl_info_cu *args = data;
	struct drm_zocl_dev *zdev = dev->dev_private;
	struct sched_exec_core *exec = zdev->exec;

	if (!exec->configured) {
		DRM_ERROR("Schduler is not configured\n");
		return -EINVAL;
	}

	args->apt_idx = get_apt_index(zdev, args->paddr);
	if (args->apt_idx == -EINVAL) {
		DRM_ERROR("Failed to find CU in aperture list 0x%llx\n", args->paddr);
		return -EINVAL;
	}
	return 0;
}
