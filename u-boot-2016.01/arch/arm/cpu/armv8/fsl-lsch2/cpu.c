/*
 * Copyright 2014 Freescale Semiconductor, Inc.
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/armv8/mmu.h>
#include <asm/io.h>
#include <asm/arch-fsl-lsch2/immap_lsch2.h>
#include "cpu.h"
#include "mp.h"
#include "speed.h"

DECLARE_GLOBAL_DATA_PTR;

#ifndef CONFIG_SYS_DCACHE_OFF
/*
 * To start MMU before DDR is available, we create MMU table in SRAM.
 * The base address of SRAM is CONFIG_SYS_FSL_OCRAM_BASE. We use three
 * levels of translation tables here to cover 40-bit address space.
 * We use 4KB granule size, with 40 bits physical address, T0SZ=24
 * Level 0 IA[39], table address @0
 * Level 1 IA[31:30], table address @01000, 0x2000
 * Level 2 IA[29:21], table address @0x3000
 */

#define SECTION_SHIFT_L0	39UL
#define SECTION_SHIFT_L1	30UL
#define SECTION_SHIFT_L2	21UL
#define BLOCK_SIZE_L0	0x8000000000UL
#define BLOCK_SIZE_L1	(1 << SECTION_SHIFT_L1)
#define BLOCK_SIZE_L2	(1 << SECTION_SHIFT_L2)
#define CONFIG_SYS_IFC_BASE	0x60000000UL
#define CONFIG_SYS_IFC_SIZE	0x20000000UL
#define CONFIG_SYS_IFC_BASE2	0x620000000UL
#define CONFIG_SYS_IFC_SIZE2	0x0e0000000UL
#define TCR_EL2_PS_40BIT	(2 << 16)
#define LSCH2_VA_BITS		(40)
#define LSCH2_TCR	(TCR_TG0_4K		| \
			TCR_EL2_PS_40BIT	| \
			TCR_SHARED_NON		| \
			TCR_ORGN_NC		| \
			TCR_IRGN_NC		| \
			TCR_T0SZ(LSCH2_VA_BITS))

/*
 * Final MMU
 * Let's start from the same layout as early MMU and modify as needed.
 * IFC regions will be cache-inhibit.
 */
#define FINAL_QBMAN_CACHED_MEM	0x818000000UL
#define FINAL_QBMAN_CACHED_SIZE	0x4000000

static inline void early_mmu_setup(void)
{
	int el;
	u64 i;
	u64 section_l1t0, section_l1t1, section_l2;
	u64 *level0_table = (u64 *)CONFIG_SYS_FSL_OCRAM_BASE;
	u64 *level1_table_0 = (u64 *)(CONFIG_SYS_FSL_OCRAM_BASE + 0x1000);
	u64 *level1_table_1 = (u64 *)(CONFIG_SYS_FSL_OCRAM_BASE + 0x2000);
	u64 *level2_table = (u64 *)(CONFIG_SYS_FSL_OCRAM_BASE + 0x3000);


	level0_table[0] =
		(u64)level1_table_0 | PMD_TYPE_TABLE;
	level0_table[1] =
		(u64)level1_table_1 | PMD_TYPE_TABLE;

	/*
	 * set level 1 table 0 to cache_inhibit, covering 0 to 512GB
	 * set level 1 table 1 to cache enabled, covering 512GB to 1TB
	 * set level 2 table to cache-inhibit, covering 0 to 1GB
	 */
	section_l1t0 = 0;
	section_l1t1 = BLOCK_SIZE_L0;
	section_l2 = 0;
	for (i = 0; i < 512; i++) {
		set_pgtable_section(level1_table_0, i, section_l1t0,
				    MT_DEVICE_NGNRNE);
		set_pgtable_section(level1_table_1, i, section_l1t1,
				    MT_NORMAL);
		set_pgtable_section(level2_table, i, section_l2,
				    MT_DEVICE_NGNRNE);
		section_l1t0 += BLOCK_SIZE_L1;
		section_l1t1 += BLOCK_SIZE_L1;
		section_l2 += BLOCK_SIZE_L2;
	}

	level1_table_0[0] =
		(u64)level2_table | PMD_TYPE_TABLE;
	level1_table_0[1] =
		0x40000000 | PMD_SECT_AF | PMD_TYPE_SECT |
		PMD_ATTRINDX(MT_DEVICE_NGNRNE);
	level1_table_0[2] =
		0x80000000 | PMD_SECT_AF | PMD_TYPE_SECT |
		PMD_ATTRINDX(MT_NORMAL);
	level1_table_0[3] =
		0xc0000000 | PMD_SECT_AF | PMD_TYPE_SECT |
		PMD_ATTRINDX(MT_NORMAL);

	/* Rewrite table to enable cache */
	set_pgtable_section(level2_table,
			    CONFIG_SYS_FSL_OCRAM_BASE >> SECTION_SHIFT_L2,
			    CONFIG_SYS_FSL_OCRAM_BASE,
			    MT_NORMAL);
	for (i = CONFIG_SYS_IFC_BASE >> SECTION_SHIFT_L2;
	     i < (CONFIG_SYS_IFC_BASE + CONFIG_SYS_IFC_SIZE)
	     >> SECTION_SHIFT_L2; i++) {
		section_l2 = i << SECTION_SHIFT_L2;
		set_pgtable_section(level2_table, i,
				    section_l2, MT_NORMAL);
	}

	el = current_el();
	set_ttbr_tcr_mair(el, (u64)level0_table, LSCH2_TCR, MEMORY_ATTRIBUTES);
	set_sctlr(get_sctlr() | CR_M);
}

