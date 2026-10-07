#include <pebble.h>
#include "message_keys.auto.h"

#define JS_READY 2
#define PERSIST_GEO 1
#define PERSIST_UNIDAD 2
#define PERSIST_IDIOMA 3
#define NUM_ZONAS 5
#define NUM_PRECIOS 48
#define SIN_DATO 2147483647

static const char *ZONA_NOMBRES[NUM_ZONAS][2] = {
    {"Península", "Peninsula"},
    {"Canarias", "Canaries"},
    {"Baleares", "Balearics"},
    {"Ceuta", "Ceuta"},
    {"Melilla", "Melilla"}};

static Window *s_main_window;
static MenuLayer *s_menu;
static AppTimer *s_retry_timer;

static int s_geo;
static int s_unidad;
static int s_idioma;
static bool s_js_ready;
static bool s_tiene_datos;
static int s_hora_actual;
static int s_hora_min;
static int s_hora_max;
static int32_t s_precios[NUM_PRECIOS];
static uint8_t s_categoria[NUM_PRECIOS];
static int s_num_filas = 24;

static const GPathInfo INFO_ICONO_BARATA = {3, (GPoint[]){{0, 0}, {12, 0}, {6, 9}}};
static const GPathInfo INFO_ICONO_MEDIA = {4, (GPoint[]){{6, 0}, {12, 6}, {6, 12}, {0, 6}}};
static const GPathInfo INFO_ICONO_CARA = {3, (GPoint[]){{0, 9}, {12, 9}, {6, 0}}};
static GPath *s_icono_barata;
static GPath *s_icono_media;
static GPath *s_icono_cara;

static char s_header_buf[48];
static char s_title_buf[40];
static char s_sub_buf[48];

static const char *TXT(const char *es, const char *en)
{
  return s_idioma == 1 ? en : es;
}

static void poner_header(const char *text)
{
  snprintf(s_header_buf, sizeof(s_header_buf), "%s", text);
  if (s_menu)
  {
    menu_layer_reload_data(s_menu);
  }
}

static void mostrar_zona(void)
{
  snprintf(s_header_buf, sizeof(s_header_buf), "PVPC - %s", ZONA_NOMBRES[s_geo][s_idioma]);
  if (s_menu)
  {
    menu_layer_reload_data(s_menu);
  }
}

static void format_precio(char *buf, size_t len, int32_t value100)
{
  int32_t v = value100 < 0 ? -value100 : value100;
  if (s_unidad == 1)
  {
    snprintf(buf, len, "%s%ld,%02ld", value100 < 0 ? "-" : "",
             (long)(v / 100), (long)(v % 100));
  }
  else
  {
    snprintf(buf, len, "%s%ld,%05ld", value100 < 0 ? "-" : "",
             (long)(v / 100000), (long)(v % 100000));
  }
}

static const char *unidad_texto(void)
{
  return s_unidad == 1 ? "€/MWh" : "€/kWh";
}

static void pedir_precios(void)
{
  DictionaryIterator *iter;
  AppMessageResult res = app_message_outbox_begin(&iter);
  if (res != APP_MSG_OK)
  {
    poner_header(TXT("Sin conexión con el móvil", "No phone connection"));
    return;
  }
  dict_write_int32(iter, MESSAGE_KEY_REQ, 1);
  dict_write_int32(iter, MESSAGE_KEY_GEO, (int32_t)s_geo);
  res = app_message_outbox_send();
  if (res != APP_MSG_OK)
  {
    poner_header(TXT("Sin conexión con el móvil", "No phone connection"));
    return;
  }
  poner_header(TXT("Actualizando...", "Updating..."));
}

static void enviar_ajustes_a_js(void)
{
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK)
  {
    return;
  }
  dict_write_int32(iter, MESSAGE_KEY_GEO, (int32_t)s_geo);
  dict_write_int32(iter, MESSAGE_KEY_UNIDAD, (int32_t)s_unidad);
  dict_write_int32(iter, MESSAGE_KEY_IDIOMA, (int32_t)s_idioma);
  app_message_outbox_send();
}

