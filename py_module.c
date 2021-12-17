#include "py/runtime.h"

STATIC mp_obj_t keys_add_ints(mp_obj_t a_obj, mp_obj_t b_obj) {
    int a = mp_obj_get_int(a_obj);
    int b = mp_obj_get_int(b_obj);

    return mp_obj_new_int(a + b);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(keys_add_ints_obj, keys_add_ints);

void keys_press_invoke(const char*);
STATIC mp_obj_t keys_press(mp_obj_t a_obj) {
    keys_press_invoke(mp_obj_str_get_str(a_obj));
    return MP_ROM_NONE;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(keys_press_obj, keys_press);


STATIC const mp_rom_map_elem_t keys_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_keys) },
    { MP_ROM_QSTR(MP_QSTR_add_ints), MP_ROM_PTR(&keys_add_ints_obj) },
    { MP_ROM_QSTR(MP_QSTR_press), MP_ROM_PTR(&keys_press_obj) },
};
STATIC MP_DEFINE_CONST_DICT(keys_module_globals, keys_module_globals_table);

const mp_obj_module_t keys_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&keys_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_keys, keys_user_cmodule, 1);
