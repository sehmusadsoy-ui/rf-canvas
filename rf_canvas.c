#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <targets/f7/furi_hal/furi_hal_subghz.h>

#define GRID_W 128
#define GRID_H 44

#define FREQ_COUNT 3
#define CALIBRATION_SAMPLES 6

#define HIT_HOLD_TICKS 18
#define RELEASE_DELTA 5

typedef enum {
    ViewAnalyze = 0,
    ViewArt,
    ViewHybrid,
    ViewCount
} ViewMode;

typedef enum {
    SensHigh = 0,
    SensMedium,
    SensLow,
    SensCount
} Sensitivity;

typedef struct {
    uint8_t grid[GRID_W][GRID_H];
    uint8_t next_grid[GRID_W][GRID_H];

    bool running;
    bool auto_scan;

    ViewMode view_mode;
    Sensitivity sensitivity;

    uint8_t freq_index;

    int16_t rssi;
    int16_t delta;

    int16_t baseline[FREQ_COUNT];
    int16_t peak[FREQ_COUNT];

    int32_t calibration_sum[FREQ_COUNT];
    uint8_t calibration_count[FREQ_COUNT];
    bool calibrated[FREQ_COUNT];

    bool signal_active[FREQ_COUNT];

    uint32_t hits[FREQ_COUNT];
    uint32_t total_hits;

    uint32_t last_hit_frequency;
    int16_t last_hit_rssi;
    int16_t last_hit_delta;

    uint8_t hit_hold;

    uint32_t generation;

    FuriMutex* mutex;
} RfCanvasApp;

static const uint32_t frequencies[FREQ_COUNT] = {
    315000000,
    433920000,
    868350000,
};

static int16_t get_hit_threshold(Sensitivity sensitivity) {
    switch(sensitivity) {
    case SensHigh:
        return 8;
    case SensMedium:
        return 12;
    case SensLow:
        return 18;
    default:
        return 12;
    }
}

static const char* get_sensitivity_name(Sensitivity sensitivity) {
    switch(sensitivity) {
    case SensHigh:
        return "HIGH";
    case SensMedium:
        return "MED";
    case SensLow:
        return "LOW";
    default:
        return "?";
    }
}

static uint8_t count_neighbors(RfCanvasApp* app, int x, int y) {
    uint8_t count = 0;

    for(int dx = -1; dx <= 1; dx++) {
        for(int dy = -1; dy <= 1; dy++) {
            if(dx == 0 && dy == 0) continue;

            int nx = x + dx;
            int ny = y + dy;

            if(nx >= 0 && nx < GRID_W &&
               ny >= 0 && ny < GRID_H) {
                count += app->grid[nx][ny];
            }
        }
    }

    return count;
}

static void life_step(RfCanvasApp* app) {
    for(int x = 0; x < GRID_W; x++) {
        for(int y = 0; y < GRID_H; y++) {
            uint8_t neighbors = count_neighbors(app, x, y);

            if(app->grid[x][y]) {
                app->next_grid[x][y] =
                    (neighbors == 2 || neighbors == 3);
            } else {
                app->next_grid[x][y] =
                    (neighbors == 3);
            }
        }
    }

    memcpy(
        app->grid,
        app->next_grid,
        sizeof(app->grid));

    memset(
        app->next_grid,
        0,
        sizeof(app->next_grid));

    app->generation++;
}

static void add_block(RfCanvasApp* app, int x, int y) {
    if(x < 0 || y < 0 ||
       x + 1 >= GRID_W ||
       y + 1 >= GRID_H) {
        return;
    }

    app->grid[x][y] = 1;
    app->grid[x + 1][y] = 1;
    app->grid[x][y + 1] = 1;
    app->grid[x + 1][y + 1] = 1;
}

static void add_glider(RfCanvasApp* app, int x, int y) {
    if(x < 0 || y < 0 ||
       x + 2 >= GRID_W ||
       y + 2 >= GRID_H) {
        return;
    }

    app->grid[x + 1][y] = 1;
    app->grid[x + 2][y + 1] = 1;
    app->grid[x][y + 2] = 1;
    app->grid[x + 1][y + 2] = 1;
    app->grid[x + 2][y + 2] = 1;
}

