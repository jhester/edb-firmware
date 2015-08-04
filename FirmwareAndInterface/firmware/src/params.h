#ifndef PARAMS_H
#define PARAMS_H

#include <stdint.h>

#include "host_comm.h"

extern uint16_t param_test;
extern float param_saved_vcap_adjust;

unsigned set_param(param_t param, uint8_t *buf);
unsigned get_param(param_t param, uint8_t *buf);

#endif
