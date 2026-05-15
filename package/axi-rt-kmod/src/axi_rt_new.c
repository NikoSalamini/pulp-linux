// SPDX-License-Identifier: GPL-2.0
/*
 * AXI-RT Linux Kernel Driver
 *
 * Port of the bare-metal AXI-RT configuration library
 * from ETH Zurich / University of Bologna.
 *
 * This driver exposes helper functions to configure:
 *  - region start/end addresses
 *  - read/write budgets
 *  - periods
 *  - length limits
 *  - enable/disable AXI-RT
 *
 * The interaction with userspace is implemented through sysfs.
 *
 * 
 */

 #include <linux/module.h>
 #include <linux/kernel.h>
 #include <linux/init.h>
 #include <linux/io.h>
 #include <linux/platform_device.h>
 #include <linux/of.h>
 #include <linux/types.h>
 #include <linux/device.h>
 #include <linux/of_address.h>
 #include "axi_rt.h"
 #include "cheshire.h"
 
 /* -------------------------------------------------------------------------- */
 /* Driver private data                                                        */
 /* -------------------------------------------------------------------------- */
 
 struct axirt_dev {
     void __iomem *base;
     phys_addr_t  phys_base;
 
     void __iomem *grd_base;
     phys_addr_t  phys_grd;
 
     uint32_t enable;
 
     uint32_t budget;
     uint32_t period;
 
     uint8_t region_id;
     uint8_t mgr_id;
 };
 
 struct cheshire_dev {
     void __iomem *base_regs;
     phys_addr_t phys_base_regs;
 };
 
 /* -------------------------------------------------------------------------- */
 /* Helper functions                                                           */
 /* -------------------------------------------------------------------------- */
 
 static inline void axirt_write32(struct axirt_dev *axirt,
                                  uint32_t val,
                                  uint32_t offset)
 {
     void __iomem *virt = axirt->base + offset;
     phys_addr_t phys = axirt->phys_base + offset;
 
     iowrite32(val, virt);
 
     pr_info("AXI-RT WRITE virt=%px phys=0x%llx offset=0x%x val=0x%08x\n",
             virt,
             (unsigned long long)phys,
             offset,
             val);
 }
 
 static inline uint32_t axirt_read32(struct axirt_dev *axirt,
                                     uint32_t offset)
 {
     void __iomem *virt = axirt->base + offset;
     phys_addr_t phys = axirt->phys_base + offset;
     uint32_t val;
 
     val = ioread32(virt);
 
     pr_info("AXI-RT READ  virt=%px phys=0x%llx offset=0x%x val=0x%08x\n",
             virt,
             (unsigned long long)phys,
             offset,
             val);
 
     return val;
 }
 
 static inline int axirt_readback32(struct axirt_dev *axirt,
                                    uint32_t expected,
                                    uint32_t offset,
                                    struct device *dev)
 {
     uint32_t val = axirt_read32(axirt, offset);
 
     if (val != expected) {
         dev_err(dev,
                 "AXI-RT READBACK MISMATCH offset=0x%x expected=0x%08x got=0x%08x\n",
                 offset, expected, val);
         return -EIO;
     }
 
     dev_info(dev,
              "AXI-RT READBACK OK offset=0x%x val=0x%08x\n",
              offset, val);
     return 0;
 }
 
 static inline uint32_t axirt_region_offset(uint32_t base, uint8_t region_id, uint8_t mgr_id)
 {
     return base + AXI_RT_PARAM_NUM_SUB * mgr_id * 4 + region_id * 4;
 }
 
 static inline uint32_t cheshire_regs_read32(struct cheshire_dev *cheshire, uint32_t offset)
 {
     void __iomem *virt = cheshire->base_regs + offset;
     phys_addr_t phys = cheshire->phys_base_regs + offset;  
 
     /* reading the value */
     uint32_t val = ioread32(virt);
 
     pr_info("CHESHIRE_REGS READ virt=%p phys=0x%llx offset=0x%x val=0x%08x\n",
             virt,
             (unsigned long long)phys,
             offset,
             val);
     
     return val;
 }
 
 /* -------------------------------------------------------------------------- */
 /* AXI-RT configuration functions                                             */
 /* -------------------------------------------------------------------------- */
 
 void axirt_claim(struct axirt_dev *axirt,
                  bool read_excl,
                  bool write_excl)
 {
     uint8_t flags = 4 | (read_excl << 1) | write_excl;
 
     pr_info("AXI-RT GRD WRITE virt=%px phys=0x%llx val=0x%02x\n",
             axirt->grd_base,
             (unsigned long long)axirt->phys_grd,
             flags);
 
    iowrite8(flags,  axirt->grd_base);
 }
 
 void axirt_release(struct axirt_dev *axirt)
 {
     pr_info("AXI-RT GRD RELEASE virt=%px phys=0x%llx\n",
             axirt->grd_base,
             (unsigned long long)axirt->phys_grd);
 
     iowrite32(0, axirt->grd_base);
 }
 
 void axirt_set_len_limit_group(struct axirt_dev *axirt,
                                uint8_t limit,
                                uint8_t group_id)
 {
     uint32_t all_limit;
     uint32_t reg;
 
     all_limit = limit |
                 (limit << 8) |
                 (limit << 16) |
                 (limit << 24);
 
     reg = AXI_RT_LEN_LIMIT_0_REG_OFFSET + group_id * 4;
 
     axirt_write32(axirt,
                   all_limit,
                   reg);
 
     pr_info("AXI-RT: len_limit group=%u limit=%u reg=0x%x val=0x%08x\n",
             group_id,
             limit,
             reg,
             all_limit);
 }
 
 void axirt_set_region(struct axirt_dev *axirt,
                       uint64_t start_addr,
                       uint64_t end_addr,
                       uint8_t region_id,
                       uint8_t mgr_id)
 {
     axirt_write32(axirt,
         (uint32_t)(end_addr >> 32),
         axirt_region_offset(
             AXI_RT_END_ADDR_SUB_HIGH_0_REG_OFFSET,
             region_id,
             mgr_id));
 
     axirt_write32(axirt,
         (uint32_t)(end_addr & 0xffffffff),
         axirt_region_offset(
             AXI_RT_END_ADDR_SUB_LOW_0_REG_OFFSET,
             region_id,
             mgr_id));
 
     axirt_write32(axirt,
         (uint32_t)(start_addr & 0xffffffff),
         axirt_region_offset(
             AXI_RT_START_ADDR_SUB_LOW_0_REG_OFFSET,
             region_id,
             mgr_id));
 
     axirt_write32(axirt,
         (uint32_t)(start_addr >> 32),
         axirt_region_offset(
             AXI_RT_START_ADDR_SUB_HIGH_0_REG_OFFSET,
             region_id,
             mgr_id));
 
     pr_info("AXI-RT: set_region mgr=%u region=%u start=0x%016llx end=0x%016llx\n",
         mgr_id,
         region_id,
         start_addr,
         end_addr);
 }
 
 void axirt_set_period(struct axirt_dev *axirt,
                       uint32_t period,
                       uint8_t region_id,
                       uint8_t mgr_id)
 {
     axirt_write32(axirt,
         period,
         axirt_region_offset(
             AXI_RT_WRITE_PERIOD_0_REG_OFFSET,
             region_id,
             mgr_id));
 
     axirt_write32(axirt,
         period,
         axirt_region_offset(
             AXI_RT_READ_PERIOD_0_REG_OFFSET,
             region_id,
             mgr_id));
     
     pr_info("AXI-RT: set_period mgr=%u region=%u period=%u\n",
         mgr_id,
         region_id,
         period);
 }
 
 void axirt_set_budget(struct axirt_dev *axirt,
                       uint32_t budget,
                       uint8_t region_id,
                       uint8_t mgr_id)
 {
     axirt_write32(axirt,
         budget,
         axirt_region_offset(
             AXI_RT_WRITE_BUDGET_0_REG_OFFSET,
             region_id,
             mgr_id));
 
     axirt_write32(axirt,
         budget,
         axirt_region_offset(
             AXI_RT_READ_BUDGET_0_REG_OFFSET,
             region_id,
             mgr_id));
     
     pr_info("AXI-RT: set_budget mgr=%u region=%u budget=%u\n",
         mgr_id,
         region_id,
         budget);
 }
 
 void axirt_enable(struct axirt_dev *axirt,
                   uint32_t enable)
 {
     axirt_write32(axirt,
                   enable,
                   AXI_RT_RT_ENABLE_REG_OFFSET);
 
     axirt_write32(axirt,
                   enable,
                   AXI_RT_IMTU_ENABLE_REG_OFFSET);
     
     pr_info("AXI-RT: enable mask=0x%08x\n", enable);
 }
 
 void axirt_disable(struct axirt_dev *axirt)
 {
     axirt_write32(axirt,
                   0,
                   AXI_RT_IMTU_ENABLE_REG_OFFSET);
 
     axirt_write32(axirt,
                   0,
                   AXI_RT_RT_ENABLE_REG_OFFSET);
 }
 
 /* -------------------------------------------------------------------------- */
 /* Readback helpers                                                           */
 /* -------------------------------------------------------------------------- */
 
 static int axirt_readback_len_limit(struct axirt_dev *axirt,
                                     uint8_t limit,
                                     uint8_t group_id,
                                     struct device *dev)
 {
     uint32_t expected = limit | (limit << 8) | (limit << 16) | (limit << 24);
     uint32_t reg = AXI_RT_LEN_LIMIT_0_REG_OFFSET + group_id * 4;
 
     return axirt_readback32(axirt, expected, reg, dev);
 }
 
 static int axirt_readback_region(struct axirt_dev *axirt,
                                  uint64_t start_addr,
                                  uint64_t end_addr,
                                  uint8_t region_id,
                                  uint8_t mgr_id,
                                  struct device *dev)
 {
     int err = 0;
 
     err |= axirt_readback32(axirt,
         (uint32_t)(end_addr >> 32),
         axirt_region_offset(AXI_RT_END_ADDR_SUB_HIGH_0_REG_OFFSET, region_id, mgr_id),
         dev);
 
     err |= axirt_readback32(axirt,
         (uint32_t)(end_addr & 0xffffffff),
         axirt_region_offset(AXI_RT_END_ADDR_SUB_LOW_0_REG_OFFSET, region_id, mgr_id),
         dev);
 
     err |= axirt_readback32(axirt,
         (uint32_t)(start_addr & 0xffffffff),
         axirt_region_offset(AXI_RT_START_ADDR_SUB_LOW_0_REG_OFFSET, region_id, mgr_id),
         dev);
 
     err |= axirt_readback32(axirt,
         (uint32_t)(start_addr >> 32),
         axirt_region_offset(AXI_RT_START_ADDR_SUB_HIGH_0_REG_OFFSET, region_id, mgr_id),
         dev);
 
     if (!err)
         dev_info(dev, "AXI-RT: readback OK region mgr=%u region=%u\n",
                  mgr_id, region_id);
 
     return err;
 }
 
 static int axirt_readback_budget(struct axirt_dev *axirt,
                                  uint32_t budget,
                                  uint8_t region_id,
                                  uint8_t mgr_id,
                                  struct device *dev)
 {
     int err = 0;
 
     err |= axirt_readback32(axirt,
         budget,
         axirt_region_offset(AXI_RT_WRITE_BUDGET_0_REG_OFFSET, region_id, mgr_id),
         dev);
 
     err |= axirt_readback32(axirt,
         budget,
         axirt_region_offset(AXI_RT_READ_BUDGET_0_REG_OFFSET, region_id, mgr_id),
         dev);
 
     if (!err)
         dev_info(dev, "AXI-RT: readback OK budget mgr=%u region=%u\n",
                  mgr_id, region_id);
 
     return err;
 }
 
 static int axirt_readback_period(struct axirt_dev *axirt,
                                  uint32_t period,
                                  uint8_t region_id,
                                  uint8_t mgr_id,
                                  struct device *dev)
 {
     int err = 0;
 
     err |= axirt_readback32(axirt,
         period,
         axirt_region_offset(AXI_RT_WRITE_PERIOD_0_REG_OFFSET, region_id, mgr_id),
         dev);
 
     err |= axirt_readback32(axirt,
         period,
         axirt_region_offset(AXI_RT_READ_PERIOD_0_REG_OFFSET, region_id, mgr_id),
         dev);
 
     if (!err)
         dev_info(dev, "AXI-RT: readback OK period mgr=%u region=%u\n",
                  mgr_id, region_id);
 
     return err;
 }
 
 static int axirt_readback_enable(struct axirt_dev *axirt,
                                  uint32_t enable,
                                  struct device *dev)
 {
     int err = 0;
 
     err |= axirt_readback32(axirt, enable, AXI_RT_RT_ENABLE_REG_OFFSET, dev);
     err |= axirt_readback32(axirt, enable, AXI_RT_IMTU_ENABLE_REG_OFFSET, dev);
 
     if (!err)
         dev_info(dev, "AXI-RT: readback OK enable mask=0x%08x\n", enable);
 
     return err;
 }
 
 /* -------------------------------------------------------------------------- */
 /* Init                                                                       */
 /* -------------------------------------------------------------------------- */
 
 /* Enable bits: bit 0 = CVA6 (mgr 0), bit 4 = ATG (mgr 4) */
 #define AXIRT_ENABLE_MASK 0x11
 #define AXIRT_MGR_CVA6 0
 #define AXIRT_MGR_ATG_OFFSET 3
 
 /* Budget and Period*/
 #define AXIRT_BUDGET_DEFAULT   8
 #define AXIRT_PERIOD_DEFAULT   100
 #define AXIRT_LEN_LIMIT_DEFAULT 2
 #define AXIRT_LEN_LIMIT_GROUP   0
 
 static void init_axi_rt(struct axirt_dev *axirt, struct cheshire_dev *cheshire, struct device *dev)
 {
     int err = 0;
 
     /* Set length limit for group 0 */
     axirt_set_len_limit_group(axirt, AXIRT_LEN_LIMIT_DEFAULT, AXIRT_LEN_LIMIT_GROUP);
     err = axirt_readback_len_limit(axirt, AXIRT_LEN_LIMIT_DEFAULT, AXIRT_LEN_LIMIT_GROUP, dev);
     if (err)
         dev_err(dev, "AXI-RT: readback FAILED for len_limit group=%u\n", AXIRT_LEN_LIMIT_GROUP);
 
     /* Configure CVA6 core (mgr 0) - two subordinate regions */
     axirt_set_region(axirt, 0x0ULL, 0xffffffffULL, 0, AXIRT_MGR_CVA6);
     err = axirt_readback_region(axirt, 0x0ULL, 0xffffffffULL, 0, AXIRT_MGR_CVA6, dev);
     if (err)
         dev_err(dev, "AXI-RT: readback FAILED for region mgr=%u region=0\n", AXIRT_MGR_CVA6);
 
     axirt_set_region(axirt, 0x100000000ULL, 0xffffffffffffffffULL, 1, AXIRT_MGR_CVA6);
     err = axirt_readback_region(axirt, 0x100000000ULL, 0xffffffffffffffffULL, 1, AXIRT_MGR_CVA6, dev);
     if (err)
         dev_err(dev, "AXI-RT: readback FAILED for region mgr=%u region=1\n", AXIRT_MGR_CVA6);
 
     axirt_set_budget(axirt, AXIRT_BUDGET_DEFAULT, 0, AXIRT_MGR_CVA6);
     err = axirt_readback_budget(axirt, AXIRT_BUDGET_DEFAULT, 0, AXIRT_MGR_CVA6, dev);
     if (err)
         dev_err(dev, "AXI-RT: readback FAILED for budget mgr=%u region=0\n", AXIRT_MGR_CVA6);
 
     axirt_set_budget(axirt, AXIRT_BUDGET_DEFAULT, 1, AXIRT_MGR_CVA6);
     err = axirt_readback_budget(axirt, AXIRT_BUDGET_DEFAULT, 1, AXIRT_MGR_CVA6, dev);
     if (err)
         dev_err(dev, "AXI-RT: readback FAILED for budget mgr=%u region=1\n", AXIRT_MGR_CVA6);
 
     axirt_set_period(axirt, AXIRT_PERIOD_DEFAULT, 0, AXIRT_MGR_CVA6);
     err = axirt_readback_period(axirt, AXIRT_PERIOD_DEFAULT, 0, AXIRT_MGR_CVA6, dev);
     if (err)
         dev_err(dev, "AXI-RT: readback FAILED for period mgr=%u region=0\n", AXIRT_MGR_CVA6);
 
     axirt_set_period(axirt, AXIRT_PERIOD_DEFAULT, 1, AXIRT_MGR_CVA6);
     err = axirt_readback_period(axirt, AXIRT_PERIOD_DEFAULT, 1, AXIRT_MGR_CVA6, dev);
     if (err)
         dev_err(dev, "AXI-RT: readback FAILED for period mgr=%u region=1\n", AXIRT_MGR_CVA6);
 
     dev_info(dev, "AXI-RT: configured CVA6 (mgr %d)\n", AXIRT_MGR_CVA6);
 
     /* read ATG id */
     int num_harts = cheshire_regs_read32(cheshire, CHESHIRE_NUM_INT_HARTS_REG_OFFSET);
     int cheshire_atg_id = num_harts + AXIRT_MGR_ATG_OFFSET;
 
     /* Configure ATG - two subordinate regions */
     axirt_set_region(axirt, 0x0ULL, 0xffffffffULL, 0, cheshire_atg_id);
     err = axirt_readback_region(axirt, 0x0ULL, 0xffffffffULL, 0, cheshire_atg_id, dev);
     if (err)
         dev_err(dev, "AXI-RT: readback FAILED for region mgr=%u region=0\n", cheshire_atg_id);
 
     axirt_set_region(axirt, 0x100000000ULL, 0xffffffffffffffffULL, 1, cheshire_atg_id);
     err = axirt_readback_region(axirt, 0x100000000ULL, 0xffffffffffffffffULL, 1, cheshire_atg_id, dev);
     if (err)
         dev_err(dev, "AXI-RT: readback FAILED for region mgr=%u region=1\n", cheshire_atg_id);
 
     axirt_set_budget(axirt, AXIRT_BUDGET_DEFAULT, 0, cheshire_atg_id);
     err = axirt_readback_budget(axirt, AXIRT_BUDGET_DEFAULT, 0, cheshire_atg_id, dev);
     if (err)
         dev_err(dev, "AXI-RT: readback FAILED for budget mgr=%u region=0\n", cheshire_atg_id);
 
     axirt_set_budget(axirt, AXIRT_BUDGET_DEFAULT, 1, cheshire_atg_id);
     err = axirt_readback_budget(axirt, AXIRT_BUDGET_DEFAULT, 1, cheshire_atg_id, dev);
     if (err)
         dev_err(dev, "AXI-RT: readback FAILED for budget mgr=%u region=1\n", cheshire_atg_id);
 
     axirt_set_period(axirt, AXIRT_PERIOD_DEFAULT, 0, cheshire_atg_id);
     err = axirt_readback_period(axirt, AXIRT_PERIOD_DEFAULT, 0, cheshire_atg_id, dev);
     if (err)
         dev_err(dev, "AXI-RT: readback FAILED for period mgr=%u region=0\n", cheshire_atg_id);
 
     axirt_set_period(axirt, AXIRT_PERIOD_DEFAULT, 1, cheshire_atg_id);
     err = axirt_readback_period(axirt, AXIRT_PERIOD_DEFAULT, 1, cheshire_atg_id, dev);
     if (err)
         dev_err(dev, "AXI-RT: readback FAILED for period mgr=%u region=1\n", cheshire_atg_id);
 
     dev_info(dev, "AXI-RT: configured ATG (mgr %d)\n", cheshire_atg_id);
 
     /* Enable RT for CVA6 (bit 0) and ATG (bit 4) */
     axirt->enable = AXIRT_ENABLE_MASK;
     axirt_enable(axirt, AXIRT_ENABLE_MASK);
     err = axirt_readback_enable(axirt, AXIRT_ENABLE_MASK, dev);
     if (err)
         dev_err(dev, "AXI-RT: readback FAILED for enable mask=0x%02x\n", AXIRT_ENABLE_MASK);
 
     dev_info(dev, "AXI-RT: enabled (mask=0x%02x)\n", AXIRT_ENABLE_MASK);
 }
 
 
 /* -------------------------------------------------------------------------- */
 /* AXI-RT Sysfs                                                               */
 /* -------------------------------------------------------------------------- */
 
 /* ############################ LEN LIMIT ############################ */
 
 static ssize_t len_limit_store(struct device *dev,
                                struct device_attribute *attr,
                                const char *buf,
                                size_t count)
 {
     struct axirt_dev *axirt = dev_get_drvdata(dev);
     uint32_t limit, group_id;
     int ret;
 
     ret = sscanf(buf, "%u %u", &limit, &group_id);
     if (ret != 2)
         return -EINVAL;
 
     if (limit > 0xff)
         return -EINVAL;
 
     axirt_set_len_limit_group(axirt, (uint8_t)limit, (uint8_t)group_id);
 
     dev_info(dev,
              "AXI-RT lenlimit config: limit=%u group_id=%u\n",
              limit, group_id);
 
     return count;
 }
 static DEVICE_ATTR_WO(len_limit);
 
 /* ############################ ENABLE ############################ */
 
 static ssize_t enable_show(struct device *dev,
                            struct device_attribute *attr,
                            char *buf)
 {
     struct axirt_dev *axirt = dev_get_drvdata(dev);
 
     return sprintf(buf, "%u\n", axirt->enable);
 }
 
 static ssize_t enable_store(struct device *dev,
                             struct device_attribute *attr,
                             const char *buf,
                             size_t count)
 {
     struct axirt_dev *axirt = dev_get_drvdata(dev);
 
     uint32_t val;
 
     if (kstrtou32(buf, 0, &val))
         return -EINVAL;
 
     axirt->enable = val;
 
     if (val)
         axirt_enable(axirt,val);
     else
         axirt_disable(axirt);
 
     return count;
 }
 
 static DEVICE_ATTR_RW(enable);
 
 /* ############################ BUDGET ############################ */
 
 static ssize_t budget_store(struct device *dev,
                             struct device_attribute *attr,
                             const char *buf,
                             size_t count)
 {
     uint32_t budget;
     uint32_t region_id;
     uint32_t mgr_id;
     struct axirt_dev *axirt = dev_get_drvdata(dev);
 
     int ret;
 
     ret = sscanf(buf, "%u %u %u",
                  &budget,
                  &region_id,
                  &mgr_id);
 
     if (ret != 3)
         return -EINVAL;
 
     axirt_set_budget(axirt,
                      budget,
                      region_id,
                      mgr_id);
 
     return count;
 }
 
 static ssize_t budget_show(struct device *dev,
                            struct device_attribute *attr,
                            char *buf)
 {
     struct axirt_dev *axirt = dev_get_drvdata(dev);
 
     return sysfs_emit(buf, "%u\n", axirt->budget);
 }
 
 static DEVICE_ATTR_RW(budget);
 
 /* ############################ PERIOD ############################ */
 
 static ssize_t period_store(struct device *dev,
                             struct device_attribute *attr,
                             const char *buf,
                             size_t count)
 {
     uint32_t period;
     uint32_t region_id;
     uint32_t mgr_id;
     struct axirt_dev *axirt = dev_get_drvdata(dev);
 
     int ret;
 
     ret = sscanf(buf, "%u %u %u",
                  &period,
                  &region_id,
                  &mgr_id);
 
     if (ret != 3)
         return -EINVAL;
 
     axirt_set_period(axirt,
                     period,
                     region_id,
                     mgr_id);
 
     return count;
 }
 
 static ssize_t period_show(struct device *dev,
                            struct device_attribute *attr,
                            char *buf)
 {
     struct axirt_dev *axirt = dev_get_drvdata(dev);
 
     return sysfs_emit(buf, "%u\n", axirt->period);
 }
 
 static DEVICE_ATTR_RW(period);
 
 /* ############################ REGION ############################ */
 
 static ssize_t region_store(struct device *dev,
                             struct device_attribute *attr,
                             const char *buf,
                             size_t count)
 {
     uint64_t start_addr;
     uint64_t end_addr;
     uint32_t region_id;
     uint32_t mgr_id;
     int ret;
     struct axirt_dev *axirt = dev_get_drvdata(dev);
 
     ret = sscanf(buf,
                  "%llx %llx %u %u",
                  &start_addr,
                  &end_addr,
                  &region_id,
                  &mgr_id);
 
     if (ret != 4)
         return -EINVAL;
 
     dev_info(dev,
              "AXI-RT region config: mgr=%u region=%u start=0x%llx end=0x%llx\n",
              mgr_id, region_id, start_addr, end_addr);
 
     axirt_set_region(axirt,
                      start_addr,
                      end_addr,
                      region_id,
                      mgr_id);
 
     dev_info(dev,
              "AXI-RT region applied successfully\n");
 
     return count;
 }
 
 static DEVICE_ATTR_WO(region);
 
 /* ############################ ATTR REGISTRATION ############################ */
 
 static struct attribute *axirt_attrs[] = {
     &dev_attr_enable.attr,
     &dev_attr_budget.attr,
     &dev_attr_period.attr,
     &dev_attr_region.attr,
     &dev_attr_len_limit.attr,
     NULL, /* terminator */
 };
 
 static const struct attribute_group axirt_group = {
     .attrs = axirt_attrs,
 };
 
 static const struct attribute_group *axirt_groups[] = {
     &axirt_group,
     NULL,
 };
 
 /* -------------------------------------------------------------------------- */
 /* Platform driver                                                            */
 /* -------------------------------------------------------------------------- */
 
 /* ############################ PROBE ############################ */
 static int axirt_probe(struct platform_device *pdev)
 {
     struct resource *res;
     struct resource cheshire_res;
 
     struct device_node *np;
     struct device_node *cheshire_np;
 
     struct axirt_dev *axirt;
     struct cheshire_dev *cheshire;
 
     axirt = devm_kzalloc(&pdev->dev,
                          sizeof(*axirt),
                          GFP_KERNEL);
     if (!axirt)
         return -ENOMEM;
 
     /* ------------------------------------------------------------------ */
     /* AXI-RT                                                             */
     /* ------------------------------------------------------------------ */
 
     res = platform_get_resource(pdev,
                                 IORESOURCE_MEM,
                                 0);
     if (!res)
         return -ENODEV;
 
     axirt->base = devm_ioremap_resource(&pdev->dev,
                                         res);
 
     if (IS_ERR(axirt->base))
         return PTR_ERR(axirt->base);
 
     axirt->phys_base = res->start;
 
    /* ------------------------------------------------------------------ */
    /* AXI-RT Guard register                                              */
    /* ------------------------------------------------------------------ */

    /* The guard register lies within the already-mapped AXI-RT window.   */
    /* Don't re-request the region — derive the virtual address directly  */
    /* from the existing mapping.                                         */
    axirt->grd_base  = axirt->base + AXI_RT_GRD_OFFSET;
    axirt->phys_grd  = axirt->phys_base + AXI_RT_GRD_OFFSET;

    dev_info(&pdev->dev,
             "AXI-RT GRD phys=0x%llx virt=%px\n",
             (u64)axirt->phys_grd,
             axirt->grd_base);
 
    /* ------------------------------------------------------------------ */
    /* Cheshire register block                                            */
    /* ------------------------------------------------------------------ */
 
     cheshire = devm_kzalloc(&pdev->dev,
                             sizeof(*cheshire),
                             GFP_KERNEL);
     if (!cheshire)
         return -ENOMEM;
 
     np = pdev->dev.of_node;
 
     cheshire_np = of_parse_phandle(np,
                                    "cheshire-regs",
                                    0);
 
     if (!cheshire_np) {
         dev_err(&pdev->dev,
                 "missing cheshire-regs phandle\n");
         return -ENODEV;
     }
 
     if (of_address_to_resource(cheshire_np,
                                0,
                                &cheshire_res)) {
         of_node_put(cheshire_np);
         return -ENODEV;
     }
 
     cheshire->base_regs =
         devm_ioremap_resource(&pdev->dev,
                               &cheshire_res);
 
     of_node_put(cheshire_np);
 
     if (IS_ERR(cheshire->base_regs))
         return PTR_ERR(cheshire->base_regs);
 
     cheshire->phys_base_regs =
         cheshire_res.start;
 
     platform_set_drvdata(pdev, axirt);
 
     dev_info(&pdev->dev,
         "AXI-RT phys=0x%llx virt=%px\n",
         (u64)axirt->phys_base,
         axirt->base);
 
     dev_info(&pdev->dev,
             "Cheshire regs phys=0x%llx virt=%px\n",
             (u64)cheshire->phys_base_regs,
             cheshire->base_regs);
 
     /* ------------------------------------------------------------------ */
     /* AXI-RT init                                                        */
     /* ------------------------------------------------------------------ */
 
     axirt_claim(axirt, 1, 1);
     init_axi_rt(axirt, cheshire, &pdev->dev);
 
     return 0;
 }
 
 static int axirt_remove(struct platform_device *pdev)
 {
     struct axirt_dev *axirt = platform_get_drvdata(pdev);  
 
     /* axi rt release */
     axirt_release(axirt);
 
     dev_info(&pdev->dev, "AXI-RT driver removed\n");
     return 0;
 }
 
 /* ############################ MATCH ON THE DEVICE TREE ############################ */
 
 static const struct of_device_id axirt_of_match[] = {
     { .compatible = "eth,axi-rt" },
     { /* sentinel */ }
 };
 MODULE_DEVICE_TABLE(of, axirt_of_match);
 
 static struct platform_driver axirt_driver = {
     .probe  = axirt_probe,
     .remove = axirt_remove,
     .driver = {
         .name           = "axi_rt",
         .of_match_table = axirt_of_match,
         .dev_groups     = axirt_groups,
     },
 };
 
 module_platform_driver(axirt_driver);
 
 /* -------------------------------------------------------------------------- */
 /* Module information                                                         */
 /* -------------------------------------------------------------------------- */
 
 MODULE_LICENSE("Dual MIT/GPL");
 MODULE_AUTHOR("Niko Salamini <niko.salamini@santannapisa.it>");
 MODULE_DESCRIPTION("AXI-RT Linux Kernel Driver");