/* Copyright (c) 2017-2019 Dmitry Stepanov a.k.a mr.DIMAS
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
* LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
* OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
* WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. */

typedef enum de_sound_source_status_t {
	DE_SOUND_SOURCE_STATUS_PLAYING,
	DE_SOUND_SOURCE_STATUS_STOPPED,
	DE_SOUND_SOURCE_STATUS_PAUSED
} de_sound_source_status_t;

struct de_sound_source_t {
	de_sound_context_t* ctx;
	de_sound_buffer_t* buffer;
	de_sound_source_status_t status;
	double playback_position;
	double current_sample_rate;
	float pan;
	float pitch;
	float gain;
	/* gain of each channel. currently mono and stereo are supported */
	float channel_gain[DE_SOUND_MAX_CHANNELS];
	/* spatial data */
	de_vec3_t position;
	DE_LINKED_LIST_ITEM(de_sound_source_t);
};

/**
 * @brief Creates new sound source. Thread-safe.
 */
de_sound_source_t* de_sound_source_create(de_sound_context_t* ctx);

/**
 * @brief Frees all resources associated with sound source. Thread-safe.
 */
void de_sound_source_free(de_sound_source_t* src);

/**
 * @brief Sets new sound buffer for sound source. Thread-safe.
 */
void de_sound_source_set_buffer(de_sound_source_t* src, de_sound_buffer_t* buf);

/**
 * @brief Writes out final samples for sound device mixer. 
 * 
 * Note: Left and right channels only! 2.1, 5.1 and so on formats not supported!
 */
void de_sound_source_sample(de_sound_source_t* src, float samples[2]);

/**
 * @brief Sound mixer uses this method to ensure that source should participate in mixing.
 * 
 * Sources without buffers or stopped sources does not produce any samples and can be
 * discarded from mixing process.
 */
bool de_sound_source_can_produce_samples(de_sound_source_t* src);