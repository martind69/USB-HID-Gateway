/* Target/host harness contract. Requires cJSON and a writable temporary directory. */
#include <assert.h>
#include <math.h>
#include <string.h>
#include "motion_profile_storage.h"
static void compare(float a,float b){assert(fabsf(a-b)<=1e-6f);}
void verify_profiles(const motion_profile_t*a,const motion_profile_t*b){assert(a->schema_version==b->schema_version);assert(strcmp(a->id,b->id)==0);for(unsigned i=0;i<8;i++){assert(a->channel[i].adc_channel==b->channel[i].adc_channel);assert(a->channel[i].dac_channel==b->channel[i].dac_channel);}for(unsigned i=0;i<360;i++){compare(a->left_stick.radial_envelope[i],b->left_stick.radial_envelope[i]);compare(a->right_stick.radial_envelope[i],b->right_stick.radial_envelope[i]);}for(unsigned i=0;i<MOTION_MODEL_CELLS;i++){assert(a->learned.cell[i].samples==b->learned.cell[i].samples);compare(a->learned.cell[i].v_sum,b->learned.cell[i].v_sum);compare(a->learned.cell[i].a_sum,b->learned.cell[i].a_sum);compare(a->learned.cell[i].j_sum,b->learned.cell[i].j_sum);}}
