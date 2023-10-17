#pragma once

#include <stdlib.h>

int usrp_spi_setup(uhd::usrp::multi_usrp::sptr exist_usrp);

void usrp_sync_time(void);
void usrp_set_mode(int mode);
int usrp_select_beam_id(int mode, int id);

void usrp_free(void);
