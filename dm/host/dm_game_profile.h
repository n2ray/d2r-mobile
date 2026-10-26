// 宿主逐游戏配置；Android 用构建期生成值，现有苹果构建保留 D2R 默认配置。
// Host game configuration: Android generates it at build time; existing Apple builds retain the D2R profile.
#pragma once
#ifdef DM_GAME_PROFILE_SELECTED
#include "dm_selected_game.h"
#else
#include "../games/0100726014352000/game_profile.h"
#endif
