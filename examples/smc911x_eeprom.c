/*
 * smc911x_eeprom.c - EEPROM interface to SMC911x parts.
 * Only tested on SMSC9118 though ...
 *
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 *
 * Based on smc91111_eeprom.c which:
 * Heavily borrowed from the following peoples GPL'ed software:
 *  - Wolfgang Denk, DENX Software Engineering, wd@denx.de
 *       Das U-boot
 *  - Ladislav Michl ladis@linux-mips.org
 *       A rejected patch on the U-Boot mailing list
 */

#include <common.h>


#include <command.h>
#include <net.h>
#include <miiphy.h>
#include <malloc.h>

#include <exports.h>

#include "../drivers/net/smc911x.h"

static inline u32 eth_reg_read(u32 addr)
{
        volatile u16 *addr_16 = (u16 *)addr;
        return ((*addr_16 & 0x0000ffff) | (*(addr_16 + 1) << 16));
}
static inline void eth_reg_write(u32 addr, u32 val)
{
        *(volatile u16*)addr = (u16)val;
        *(volatile u16*)(addr + 2) = (u16)(val >> 16);
}

static void smc911x_reset(void)
{
        int timeout;
        /* Take out of PM setting first */
        if (eth_reg_read(PMT_CTRL) & PMT_CTRL_READY) {
                /* Write to the bytetest will take out of powerdown */
                eth_reg_write(BYTE_TEST, 0x0);

                timeout = 10;

                while (timeout-- && !(eth_reg_read(PMT_CTRL) & PMT_CTRL_READY))
                        udelay(10);
                if (!timeout) {
                        printf(DRIVERNAME
                                ": timeout waiting for PM restore\n");
                        return;
                }
        }
    printf("HCJ %s enter 222\n",__func__);

        /* Disable interrupts */
        eth_reg_write(INT_EN, 0);

        eth_reg_write(HW_CFG, HW_CFG_SRST);

        timeout = 1000;
        while (timeout-- && eth_reg_read(E2P_CMD) & E2P_CMD_EPC_BUSY)
                udelay(10);

        if (!timeout) {
                printf(DRIVERNAME ": reset timeout\n");
                return;
        }
    printf("HCJ %s enter 33333333333\n",__func__);

        /* Reset the FIFO level and flow control settings */
        smc911x_set_mac_csr(FLOW, FLOW_FCPT | FLOW_FCEN);
        eth_reg_write(AFC_CFG, 0x0050287F);
        /* Set to LED outputs */
        eth_reg_write(GPIO_CFG, 0x70070000);
}


/**
 *	smsc_ctrlc - detect press of CTRL+C (common ctrlc() isnt exported!?)
 */
u32 smc911x_get_mac_csr(u8 reg)
{
        while (eth_reg_read(MAC_CSR_CMD) & MAC_CSR_CMD_CSR_BUSY)
                ;
        eth_reg_write(MAC_CSR_CMD, MAC_CSR_CMD_CSR_BUSY | MAC_CSR_CMD_R_NOT_W | reg);
        while (eth_reg_read(MAC_CSR_CMD) & MAC_CSR_CMD_CSR_BUSY)
                ;

        return eth_reg_read(MAC_CSR_DATA);
}

