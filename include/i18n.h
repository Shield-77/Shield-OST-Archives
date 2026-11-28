#ifndef SHIELD_ONLINE_I18N_H
#define SHIELD_ONLINE_I18N_H

#include <stdbool.h>

void i18n_set_english(bool english);
bool i18n_is_english(void);
const char *tr(const char *french);

#endif
