/* Engine-independent camera-relative controls. Gothic units: cm, Y up. */
#ifndef MODERN_CORE_H
#define MODERN_CORE_H
#include <math.h>
typedef struct ModernVec { float x, y, z; } ModernVec;
static inline float modern_clamp(float v, float a, float b) { return fmaxf(a, fminf(v, b)); }
static inline ModernVec modern_add(ModernVec a, ModernVec b) { return (ModernVec){a.x+b.x,a.y+b.y,a.z+b.z}; }
static inline ModernVec modern_scale(ModernVec a, float k) { return (ModernVec){a.x*k,a.y*k,a.z*k}; }
static inline float modern_length(ModernVec a) { return sqrtf(a.x*a.x+a.y*a.y+a.z*a.z); }
static inline ModernVec modern_flat(ModernVec a) {
    float n = hypotf(a.x,a.z);
    return n > 0.0001f ? (ModernVec){a.x/n,0,a.z/n} : (ModernVec){0,0,1};
}
/* side=+1 for a conventional +X right / +Z forward transform; measured from host. */
static inline ModernVec modern_right(ModernVec a, float side) { return (ModernVec){a.z*side,0,-a.x*side}; }
static inline float modern_yaw(ModernVec a) { return atan2f(a.x,a.z); }
static inline float modern_angle(float a) { return atan2f(sinf(a),cosf(a)); }
static inline ModernVec modern_forward(float yaw) { return (ModernVec){sinf(yaw),0,cosf(yaw)}; }
static inline ModernVec modern_direction(ModernVec forward, float side, int up, int right) {
    return modern_flat(modern_add(modern_scale(forward,(float)up),modern_scale(modern_right(forward,side),(float)right)));
}
static inline ModernVec modern_turn(ModernVec at, ModernVec want, float rate, float dt) {
    float error = modern_angle(modern_yaw(want)-modern_yaw(at));
    float step = fmaxf(0,rate)*modern_clamp(dt,0,0.05f)*0.01745329251994f;
    return modern_forward(modern_yaw(at)+modern_clamp(error,-step,step));
}
static inline ModernVec modern_look(float yaw, float pitch) {
    float p = pitch*0.01745329251994f;
    return (ModernVec){sinf(yaw)*cosf(p),-sinf(p),cosf(yaw)*cosf(p)};
}
static inline ModernVec modern_target(ModernVec origin, const float bounds[6], float height) {
    /* zTBBox3D is {mins.xyz,maxs.xyz}; never mistake world X for feet Y. */
    float feet=bounds[1];
    if(!isfinite(feet) || fabsf(feet-origin.y)>300) feet=origin.y-90;
    return (ModernVec){origin.x,feet+height,origin.z};
}
/* Pinned oNpc.h: none/fist/dagger/1HS/2HS = 0..4; bow/crossbow/magic = 5..7.
   Camera ownership is independent of locomotion ownership in melee combat. */
static inline int modern_body_allowed(unsigned body, int weapon, int movement) {
    if(body&~127u || weapon<0 || weapon>4 || (movement&&weapon!=0)) return 0;
    return body<=4 || body==8 || body==10 || (!movement&&(body==26||body==27));
}
#endif
