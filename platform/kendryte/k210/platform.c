/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *   Damien Le Moal <damien.lemoal@wdc.com>
 */

#include <sbi/riscv_asm.h>
#include <sbi/riscv_encoding.h>
#include <sbi/sbi_const.h>
#include <sbi/sbi_platform.h>
#include <sbi/sbi_system.h>
#include <sbi_utils/fdt/fdt_helper.h>
#include <sbi_utils/fdt/fdt_fixup.h>
#include <sbi_utils/ipi/aclint_mswi.h>
#include <sbi_utils/irqchip/plic.h>
#include <sbi_utils/serial/sifive-uart.h>
#include <sbi_utils/timer/aclint_mtimer.h>
#include "platform.h"

extern const char dt_k210_start[];

unsigned long fw_platform_init(unsigned long arg0, unsigned long arg1,
				unsigned long arg2, unsigned long arg3,
				unsigned long arg4)
{
	return (unsigned long)&dt_k210_start[0];
}

static struct plic_data plic = {
	.addr = K210_PLIC_BASE_ADDR,
	.size = K210_PLIC_BASE_SIZE,
	.num_src = K210_PLIC_NUM_SOURCES,
	.context_map = {
		[0] = { 0, 1 },
		[1] = { 2, 3 },
	},
};

static struct aclint_mswi_data mswi = {
	.addr = K210_ACLINT_MSWI_ADDR,
	.size = ACLINT_MSWI_SIZE,
	.first_hartid = 0,
	.hart_count = K210_HART_COUNT,
};

static struct aclint_mtimer_data mtimer = {
	.mtime_freq = K210_ACLINT_MTIMER_FREQ,
	.mtime_addr = K210_ACLINT_MTIMER_ADDR +
		      ACLINT_DEFAULT_MTIME_OFFSET,
	.mtime_size = ACLINT_DEFAULT_MTIME_SIZE,
	.mtimecmp_addr = K210_ACLINT_MTIMER_ADDR +
			 ACLINT_DEFAULT_MTIMECMP_OFFSET,
	.mtimecmp_size = ACLINT_DEFAULT_MTIMECMP_SIZE,
	.first_hartid = 0,
	.hart_count = K210_HART_COUNT,
	.has_64bit_mmio = true,
};

static u32 k210_get_clk_freq(void)
{
	u32 clksel0, pll0;
	u64 pll0_freq, clkr0, clkf0, clkod0, div;

	/*
	 * If the clock selector is not set, use the base frequency.
	 * Otherwise, use PLL0 frequency with a frequency divisor.
	 */
	clksel0 = k210_read_sysreg(K210_CLKSEL0);
	if (!(clksel0 & 0x1))
		return K210_CLK0_FREQ;

	/*
	 * Get PLL0 frequency:
	 * freq = base frequency * clkf0 / (clkr0 * clkod0)
	 */
	pll0 = k210_read_sysreg(K210_PLL0);
	clkr0 = 1 + (pll0 & 0x0000000f);
	clkf0 = 1 + ((pll0 & 0x000003f0) >> 4);
	clkod0 = 1 + ((pll0 & 0x00003c00) >> 10);
	pll0_freq = clkf0 * K210_CLK0_FREQ / (clkr0 * clkod0);

	/* Get the frequency divisor from the clock selector */
	div = 2ULL << ((clksel0 & 0x00000006) >> 1);

	return pll0_freq / div;
}

static int k210_system_reset_check(u32 type, u32 reason)
{
	return 1;
}

static void k210_system_reset(u32 type, u32 reason)
{
	u32 val;

	val = k210_read_sysreg(K210_RESET);
	val |= K210_RESET_MASK;
	k210_write_sysreg(val, K210_RESET);

	while (1);
}

static struct sbi_system_reset_device k210_reset = {
	.name = "kendryte_k210_reset",
	.system_reset_check = k210_system_reset_check,
	.system_reset = k210_system_reset
};

static int k210_early_init(bool cold_boot)
{
	if (!cold_boot)
		return 0;

	sbi_system_reset_add_device(&k210_reset);

	return sifive_uart_init(K210_UART_BASE_ADDR, k210_get_clk_freq(),
				K210_UART_BAUDRATE);
}

static int k210_final_init(bool cold_boot)
{
	void *fdt;

	if (!cold_boot)
		return 0;

	fdt = fdt_get_address_rw();

	fdt_cpu_fixup(fdt);
	fdt_fixups(fdt);

	return 0;
}

static int k210_irqchip_init(void)
{
	return plic_cold_irqchip_init(&plic);
}

static int k210_ipi_init(void)
{
	return aclint_mswi_cold_init(&mswi);
}

static int k210_timer_init(void)
{
	return aclint_mtimer_cold_init(&mtimer, NULL);
}

const struct sbi_platform_operations platform_ops = {
	.early_init	= k210_early_init,

	.final_init	= k210_final_init,

	.irqchip_init = k210_irqchip_init,

	.ipi_init  = k210_ipi_init,

	.timer_init	   = k210_timer_init,
};

const struct sbi_platform platform = {
	.opensbi_version	= OPENSBI_VERSION,
	.platform_version   	= SBI_PLATFORM_VERSION(0x0, 0x01),
	.name			= "Kendryte K210",
	.features		= 0,
	.hart_count		= K210_HART_COUNT,
	.hart_stack_size	= SBI_PLATFORM_DEFAULT_HART_STACK_SIZE,
	.heap_size		=
			SBI_PLATFORM_DEFAULT_HEAP_SIZE(K210_HART_COUNT),
	.platform_ops_addr	= (unsigned long)&platform_ops
};
