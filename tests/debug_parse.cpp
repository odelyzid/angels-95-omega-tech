#include "../Source/Script/LightningScriptParser.hpp"
#include <cstdio>
int main() {
    std::string ozls = R"(entity "snowy_zone" : skyzone {
        fog_color = (0.8, 0.85, 0.9)
        fog_density = 0.015
        ambient_light = (0.6, 0.6, 0.7)
        skybox = "skybox_snowy"
        actions {
            on_enter {
                set_fog 0.8 0.85 0.9 0.015
                set_skybox "skybox_snowy"
            }
            on_exit {
                set_fog 0.5 0.5 0.5 0.002
                restore_skybox
            }
        }
    })";
    EntityDef def = LightningScriptParser::Parse(ozls, "test.ozls");
    printf("name='%s' type=%d\n", def.name.c_str(), (int)def.type);
    printf("skybox='%s' music='%s'\n", def.skybox.c_str(), def.music.c_str());
    printf("fog_density=%.3f\n", def.stats.floats.count("fog_density") ? def.stats.floats.at("fog_density") : -1.0f);
    printf("fog_color=(%.2f,%.2f,%.2f)\n", 
        def.stats.vec3s.count("fog_color") ? def.stats.vec3s.at("fog_color")[0] : 0,
        def.stats.vec3s.count("fog_color") ? def.stats.vec3s.at("fog_color")[1] : 0,
        def.stats.vec3s.count("fog_color") ? def.stats.vec3s.at("fog_color")[2] : 0);
    printf("actions=%zu\n", def.actions.size());
    for (size_t i = 0; i < def.actions.size(); i++)
        printf("  action[%zu]: name='%s' lines=%zu\n", i, def.actions[i].name.c_str(), def.actions[i].scriptLines.size());
    printf("action[0].scriptLines:\n");
    for (auto& l : def.actions[0].scriptLines)
        printf("    '%s'\n", l.c_str());
    return 0;
}