static int cmp_int32(const void *a, const void *b)
{
  int32_t va = *(const int32_t *)a;
  int32_t vb = *(const int32_t *)b;
  return (va > vb) - (va < vb);
}

static void categorizar(int base)
{
  int32_t vals[24];
  int n = 0;
  for (int i = 0; i < 24; i++)
  {
    if (s_precios[base + i] != SIN_DATO)
    {
      vals[n++] = s_precios[base + i];
    }
  }
  if (n == 0)
  {
    return;
  }
  qsort(vals, n, sizeof(int32_t), cmp_int32);
  int tercio = (n + 2) / 3;
  int32_t umbral_barata = vals[tercio - 1];
  int32_t umbral_cara = vals[n - tercio];
  for (int i = 0; i < 24; i++)
  {
    if (s_precios[base + i] == SIN_DATO)
    {
      s_categoria[base + i] = 0;
    }
    else if (s_precios[base + i] <= umbral_barata)
    {
      s_categoria[base + i] = 1;
    }
    else if (s_precios[base + i] >= umbral_cara)
    {
      s_categoria[base + i] = 3;
    }
    else
    {
      s_categoria[base + i] = 2;
    }
  }
}

static void aplicar_datos(int hora, int hora_min, int hora_max)
{
  if (s_retry_timer)
  {
    app_timer_cancel(s_retry_timer);
    s_retry_timer = NULL;
  }
  s_hora_actual = hora;
  s_hora_min = hora_min;
  s_hora_max = hora_max;
  s_num_filas = 24;
  for (int i = 24; i < NUM_PRECIOS; i++)
  {
    if (s_precios[i] != SIN_DATO)
    {
      s_num_filas++;
    }
  }
  categorizar(0);
  categorizar(24);
  s_tiene_datos = true;
  mostrar_zona();
  menu_layer_set_selected_index(s_menu, MenuIndex(0, (uint16_t)s_hora_actual), MenuRowAlignCenter, false);
  menu_layer_reload_data(s_menu);
}

static void inbox_recibido(DictionaryIterator *iter, void *context)
{
  Tuple *t = dict_find(iter, MESSAGE_KEY_REQ);
  if (t && t->value->int32 == JS_READY)
  {
    s_js_ready = true;
    enviar_ajustes_a_js();
    return;
  }

  t = dict_find(iter, MESSAGE_KEY_ERR);
  if (t)
  {
    poner_header(t->value->cstring);
    return;
  }

  t = dict_find(iter, MESSAGE_KEY_PRICES);
  if (t && t->type == TUPLE_BYTE_ARRAY && t->length >= NUM_PRECIOS * (int)sizeof(int32_t))
  {
    memcpy(s_precios, t->value->data, NUM_PRECIOS * sizeof(int32_t));

    int hora = 0;
    int hora_min = 0;
    int hora_max = 0;
    t = dict_find(iter, MESSAGE_KEY_HORA);
    if (t)
    {
      hora = (int)t->value->int32;
    }
    t = dict_find(iter, MESSAGE_KEY_MINH);
    if (t)
    {
      hora_min = (int)t->value->int32;
    }
    t = dict_find(iter, MESSAGE_KEY_MAXH);
    if (t)
    {
      hora_max = (int)t->value->int32;
    }
    aplicar_datos(hora, hora_min, hora_max);
    return;
  }

  t = dict_find(iter, MESSAGE_KEY_IDIOMA);
  if (t)
  {
    int idioma = (int)t->value->int32;
    if (idioma == 0 || idioma == 1)
    {
      s_idioma = idioma;
      persist_write_int(PERSIST_IDIOMA, s_idioma);
      mostrar_zona();
    }
  }

  t = dict_find(iter, MESSAGE_KEY_GEO);
  if (t)
  {
    int geo = (int)t->value->int32;
    if (geo >= 0 && geo < NUM_ZONAS && geo != s_geo)
    {
      s_geo = geo;
      persist_write_int(PERSIST_GEO, s_geo);
      mostrar_zona();
      pedir_precios();
    }
  }

  t = dict_find(iter, MESSAGE_KEY_UNIDAD);
  if (t)
  {
    int unidad = (int)t->value->int32;
    if (unidad == 0 || unidad == 1)
    {
      s_unidad = unidad;
      persist_write_int(PERSIST_UNIDAD, s_unidad);
      if (s_menu)
      {
        menu_layer_reload_data(s_menu);
      }
    }
  }
}

