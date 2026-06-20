/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>

#include <oboe/Oboe.h>

#include "snd_backend.h"

extern "C" void Cbuf_AddText(const char *text);
extern "C" void Com_Printf(const char *fmt, ...);

namespace {

class EzquakeOboeCallback final : public oboe::AudioStreamCallback {
public:
	oboe::DataCallbackResult onAudioReady(oboe::AudioStream *stream, void *audio_data, int32_t num_frames) override
	{
		const int32_t channels = stream ? stream->getChannelCount() : 2;
		const int len = static_cast<int>(num_frames * channels * static_cast<int32_t>(sizeof(int16_t)));

		S_MixOutput(static_cast<byte *>(audio_data), len, 0);
		return oboe::DataCallbackResult::Continue;
	}

	void onErrorAfterClose(oboe::AudioStream *stream, oboe::Result error) override
	{
		(void)stream;
		last_error.store(static_cast<int>(error), std::memory_order_relaxed);
		restart_requested.store(true, std::memory_order_release);
	}

	std::atomic<bool> restart_requested{false};
	std::atomic<int> last_error{static_cast<int>(oboe::Result::OK)};
};

EzquakeOboeCallback oboe_callback;
std::shared_ptr<oboe::AudioStream> oboe_stream;
int oboe_frames_per_burst;
int oboe_buffer_size;
int oboe_buffer_capacity;

int ClampInt(int value, int min_value, int max_value)
{
	if (value < min_value) {
		return min_value;
	}
	if (value > max_value) {
		return max_value;
	}
	return value;
}

int PreferredBufferFrames(oboe::AudioStream *stream)
{
	const int frames_per_burst = stream->getFramesPerBurst();
	const int capacity = stream->getBufferCapacityInFrames();
	int requested = S_Backend_DesiredBufferSamples();

	if (requested <= 0) {
		requested = frames_per_burst > 0 ? frames_per_burst * 2 : 512;
	}

	if (capacity > 0) {
		requested = ClampInt(requested, frames_per_burst > 0 ? frames_per_burst : 1, capacity);
	}

	return requested;
}

void TuneBufferSize(oboe::AudioStream *stream)
{
	const int requested = PreferredBufferFrames(stream);
	oboe::ResultWithValue<int32_t> result = stream->setBufferSizeInFrames(requested);

	oboe_frames_per_burst = stream->getFramesPerBurst();
	oboe_buffer_capacity = stream->getBufferCapacityInFrames();
	if (result.error() == oboe::Result::OK) {
		oboe_buffer_size = result.value();
	}
	else {
		oboe_buffer_size = stream->getBufferSizeInFrames();
	}

	S_Backend_SetActualBufferSamples(oboe_buffer_size);
}

oboe::Result OpenStream(oboe::SharingMode sharing_mode)
{
	std::shared_ptr<oboe::AudioStream> stream;
	const int sample_rate = S_SampleRateFromKHzCvar();
	oboe::AudioStreamBuilder builder;

	builder.setDirection(oboe::Direction::Output);
	builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
	builder.setSharingMode(sharing_mode);
	builder.setUsage(oboe::Usage::Game);
	builder.setFormat(oboe::AudioFormat::I16);
	builder.setChannelCount(2);
	builder.setSampleRate(sample_rate);
	builder.setDataCallback(&oboe_callback);
	builder.setErrorCallback(&oboe_callback);
	if (sample_rate != 48000) {
		builder.setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium);
	}

	oboe::Result result = builder.openStream(stream);
	if (result == oboe::Result::OK) {
		oboe_stream = stream;
	}
	return result;
}

} // namespace

extern "C" void S_Backend_Update(void)
{
	if (oboe_callback.restart_requested.exchange(false, std::memory_order_acq_rel)) {
		const oboe::Result error = static_cast<oboe::Result>(oboe_callback.last_error.load(std::memory_order_relaxed));

		Com_Printf("Oboe audio stream error after close: %s; restarting sound\n", oboe::convertToText(error));
		Cbuf_AddText("s_restart\n");
	}
}

extern "C" void S_Backend_ListDrivers(void)
{
	Com_Printf("Audio backend: Oboe (AAudio/OpenSL ES)\n");
}

extern "C" void S_Backend_ListAudioDevices(void)
{
	Com_Printf("Playback devices:\n");
	Com_Printf(" id  device name\n-------------------------\n");
	Com_Printf("  0  system default (Oboe)\n");
	Com_Printf("\nInput devices:\n");
	Com_Printf(" id  device name\n-------------------------\n");
	Com_Printf("  0  local capture deferred on Android\n");
}

extern "C" void S_Backend_PrintSoundInfo(void)
{
	if (!oboe_stream) {
		Com_Printf("backend: Oboe (stopped)\n");
		return;
	}

	Com_Printf("backend: Oboe (%s, %s sharing)\n",
			oboe::convertToText(oboe_stream->getAudioApi()),
			oboe::convertToText(oboe_stream->getSharingMode()));
	Com_Printf("%5d frames per burst\n", oboe_frames_per_burst);
	Com_Printf("%5d buffer frames\n", oboe_buffer_size);
	Com_Printf("%5d buffer capacity\n", oboe_buffer_capacity);
}

extern "C" void S_Backend_Shutdown(void)
{
	Com_Printf("Shutting down Oboe audio.\n");

	oboe_callback.restart_requested.store(false, std::memory_order_release);

	if (oboe_stream) {
		oboe_stream->requestStop();
		oboe_stream->close();
		oboe_stream.reset();
	}

	S_MixerLock_Shutdown();
	S_Backend_FreeSoundHardware();

	oboe_frames_per_burst = 0;
	oboe_buffer_size = 0;
	oboe_buffer_capacity = 0;
}

extern "C" qbool S_Backend_Init(void)
{
	oboe::Result result;

	S_MixerLock_Init();

	result = OpenStream(oboe::SharingMode::Exclusive);
	if (result != oboe::Result::OK) {
		Com_Printf("sound: couldn't open exclusive Oboe stream: %s\n", oboe::convertToText(result));
		Com_Printf("sound: retrying Oboe stream in shared mode\n");
		result = OpenStream(oboe::SharingMode::Shared);
	}

	if (result != oboe::Result::OK) {
		Com_Printf("sound: couldn't open Oboe stream: %s\n", oboe::convertToText(result));
		goto fail;
	}

	if (oboe_stream->getFormat() != oboe::AudioFormat::I16) {
		Com_Printf("Oboe audio format %s unsupported.\n", oboe::convertToText(oboe_stream->getFormat()));
		goto fail;
	}

	if (oboe_stream->getChannelCount() != 2) {
		Com_Printf("Oboe audio channels %d unsupported.\n", oboe_stream->getChannelCount());
		goto fail;
	}

	TuneBufferSize(oboe_stream.get());

	if (!S_Backend_AllocSoundHardware(oboe_stream->getSampleRate(), oboe_stream->getChannelCount(), 16)) {
		goto fail;
	}

	result = oboe_stream->requestStart();
	if (result != oboe::Result::OK) {
		Com_Printf("sound: couldn't start Oboe stream: %s\n", oboe::convertToText(result));
		goto fail;
	}

	Com_Printf("Using Oboe audio backend: %s @ %d Hz\n",
			oboe::convertToText(oboe_stream->getAudioApi()),
			oboe_stream->getSampleRate());

	return 1;

fail:
	S_Backend_Shutdown();
	return 0;
}
