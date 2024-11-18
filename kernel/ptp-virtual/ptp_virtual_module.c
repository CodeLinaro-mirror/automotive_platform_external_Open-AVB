/* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2017, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*!@file: ptp_virtual.c
 * @brief: Driver functions.
 */


#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/completion.h>
#include <linux/jiffies.h>
#include <linux/iopoll.h>
#include <linux/qcom_scm.h>
#include <linux/soc/qcom/mdt_loader.h>
#include <linux/firmware.h>
#include <linux/of_address.h>
#include <linux/sysfs.h>
#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/dmapool.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/iommu.h>
#include <soc/qcom/boot_stats.h>
#include <soc/qcom/secure_buffer.h>
#include <linux/gunyah/gh_rm_drv.h>
#include <linux/gunyah/gh_vm.h>
#include <linux/qcom-iommu-util.h>
#include <linux/gunyah/gh_dbl.h>
#include <linux/suspend.h>
#include <linux/kthread.h>
#include <linux/timekeeping.h>
#include <linux/moduleparam.h>


#define PTP_REG_BASE                0x23047008
#define PTP_REG_SIZE                0x1000
#define PTP_REG_OFFSET              0x00000008
#define PTP_REG_OFFSET              0x00000008
#define PTP_BUFF_IOVA               0x70000000
#define AC_VM_HLOS                      3
#define GET_PTP_DATA                100
#define SET_PTP_DATA                101


enum vm_variant {
    PVM_ONLY = 1,
    HOSTVM,
    TELEVM,
};

typedef struct __attribute__ ((packed))
{
    bool status;
    int32_t port_status;
    uint32_t tv_sec;
    uint32_t tv_nsec;
}
gptpTimeInfo_t;

struct ptp_client {
    struct ptp_device *ptp_dev;
    gptpTimeInfo_t ptp_data;
};

struct ptp_device {
    struct device *dev;
    enum vm_variant vm_variant;
    struct notifier_block rm_nb;
    uint32_t ptp_status_shm_label;
    struct iommu_domain *domain;
    gptpTimeInfo_t *ptp_buff;
    gh_memparcel_handle_t ptp_buff_mem_handle;
    dev_t ptp_cdev_devid;
    struct cdev ptp_cdev;
    struct class *ptp_class;
    gh_vmid_t televm_vmid;
    dma_addr_t ptp_status_buff_dma;
};

static void __iomem *ptp_base_addr = NULL;


static int ptp_open(struct inode *inode, struct file *filp)
{
    int ret = 0;
    struct ptp_device *ptp_dev = NULL;

    if (!inode || !filp) {
        ret = -EFAULT;
        goto ret;
    }

    ptp_dev = container_of(inode->i_cdev,
                           struct ptp_device, ptp_cdev);

    if (!ptp_dev || !ptp_dev->dev) {
        ret = -EFAULT;
        goto ret;
    }

    filp->private_data = ptp_dev;
    dev_info(ptp_dev->dev, "ptp_open is sucess \r\n");
ret:
    return ret;
}


