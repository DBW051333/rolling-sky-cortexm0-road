/* Run the actual ADC sampling, calibration and steering with injected pins.
 * gcc -O2 -ffunction-sections -fdata-sections tools/verify_adc.c
 *     '-Wl,--gc-sections' -o tmp/verify_adc.exe */
#include <stdint.h>
static uint32_t pins[7];
#define ADC_LEVEL(ch) pins[(ch)-1u]
#define HUD_HOST_TEST
#include "../firmware/main.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void sample(unsigned value) {
    unsigned i;
    pins[2]=value;
    for(i=0;i<64;i++) { adc_poll(); adc_select(); }
}
static void reset(void) {
    memset(pins,0,sizeof pins);
    adc_channel=adc_live=adc_primed=adc_manual=adc_calibrated=0;
    road.camera_x_q8=ROAD_CAMERA_CENTER;
}
int main(void) {
    unsigned i,last=0;
    reset();
    sample(600); adc_calibrate();
    assert(!adc_calibrated); /* A static/noisy pin is not a calibration. */
    sample(1400);
    assert(adc_channel==2 && adc_live);
    adc_steer(&road,1000);
    assert(road.camera_x_q8==ROAD_CAMERA_CENTER); /* Partial first sweep. */
    sample(3000); sample(600); adc_calibrate();
    assert(adc_calibrated && adc_low[2]==600 && adc_high[2]>=2997);
    for(i=600;i<=3000;i+=120) {
        unsigned expected;
        sample(i);
        adc_steer(&road,1000);
        expected=1000u-hud_divide((i-600u)*1000u,2400u);
        assert(adc_position()<=1000);
        assert((int)(1000-adc_position())-(int)expected>=-3);
        assert((int)(1000-adc_position())-(int)expected<=3);
        assert(road.camera_x_q8<=ROAD_CAMERA_MAX);
        assert(road.camera_x_q8>=ROAD_CAMERA_MIN);
        assert(adc_position()>=last); last=adc_position();
        if(i==1800) assert(road.camera_x_q8>630 && road.camera_x_q8<650);
    }
    /* Calibration and channel must not drift, even past either endpoint or
     * when an unrelated pin has a bigger swing during gameplay. */
    pins[5]=4095; sample(4095);
    assert(adc_channel==2 && adc_position()==1000);
    sample(0); assert(adc_position()==0);
    assert(adc_low[2]==600 && adc_high[2]>=2997 && adc_high[2]<=3000);
    adc_steer(&road,1000); assert(road.camera_x_q8==ROAD_CAMERA_MAX);
    sample(3000); adc_steer(&road,1000); assert(road.camera_x_q8==ROAD_CAMERA_MIN);
    sample(1800); adc_steer(&road,33);
    assert(road.camera_x_q8<=ROAD_CAMERA_MIN+99); /* Same slew limit. */
    road.camera_x_q8=640; adc_level[2]=1800; adc_steer(&road,33);
    assert(road.camera_x_q8==640); /* Noise deadband. */
    adc_calibrate(); assert(!adc_calibrated && !adc_live && !adc_primed);
    sample(1000); sample(2600); adc_calibrate(); assert(adc_calibrated);
    sample(1800); assert(adc_position()>=498 && adc_position()<=502);
    adc_cycle(); assert(!adc_calibrated && !adc_live && !adc_primed && adc_manual);
    /* Either physical sweep direction works. */
    reset(); sample(3000); sample(600); adc_calibrate();
    sample(1800); assert(adc_position()>=498 && adc_position()<=502);
    puts("PASS ADC: first sweep gated, frozen endpoints, center/linear travel, endpoint noise clamped, channel lock, recalibration, slew and deadband");
    return 0;
}
