#include "cclaw/include/core/skill.h"
#include <stdio.h>

int main() {
    printf("Testing skill system...\n");
    
    // Initialize skill registry
    err_t err = skill_registry_init();
    if (err != ERR_OK) {
        printf("Failed to initialize skill registry\n");
        return 1;
    }
    
    printf("Skill registry initialized\n");
    
    // Shutdown skill registry
    skill_registry_shutdown();
    printf("Skill registry shutdown\n");
    
    return 0;
}