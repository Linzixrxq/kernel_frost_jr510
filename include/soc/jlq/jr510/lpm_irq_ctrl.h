#ifndef __LPM_IRQ_CTRL_H_
#define __LPM_IRQ_CTRL_H_

#ifdef CONFIG_JLQ_LPM_IRQ_CTRL
int jlq_lpm_irq_clear(int virq);
int jlq_lpm_irq_enable(int virq);
#else
static inline int jlq_lpm_irq_clear(int virq) {return 0;}
static inline int jlq_lpm_irq_enable(int virq) {return 0;}
#endif
#endif