static void outbox_fallado(DictionaryIterator *iterator, AppMessageResult reason, void *context)
{
  poner_header(TXT("Sin conexión con el móvil", "No phone connection"));
}

static void reintento_cb(void *context)
{
  s_retry_timer = NULL;
  if (!s_tiene_datos)
  {
    pedir_precios();
    s_retry_timer = app_timer_register(3000, reintento_cb, NULL);
  }
}

#ifdef PBL_ROUND
static int16_t prv_semiancho_cuerda(const Layer *cell_layer, int16_t y_local, int16_t alto)
{
  GRect scr = layer_get_bounds(window_get_root_layer(s_main_window));
  int16_t cx = scr.size.w / 2;
  int16_t cy = scr.size.h / 2;
  int32_t r2 = (int32_t)(cx - 4) * (cx - 4);
  GPoint org = layer_convert_point_to_screen(cell_layer, GPointZero);
  int32_t dy = (int32_t)org.y + y_local + alto / 2 - cy;
  if (dy < 0)
  {
    dy = -dy;
  }
  int32_t objetivo = r2 - dy * dy;
  if (objetivo <= 0)
  {
    return 0;
  }
  int32_t medio = 0;
  while ((medio + 1) * (medio + 1) <= objetivo)
  {
    medio++;
  }
  return (int16_t)medio;
}

static void prv_dibujar_linea_arco(GContext *ctx, const Layer *cell_layer, const char *texto,
                                   GFont fuente, int16_t y_local, int16_t alto, GColor color)
{
  int16_t medio = prv_semiancho_cuerda(cell_layer, y_local, alto);
  GRect cell = layer_get_bounds(cell_layer);
  GRect scr = layer_get_bounds(window_get_root_layer(s_main_window));
  GPoint org = layer_convert_point_to_screen(cell_layer, GPointZero);
  int16_t cx_local = scr.size.w / 2 - org.x;
  int16_t izq = cx_local - medio;
  int16_t der = cx_local + medio;
  if (izq < 0)
  {
    izq = 0;
  }
  if (der > cell.size.w)
  {
    der = cell.size.w;
  }
  if (der <= izq)
  {
    return;
  }
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, texto, fuente,
                     GRect(izq, y_local, der - izq, alto),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}
#endif

