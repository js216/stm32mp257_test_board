// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file ddr_init.c
 * @brief LPDDR4 bring-up for the STM32MP257F-DK (no device tree).
 * @copyright 2026 Jakob Kastelic
 *
 * Replaces TF-A's drivers/st/ddr/stm32mp2_ram.c: the DDRCTRL/PHY parameter
 * set is compiled in (stm32mp25-lpddr4-1x32Gbits-1x32bits-1200MHz.h, the
 * same values TF-A reads from the device tree), and the setup sequence is
 * the cold-boot path of stm32mp2_ddr_setup() with the standby/self-refresh
 * handling dropped. The PMIC rails are already up (pmic_init_rails()).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <common/debug.h>
#include <drivers/st/stm32mp_ddr.h>
#include <drivers/st/stm32mp_ddr_test.h>
#include <drivers/st/stm32mp2_ddr.h>
#include <drivers/st/stm32mp2_ddr_helpers.h>
#include <lib/mmio.h>
#include <platform_def.h>

#include "ddr.h"
#include "rcc.h"
#include "stm32mp25-lpddr4-1x32Gbits-1x32bits-1200MHz.h"

/* RISAB5 guards RETRAM; let secure accesses through to non-secure pages so
 * the PHY-init code can keep its register save area there (TF-A does the
 * same in bl2_plat_setup.c). */
#define RISAB5_BASE     0x42130000UL
#define RISAB_CR        0x00UL
#define RISAB_CR_SRWIAD BIT(31)

uintptr_t stm32mp_rcc_base(void)
{
   return 0x44200000UL;
}

uintptr_t stm32mp_pwr_base(void)
{
   return 0x44210000UL;
}

uintptr_t stm32mp_ddrctrl_base(void)
{
   return DDRCTRL_BASE;
}

uintptr_t stm32mp_ddrphyc_base(void)
{
   return DDRPHYC_BASE;
}

uintptr_t stm32_ddrdbg_get_base(void)
{
   return DDRDBG_BASE;
}

int stm32mp_board_ddr_power_init(enum ddr_type ddr_type)
{
   /* vdd1_ddr/vdd2_ddr were sequenced on by pmic_init_rails() at boot. */
   (void)ddr_type;
   return 0;
}

void __dead2_panic(void)
{
   my_printf("PANIC\r\n");
   while (1) {
   }
}

static struct stm32mp_ddr_priv ddr_priv;

