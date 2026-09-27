#include "voice.h"
#include "i18n.h"
#include <plugin-support.h>
#include <obs-module.h>
#include <media-io/audio-io.h>
#include <cmath>
#include <cstring>

static constexpr uint32_t kOutRate = 16000;

VoiceTap::VoiceTap(QObject *parent) : QObject(parent)
{
	timer_.setInterval(100);
	connect(&timer_, &QTimer::timeout, this, &VoiceTap::flush);
}

VoiceTap::~VoiceTap()
{
	detach();
}

QString VoiceTap::pickMic()
{
	// microphones first, then anything that captures input audio
	QString found;
	auto walk = [](void *p, obs_source_t *s) -> bool {
		auto *out = (QString *)p;
		const char *id = obs_source_get_unversioned_id(s);
		if (!id)
			return true;
		QString sid = id;
		if (sid == "wasapi_input_capture" || sid == "coreaudio_input_capture" || sid == "pulse_input_capture" ||
		    sid == "alsa_input_capture") {
			*out = obs_source_get_name(s);
			return false;
		}
		return true;
	};
	obs_enum_sources(walk, &found);
	return found;
}

QString VoiceTap::attach(const QString &sourceName)
{
	detach();
	if (sourceName.isEmpty())
		return tx("no microphone source chosen");
	obs_source_t *s = obs_get_source_by_name(sourceName.toUtf8().constData());
	if (!s)
		return tx("microphone source '%1' not found").arg(sourceName);
	const struct audio_output_info *ai = audio_output_get_info(obs_get_audio());
	inRate_ = ai ? ai->samples_per_sec : 48000;
	inFormat_ = ai ? (int)ai->format : (int)AUDIO_FORMAT_FLOAT_PLANAR;
	phase_ = 0;
	src_ = s;
	weak_ = obs_source_get_weak_source(s);
	name_ = sourceName;
	obs_source_add_audio_capture_callback(src_, &VoiceTap::onAudio, this);
	timer_.start();
	return QString();
}

void VoiceTap::detach()
{
	timer_.stop();
	if (src_) {
		obs_source_remove_audio_capture_callback(src_, &VoiceTap::onAudio, this);
		obs_source_release(src_);
		src_ = nullptr;
	}
	if (weak_) {
		obs_weak_source_release(weak_);
		weak_ = nullptr;
	}
	name_.clear();
	std::lock_guard<std::mutex> lk(mu_);
	pending_.clear();
}

/// Audio thread. Left channel (a microphone is mono, upmixed by OBS), folded down to 16 kHz by
/// linear interpolation: plenty for speech recognition, which works at 16 kHz itself.
void VoiceTap::onAudio(void *param, obs_source_t *, const struct audio_data *data, bool)
{
	auto *self = (VoiceTap *)param;
	if (!data || !data->frames || !data->data[0])
		return;
	const uint32_t n = data->frames;
	auto sample = [&](uint32_t i) -> float {
		switch (self->inFormat_) {
		case AUDIO_FORMAT_FLOAT_PLANAR:
			return ((const float *)data->data[0])[i];
		case AUDIO_FORMAT_FLOAT:
			return ((const float *)data->data[0])[i * 2];
		case AUDIO_FORMAT_16BIT_PLANAR:
			return ((const int16_t *)data->data[0])[i] / 32768.0f;
		case AUDIO_FORMAT_16BIT:
			return ((const int16_t *)data->data[0])[i * 2] / 32768.0f;
		case AUDIO_FORMAT_32BIT_PLANAR:
			return ((const int32_t *)data->data[0])[i] / 2147483648.0f;
		default:
			return 0.0f;
		}
	};
	const double step = (double)self->inRate_ / kOutRate;
	std::vector<int16_t> out;
	out.reserve((size_t)(n / step) + 2);
	double pos = self->phase_;
	while (pos + 1 < n) {
		uint32_t i = (uint32_t)pos;
		float frac = (float)(pos - i);
		float v = sample(i) * (1 - frac) + sample(i + 1) * frac;
		v = std::max(-1.0f, std::min(1.0f, v));
		out.push_back((int16_t)std::lround(v * 32767.0f));
		pos += step;
	}
	self->phase_ = pos - n;
	std::lock_guard<std::mutex> lk(self->mu_);
	if (self->pending_.size() < kOutRate * 10) // never more than 10 s backed up
		self->pending_.insert(self->pending_.end(), out.begin(), out.end());
}

void VoiceTap::flush()
{
	std::vector<int16_t> take;
	{
		std::lock_guard<std::mutex> lk(mu_);
		take.swap(pending_);
	}
	if (take.empty()) {
		// the source may have been removed and re-made (a scene collection switch): re-attach by name
		if (weak_ && obs_weak_source_expired(weak_)) {
			QString n = name_;
			obs_log(LOG_INFO, "[voice] microphone source '%s' went away; looking for it again",
				n.toUtf8().constData());
			attach(n);
		}
		return;
	}
	QByteArray bytes((const char *)take.data(), (int)(take.size() * sizeof(int16_t)));
	emit pcm(bytes);
}
