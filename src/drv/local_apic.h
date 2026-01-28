// ------------------------------------------------------------------------------------------------
// intr/local_apic.h
// ------------------------------------------------------------------------------------------------

#pragma once

#include "stdint.h"

extern uint8_t *g_localApicAddr;

void LocalApicInit();
void LocalApicSendEOI();
int LocalApicGetId();
void LocalApicSendInit(int apic_id);
void LocalApicSendStartup(int apic_id, int vector);
