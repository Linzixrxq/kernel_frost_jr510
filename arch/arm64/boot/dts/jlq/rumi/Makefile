#ifdef CONFIG_ARCH_JLQ

#ifdef CONFIG_JLQ_EMULATOR

#ifdef CONFIG_JLQ_RUMI
dtb-y = jlq_jr510_rumi.dtb
#else
#dtb-$(CONFIG_JLQ_EMULATOR) = jlq_jr510_zebu.dtb
#endif

#endif

#endif

always          := $(dtb-y)
subdir-y        := $(dts-dirs)
clean-files     := *.dtb

