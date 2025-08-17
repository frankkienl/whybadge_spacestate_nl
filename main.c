#include "badgevms/wifi.h"
#include "curl/curl.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h> // voor isspace()

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

int render_png_to_framebuffer(
    uint16_t *framebuffer, int fb_width, int fb_height, char const *filename, int dest_x, int dest_y
);
int stbi_info(char const *filename, int *x, int *y, int *comp);
int render_png_with_alpha_scaled(
    uint16_t *framebuffer, int fb_width, int fb_height, char const *filename, int dest_x, int dest_y, int scale_factor
);

// Data van 1 hacker space
typedef struct {
    char* display_name;
    const char* url;
    const int x;
    const int y;
    bool is_open;
    uint32_t last_checked;
} hacker_space_t;

// Enum van Nederlandse hacker spaces
typedef enum {
    ackspace,
    awesomespace,
    bitlair,
    hack42,
    hackalot,
    hs_drenthe,
    hs_nijmegen,
    nurdspace,
    pixelbar,
    randomdata,
    revspace,
    space_leiden,
    tdvenlo,
    techinc,
    tkkrlab,
    COUNT
} hacker_spaces_e;

#define NUM_HACKER_SPACES 15

typedef struct {
    hacker_space_t hackerspaces[NUM_HACKER_SPACES/*COUNT*/];
} hacker_spaces_t;

static hacker_spaces_t g_space_state = {
    .hackerspaces = {
        [ackspace] = { "ACKspace", "https://ackspace.nl/spaceAPI", 445, 555, false, 0 },
        [awesomespace] = { "AwesomeSpace", "https://state.awesomespace.nl", 345, 319, false, 0 },
        [bitlair] = { "Bitlair", "https://bitlair.nl/statejson.php", 378, 343, false, 0 },
        [hack42] = { "Hack42", "https://hack42.nl/spacestate/json.php", 438, 348, false, 0 },
        [hackalot] = { "Hackalot", "https://hackalot.nl/statejson", 390, 443, false, 0 },
        [hs_drenthe] = { "Hackerspace Drenthe", "https://mqtt.hackerspace-drenthe.nl/spaceapi", 527, 209, false, 0 },
        [hs_nijmegen] = { "Hackerspace Nijmegen", "https://state.hackerspacenijmegen.nl/state.json", 416, 380, false, 0 },
        [nurdspace] = { "NURDSpace", "https://space.nurdspace.nl/spaceapi/status.json", 385, 385, false, 0 }, 
        [pixelbar] = { "Pixelbar", "https://spaceapi.pixelbar.nl/", 272, 368, false, 0 },
        [randomdata] = { "RandomData", "", 326, 353, false, 0 },
        [revspace] = { "RevSpace", "https://revspace.nl/status/status.php", 234, 345, false, 0 },
        [space_leiden] = { "Space Leiden", "https://portal.spaceleiden.nl/api/public/status.json", 260, 320, false, 0 },
        [tdvenlo] = { "TDvenlo", "https://spaceapi.tdvenlo.nl/spaceapi.json", 458, 470, false, 0},
        [techinc] = { "TechInc", "", 290, 270, false, 0 },
        [tkkrlab] = { "TkkrLab", "https://spaceapi.tkkrlab.nl", 530, 310, false, 0} 
    }
};

typedef struct {
    uint16_t   *temp_framebuffer;
    uint16_t   *clean_background;
    int         fb_width;
    int         fb_height;
} app_state_t;

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

void remove_whitespace(char *str) {
    char *src = str;  // lezer
    char *dst = str;  // schrijver

    while (*src) {
        if (!isspace((unsigned char)*src)) {
            *dst++ = *src; // kopieer als het GEEN whitespace is
        }
        src++;
    }
    *dst = '\0'; // afsluiten
}

bool get_space_state(const char *space_url) {
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
            // Remove whitespace
            remove_whitespace(chunk.memory);
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

    uint32_t big_timestamp = 0;
    uint32_t big_interval = 30*1000;
    uint32_t small_timestamp = 0;
    uint32_t small_interval = 250;

    // Main loop
    int i = 0;
    while(true) {
        event_t e = window_event_poll(window, false, 0);
        if (e.type == EVENT_KEY_DOWN) {
            if (e.keyboard.scancode == KEY_SCANCODE_ESCAPE) {
                printf("Space State NL - ESCAPE KEY\n");
                break; //exit loop
            }
        }

        uint32_t current_time = time(NULL) * 1000;
        if (current_time - big_timestamp >= big_interval) {
            if (current_time - small_timestamp >= small_interval) {
                // Check Spaces
                printf("Space State NL - Checking %s %s", g_space_state.hackerspaces[i].display_name, "...");
                bool isOpen = get_space_state(g_space_state.hackerspaces[i].url);
                g_space_state.hackerspaces[i].is_open = isOpen;
                if (isOpen) {
                    printf("Space State NL - Checking %s %s", g_space_state.hackerspaces[i].display_name, " is OPEN");
                    render_png_with_alpha_scaled(framebuffer->pixels, g_app_state.fb_width, g_app_state.fb_height, PIN_GREEN, g_space_state.hackerspaces[i].x, g_space_state.hackerspaces[i].y, 1);
                } else {
                    printf("Space State NL - Checking %s %s", g_space_state.hackerspaces[i].display_name, " is CLOSED");
                    render_png_with_alpha_scaled(framebuffer->pixels, g_app_state.fb_width, g_app_state.fb_height, PIN_RED, g_space_state.hackerspaces[i].x, g_space_state.hackerspaces[i].y, 1);
                }
                i++;
                small_timestamp = current_time;
                if (i>=NUM_HACKER_SPACES) {
                    printf("Space State NL - Checked all, waiting for about 30 seconds");
                    i = 0;
                    big_timestamp = current_time;
                }
            }
        }


        window_present(window, true, NULL, 0);
    }

    printf("Space State NL - END OF MAIN\n");
    return 0;
}
