#ifndef SBI_H
#define SBI_H


#define SBI_EXT_SET_TIMER 0x0
#define SBI_EXT_SHUTDOWN  0x8
#define SBI_EXT_BASE      0x10

long sbi_get_spec_version(void);
long sbi_get_imp_id(void);
long sbi_get_imp_version(void);
void sbi_set_timer(unsigned long stime_value);

#endif