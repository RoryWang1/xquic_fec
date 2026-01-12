// Safe multi-threaded SDL2 player

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 480
#define FRAME_WIDTH 640
#define FRAME_HEIGHT 480
#define FRAME_SIZE (FRAME_WIDTH * FRAME_HEIGHT * 3 / 2)

typedef struct {
    char *fifo_path;
    SDL_Texture *texture;
    int is_running;
    
    // Thread synchronization
    unsigned char pixel_data[FRAME_SIZE];
    int has_new_frame;
    pthread_mutex_t mutex;
} VideoSource;

VideoSource msgLeft, msgRight;
SDL_Renderer *renderer = NULL;

// Global flag for graceful shutdown
static volatile int g_quit_requested = 0;

// Signal handler for graceful shutdown
void signal_handler(int signum) {
    printf("[DualPlayer] Received signal %d, shutting down gracefully...\n", signum);
    g_quit_requested = 1;
}

void* reader_thread(void *arg) {
    VideoSource *src = (VideoSource*)arg;
    int fd = -1;
    unsigned char local_buffer[FRAME_SIZE]; // Read into local stack buffer first

    printf("[DualPlayer] Thread started for %s\n", src->fifo_path);

    while (src->is_running) {
        if (fd < 0) {
            fd = open(src->fifo_path, O_RDONLY);
            if (fd < 0) { usleep(100000); continue; }
            printf("[DualPlayer] Opened %s (fd=%d)\n", src->fifo_path, fd);
        }

        size_t total_read = 0;
        int error_state = 0;
        
        while (total_read < FRAME_SIZE && src->is_running) {
            ssize_t n = read(fd, local_buffer + total_read, FRAME_SIZE - total_read);
            if (n > 0) {
                total_read += n;
            } else if (n <= 0) {
                 // EOF or Error
                 close(fd); fd = -1; error_state = 1; break;
            }
        }

        if (src->is_running && !error_state && total_read == FRAME_SIZE) {
            // Frame ready. Copy to shared buffer safely.
            pthread_mutex_lock(&src->mutex);
            memcpy(src->pixel_data, local_buffer, FRAME_SIZE);
            src->has_new_frame = 1;
            pthread_mutex_unlock(&src->mutex);
        }
    }
    if (fd >= 0) close(fd);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 3) return 1;

    msgLeft.fifo_path = argv[1];
    msgRight.fifo_path = argv[2];
    msgLeft.is_running = 1;
    msgRight.is_running = 1;
    pthread_mutex_init(&msgLeft.mutex, NULL);
    pthread_mutex_init(&msgRight.mutex, NULL);

    // Register signal handlers for graceful shutdown
    signal(SIGINT, signal_handler);   // Ctrl+C
    signal(SIGTERM, signal_handler);  // kill command

    if (SDL_Init(SDL_INIT_VIDEO) < 0) return 1;

    SDL_Window *window = SDL_CreateWindow("XQUIC Comparison (Single Window, Separate Decoders)",
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    if (!window) return 1;

    // Use Software Renderer if Hardware fails? No, Hardware is better but main thread only.
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) return 1;

    msgLeft.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, FRAME_WIDTH, FRAME_HEIGHT);
    msgRight.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, FRAME_WIDTH, FRAME_HEIGHT);

    pthread_t threadL, threadR;
    pthread_create(&threadL, NULL, reader_thread, &msgLeft);
    pthread_create(&threadR, NULL, reader_thread, &msgRight);

    SDL_Event e;
    int quit = 0;
    while (!quit && !g_quit_requested) {
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT) quit = 1;
        }

        // Texture Update Phase (Main Thread)
        // Left
        pthread_mutex_lock(&msgLeft.mutex);
        if (msgLeft.has_new_frame) {
            SDL_UpdateTexture(msgLeft.texture, NULL, msgLeft.pixel_data, FRAME_WIDTH);
            msgLeft.has_new_frame = 0;
        }
        pthread_mutex_unlock(&msgLeft.mutex);

        // Right
        pthread_mutex_lock(&msgRight.mutex);
        if (msgRight.has_new_frame) {
            SDL_UpdateTexture(msgRight.texture, NULL, msgRight.pixel_data, FRAME_WIDTH);
            msgRight.has_new_frame = 0;
        }
        pthread_mutex_unlock(&msgRight.mutex);

        // Render Phase
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        SDL_Rect leftRect = {0, 0, FRAME_WIDTH, FRAME_HEIGHT};
        SDL_RenderCopy(renderer, msgLeft.texture, NULL, &leftRect);

        SDL_Rect rightRect = {FRAME_WIDTH, 0, FRAME_WIDTH, FRAME_HEIGHT};
        SDL_RenderCopy(renderer, msgRight.texture, NULL, &rightRect);

        SDL_RenderPresent(renderer);
    }

    msgLeft.is_running = 0;
    msgRight.is_running = 0;
    pthread_join(threadL, NULL);
    pthread_join(threadR, NULL);

    SDL_DestroyTexture(msgLeft.texture);
    SDL_DestroyTexture(msgRight.texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