static inline void final_mmu_setup(void)
{
	int el;
	u64 i, tbl_base, tbl_limit, section_base;
	u64 section_l1t0, section_l1t1, section_l2;
	u64 *level0_table = (u64 *)gd->arch.tlb_addr;
	u64 *level1_table_0 = (u64 *)(gd->arch.tlb_addr + 0x1000);
	u64 *level1_table_1 = (u64 *)(gd->arch.tlb_addr + 0x2000);
	u64 *level2_table_0 = (u64 *)(gd->arch.tlb_addr + 0x3000);
	u64 *level2_table_1 = (u64 *)(gd->arch.tlb_addr + 0x4000);


	level0_table[0] =
		(u64)level1_table_0 | PMD_TYPE_TABLE;
	level0_table[1] =
		(u64)level1_table_1 | PMD_TYPE_TABLE;

	/*
	 * set level 1 table 0 to cache_inhibit, covering 0 to 512GB
	 * set level 1 table 1 to cache enabled, covering 512GB to 1TB
	 * set level 2 table 0 to cache-inhibit, covering 0 to 1GB
	 */
	section_l1t0 = 0;
	section_l1t1 = BLOCK_SIZE_L0;
	section_l2 = 0;
	for (i = 0; i < 512; i++) {
		set_pgtable_section(level1_table_0, i, section_l1t0,
				    MT_DEVICE_NGNRNE);
		set_pgtable_section(level1_table_1, i, section_l1t1,
				    MT_NORMAL);
		set_pgtable_section(level2_table_0, i, section_l2,
				    MT_DEVICE_NGNRNE);
		section_l1t0 += BLOCK_SIZE_L1;
		section_l1t1 += BLOCK_SIZE_L1;
		section_l2 += BLOCK_SIZE_L2;
	}

	level1_table_0[0] =
		(u64)level2_table_0 | PMD_TYPE_TABLE;
	level1_table_0[2] =
		0x80000000 | PMD_SECT_AF | PMD_TYPE_SECT |
		PMD_ATTRINDX(MT_NORMAL);
	level1_table_0[3] =
		0xc0000000 | PMD_SECT_AF | PMD_TYPE_SECT |
		PMD_ATTRINDX(MT_NORMAL);

	/* Rewrite table to enable cache */
	set_pgtable_section(level2_table_0,
			    CONFIG_SYS_FSL_OCRAM_BASE >> SECTION_SHIFT_L2,
			    CONFIG_SYS_FSL_OCRAM_BASE,
			    MT_NORMAL);

	/*
	 * Fill in other part of tables if cache is needed
	 * If finer granularity than 1GB is needed, sub table
	 * should be created.
	 */
	section_base = FINAL_QBMAN_CACHED_MEM & ~(BLOCK_SIZE_L1 - 1);
	i = section_base >> SECTION_SHIFT_L1;
	level1_table_0[i] = (u64)level2_table_1 | PMD_TYPE_TABLE;
	section_l2 = section_base;
	for (i = 0; i < 512; i++) {
		set_pgtable_section(level2_table_1, i, section_l2,
				    MT_DEVICE_NGNRNE);
		section_l2 += BLOCK_SIZE_L2;
	}
	tbl_base = FINAL_QBMAN_CACHED_MEM & (BLOCK_SIZE_L1 - 1);
	tbl_limit = (FINAL_QBMAN_CACHED_MEM + FINAL_QBMAN_CACHED_SIZE) &
		    (BLOCK_SIZE_L1 - 1);
	for (i = tbl_base >> SECTION_SHIFT_L2;
	     i < tbl_limit >> SECTION_SHIFT_L2; i++) {
		section_l2 = section_base + (i << SECTION_SHIFT_L2);
		set_pgtable_section(level2_table_1, i,
				    section_l2, MT_NORMAL);
	}

	/* flush new MMU table */
	flush_dcache_range(gd->arch.tlb_addr,
			   gd->arch.tlb_addr +  gd->arch.tlb_size);

	el = current_el();
	asm volatile("dsb sy");
	if (el == 1) {
		asm volatile("msr ttbr0_el1, %0"
			     : : "r" ((u64)level0_table) : "memory");
	} else if (el == 2) {
		asm volatile("msr ttbr0_el2, %0"
			     : : "r" ((u64)level0_table) : "memory");
	} else if (el == 3) {
		asm volatile("msr ttbr0_el3, %0"
			     : : "r" ((u64)level0_table) : "memory");
	} else {
		hang();
	}
	asm volatile("isb");

	/*
	 * MMU is already enabled, just need to invalidate TLB to load the
	 * new table. The new table is compatible with the current table, if
	 * MMU somehow walks through the new table before invalidation TLB,
	 * it still works. So we don't need to turn off MMU here.
	 */
}

