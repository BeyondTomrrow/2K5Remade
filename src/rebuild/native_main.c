/* First native reconstruction milestone.  This executable never executes an XBE
   and never launches an emulator.  It mounts the user's local extraction and
   provides a small Windows/game-loop foundation for the C rewrite. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "legal_texture.h"

/* External declarations for the generalized HITX scanner and viewer */
extern void scan_hitx_blocks(void);
extern uint32_t* load_texture_by_index(int index, unsigned *w, unsigned *h);
extern int get_total_texture_count(void);

static uint32_t *current_texture_pixels = NULL;
static unsigned current_width, current_height;
static int current_texture_index = 0;
static int total_discovered_textures = 1;

typedef struct MountedGame {
    int ready;
    uint64_t xbe_bytes;
    uint64_t archive_bytes;
    unsigned archives;
} MountedGame;

static MountedGame g_game;
static float g_ball_x = 0.50f, g_ball_y = 0.50f;

static uint64_t file_size(const char *path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data)) return 0;
    return ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
}

static void mount_local_game(void) {
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s/original/default.xbe", NFL2K5_PROJECT_ROOT);
    g_game.xbe_bytes = file_size(path);
    for (char name = '0'; name <= '9'; ++name) {
        snprintf(path, sizeof path, "%s/original/vc_53450030/%c", NFL2K5_PROJECT_ROOT, name);
        uint64_t bytes = file_size(path);
        if (bytes) { g_game.archives++; g_game.archive_bytes += bytes; }
    }
    for (char name = 'A'; name <= 'F'; ++name) {
        snprintf(path, sizeof path, "%s/original/vc_53450030/%c", NFL2K5_PROJECT_ROOT, name);
        uint64_t bytes = file_size(path);
        if (bytes) { g_game.archives++; g_game.archive_bytes += bytes; }
    }
    g_game.ready = g_game.xbe_bytes > 0 && g_game.archives == 16;
}

static void text(HDC dc, int x, int y, COLORREF color, const char *value) {
    SetTextColor(dc, color); SetBkMode(dc, TRANSPARENT); TextOutA(dc, x, y, value, (int)strlen(value));
}

