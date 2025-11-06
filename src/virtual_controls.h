// virtual_controls.h
#ifndef VIRTUAL_CONTROLS_H
#define VIRTUAL_CONTROLS_H

#include <SDL2/SDL.h>
#include <stdint.h>

typedef struct VC_Button {
    float x;      // normalized [0..1] from left
    float y;      // normalized [0..1] from top
    float w;      // normalized width (fraction of window)
    float h;      // normalized height
    SDL_Keycode key; // key to synthesize
    int id;
    int pressed;  // runtime state
} VC_Button;

int VirtualControls_Init(int win_w, int win_h); // call once after sdlWin created
void VirtualControls_OnWindowResized(int new_w, int new_h);
void VirtualControls_HandleFingerEvent(const SDL_TouchFingerEvent *tf); // finger down/up/motion
void VirtualControls_HandleMouseEvent(const SDL_MouseButtonEvent *mb); // fallback if touch->mouse
void VirtualControls_Draw(void); // call before SDL_GL_SwapWindow()
#ifdef OPENGL1X
void VirtualControls_Draw_GL1(void); // call before SDL_GL_SwapWindow()
#endif
void VirtualControls_Shutdown(void);

#endif // VIRTUAL_CONTROLS_H
