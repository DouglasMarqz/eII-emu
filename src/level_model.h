#pragma once
// Common analog level model and output stage for every VCF/VCA model
// (docs/OUTPUT_STAGE.md). Hardware first: voltages follow the EII service
// sheets and the parts' datasheets. Host full scale is a documented choice.
#include <algorithm>
#include <cmath>

namespace eii::analog {
// Soft output-swing limit: linear within 1% up to 0.88 x swing, never beyond it.
// Datasheets give only the swing; the knee shape is a modelling choice.
inline double opampSwing(double y,double swing)noexcept{
    if(!(swing>0))return y;
    const double r=y/swing,r2=r*r,r4=r2*r2,r8=r4*r4;
    return y/std::pow(1+r8*r8,1.0/16);
}
// TI TL08x (SLOS081O, table 5.8, PDF page 14): VOM +/-13.5 V typ, RL = 10k,
// VS = +/-15 V. IC73, the channel deglitch integrator on sheet 7-45, is a TL084.
inline constexpr double tl084SwingVolts=13.5;
// AMI S3528B (Feb 1993, PDF2 = page 45, VDD-VSS = 10 V): maximum input signal
// and output signal level 2.1 Vrms typ (2.97 V peak). The S3528 runs from
// +5VA/-5V on sheet 7-45; its SIG/SIGFB input amplifier (R66/R58 stage) and its
// BUFOUT amplifier (R154 4.7k || C161 1 nF, the VCA current-to-voltage stage
// that drives CnOUT.V) are both limited to this swing.
inline constexpr double s3528SwingVolts=2.9698484809834995;
// Motorola MC34004 (DL128 1983, p. 3-198 = PDF275): VO +/-14 V typ at RL >= 10k,
// +/-13 V typ at RL >= 2k, VS = +/-15 V. IC96 (sheet 7-52) sees about 5 k
// (feedback pot + R115 1k into a line input), so +/-13.5 V.
inline constexpr double mc34004SwingVolts=13.5;
// Vendor/legacy VCF outputs are in the vendor "y" scale (SIG IN modelled as a
// 900R/68.9k divider, in-band gain +1). The SSM2045 Figure 1 summing node gives
// pin 8 = -(8.1k/R138 68k) per deglitch volt (sheet 7-45 R138, datasheet PDF1).
inline constexpr double vendorToPin8=-(8100.0/68000.0)/(900.0/68900.0); // -9.1191
// "Hardware DAC + deglitch" OFF = raw digital route: the DAC codes are used
// directly, without the IC73 integration that reconstructs the DPCM data.
// Its level is matched to the hardware route at 1 kHz for a 27.78 kHz channel
// rate: 1/(51 |1 - 50/51 e^-jw|) = -21.17 dB (validation/native-response-071.csv).
inline constexpr double fastRouteMatch=0.08739944952996512;
// Sheet 7-52 mix amplifier IC96: -sum(CnOUT.V) x RT5/R164..R171 (10k), with
// RT5 = 10k log pot as a rheostat in the feedback (left panel sheet 7-5,
// PDF68); C148 470 pF across it. Full volume (RT5 = 10k): gain -1, pole 33.86 kHz.
inline constexpr double mixInputOhms=10000,mixPotOhms=10000,mixFeedbackFarads=470e-12;
// Host full scale: 1 V peak at MIX OUT (J9) at full volume = 0 dBFS, the same
// reference as the sampling input (+INPUT.V 1 V peak = host 0 dBFS).
inline constexpr double hostOutputReference=1.0;

struct LevelModel{
    double pinScale=1;          // vendor-y VCF output -> pin-8 volts (model 2 is already in volts)
    double fastRouteGain=1;     // multiplies the nominal -R34/R26 = -50 raw-digital gain
    double swing=0;             // TL084 swing of IC73 (0 = unlimited)
    double s3528Swing=0;        // S3528 input stage and BUFOUT swing (0 = unlimited)
    double hostReference=1;     // channel output -> host, both S3528 states
    double s3528OnMatch=1;      // historical S3528-only level match
    bool s3528OffInputStage=false; // 7-45 R58/R66 stage also with the S3528 filter bypassed
    bool mixStage=false;        // 7-52 IC96 mix amplifier (inversion, C148 pole, MC34004 rails)
    static constexpr LevelModel hardware(){return {vendorToPin8,fastRouteMatch,tl084SwingVolts,s3528SwingVolts,hostOutputReference,1,true,true};}
    // Level model of ce2e306 / bab6f4a (frozen oracles, provenance only).
    static constexpr LevelModel ce2e306(){return {1,1,0,0,1,4700.0/10000.0,false,false};}
};
// 7-52 IC96 mix amplifier at the voice-bank rate: y = -x * pot/10k through the
// C148 pole (bilinear, prewarped), MC34004 rails. Not used when disabled.
class MixAmplifier{
public:
    explicit MixAmplifier(double rate=96000){configure(rate);}
    void configure(double rate){
        constexpr double pi=3.14159265358979323846;
        const double fc=1/(2*pi*mixPotOhms*mixFeedbackFarads),k=std::tan(pi*std::min(fc,0.49*rate)/rate);
        b_=k/(1+k);a_=(1-k)/(1+k);
    }
    void reset()noexcept{x1_=y1_=0;}
    double process(double sum,double swing)noexcept{
        const double x=-sum*(mixPotOhms/mixInputOhms);
        const double y=b_*(x+x1_)+a_*y1_;x1_=x;y1_=y;
        return opampSwing(y,swing);
    }
private:double b_=1,a_=0,x1_=0,y1_=0;
};
// Stored NativeOutDb trims that a user set before this level model (non-zero)
// are lowered by this amount, so their native-route loudness is unchanged:
// |vendorToPin8| x 1.0 / 0.47 = 19.40 = +25.7571 dB, S3528 ON and OFF.
// Untouched (0 dB) trims follow the new hardware level.
inline constexpr double levelModelMigrationDb=25.757099214099938;
}
