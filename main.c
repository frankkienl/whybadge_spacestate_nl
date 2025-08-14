#include "badgevms/wifi.h"
#include "curl/curl.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <badgevms/compositor.h>
#include <badgevms/event.h>
#include <badgevms/framebuffer.h>
#include <badgevms/process.h>

#include <dirent.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define RGB565(r, g, b)           ((((r) & 0x1F) << 11) | (((g) & 0x3F) << 5) | ((b) & 0x1F))
#define RGB888_TO_RGB565(r, g, b) RGB565(((r) * 31 + 127) / 255, ((g) * 63 + 127) / 255, ((b) * 31 + 127) / 255)

#define PIN_IMAGES_DIR             "APPS:[SPACESTATE_NL.IMAGES]"
#define PIN_GREEN                  "APPS:[SPACESTATE_NL.IMAGES]PIN_GREEN.PNG"
#define PIN_RED                    "APPS:[SPACESTATE_NL.IMAGES]PIN_RED.PNG"

#define pixelbar_url "https://spaceapi.pixelbar.nl/"
#define pixelbar_x 272
#define pixelbar_y 368

int render_png_to_framebuffer(
    uint16_t *framebuffer, int fb_width, int fb_height, char const *filename, int dest_x, int dest_y
);
int stbi_info(char const *filename, int *x, int *y, int *comp);
int render_png_with_alpha_scaled(
    uint16_t *framebuffer, int fb_width, int fb_height, char const *filename, int dest_x, int dest_y, int scale_factor
);

typedef struct {
    bool ackspace;
    bool awesomespace;
    bool bitlair;
    bool hack42;
    bool hackalot;
    bool hs_drenthe;
    bool hs_nijmegen;
    bool nurdspace;
    bool pixelbar;
    bool randomdata;
    bool revspace;
    bool space_leiden;
    bool hs_tdvenlo;
    bool techinc;
    bool tkkrlab;
} space_state_t;

typedef struct {
    uint16_t   *temp_framebuffer;
    uint16_t   *clean_background;
    int         fb_width;
    int         fb_height;
} app_state_t;

static space_state_t g_space_state = {0};
static app_state_t g_app_state = {0};

// For cURL response
typedef struct {
    char *memory;
    size_t size;
} MemoryStruct;

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, MemoryStruct *mem) {
    size_t realsize = size * nmemb;
    printf("Callback recieved %u bytes\n", realsize);

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) {
        printf("Not enough memory (realloc returned NULL)\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size              += realsize;
    mem->memory[mem->size]  = 0;

    return realsize;
}

bool get_space_state(char *space_url) {
    CURL *curl;
    CURLcode res;
    MemoryStruct chunk;
    chunk.memory = malloc(1);
    chunk.size   = 0;

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, space_url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "BadgeVMS-libcurl/1.0");
        curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 128);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            printf("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        } else {
            printf("Received %lu bytes:\n%s\n", (unsigned long)chunk.size, chunk.memory);
            // Check if hacker space is open
            if (strstr(chunk.memory, "\"open\":true") != NULL) {
                return true;
            }
        }

        curl_easy_cleanup(curl);
    }
    free(chunk.memory);
    return false;
}

void get_space_states(uint16_t *framebuffer) {
    // Pixelbar
    g_space_state.pixelbar = get_space_state(pixelbar_url);

    // Write results to framebuffer
    if (g_space_state.pixelbar) {
        render_png_with_alpha_scaled(framebuffer, g_app_state.fb_width, g_app_state.fb_height, PIN_GREEN, pixelbar_x, pixelbar_y, 1);
    } else {
        render_png_with_alpha_scaled(framebuffer, g_app_state.fb_width, g_app_state.fb_height, PIN_RED, pixelbar_x, pixelbar_y, 1);
    }
}

int main(int argc, char *argv[]) {
    printf("Space State NL app\n");

    wifi_connect();
    curl_global_init(0);

    // Create window / frame buffer
    window_size_t size;
    size.w = 720;
    size.h = 720;
    window_handle_t window      = window_create("Space State window", size, WINDOW_FLAG_FULLSCREEN);
    framebuffer_t  *framebuffer = window_framebuffer_create(window, size, BADGEVMS_PIXELFORMAT_RGB565);
    printf("Space State NL - created window\n");

    // Render background
    render_png_to_framebuffer(
        framebuffer->pixels,
        framebuffer->w,
        framebuffer->h,
        "APPS:[SPACESTATE_NL]BACKGROUND.PNG",
        0,
        0
    );
    printf("Space State NL - rendered background\n");

    g_app_state.fb_width = framebuffer->w;
    g_app_state.fb_height = framebuffer->h;
    g_app_state.clean_background = malloc(framebuffer->w * framebuffer->h * sizeof(uint16_t));
    memcpy(g_app_state.clean_background, framebuffer->pixels, framebuffer->w * framebuffer->h * sizeof(uint16_t));
    printf("Space State NL - saved background\n");

    uint32_t timestamp = 0;
    uint32_t interval = 10*1000;

    // Main loop
    while(true) {
        event_t e = window_event_poll(window, false, 0);
        if (e.type == EVENT_KEY_DOWN) {
            if (e.keyboard.scancode == KEY_SCANCODE_ESCAPE) {
                printf("Space State NL - ESCAPE KEY\n");
                break; //exit loop
            }
        }

        uint32_t current_time = time(NULL) * 1000;
        if (current_time - timestamp >= interval) {
            timestamp = current_time;
            get_space_states(framebuffer->pixels);
        }

        window_present(window, true, NULL, 0);
    }

    printf("Space State NL - END OF MAIN\n");
    return 0;
}
