/* Tekstuur voor de FM-1. Copyright (C) 2026 BARNIR. Alle rechten voorbehouden.
 * De helft van de app in de afspraak met het platform (fm1-base: device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "TEKSTUUR"
#define APP_TAGLINE "by BARNIR"
#define APP_SPLASH_RGB RGB(0xf1, 0xe9, 0xda)
#define APP_BLOCK 256u                   /* frames per audioblok: 5,8 ms */
#define APP_TOUCH                        /* aanslag en druk van de toetsen (lib/touch.h) */
#define APP_SPLASH                       /* eigen opstartscherm: het woordmerk */
#define APP_OVERLOAD                     /* het geluidsblok kwam bijna te laat: minder doorgangen */
#define APP_CANVAS_PX (240u * 116u)      /* het grootste vlak dat in een keer getekend wordt */
#define APP_REDRAW                       /* de schermspiegel (mirror) vraagt het hele scherm opnieuw */
void app_redraw(void);
#define APP_USB_CMD                      /* eigen bronnen vanaf de computer (tools/tekstuur_samples.py) */
void app_usb_cmd(const uint8_t *d, uint32_t n);
void app_splash(void);
void app_overload(void);

/* wat bewaard wordt, en waar in het flashgeheugen: de eigen bronnen staan ervoor, 0x97000 .. 0xDBFFF (app.c BANK_BASE) */
enum { OBJ_STATE, OBJ_PANEL, OBJ_USER, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

#define APP_HOST_EXPECT                  /* de controles van de simulator (tests/) */
int app_expect(const char *what, int *got);
