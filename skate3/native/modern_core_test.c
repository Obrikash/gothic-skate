#include "modern_core.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
static void close_to(float a,float b) { assert(fabsf(a-b)<0.0001f); }
int main(void) {
    /* Eight movement directions at 72 camera headings, both host handednesses. */
    for(int side=-1;side<=1;side+=2) for(int h=0;h<360;h+=5) {
        float yaw=h*0.01745329251994f;
        ModernVec f=modern_forward(yaw),r=modern_right(f,(float)side);
        for(int u=-1;u<=1;++u) for(int d=-1;d<=1;++d) if(u||d) {
            ModernVec want=modern_direction(f,(float)side,u,d);
            close_to(modern_length(want),1);
            float norm=hypotf((float)u,(float)d);
            close_to(want.x*f.x+want.z*f.z,u/norm);
            close_to(want.x*r.x+want.z*r.z,d/norm);
        }
        /* Orbit does not change an idle actor's heading: only the movement path calls turn. */
        ModernVec backwards=modern_scale(f,-1);
        ModernVec out=modern_turn(f,backwards,720,1.0f/60);
        close_to(fabsf(modern_angle(modern_yaw(out)-yaw)),12*0.01745329251994f);
    }
    /* Turn rate and seam behavior are independent of render frame rate. */
    for(int fps=30;fps<=240;fps*=2) {
        ModernVec f={0,0,1},target={1,0,0};
        for(int i=0;i<fps;++i) f=modern_turn(f,target,90,1.0f/fps);
        close_to(f.x,1); close_to(f.z,0);
    }
    ModernVec seam=modern_turn(modern_forward(179*0.01745329251994f),modern_forward(-179*0.01745329251994f),90,0.05f);
    close_to(modern_angle(modern_yaw(seam)+179*0.01745329251994f),0);
    for(int p=-35;p<=60;++p) close_to(modern_length(modern_look(2.8f,(float)p)),1);
    /* Reported ship-area world offset: three very different coordinate axes. */
    float bounds[6]={-39830,-1888,-154270,-39790,-1700,-154230};
    ModernVec focus=modern_target((ModernVec){-39808,-1790,-154247},bounds,135);
    close_to(focus.y,-1753); close_to(focus.x,-39808); close_to(focus.z,-154247);
    bounds[1]=NAN;
    close_to(modern_target((ModernVec){0,500,0},bounds,135).y,545);
    /* Drawing melee weapons and hit/parry states retain the modern camera,
       while Gothic still owns combat inputs and Free Aim owns ranged modes. */
    for(int weapon=0;weapon<=7;++weapon) {
        assert(modern_body_allowed(0,weapon,0)==(weapon<=4));
        assert(modern_body_allowed(0,weapon,1)==(weapon==0));
        assert(modern_body_allowed(26,weapon,0)==(weapon<=4));
        assert(modern_body_allowed(27,weapon,0)==(weapon<=4));
        assert(!modern_body_allowed(26,weapon,1));
        for(unsigned state=11;state<=25;++state) assert(!modern_body_allowed(state,weapon,0));
        assert(!modern_body_allowed(0x800,weapon,0));
    }
    puts("Modern controls: 1,152 directions, turn rates, yaw seam, camera basis/focus, and melee/ranged/interaction ownership passed.");
    return 0;
}