void smc911x_set_mac_csr(u8 reg, u32 data)
{
        while (eth_reg_read(MAC_CSR_CMD) & MAC_CSR_CMD_CSR_BUSY)
                ;
        eth_reg_write(MAC_CSR_DATA, data);
        eth_reg_write(MAC_CSR_CMD, MAC_CSR_CMD_CSR_BUSY | reg);
        while (eth_reg_read(MAC_CSR_CMD) & MAC_CSR_CMD_CSR_BUSY)
                ;
}
static int smc911x_phy_reset(void)
{
        u32 reg;

        reg = eth_reg_read(PMT_CTRL);
        reg &= ~0xfffff030;
        reg |= PMT_CTRL_PHY_RST;
        eth_reg_write(PMT_CTRL, reg);

        mdelay(100);

        return 0;
}
int smc911x_initialize (struct eth_device *dev)
{
        int i, val = 0;

        val = eth_reg_read(BYTE_TEST);
        if (val != 0x87654321) {
                return -1;
        }

        val = eth_reg_read(ID_REV) >> 16;
        for (i = 0; chip_ids[i].id != 0; i++) {
                if (chip_ids[i].id == val)
                        break;
        }
        if (!chip_ids[i].id) {
                return -1;
        }

        dev = (struct eth_device *) malloc (sizeof *dev);

        //memcpy(dev->enetaddr, bis->bi_enetaddr, 6);

        //sprintf(dev->name, DRIVERNAME);
        dev->priv = (void *)NULL; /* this have to come before bus_to_phys() */
        dev->iobase = CONFIG_DRIVER_SMC911X_BASE;
        //dev->init = smc911x_eth_init;
        //dev->halt = smc911x_eth_halt;
        //dev->send = smc911x_eth_send;
        //dev->recv = smc911x_eth_rx;

        //eth_register (dev);

        return 0;
}

static int smsc_ctrlc(void)
{
	return (tstc() && getc() == 0x03);
}

/**
 *	usage - dump usage information
 */
static void usage(void)
{
	puts(
		"MAC/EEPROM Commands:\n"
		" P : Print the MAC addresses\n"
		" D : Dump the EEPROM contents\n"
		" M : Dump the MAC contents\n"
		" C : Copy the MAC address from the EEPROM to the MAC\n"
		" W : Write a register in the EEPROM or in the MAC\n"
		" Q : Quit\n"
		"\n"
		"Some commands take arguments:\n"
		" W <E|M> <register> <value>\n"
		"    E: EEPROM   M: MAC\n"
	);
}

/**
 *	dump_regs - dump the MAC registers
 *
 * Registers 0x00 - 0x50 are FIFOs.  The 0x50+ are the control registers
 * and they're all 32bits long.  0xB8+ are reserved, so don't bother.
 */
static void dump_regs(struct eth_device *dev)
{
	u8 i, j = 0;
	for (i = 0x50; i < 0xB8; i += sizeof(u32))
		printf("%02x: 0x%08x %c", i,
			smc911x_eth_reg_read(dev, i),
			(j++ % 2 ? '\n' : ' '));
}

/**
 *	do_eeprom_cmd - handle eeprom communication
 */
static int do_eeprom_cmd(struct eth_device *dev, int cmd, u8 reg)
{
	if (smc911x_eth_reg_read(dev, E2P_CMD) & E2P_CMD_EPC_BUSY) {
		printf("eeprom_cmd: busy at start (E2P_CMD = 0x%08x)\n",
			smc911x_eth_reg_read(dev, E2P_CMD));
		return -1;
	}

	smc911x_eth_reg_write(dev, E2P_CMD, E2P_CMD_EPC_BUSY | cmd | reg);

	while (smc911x_eth_reg_read(dev, E2P_CMD) & E2P_CMD_EPC_BUSY)
		if (smsc_ctrlc()) {
			printf("eeprom_cmd: timeout (E2P_CMD = 0x%08x)\n",
				smc911x_eth_reg_read(dev, E2P_CMD));
			return -1;
		}

	return 0;
}

/**
 *	read_eeprom_reg - read specified register in EEPROM
 */
static u8 read_eeprom_reg(struct eth_device *dev, u8 reg)
{
	int ret = do_eeprom_cmd(dev, E2P_CMD_EPC_CMD_READ, reg);
	return (ret ? : smc911x_eth_reg_read(dev, E2P_DATA));
}

/**
 *	write_eeprom_reg - write specified value into specified register in EEPROM
 */
