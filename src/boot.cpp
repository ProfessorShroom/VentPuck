// Copyright © Charlie Howard 2026 All rights reserved

#include "boot.h"
#include <Arduino.h>
#include <math.h>
#include <lvgl.h>
#include "esp_heap_caps.h"
#include "settings.h"
#include "obd.h"
#include "ui.h"

LV_FONT_DECLARE(font_wordmark);
LV_FONT_DECLARE(font_digits);

extern "C" {
extern const uint8_t logo_default_png[];
extern const size_t logo_default_png_len;
}

static const int SCREEN = 466;
static const int CX = 233;  // centre of the logo and of the iris
static const int CY = 175;
static const int LOGO_PX = 208;  // on-screen size of the logo at the start
static const int IRIS_END = 300;
static const int RING_R = 400;  // fixed outer radius of the cover ring
static const int TOTAL_MS = 2700;

static lv_obj_t *logoObj, *wordObj, *ringObj;
static lv_img_dsc_t logoDsc;  // the PNG as stored
static lv_img_dsc_t logoScaled;
static uint8_t* logoBuf = nullptr;
static int baseZoom = 256;
static lv_anim_t bootAnim;
static uint32_t bootStartMs = 0;

static uint32_t be32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
static float easeOut(float x) { float u = 1.0f - x; return 1.0f - u * u * u; }
static float easeIn(float x) { return x * x * x; }

static int irisR = 1;  // current radius of the hole in the cover

static void ringDraw(lv_event_t* e) {
  if (irisR >= RING_R) return;  // fully open
  lv_draw_ctx_t* ctx = lv_event_get_draw_ctx(e);
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_opa = LV_OPA_TRANSP;
  d.border_opa = LV_OPA_COVER;
  d.border_color = g_settings.darkMode ? lv_color_hex(0x000000) : lv_color_hex(g_settings.colour1);
  d.border_width = RING_R - irisR;
  d.radius = LV_RADIUS_CIRCLE;
  lv_area_t a = {(lv_coord_t)(CX - RING_R), (lv_coord_t)(CY - RING_R), (lv_coord_t)(CX + RING_R - 1),
                 (lv_coord_t)(CY + RING_R - 1)};
  lv_draw_rect(ctx, &d, &a);
}

static int halfW(int r, int dy) {
  if (dy >= r) return -1;
  return (int)ceilf(sqrtf((float)(r * r - dy * dy)));
}

static void invalidateIris(int r0, int r1) {
  const int BAND = 40;
  lv_obj_t* scr = lv_scr_act();
  int yTop = CY - r1 < 0 ? 0 : CY - r1;
  int yBot = CY + r1 > SCREEN - 1 ? SCREEN - 1 : CY + r1;
  for (int y = yTop; y <= yBot; y += BAND) {
    int y2 = y + BAND - 1 > yBot ? yBot : y + BAND - 1;
    int dNear = (y <= CY && CY <= y2) ? 0 : min(abs(y - CY), abs(y2 - CY));
    int dFar = max(abs(y - CY), abs(y2 - CY));
    int xo = halfW(r1, dNear);
    if (xo < 0) continue;
    int xi = halfW(r0, dFar);
    if (xi < 0) xi = 0;
    lv_area_t a;
    if (xi == 0) {
      a = {(lv_coord_t)max(0, CX - xo), (lv_coord_t)y, (lv_coord_t)min(SCREEN - 1, CX + xo), (lv_coord_t)y2};
      lv_obj_invalidate_area(scr, &a);
    } else {
      a = {(lv_coord_t)max(0, CX - xo), (lv_coord_t)y, (lv_coord_t)min(SCREEN - 1, CX - xi), (lv_coord_t)y2};
      lv_obj_invalidate_area(scr, &a);
      a = {(lv_coord_t)max(0, CX + xi), (lv_coord_t)y, (lv_coord_t)min(SCREEN - 1, CX + xo), (lv_coord_t)y2};
      lv_obj_invalidate_area(scr, &a);
    }
  }
}

