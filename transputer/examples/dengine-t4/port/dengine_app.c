/* The platform side of dEngine, as EAGLView.m is on iOS: texture loading
 * through the system image decoder, start-up, and the per-frame call.
 *
 * Start-up follows -initWithCoder: and -layoutSubviews: the ES 2.0
 * renderer, the user-default settings (high material quality, shadow, bump
 * and specular mapping on, statistics off), then binding the window
 * framebuffer, which createFramebuffer did after dEngine_Init had left the
 * shadow FBO bound. Each frame follows -drawView: with PLAY_VIDEO set: the
 * first frame starts the original fps.cp camera path, then
 * dEngine_HostFrame() runs. The clock is the platform's frame clock rather
 * than the wall clock, so every node computes the same frame. */
#include "../../dEngine/src/dEngine.h"
#include "../../dEngine/src/renderer.h"
#include "../../dEngine/src/texture.h"
#include "../../dEngine/src/camera.h"
#include "../../dEngine/src/timer.h"
#include "../../dEngine/src/material.h"
#include "../../dEngine/src/world.h"
#include "include/OpenGLES/ES2/gl.h"
#include "platform.h"
#include "png.h"
#include "dengine_app.h"

/* E_Sys_Milliseconds() treats a zero seconds count as "not started", so the
 * clock starts well past zero. */
#define CLOCK_BASE_MS 1000000u
static unsigned int clock_ms = CLOCK_BASE_MS;
static int frames_drawn;
static de_app_settings settings;

unsigned int de_platform_time_ms(void) { return clock_ms; }

/* EAGLView's -loadTexture: decoded the PNG through UIImage/CoreGraphics:
 * 24-bit images became TEXTURE_GL_RGB, others TEXTURE_GL_RGBA with colour
 * premultiplied by alpha; texture->file stays NULL. */
void loadNativePNG(texture_t *tex)
{
    unsigned long size;
    const unsigned char *data = de_rom_open(tex->path, &size);
    de_png_image img;
    const char *error;
    tex->file = NULL;
    if (!data) { printf("[PNG Loader] could not load: %s\n", tex->path); return; }
    error = de_png_decode(data, size, &img);
    if (error) { printf("[PNG Loader] could not load: %s (%s)\n", tex->path, error); return; }
    tex->width = img.width;
    tex->height = img.height;
    tex->bpp = img.bits_per_pixel;
    tex->data = img.rgba;
    tex->format = tex->bpp == 24 ? TEXTURE_GL_RGB : TEXTURE_GL_RGBA;
}

/* renderer.c can select the OpenGL ES 1.1 path; this platform offers only
 * ES 2.0, as an iPhone 3GS-class device did when the ES 2 context worked. */
void initFixedRenderer(renderer_t *r)
{
    (void)r;
    printf("[platform] OpenGL ES 1.1 is not available on this platform\n");
    exit(1);
}

void de_app_init(const de_app_settings *s)
{
    settings = *s;
    renderer.statsEnabled = settings.stats;
    renderer.materialQuality = settings.low_quality ? MATERIAL_QUALITY_LOW : MATERIAL_QUALITY_HIGH;
    dEngine_Init(GL_20_RENDERER, settings.width, settings.height);
    if (settings.shadows) renderer.props |= PROP_SHADOW; else renderer.props &= ~PROP_SHADOW;
    if (settings.bump) renderer.props |= PROP_BUMP; else renderer.props &= ~PROP_BUMP;
    if (settings.specular) renderer.props |= PROP_SPEC; else renderer.props &= ~PROP_SPEC;
    renderer.mainFramebufferId = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    frames_drawn = 0;
#ifdef DE_GL_TRACE
    {
        extern light_t light;
        extern entity_t map[];
        int i;
        printf("[apptrace] light pos %g %g %g %g look %g %g %g up %g %g %g fov %g\n",
               (double)light.position[0], (double)light.position[1], (double)light.position[2], (double)light.position[3],
               (double)light.lookAt[0], (double)light.lookAt[1], (double)light.lookAt[2],
               (double)light.upVector[0], (double)light.upVector[1], (double)light.upVector[2], (double)light.fov);
        printf("[apptrace] map[0].matrix");
        for (i = 0; i < 16; i++) printf(" %g", (double)map[0].matrix[i]);
        printf("\n[apptrace] camera aspect %g near %g far %g fov %g\n", (double)camera.aspect, (double)camera.zNear, (double)camera.zFar, (double)camera.fov);
    }
#endif
}

void de_app_frame(void)
{
    if (!frames_drawn) {
        /* CAM_StartPlaying sets simulationTime to the path's first key.
         * start_ms then moves the engine clock on, as if the demo had been
         * running that long: the camera path and MD5 animation both follow
         * simulationTime. */
        if (settings.camera_path) CAM_StartPlaying("data/cameraPath/fps.cp");
        simulationTime += settings.start_ms;
    }
    clock_ms += settings.frame_ms;
    dEngine_HostFrame();
    frames_drawn++;
}

unsigned int de_app_simulation_time(void) { return simulationTime; }
