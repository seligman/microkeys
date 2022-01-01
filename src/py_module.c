#include "py/runtime.h"

/* ----- clip.copy --------------------------------------------------------- */
char* clip_copy_invoke();
STATIC mp_obj_t clip_copy(void) {
    char* temp = clip_copy_invoke();
    mp_obj_t ret = mp_obj_new_str(temp, strlen(temp));
    free(temp);
    return ret;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(clip_copy_obj, clip_copy);

/* ----- clip.paste -------------------------------------------------------- */
void clip_paste_invoke(const char*);
STATIC mp_obj_t clip_paste(mp_obj_t a_obj) {
    clip_paste_invoke(mp_obj_str_get_str(a_obj));
    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(clip_paste_obj, clip_paste);

/* ----- keys.log ---------------------------------------------------------- */
void keys_log_invoke(const char*);
STATIC mp_obj_t keys_log(mp_obj_t a_obj) {
    keys_log_invoke(mp_obj_str_get_str(a_obj));
    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(keys_log_obj, keys_log);

/* ----- keys.press -------------------------------------------------------- */
void keys_press_invoke(const char*);
STATIC mp_obj_t keys_press(mp_obj_t a_obj) {
    keys_press_invoke(mp_obj_str_get_str(a_obj));
    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(keys_press_obj, keys_press);

/* ----- keys.press_raw ---------------------------------------------------- */
void keys_press_raw_invoke(const char*);
STATIC mp_obj_t keys_press_raw(mp_obj_t a_obj) {
    keys_press_raw_invoke(mp_obj_str_get_str(a_obj));
    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(keys_press_raw_obj, keys_press_raw);

/* ----- key_internal ------------------------------------------------------ */
// This is the internal helper that's called back after the decorator is 
// declared.  It just needs to store the function and turn around and return 
// it.  Note that there's only one data blob from keys.key, since decorators
// are processed in a single thread (well, everything is), so we just need to
// know what the last one was, not all of them, at least at this level.
int last_func_key_vk = 0;
char last_func_key_name[250];
void key_press_store_fun(int vk, char* name, void* fun);
mp_obj_t key_internal(mp_obj_t self_in, mp_uint_t n_args, mp_uint_t n_kw, const mp_obj_t* args) {
    key_press_store_fun(last_func_key_vk, last_func_key_name, args[0]);
    return args[0];
}

/* ----- keys.key ---------------------------------------------------------- */
STATIC mp_obj_t keys_key(mp_obj_t a_obj) {
    strcpy(last_func_key_name, "");
    last_func_key_vk = 0;
    const mp_obj_type_t* a_type = mp_obj_get_type(a_obj);
    if (a_type->name == MP_QSTR_int) {
        last_func_key_vk = mp_obj_get_int(a_obj);
    }
    else if (a_type->name == MP_QSTR_str) {
        strcpy(last_func_key_name, mp_obj_str_get_str(a_obj));
    }
    return mp_obj_new_fun_native(MP_OBJ_NULL, MP_OBJ_NULL, key_internal, m_new0(mp_uint_t, 1));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(keys_key_obj, keys_key);

/* ----- mouse.position ---------------------------------------------------- */
void mouse_position_invoke(int* x, int* y);
STATIC mp_obj_t mouse_position(void) {
    int x = 0;
    int y = 0;
    mouse_position_invoke(&x, &y);
    mp_obj_t items[] = { mp_obj_new_int(x), mp_obj_new_int(y) };
    return mp_obj_new_tuple(2, items);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mouse_position_obj, mouse_position);

/* ----- mouse.move -------------------------------------------------------- */
void mouse_move_invoke(int x, int y, int offset);
STATIC mp_obj_t mouse_move(mp_obj_t a_obj, mp_obj_t b_obj) {
    int x = mp_obj_get_int(a_obj);
    int y = mp_obj_get_int(b_obj);
    mouse_move_invoke(x, y, 0);
    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(mouse_move_obj, mouse_move);

/* ----- mouse.offset ------------------------------------------------------ */
STATIC mp_obj_t mouse_offset(mp_obj_t a_obj, mp_obj_t b_obj) {
    int x = mp_obj_get_int(a_obj);
    int y = mp_obj_get_int(b_obj);
    mouse_move_invoke(x, y, 1);
    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(mouse_offset_obj, mouse_offset);

/* ----- mouse.click ------------------------------------------------------- */
void mouse_click_invoke(int left_down, int left_up, int right_down, int right_up); 
STATIC mp_obj_t mouse_click(void) {
    mouse_click_invoke(1, 1, 0, 0);
    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mouse_click_obj, mouse_click);

/* ----- mouse.right_click ------------------------------------------------- */
STATIC mp_obj_t mouse_right_click(void) {
    mouse_click_invoke(0, 0, 1, 1);
    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mouse_right_click_obj, mouse_right_click);

/* ----- mouse.down -------------------------------------------------------- */
STATIC mp_obj_t mouse_down(void) {
    mouse_click_invoke(1, 0, 0, 0);
    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mouse_down_obj, mouse_down);

/* ----- mouse.right_down -------------------------------------------------- */
STATIC mp_obj_t mouse_right_down(void) {
    mouse_click_invoke(0, 0, 1, 0);
    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mouse_right_down_obj, mouse_right_down);

/* ----- mouse.up ---------------------------------------------------------- */
STATIC mp_obj_t mouse_up(void) {
    mouse_click_invoke(0, 1, 0, 0);
    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mouse_up_obj, mouse_up);

/* ----- mouse.right_up ---------------------------------------------------- */
STATIC mp_obj_t mouse_right_up(void) {
    mouse_click_invoke(0, 0, 0, 1);
    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(mouse_right_up_obj, mouse_right_up);

/* ----- windows.list_all -------------------------------------------------- */
mp_obj_t make_windows_tuple(const char* handle, const char* title, const char* classname) {
    mp_obj_t mpHandle = mp_obj_new_str(handle, strlen(handle));
    mp_obj_t mpTitle = mp_obj_new_str(title, strlen(title));
    mp_obj_t mpClass = mp_obj_new_str(classname, strlen(classname));
    mp_obj_t items[] = { mpHandle, mpTitle, mpClass };
    return mp_obj_new_tuple(3, items);
}

void windows_list_all_impl(mp_obj_t** list, int* count);
STATIC mp_obj_t windows_list_all(void) {
    mp_obj_t* source_list = NULL;
    int count = 0;
    windows_list_all_impl(&source_list, &count);
    mp_obj_list_t* lst = MP_OBJ_TO_PTR(mp_obj_new_list(count, NULL));
    for (int i = 0; i < count; ++i) {
        lst->items[i] = source_list[i];
    }
    free(source_list);
    return MP_OBJ_FROM_PTR(lst);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(windows_list_all_obj, windows_list_all);

/* ----- windows.get_active ------------------------------------------------ */
void* windows_get_active_impl();
STATIC mp_obj_t windows_get_active(void) {
    return (mp_obj_t)windows_get_active_impl();
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(windows_get_active_obj, windows_get_active);

/* ----- windows.set_active ------------------------------------------------ */
void windows_set_active_impl(const char* handle);
STATIC mp_obj_t windows_set_active(mp_obj_t a_obj) {
    windows_set_active_impl(mp_obj_str_get_str(a_obj));
    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(windows_set_active_obj, windows_set_active);

/* ----- windows.get_position ---------------------------------------------- */
void windows_get_position_impl(const char* handle, int* x, int* y, int* width, int* height);
STATIC mp_obj_t windows_get_position(mp_obj_t a_obj) {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    windows_get_position_impl(mp_obj_str_get_str(a_obj), &x, &y, &width, &height);
    mp_obj_t items[] = {
        mp_obj_new_int(x),
        mp_obj_new_int(y),
        mp_obj_new_int(width),
        mp_obj_new_int(height),
    };
    return mp_obj_new_tuple(4, items);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(windows_get_position_obj, windows_get_position);

/* ----- windows.set_position ---------------------------------------------- */
void windows_set_position_impl(const char* handle, int x, int y, int width, int height);
STATIC mp_obj_t windows_set_position(size_t n_args, const mp_obj_t* args) {
    windows_set_position_impl(
        mp_obj_str_get_str(args[0]),
        mp_obj_get_int(args[1]),
        mp_obj_get_int(args[2]),
        mp_obj_get_int(args[3]),
        mp_obj_get_int(args[4]));
    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(windows_set_position_obj, 5, 5, windows_set_position);

/* ----- windows.launch ---------------------------------------------------- */
void windows_launch_impl(const char* command_line);
STATIC mp_obj_t windows_launch(mp_obj_t a_obj) {
    windows_launch_impl(mp_obj_str_get_str(a_obj));
    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(windows_launch_obj, windows_launch);

/* ----- Connection Glue --------------------------------------------------- */
/*       keys module                                                         */
STATIC const mp_rom_map_elem_t keys_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_keys) },
    { MP_ROM_QSTR(MP_QSTR_log), MP_ROM_PTR(&keys_log_obj) },
    { MP_ROM_QSTR(MP_QSTR_press), MP_ROM_PTR(&keys_press_obj) },
    { MP_ROM_QSTR(MP_QSTR_press_raw), MP_ROM_PTR(&keys_press_raw_obj) },
    { MP_ROM_QSTR(MP_QSTR_key), MP_ROM_PTR(&keys_key_obj) },
    // Start of generated section -- Do not edit!
    // This section is generated by create_key_constants.py, do not edit by hand
    { MP_ROM_QSTR(MP_QSTR_KEY_BREAK), MP_ROM_INT(0x03) },
    { MP_ROM_QSTR(MP_QSTR_KEY_BACK), MP_ROM_INT(0x08) },
    { MP_ROM_QSTR(MP_QSTR_KEY_BS), MP_ROM_INT(0x08) },
    { MP_ROM_QSTR(MP_QSTR_KEY_BACKSPACE), MP_ROM_INT(0x08) },
    { MP_ROM_QSTR(MP_QSTR_KEY_BKSP), MP_ROM_INT(0x08) },
    { MP_ROM_QSTR(MP_QSTR_KEY_TAB), MP_ROM_INT(0x09) },
    { MP_ROM_QSTR(MP_QSTR_KEY_CLEAR), MP_ROM_INT(0x0C) },
    { MP_ROM_QSTR(MP_QSTR_KEY_ENTER), MP_ROM_INT(0x0D) },
    { MP_ROM_QSTR(MP_QSTR_KEY_RETURN), MP_ROM_INT(0x0D) },
    { MP_ROM_QSTR(MP_QSTR_KEY_CAPSLOCK), MP_ROM_INT(0x14) },
    { MP_ROM_QSTR(MP_QSTR_KEY_CAPS), MP_ROM_INT(0x14) },
    { MP_ROM_QSTR(MP_QSTR_KEY_CAPS_LOCK), MP_ROM_INT(0x14) },
    { MP_ROM_QSTR(MP_QSTR_KEY_ESC), MP_ROM_INT(0x1B) },
    { MP_ROM_QSTR(MP_QSTR_KEY_ESCAPE), MP_ROM_INT(0x1B) },
    { MP_ROM_QSTR(MP_QSTR_KEY_SPACE), MP_ROM_INT(0x20) },
    { MP_ROM_QSTR(MP_QSTR_KEY_PAGEUP), MP_ROM_INT(0x21) },
    { MP_ROM_QSTR(MP_QSTR_KEY_PAGE_UP), MP_ROM_INT(0x21) },
    { MP_ROM_QSTR(MP_QSTR_KEY_PGUP), MP_ROM_INT(0x21) },
    { MP_ROM_QSTR(MP_QSTR_KEY_PAGEDOWN), MP_ROM_INT(0x22) },
    { MP_ROM_QSTR(MP_QSTR_KEY_PAGE_DOWN), MP_ROM_INT(0x22) },
    { MP_ROM_QSTR(MP_QSTR_KEY_PGDN), MP_ROM_INT(0x22) },
    { MP_ROM_QSTR(MP_QSTR_KEY_END), MP_ROM_INT(0x23) },
    { MP_ROM_QSTR(MP_QSTR_KEY_HOME), MP_ROM_INT(0x24) },
    { MP_ROM_QSTR(MP_QSTR_KEY_LEFT), MP_ROM_INT(0x25) },
    { MP_ROM_QSTR(MP_QSTR_KEY_UP), MP_ROM_INT(0x26) },
    { MP_ROM_QSTR(MP_QSTR_KEY_RIGHT), MP_ROM_INT(0x27) },
    { MP_ROM_QSTR(MP_QSTR_KEY_DOWN), MP_ROM_INT(0x28) },
    { MP_ROM_QSTR(MP_QSTR_KEY_PRINTSCREEN), MP_ROM_INT(0x2C) },
    { MP_ROM_QSTR(MP_QSTR_KEY_PRINT_SCREEN), MP_ROM_INT(0x2C) },
    { MP_ROM_QSTR(MP_QSTR_KEY_PRTSC), MP_ROM_INT(0x2C) },
    { MP_ROM_QSTR(MP_QSTR_KEY_INS), MP_ROM_INT(0x2D) },
    { MP_ROM_QSTR(MP_QSTR_KEY_INSERT), MP_ROM_INT(0x2D) },
    { MP_ROM_QSTR(MP_QSTR_KEY_DEL), MP_ROM_INT(0x2E) },
    { MP_ROM_QSTR(MP_QSTR_KEY_DELETE), MP_ROM_INT(0x2E) },
    { MP_ROM_QSTR(MP_QSTR_KEY_HELP), MP_ROM_INT(0x2F) },
    { MP_ROM_QSTR(MP_QSTR_KEY_NUMLOCK), MP_ROM_INT(0x90) },
    { MP_ROM_QSTR(MP_QSTR_KEY_NUM_LOCK), MP_ROM_INT(0x90) },
    { MP_ROM_QSTR(MP_QSTR_KEY_NUM), MP_ROM_INT(0x90) },
    { MP_ROM_QSTR(MP_QSTR_KEY_SCROLLLOCK), MP_ROM_INT(0x91) },
    { MP_ROM_QSTR(MP_QSTR_KEY_SCROLL_LOCK), MP_ROM_INT(0x91) },
    { MP_ROM_QSTR(MP_QSTR_KEY_SCROLL), MP_ROM_INT(0x91) },
    { MP_ROM_QSTR(MP_QSTR_KEY_SEMI_COLON), MP_ROM_INT(0xBA) },
    { MP_ROM_QSTR(MP_QSTR_KEY_SEMI), MP_ROM_INT(0xBA) },
    { MP_ROM_QSTR(MP_QSTR_KEY_SEMICOLON), MP_ROM_INT(0xBA) },
    { MP_ROM_QSTR(MP_QSTR_KEY_PLUS), MP_ROM_INT(0xBB) },
    { MP_ROM_QSTR(MP_QSTR_KEY_COMMA), MP_ROM_INT(0xBC) },
    { MP_ROM_QSTR(MP_QSTR_KEY_MINUS), MP_ROM_INT(0xBD) },
    { MP_ROM_QSTR(MP_QSTR_KEY_DASH), MP_ROM_INT(0xBD) },
    { MP_ROM_QSTR(MP_QSTR_KEY_PERIOD), MP_ROM_INT(0xBE) },
    { MP_ROM_QSTR(MP_QSTR_KEY_DOT), MP_ROM_INT(0xBE) },
    { MP_ROM_QSTR(MP_QSTR_KEY_SLASH), MP_ROM_INT(0xBF) },
    { MP_ROM_QSTR(MP_QSTR_KEY_FORWARD_SLASH), MP_ROM_INT(0xBF) },
    { MP_ROM_QSTR(MP_QSTR_KEY_FORWARDSLASH), MP_ROM_INT(0xBF) },
    { MP_ROM_QSTR(MP_QSTR_KEY_BACK_QUOTE), MP_ROM_INT(0xC0) },
    { MP_ROM_QSTR(MP_QSTR_KEY_BACKQUOTE), MP_ROM_INT(0xC0) },
    { MP_ROM_QSTR(MP_QSTR_KEY_OPEN_BRACKET), MP_ROM_INT(0xDB) },
    { MP_ROM_QSTR(MP_QSTR_KEY_OPENBRACKET), MP_ROM_INT(0xDB) },
    { MP_ROM_QSTR(MP_QSTR_KEY_BACK_SLASH), MP_ROM_INT(0xDC) },
    { MP_ROM_QSTR(MP_QSTR_KEY_BACKSLASH), MP_ROM_INT(0xDC) },
    { MP_ROM_QSTR(MP_QSTR_KEY_CLOSE_BRACKET), MP_ROM_INT(0xDD) },
    { MP_ROM_QSTR(MP_QSTR_KEY_CLOSEBRACKET), MP_ROM_INT(0xDD) },
    { MP_ROM_QSTR(MP_QSTR_KEY_QUOTE), MP_ROM_INT(0xDE) },
    { MP_ROM_QSTR(MP_QSTR_KEY_SINGLE_QUOTE), MP_ROM_INT(0xDE) },
    { MP_ROM_QSTR(MP_QSTR_KEY_SINGLEQUOTE), MP_ROM_INT(0xDE) },
    { MP_ROM_QSTR(MP_QSTR_KEY_ALT), MP_ROM_INT(0x100) },
    { MP_ROM_QSTR(MP_QSTR_KEY_CONTROL), MP_ROM_INT(0x200) },
    { MP_ROM_QSTR(MP_QSTR_KEY_CTRL), MP_ROM_INT(0x200) },
    { MP_ROM_QSTR(MP_QSTR_KEY_SHIFT), MP_ROM_INT(0x400) },
    { MP_ROM_QSTR(MP_QSTR_KEY_WIN), MP_ROM_INT(0x800) },
    { MP_ROM_QSTR(MP_QSTR_KEY_WINDOWS), MP_ROM_INT(0x800) },
    { MP_ROM_QSTR(MP_QSTR_KEY_0), MP_ROM_INT(0x30) },
    { MP_ROM_QSTR(MP_QSTR_KEY_1), MP_ROM_INT(0x31) },
    { MP_ROM_QSTR(MP_QSTR_KEY_2), MP_ROM_INT(0x32) },
    { MP_ROM_QSTR(MP_QSTR_KEY_3), MP_ROM_INT(0x33) },
    { MP_ROM_QSTR(MP_QSTR_KEY_4), MP_ROM_INT(0x34) },
    { MP_ROM_QSTR(MP_QSTR_KEY_5), MP_ROM_INT(0x35) },
    { MP_ROM_QSTR(MP_QSTR_KEY_6), MP_ROM_INT(0x36) },
    { MP_ROM_QSTR(MP_QSTR_KEY_7), MP_ROM_INT(0x37) },
    { MP_ROM_QSTR(MP_QSTR_KEY_8), MP_ROM_INT(0x38) },
    { MP_ROM_QSTR(MP_QSTR_KEY_9), MP_ROM_INT(0x39) },
    { MP_ROM_QSTR(MP_QSTR_KEY_A), MP_ROM_INT(0x41) },
    { MP_ROM_QSTR(MP_QSTR_KEY_B), MP_ROM_INT(0x42) },
    { MP_ROM_QSTR(MP_QSTR_KEY_C), MP_ROM_INT(0x43) },
    { MP_ROM_QSTR(MP_QSTR_KEY_D), MP_ROM_INT(0x44) },
    { MP_ROM_QSTR(MP_QSTR_KEY_E), MP_ROM_INT(0x45) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F), MP_ROM_INT(0x46) },
    { MP_ROM_QSTR(MP_QSTR_KEY_G), MP_ROM_INT(0x47) },
    { MP_ROM_QSTR(MP_QSTR_KEY_H), MP_ROM_INT(0x48) },
    { MP_ROM_QSTR(MP_QSTR_KEY_I), MP_ROM_INT(0x49) },
    { MP_ROM_QSTR(MP_QSTR_KEY_J), MP_ROM_INT(0x4A) },
    { MP_ROM_QSTR(MP_QSTR_KEY_K), MP_ROM_INT(0x4B) },
    { MP_ROM_QSTR(MP_QSTR_KEY_L), MP_ROM_INT(0x4C) },
    { MP_ROM_QSTR(MP_QSTR_KEY_M), MP_ROM_INT(0x4D) },
    { MP_ROM_QSTR(MP_QSTR_KEY_N), MP_ROM_INT(0x4E) },
    { MP_ROM_QSTR(MP_QSTR_KEY_O), MP_ROM_INT(0x4F) },
    { MP_ROM_QSTR(MP_QSTR_KEY_P), MP_ROM_INT(0x50) },
    { MP_ROM_QSTR(MP_QSTR_KEY_Q), MP_ROM_INT(0x51) },
    { MP_ROM_QSTR(MP_QSTR_KEY_R), MP_ROM_INT(0x52) },
    { MP_ROM_QSTR(MP_QSTR_KEY_S), MP_ROM_INT(0x53) },
    { MP_ROM_QSTR(MP_QSTR_KEY_T), MP_ROM_INT(0x54) },
    { MP_ROM_QSTR(MP_QSTR_KEY_U), MP_ROM_INT(0x55) },
    { MP_ROM_QSTR(MP_QSTR_KEY_V), MP_ROM_INT(0x56) },
    { MP_ROM_QSTR(MP_QSTR_KEY_W), MP_ROM_INT(0x57) },
    { MP_ROM_QSTR(MP_QSTR_KEY_X), MP_ROM_INT(0x58) },
    { MP_ROM_QSTR(MP_QSTR_KEY_Y), MP_ROM_INT(0x59) },
    { MP_ROM_QSTR(MP_QSTR_KEY_Z), MP_ROM_INT(0x5A) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F1), MP_ROM_INT(0x70) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F2), MP_ROM_INT(0x71) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F3), MP_ROM_INT(0x72) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F4), MP_ROM_INT(0x73) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F5), MP_ROM_INT(0x74) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F6), MP_ROM_INT(0x75) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F7), MP_ROM_INT(0x76) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F8), MP_ROM_INT(0x77) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F9), MP_ROM_INT(0x78) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F10), MP_ROM_INT(0x79) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F11), MP_ROM_INT(0x7A) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F12), MP_ROM_INT(0x7B) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F13), MP_ROM_INT(0x7C) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F14), MP_ROM_INT(0x7D) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F15), MP_ROM_INT(0x7E) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F16), MP_ROM_INT(0x7F) },
    // End of generated section -- Do not edit!
};
STATIC MP_DEFINE_CONST_DICT(keys_module_globals, keys_module_globals_table);
const mp_obj_module_t keys_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&keys_module_globals,
};
MP_REGISTER_MODULE(MP_QSTR_keys, keys_user_cmodule, 1);

