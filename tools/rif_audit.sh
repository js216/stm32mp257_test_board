#!/bin/sh
# rif_audit.sh - dump the RIF firewall configuration registers that are
# readable from the non-secure world on STM32MP25, and decode which
# resources are still marked secure (i.e. still locked out of Linux).
# Register offsets per RM0457 and the OP-TEE stm32mp2 drivers:
#   RIFSC RISC_SECCFGRx  0x42080010 + 4x   (x = 0..5, one bit per periph ID)
#   RIFSC RISC_PRIVCFGRx 0x42080030 + 4x
#   RIFSC RIMC_ATTRx     0x42080C10 + 4x   (master CID attributes)
#   RCC   SECCFGRx       0x44200000 + 4x   (x = 0..3, 114 RCC resources)
#   PWR   RSECCFGR       0x44210100        (resources 0..6)
#   PWR   WIOSECCFGR     0x44210180        (wakeup IOs 1..6)
#   EXTIn SECCFGRx       base + 0x14 + 0x20x
#   GPIOx SECCFGR        base + 0x30       (one bit per pin)
#   TAMP  SECCFGR        0x46010020
# Read-only. Only firewall *configuration* registers are touched; the
# secure peripherals themselves (RNG, SAES, STGEN, IAC, SERC...) are NOT
# read since an illegal access would raise a bus error.

DM=$(command -v devmem || echo /sbin/devmem)

rd() {
	$DM "$1" 32
}

RIFSC_NAMES="0:TIM1 1:TIM2 2:TIM3 3:TIM4 4:TIM5 5:TIM6 6:TIM7 7:TIM8 8:TIM10 9:TIM11 10:TIM12 11:TIM13 12:TIM14 13:TIM15 14:TIM16 15:TIM17 16:TIM20 17:LPTIM1 18:LPTIM2 19:LPTIM3 20:LPTIM4 21:LPTIM5 22:SPI1 23:SPI2 24:SPI3 25:SPI4 26:SPI5 27:SPI6 28:SPI7 29:SPI8 30:SPDIFRX 31:USART1 32:USART2 33:USART3 34:UART4 35:UART5 36:USART6 37:UART7 38:UART8 39:UART9 40:LPUART1 41:I2C1 42:I2C2 43:I2C3 44:I2C4 45:I2C5 46:I2C6 47:I2C7 48:I2C8 49:SAI1 50:SAI2 51:SAI3 52:SAI4 54:MDF1 55:ADF1 56:FDCAN 57:HDP 58:ADC12 59:ADC3 60:ETH1 61:ETH2 63:USBH 66:USB3DR 67:COMBOPHY 68:PCIE 69:UCPD1 70:ETHSW_DEIP 71:ETHSW_ACM_CFG 72:ETHSW_ACM_MSGBUF 73:STGEN 74:OCTOSPI1 75:OCTOSPI2 76:SDMMC1 77:SDMMC2 78:SDMMC3 79:GPU 80:LTDC_CMN 81:DSI_CMN 84:LVDS 86:CSI 87:DCMIPP 88:DCMI_PSSI 89:VDEC 90:VENC 92:RNG 93:PKA 94:SAES 95:HASH 96:CRYP1 97:CRYP2 98:IWDG1 99:IWDG2 100:IWDG3 101:IWDG4 102:IWDG5 103:WWDG1 104:WWDG2 106:VREFBUF 107:DTS 108:RAMCFG 109:CRC 110:SERC 111:OCTOSPIM 112:GICV2M 114:I3C1 115:I3C2 116:I3C3 117:I3C4 118:ICACHE_DCACHE 119:LTDC_L1L2 120:LTDC_L3 121:LTDC_ROT 122:DSI_TRIG 123:DSI_RDFIFO 125:OTFDEC1 126:OTFDEC2 127:IAC 155:PWR 160:GPIOA 161:GPIOB 162:GPIOC 163:GPIOD 164:GPIOE 165:GPIOF 166:GPIOG 167:GPIOH 168:GPIOI 169:GPIOJ 170:GPIOK 171:GPIOZ"