static int write_eeprom_reg(struct eth_device *dev, u8 value, u8 reg)
{
	int ret;

	/* enable erasing/writing */
	ret = do_eeprom_cmd(dev, E2P_CMD_EPC_CMD_EWEN, reg);
	if (ret)
		goto done;

	/* erase the eeprom reg */
	ret = do_eeprom_cmd(dev, E2P_CMD_EPC_CMD_ERASE, reg);
	if (ret)
		goto done;

	/* write the eeprom reg */
	smc911x_eth_reg_write(dev, E2P_DATA, value);
	ret = do_eeprom_cmd(dev, E2P_CMD_EPC_CMD_WRITE, reg);
	if (ret)
		goto done;

	/* disable erasing/writing */
	ret = do_eeprom_cmd(dev, E2P_CMD_EPC_CMD_EWDS, reg);

 done:
	return ret;
}

/**
 *	skip_space - find first non-whitespace in given pointer
 */
static char *skip_space(char *buf)
{
	while (buf[0] == ' ' || buf[0] == '\t')
		++buf;
	return buf;
}

/**
 *	write_stuff - handle writing of MAC registers / eeprom
 */
static void write_stuff(struct eth_device *dev, char *line)
{
	char dest;
	char *endp;
	u8 reg;
	u32 value;

	/* Skip over the "W " part of the command */
	line = skip_space(line + 1);

	/* Figure out destination */
	switch (line[0]) {
	case 'E':
	case 'M':
		dest = line[0];
		break;
	default:
	invalid_usage:
		printf("ERROR: Invalid write usage\n");
		usage();
		return;
	}

	/* Get the register to write */
	line = skip_space(line + 1);
	reg = simple_strtoul(line, &endp, 16);
	if (line == endp)
		goto invalid_usage;

	/* Get the value to write */
	line = skip_space(endp);
	value = simple_strtoul(line, &endp, 16);
	if (line == endp)
		goto invalid_usage;

	/* Check for trailing cruft */
	line = skip_space(endp);
	if (line[0])
		goto invalid_usage;

	/* Finally, execute the command */
	if (dest == 'E') {
		printf("Writing EEPROM register %02x with %02x\n", reg, value);
		write_eeprom_reg(dev, value, reg);
	} else {
		printf("Writing MAC register %02x with %08x\n", reg, value);
		smc911x_eth_reg_write(dev, reg, value);
	}
}

/**
 *	copy_from_eeprom - copy MAC address in eeprom to address registers
 */
static void copy_from_eeprom(struct eth_device *dev)
{
	ulong addrl =
		read_eeprom_reg(dev, 0x01) |
		read_eeprom_reg(dev, 0x02) << 8 |
		read_eeprom_reg(dev, 0x03) << 16 |
		read_eeprom_reg(dev, 0x04) << 24;
	ulong addrh =
		read_eeprom_reg(dev, 0x05) |
		read_eeprom_reg(dev, 0x06) << 8;
	smc911x_set_mac_csr( ADDRL, addrl);
	smc911x_set_mac_csr( ADDRH, addrh);
	puts("EEPROM contents copied to MAC\n");
}

/**
 *	print_macaddr - print MAC address registers and MAC address in eeprom
 */
static void print_macaddr(struct eth_device *dev)
{
	puts("Current MAC Address in MAC:     ");
	ulong addrl = smc911x_get_mac_csr(ADDRL);
	ulong addrh = smc911x_get_mac_csr( ADDRH);
	printf("%02x:%02x:%02x:%02x:%02x:%02x\n",
		(u8)(addrl), (u8)(addrl >> 8), (u8)(addrl >> 16),
		(u8)(addrl >> 24), (u8)(addrh), (u8)(addrh >> 8));

	puts("Current MAC Address in EEPROM:  ");
	int i;
	for (i = 1; i < 6; ++i)
		printf("%02x:", read_eeprom_reg(dev, i));
	printf("%02x\n", read_eeprom_reg(dev, i));
}

/**
 *	dump_eeprom - dump the whole content of the EEPROM
 */