int arch_cpu_init(void)
{
	icache_enable();
	__asm_invalidate_dcache_all();
	__asm_invalidate_tlb_all();
	early_mmu_setup();
	set_sctlr(get_sctlr() | CR_C);
	return 0;
}

/*
 * This function is called from lib/board.c.
 * It recreates MMU table in main memory. MMU and d-cache are enabled earlier.
 * There is no need to disable d-cache for this operation.
 */
void enable_caches(void)
{
	final_mmu_setup();
	__asm_invalidate_tlb_all();
}
#endif


static inline u32 initiator_type(u32 cluster, int init_id)
{
	struct ccsr_gur *gur = (void *)(CONFIG_SYS_FSL_GUTS_ADDR);
	u32 idx = (cluster >> (init_id * 8)) & TP_CLUSTER_INIT_MASK;
	u32 type = in_be32(&gur->tp_ityp[idx]);

	if (type & TP_ITYP_AV)
		return type;

	return 0;
}

u32 cpu_mask(void)
{
	struct ccsr_gur __iomem *gur = (void *)(CONFIG_SYS_FSL_GUTS_ADDR);
	int i = 0, count = 0;
	u32 cluster, type, mask = 0;

	do {
		int j;
		cluster = in_be32(&gur->tp_cluster[i].lower);
		for (j = 0; j < TP_INIT_PER_CLUSTER; j++) {
			type = initiator_type(cluster, j);
			if (type) {
				if (TP_ITYP_TYPE(type) == TP_ITYP_TYPE_ARM)
					mask |= 1 << count;
				count++;
			}
		}
		i++;
	} while ((cluster & TP_CLUSTER_EOC_MASK) == 0x0);

	return mask;
}