idname() {
	for kv in $RIFSC_NAMES; do
		case "$kv" in
		"$1:"*)
			echo "${kv#*:}"
			return
			;;
		esac
	done
	echo "ID$1"
}

# print set-bit positions of $1 (hex word), offset by $2, with names if $3=rifsc
setbits() {
	v=$(($1))
	b=0
	while [ $b -lt 32 ]; do
		if [ $(((v >> b) & 1)) -eq 1 ]; then
			n=$(($2 + b))
			if [ "$3" = "rifsc" ]; then
				printf '%s(%s) ' "$n" "$(idname $n)"
			else
				printf '%s ' "$n"
			fi
		fi
		b=$((b + 1))
	done
}

echo "=== RIF_AUDIT_BEGIN ==="

echo "--- RIFSC peripheral security, 0x42080000 ---"
SECLIST=""
i=0
while [ $i -le 5 ]; do
	v=$(rd $((0x42080010 + 4 * i)))
	p=$(rd $((0x42080030 + 4 * i)))
	echo "RISC_SECCFGR$i=$v RISC_PRIVCFGR$i=$p"
	SECLIST="$SECLIST$(setbits "$v" $((32 * i)) rifsc)"
	i=$((i + 1))
done
echo "RIFSC_SECURE_PERIPHS: $SECLIST"

echo "--- RCC resource security, 0x44200000 ---"
RCCLIST=""
i=0
while [ $i -le 3 ]; do
	v=$(rd $((0x44200000 + 4 * i)))
	echo "RCC_SECCFGR$i=$v"
	RCCLIST="$RCCLIST$(setbits "$v" $((32 * i)))"
	i=$((i + 1))
done
echo "RCC_SECURE_RESOURCES: $RCCLIST"

echo "--- PWR resource security, 0x44210000 ---"
v=$(rd 0x44210100)
echo "PWR_RSECCFGR=$v secure_resources: $(setbits "$v" 0)"
v=$(rd 0x44210180)
echo "PWR_WIOSECCFGR=$v secure_wakeup_ios: $(setbits "$v" 0)"

echo "--- EXTI line security ---"
for e in 1:0x44220000 2:0x46230000; do
	n=${e%%:*}
	base=${e#*:}
	LIST=""
	i=0
	while [ $i -le 2 ]; do
		v=$(rd $((base + 0x14 + 0x20 * i)))
		echo "EXTI${n}_SECCFGR$i=$v"
		LIST="$LIST$(setbits "$v" $((32 * i)))"
		i=$((i + 1))
	done
	echo "EXTI${n}_SECURE_LINES: $LIST"
done

echo "--- GPIO pin security (SECCFGR @ base+0x30) ---"
for g in A:0x44240000 B:0x44250000 C:0x44260000 D:0x44270000 E:0x44280000 F:0x44290000 G:0x442a0000 H:0x442b0000 I:0x442c0000 J:0x442d0000 K:0x442e0000 Z:0x46200000; do
	n=${g%%:*}
	base=${g#*:}
	v=$(rd $((base + 0x30)))
	echo "GPIO${n}_SECCFGR=$v secure_pins: $(setbits "$v" 0)"
done

echo "--- TAMP security, 0x46010020 ---"
v=$(rd 0x46010020)
echo "TAMP_SECCFGR=$v"

echo "--- RIFSC master attributes (RIMC_ATTRx @ 0x42080C10) ---"
i=0
while [ $i -le 15 ]; do
	v=$(rd $((0x42080C10 + 4 * i)))
	printf 'RIMC_ATTR%d=%s ' "$i" "$v"
	i=$((i + 1))
done
echo ""

echo "=== RIF_AUDIT_END ==="
exit 0