static void add_rf_art(RfCanvasApp* app, int16_t delta) {
    int gliders = 2;
    int blocks = 1;

    if(delta >= 15) {
        gliders = 3;
        blocks = 2;
    }

    if(delta >= 25) {
        gliders = 5;
        blocks = 3;
    }

    if(delta >= 40) {
        gliders = 8;
        blocks = 5;
    }

    if(delta >= 55) {
        gliders = 12;
        blocks = 7;
    }

    for(int i = 0; i < gliders; i++) {
        int x = 2 + (rand() % (GRID_W - 8));
        int y = 2 + (rand() % (GRID_H - 8));

        add_glider(app, x, y);
    }

    for(int i = 0; i < blocks; i++) {
        int x = 2 + (rand() % (GRID_W - 5));
        int y = 2 + (rand() % (GRID_H - 5));

        add_block(app, x, y);
    }
}

static void tune_frequency(uint32_t frequency) {
    furi_hal_subghz_idle();
    furi_hal_subghz_set_frequency_and_path(frequency);
    furi_hal_subghz_rx();
}

static void draw_art(Canvas* canvas, RfCanvasApp* app) {
    for(int x = 0; x < GRID_W; x++) {
        for(int y = 0; y < GRID_H; y++) {
            if(app->grid[x][y]) {
                canvas_draw_dot(canvas, x, y);
            }
        }
    }
}

static void draw_analyze(Canvas* canvas, RfCanvasApp* app) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "RF CANVAS ANALYZE");

    canvas_set_font(canvas, FontSecondary);

    char text[32];

    uint32_t frequency =
        frequencies[app->freq_index];

    snprintf(
        text,
        sizeof(text),
        "F %lu.%03lu MHz",
        (unsigned long)(frequency / 1000000),
        (unsigned long)((frequency / 1000) % 1000));

    canvas_draw_str(canvas, 2, 21, text);

    if(!app->calibrated[app->freq_index]) {
        snprintf(
            text,
            sizeof(text),
            "CAL %u/%u",
            app->calibration_count[app->freq_index],
            CALIBRATION_SAMPLES);

        canvas_draw_str(canvas, 2, 31, text);
    } else {
        snprintf(
            text,
            sizeof(text),
            "RSSI %d  B %d",
            app->rssi,
            app->baseline[app->freq_index]);

        canvas_draw_str(canvas, 2, 31, text);

        snprintf(
            text,
            sizeof(text),
            "D %+d  PK %d",
            app->delta,
            app->peak[app->freq_index]);

        canvas_draw_str(canvas, 2, 41, text);
    }

    snprintf(
        text,
        sizeof(text),
        "HIT %lu  %s",
        (unsigned long)app->total_hits,
        app->auto_scan ? "AUTO" : "MAN");

    canvas_draw_str(canvas, 2, 52, text);

    snprintf(
        text,
        sizeof(text),
        "SENS %s",
        get_sensitivity_name(app->sensitivity));

    canvas_draw_str(canvas, 2, 62, text);
}

static void draw_art_view(Canvas* canvas, RfCanvasApp* app) {
    draw_art(canvas, app);

    canvas_draw_line(
        canvas,
        0,
        46,
        127,
        46);

    canvas_set_font(canvas, FontSecondary);

    char text[32];

    if(app->hit_hold > 0) {
        uint32_t f =
            app->last_hit_frequency;

        snprintf(
            text,
            sizeof(text),
            "HIT %lu.%03lu %d",
            (unsigned long)(f / 1000000),
            (unsigned long)((f / 1000) % 1000),
            app->last_hit_rssi);
    } else {
        uint32_t f =
            frequencies[app->freq_index];

        snprintf(
            text,
            sizeof(text),
            "%lu.%03lu %d H%lu",
            (unsigned long)(f / 1000000),
            (unsigned long)((f / 1000) % 1000),
            app->rssi,
            (unsigned long)app->total_hits);
    }

    canvas_draw_str(canvas, 1, 61, text);
}

