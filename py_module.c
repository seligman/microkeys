#include "py/runtime.h"

/* ----- keys.add_ints ----------------------------------------------------- */
// This is just here as a test/demo function, in the long term it should be removed
STATIC mp_obj_t keys_add_ints(mp_obj_t a_obj, mp_obj_t b_obj) {
    int a = mp_obj_get_int(a_obj);
    int b = mp_obj_get_int(b_obj);

    return mp_obj_new_int(a + b);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(keys_add_ints_obj, keys_add_ints);

/* ----- keys.press -------------------------------------------------------- */
void keys_press_invoke(const char*);
STATIC mp_obj_t keys_press(mp_obj_t a_obj) {
    keys_press_invoke(mp_obj_str_get_str(a_obj));
    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(keys_press_obj, keys_press);

/* ----- key_internal ------------------------------------------------------ */
// This is the internal helper that's called back after the decorator is declared
// I just needs to store the function and turn around and return it
// Note that there's only one data blob from keys.key, since decorators
// are processed in a single thread (well, everything is), so we just need to
// know what the last one was, not all of them.
int last_func_key_vk = 0;
void key_press_store_fun(int vk, void* fun);
mp_obj_t key_internal(mp_obj_t self_in, mp_uint_t n_args, mp_uint_t n_kw, const mp_obj_t* args) {
    key_press_store_fun(last_func_key_vk, args[0]);
    return args[0];
}

/* ----- keys.key ---------------------------------------------------------- */
STATIC mp_obj_t keys_key(mp_obj_t a_obj) {
    last_func_key_vk = mp_obj_get_int(a_obj);
    return mp_obj_new_fun_native(MP_OBJ_NULL, MP_OBJ_NULL, key_internal, m_new0(mp_uint_t, 1));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(keys_key_obj, keys_key);

/* ----- Connection Glue --------------------------------------------------- */
STATIC const mp_rom_map_elem_t keys_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_keys) },
    { MP_ROM_QSTR(MP_QSTR_add_ints), MP_ROM_PTR(&keys_add_ints_obj) },
    { MP_ROM_QSTR(MP_QSTR_press), MP_ROM_PTR(&keys_press_obj) },
    { MP_ROM_QSTR(MP_QSTR_key), MP_ROM_PTR(&keys_key_obj) },

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
    { MP_ROM_QSTR(MP_QSTR_KEY_ALT), MP_ROM_INT(0x100) },
    { MP_ROM_QSTR(MP_QSTR_KEY_B), MP_ROM_INT(0x42) },
    { MP_ROM_QSTR(MP_QSTR_KEY_BACK), MP_ROM_INT(0x08) },
    { MP_ROM_QSTR(MP_QSTR_KEY_C), MP_ROM_INT(0x43) },
    { MP_ROM_QSTR(MP_QSTR_KEY_CLEAR), MP_ROM_INT(0x0C) },
    { MP_ROM_QSTR(MP_QSTR_KEY_CONTROL), MP_ROM_INT(0x200) },
    { MP_ROM_QSTR(MP_QSTR_KEY_CTRL), MP_ROM_INT(0x200) },
    { MP_ROM_QSTR(MP_QSTR_KEY_D), MP_ROM_INT(0x44) },
    { MP_ROM_QSTR(MP_QSTR_KEY_DEL), MP_ROM_INT(0x2E) },
    { MP_ROM_QSTR(MP_QSTR_KEY_DELETE), MP_ROM_INT(0x2E) },
    { MP_ROM_QSTR(MP_QSTR_KEY_DOWN), MP_ROM_INT(0x28) },
    { MP_ROM_QSTR(MP_QSTR_KEY_E), MP_ROM_INT(0x45) },
    { MP_ROM_QSTR(MP_QSTR_KEY_END), MP_ROM_INT(0x23) },
    { MP_ROM_QSTR(MP_QSTR_KEY_ENTER), MP_ROM_INT(0x0D) },
    { MP_ROM_QSTR(MP_QSTR_KEY_ESC), MP_ROM_INT(0x1B) },
    { MP_ROM_QSTR(MP_QSTR_KEY_ESCAPE), MP_ROM_INT(0x1B) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F), MP_ROM_INT(0x46) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F1), MP_ROM_INT(0x70) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F10), MP_ROM_INT(0x79) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F11), MP_ROM_INT(0x7A) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F12), MP_ROM_INT(0x7B) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F2), MP_ROM_INT(0x71) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F3), MP_ROM_INT(0x72) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F4), MP_ROM_INT(0x73) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F5), MP_ROM_INT(0x74) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F6), MP_ROM_INT(0x75) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F7), MP_ROM_INT(0x76) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F8), MP_ROM_INT(0x77) },
    { MP_ROM_QSTR(MP_QSTR_KEY_F9), MP_ROM_INT(0x78) },
    { MP_ROM_QSTR(MP_QSTR_KEY_G), MP_ROM_INT(0x47) },
    { MP_ROM_QSTR(MP_QSTR_KEY_H), MP_ROM_INT(0x48) },
    { MP_ROM_QSTR(MP_QSTR_KEY_HOME), MP_ROM_INT(0x24) },
    { MP_ROM_QSTR(MP_QSTR_KEY_I), MP_ROM_INT(0x49) },
    { MP_ROM_QSTR(MP_QSTR_KEY_INS), MP_ROM_INT(0x2D) },
    { MP_ROM_QSTR(MP_QSTR_KEY_INSERT), MP_ROM_INT(0x2D) },
    { MP_ROM_QSTR(MP_QSTR_KEY_J), MP_ROM_INT(0x4A) },
    { MP_ROM_QSTR(MP_QSTR_KEY_K), MP_ROM_INT(0x4B) },
    { MP_ROM_QSTR(MP_QSTR_KEY_L), MP_ROM_INT(0x4C) },
    { MP_ROM_QSTR(MP_QSTR_KEY_LEFT), MP_ROM_INT(0x25) },
    { MP_ROM_QSTR(MP_QSTR_KEY_M), MP_ROM_INT(0x4D) },
    { MP_ROM_QSTR(MP_QSTR_KEY_N), MP_ROM_INT(0x4E) },
    { MP_ROM_QSTR(MP_QSTR_KEY_O), MP_ROM_INT(0x4F) },
    { MP_ROM_QSTR(MP_QSTR_KEY_P), MP_ROM_INT(0x50) },
    { MP_ROM_QSTR(MP_QSTR_KEY_Q), MP_ROM_INT(0x51) },
    { MP_ROM_QSTR(MP_QSTR_KEY_R), MP_ROM_INT(0x52) },
    { MP_ROM_QSTR(MP_QSTR_KEY_RETURN), MP_ROM_INT(0x0D) },
    { MP_ROM_QSTR(MP_QSTR_KEY_RIGHT), MP_ROM_INT(0x27) },
    { MP_ROM_QSTR(MP_QSTR_KEY_S), MP_ROM_INT(0x53) },
    { MP_ROM_QSTR(MP_QSTR_KEY_SHIFT), MP_ROM_INT(0x400) },
    { MP_ROM_QSTR(MP_QSTR_KEY_SPACE), MP_ROM_INT(0x20) },
    { MP_ROM_QSTR(MP_QSTR_KEY_T), MP_ROM_INT(0x54) },
    { MP_ROM_QSTR(MP_QSTR_KEY_TAB), MP_ROM_INT(0x09) },
    { MP_ROM_QSTR(MP_QSTR_KEY_U), MP_ROM_INT(0x55) },
    { MP_ROM_QSTR(MP_QSTR_KEY_UP), MP_ROM_INT(0x26) },
    { MP_ROM_QSTR(MP_QSTR_KEY_V), MP_ROM_INT(0x56) },
    { MP_ROM_QSTR(MP_QSTR_KEY_W), MP_ROM_INT(0x57) },
    { MP_ROM_QSTR(MP_QSTR_KEY_WIN), MP_ROM_INT(0x800) },
    { MP_ROM_QSTR(MP_QSTR_KEY_WINDOWS), MP_ROM_INT(0x800) },
    { MP_ROM_QSTR(MP_QSTR_KEY_X), MP_ROM_INT(0x58) },
    { MP_ROM_QSTR(MP_QSTR_KEY_Y), MP_ROM_INT(0x59) },
    { MP_ROM_QSTR(MP_QSTR_KEY_Z), MP_ROM_INT(0x5A) },
};
STATIC MP_DEFINE_CONST_DICT(keys_module_globals, keys_module_globals_table);
const mp_obj_module_t keys_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&keys_module_globals,
};
MP_REGISTER_MODULE(MP_QSTR_keys, keys_user_cmodule, 1);