static struct stm32mp_ddr_config ddr_config = {
   .info = {
      .name  = DDR_MEM_NAME,
      .speed = DDR_MEM_SPEED,
      .size  = DDR_MEM_SIZE,
   },
   .self_refresh = false,
   .zdata        = 0,
   .c_reg = {
      .mstr = DDR_MSTR,
      .mrctrl0 = DDR_MRCTRL0,
      .mrctrl1 = DDR_MRCTRL1,
      .mrctrl2 = DDR_MRCTRL2,
      .derateen = DDR_DERATEEN,
      .derateint = DDR_DERATEINT,
      .deratectl = DDR_DERATECTL,
      .pwrctl = DDR_PWRCTL,
      .pwrtmg = DDR_PWRTMG,
      .hwlpctl = DDR_HWLPCTL,
      .rfshctl0 = DDR_RFSHCTL0,
      .rfshctl1 = DDR_RFSHCTL1,
      .rfshctl3 = DDR_RFSHCTL3,
      .crcparctl0 = DDR_CRCPARCTL0,
      .crcparctl1 = DDR_CRCPARCTL1,
      .init0 = DDR_INIT0,
      .init1 = DDR_INIT1,
      .init2 = DDR_INIT2,
      .init3 = DDR_INIT3,
      .init4 = DDR_INIT4,
      .init5 = DDR_INIT5,
      .init6 = DDR_INIT6,
      .init7 = DDR_INIT7,
      .dimmctl = DDR_DIMMCTL,
      .rankctl = DDR_RANKCTL,
      .rankctl1 = DDR_RANKCTL1,
      .zqctl0 = DDR_ZQCTL0,
      .zqctl1 = DDR_ZQCTL1,
      .zqctl2 = DDR_ZQCTL2,
      .dfitmg0 = DDR_DFITMG0,
      .dfitmg1 = DDR_DFITMG1,
      .dfilpcfg0 = DDR_DFILPCFG0,
      .dfilpcfg1 = DDR_DFILPCFG1,
      .dfiupd0 = DDR_DFIUPD0,
      .dfiupd1 = DDR_DFIUPD1,
      .dfiupd2 = DDR_DFIUPD2,
      .dfimisc = DDR_DFIMISC,
      .dfitmg2 = DDR_DFITMG2,
      .dfitmg3 = DDR_DFITMG3,
      .dbictl = DDR_DBICTL,
      .dfiphymstr = DDR_DFIPHYMSTR,
      .dbg0 = DDR_DBG0,
      .dbg1 = DDR_DBG1,
      .dbgcmd = DDR_DBGCMD,
      .swctl = DDR_SWCTL,
      .swctlstatic = DDR_SWCTLSTATIC,
      .poisoncfg = DDR_POISONCFG,
      .pccfg = DDR_PCCFG,
   },
   .c_timing = {
      .rfshtmg = DDR_RFSHTMG,
      .rfshtmg1 = DDR_RFSHTMG1,
      .dramtmg0 = DDR_DRAMTMG0,
      .dramtmg1 = DDR_DRAMTMG1,
      .dramtmg2 = DDR_DRAMTMG2,
      .dramtmg3 = DDR_DRAMTMG3,
      .dramtmg4 = DDR_DRAMTMG4,
      .dramtmg5 = DDR_DRAMTMG5,
      .dramtmg6 = DDR_DRAMTMG6,
      .dramtmg7 = DDR_DRAMTMG7,
      .dramtmg8 = DDR_DRAMTMG8,
      .dramtmg9 = DDR_DRAMTMG9,
      .dramtmg10 = DDR_DRAMTMG10,
      .dramtmg11 = DDR_DRAMTMG11,
      .dramtmg12 = DDR_DRAMTMG12,
      .dramtmg13 = DDR_DRAMTMG13,
      .dramtmg14 = DDR_DRAMTMG14,
      .dramtmg15 = DDR_DRAMTMG15,
      .odtcfg = DDR_ODTCFG,
      .odtmap = DDR_ODTMAP,
   },
   .c_map = {
      .addrmap0 = DDR_ADDRMAP0,
      .addrmap1 = DDR_ADDRMAP1,
      .addrmap2 = DDR_ADDRMAP2,
      .addrmap3 = DDR_ADDRMAP3,
      .addrmap4 = DDR_ADDRMAP4,
      .addrmap5 = DDR_ADDRMAP5,
      .addrmap6 = DDR_ADDRMAP6,
      .addrmap7 = DDR_ADDRMAP7,
      .addrmap8 = DDR_ADDRMAP8,
      .addrmap9 = DDR_ADDRMAP9,
      .addrmap10 = DDR_ADDRMAP10,
      .addrmap11 = DDR_ADDRMAP11,
   },
   .c_perf = {
      .sched = DDR_SCHED,
      .sched1 = DDR_SCHED1,
      .perfhpr1 = DDR_PERFHPR1,
      .perflpr1 = DDR_PERFLPR1,
      .perfwr1 = DDR_PERFWR1,
      .sched3 = DDR_SCHED3,
      .sched4 = DDR_SCHED4,
      .pcfgr_0 = DDR_PCFGR_0,
      .pcfgw_0 = DDR_PCFGW_0,
      .pctrl_0 = DDR_PCTRL_0,
      .pcfgqos0_0 = DDR_PCFGQOS0_0,
      .pcfgqos1_0 = DDR_PCFGQOS1_0,
      .pcfgwqos0_0 = DDR_PCFGWQOS0_0,
      .pcfgwqos1_0 = DDR_PCFGWQOS1_0,
      .pcfgr_1 = DDR_PCFGR_1,
      .pcfgw_1 = DDR_PCFGW_1,
      .pctrl_1 = DDR_PCTRL_1,
      .pcfgqos0_1 = DDR_PCFGQOS0_1,
      .pcfgqos1_1 = DDR_PCFGQOS1_1,
      .pcfgwqos0_1 = DDR_PCFGWQOS0_1,
      .pcfgwqos1_1 = DDR_PCFGWQOS1_1,
   },
   .uib = {
      .dramtype = DDR_UIB_DRAMTYPE,
      .dimmtype = DDR_UIB_DIMMTYPE,
      .lp4xmode = DDR_UIB_LP4XMODE,
      .numdbyte = DDR_UIB_NUMDBYTE,
      .numactivedbytedfi0 = DDR_UIB_NUMACTIVEDBYTEDFI0,
      .numactivedbytedfi1 = DDR_UIB_NUMACTIVEDBYTEDFI1,
      .numanib = DDR_UIB_NUMANIB,
      .numrank_dfi0 = DDR_UIB_NUMRANK_DFI0,
      .numrank_dfi1 = DDR_UIB_NUMRANK_DFI1,
      .dramdatawidth = DDR_UIB_DRAMDATAWIDTH,
      .numpstates = DDR_UIB_NUMPSTATES,
      .frequency = DDR_UIB_FREQUENCY_0,
      .pllbypass = DDR_UIB_PLLBYPASS_0,
      .dfifreqratio = DDR_UIB_DFIFREQRATIO_0,
      .dfi1exists = DDR_UIB_DFI1EXISTS,
      .train2d = DDR_UIB_TRAIN2D,
      .hardmacrover = DDR_UIB_HARDMACROVER,
      .readdbienable = DDR_UIB_READDBIENABLE_0,
      .dfimode = DDR_UIB_DFIMODE,
   },
   .uia = {
      .lp4rxpreamblemode = DDR_UIA_LP4RXPREAMBLEMODE_0,
      .lp4postambleext = DDR_UIA_LP4POSTAMBLEEXT_0,
      .d4rxpreamblelength = DDR_UIA_D4RXPREAMBLELENGTH_0,
      .d4txpreamblelength = DDR_UIA_D4TXPREAMBLELENGTH_0,
      .extcalresval = DDR_UIA_EXTCALRESVAL,
      .is2ttiming = DDR_UIA_IS2TTIMING_0,
      .odtimpedance = DDR_UIA_ODTIMPEDANCE_0,
      .tximpedance = DDR_UIA_TXIMPEDANCE_0,
      .atximpedance = DDR_UIA_ATXIMPEDANCE,
      .memalerten = DDR_UIA_MEMALERTEN,
      .memalertpuimp = DDR_UIA_MEMALERTPUIMP,
      .memalertvreflevel = DDR_UIA_MEMALERTVREFLEVEL,
      .memalertsyncbypass = DDR_UIA_MEMALERTSYNCBYPASS,
      .disdynadrtri = DDR_UIA_DISDYNADRTRI_0,
      .phymstrtraininterval = DDR_UIA_PHYMSTRTRAININTERVAL_0,
      .phymstrmaxreqtoack = DDR_UIA_PHYMSTRMAXREQTOACK_0,
      .wdqsext = DDR_UIA_WDQSEXT,
      .calinterval = DDR_UIA_CALINTERVAL,
      .calonce = DDR_UIA_CALONCE,
      .lp4rl = DDR_UIA_LP4RL_0,
      .lp4wl = DDR_UIA_LP4WL_0,
      .lp4wls = DDR_UIA_LP4WLS_0,
      .lp4dbird = DDR_UIA_LP4DBIRD_0,
      .lp4dbiwr = DDR_UIA_LP4DBIWR_0,
      .lp4nwr = DDR_UIA_LP4NWR_0,
      .lp4lowpowerdrv = DDR_UIA_LP4LOWPOWERDRV,
      .drambyteswap = DDR_UIA_DRAMBYTESWAP,
      .rxenbackoff = DDR_UIA_RXENBACKOFF,
      .trainsequencectrl = DDR_UIA_TRAINSEQUENCECTRL,
      .snpsumctlopt = DDR_UIA_SNPSUMCTLOPT,
      .snpsumctlf0rc5x = DDR_UIA_SNPSUMCTLF0RC5X_0,
      .txslewrisedq = DDR_UIA_TXSLEWRISEDQ_0,
      .txslewfalldq = DDR_UIA_TXSLEWFALLDQ_0,
      .txslewriseac = DDR_UIA_TXSLEWRISEAC,
      .txslewfallac = DDR_UIA_TXSLEWFALLAC,
      .disableretraining = DDR_UIA_DISABLERETRAINING,
      .disablephyupdate = DDR_UIA_DISABLEPHYUPDATE,
      .enablehighclkskewfix = DDR_UIA_ENABLEHIGHCLKSKEWFIX,
      .disableunusedaddrlns = DDR_UIA_DISABLEUNUSEDADDRLNS,
      .phyinitsequencenum = DDR_UIA_PHYINITSEQUENCENUM,
      .enabledficspolarityfix = DDR_UIA_ENABLEDFICSPOLARITYFIX,
      .phyvref = DDR_UIA_PHYVREF,
      .sequencectrl = DDR_UIA_SEQUENCECTRL_0,
   },
   .uim = {
      .mr0 = DDR_UIM_MR0_0,
      .mr1 = DDR_UIM_MR1_0,
      .mr2 = DDR_UIM_MR2_0,
      .mr3 = DDR_UIM_MR3_0,
      .mr4 = DDR_UIM_MR4_0,
      .mr5 = DDR_UIM_MR5_0,
      .mr6 = DDR_UIM_MR6_0,
      .mr11 = DDR_UIM_MR11_0,
      .mr12 = DDR_UIM_MR12_0,
      .mr13 = DDR_UIM_MR13_0,
      .mr14 = DDR_UIM_MR14_0,
      .mr22 = DDR_UIM_MR22_0,
   },
   .uis = {
      .swizzle = {DDR_UIS_SWIZZLE_0, DDR_UIS_SWIZZLE_1, DDR_UIS_SWIZZLE_2, DDR_UIS_SWIZZLE_3,
                   DDR_UIS_SWIZZLE_4, DDR_UIS_SWIZZLE_5, DDR_UIS_SWIZZLE_6, DDR_UIS_SWIZZLE_7,
                   DDR_UIS_SWIZZLE_8, DDR_UIS_SWIZZLE_9, DDR_UIS_SWIZZLE_10, DDR_UIS_SWIZZLE_11,
                   DDR_UIS_SWIZZLE_12, DDR_UIS_SWIZZLE_13, DDR_UIS_SWIZZLE_14, DDR_UIS_SWIZZLE_15,
                   DDR_UIS_SWIZZLE_16, DDR_UIS_SWIZZLE_17, DDR_UIS_SWIZZLE_18, DDR_UIS_SWIZZLE_19,
                   DDR_UIS_SWIZZLE_20, DDR_UIS_SWIZZLE_21, DDR_UIS_SWIZZLE_22, DDR_UIS_SWIZZLE_23,
                   DDR_UIS_SWIZZLE_24, DDR_UIS_SWIZZLE_25, DDR_UIS_SWIZZLE_26, DDR_UIS_SWIZZLE_27,
                   DDR_UIS_SWIZZLE_28, DDR_UIS_SWIZZLE_29, DDR_UIS_SWIZZLE_30, DDR_UIS_SWIZZLE_31,
                   DDR_UIS_SWIZZLE_32, DDR_UIS_SWIZZLE_33, DDR_UIS_SWIZZLE_34, DDR_UIS_SWIZZLE_35,
                   DDR_UIS_SWIZZLE_36, DDR_UIS_SWIZZLE_37, DDR_UIS_SWIZZLE_38, DDR_UIS_SWIZZLE_39,
                   DDR_UIS_SWIZZLE_40, DDR_UIS_SWIZZLE_41, DDR_UIS_SWIZZLE_42, DDR_UIS_SWIZZLE_43},
   },
};

