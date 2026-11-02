/*
 * dsm_core.h - 1-bit delta-sigma modulator for the PicoCalc I2S speaker path.
 *
 * Each input sample becomes 64 output bits (two 32-bit I2S slots), so at a
 * 48 kHz frame rate the pin carries a 3.072 Mbit/s one-bit stream. A
 * second-order modulator pushes the quantisation noise far above hearing,
 * where the PicoCalc's RC filter and the speaker throw it away.
 */
#ifndef DSM_CORE_H
#define DSM_CORE_H
#include <stdint.h>

typedef struct {
	float i1, i2;	/* integrators */
	float y;	/* last output, +1 or -1 */
	float prev;	/* previous input, for interpolation */
} dsm_state;

static inline void dsm_reset(dsm_state *s)
{
	s->i1 = s->i2 = 0.0f;
	s->y = 1.0f;
	s->prev = 0.0f;
}

/* x is in -1..1. Writes 64 bits, first-out bit is the MSB of *w0. */
static inline void dsm_frame(dsm_state *s, float x, uint32_t *w0, uint32_t *w1)
{
	const float lim = 0.88f;	/* tested stable; 0.92+ starts to wobble */
	float step, xi;
	uint32_t a = 0, b = 0;
	int k;

	if (x > lim)
		x = lim;
	else if (x < -lim)
		x = -lim;

	step = (x - s->prev) * (1.0f / 64.0f);
	xi = s->prev;
	for (k = 0; k < 64; k++) {
		uint32_t bit;

		xi += step;
		s->i1 += xi - s->y;
		s->i2 += s->i1 - s->y;
		s->y = (s->i2 >= 0.0f) ? 1.0f : -1.0f;
		bit = s->y > 0.0f;
		if (k < 32)
			a = (a << 1) | bit;
		else
			b = (b << 1) | bit;
	}
	s->prev = x;

	/* If something ever upsets the loop, pull it back instead of screaming. */
	if (s->i2 > 64.0f || s->i2 < -64.0f || s->i1 > 16.0f || s->i1 < -16.0f) {
		s->i1 = 0.0f;
		s->i2 = 0.0f;
	}
	*w0 = a;
	*w1 = b;
}
#endif