/*
 * Return the number of cores on this SOC.
 */
int cpu_numcores(void)
{
	return hweight32(cpu_mask());
}

int fsl_qoriq_core_to_cluster(unsigned int core)
{
	struct ccsr_gur __iomem *gur =
		(void __iomem *)(CONFIG_SYS_FSL_GUTS_ADDR);
	int i = 0, count = 0;
	u32 cluster;

	do {
		int j;
		cluster = in_be32(&gur->tp_cluster[i].lower);
		for (j = 0; j < TP_INIT_PER_CLUSTER; j++) {
			if (initiator_type(cluster, j)) {
				if (count == core)
					return i;
				count++;
			}
		}
		i++;
	} while ((cluster & TP_CLUSTER_EOC_MASK) == 0x0);

	return -1;      /* cannot identify the cluster */
}

u32 fsl_qoriq_core_to_type(unsigned int core)
{
	struct ccsr_gur __iomem *gur =
		(void __iomem *)(CONFIG_SYS_FSL_GUTS_ADDR);
	int i = 0, count = 0;
	u32 cluster, type;

	do {
		int j;
		cluster = in_be32(&gur->tp_cluster[i].lower);
		for (j = 0; j < TP_INIT_PER_CLUSTER; j++) {
			type = initiator_type(cluster, j);
			if (type) {
				if (count == core)
					return type;
				count++;
			}
		}
		i++;
	} while ((cluster & TP_CLUSTER_EOC_MASK) == 0x0);

	return -1;      /* cannot identify the cluster */
}

#ifdef CONFIG_DISPLAY_CPUINFO
int print_cpuinfo(void)
{
	struct sys_info sysinfo;
	struct ccsr_gur __iomem *gur = (void *)(CONFIG_SYS_FSL_GUTS_ADDR);
	char buf[32];
	unsigned int i, core;
	u32 type;

	get_sys_info(&sysinfo);
	puts("Clock Configuration:");
	for_each_cpu(i, core, cpu_numcores(), cpu_mask()) {
		if (!(i % 3))
			puts("\n       ");
		type = TP_ITYP_VER(fsl_qoriq_core_to_type(core));
		printf("CPU%d(%s):%-4s MHz  ", core,
		       type == TY_ITYP_VER_A7 ? "A7 " :
		       (type == TY_ITYP_VER_A53 ? "A53" :
			(type == TY_ITYP_VER_A57 ? "A57" : "   ")),
		       strmhz(buf, sysinfo.freq_processor[core]));
	}
	printf("\n       Bus:      %-4s MHz  ",
	       strmhz(buf, sysinfo.freq_systembus));
	printf("DDR:      %-4s MHz", strmhz(buf, sysinfo.freq_ddrbus));
	puts("\n");

	/*
	 * Display the RCW, so that no one gets confused as to what RCW
	 * we're actually using for this boot.
	 */
	puts("Reset Configuration Word (RCW):");
	for (i = 0; i < ARRAY_SIZE(gur->rcwsr); i++) {
		u32 rcw = in_be32(&gur->rcwsr[i]);

		if ((i % 4) == 0)
			printf("\n       %08x:", i * 4);
		printf(" %08x", rcw);
	}
	puts("\n");

	return 0;
}
#endif

/*
 * Initializes on-chip ethernet controllers.
 * to override, implement board_eth_init()
 */
int cpu_eth_init(bd_t *bis)
{
#if defined(CONFIG_ETHER_ON_FCC)
	fec_initialize(bis);
#endif

#if defined(CONFIG_UEC_ETH)
	uec_standard_init(bis);
#endif

#if defined(CONFIG_TSEC_ENET) || defined(CONFIG_MPC85XX_FEC)
	tsec_standard_init(bis);
#endif

#ifdef CONFIG_FMAN_ENET
	fm_standard_init(bis);
#endif
	return 0;
}

int arch_early_init_r(void)
{
	int rv;
	rv = fsl_lsch2_wake_seconday_cores();

	if (rv)
		printf("Did not wake secondary cores\n");

	return 0;
}