static void bootTick(void*, int32_t t) {
  static int lastZoom = -1, lastOpa = -1, lastWordOpa = -1;
  float fade = clamp01(t / 500.0f);

  float s = (t < 1300) ? 1.0f : (1.0f - easeIn(clamp01((t - 1300) / 900.0f)));
  if (s <= 0.06f) {
    if (!lv_obj_has_flag(logoObj, LV_OBJ_FLAG_HIDDEN)) lv_obj_add_flag(logoObj, LV_OBJ_FLAG_HIDDEN);
  } else {
    int zoom = (int)(s * baseZoom);
    if (zoom < 16) zoom = 16;
    int opa = (int)(fade * 255.0f);
    if (zoom != lastZoom) {
      lv_img_set_zoom(logoObj, zoom);
      lastZoom = zoom;
    }
    if (opa != lastOpa) {
      lv_obj_set_style_img_opa(logoObj, (lv_opa_t)opa, 0);
      lastOpa = opa;
    }
  }

  float wf = fade * (1.0f - clamp01((t - 1300) / 450.0f));
  int wopa = (int)(wf * 255.0f);
  if (wopa != lastWordOpa) {
    lv_obj_set_style_text_opa(wordObj, (lv_opa_t)wopa, 0);
    lastWordOpa = wopa;
  }

  float p = clamp01((t - 1500) / 1100.0f);
  int r = 1 + (int)((IRIS_END - 1) * easeOut(p));
  if (r != irisR) {
    int r0 = irisR - 2 < 0 ? 0 : irisR - 2;
    int r1 = r + 2;
    irisR = r;
    invalidateIris(r0, r1);
  }
}

static void bootDone(lv_anim_t*) {
  lv_obj_del(ringObj);
  lv_obj_del(logoObj);
  lv_obj_del(wordObj);
  ringObj = logoObj = wordObj = nullptr;
  if (logoBuf) {
    heap_caps_free(logoBuf);
    logoBuf = nullptr;
  }
  Serial.printf("Boot animation finished after %u ms\n", (unsigned)(millis() - bootStartMs));
}

static bool prescaleLogo(int& outW, int& outH) {
  lv_img_decoder_dsc_t dec;
  if (lv_img_decoder_open(&dec, &logoDsc, lv_color_black(), 0) != LV_RES_OK || !dec.img_data) {
    Serial.println("Logo: couldn't decode for pre-scaling, using the PNG directly");
    return false;
  }
  if (dec.header.cf != LV_IMG_CF_TRUE_COLOR_ALPHA && dec.header.cf != LV_IMG_CF_RAW_ALPHA) {
    Serial.printf("Logo: unexpected decoded format %d, using the PNG directly\n", (int)dec.header.cf);
    lv_img_decoder_close(&dec);
    return false;
  }
  int sw = dec.header.w ? dec.header.w : logoDsc.header.w;
  int sh = dec.header.h ? dec.header.h : logoDsc.header.h;
  int big = sw > sh ? sw : sh;
  int tw = sw * LOGO_PX / big, th = sh * LOGO_PX / big;
  if (tw < 1) tw = 1;
  if (th < 1) th = 1;
  uint8_t* out = (uint8_t*)heap_caps_malloc((size_t)tw * th * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!out) {
    Serial.println("Logo: out of memory for pre-scaling, using the PNG directly");
    lv_img_decoder_close(&dec);
    return false;
  }
  const uint8_t* src = dec.img_data;
  for (int y = 0; y < th; y++) {
    int sy0 = y * sh / th, sy1 = (y + 1) * sh / th;
    if (sy1 <= sy0) sy1 = sy0 + 1;
    for (int x = 0; x < tw; x++) {
      int sx0 = x * sw / tw, sx1 = (x + 1) * sw / tw;
      if (sx1 <= sx0) sx1 = sx0 + 1;
      uint32_t sumA = 0, sumR = 0, sumG = 0, sumB = 0, n = 0;
      for (int sy = sy0; sy < sy1; sy++) {
        for (int sx = sx0; sx < sx1; sx++) {
          const uint8_t* px = src + ((size_t)sy * sw + sx) * 3;
          uint16_t c = px[0] | (px[1] << 8);
          uint32_t a = px[2];
          sumA += a;
          sumR += ((c >> 11) & 31) * a;
          sumG += ((c >> 5) & 63) * a;
          sumB += (c & 31) * a;
          n++;
        }
      }
      uint16_t c = 0;
      if (sumA) c = (uint16_t)(((sumR / sumA) << 11) | ((sumG / sumA) << 5) | (sumB / sumA));
      uint8_t* d = out + ((size_t)y * tw + x) * 3;
      d[0] = c & 0xFF;
      d[1] = c >> 8;
      d[2] = (uint8_t)(sumA / n);
    }
  }
  lv_img_decoder_close(&dec);

  memset(&logoScaled, 0, sizeof logoScaled);
  logoScaled.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
  logoScaled.header.w = tw;
  logoScaled.header.h = th;
  logoScaled.data_size = (uint32_t)tw * th * 3;
  logoScaled.data = out;
  logoBuf = out;
  outW = tw;
  outH = th;
  return true;
}

