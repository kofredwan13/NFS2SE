// virtual_controls.c
// Simple virtual on-screen buttons: draws colored rects and synthesizes keydowns/up.
#include "virtual_controls.h"
#include <SDL2/SDL.h>
#ifdef __ANDROID__
#include <GLES/gl2.h> // optional if available; we use GL immediate mode if present
#else
#include <GL/gl.h>
#endif
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define MAX_BUTTONS 16

// Update code for shader-based gampad button overlay draw
// Simple vertex + fragment shaders for colored rectangles
static const char *vc_vs_src =
    "attribute vec2 aPos;\n"
    "attribute vec4 aColor;\n"
    "varying vec4 vColor;\n"
    "void main() {\n"
    "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "  vColor = aColor;\n"
    "}\n";

static const char *vc_fs_src =
    "precision mediump float;\n"
    "varying vec4 vColor;\n"
    "void main() {\n"
    "  gl_FragColor = vColor;\n"
    "}\n";

static GLuint vc_prog = 0;
static GLint vc_loc_pos = -1;
static GLint vc_loc_col = -1;
static GLuint vc_vbo = 0;
// Segment end - shader-based code

static VC_Button buttons[MAX_BUTTONS];
static int button_count = 0;
static int window_w = 800, window_h = 600;

// helper: add a button (normalized coords)
static int vc_add_button(float x, float y, float w, float h, SDL_Keycode key) {
    if (button_count >= MAX_BUTTONS) return -1;
    VC_Button *b = &buttons[button_count];
    b->x = x; b->y = y; b->w = w; b->h = h; b->key = key; b->id = button_count; b->pressed = 0;
    return button_count++;
}

int VirtualControls_Init(int win_w, int win_h) {
    window_w = win_w; window_h = win_h;
    button_count = 0;
    memset(buttons, 0, sizeof(buttons));
    // example layout: left/down/right arrows bottom-left, and an action button bottom-right
    // customize positions/sizes as needed.
    vc_add_button(0.05f, 0.75f, 0.12f, 0.18f, SDLK_LEFT);   // left
    vc_add_button(0.22f, 0.75f, 0.12f, 0.18f, SDLK_DOWN);   // down
    vc_add_button(0.22f, 0.55f, 0.12f, 0.18f, SDLK_UP);   // up
    vc_add_button(0.39f, 0.75f, 0.12f, 0.18f, SDLK_RIGHT);  // right
    vc_add_button(0.80f, 0.75f, 0.15f, 0.18f, SDLK_RETURN); // enter/action
    return 0;
}

void VirtualControls_OnWindowResized(int new_w, int new_h) {
    window_w = new_w;
    window_h = new_h;
}

static int point_in_button(const VC_Button *b, float px_norm, float py_norm) {
    return (px_norm >= b->x && px_norm <= (b->x + b->w) &&
            py_norm >= b->y && py_norm <= (b->y + b->h));
}

static void synthesize_key(SDL_Keycode key, int is_down) {
    SDL_Event e;
    memset(&e,0,sizeof(e));
    e.type = is_down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.type = e.type;
    e.key.timestamp = SDL_GetTicks();
    e.key.windowID = 0;
    e.key.state = is_down ? SDL_PRESSED : SDL_RELEASED;
    e.key.repeat = 0;
    e.key.keysym.scancode = SDL_GetScancodeFromKey(key);
    e.key.keysym.sym = key;
    e.key.keysym.mod = 0;
    SDL_PushEvent(&e);
}

