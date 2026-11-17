#include "human_motion_model.h"
void human_motion_model_init(human_motion_model_t*m,const learned_motion_model_t*l){m->learned=l;motion_model_tuning_defaults(&m->tuning);}motion_limits_t human_motion_model_limits(const human_motion_model_t*m,motion_vec2_t d,float a){return motion_model_tuner_apply(learned_motion_model_query(m->learned,d,a),&m->tuning);}
