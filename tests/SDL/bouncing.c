// This is a modified version of:
// ==============================
// Bouncing by Ricardo Cruz <rick2@aeiou.pt>
// http://rpmcruz.planetaclix.pt/
// Download link: http://rpmcruz.planetaclix.pt/hacks/bouncing.tgz

// includes
#include <math.h>

#include <SDL.h>

// definitions
#define SCREEN_WIDTH 864
#define SCREEN_HEIGHT 480
#define X 0
#define Y 1
#define GRAVITY 1
#define FRICTION 0.95
#define NORMAL_DELAY 42
#define ANIM_DELAY 1000
#define TOTAL_FRAMES 8
#define COLORKEY 255, 0, 255
#define BGCOLOR 200, 200, 200
#define FALSE 0
#define TRUE  1

void rungame();
void keyEvents(float lastTick);
Uint32 getpixel(SDL_Surface *surface, int x, int y);
void parseCommandLineOptions(int *showFullscreen, int argc, char **argv);
void blitSwitchFullscreenButton();
void switchFullscreenMode();
int pressedRect(SDL_Event *event, SDL_Rect *rect);
void blitStopRespondingButton();
void stopResponding();

int done;
float f_vel[2];
int i_pos[2];
int i_oldPos[2];
int i_mouseMargin[2];
int i_mouseMov[2];
int b_inMove;
SDL_Surface *s_surface;
SDL_Surface *s_switchFullscreen;
SDL_Surface *s_stopResponding;
SDL_Surface *s_screen;
SDL_Rect r_switchFullscreenButton;
SDL_Rect r_stopRespondingButton;
int showFullscreen;

int main(int argc, char **argv)
{

    printf("Command line options: \n");
    printf("--fullscreen   Use fullscreen mode\n\n");

    if(SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        fprintf(stderr, "Error: unable to initialize video: %s\n", SDL_GetError());
        return 1;
    }

    parseCommandLineOptions(&showFullscreen, argc, argv);

    if (showFullscreen) {
        s_screen = SDL_SetVideoMode(SCREEN_WIDTH, SCREEN_HEIGHT, 16, SDL_SWSURFACE | SDL_FULLSCREEN);
        printf("Using fullscreen mode.\n");
    } else {
        s_screen = SDL_SetVideoMode(SCREEN_WIDTH, SCREEN_HEIGHT, 16, SDL_SWSURFACE);
        printf("Using windowed mode.\n");
    }

    if(s_screen == NULL)
    {
        fprintf(stderr, "Error: unable to set video mode: %s\n", SDL_GetError());
        return 1;
    }

    SDL_WM_SetCaption("Bouncing", "Bouncing");

    rungame();

    SDL_Quit();
    return 0;
}

void parseCommandLineOptions(int *showFullscreen, int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--fullscreen") == 0) {
        *showFullscreen = 1;
    } else {
        *showFullscreen = 0;
    }
}

