#ifndef STRS_H
#define STRS_H

#include <QDir>
namespace strings{
    inline constexpr auto app_key = "supreme.vrdio";
    inline constexpr auto overlay_friendly_name = "Audio Control";
    inline auto config_dir_loc = QDir::homePath() + "/.config/vrdio";
    inline auto vrmanifest_loc = config_dir_loc + "/vrdio.vrmanifest";
    inline auto audioconfig_loc = config_dir_loc + "/audioconfig.txt";

}

#endif