static long ptp_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int ret = 0;
    int32_t tv_sec = 0;
    struct ptp_device *ptp_dev = NULL;

    if (!filp) {
        ret = -EFAULT;
        goto ret;
    }

    ptp_dev = filp->private_data;

    if (!ptp_dev || !ptp_dev->dev) {
        ret = -EFAULT;
        goto ret;
    }

    if (!ptp_dev->ptp_buff) {
        dev_err(ptp_dev->dev, "ptp_buff is NULL \r\n");
        ret = -EFAULT;
        goto ret;
    }

    switch (cmd) {
        case GET_PTP_DATA:
            if (ptp_base_addr) {
                /* handle the wrap around scenario */
                do {
                    /* Reading PTP time in Sec from register */
                    tv_sec = readl_relaxed(ptp_base_addr);
                    /* Reading PTP time in nSec from register */
                    ptp_dev->ptp_buff->tv_nsec = readl_relaxed((char *)ptp_base_addr + sizeof(
                                                     uint32_t));
                    /* Reading PTP time in Sec from register */
                    ptp_dev->ptp_buff->tv_sec = readl_relaxed(ptp_base_addr);
                } while (tv_sec != ptp_dev->ptp_buff->tv_sec);
            } else {
                dev_err(ptp_dev->dev, "PTP register adress is NULL\r\n");
            }

            if (copy_to_user((void __user *)arg, ptp_dev->ptp_buff,
                             sizeof(gptpTimeInfo_t))) {
                dev_err(ptp_dev->dev, "Failed to copy_to_user \r\n");
                ret = -EFAULT;
            }

            break;

        case SET_PTP_DATA:
            if (copy_from_user(ptp_dev->ptp_buff, (void __user *) arg,
                               sizeof(gptpTimeInfo_t))) {
                dev_err(ptp_dev->dev, "Failed to copy_from_user \r\n");
                ret = -EFAULT;
            }

            break;

        default:
            break;
    }

ret:
    return ret;
}

static long ptp_compact_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    return ptp_ioctl(filp,cmd,arg);
}

static const struct file_operations ptp_fileops = {
    .open = ptp_open,
    .unlocked_ioctl = ptp_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl = ptp_compact_ioctl,
#endif
    .owner = THIS_MODULE,
};


static int read_shm_labels(struct ptp_device *ptp_dev)
{
    int ret = 0;
    struct device_node *node;

    if (!ptp_dev || !ptp_dev->dev) {
        ret = -EINVAL;
        goto ret;
    }

    node = ptp_dev->dev->of_node;

    if (!node) {
        dev_err(ptp_dev->dev, "qcom,ptp read_shm_labels node is NULL\n");
        ret = -EINVAL;
        goto ret;
    }

    ret = of_property_read_u32(node, "qcom,ptp-status-buff-shm-label",
                               &ptp_dev->ptp_status_shm_label);

    if (ret) {
        dev_err(ptp_dev->dev, "qcom,ptp-status-buff-shm-label not defined\n");
        ret = -EINVAL;
    }

ret:
    return ret;
}

static int hyp_assign_mem_share(struct ptp_device *ptp_dev,
                                struct gh_acl_desc *ptp_acl_desc,
                                struct gh_sgl_desc *ptp_sgl_desc, dma_addr_t dma_addr,
                                uint32_t size, uint32_t label, gh_memparcel_handle_t *handle)
{
    int ret = 0;
    int srcVMperm[1] = {PERM_READ | PERM_WRITE};
    int destVMperm[2] = {PERM_READ | PERM_WRITE, PERM_READ | PERM_WRITE};
    int srcVM[1] = {AC_VM_HLOS};
    int destVM[2] = {AC_VM_HLOS, 0};

    if (!ptp_dev || !ptp_dev->dev) {
        ret = -EINVAL;
        goto ret;
    }

    if (!ptp_acl_desc || !ptp_sgl_desc || !handle) {
        dev_err(ptp_dev->dev,
                "hyp_assign_mem_share has null arguments\n");
        ret = -EINVAL;
        goto ret;
    }

    destVM[1] = ptp_dev->televm_vmid;
    ptp_sgl_desc->n_sgl_entries = 1;
    ptp_sgl_desc->sgl_entries[0].ipa_base = dma_addr;
    ptp_sgl_desc->sgl_entries[0].size = size;
    ret = hyp_assign_phys(dma_addr, size, srcVM, 1, destVM, destVMperm, 2);

    if (ret) {
        dev_err(ptp_dev->dev,
                "Couldnt hyp_assign ptp status buffers from hostvm to televm\n");
        goto ret;
    }

    ret = gh_rm_mem_share(GH_RM_MEM_TYPE_NORMAL, 0, label, ptp_acl_desc,
                          ptp_sgl_desc,
                          NULL, handle);

    if (ret) {
        dev_err(ptp_dev->dev,
                "mem_share of ptp status buffers from hostvm to televm failed\n");
        goto mem_share_fail;
    }

    goto ret;
mem_share_fail:
    hyp_assign_phys(dma_addr, size, destVM, 2, srcVM, srcVMperm, 1);
ret:
    return ret;
}