void rungame()
{
    i_oldPos[X] = i_pos[X] = 40;
    i_oldPos[Y] = i_pos[Y] = 20;
    f_vel[X] = f_vel[Y] = 0;

    float f_lastTick = -1;
    float f_ticksDiff;

    float f_animDelay = SDL_GetTicks();
    int i_frameNb = 0;

    b_inMove = TRUE;

    done = 0;

    s_surface = SDL_LoadBMP("bouncing.bmp");
    if (s_surface == NULL) {
        fprintf(stderr, "Error: 'bouncing.bmp' could not be open: %s\n", SDL_GetError());
        done = 1;
    }

    s_switchFullscreen = SDL_LoadBMP("switch_fullscreen.bmp");
    if (s_switchFullscreen == NULL) {
        fprintf(stderr, "Error: 'switch_fullscreen.bmp' could not be open: %s\n", SDL_GetError());
        done = 1;
    }

    r_switchFullscreenButton.x = 10;
    r_switchFullscreenButton.y = 10;
    r_switchFullscreenButton.w = s_switchFullscreen->w;
    r_switchFullscreenButton.h = s_switchFullscreen->h;

    s_stopResponding = SDL_LoadBMP("stop_responding.bmp");
    if (s_stopResponding == NULL) {
        fprintf(stderr, "Error: 'stop_responding.bmp' could not be open: %s\n", SDL_GetError());
        done = 1;
    }

    r_stopRespondingButton.x = r_switchFullscreenButton.x + r_switchFullscreenButton.w + 15;
    r_stopRespondingButton.y = 10;
    r_stopRespondingButton.w = s_stopResponding->w;
    r_stopRespondingButton.h = s_stopResponding->h;

    if(SDL_SetColorKey(s_surface, SDL_SRCCOLORKEY | SDL_RLEACCEL, SDL_MapRGB(s_surface->format, COLORKEY)) == -1)
        fprintf(stderr, "Warning: colorkey will not be used, reason: %s\n", SDL_GetError());


    while(done == 0)
    {
        f_ticksDiff = SDL_GetTicks() - f_lastTick;
        f_ticksDiff /= NORMAL_DELAY;
        if(f_lastTick == -1)
            f_ticksDiff = 0;
        f_lastTick = SDL_GetTicks();

        i_oldPos[X] = i_pos[X];
        i_oldPos[Y] = i_pos[Y];

        keyEvents(f_ticksDiff);

        if(b_inMove == TRUE)
        {
            f_vel[Y] += GRAVITY * f_ticksDiff;

            i_pos[X] += (int)(f_vel[X] * f_ticksDiff);
            i_pos[Y] += (int)(f_vel[Y] * f_ticksDiff);

        }
        if(b_inMove == FALSE)
        {
            f_vel[X] = i_pos[X] - i_oldPos[X];
            f_vel[Y] = i_pos[Y] - i_oldPos[Y];
        }

        if(i_pos[X] < 0)
        {
            i_pos[X] = 0;
            f_vel[X] = - f_vel[X];
        }
        if(i_pos[X] + (s_surface->w / TOTAL_FRAMES) >= SCREEN_WIDTH)
        {
            i_pos[X] = SCREEN_WIDTH - (s_surface->w / TOTAL_FRAMES);
            f_vel[X] = - f_vel[X];
        }
        if(i_pos[Y] < 0)
            i_pos[Y] = 0;
        if(i_pos[Y] + s_surface->h > SCREEN_HEIGHT)
        {
            i_pos[Y] = SCREEN_HEIGHT - s_surface->h;
            f_vel[Y] *= - FRICTION;
        }

        if(SDL_GetTicks() - f_animDelay > ANIM_DELAY / sqrt(f_vel[X]*f_vel[X] + f_vel[Y]*f_vel[Y]))
        {
            f_animDelay = SDL_GetTicks();

            i_frameNb++;
            if(i_frameNb >= TOTAL_FRAMES)
                i_frameNb = 0;
        }

        SDL_Rect r_src, r_dst;
        r_src.x = (s_surface->w / TOTAL_FRAMES) * i_frameNb;
        r_src.y = 0;
        r_src.w = s_surface->w / TOTAL_FRAMES;
        r_src.h = s_surface->h;

        r_dst.x = i_oldPos[X];
        r_dst.y = i_oldPos[Y];
        r_dst.w = r_src.w;        // these two are not
        r_dst.h = r_src.h;        // used by SDL anyway


        r_dst.x = i_pos[X];
        r_dst.y = i_pos[Y];

        SDL_FillRect(s_screen, NULL, SDL_MapRGB(s_screen->format, BGCOLOR));        // erase the older surface

        SDL_BlitSurface(s_surface, &r_src, s_screen, &r_dst);

        blitSwitchFullscreenButton();
        blitStopRespondingButton();

        SDL_UpdateRect(s_screen, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    }

    SDL_FreeSurface(s_surface);        // remove surface from the memory
    SDL_FreeSurface(s_switchFullscreen);
    SDL_FreeSurface(s_stopResponding);
}

void keyEvents(float lastTick)
{
SDL_Event event;

while(SDL_PollEvent(&event))
    {
    switch(event.type)
        {
        case SDL_KEYUP:        // key released, use KEYDOWN to check for pressed ones
            switch(event.key.keysym.sym)
                {
                case SDLK_ESCAPE:
                    done = 1;
                    break;
                default:
                    break;
                }
            break;
        case SDL_MOUSEBUTTONDOWN:
            switch(event.button.button)
                {
                case SDL_BUTTON_LEFT:
                    if (pressedRect(&event, &r_switchFullscreenButton)) {
                        switchFullscreenMode();

                    } else if (pressedRect(&event, &r_stopRespondingButton)) {
                        stopResponding();

                    } else if (event.button.x >= i_pos[X] && event.button.y >= i_pos[Y] &&
                            event.button.x < i_pos[X] + (s_surface->w / TOTAL_FRAMES) && event.button.y < i_pos[Y] + s_surface->h)
                        {
                        SDL_LockSurface(s_surface);
                        if(getpixel(s_surface, event.button.x - i_pos[X], event.button.y - i_pos[Y]) != SDL_MapRGB(s_surface->format, COLORKEY))
                            {
                            i_mouseMargin[X] = event.button.x - i_pos[X];
                            i_mouseMargin[Y] = event.button.y - i_pos[Y];
                            b_inMove = FALSE;
                            }
                        SDL_UnlockSurface(s_surface);
                        }
                    break;
                default:
                    break;
                }
            break;
        case SDL_MOUSEBUTTONUP:
            switch(event.button.button)
                {
                case SDL_BUTTON_LEFT:
                    if(b_inMove == FALSE)
                        {
                        f_vel[X] = i_mouseMov[X];
                        f_vel[Y] = i_mouseMov[Y];
                        b_inMove = TRUE;
                        }
                default:
                    break;
                }
            break;
        case SDL_MOUSEMOTION:
            if(b_inMove == FALSE)
                {
                i_pos[X] = event.motion.x - i_mouseMargin[X];
                i_pos[Y] = event.motion.y - i_mouseMargin[Y];

                i_mouseMov[X] = event.motion.xrel;
                i_mouseMov[Y] = event.motion.yrel;
                }
            break;
        case SDL_QUIT:        // done by wm or Ctrl-C
            done = 1;
            break;
        default:
            break;
        }
    }
}

// return the pixel value at x, y
// NOTE: you have to lock the surface in order to access its pixels data
Uint32 getpixel(SDL_Surface *surface, int x, int y)
{
int bpp = surface->format->BytesPerPixel;

// here p is the address to the pixel we want to retrieve
Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;

switch(bpp)
    {
    case 1:
        return *p;

    case 2:
        return *(Uint16 *)p;

    case 3:
        if(SDL_BYTEORDER == SDL_BIG_ENDIAN)
            return p[0] << 16 | p[1] << 8 | p[2];
        else
            return p[0] | p[1] << 8 | p[2] << 16;

    case 4:
        return *(Uint32 *)p;

    default:
        return 0;
    }
}

void blitSwitchFullscreenButton()
{
    SDL_Rect r_src;
    r_src.x = 0;
    r_src.y = 0;
    r_src.w = s_switchFullscreen->w;
    r_src.h = s_switchFullscreen->h;

    SDL_BlitSurface(s_switchFullscreen, &r_src, s_screen, &r_switchFullscreenButton);
}

void switchFullscreenMode()
{
    if (showFullscreen) {
        s_screen = SDL_SetVideoMode(SCREEN_WIDTH, SCREEN_HEIGHT, 16, SDL_SWSURFACE);
        printf("Switching to windowed mode.\n");
        showFullscreen = 0;
    } else {
        s_screen = SDL_SetVideoMode(SCREEN_WIDTH, SCREEN_HEIGHT, 16, SDL_SWSURFACE | SDL_FULLSCREEN);
        printf("Switching to fullscreen mode.\n");
        showFullscreen = 1;
    }
}

int pressedRect(SDL_Event *event, SDL_Rect *rect)
{
    if (event->button.x >= rect->x &&
            event->button.x <= (rect->x + rect->w) &&
            event->button.y >= rect->y &&
            event->button.y <= (rect->x + rect->h)) {
        return 1;
    } else {
        return 0;
    }
}

void blitStopRespondingButton()
{
    SDL_Rect r_src;
    r_src.x = 0;
    r_src.y = 0;
    r_src.w = s_stopResponding->w;
    r_src.h = s_stopResponding->h;

    SDL_BlitSurface(s_stopResponding, &r_src, s_screen, &r_stopRespondingButton);
}

void stopResponding()
{
    unsigned int i;
    while (1) {i++;}
}