static void draw_hybrid(Canvas* canvas, RfCanvasApp* app) {
    draw_art(canvas, app);

    canvas_draw_line(
        canvas,
        0,
        44,
        127,
        44);

    canvas_set_font(canvas, FontSecondary);

    char line1[32];
    char line2[32];

    uint32_t f =
        frequencies[app->freq_index];

    if(app->hit_hold > 0) {
        snprintf(
            line1,
            sizeof(line1),
            "HIT %lu.%03lu %d",
            (unsigned long)(app->last_hit_frequency / 1000000),
            (unsigned long)((app->last_hit_frequency / 1000) % 1000),
            app->last_hit_rssi);
    } else {
        snprintf(
            line1,
            sizeof(line1),
            "%lu.%03lu R%d D%+d",
            (unsigned long)(f / 1000000),
            (unsigned long)((f / 1000) % 1000),
            app->rssi,
            app->delta);
    }

    snprintf(
        line2,
        sizeof(line2),
        "%s %s H%lu",
        app->auto_scan ? "AUTO" : "MAN",
        get_sensitivity_name(app->sensitivity),
        (unsigned long)app->total_hits);

    canvas_draw_str(canvas, 1, 53, line1);
    canvas_draw_str(canvas, 1, 63, line2);
}

static void render_callback(Canvas* canvas, void* ctx) {
    RfCanvasApp* app = ctx;

    furi_mutex_acquire(
        app->mutex,
        FuriWaitForever);

    canvas_clear(canvas);

    switch(app->view_mode) {
    case ViewAnalyze:
        draw_analyze(canvas, app);
        break;

    case ViewArt:
        draw_art_view(canvas, app);
        break;

    case ViewHybrid:
        draw_hybrid(canvas, app);
        break;

    default:
        break;
    }

    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* input_event, void* ctx) {
    FuriMessageQueue* queue = ctx;

    furi_message_queue_put(
        queue,
        input_event,
        0);
}

static void previous_frequency(RfCanvasApp* app) {
    if(app->freq_index == 0) {
        app->freq_index =
            FREQ_COUNT - 1;
    } else {
        app->freq_index--;
    }

    tune_frequency(
        frequencies[app->freq_index]);
}

static void next_frequency(RfCanvasApp* app) {
    app->freq_index++;

    if(app->freq_index >= FREQ_COUNT) {
        app->freq_index = 0;
    }

    tune_frequency(
        frequencies[app->freq_index]);
}