static void buildBoot(lv_obj_t* scr) {
  const uint8_t* data = g_logoPng ? g_logoPng : logo_default_png;
  size_t len = g_logoPng ? g_logoLen : logo_default_png_len;
  memset(&logoDsc, 0, sizeof logoDsc);
  logoDsc.header.cf = LV_IMG_CF_RAW_ALPHA;
  logoDsc.header.w = be32(data + 16);
  logoDsc.header.h = be32(data + 20);
  logoDsc.data_size = len;
  logoDsc.data = data;

  const lv_img_dsc_t* logoSrc = &logoDsc;
  int w = logoDsc.header.w, h = logoDsc.header.h;
  if (prescaleLogo(w, h)) {
    logoSrc = &logoScaled;
    baseZoom = 256;  // already the right size
  } else {
    int big = w > h ? w : h;
    baseZoom = 256 * LOGO_PX / big;
    if (baseZoom < 8) baseZoom = 8;
  }

  ringObj = lv_obj_create(scr);
  lv_obj_remove_style_all(ringObj);
  lv_obj_clear_flag(ringObj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(ringObj, 2 * RING_R, 2 * RING_R);
  lv_obj_set_pos(ringObj, CX - RING_R, CY - RING_R);
  lv_obj_add_event_cb(ringObj, ringDraw, LV_EVENT_DRAW_MAIN, nullptr);
  irisR = 1;

  logoObj = lv_img_create(scr);
  lv_img_set_src(logoObj, logoSrc);
  lv_img_set_antialias(logoObj, false);
  lv_img_set_pivot(logoObj, w / 2, h / 2);
  lv_img_set_zoom(logoObj, baseZoom);
  lv_obj_set_pos(logoObj, CX - w / 2, CY - h / 2);
  lv_obj_set_style_img_opa(logoObj, LV_OPA_TRANSP, 0);

  wordObj = lv_label_create(scr);
  lv_label_set_recolor(wordObj, true);
  lv_label_set_text(wordObj, "VENT#ead9ff PUCK#");
  lv_obj_set_width(wordObj, LV_SIZE_CONTENT);
  lv_obj_align(wordObj, LV_ALIGN_TOP_MID, 0, 292);
  lv_obj_set_style_text_font(wordObj, &font_wordmark, 0);
  lv_obj_set_style_text_color(wordObj, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_align(wordObj, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_letter_space(wordObj, 3, 0);
  lv_obj_set_style_text_opa(wordObj, LV_OPA_TRANSP, 0);

  lv_anim_init(&bootAnim);
  lv_anim_set_var(&bootAnim, ringObj);
  lv_anim_set_exec_cb(&bootAnim, bootTick);
  lv_anim_set_values(&bootAnim, 0, TOTAL_MS);
  lv_anim_set_time(&bootAnim, TOTAL_MS);
  lv_anim_set_path_cb(&bootAnim, lv_anim_path_linear);
  lv_anim_set_ready_cb(&bootAnim, bootDone);
}

static void warmUp() {
  lv_obj_set_style_img_opa(logoObj, LV_OPA_COVER, 0);
  lv_obj_set_style_text_opa(wordObj, LV_OPA_COVER, 0);
  lv_refr_now(nullptr);
  lv_refr_now(nullptr);
  lv_obj_set_style_img_opa(logoObj, LV_OPA_TRANSP, 0);
  lv_obj_set_style_text_opa(wordObj, LV_OPA_TRANSP, 0);
  lv_refr_now(nullptr);
}

void bootShow() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
  uiBuild(scr);
  if (g_settings.bootAnimation) {
    buildBoot(scr);
    warmUp();
  }
}

void bootStart() {
  if (!logoObj) return;  // animation disabled
  bootStartMs = millis();
  lv_anim_start(&bootAnim);
  Serial.println("Boot animation started");
}

bool bootRunning() { return logoObj != nullptr; }
