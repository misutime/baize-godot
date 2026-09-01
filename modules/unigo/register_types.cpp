/**************************************************************************/
/*  register_types.cpp                                                    */
/**************************************************************************/
/*  UniGo C ABI 模块注册实现(空实现)。本模块不注册任何 Godot 类,         */
/*  仅提供 initialize/uninitialize 空函数以满足构建系统约定。             */
/*  (注意:register_types 生成器按约定查找 initialize_<module>_module。)  */
/**************************************************************************/

#include "register_types.h"

void initialize_unigo_module(ModuleInitializationLevel p_level) {
	/* UniGo C ABI 不依赖 ClassDB 注册,无初始化动作。 */
}

void uninitialize_unigo_module(ModuleInitializationLevel p_level) {
	/* 同上,无清理动作。 */
}
