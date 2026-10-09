#include "QcSoundDefinition.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

bool within(double value, double low, double high)
{
    return std::isfinite(value) && value >= low && value <= high;
}

} // namespace

double QcSoundDefinition::seconds() const
{
    double end = 0.0;
    for (const QcSoundLayer &layer : layers) {
        end = std::max(end, layer.start + layer.duration);
    }
    return end;
}

QString QcSoundDefinition::problem() const
{
    if (bus > QcSoundBus::Music) {
        return u"invalid bus"_s;
    }
    if (!within(volume, 0.0, 1.0) || !within(pitchVariation, 0.0, 0.5) || !within(range, 1.0, maxRange)) {
        return u"'volume' (0..1), 'pitchVariation' (0..0.5) or 'range' (1..%1) out of range"_s.arg(maxRange);
    }
    if (variants < 1 || variants > maxVariants) {
        return u"'variants' must be within 1..%1"_s.arg(maxVariants);
    }
    if (layers.isEmpty() || layers.size() > maxLayers) {
        return u"'layers' needs 1 to %1 layers"_s.arg(maxLayers);
    }

    for (const QcSoundLayer &layer : layers) {
        const bool filtersOk = (layer.lowpass == 0.0 || within(layer.lowpass, minFrequency, maxFrequency))
                               && (layer.highpass == 0.0 || within(layer.highpass, minFrequency, maxFrequency));
        const bool sweepOk = layer.endFrequency == 0.0 || within(layer.endFrequency, minFrequency, maxFrequency);
        if (layer.wave > QcSoundWave::Crackle || !within(layer.frequency, minFrequency, maxFrequency) || !sweepOk
            || !within(layer.start, 0.0, maxLayerStart) || !within(layer.duration, 0.001, maxLayerSeconds)
            || !within(layer.attack, 0.0, layer.duration) || !within(layer.curve, 0.0, 8.0) || !within(layer.gain, 0.0, 1.0)
            || !filtersOk || !within(layer.tremoloRate, 0.0, 50.0) || !within(layer.tremoloDepth, 0.0, 1.0)
            || !within(layer.density, 1.0, 5000.0)) {
            return u"a layer value is out of range"_s;
        }
    }
    return {};
}
