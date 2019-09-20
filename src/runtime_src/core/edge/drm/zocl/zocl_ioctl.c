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

#include "sched_exec.h"
#include "zocl_xclbin.h"

int
zocl_read_axlf_ioctl(struct drm_device *ddev, void *data, struct drm_file *filp)
{
	struct drm_zocl_axlf *axlf_obj = data;
	struct drm_zocl_dev *zdev = ZOCL_GET_ZDEV(ddev);
	int ret;

	mutex_lock(&zdev->zdev_xclbin_lock);
	ret = zocl_xclbin_read_axlf(zdev, axlf_obj);
	mutex_unlock(&zdev->zdev_xclbin_lock);

	return ret;
}

int
zocl_ctx_ioctl(struct drm_device *ddev, void *data, struct drm_file *filp)
{
	struct drm_zocl_ctx *args = data;
	struct drm_zocl_dev *zdev = ZOCL_GET_ZDEV(ddev);
	int ret = 0;

	mutex_lock(&zdev->zdev_xclbin_lock);
	ret = zocl_xclbin_ctx(zdev, args, filp->driver_priv);
	mutex_unlock(&zdev->zdev_xclbin_lock);

	return ret;
}

/* IOCTL to get CU index in aperture list
 * used for recognizing BO and CU in mmap
 */
int
zocl_info_cu_ioctl(struct drm_device *ddev, void *data, struct drm_file *filp)
{
	struct drm_zocl_info_cu *args = data;
	struct drm_zocl_dev *zdev = ddev->dev_private;
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