static int ptp_hostvm_mem_share(struct ptp_device *ptp_dev)
{
    int ret = 0;
    struct gh_acl_desc *ptp_acl_desc;
    struct gh_sgl_desc *ptp_sgl_desc;
    int srcVMperm[1] = {PERM_READ | PERM_WRITE};
    int srcVM[1] = {AC_VM_HLOS};
    int destVM[2] = {AC_VM_HLOS, 0};

    if (!ptp_dev || !ptp_dev->dev) {
        ret = -EINVAL;
        goto acl_alloc_fail;
    }

    destVM[1] = ptp_dev->televm_vmid;
    ptp_dev->ptp_buff_mem_handle = 0;
    ptp_acl_desc = kzalloc(offsetof(struct gh_acl_desc, acl_entries[2]),
                           GFP_KERNEL);

    if (!ptp_acl_desc) {
        ret = -ENOMEM;
        goto acl_alloc_fail;
    }

    ptp_sgl_desc = kzalloc(offsetof(struct gh_sgl_desc, sgl_entries[1]),
                           GFP_KERNEL);

    if (!ptp_sgl_desc) {
        ret = -ENOMEM;
        goto sgl_alloc_fail;
    }

    ptp_acl_desc->n_acl_entries = 2;
    ptp_acl_desc->acl_entries[0].vmid = AC_VM_HLOS;
    ptp_acl_desc->acl_entries[0].perms = GH_RM_ACL_R | GH_RM_ACL_W;
    ptp_acl_desc->acl_entries[1].vmid = ptp_dev->televm_vmid;
    ptp_acl_desc->acl_entries[1].perms = GH_RM_ACL_R | GH_RM_ACL_W;
    /* Share ptp buffer from hostvm to televm */
    ret = hyp_assign_mem_share(ptp_dev, ptp_acl_desc, ptp_sgl_desc,
                               ptp_dev->ptp_status_buff_dma,
                               round_up(sizeof(gptpTimeInfo_t), PAGE_SIZE),
                               ptp_dev->ptp_status_shm_label,
                               &ptp_dev->ptp_buff_mem_handle);

    if (ret) {
        goto mem_share_status_buff_fail;
    } else {
        goto free_mem;
    }

mem_share_status_buff_fail:
    gh_rm_mem_reclaim(ptp_dev->ptp_buff_mem_handle, 0);
    hyp_assign_phys(ptp_dev->ptp_status_buff_dma,
                    round_up(sizeof(gptpTimeInfo_t), PAGE_SIZE),
                    destVM, 2, srcVM, srcVMperm, 1);
free_mem:
    kfree(ptp_sgl_desc);
sgl_alloc_fail:
    kfree(ptp_acl_desc);
acl_alloc_fail:
    return ret;
}

static int hyp_unassign_mem_reclaim(struct ptp_device *ptp_dev,
                                    dma_addr_t dma_addr, uint32_t label,
                                    uint32_t size, gh_memparcel_handle_t handle)
{
    int ret = 0;
    int srcVMperm[1] = {PERM_READ | PERM_WRITE};
    int srcVM[2] = {AC_VM_HLOS, 0};
    int destVM[1] = {AC_VM_HLOS};

    if (!ptp_dev || !ptp_dev->dev) {
        ret = -EINVAL;
        goto ret;
    }

    srcVM[1] = ptp_dev->televm_vmid;
    ret = gh_rm_mem_reclaim(handle, 0);

    if (ret) {
        dev_err(ptp_dev->dev, "mem reclaim for label %u failed with ret = %d\n", label,
                ret);
    } else {
        ret = hyp_assign_phys(dma_addr, size, srcVM, 2, destVM, srcVMperm, 1);
    }

ret:
    return ret;
}


