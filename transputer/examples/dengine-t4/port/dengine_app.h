#ifndef DE_APP_H
#define DE_APP_H
typedef struct {
    int width, height;          /* the original iPhone view is 320 x 480 */
    int low_quality;            /* MaterialQuality 0: the PVRTC material set */
    int shadows, bump, specular;
    int stats;                  /* StatisticsEnabled: the text overlay */
    int camera_path;            /* PLAY_VIDEO: follow data/cameraPath/fps.cp */
    unsigned int start_ms;      /* platform clock at the first frame */
    unsigned int frame_ms;      /* clock step per frame */
} de_app_settings;
void de_app_init(const de_app_settings *settings);
void de_app_frame(void);
unsigned int de_app_simulation_time(void);
#endif
