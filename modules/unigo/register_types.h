/**************************************************************************/
/*  register_types.h                                                      */
/**************************************************************************/
/*  UniGo C ABI 模块注册头。本模块无 Godot 类,注册函数为空实现,         */
/* 仅满足 Godot 构建系统的模块注册约定(register_module_types.gen.cpp)。  */
/**************************************************************************/

#pragma once

#include "modules/register_module_types.h"

void initialize_unigo_module(ModuleInitializationLevel p_level);
void uninitialize_unigo_module(ModuleInitializationLevel p_level);