static void ptp_hostvm_unshare_mem(struct ptp_device *ptp_dev)
{
    int ret = 0;

    if (!ptp_dev || !ptp_dev->dev) {
        ret = -EINVAL;
        return;
    }

    ret = hyp_unassign_mem_reclaim(ptp_dev, ptp_dev->ptp_status_buff_dma,
                                   ptp_dev->ptp_status_shm_label,
                                   round_up(sizeof(gptpTimeInfo_t), PAGE_SIZE),
                                   ptp_dev->ptp_buff_mem_handle);

    if (ret)
        dev_err(ptp_dev->dev,
                "hyp_unassign_mem_reclaim failed for label %u with ret = %d\n",
                ptp_dev->ptp_status_shm_label, ret);
}


static int qcom_ptp_rm_cb(struct notifier_block *nb, unsigned long cmd,
                          void *data)
{
    int ret = 0;
    struct gh_rm_notif_vm_status_payload *vm_status_payload;
    struct ptp_device *ptp_dev;
    gh_vmid_t vmid;

    if (!nb || !data) {
        ret = -EINVAL;
        goto ret;
    }

    ptp_dev = container_of(nb, struct ptp_device, rm_nb);

    if (!ptp_dev || !ptp_dev->dev) {
        ret = -EINVAL;
        goto ret;
    }

    vm_status_payload = data;
    ret = gh_rm_get_vmid(GH_TELE_VM, &vmid);

    if (ret) {
        dev_err(ptp_dev->dev, "gh_rm_get_vmid failed\n");
        goto ret;
    }

    if (vm_status_payload->vmid == vmid && cmd == GH_VM_BEFORE_POWERUP) {
        ptp_dev->televm_vmid = vmid;
        ret = read_shm_labels(ptp_dev);

        if (ret) {
            dev_err(ptp_dev->dev, "read_shm_labels failed\n");
            goto ret;
        }

        ret = ptp_hostvm_mem_share(ptp_dev);

        if (ret) {
            dev_err(ptp_dev->dev, "ptp_hostvm_mem_share failed\n");
            goto ret;
        }
    } else if (vm_status_payload->vmid == ptp_dev->televm_vmid
               && cmd == GH_VM_POWEROFF) {
        ptp_hostvm_unshare_mem(ptp_dev);
    }

    return NOTIFY_DONE;
ret:
    return ret;
}

static int ptp_televm_map_shared_mem(struct ptp_device *ptp_dev, char *compat,
                                     uint32_t shm_label)
{
    struct device_node *np = NULL, *shm_np;
    struct resource res;
    uint32_t label;
    int ret = 0;

    if (!ptp_dev || !ptp_dev->dev) {
        ret = -EINVAL;
        goto err;
    }

    while ((np = of_find_compatible_node(np, NULL, compat))) {
        ret = of_property_read_u32(np, "qcom,label", &label);

        if (ret) {
            of_node_put(np);
            continue;
        }

        if (label == shm_label) {
            break;
        }

        of_node_put(np);
    }

    if (!np) {
        ret = -EINVAL;
        goto err;
    }

    shm_np = of_parse_phandle(np, "memory-region", 0);

    if (!shm_np) {
        dev_err(ptp_dev->dev, "can't parse ptp-shm node for ptp buffers\n");
        ret = -EINVAL;
        goto put_np;
    }

    ret = of_address_to_resource(shm_np, 0, &res);

    if (ret) {
        dev_err(ptp_dev->dev, "of_address_to_resource of ptp buffers failed\n");
        ret = -EINVAL;
        goto put_shm_np;
    }

    if (label == ptp_dev->ptp_status_shm_label) {
        ptp_dev->ptp_buff = devm_ioremap_wc(ptp_dev->dev, res.start,
                                            resource_size(&res));

        if (IS_ERR(ptp_dev->ptp_buff)) {
            ret = -ENOMEM;
            dev_err(ptp_dev->dev, "ioremap of ptp status buffers failed\n");
            goto ioremap_ptp_status_buff_fail;
        }

        dev_info(ptp_dev->dev, "ioremap of ptp status buffers created\n");
    }

    goto put_shm_np;
ioremap_ptp_status_buff_fail:
    devm_iounmap(ptp_dev->dev, ptp_dev->ptp_buff);
put_shm_np:
    of_node_put(shm_np);
put_np:
    of_node_put(np);
err:
    return ret;
}


