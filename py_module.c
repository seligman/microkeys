#include "py/runtime.h"

// keys.add_ints
STATIC mp_obj_t keys_add_ints(mp_obj_t a_obj, mp_obj_t b_obj) {
    int a = mp_obj_get_int(a_obj);
    int b = mp_obj_get_int(b_obj);

    return mp_obj_new_int(a + b);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(keys_add_ints_obj, keys_add_ints);

// keys.press
void keys_press_invoke(const char*);
STATIC mp_obj_t keys_press(mp_obj_t a_obj) {
    keys_press_invoke(mp_obj_str_get_str(a_obj));
    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(keys_press_obj, keys_press);

// keys.key_internal
const char* last_func = NULL;
void key_press_store_fun(const char* desc, void* fun);
mp_obj_t keys_key_internal(mp_obj_t self_in, mp_uint_t n_args, mp_uint_t n_kw, const mp_obj_t* args) {
    //const char* desc = mp_obj_str_get_str(a_obj);
    key_press_store_fun(last_func, args[0]);
    return args[0];
}

// keys.key
STATIC mp_obj_t keys_key(mp_obj_t a_obj) {
    last_func = mp_obj_str_get_str(a_obj);
    return mp_obj_new_fun_native(MP_OBJ_NULL, MP_OBJ_NULL, keys_key_internal, m_new0(mp_uint_t, 1));
    //return MP_ROM_PTR(&keys_key_internal_obj);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(keys_key_obj, keys_key);

STATIC const mp_rom_map_elem_t keys_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_keys) },
    { MP_ROM_QSTR(MP_QSTR_add_ints), MP_ROM_PTR(&keys_add_ints_obj) },
    { MP_ROM_QSTR(MP_QSTR_press), MP_ROM_PTR(&keys_press_obj) },
    { MP_ROM_QSTR(MP_QSTR_key), MP_ROM_PTR(&keys_key_obj) },
};
STATIC MP_DEFINE_CONST_DICT(keys_module_globals, keys_module_globals_table);

const mp_obj_module_t keys_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&keys_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_keys, keys_user_cmodule, 1);
