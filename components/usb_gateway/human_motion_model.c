#include "human_motion_model.h"

void human_motion_model_init(
    human_motion_model_t *model,
    const learned_motion_model_t *learned)
{
    if (model == NULL) {
        return;
    }

    model->learned = learned;
    motion_model_tuning_defaults(&model->tuning);
}

motion_limits_t human_motion_model_limits(
    const human_motion_model_t *model,
    motion_vec2_t direction,
    float magnitude)
{
    if (model == NULL) {
        return (motion_limits_t) { 0 };
    }

    motion_limits_t limits = learned_motion_model_query(
        model->learned,
        direction,
        magnitude);

    return motion_model_tuner_apply(
        limits,
        &model->tuning);
}