static void ptp_dma_mem_free(struct ptp_device *ptp_dev)
{
    if (!ptp_dev || !ptp_dev->dev) {
        return;
    }

    dma_free_coherent(ptp_dev->dev, round_up(sizeof(gptpTimeInfo_t),
                      PAGE_SIZE),
                      ptp_dev->ptp_buff, ptp_dev->ptp_status_buff_dma);
}


static int ptp_dma_mem_alloc(struct ptp_device *ptp_dev)
{
    int ret = 0;

    if (!ptp_dev || !ptp_dev->dev) {
        ret = -EINVAL;
        goto ret;
    }

    ptp_dev->ptp_buff = dma_alloc_coherent(ptp_dev->dev,
                                           round_up(sizeof(gptpTimeInfo_t), PAGE_SIZE),
                                           &ptp_dev->ptp_status_buff_dma, GFP_KERNEL);

    if (!ptp_dev->ptp_buff) {
        dev_err(ptp_dev->dev, "dma_alloc_coherent of ptp buffers failed\n");
        ret = -ENOMEM;
    }

ret:
    return ret;
}


static int ptp_virtual_probe(struct platform_device *pdev)
{
    int ret = 0;
    struct device_node *dev_node = NULL;
    struct ptp_device *ptp_dev;

    if (!pdev) {
        return -EINVAL;
    }

    dev_node = pdev->dev.of_node;
    ptp_dev = devm_kzalloc(&pdev->dev, sizeof(*ptp_dev), GFP_KERNEL);

    if (!ptp_dev) {
        return -ENOMEM;
    }

    ptp_dev->dev = &pdev->dev;
    platform_set_drvdata(pdev, ptp_dev);
    ret = of_property_read_u32(dev_node, "qcom,vm-variant", &ptp_dev->vm_variant);

    if (ret) {
        dev_err(ptp_dev->dev, "qcom,vm_variant property not defined\n");
    }

    if (ptp_dev->vm_variant == HOSTVM || ptp_dev->vm_variant == TELEVM) {
        struct device *dev;
        ret = alloc_chrdev_region(&ptp_dev->ptp_cdev_devid, 0, 1, "gptp");

        if (ret < 0) {
            dev_err(ptp_dev->dev,
                    "can't allocate major number, %d\n", ret);
            goto drv_err;
        }

        cdev_init(&ptp_dev->ptp_cdev, &ptp_fileops);
        cdev_add(&ptp_dev->ptp_cdev, ptp_dev->ptp_cdev_devid, 1);
        ptp_dev->ptp_class = class_create(THIS_MODULE, "gptp");

        if (IS_ERR(ptp_dev->ptp_class)) {
            dev_err(ptp_dev->dev,
                    "can't create rmt_sys_evt class, %d\n",
                    -ENOMEM)    ;
            goto class_fail;
        }

        dev = device_create(ptp_dev->ptp_class, &pdev->dev,
                            ptp_dev->ptp_cdev_devid, ptp_dev,
                            "gptp");

        if (IS_ERR(dev)) {
            dev_err(ptp_dev->dev,
                    "can't create rmt_sys_evt device, %d\n",
                    -ENOMEM);
            goto device_fail;
        }

        dev_info(ptp_dev->dev, "ptp character device driver created\n");
    }

    if (ptp_dev->vm_variant == HOSTVM) {
        ret = ptp_dma_mem_alloc(ptp_dev);

        if (ret) {
            dev_err(ptp_dev->dev, " ptp_dma_mem_alloc for PTP failed.\r\n");
            goto dma_mem_fail;
        }

        ptp_dev->rm_nb.notifier_call = qcom_ptp_rm_cb;
        ptp_dev->rm_nb.priority = INT_MAX;
#ifdef CONFIG_GUNYAH
        gh_register_vm_notifier(&ptp_dev->rm_nb);
#endif /* CONFIG_GUNYAH */
    } else if (ptp_dev->vm_variant == TELEVM) {
        ptp_base_addr = ioremap(PTP_REG_BASE, sizeof(uint32_t) * 2);

        if (!ptp_base_addr) {
            dev_err(ptp_dev->dev, " ioremap for PTP failed .\r\n");
            return ret;
        }

        ret = read_shm_labels(ptp_dev);

        if (ret) {
            dev_err(ptp_dev->dev, "read_shm_labels failed\n");
            goto shm_label_fail;
        }

        ret = ptp_televm_map_shared_mem(ptp_dev, "ptp-status-buff-shm",
                                        ptp_dev->ptp_status_shm_label);

        if (ret) {
            dev_err(ptp_dev->dev, "Couldnt map shared mem from hostvm to televm\n");
            ret = -ENOMEM;
            goto shm_label_fail;
        }
    }

    return 0;
shm_label_fail:

    if (ptp_dev->vm_variant == TELEVM) {
        iounmap(ptp_base_addr);
    }

    if (ptp_dev->vm_variant == HOSTVM) {
        ptp_dma_mem_free(ptp_dev);
    }

dma_mem_fail:
    device_destroy(ptp_dev->ptp_class, ptp_dev->ptp_cdev_devid);

    if (ptp_dev->vm_variant == HOSTVM  || ptp_dev->vm_variant == TELEVM) {
device_fail:
        class_destroy(ptp_dev->ptp_class);
class_fail:
        cdev_del(&ptp_dev->ptp_cdev);
        unregister_chrdev_region(ptp_dev->ptp_cdev_devid, 1);
    }

drv_err:
    platform_set_drvdata(pdev, NULL);
    return ret;
}

