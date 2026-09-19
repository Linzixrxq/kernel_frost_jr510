# SPDX-License-Identifier: GPL-2.0
CFLAGS_rpmh-rsc.o := -I$(src)
obj-$(CONFIG_QCOM_AOSS_QMP) +=	qcom_aoss.o
obj-$(CONFIG_QCOM_GENI_SE) +=	qcom-geni-se.o
obj-$(CONFIG_QCOM_COMMAND_DB) += cmd-db.o
obj-$(CONFIG_QCOM_GLINK_SSR) +=	glink_ssr.o
obj-$(CONFIG_QCOM_GSBI)	+=	qcom_gsbi.o
obj-$(CONFIG_QCOM_MDT_LOADER)	+= mdt_loader.o
obj-$(CONFIG_QCOM_PM)	+=	spm.o
obj-$(CONFIG_MSM_PIL)   +=      peripheral-loader.o
obj-$(CONFIG_MSM_SUBSYSTEM_RESTART) += subsystem_restart.o
subsystem_restart-y := msm_subsystem_restart.o subsystem_notif.o ramdump.o sysmon-qmi.o
obj-$(CONFIG_MSM_PIL_SSR_GENERIC) += subsys-pil-tz.o
obj-$(CONFIG_MSM_SERVICE_NOTIFIER) += service-notifier.o
obj-$(CONFIG_MSM_SERVICE_LOCATOR) += service-locator.o
obj-$(CONFIG_QCOM_QMI_HELPERS)	+= qmi_helpers.o
qmi_helpers-y	+= qmi_encdec.o qmi_interface.o
obj-$(CONFIG_MEM_SHARE_QMI_SERVICE) += memshare/
obj-$(CONFIG_QCOM_RMTFS_MEM)	+= rmtfs_mem.o
obj-$(CONFIG_QCOM_RPMH)		+= qcom_rpmh.o
qcom_rpmh-y			+= rpmh-rsc.o
qcom_rpmh-y			+= rpmh.o
obj-$(CONFIG_QCOM_SOC_SLEEP_STATS)	+= soc_sleep_stats.o
obj-$(CONFIG_QCOM_SMD_RPM)	+= smd-rpm.o
obj-$(CONFIG_QCOM_SMEM) +=	smem.o
obj-$(CONFIG_QCOM_SMEM_STATE) += smem_state.o
obj-$(CONFIG_QCOM_SMP2P)	+= smp2p.o
obj-$(CONFIG_QCOM_SMSM)	+= smsm.o
obj-$(CONFIG_QCOM_SECURE_BUFFER) += secure_buffer.o
obj-$(CONFIG_QCOM_SOCINFO)	+= socinfo.o
obj-$(CONFIG_QCOM_WCNSS_CTRL) += wcnss_ctrl.o
obj-$(CONFIG_QCOM_APR) += apr.o
obj-$(CONFIG_QCOM_LLCC) += llcc-slice.o
obj-$(CONFIG_QCOM_MINIDUMP) += msm_minidump.o minidump_log.o
obj-$(CONFIG_QCOM_MICRODUMP) += microdump_collector.o
obj-$(CONFIG_QCOM_SDM845_LLCC) += llcc-sdm845.o
obj-$(CONFIG_QCOM_RPMHPD) += rpmhpd.o
obj-$(CONFIG_QCOM_RPMPD) += rpmpd.o
obj-$(CONFIG_QCOM_SMP2P_SLEEPSTATE) += smp2p_sleepstate.o
obj-$(CONFIG_QCOM_GLINK_PKT) += glink_pkt.o
obj-$(CONFIG_QCOM_GLINK) += glink_probe.o
obj-$(CONFIG_MSM_GLINK_SSR) += msm_glink_ssr.o
obj-$(CONFIG_QTI_RPM_STATS_LOG) += rpmh_master_stat.o
ifdef CONFIG_MSM_RPM_SMD
obj-$(CONFIG_QTI_RPM_STATS_LOG) += rpm_master_stat.o
endif
ifdef CONFIG_DEBUG_FS
obj-$(CONFIG_MSM_RPM_SMD)   +=  rpm-smd-debug.o
endif
obj-$(CONFIG_ICNSS2) += icnss2/