void VirtualControls_HandleFingerEvent(const SDL_TouchFingerEvent *tf) {
    if (!tf) return;
    // tf->x, tf->y are normalized [0..1] relative to the window
    float nx = tf->x;
    float ny = tf->y;
    // On Android, y=0 top, x=0 left — matches our normalized coords defined above.
    // tf->type: SDL_FINGERDOWN, SDL_FINGERUP, SDL_FINGERMOTION are provided via event.type in caller.
    // But this function receives the inner tf struct. Caller should call on DOWN/UP.
    // We decide which button is affected; multiple fingers may press multiple buttons.
    // Use fingerId to track? We'll use simple approach: when DOWN -> mark pressed & synthesize KEYDOWN,
    // when UP -> find any button that contains finger position and release it (synthesize KEYUP).
    // The caller should route the event appropriately (pass tf from event.tfinger).
    // We'll rely only on position: good enough for basic virtual controls.
    switch (tf->type) {
        case SDL_FINGERDOWN: {
            for (int i=0;i<button_count;++i) {
                if (!buttons[i].pressed && point_in_button(&buttons[i], nx, ny)) {
                    buttons[i].pressed = 1;
                    synthesize_key(buttons[i].key, 1);
                }
            }
            break;
        }
        case SDL_FINGERUP: {
            // release any button which contains the finger (or simply release all pressed buttons
            // that contain the last finger position)
            for (int i=0;i<button_count;++i) {
                if (buttons[i].pressed && point_in_button(&buttons[i], nx, ny)) {
                    buttons[i].pressed = 0;
                    synthesize_key(buttons[i].key, 0);
                }
            }
            break;
        }
        case SDL_FINGERMOTION: {
            // optionally handle drag across buttons: if a finger moves off a pressed button, release it.
            for (int i=0;i<button_count;++i) {
                if (buttons[i].pressed && !point_in_button(&buttons[i], nx, ny)) {
                    buttons[i].pressed = 0;
                    synthesize_key(buttons[i].key, 0);
                }
            }
            break;
        }
        default:
            break;
    }
}

// fallback for mouse button events (if touch is translated to mouse)
void VirtualControls_HandleMouseEvent(const SDL_MouseButtonEvent *mb) {
    if (!mb) return;
    float nx = ((float)mb->x) / (float)window_w;
    float ny = ((float)mb->y) / (float)window_h;
    if (mb->state == SDL_PRESSED) {
        for (int i=0;i<button_count;++i) {
            if (!buttons[i].pressed && point_in_button(&buttons[i], nx, ny)) {
                buttons[i].pressed = 1;
                synthesize_key(buttons[i].key, 1);
            }
        }
    } else if (mb->state == SDL_RELEASED) {
        for (int i=0;i<button_count;++i) {
            if (buttons[i].pressed) {
                // if mouse release inside button or not, release all pressed (simple)
                buttons[i].pressed = 0;
                synthesize_key(buttons[i].key, 0);
            }
        }
    }
}

// Simple immediate-mode GL draw of rectangle (normalized coords to window pixels)
static void draw_filled_rect_pixels(int x, int y, int w, int h) {
    // Use glBegin if available. This is simple and likely to work in a compatibility profile.
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
        glVertex2f((float)x, (float)y);
        glVertex2f((float)(x + w), (float)y);
        glVertex2f((float)(x + w), (float)(y + h));
        glVertex2f((float)x, (float)(y + h));
    glEnd();
}

// Shader-based gamepad code
static void vc_create_shader(void) {
    GLint ok;
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vc_vs_src, NULL);
    glCompileShader(vs);
    glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[256]; glGetShaderInfoLog(vs, sizeof(buf), NULL, buf);
        SDL_Log("VC vertex shader error: %s", buf);
    }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &vc_fs_src, NULL);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[256]; glGetShaderInfoLog(fs, sizeof(buf), NULL, buf);
        SDL_Log("VC fragment shader error: %s", buf);
    }

    vc_prog = glCreateProgram();
    glAttachShader(vc_prog, vs);
    glAttachShader(vc_prog, fs);
    glBindAttribLocation(vc_prog, 0, "aPos");
    glBindAttribLocation(vc_prog, 1, "aColor");
    glLinkProgram(vc_prog);
    glGetProgramiv(vc_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[256]; glGetProgramInfoLog(vc_prog, sizeof(buf), NULL, buf);
        SDL_Log("VC link error: %s", buf);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    vc_loc_pos = 0;
    vc_loc_col = 1;
    glGenBuffers(1, &vc_vbo);
}