int32_t rf_canvas_app(void* p) {
    UNUSED(p);

    RfCanvasApp* app =
        malloc(sizeof(RfCanvasApp));

    memset(
        app,
        0,
        sizeof(RfCanvasApp));

    app->running = true;
    app->auto_scan = true;
    app->view_mode = ViewHybrid;
    app->sensitivity = SensMedium;
    app->freq_index = 0;
    app->rssi = -120;
    app->delta = 0;
    app->last_hit_rssi = -120;

    app->mutex =
        furi_mutex_alloc(
            FuriMutexTypeNormal);

    for(int i = 0; i < FREQ_COUNT; i++) {
        app->baseline[i] = -110;
        app->peak[i] = -127;
        app->calibrated[i] = false;
        app->signal_active[i] = false;
    }

    add_glider(app, 15, 10);
    add_glider(app, 55, 22);
    add_block(app, 100, 18);

    furi_hal_subghz_reset();
    furi_hal_subghz_idle();

    tune_frequency(
        frequencies[0]);

    FuriMessageQueue* queue =
        furi_message_queue_alloc(
            16,
            sizeof(InputEvent));

    ViewPort* viewport =
        view_port_alloc();

    view_port_draw_callback_set(
        viewport,
        render_callback,
        app);

    view_port_input_callback_set(
        viewport,
        input_callback,
        queue);

    Gui* gui =
        furi_record_open(
            RECORD_GUI);

    gui_add_view_port(
        gui,
        viewport,
        GuiLayerFullscreen);

    InputEvent event;

    uint32_t life_frame = 0;

    while(app->running) {

        while(
            furi_message_queue_get(
                queue,
                &event,
                0) == FuriStatusOk) {

            if(event.type != InputTypePress) {
                continue;
            }

            if(event.key == InputKeyBack) {
                app->running = false;
                break;
            }

            furi_mutex_acquire(
                app->mutex,
                FuriWaitForever);

            if(event.key == InputKeyOk) {
                app->view_mode++;

                if(app->view_mode >= ViewCount) {
                    app->view_mode = ViewAnalyze;
                }
            }

            else if(event.key == InputKeyUp) {
                app->auto_scan =
                    !app->auto_scan;
            }

            else if(event.key == InputKeyDown) {
                app->sensitivity++;

                if(app->sensitivity >= SensCount) {
                    app->sensitivity = SensHigh;
                }
            }

            else if(event.key == InputKeyLeft) {
                app->auto_scan = false;
                app->hit_hold = 0;

                previous_frequency(app);
            }

            else if(event.key == InputKeyRight) {
                app->auto_scan = false;
                app->hit_hold = 0;

                next_frequency(app);
            }

            furi_mutex_release(app->mutex);
        }

        if(!app->running) {
            break;
        }

        furi_delay_ms(70);

        float measured =
            furi_hal_subghz_get_rssi();

        int16_t current =
            (int16_t)measured;

        furi_mutex_acquire(
            app->mutex,
            FuriWaitForever);

        uint8_t i =
            app->freq_index;

        app->rssi = current;

        if(!app->calibrated[i]) {
            app->calibration_sum[i] +=
                current;

            app->calibration_count[i]++;

            if(
                app->calibration_count[i] >=
                CALIBRATION_SAMPLES) {

                app->baseline[i] =
                    app->calibration_sum[i] /
                    CALIBRATION_SAMPLES;

                app->calibrated[i] =
                    true;

                app->peak[i] =
                    current;
            }

        } else {

            if(current > app->peak[i]) {
                app->peak[i] =
                    current;
            }

            app->delta =
                current -
                app->baseline[i];

            int16_t hit_threshold =
                get_hit_threshold(
                    app->sensitivity);

            if(
                !app->signal_active[i] &&
                app->delta >= hit_threshold) {

                app->signal_active[i] = true;

                app->hits[i]++;
                app->total_hits++;

                app->last_hit_frequency =
                    frequencies[i];

                app->last_hit_rssi =
                    current;

                app->last_hit_delta =
                    app->delta;

                app->hit_hold =
                    HIT_HOLD_TICKS;

                add_rf_art(
                    app,
                    app->delta);
            }

            if(
                app->signal_active[i] &&
                app->delta <= RELEASE_DELTA) {

                app->signal_active[i] = false;
            }

            if(
                !app->signal_active[i] &&
                app->delta < RELEASE_DELTA) {

                app->baseline[i] =
                    (
                        app->baseline[i] * 15 +
                        current
                    ) / 16;
            }
        }

        life_frame++;

        if(life_frame >= 3) {
            life_step(app);
            life_frame = 0;
        }

        bool hold_frequency = false;

        if(app->hit_hold > 0) {
            app->hit_hold--;
            hold_frequency = true;
        }

        furi_mutex_release(
            app->mutex);

        view_port_update(
            viewport);

        if(
            app->auto_scan &&
            !hold_frequency) {

            app->freq_index++;

            if(app->freq_index >= FREQ_COUNT) {
                app->freq_index = 0;
            }

            tune_frequency(
                frequencies[app->freq_index]);
        }
    }

    furi_hal_subghz_idle();
    furi_hal_subghz_sleep();

    gui_remove_view_port(
        gui,
        viewport);

    view_port_free(
        viewport);

    furi_record_close(
        RECORD_GUI);

    furi_message_queue_free(
        queue);

    furi_mutex_free(
        app->mutex);

    free(app);

    return 0;
}