static void prv_menu_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data)
{
  int h = (int)cell_index->row;
  bool manana = h >= 24;
  int hh = manana ? h - 24 : h;
  bool es_actual = s_tiene_datos && !manana && h == s_hora_actual;
  bool es_min = s_tiene_datos && !manana && h == s_hora_min;
  bool es_max = s_tiene_datos && !manana && h == s_hora_max;
  bool con_dato = s_tiene_datos && s_precios[h] != SIN_DATO;

  char precio[16];
  if (con_dato)
  {
    format_precio(precio, sizeof(precio), s_precios[h]);
  }

#ifdef PBL_ROUND
  bool enfocada = menu_cell_layer_is_highlighted(cell_layer);
  if (enfocada)
  {
    snprintf(s_title_buf, sizeof(s_title_buf), "%s%s%02d:00 - %02d:00%s%s",
             manana ? TXT("Mañana ", "Tomorrow ") : "", es_actual ? "→ " : "", hh, (hh + 1) % 24,
             es_min ? TXT(" · mín", " · min") : "", es_max ? TXT(" · máx", " · max") : "");
    snprintf(s_sub_buf, sizeof(s_sub_buf), "%s %s",
             con_dato ? precio : TXT("Sin dato", "No data"), unidad_texto());
  }
  else
  {
    snprintf(s_title_buf, sizeof(s_title_buf), "%s%s%02d:00 %s%s%s",
             manana ? TXT("M ", "T ") : "", es_actual ? "→ " : "", hh,
             con_dato ? precio : TXT("Sin dato", "No data"),
             es_min ? TXT(" mín", " min") : "", es_max ? TXT(" máx", " max") : "");
  }
#else
  snprintf(s_title_buf, sizeof(s_title_buf), "%s%s%02d:00 - %02d:00%s%s",
           manana ? TXT("Mañana ", "Tomorrow ") : "", es_actual ? "→ " : "", hh, (hh + 1) % 24,
           es_min ? TXT(" · mín", " · min") : "", es_max ? TXT(" · máx", " · max") : "");
  snprintf(s_sub_buf, sizeof(s_sub_buf), "%s %s",
           con_dato ? precio : TXT("Sin dato", "No data"), unidad_texto());
#endif

  GColor fg_hora = GColorBlack;
  GColor fg_precio = GColorBlack;
#ifdef PBL_COLOR
  if (!con_dato)
  {
    fg_hora = GColorDarkGray;
    fg_precio = GColorDarkGray;
  }
  else if (s_categoria[h] == 1)
  {
    fg_hora = GColorDarkGreen;
    fg_precio = GColorDarkGreen;
  }
  else if (s_categoria[h] == 2)
  {
    fg_hora = GColorRajah;
    fg_precio = GColorRajah;
  }
  else if (s_categoria[h] == 3)
  {
    fg_hora = GColorDarkCandyAppleRed;
    fg_precio = GColorDarkCandyAppleRed;
  }
#endif
  if (menu_cell_layer_is_highlighted(cell_layer))
  {
    fg_hora = GColorWhite;
    fg_precio = GColorWhite;
  }

#ifdef PBL_ROUND
  if (enfocada)
  {
    prv_dibujar_linea_arco(ctx, cell_layer, s_title_buf, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                           12, 18, fg_hora);
    prv_dibujar_linea_arco(ctx, cell_layer, s_sub_buf, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                           30, 26, fg_precio);
  }
  else
  {
    prv_dibujar_linea_arco(ctx, cell_layer, s_title_buf, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                           3, 18, fg_precio);
  }
#else
  GRect bounds = layer_get_bounds(cell_layer);
  int16_t alto = bounds.size.h;
  int16_t alto_hora = (alto * 4) / 10;
  int x_texto = 4;
#ifndef PBL_COLOR
  if (s_tiene_datos && s_categoria[h] != 0)
  {
    bool destacada = menu_cell_layer_is_highlighted(cell_layer);
    GPath *icono = s_categoria[h] == 1 ? s_icono_barata : (s_categoria[h] == 2 ? s_icono_media : s_icono_cara);
    gpath_move_to(icono, GPoint(2, (alto_hora - 12) / 2));
    if (s_categoria[h] == 2)
    {
      graphics_context_set_stroke_color(ctx, destacada ? GColorWhite : GColorBlack);
      gpath_draw_outline(ctx, icono);
    }
    else
    {
      graphics_context_set_fill_color(ctx, destacada ? GColorWhite : GColorBlack);
      gpath_draw_filled(ctx, icono);
    }
    x_texto = 18;
  }
#endif
  graphics_context_set_text_color(ctx, fg_hora);
  graphics_draw_text(ctx, s_title_buf, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(x_texto, 0, bounds.size.w - x_texto - 4, alto_hora),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_context_set_text_color(ctx, fg_precio);
  graphics_draw_text(ctx, s_sub_buf, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(x_texto, alto_hora, bounds.size.w - x_texto - 4, alto - alto_hora),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
#endif
}

static int16_t prv_menu_cell_height(MenuLayer *menu_layer, MenuIndex *cell_index, void *data)
{
#ifdef PBL_ROUND
  return menu_layer_is_index_selected(menu_layer, cell_index)
             ? MENU_CELL_ROUND_FOCUSED_SHORT_CELL_HEIGHT
             : MENU_CELL_ROUND_UNFOCUSED_SHORT_CELL_HEIGHT;
#else
  return 44;
#endif
}

static uint16_t prv_menu_num_sections(MenuLayer *menu_layer, void *data)
{
  return 1;
}

static uint16_t prv_menu_num_rows(MenuLayer *menu_layer, uint16_t section_index, void *data)
{
  return (uint16_t)s_num_filas;
}

static int16_t prv_menu_header_height(MenuLayer *menu_layer, uint16_t section_index, void *data)
{
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void prv_menu_draw_header(GContext *ctx, const Layer *cell_layer, uint16_t section_index, void *data)
{
  GRect bounds = layer_get_bounds(cell_layer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_header_buf, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                     GRect(0, 0, bounds.size.w, MENU_CELL_BASIC_HEADER_HEIGHT),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void prv_main_window_load(Window *window)
{
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_menu = menu_layer_create(bounds);
  s_icono_barata = gpath_create(&INFO_ICONO_BARATA);
  s_icono_media = gpath_create(&INFO_ICONO_MEDIA);
  s_icono_cara = gpath_create(&INFO_ICONO_CARA);
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks){
                                             .get_num_sections = prv_menu_num_sections,
                                             .get_num_rows = prv_menu_num_rows,
                                             .get_cell_height = prv_menu_cell_height,
                                             .get_header_height = prv_menu_header_height,
                                             .draw_header = prv_menu_draw_header,
                                             .draw_row = prv_menu_draw_row,
                                         });
  menu_layer_set_click_config_onto_window(s_menu, window);
  layer_add_child(window_layer, menu_layer_get_layer(s_menu));
}

static void prv_main_window_unload(Window *window)
{
  gpath_destroy(s_icono_barata);
  gpath_destroy(s_icono_media);
  gpath_destroy(s_icono_cara);
  menu_layer_destroy(s_menu);
}

static void prv_init(void)
{
  s_geo = persist_exists(PERSIST_GEO) ? (int)persist_read_int(PERSIST_GEO) : 0;
  if (s_geo < 0 || s_geo >= NUM_ZONAS)
  {
    s_geo = 0;
  }
  s_unidad = persist_exists(PERSIST_UNIDAD) ? (int)persist_read_int(PERSIST_UNIDAD) : 0;
  if (s_unidad != 0 && s_unidad != 1)
  {
    s_unidad = 0;
  }
  s_idioma = persist_exists(PERSIST_IDIOMA) ? (int)persist_read_int(PERSIST_IDIOMA) : 0;
  if (s_idioma != 0 && s_idioma != 1)
  {
    s_idioma = 0;
  }

  s_main_window = window_create();
  window_set_window_handlers(s_main_window, (WindowHandlers){
                                                .load = prv_main_window_load,
                                                .unload = prv_main_window_unload,
                                            });

  app_message_register_inbox_received(inbox_recibido);
  app_message_register_outbox_failed(outbox_fallado);
  app_message_open(512, 64);
  s_retry_timer = app_timer_register(1500, reintento_cb, NULL);

  window_stack_push(s_main_window, true);
}

static void prv_deinit(void)
{
  window_destroy(s_main_window);
}

int main(void)
{
  prv_init();
  app_event_loop();
  prv_deinit();
}
