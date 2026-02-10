#ifndef UI_SCREEN_H
#define UI_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_ACTION_NONE = 0,
    UI_ACTION_NAV_NEXT,
    UI_ACTION_NAV_PREV,
    UI_ACTION_ENTER,
    UI_ACTION_BACK,
    UI_ACTION_VALUE_INC,
    UI_ACTION_VALUE_DEC
} ui_action_t;

typedef struct ui_screen ui_screen_t;

typedef void (*ui_screen_enter_fn)(void);
typedef void (*ui_screen_exit_fn)(void);
typedef void (*ui_screen_render_fn)(void);
typedef void (*ui_screen_action_fn)(ui_action_t action);

struct ui_screen {
    const char *name;
    ui_screen_enter_fn on_enter;
    ui_screen_exit_fn on_exit;
    ui_screen_render_fn on_render;
    ui_screen_action_fn on_action;
};

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_H */