void VirtualControls_Draw(void) {
    if (button_count == 0) return;
    if (!vc_prog) vc_create_shader();

    // Convert screen coords to normalized device coords (-1..1)
    typedef struct { float x, y; float r, g, b, a; } Vertex;
    Vertex verts[6 * MAX_BUTTONS];
    int vcount = 0;

    for (int i = 0; i < button_count; ++i) {
        VC_Button *b = &buttons[i];
        int px = (int)(b->x * window_w);
        int py = (int)(b->y * window_h);
        int pw = (int)(b->w * window_w);
        int ph = (int)(b->h * window_h);

        float x1 =  2.0f * (float)px / window_w - 1.0f;
        float y1 =  1.0f - 2.0f * (float)py / window_h;
        float x2 =  2.0f * (float)(px + pw) / window_w - 1.0f;
        float y2 =  1.0f - 2.0f * (float)(py + ph) / window_h;

        float r,g,b_,a;
        if (b->pressed) { r=0.2f; g=0.6f; b_=0.2f; a=0.7f; }
        else            { r=0.1f; g=0.1f; b_=0.1f; a=0.4f; }

        verts[vcount++] = (Vertex){x1,y1,r,g,b_,a};
        verts[vcount++] = (Vertex){x2,y1,r,g,b_,a};
        verts[vcount++] = (Vertex){x2,y2,r,g,b_,a};
        verts[vcount++] = (Vertex){x1,y1,r,g,b_,a};
        verts[vcount++] = (Vertex){x2,y2,r,g,b_,a};
        verts[vcount++] = (Vertex){x1,y2,r,g,b_,a};
    }

     // Save previous GL state (minimal manual save)
    GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLboolean blendEnabled = glIsEnabled(GL_BLEND);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);                    // disable depth writes
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(vc_prog);
    glBindBuffer(GL_ARRAY_BUFFER, vc_vbo);
    glBufferData(GL_ARRAY_BUFFER, vcount * sizeof(Vertex), verts, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(vc_loc_pos);
    glEnableVertexAttribArray(vc_loc_col);
    glVertexAttribPointer(vc_loc_pos, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glVertexAttribPointer(vc_loc_col, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(sizeof(float)*2));

    // glEnable(GL_BLEND);
    // glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glDrawArrays(GL_TRIANGLES, 0, vcount);

    glDisableVertexAttribArray(vc_loc_pos);
    glDisableVertexAttribArray(vc_loc_col);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);

    // restore state
    if (!blendEnabled) glDisable(GL_BLEND);
    if (depthTestEnabled) glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}

// End shader-based code segment
#ifdef OPENGL1X
void VirtualControls_Draw_GL1(void) {
    if (button_count == 0) return;
    // Save state and set up 2D ortho
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();

    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (GLdouble)window_w, (GLdouble)window_h, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    for (int i=0;i<button_count;++i) {
        VC_Button *b = &buttons[i];
        int px = (int)(b->x * window_w);
        int py = (int)(b->y * window_h);
        int pw = (int)(b->w * window_w);
        int ph = (int)(b->h * window_h);

        if (b->pressed) {
            glColor4f(0.2f, 0.6f, 0.2f, 0.7f); // greenish when pressed
        } else {
            glColor4f(0.1f, 0.1f, 0.1f, 0.5f); // dark translucent
        }
        draw_filled_rect_pixels(px, py, pw, ph);

        // Outline
        glColor4f(1.0f, 1.0f, 1.0f, 0.6f);
        glLineWidth(2.0f);
        glBegin(GL_LINE_LOOP);
            glVertex2f((GLfloat)px, (GLfloat)py);
            glVertex2f((GLfloat)(px+pw), (GLfloat)py);
            glVertex2f((GLfloat)(px+pw), (GLfloat)(py+ph));
            glVertex2f((GLfloat)px, (GLfloat)(py+ph));
        glEnd();
    }

    // restore matrices
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopAttrib();
}
#endif

void VirtualControls_Shutdown(void) {
    // nothing for now
    // Start shader-based code
    if (vc_vbo) {
        glDeleteBuffers(1, &vc_vbo);
        vc_vbo = 0;
    }
    if (vc_prog) {
        glDeleteProgram(vc_prog);
        vc_prog = 0;
    }
}