static int ptp_virtual_remove(struct platform_device *pdev)
{
    int ret = 0;
    struct ptp_device *ptp_dev = NULL;

    if (!pdev) {
        return -EINVAL;
    }

    ptp_dev = dev_get_drvdata(&pdev->dev);

    if (!ptp_dev) {
        return -ENOMEM;
    }

    if (ptp_dev->vm_variant == HOSTVM) {
        ptp_hostvm_unshare_mem(ptp_dev);
        ptp_dma_mem_free(ptp_dev);
    }

    device_destroy(ptp_dev->ptp_class, ptp_dev->ptp_cdev_devid);
    class_destroy(ptp_dev->ptp_class);
    cdev_del(&ptp_dev->ptp_cdev);
    unregister_chrdev_region(ptp_dev->ptp_cdev_devid, 1);
    dev_info(ptp_dev->dev, "Exit: %s\n", __func__);
    return ret;
}

static struct of_device_id ptp_virtual_match[] = {
    {
        .compatible = "qcom,ptp_virtual",
    },
    {}
};

MODULE_DEVICE_TABLE(of, ptp_virtual_match);

static struct platform_driver ptp_virtual_driver = {
    .probe  = ptp_virtual_probe,
    .remove = ptp_virtual_remove,
    .driver = {
        .name       = "ptp_virtual",
        .owner      = THIS_MODULE,
        .of_match_table = ptp_virtual_match,
    },
};


static int __init ptp_virtual_init(void)
{
    return (platform_driver_register(&ptp_virtual_driver));
}

static void __exit ptp_virtual_exit(void)
{
    platform_driver_unregister(&ptp_virtual_driver);
}

module_init(ptp_virtual_init);
module_exit(ptp_virtual_exit);
MODULE_LICENSE("GPL v2");