/*       clip module                                                         */
STATIC const mp_rom_map_elem_t clip_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_clip) },
    { MP_ROM_QSTR(MP_QSTR_paste), MP_ROM_PTR(&clip_paste_obj) },
    { MP_ROM_QSTR(MP_QSTR_copy), MP_ROM_PTR(&clip_copy_obj) },
};
STATIC MP_DEFINE_CONST_DICT(clip_module_globals, clip_module_globals_table);
const mp_obj_module_t clip_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&clip_module_globals,
};
MP_REGISTER_MODULE(MP_QSTR_clip, clip_user_cmodule, 1);

/*       mouse module                                                        */
STATIC const mp_rom_map_elem_t mouse_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_mouse) },
    { MP_ROM_QSTR(MP_QSTR_position), MP_ROM_PTR(&mouse_position_obj) },
    { MP_ROM_QSTR(MP_QSTR_move), MP_ROM_PTR(&mouse_move_obj) },
    { MP_ROM_QSTR(MP_QSTR_offset), MP_ROM_PTR(&mouse_offset_obj) },
    { MP_ROM_QSTR(MP_QSTR_click), MP_ROM_PTR(&mouse_click_obj) },
    { MP_ROM_QSTR(MP_QSTR_right_click), MP_ROM_PTR(&mouse_right_click_obj) },
    { MP_ROM_QSTR(MP_QSTR_down), MP_ROM_PTR(&mouse_down_obj) },
    { MP_ROM_QSTR(MP_QSTR_right_down), MP_ROM_PTR(&mouse_right_down_obj) },
    { MP_ROM_QSTR(MP_QSTR_up), MP_ROM_PTR(&mouse_up_obj) },
    { MP_ROM_QSTR(MP_QSTR_right_up), MP_ROM_PTR(&mouse_right_up_obj) },
};
STATIC MP_DEFINE_CONST_DICT(mouse_module_globals, mouse_module_globals_table);
const mp_obj_module_t mouse_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&mouse_module_globals,
};
MP_REGISTER_MODULE(MP_QSTR_mouse, mouse_user_cmodule, 1);

/*       windows module                                                      */
STATIC const mp_rom_map_elem_t windows_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_windows) },
    { MP_ROM_QSTR(MP_QSTR_list_all), MP_ROM_PTR(&windows_list_all_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_active), MP_ROM_PTR(&windows_get_active_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_active), MP_ROM_PTR(&windows_set_active_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_position), MP_ROM_PTR(&windows_get_position_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_position), MP_ROM_PTR(&windows_set_position_obj) },
    { MP_ROM_QSTR(MP_QSTR_launch), MP_ROM_PTR(&windows_launch_obj) },
};
STATIC MP_DEFINE_CONST_DICT(windows_module_globals, windows_module_globals_table);
const mp_obj_module_t windows_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&windows_module_globals,
};
MP_REGISTER_MODULE(MP_QSTR_windows, windows_user_cmodule, 1);
