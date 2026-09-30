#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void platform_init();
void platform_shutdown();

int  platform_register_hotkey(int index, const char* combo);
void platform_unregister_all_hotkeys();
int  platform_pop_hotkey();

void  platform_set_clipboard(const char* text);
char* platform_get_clipboard();
void  platform_send_paste();

void platform_create_tray(const char* tooltip);
void platform_tray_set_paused(int paused);
void platform_tray_set_callback(void (*cb)(int action));

int  platform_get_launch_at_login();
int  platform_set_launch_at_login(int enable);

char* platform_open_file_dialog();
char* platform_save_file_dialog(const char* default_name);

void platform_notify(const char* title, const char* body);

char* platform_config_dir();

/* 让窗口无边框透明，并挂载原生毛玻璃效果 */
void platform_make_window_transparent();

#ifdef __cplusplus
}
#endif