static void dump_eeprom(struct eth_device *dev)
{
	int i;
	puts("EEPROM:\n");
	for (i = 0; i < 7; ++i)
		printf("%02x: 0x%02x\n", i, read_eeprom_reg(dev, i));
}

/**
 *	smc911x_init - get the MAC/EEPROM up and ready for use
 */
static int smc911x_init(struct eth_device *dev)
{
	/* See if there is anything there */
	//if (smc911x_detect_chip(dev))
	//	return 1;
        if(smc911x_initialize(dev))
		return 1;
	smc911x_reset();

	/* Make sure we set EEDIO/EECLK to the EEPROM */
	if (smc911x_eth_reg_read(dev, GPIO_CFG) & GPIO_CFG_EEPR_EN) {
		while (smc911x_eth_reg_read(dev, E2P_CMD) & E2P_CMD_EPC_BUSY)
			if (smsc_ctrlc()) {
				printf("init: timeout (E2P_CMD = 0x%08x)\n",
					smc911x_eth_reg_read(dev, E2P_CMD));
				return 1;
			}
		smc911x_eth_reg_write(dev, GPIO_CFG,
			smc911x_eth_reg_read(dev, GPIO_CFG) & ~GPIO_CFG_EEPR_EN);
	}

	return 0;
}

/**
 *	getline - consume a line of input and handle some escape sequences
 */
static char *getline(void)
{
	static char buffer[100];
	char c;
	size_t i;

	i = 0;
	while (1) {
		buffer[i] = '\0';
		while (!tstc())
			continue;

		c = getc();
		/* Convert to uppercase */
		if (c >= 'a' && c <= 'z')
			c -= ('a' - 'A');

		switch (c) {
		case '\r':	/* Enter/Return key */
		case '\n':
			puts("\n");
			return buffer;

		case 0x03:	/* ^C - break */
			return NULL;

		case 0x5F:
		case 0x08:	/* ^H  - backspace */
		case 0x7F:	/* DEL - backspace */
			if (i) {
				puts("\b \b");
				i--;
			}
			break;

		default:
			/* Ignore control characters */
			if (c < 0x20)
				break;
			/* Queue up all other characters */
			buffer[i++] = c;
			printf("%c", c);
			break;
		}
	}
}

/**
 *	smc911x_eeprom - our application's main() function
 */
int smc911x_eeprom(int argc, char * const argv[])
{
	/* Avoid initializing on stack as gcc likes to call memset() */
	struct eth_device dev;
	dev.iobase = CONFIG_DRIVER_SMC911X_BASE;

	/* Print the ABI version */
	app_startup(argv);
	if (XF_VERSION != get_version()) {
		printf("Expects ABI version %d\n", XF_VERSION);
		printf("Actual U-Boot ABI version %lu\n", get_version());
		printf("Can't run\n\n");
		return 1;
	}

	/* Initialize the MAC/EEPROM somewhat */
	puts("\n");
	if (smc911x_init(&dev))
		return 1;

	/* Dump helpful usage information */
	puts("\n");
	usage();
	puts("\n");

	while (1) {
		char *line;

		/* Send the prompt and wait for a line */
		puts("eeprom> ");
		line = getline();

		/* Got a ctrl+c */
		if (!line)
			return 0;

		/* Eat leading space */
		line = skip_space(line);

		/* Empty line, try again */
		if (!line[0])
			continue;

		/* Only accept 1 letter commands */
		if (line[0] && line[1] && line[1] != ' ' && line[1] != '\t')
			goto unknown_cmd;

		/* Now parse the command */
		switch (line[0]) {
		case 'W': write_stuff(&dev, line); break;
		case 'D': dump_eeprom(&dev);       break;
		case 'M': dump_regs(&dev);         break;
		case 'C': copy_from_eeprom(&dev);  break;
		case 'P': print_macaddr(&dev);     break;
		unknown_cmd:
		default:  puts("ERROR: Unknown command!\n\n");
		case '?':
		case 'H': usage();            break;
		case 'Q': return 0;
		}
	}
}