static void draw_game(HWND window, HDC dc) {
    RECT area; GetClientRect(window, &area);
    int w = area.right, h = area.bottom;
    
    if (current_texture_pixels) {
        FillRect(dc, &area, (HBRUSH)GetStockObject(BLACK_BRUSH));
        BITMAPINFO info={0}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=current_width; info.bmiHeader.biHeight=-(LONG)current_height;
        info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
        int dh=h-36,dw=dh*4/3; if(dw>w){dw=w;dh=dw*3/4;}
        SetStretchBltMode(dc,HALFTONE);
        StretchDIBits(dc,(w-dw)/2,(h-36-dh)/2,dw,dh,0,0,current_width,current_height,current_texture_pixels,&info,DIB_RGB_COLORS,SRCCOPY);
        
        char label[128];
        snprintf(label, sizeof label, "Viewing Texture %d of %d | Left/Right Arrows to navigate | Esc: close", 
                 current_texture_index + 1, total_discovered_textures);
        text(dc,16,h-26,RGB(255,255,255),label);
        return;
    }
    
    HBRUSH turf = CreateSolidBrush(RGB(20, 105, 48));
    FillRect(dc, &area, turf); DeleteObject(turf);
    HPEN white = CreatePen(PS_SOLID, 3, RGB(238, 242, 238));
    SelectObject(dc, white);
    for (int i = 1; i < 10; ++i) {
        int x = i * w / 10; MoveToEx(dc, x, 70, NULL); LineTo(dc, x, h - 40);
    }
    Rectangle(dc, 12, 70, w - 12, h - 40); DeleteObject(white);
    HBRUSH ball = CreateSolidBrush(RGB(120, 58, 20));
    int bx = (int)(g_ball_x * (w - 60)) + 30, by = (int)(g_ball_y * (h - 150)) + 100;
    Ellipse(dc, bx - 14, by - 9, bx + 14, by + 9); DeleteObject(ball);
    HFONT title = CreateFontA(28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Arial");
    HFONT normal = CreateFontA(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Arial");
    SelectObject(dc, title); text(dc, 20, 18, RGB(255,255,255), "NFL 2K5 — Native C Reconstruction");
    SelectObject(dc, normal);
    char status[256];
    if (g_game.ready) snprintf(status, sizeof status,
        "Local extraction mounted: default.xbe + %u game archives (%.2f GB)",
        g_game.archives, (double)g_game.archive_bytes / 1073741824.0);
    else snprintf(status, sizeof status, "Game data missing: place extracted files in original/");
    text(dc, 22, 48, RGB(255, 232, 110), status);
    text(dc, 22, h - 30, RGB(255,255,255),
        "Native executable proof-of-concept — arrows/WASD move the ball. Asset decoders and gameplay are next.");
    DeleteObject(title); DeleteObject(normal);
}

static LRESULT CALLBACK window_proc(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) DestroyWindow(window);
        
        /* Texture Viewer Navigation */
        if (wp == VK_RIGHT) {
            if (total_discovered_textures > 0) {
                if (current_texture_pixels) free(current_texture_pixels);
                current_texture_index = (current_texture_index + 1) % total_discovered_textures;
                current_texture_pixels = load_texture_by_index(current_texture_index, &current_width, &current_height);
                InvalidateRect(window, NULL, TRUE);
            }
        } 
        else if (wp == VK_LEFT) {
            if (total_discovered_textures > 0) {
                if (current_texture_pixels) free(current_texture_pixels);
                current_texture_index = (current_texture_index - 1 + total_discovered_textures) % total_discovered_textures;
                current_texture_pixels = load_texture_by_index(current_texture_index, &current_width, &current_height);
                InvalidateRect(window, NULL, TRUE);
            }
        }
        /* Fallback ball movement if no textures loaded */
        else {
            if (wp == 'A') g_ball_x -= .025f;
            if (wp == 'D') g_ball_x += .025f;
            if (wp == 'W') g_ball_y -= .025f;
            if (wp == 'S') g_ball_y += .025f;
            if (g_ball_x < 0) g_ball_x = 0; if (g_ball_x > 1) g_ball_x = 1;
            if (g_ball_y < 0) g_ball_y = 0; if (g_ball_y > 1) g_ball_y = 1;
            InvalidateRect(window, NULL, TRUE);
        }
        return 0;
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(window, &ps); draw_game(window, dc); EndPaint(window, &ps); return 0; }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(window, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show) {
    (void)previous; mount_local_game();
    
    /* Command Line Integration for Bulk Export */
    if (command && strstr(command, "--export-all-textures")) {
        scan_hitx_blocks();
        printf("Exported valid P8 textures to analysis/textures/\n");
        return 0;
    }

    /* Initialize Viewer with first texture (or legal page fallback) */
    total_discovered_textures = get_total_texture_count();
    if (total_discovered_textures == 0) {
        total_discovered_textures = 1; /* Fallback to standard legal texture */
    }
    current_texture_pixels = load_texture_by_index(current_texture_index, &current_width, &current_height);

    if (command && strstr(command, "--export-legal")) {
        if(!current_texture_pixels) return 2;
        char path[1024]; snprintf(path,sizeof path,"%s/analysis/legalpage.bmp",NFL2K5_PROJECT_ROOT);
        FILE *f=fopen(path,"wb"); if(!f){free(current_texture_pixels);return 3;}
        BITMAPFILEHEADER fh={0}; BITMAPINFOHEADER ih={0};
        fh.bfType=0x4D42;fh.bfOffBits=sizeof fh+sizeof ih;fh.bfSize=fh.bfOffBits+current_width*current_height*4;
        ih.biSize=sizeof ih;ih.biWidth=current_width;ih.biHeight=-(LONG)current_height;ih.biPlanes=1;ih.biBitCount=32;
        int ok=fwrite(&fh,sizeof fh,1,f)==1 && fwrite(&ih,sizeof ih,1,f)==1 && fwrite(current_texture_pixels,4,current_width*current_height,f)==current_width*current_height;
        fclose(f);free(current_texture_pixels);return ok?0:4;
    }

    WNDCLASSA cls = {0}; cls.hInstance = instance; cls.lpszClassName = "NFL2K5NativeRebuild";
    cls.lpfnWndProc = window_proc; cls.hCursor = LoadCursor(NULL, IDC_ARROW); cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassA(&cls);
    HWND window = CreateWindowExA(0, cls.lpszClassName, "NFL 2K5 Native Reconstruction", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800, NULL, NULL, instance, NULL);
    if (!window) return 1;
    MSG msg; while (GetMessageA(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    
    if (current_texture_pixels) free(current_texture_pixels);
    return 0;
}