size_t ddr_get_size(void)
{
   return ddr_priv.info.size;
}

int ddr_init(void)
{
   struct stm32mp_ddr_priv *priv = &ddr_priv;
   int ret;
   uintptr_t uret;
   size_t retsize;

   /* DDR sub-system bus clock on; must precede the PLL2 start. */
   ddr_sub_system_clk_init();

   ret = rcc_pll2_init();
   if (ret != 0) {
      ERROR("PLL2: %d\r\n", ret);
      return ret;
   }

   mmio_write_32(RISAB5_BASE + RISAB_CR, RISAB_CR_SRWIAD);

   priv->ctl = (struct stm32mp_ddrctl *)stm32mp_ddrctrl_base();
   priv->phy = (struct stm32mp_ddrphy *)stm32mp_ddrphyc_base();
   priv->pwr = stm32mp_pwr_base();
   priv->rcc = stm32mp_rcc_base();
   priv->info.base = STM32MP_DDR_BASE;
   priv->info.size = 0;

   stm32mp2_ddr_init(priv, &ddr_config);

   priv->info.size = ddr_config.info.size;

   uret = stm32mp_ddr_test_data_bus();
   if (uret != 0UL) {
      ERROR("data bus test @ 0x%lx\r\n", uret);
      return -1;
   }

   uret = stm32mp_ddr_test_addr_bus(ddr_config.info.size);
   if (uret != 0UL) {
      ERROR("addr bus test @ 0x%lx\r\n", uret);
      return -1;
   }

   retsize = stm32mp_ddr_check_size();
   if (retsize < ddr_config.info.size) {
      ERROR("size check: 0x%lx < 0x%lx\r\n", (unsigned long)retsize,
            (unsigned long)ddr_config.info.size);
      return -1;
   }

   /* Lock in the self-refresh mode matching the programmed settings. */
   ddr_set_sr_mode(ddr_read_sr_mode());

   NOTICE("DDR: %s, %lu MB\r\n", ddr_config.info.name,
          (unsigned long)(retsize / (1024U * 1024U)));

   return 0;
}
